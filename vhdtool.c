#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

#define COOKIE(x)           (*(uint64_t *) x)
#define CXSPARSE_COOKIE     COOKIE("cxsparse")
#define FOOTER_FEAT_RSVD    (2)
#define VHD_VERSION_1       (0x00010000UL)
#define VHD_VMAJ_MASK       (0xFFFF0000UL)
#define VHD_VMIN_MASK       (0x0000FFFFUL)
#define DYN_VERSION_1       (0x00010000UL)
#define DYN_VMAJ_MASK       (0xFFFF0000UL)
#define DYN_VMIN_MASK       (0x0000FFFFUL)
#define FOOTER_DOFF_FIXED   (0xFFFFFFFFFFFFFFFFULL)
#define DYN_DOFF_DYN        (0xFFFFFFFFFFFFFFFFULL)
#define FOOTER_TYPE_FIXED   (2)
#define FOOTER_TYPE_DYN     (3)
#define FOOTER_TYPE_DIFF    (4)
#define SEC_SHIFT           (9)
#define SEC_SZ              (1 << SEC_SHIFT)
#define SEC_MASK            (SEC_SZ - 1)
#define BAT_SZ(entries)     round_up(sizeof(vhd_batent) * (entries), SEC_SZ)
#define SECTOR_BMP_SZ(usz)  round_up((usz) >> (SEC_SHIFT + 3), SEC_SZ)

/* All fields Big-Endian */
struct vhd_id
{
	uint32_t f1;
	uint16_t f2;
	uint16_t f3;
	uint8_t  f4[8];
};

/* All fields Big-Endian */
struct vhd_chs
{
	uint16_t c;
	uint8_t  h;
	uint8_t  s;
};

/* All fields Big-Endian */
struct vhd_footer
{
	uint64_t cookie;
	uint32_t features;
	uint32_t file_format_ver;
	uint64_t data_offset;
	uint32_t time_stamp;
	uint32_t creator_app;
	uint32_t creator_ver;
	uint32_t creator_os;
	uint64_t original_size;
	uint64_t current_size;
	struct vhd_chs disk_geometry;
	uint32_t disk_type;
	uint32_t checksum;
	struct vhd_id vhd_id;
	uint8_t saved_state;
	uint8_t reserved[427];
};

/* All fields Big-Endian */
struct vhd_ploc
{
	uint32_t code;
	uint32_t sectors;
	uint32_t length;
	uint32_t reserved;
	uint64_t offset;
};

/* All fields Big-Endian */
struct vhd_dyn
{
	uint64_t cookie;
	uint64_t data_offset;
	uint64_t table_offset;
	uint32_t header_version;
	uint32_t max_tab_entries;
	uint32_t block_size;
	uint32_t checksum;
	struct vhd_id parent;
	uint32_t parent_time_stamp;
	uint32_t reserved0;
	uint8_t parent_utf16[512];
	struct vhd_ploc pe[8];
	uint8_t reserved1[256];
};

int main(int argc, char **argv)
{
	uint64_t size;

	while (1) {
		int c;
		c = getopt(argc, argv, "s:t:");
		if (c == -1)
			break;

		switch (c) {
		case 's':

			/* Handle VHD size. */
			size = 0;
			break;
		case 't':

			/* Handle VHD type. */
			break;
		};
	}

	if (!size || optind != (argc - 1)) {
		printf("%s -s size [-t type] vhd-file-name\n",
			argv[0]);
		return -1;
	};

	return 0;
}
