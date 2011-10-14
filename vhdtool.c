#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

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
