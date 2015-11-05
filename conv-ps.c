#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include "urf.h"

#define VERSION "0.1"

static bool xprintf(struct urf_context *ctx, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	int status = vfprintf((FILE *)ctx->impl, format, ap);
	va_end(ap);

	if (status < 0 || ferror((FILE *)ctx->impl)) {
		URF_SET_ERRNO(ctx, "vfprintf");
		return false;
	}

	return true;
}

static bool context_setup(struct urf_context *ctx, void *arg)
{
	ctx->impl = fdopen(ctx->ofd, "w");
	if (!ctx->impl) {
		URF_SET_ERRNO(ctx, "fdopen");
		return false;
	}

	return true;
}

static bool context_cleanup(struct urf_context *ctx)
{
	if (ctx->impl) {
		fclose((FILE *)ctx->impl);
		ctx->impl = NULL;
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
			"%%%%BoundingBox: %u %u"
			"%%%%EndComments\n", 
			ctx->file_hdr->pages, height, width
	);
}

static bool page_begin(struct urf_context *ctx)
{
	if (ctx->page_hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "unsupported bpp", -ctx->page_hdr->bpp);
		return false;
	}

	return xprintf(ctx, "%%%%Page: %u %u\n", ctx->page_n, ctx->page_n);
}

static bool rast_begin(struct urf_context *ctx)
{
	unsigned width = ctx->page_hdr->width;
	unsigned height = ctx->page_hdr->height;

	return xprintf(ctx,
			"0 0 translate\n"
			"%u %u scale\n"
			"%u %u %u [%u 0 0 -%u 0 %u]\n"
			"{<\n", 
			width, height, width, height, ctx->page_hdr->bpp, 
			width, height, height
	);
}

static bool rast_line(struct urf_context *ctx)
{
	size_t i = 0;
	for (; i != ctx->page_line_bytes; i += ctx->page_pixel_bytes) {
		int r = ctx->page_line[i];
		int g = ctx->page_line[i + 1];
		int b = ctx->page_line[i + 2];

		if (!xprintf(ctx, "%02x%02x%02x", r, g, b)) {
			return false;
		}

		if (i != 0 && (i % 9) == 0) {
			if (!xprintf(ctx, "\n")) {
				return false;
			}
		}
	}

	return true;
}

static bool rast_end(struct urf_context *ctx)
{
	return xprintf(ctx, 
			">}\n"
			"false %d colorimage\n",
			ctx->page_pixel_bytes);
}

static bool page_end(struct urf_context *ctx)
{
	return xprintf(ctx, "showpage\n");
}

static bool doc_end(struct urf_context *ctx)
{
	return xprintf(ctx, "%%EOF\n");
}

struct urf_conv_ops urf_postscript_ops = {
	.context_setup = &context_setup,
	.context_cleanup = &context_cleanup,
	.doc_begin = &doc_begin,
	.doc_end = &doc_end,
	.page_begin = &page_begin,
	.rast_begin = &rast_begin,
	.rast_line = &rast_line,
	.rast_end = &rast_end,
	.page_end = &page_end,
	.doc_end = &doc_end,
	.id = "postscript"
};
