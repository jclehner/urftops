#include <stdio.h>
#include <fcntl.h>
#include "urf.h"

extern struct urf_conv_ops urf_postscript_ops;

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
			ofd = open(argv[2], O_CREAT | O_WRONLY, 0600);
			if (ofd < 0) {
				perror("open");
				return 1;
			}
		}
	}

	return urf_convert(ifd, ofd, &urf_postscript_ops, NULL);
}
