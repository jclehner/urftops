#ifndef URFTOPS_URF_H
#define URFTOPS_URF_H
#include <stdbool.h>
#include <stdint.h>

struct urf_file_header {
	char magic[8];
	uint32_t pages;
} __attribute__((__packed__));

struct urf_page_header {
	uint8_t bpp;
	uint8_t colorspace;
	uint8_t duplex;
	uint8_t quality;
	uint32_t unknown0;
	uint32_t unknown1;
	uint32_t width;
	uint32_t height;
	uint32_t dpi;
	uint32_t unknown2;
	uint32_t unknown3;
} __attribute__((__packed__));

struct urf_pixels {
	unsigned repeat;
	char *data;
	size_t count;
};

struct urf_error {
	int code;
	const char *msg;
};

struct urf_context {
	int ifd;
	int ofd;
	struct urf_error *error;
	struct urf_file_header *file_hdr;
	struct urf_page_header *page1_hdr;
	struct urf_page_header *page_hdr;
	uint32_t page_n;
	size_t page_line_bytes;
	size_t page_pixel_bytes;
	size_t page_lines;
	char page_fill;
	char *page_line;
	void *impl;
};

struct urf_conv_ops {
	bool (*context_setup)(struct urf_context *, void *);
	bool (*context_cleanup)(struct urf_context *);
	bool (*doc_begin)(struct urf_context *);
	bool (*page_begin)(struct urf_context *);
	bool (*rast_begin)(struct urf_context *);
	bool (*rast_line)(struct urf_context *);
	bool (*rast_end)(struct urf_context *);
	bool (*page_end)(struct urf_context *);
	bool (*doc_end)(struct urf_context *);
	char id[16];
};

int urf_convert(int ifd, int ofd, struct urf_conv_ops *ops, void *arg);

#define URF_SET_ERROR(c, m, e) \
	do { \
		(c)->error->code = e; \
		(c)->error->msg = m; \
	} while(0)
#define URF_SET_ERRNO(c, m) URF_SET_ERROR(c, m, errno)

#endif

