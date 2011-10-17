/*
   VHD manipulation tool.

   Copyright (C) 2011 Andrei Warkentin <andreiw@msalumni.com>

   This module is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This module is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this module; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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
#define round_up(what, on) (((what) + (on) - 1) & ~((on) - 1))
#define DYN_BLOCK_SZ        0x200000
#define BAT_ENTRY_EMPTY     0xFFFFFFFF

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

typedef uint32_t vhd_batent;

struct vhd
{
	struct vhd_footer footer;
	struct vhd_dyn dyn;
	char uuid_str[37];
	char *name;
	off64_t size;
	off64_t offset;
	int fd;
};

int vhd_init(struct vhd *vhd,
	     char *name)
{
	memset(vhd, 0, sizeof(*vhd));
	vhd->fd = -1;
	vhd->name = name;

	return 0;
}

int vhd_write(struct vhd *vhd,
	      uint8_t *buf,
	      size_t size)
{
	if (vhd->fd == -1) {
		vhd->fd = creat(vhd->name, 0644);
		if (vhd->fd == -1) {
			perror("Couldn't open VHD file for writing");
			return -1;
		}
	}

	if (lseek64(vhd->fd, vhd->offset, SEEK_SET) != vhd->offset) {
		perror("Couldn't seek VHD file");
		return -1;
	}

	if (write(vhd->fd, buf, size) !=  (int) size) {
		perror("Couldn't write VHD metadata");
		return -1;
	}

	vhd->offset += size;
	return 0;
}

int vhd_close(struct vhd *vhd, int status)
{
	if (vhd->fd != -1) {
		if (!status) {
			if (fsync(vhd->fd)) {
				perror("Couldn't flush VHD data");
				return -1;
			}

			if (close(vhd->fd)) {
				perror("Couldn't close VHD file");
				return -1;
			}
		} else {
			if (unlink(vhd->name)) {
				perror("Couldn't clean up VHD file");
				return -1;
			}
		}
	}

	return 0;
}

uint32_t vhd_checksum(uint8_t *data, size_t size)
{
	uint32_t csum = 0;
	while (size--) {
		csum += *data++;
	}
	return ~csum;
}

void vhd_chs(struct vhd *vhd)
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
		chs.h = (cyl_x_heads + 1023) >> 10;
		if (chs.h < 4)
			chs.h = 4;

		if (cyl_x_heads >= (uint64_t) (chs.h << 10) ||
		    chs.h > 16) {
			chs.s = 31;
			chs.h = 16;
			cyl_x_heads = sectors / chs.s;
		}
		if (cyl_x_heads >= (uint64_t) (chs.h << 10)) {
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

int vhd_footer(struct vhd *vhd,
	       off64_t size,
	       uint32_t type,
	       uint64_t data_offset)
{
	vhd->size = size;
	if (vhd->size >> 9 << 9 != vhd->size) {
		fprintf(stderr, "Size must be in units of 512-byte sectors\n");
		return -1;
	}

	vhd->footer.cookie = COOKIE("conectix");
	vhd->footer.features = htobe32(FOOTER_FEAT_RSVD);
	vhd->footer.data_offset = htobe64(data_offset);
	vhd->footer.file_format_ver = htobe32(VHD_VERSION_1);
	vhd->footer.time_stamp = htobe32(time(NULL) + SECONDS_OFFSET);
	vhd->footer.creator_app = COOKIE32("vhdt");
	vhd->footer.creator_ver = htobe32(0x1);
	vhd->footer.creator_os = COOKIE32("Lnux");
	vhd->footer.original_size = htobe64(vhd->size);
	vhd->footer.current_size = htobe64(vhd->size);
	vhd->footer.disk_type = htobe32(type);
	vhd_chs(vhd);
	uuid_generate((uint8_t *) &vhd->footer.vhd_id);
	uuid_unparse((uint8_t *) &vhd->footer.vhd_id, vhd->uuid_str);
	vhd->footer.vhd_id.f1 = htobe32(vhd->footer.vhd_id.f1);
	vhd->footer.vhd_id.f2 = htobe16(vhd->footer.vhd_id.f2);
	vhd->footer.vhd_id.f3 = htobe16(vhd->footer.vhd_id.f3);
	vhd->footer.checksum = vhd_checksum((uint8_t *) &vhd->footer,
					   sizeof(vhd->footer));
	vhd->footer.checksum = htobe32(vhd->footer.checksum);

	return 0;
}

int vhd_dyn(struct vhd *vhd, uint32_t block_size)
{
	vhd->dyn.cookie = COOKIE("cxsparse");
	vhd->dyn.data_offset = htobe64(DYN_DOFF_DYN);
	vhd->dyn.table_offset = htobe64(vhd->offset + sizeof(vhd->dyn));
	vhd->dyn.header_version = htobe32(DYN_VERSION_1);

	if (block_size >> 9 << 9 != block_size) {
		fprintf(stderr, "Block size must be in units of 512-byte sectors\n");
		return -1;
	}

	vhd->dyn.block_size = htobe32(block_size);
	vhd->dyn.max_tab_entries = vhd->size / block_size;
	if (!vhd->dyn.max_tab_entries) {
		fprintf(stderr, "Block size can't be larger than the VHD\n");
		return -1;
	}
	if (vhd->dyn.max_tab_entries * block_size != vhd->size) {
		fprintf(stderr, "VHD size not multiple of block size\n");
		return -1;		
	}
	vhd->dyn.max_tab_entries = htobe32(vhd->dyn.max_tab_entries);
	vhd->dyn.checksum = vhd_checksum((uint8_t *) &vhd->dyn,
					   sizeof(vhd->dyn));
	vhd->dyn.checksum = htobe32(vhd->dyn.checksum);
	return 0;
}

int main(int argc, char **argv)
{
	int status;
	struct vhd vhd;
	off64_t vhd_size = 0;
	uint32_t vhd_type = 0;
	off64_t block_size = 0;
	bool do_help = false;

	while (1) {
		int c;
		opterr = 0;
		c = getopt(argc, argv, "b:s:t:");
		if (c == -1)
			break;
		else if (c == '?') {
			do_help = true;
			break;
		}
			
		switch (c) {
		case 'b':
		case 's':
		{
			off64_t *size;
			char type = 0;

			if (c == 'b')
				size = &block_size;
			else
				size = &vhd_size;

			/* Handle VHD size. */
			sscanf (optarg, "%ju%c", size, &type);
			switch (type) {
			case 't':
			case 'T':
				*size <<= 10;
			case 'g':
			case 'G':
				*size <<= 10;
			case 'm':
			case 'M':
				*size <<= 10;
			case 'k':
			case 'K':
				*size <<= 10;
			case '\0':
			case 'b':
			case 'B':
				break;
			case 's':
			case 'S':
				*size <<= 9;
                                break;
			default:
				fprintf(stderr, "%s size modifer '%c' not one of [BKMGTS]\n",
					size == &block_size ? "Block" : "VHD",
					type);
				return -1;
			}
			break;
		}
		case 't':
		{
			static const char fixed_str[] = "fixed";
			static const char dynamic_str[] = "dynamic";
			if (strstr(fixed_str, optarg) == fixed_str)
				vhd_type = FOOTER_TYPE_FIXED;
			else if (strstr(dynamic_str, optarg) == dynamic_str)
				vhd_type = FOOTER_TYPE_DYN;
			else {
				fprintf(stderr, "Disk type not one of 'fixed' or 'dynamic'\n");
				return -1;
			}
			break;
		}
		}
	}
	
	if (do_help || !vhd_size || optind != (argc - 1)) {
		printf("%s -s size [-b block_size] [-t type] vhd-file-name\n",
			argv[0]);
		return -1;
	};

	if (vhd_init(&vhd, argv[optind]))
		return -1;

	if (!vhd_type) {
		if (block_size)
			vhd_type = FOOTER_TYPE_DYN;
		else
			vhd_type = FOOTER_TYPE_FIXED;
	}

	if (!block_size)
		block_size = DYN_BLOCK_SZ;

	if ((status = vhd_footer(&vhd,
				 vhd_size, vhd_type,
				 vhd_type == FOOTER_TYPE_FIXED ? 
				 FOOTER_DOFF_FIXED :
				 sizeof(vhd.footer))))
		goto done;

	printf("Generating %s VHD %s (%ju bytes)\n",
	       vhd_type == FOOTER_TYPE_FIXED ? "fixed" : "dynamic",
	       vhd.uuid_str, vhd.size);

	if (vhd_type == FOOTER_TYPE_FIXED) {
		vhd.offset = vhd.size;
		if ((status = vhd_write(&vhd, (uint8_t *) &vhd.footer, sizeof(vhd.footer))))
			goto done;
	} else {
		size_t bat_entries;
		vhd.offset = 0;
		vhd_batent empty = BAT_ENTRY_EMPTY;

		if ((status = vhd_write(&vhd, (uint8_t *) &vhd.footer, sizeof(vhd.footer))))
			goto done;
		if ((status = vhd_dyn(&vhd, block_size)))
			goto done;
		if ((status = vhd_write(&vhd, (uint8_t *) &vhd.dyn, sizeof(vhd.dyn))))
			goto done;

		bat_entries = vhd_size / block_size;
		while (bat_entries--)
			if ((status = vhd_write(&vhd, (uint8_t *) &empty, sizeof(empty))))
				goto done;
		vhd.offset = round_up(vhd.offset, 512);
		if ((status = vhd_write(&vhd, (uint8_t *) &vhd.footer, sizeof(vhd.footer))))
			goto done;
	}

done:
	if (vhd_close(&vhd, status))
		return -1;
	return 0;
}
