/*
 * s390 diagnose functions
 *
 * Copyright IBM Corp. 2007
 * Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _ASM_S390_DIAG_H
#define _ASM_S390_DIAG_H

#include <linux/if_ether.h>

/*
 * Diagnose 10: Release page range
 */
static inline void diag10_range(unsigned long start_pfn, unsigned long num_pfn)
{
	unsigned long start_addr, end_addr;

	start_addr = start_pfn << PAGE_SHIFT;
	end_addr = (start_pfn + num_pfn - 1) << PAGE_SHIFT;

	asm volatile(
		"0:	diag	%0,%1,0x10\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		EX_TABLE(1b, 1b)
		: : "a" (start_addr), "a" (end_addr));
}

/*
 * Diagnose 14: Input spool file manipulation
 */
extern int diag14(unsigned long rx, unsigned long ry1, unsigned long subcode);

/*
 * Diagnose 210: Get information about a virtual device
 */
struct diag210 {
	u16 vrdcdvno;	/* device number (input) */
	u16 vrdclen;	/* data block length (input) */
	u8 vrdcvcla;	/* virtual device class (output) */
	u8 vrdcvtyp;	/* virtual device type (output) */
	u8 vrdcvsta;	/* virtual device status (output) */
	u8 vrdcvfla;	/* virtual device flags (output) */
	u8 vrdcrccl;	/* real device class (output) */
	u8 vrdccrty;	/* real device type (output) */
	u8 vrdccrmd;	/* real device model (output) */
	u8 vrdccrft;	/* real device feature (output) */
} __attribute__((packed, aligned(4)));

extern int diag210(struct diag210 *addr);

enum diag26c_sc {
	DIAG26C_MAC_SERVICES = 0x00000030
};

enum diag26c_version {
	DIAG26C_VERSION2 = 0x00000002	/* z/VM 5.4.0 */
};

#define DIAG26C_GET_MAC	0x0000
struct diag26c_mac_req {
	u32	resp_buf_len;
	u32	resp_version;
	u16	op_code;
	u16	devno;
	u8	res[4];
};

struct diag26c_mac_resp {
	u32	version;
	u8	mac[ETH_ALEN];
	u8	res[2];
} __aligned(8);

int diag26c(void *req, void *resp, enum diag26c_sc subcode);

/* bit is set in flags, when physical cpu info is included in diag 204 data */
#define DIAG204_LPAR_PHYS_FLG 0x80
#define DIAG204_LPAR_NAME_LEN 8		/* lpar name len in diag 204 data */
#define DIAG204_CPU_NAME_LEN 16		/* type name len of cpus in diag224 name table */

/* diag 204 subcodes */
enum diag204_sc {
	DIAG204_SUBC_STIB4 = 4,
	DIAG204_SUBC_RSI = 5,
	DIAG204_SUBC_STIB6 = 6,
	DIAG204_SUBC_STIB7 = 7
};

/* The two available diag 204 data formats */
enum diag204_format {
	DIAG204_INFO_SIMPLE = 0,
	DIAG204_INFO_EXT = 0x00010000
};

enum diag204_cpu_flags {
	DIAG204_CPU_ONLINE = 0x20,
	DIAG204_CPU_CAPPED = 0x40,
};

struct diag204_info_blk_hdr {
	__u8  npar;
	__u8  flags;
	__u16 tslice;
	__u16 phys_cpus;
	__u16 this_part;
	__u64 curtod;
} __packed;

struct diag204_x_info_blk_hdr {
	__u8  npar;
	__u8  flags;
	__u16 tslice;
	__u16 phys_cpus;
	__u16 this_part;
	__u64 curtod1;
	__u64 curtod2;
	char reserved[40];
} __packed;

struct diag204_part_hdr {
	__u8 pn;
	__u8 cpus;
	char reserved[6];
	char part_name[DIAG204_LPAR_NAME_LEN];
} __packed;

struct diag204_x_part_hdr {
	__u8  pn;
	__u8  cpus;
	__u8  rcpus;
	__u8  pflag;
	__u32 mlu;
	char  part_name[DIAG204_LPAR_NAME_LEN];
	char  lpc_name[8];
	char  os_name[8];
	__u64 online_cs;
	__u64 online_es;
	__u8  upid;
	__u8  reserved:3;
	__u8  mtid:5;
	char  reserved1[2];
	__u32 group_mlu;
	char  group_name[8];
	char  hardware_group_name[8];
	char  reserved2[24];
} __packed;

struct diag204_cpu_info {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	__u8  cflag;
	__u16 weight;
	__u64 acc_time;
	__u64 lp_time;
} __packed;

struct diag204_x_cpu_info {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	__u8  cflag;
	__u16 weight;
	__u64 acc_time;
	__u64 lp_time;
	__u16 min_weight;
	__u16 cur_weight;
	__u16 max_weight;
	char  reseved2[2];
	__u64 online_time;
	__u64 wait_time;
	__u32 pma_weight;
	__u32 polar_weight;
	__u32 cpu_type_cap;
	__u32 group_cpu_type_cap;
	char  reserved3[32];
} __packed;

struct diag204_phys_hdr {
	char reserved1[1];
	__u8 cpus;
	char reserved2[6];
	char mgm_name[8];
} __packed;

struct diag204_x_phys_hdr {
	char reserved1[1];
	__u8 cpus;
	char reserved2[6];
	char mgm_name[8];
	char reserved3[80];
} __packed;

struct diag204_phys_cpu {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	char  reserved2[3];
	__u64 mgm_time;
	char  reserved3[8];
} __packed;

struct diag204_x_phys_cpu {
	__u16 cpu_addr;
	char  reserved1[2];
	__u8  ctidx;
	char  reserved2[1];
	__u16 weight;
	__u64 mgm_time;
	char  reserved3[80];
} __packed;

struct diag204_x_part_block {
	struct diag204_x_part_hdr hdr;
	struct diag204_x_cpu_info cpus[];
} __packed;

struct diag204_x_phys_block {
	struct diag204_x_phys_hdr hdr;
	struct diag204_x_phys_cpu cpus[];
} __packed;

#define CPNC_LINUX		0x4
union diag318_info {
	unsigned long val;
	struct {
		unsigned int cpnc : 8;
		unsigned int cpvc_linux : 24;
		unsigned char cpvc_distro[3];
		unsigned char zero;
	};
};

int diag204(unsigned long subcode, unsigned long size, void *addr);
int diag224(void *ptr);
#endif /* _ASM_S390_DIAG_H */
