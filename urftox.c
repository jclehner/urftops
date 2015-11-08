#include <stdio.h>
#include <fcntl.h>
#include "urf.h"

#ifndef URF_CONV
#error "URF_CONV not defined"
#endif

#define OPS_NAME_1(name) urf_ ## name ## _ops
#define OPS_NAME(name) OPS_NAME_1(name)

extern struct urf_conv_ops OPS_NAME(URF_CONV);

int main(int argc, char **argv)
{
	int ifd = 0;
	int ofd = 1;

	if (argc == 3) {
		if (*argv[1] != '-') {
			ifd = open(argv[1], O_RDONLY);
			if (ifd < 0) {
				perror("open");
				return 1;
			}
		}

		if (*argv[2] != '-') {
			ofd = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0600);
			if (ofd < 0) {
				perror("open");
				return 1;
			}
		}
	}

	return urf_convert(ifd, ofd, &OPS_NAME(URF_CONV), NULL);
}
