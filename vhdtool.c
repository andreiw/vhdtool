#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <stdio.h>
#include <endian.h>
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define COOKIE(x)           (*(uint64_t *) x)
#define COOKIE32(x)         (*(uint32_t *) x)
#define MY_COOKIE           COOKIE("andreiwv")
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
#define SECONDS_OFFSET      946684800
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

struct vhd
{
	struct vhd_footer footer;
	struct vhd_dyn dyn;
	char uuid_str[37];
	off64_t size;
	int type;
	int fd;
};

static uint32_t vhd_checksum(uint8_t *data, size_t size)
{
	uint32_t csum = 0;
	while (size--) {
		csum += *data++;
	}
	return ~csum;
}

static void vhd_chs(struct vhd *vhd)
{
	uint64_t cyl_x_heads;
	struct vhd_chs chs;
	uint64_t sectors = vhd->size >> 9;

	/*
	 * Blame AndrewN for this one... All this logic is from
	 * the VHD specification.
	 */
	if (sectors > 65535 * 16 * 255) {

		/* ~127GiB */
		sectors =  65535 * 16 * 255;
	}
	if (sectors >= 65535 * 16 * 63) {
		chs.s = 255;
		chs.h = 16;
		cyl_x_heads = sectors / chs.s;
	} else {
		chs.s = 17;
		cyl_x_heads = sectors / chs.s;
		chs.h = (cyl_x_heads + 1023) / 1024;
		if (chs.h < 4)
			chs.h = 4;

		if (cyl_x_heads >= (chs.h * 1024) || chs.h > 16) {
			chs.s = 31;
			chs.h = 16;
			cyl_x_heads = sectors / chs.s;
		}
		if (cyl_x_heads >= (chs.h * 1024)) {
			chs.s = 63;
			chs.h = 16;
			cyl_x_heads = sectors / chs.s;
		}
	}
	chs.c = cyl_x_heads / chs.h;
	vhd->footer.disk_geometry.c = htobe16(chs.c);
	vhd->footer.disk_geometry.h = chs.h;
	vhd->footer.disk_geometry.s = chs.s;
}

void vhd_footer(struct vhd *vhd, uint64_t data_offset)
{
	memset(&vhd->footer, 0, sizeof(vhd->footer));
	vhd->footer.cookie = htobe64(COOKIE("andreiwv"));
	vhd->footer.features = htobe32(FOOTER_FEAT_RSVD);
	vhd->footer.data_offset = htobe64(data_offset);
	vhd->footer.file_format_ver = htobe32(VHD_VERSION_1);
	vhd->footer.time_stamp = htobe32(time(NULL) + SECONDS_OFFSET);
	vhd->footer.creator_app = htobe32(COOKIE32("vhdt"));
	vhd->footer.creator_ver = htobe32(0x1);
	vhd->footer.creator_os = htobe32(COOKIE32("Lnux"));
	vhd->footer.original_size = htobe64(vhd->size);
	vhd->footer.current_size = htobe64(vhd->size);
	vhd->footer.disk_type = htobe32(vhd->type);
	vhd_chs(vhd);
	uuid_generate((uint8_t *) &vhd->footer.vhd_id);
	uuid_unparse((uint8_t *) &vhd->footer.vhd_id, vhd->uuid_str);
	printf("Generating %s VHD %s (%ju bytes)\n",
	       vhd->type == FOOTER_TYPE_FIXED ? "fixed" : "dynamic",
	       vhd->uuid_str, vhd->size);
	vhd->footer.vhd_id.f1 = htobe32(vhd->footer.vhd_id.f1);
	vhd->footer.vhd_id.f2 = htobe16(vhd->footer.vhd_id.f2);
	vhd->footer.vhd_id.f3 = htobe16(vhd->footer.vhd_id.f3);
	vhd->footer.checksum = vhd_checksum((uint8_t *) &vhd->footer,
					   sizeof(vhd->footer));
	vhd->footer.checksum = htobe32(vhd->footer.checksum);
}

int main(int argc, char **argv)
{
	struct vhd vhd;

	while (1) {
		int c;
		c = getopt(argc, argv, "s:t:");
		if (c == -1)
			break;

		switch (c) {
		case 's':
		{
			char type;

			/* Handle VHD size. */
			sscanf (optarg, "%ju%c", &vhd.size, &type);
			switch (type) {
			case 't':
			case 'T':
				vhd.size <<= 10;
			case 'g':
			case 'G':
				vhd.size <<= 10;
			case 'm':
			case 'M':
				vhd.size <<= 10;
			case 'k':
			case 'K':
				vhd.size <<= 10;
			case '\0':
			case 'b':
			case 'B':
				if (vhd.size >> 9 << 9 != vhd.size) {
					fprintf(stderr, "size must be in units of 512-byte sectors\n");
					return -1;
				}
				break;
			case 's':
			case 'S':
				vhd.size <<= 9;
                                break;
			default:
				fprintf(stderr, "Size modifer '%c' not one of [BKMGTS]\n",
					type);
				return -1;
			}
			break;
		}
		case 't':
			
			if (!strcmp(optarg, "fixed"))
				vhd.type = FOOTER_TYPE_FIXED;
			else {
				fprintf(stderr, "Supported disk types are: fixed\n");
				return -1;
			}
			break;
		};
	}

	if (!vhd.type)
		vhd.type = FOOTER_TYPE_FIXED;

	if (!vhd.size || optind != (argc - 1)) {
		printf("%s -s size [-t type] vhd-file-name\n",
			argv[0]);
		return -1;
	};

	vhd_footer(&vhd, FOOTER_DOFF_FIXED);

	vhd.fd = creat(argv[optind], 0644);
	if (vhd.fd == -1) {
		perror("couldn't open VHD file for writing");
		return -1;
	}

	if (lseek64(vhd.fd, vhd.size, SEEK_SET) != vhd.size) {
		perror("couldn't seek to end of VHD file");
		return -1;
	}

	if (write(vhd.fd, &vhd.footer, sizeof(vhd.footer)) !=
	    sizeof(vhd.footer)) {
		perror("couldn't write VHD metadata");
		return -1;
	}

	if (fsync(vhd.fd)) {
		perror("couldn't flush VHD data");
		return -1;
	}

	if (close(vhd.fd)) {
		perror("couldn't close VHD file");
		return -1;
	}

	return 0;
}
