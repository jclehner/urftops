#ifndef URFTOPS_URF_H
#define URFTOPS_URF_H
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>

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
	/** input file descriptor */
	int ifd;
	/** output file descriptor */
	int ofd;
	/** error info */
	struct urf_error *error;
	/** URF file header */
	struct urf_file_header *file_hdr;
	/** URF page header of first page */
	struct urf_page_header *page1_hdr;
	/** URF page header of current page */
	struct urf_page_header *page_hdr;
	/** current page number (starting at 1) */
	uint32_t page_n;
	/** bytes per line on current page */
	size_t page_line_bytes;
	/** bytes per pixel on current page */
	size_t page_pixel_bytes;
	/** fill character for the blank opcode */
	char page_fill;
	/** number of times the current line should be repeated */
	uint8_t line_repeat;
	/** current line number (starting at 0) */
	size_t line_n;
	/** line data */
	char *line_data;
	/** number of raw bytes in current line */
	size_t line_raw_bytes;
	/** for private use by converters */
	void *impl;
};

struct urf_conv_ops {
	bool (*context_setup)(struct urf_context *, void *);
	void (*context_cleanup)(struct urf_context *);
	bool (*doc_begin)(struct urf_context *);
	bool (*page_begin)(struct urf_context *);
	bool (*rast_begin)(struct urf_context *);
	bool (*rast_lines)(struct urf_context *);
	bool (*rast_lines_raw)(struct urf_context *);
	bool (*rast_rle_blob)(struct urf_context *, size_t, char *, size_t);
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

