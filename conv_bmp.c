#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "urf.h"

struct bmp_file_header
{
	char magic[2];
	uint32_t file_size;
	uint16_t reserved1;
	uint16_t reserved2;
	uint32_t offset;
} __attribute__((__packed__));

struct bmp_dib_header
{
	uint32_t hdr_size;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bpp;
	uint32_t comp;
	uint32_t bitmap_size;
	int32_t hres;
	int32_t vres;
	uint32_t colors;
	uint32_t important_colors;
} __attribute__((__packed__));

static bool doc_begin(struct urf_context *ctx)
{
	uint32_t h, w;

	h = ctx->page1_hdr->height;
	w  = ctx->page1_hdr->width;

	size_t plb = ctx->page_line_bytes;

	struct bmp_dib_header dib_hdr = {
		.hdr_size = sizeof(struct bmp_dib_header),
		.width = w,
		.height = ctx->file_hdr->pages * h,
		.bitmap_size = ctx->file_hdr->pages * h * (plb + (plb % 4)),
		.planes = 1,
		.bpp = ctx->page1_hdr->bpp,
#if 0
		.hres = ctx->page1_hdr->dpi * 39,
		.vres = ctx->page1_hdr->dpi * 39
#endif
	};

	size_t offset = sizeof(struct bmp_file_header) + dib_hdr.hdr_size;

	struct bmp_file_header bmp_hdr = {
		.magic = "BM",
		.file_size = offset + dib_hdr.bitmap_size,
		.offset = offset
	};

	dib_hdr.height = -dib_hdr.height;

	write(ctx->ofd, &bmp_hdr, sizeof(bmp_hdr));
	write(ctx->ofd, &dib_hdr, sizeof(dib_hdr));
	return true;
}

static bool context_cleanup(struct urf_context *ctx)
{
	/*if (ctx->impl) {
		(void) realloc(ctx->impl, 0);
		ctx->impl = NULL;
	}*/

	return true;
}

static bool rast_begin(struct urf_context *ctx)
{
	size_t plb = ctx->page_line_bytes;

	ctx->impl = realloc(ctx->impl, plb + (plb % 4));
	if (!ctx->impl) {
		URF_SET_ERRNO(ctx, "realloc");
		return false;
	}

	return true;
}

static bool rast_lines(struct urf_context *ctx)
{
	uint8_t *brg = (uint8_t *)ctx->impl;
	uint8_t *rgb = (uint8_t *)ctx->line_data;

	do {
		size_t i = 0;
		for (; i != ctx->page_hdr->width; ++i) {
			size_t offset = i * ctx->page_pixel_bytes;

			brg[offset + 0] = rgb[offset + 2];
			brg[offset + 1] = rgb[offset + 1];
			brg[offset + 2] = rgb[offset + 0];
		}

		memset(brg + ctx->page_line_bytes, 0x00, i % 4);
		
		if (write(ctx->ofd, brg, ctx->page_line_bytes + i % 4) < 0) {
			URF_SET_ERRNO(ctx, "write");
			return false;
		}

		++ctx->line_n;
	} while (ctx->line_repeat--);

	return true;
}

struct urf_conv_ops urf_bmp_ops = {
	.doc_begin = &doc_begin,
	.rast_begin = &rast_begin,
	.rast_lines = &rast_lines,
	.context_cleanup = &context_cleanup,
	.id = "bmp"
};
