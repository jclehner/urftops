#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include "urf.h"

#define VERSION "0.1"

static bool xprintf(struct urf_context *ctx, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	int status = vfprintf((FILE *)ctx->impl, format, ap);
	va_end(ap);

	if (status < 0) {
		URF_SET_ERRNO(ctx, "vfprintf");
		return false;
	}

	return true;
}

static bool context_setup(struct urf_context *ctx, void *arg)
{
	if (ctx->ofd == 1) {
		ctx->impl = stdout;
	} else if (ctx->ofd == 2) {
		ctx->impl = stderr;
	} else {
		ctx->impl = fdopen(ctx->ofd, "w");
		if (!ctx->impl) {
			URF_SET_ERRNO(ctx, "fdopen");
			return false;
		}
	}

	return true;
}

static bool doc_begin(struct urf_context *ctx)
{
	unsigned height = ctx->page1_hdr->height * ctx->file_hdr->pages;
	unsigned width = ctx->page1_hdr->width;

	return xprintf(ctx,
			"%%!PS-Adobe-2.0\n"
			"%%%%Creator: urftops " VERSION "\n"
			"%%%%Title: unknown\n"
			"%%%%Pages: %u\n"
			"%%%%PageOrder: Ascend\n"
			//"%%%%BoundingBox: %u %u\n"
			"%%%%EndComments\n" 
			"/imgdata 128 3 mul string def\n",
			ctx->file_hdr->pages, ctx->file_hdr->pages * height, width
	);
}

static bool page_begin(struct urf_context *ctx)
{
	if (ctx->page_hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "unsupported bpp", -ctx->page_hdr->bpp);
		return false;
	}

	fprintf(stderr, "page %" PRIu32 "/ %" PRIu32 "\n", ctx->page_n, ctx->file_hdr->pages);

	return xprintf(ctx, "%%%%Page: %u %u\n", ctx->page_n, ctx->page_n);

	return xprintf(ctx, "%%%%Page: %u %u\n", ctx->page_n, ctx->page_n);
}

static bool rast_begin(struct urf_context *ctx)
{
	unsigned w = ctx->page_hdr->width;
	unsigned h = ctx->page_hdr->height;

	return true;

	return xprintf(ctx, 
			"1 1 scale\n"
			"%u %u 24\n"
			"[%u 0 0 -%u 0 %u]\n"
			"{\n"
			"  currentfile\n"
			"  imgdata\n"
			"  readhexstring\n"
			"  pop\n"
			"}\n"
			"false\n"
			"colorimage\n",
			ctx->page_n, w, h,
			w, h,
			w, h, h,
			ctx->page_n
	);
}

static bool rast_line(struct urf_context *ctx)
{
	size_t i = 0;

	for (; i != ctx->page_line_bytes; i += ctx->page_pixel_bytes) {
		int r = ctx->page_line[i] & 0xff;
		int g = ctx->page_line[i + 1] & 0xff;
		int b = ctx->page_line[i + 2] & 0xff;

		if (i && i % (ctx->page_pixel_bytes * 10) == 0) {
			xprintf(ctx, "\n");
		}

		if (!xprintf(ctx, "%02x%02x%02x", r, g, b)) {
			return false;
		}
	}

	xprintf(ctx, "\n\n");

	return true;
}

static bool rast_end(struct urf_context *ctx)
{
	return true;
}

static bool page_end(struct urf_context *ctx)
{
	return true;
	return xprintf(ctx, "showpage\n");
}

static bool doc_end(struct urf_context *ctx)
{
	return xprintf(ctx, "%%%%EOF\n");
}

struct urf_conv_ops urf_postscript_ops = {
	.context_setup = &context_setup,
	.doc_begin = &doc_begin,
	.page_begin = &page_begin,
	.rast_begin = &rast_begin,
	.rast_line = &rast_line,
	.rast_end = &rast_end,
	.page_end = &page_end,
	.doc_end = &doc_end,
	.id = "postscript"
};
