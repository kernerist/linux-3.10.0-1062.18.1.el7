#include "../perf.h"
#include "util.h"
#include "debug.h"
#include <api/fs/fs.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/time64.h>
#include <unistd.h>
#include "strlist.h"

/*
 * XXX We need to find a better place for these things...
 */

bool perf_singlethreaded = true;

void perf_set_singlethreaded(void)
{
	perf_singlethreaded = true;
}

void perf_set_multithreaded(void)
{
	perf_singlethreaded = false;
}

unsigned int page_size;

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
#define cache_line_size(cacheline_sizep) *cacheline_sizep = sysconf(_SC_LEVEL1_DCACHE_LINESIZE)
#else
static void cache_line_size(int *cacheline_sizep)
{
	if (sysfs__read_int("devices/system/cpu/cpu0/cache/index0/coherency_line_size", cacheline_sizep))
		pr_debug("cannot determine cache line size");
}
#endif

int cacheline_size(void)
{
	static int size;

	if (!size)
		cache_line_size(&size);

	return size;
}

unsigned int sysctl_perf_event_max_stack = PERF_MAX_STACK_DEPTH;

bool test_attr__enabled;

bool perf_host  = true;
bool perf_guest = false;

void event_attr_init(struct perf_event_attr *attr)
{
	if (!perf_host)
		attr->exclude_host  = 1;
	if (!perf_guest)
		attr->exclude_guest = 1;
	/* to capture ABI version */
	attr->size = sizeof(*attr);
}

int mkdir_p(char *path, mode_t mode)
{
	struct stat st;
	int err;
	char *d = path;

	if (*d != '/')
		return -1;

	if (stat(path, &st) == 0)
		return 0;

	while (*++d == '/');

	while ((d = strchr(d, '/'))) {
		*d = '\0';
		err = stat(path, &st) && mkdir(path, mode);
		*d++ = '/';
		if (err)
			return -1;
		while (*d == '/')
			++d;
	}
	return (stat(path, &st) && mkdir(path, mode)) ? -1 : 0;
}

int rm_rf(const char *path)
{
	DIR *dir;
	int ret = 0;
	struct dirent *d;
	char namebuf[PATH_MAX];

	dir = opendir(path);
	if (dir == NULL)
		return 0;

	while ((d = readdir(dir)) != NULL && !ret) {
		struct stat statbuf;

		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		scnprintf(namebuf, sizeof(namebuf), "%s/%s",
			  path, d->d_name);

		/* We have to check symbolic link itself */
		ret = lstat(namebuf, &statbuf);
		if (ret < 0) {
			pr_debug("stat failed: %s\n", namebuf);
			break;
		}

		if (S_ISDIR(statbuf.st_mode))
			ret = rm_rf(namebuf);
		else
			ret = unlink(namebuf);
	}
	closedir(dir);

	if (ret < 0)
		return ret;

	return rmdir(path);
}

/* A filter which removes dot files */
bool lsdir_no_dot_filter(const char *name __maybe_unused, struct dirent *d)
{
	return d->d_name[0] != '.';
}

/* lsdir reads a directory and store it in strlist */
struct strlist *lsdir(const char *name,
		      bool (*filter)(const char *, struct dirent *))
{
	struct strlist *list = NULL;
	DIR *dir;
	struct dirent *d;

	dir = opendir(name);
	if (!dir)
		return NULL;

	list = strlist__new(NULL, NULL);
	if (!list) {
		errno = ENOMEM;
		goto out;
	}

	while ((d = readdir(dir)) != NULL) {
		if (!filter || filter(name, d))
			strlist__add(list, d->d_name);
	}

out:
	closedir(dir);
	return list;
}

static int slow_copyfile(const char *from, const char *to)
{
	int err = -1;
	char *line = NULL;
	size_t n;
	FILE *from_fp = fopen(from, "r"), *to_fp;

	if (from_fp == NULL)
		goto out;

	to_fp = fopen(to, "w");
	if (to_fp == NULL)
		goto out_fclose_from;

	while (getline(&line, &n, from_fp) > 0)
		if (fputs(line, to_fp) == EOF)
			goto out_fclose_to;
	err = 0;
out_fclose_to:
	fclose(to_fp);
	free(line);
out_fclose_from:
	fclose(from_fp);
out:
	return err;
}

int copyfile_offset(int ifd, loff_t off_in, int ofd, loff_t off_out, u64 size)
{
	void *ptr;
	loff_t pgoff;

	pgoff = off_in & ~(page_size - 1);
	off_in -= pgoff;

	ptr = mmap(NULL, off_in + size, PROT_READ, MAP_PRIVATE, ifd, pgoff);
	if (ptr == MAP_FAILED)
		return -1;

	while (size) {
		ssize_t ret = pwrite(ofd, ptr + off_in, size, off_out);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			break;

		size -= ret;
		off_in += ret;
		off_out += ret;
	}
	munmap(ptr, off_in + size);

	return size ? -1 : 0;
}

int copyfile_mode(const char *from, const char *to, mode_t mode)
{
	int fromfd, tofd;
	struct stat st;
	int err = -1;
	char *tmp = NULL, *ptr = NULL;

	if (stat(from, &st))
		goto out;

	/* extra 'x' at the end is to reserve space for '.' */
	if (asprintf(&tmp, "%s.XXXXXXx", to) < 0) {
		tmp = NULL;
		goto out;
	}
	ptr = strrchr(tmp, '/');
	if (!ptr)
		goto out;
	ptr = memmove(ptr + 1, ptr, strlen(ptr) - 1);
	*ptr = '.';

	tofd = mkstemp(tmp);
	if (tofd < 0)
		goto out;

	if (fchmod(tofd, mode))
		goto out_close_to;

	if (st.st_size == 0) { /* /proc? do it slowly... */
		err = slow_copyfile(from, tmp);
		goto out_close_to;
	}

	fromfd = open(from, O_RDONLY);
	if (fromfd < 0)
		goto out_close_to;

	err = copyfile_offset(fromfd, 0, tofd, 0, st.st_size);

	close(fromfd);
out_close_to:
	close(tofd);
	if (!err)
		err = link(tmp, to);
	unlink(tmp);
out:
	free(tmp);
	return err;
}

int copyfile(const char *from, const char *to)
{
	return copyfile_mode(from, to, 0755);
}

static ssize_t ion(bool is_read, int fd, void *buf, size_t n)
{
	void *buf_start = buf;
	size_t left = n;

	while (left) {
		/* buf must be treated as const if !is_read. */
		ssize_t ret = is_read ? read(fd, buf, left) :
					write(fd, buf, left);

		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			return ret;

		left -= ret;
		buf  += ret;
	}

	BUG_ON((size_t)(buf - buf_start) != n);
	return n;
}

/*
 * Read exactly 'n' bytes or return an error.
 */
ssize_t readn(int fd, void *buf, size_t n)
{
	return ion(true, fd, buf, n);
}

/*
 * Write exactly 'n' bytes or return an error.
 */
ssize_t writen(int fd, const void *buf, size_t n)
{
	/* ion does not modify buf. */
	return ion(false, fd, (void *)buf, n);
}

size_t hex_width(u64 v)
{
	size_t n = 1;

	while ((v >>= 4))
		++n;

	return n;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int hex2u64(const char *ptr, u64 *long_val)
{
	char *p;

	*long_val = strtoull(ptr, &p, 16);

	return p - ptr;
}

int perf_event_paranoid(void)
{
	int value;

	if (sysctl__read_int("kernel/perf_event_paranoid", &value))
		return INT_MAX;

	return value;
}

const char *perf_tip(const char *dirpath)
{
	struct strlist *tips;
	struct str_node *node;
	char *tip = NULL;
	struct strlist_config conf = {
		.dirname = dirpath,
		.file_only = true,
	};

	tips = strlist__new("tips.txt", &conf);
	if (tips == NULL)
		return errno == ENOENT ? NULL :
			"Tip: check path of tips.txt or get more memory! ;-p";

	if (strlist__nr_entries(tips) == 0)
		goto out;

	node = strlist__entry(tips, random() % strlist__nr_entries(tips));
	if (asprintf(&tip, "Tip: %s", node->s) < 0)
		tip = (char *)"Tip: get more memory! ;-)";

out:
	strlist__delete(tips);

	return tip;
}

int
fetch_kernel_version(unsigned int *puint, char *str,
		     size_t str_size)
{
	struct utsname utsname;
	int version, patchlevel, sublevel, err;

	if (uname(&utsname))
		return -1;

	if (str && str_size) {
		strncpy(str, utsname.release, str_size);
		str[str_size - 1] = '\0';
	}

	err = sscanf(utsname.release, "%d.%d.%d",
		     &version, &patchlevel, &sublevel);

	if (err != 3) {
		pr_debug("Unablt to get kernel version from uname '%s'\n",
			 utsname.release);
		return -1;
	}

	if (puint)
		*puint = (version << 16) + (patchlevel << 8) + sublevel;
	return 0;
}
