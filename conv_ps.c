#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <zlib.h>
#include "urf.h"

#define VERSION "0.1"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define PACK_CODE_REPEAT(n) (-(int8_t)(n) + 1)
#define PACK_CODE_COPY(n) ((int8_t)(n) - 1)

//#define NODEFLATE

#define log fprintf
#define LOG_DBG stderr
#define LOG_ERR stderr

#define CHUNK (16 * 1024)

#define IMPL(ctx) ((struct impl *)ctx->impl)

//#define RAW_Z
//#define RAW_Z_85

struct impl
{
	FILE *fp;
	unsigned char *zbuf;
	size_t zlen;
	unsigned char *page;
	unsigned char *line;
	size_t idx;

	z_stream strm;
};

static bool buf_realloc(struct urf_context *ctx, unsigned char **buf, size_t size)
{
	unsigned char *p = realloc(*buf, size);
	if (!p) {
		log(LOG_ERR, "realloc failed\n");
		URF_SET_ERRNO(ctx, "realloc");
		return false;
	}

	*buf = p;
	return true;
}

static bool xprintf(struct urf_context *ctx, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	int status = vfprintf(IMPL(ctx)->fp, format, ap);
	va_end(ap);

	if (status < 0) {
		URF_SET_ERRNO(ctx, "vfprintf");
		return false;
	}

	return true;
}


static bool context_setup(struct urf_context *ctx, void *arg)
{
	struct impl *impl = ctx->impl = malloc(sizeof(struct impl));
	if (!impl) {
		URF_SET_ERRNO(ctx, "malloc");
		return false;
	}

	impl->zbuf = impl->page = impl->line = NULL;
	impl->zlen = impl->idx = 0;

	if (!(impl->fp = fdopen(ctx->ofd, "w"))) {
		URF_SET_ERRNO(ctx, "fdopen");
		return false;
	}

	impl->strm.zalloc = Z_NULL;
	impl->strm.zfree = Z_NULL;
	impl->strm.opaque = Z_NULL;

	if (deflateInit(&impl->strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		URF_SET_ERRNO(ctx, "deflateInit");
		return false;
	}

	return true;
}

static void context_cleanup(struct urf_context *ctx)
{
	struct impl *impl = IMPL(ctx);

	if (impl) {
		deflateEnd(&impl->strm);
		fclose(impl->fp);
		free(impl->zbuf);
		free(impl->page);
		free(impl);
	}
}

static bool doc_begin(struct urf_context *ctx)
{
	size_t xmax = ctx->page1_hdr->width;
	size_t ymax = ctx->page1_hdr->height * ctx->file_hdr->pages;

	return xprintf(ctx,
			"%%!PS-Adobe-2.0\n"
			"%%%%LanguageLevel: 2\n"
			"%%%%Creator: urftops " VERSION "\n"
			"%%%%Title: unknown\n"
			"%%%%Pages: %u\n"
			"%%%%DocumentData: Clean7Bit\n"
			"%%%%BoundingBox: 0 0 %zu %zu\n"
			"%%%%EndComments\n" 
			"%%%%EndProlog\n",
			ctx->file_hdr->pages, ctx->page1_hdr->width, 
			ctx->page1_hdr->height, ctx->page1_hdr->dpi,
			xmax, ymax);
}

static bool page_begin(struct urf_context *ctx)
{
	if (ctx->page_hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "unsupported bpp", -ctx->page_hdr->bpp);
		return false;
	}

	IMPL(ctx)->zlen = 2 * deflateBound(&IMPL(ctx)->strm, ctx->page_line_bytes);
	if (!buf_realloc(ctx, &IMPL(ctx)->zbuf, IMPL(ctx)->zlen)) {
		return false;
	}

	IMPL(ctx)->idx = 0;

	return xprintf(ctx,
			"%%%%Page: %" PRIu32 " %" PRIu32 "\n"
			"%%%%PageBoundingBox: 0 0 %" PRIu32 " %" PRIu32 "\n"
			"save\n"
			"/DeviceRGB setcolorspace\n"
#if 0
			"8 dict dup begin\n"
			"  /ImageType 1 def\n"
			"  /Width %" PRIu32 " def\n"
			"  /Height %" PRIu32 " def\n"
			"  /Interpolate true def\n"
			"  /BitsPerComponent 8 def\n"
			"  /Decode [ 0 1 0 1 0 1 ] def\n"
			"  /DataSource currentfile /ASCIIHexDecode filter /FlateDecode filter def\n"
			"  /ImageMatrix [ 1 0 0 -1 0 %" PRIu32 " ] def\n"
			"end\n"
#else
			"<<\n"
			"  /ImageType 1\n"
			"  /Width %" PRIu32 "\n"
			"  /Height %" PRIu32 "\n"
			//"  /ImageMatrix [ %" PRIu32 " 0 0 -%" PRIu32 " 0 %" PRIu32 " ]\n"
			"  /ImageMatrix [ 1 0 0 -1 0 %" PRIu32 " ]\n"
			"  /BitsPerComponent 8\n"
			"  /Decode [ 0 1 0 1 0 1 ]\n"
			"  /DataSource currentfile\n"
			"    /ASCIIHexDecode filter\n"
#ifndef NODEFLATE
			"    /FlateDecode filter\n"
#endif
			">>\n"
#endif
			"image\n",
			ctx->page_n, ctx->page_n, ctx->page_hdr->width,
			ctx->page_hdr->height, ctx->page_hdr->width,
			ctx->page_hdr->height,
		//	ctx->page_hdr->width, ctx->page_hdr->height,
			ctx->page_hdr->height);
}

static bool rast_begin(struct urf_context *ctx)
{
	return true;
}

static bool rast_line(struct urf_context *ctx)
{
#ifdef NODEFLATE
	size_t i = 0;
	for (; i < ctx->page_line_bytes; ++i, ++IMPL(ctx)->idx) {
		if (IMPL(ctx)->idx && !((IMPL(ctx)->idx % 35))) {
			xprintf(ctx, "\n");
		}

		if (!xprintf(ctx, "%02x", ctx->line_data[i] & 0xff)) {
			return false;
		}
	}

	fprintf(stderr, "\rpage %u, line %zu", ctx->page_n, ctx->line_n);
	fflush(stderr);

	return true;

#else
	z_stream *strm = &IMPL(ctx)->strm;
	strm->avail_in = ctx->page_line_bytes;
	strm->next_in = (unsigned char*)ctx->line_data;

	do {
		strm->avail_out = IMPL(ctx)->zlen;
		strm->next_out = IMPL(ctx)->zbuf;

		int flush = (ctx->line_n < ctx->page_hdr->height) ?
			Z_NO_FLUSH : Z_FINISH;

		if (deflate(strm, flush) != Z_STREAM_ERROR) {
			size_t i, have = IMPL(ctx)->zlen - strm->avail_out;
			for (i = 0; i < have; ++i, ++(IMPL(ctx)->idx)) {
#if 0
				if (IMPL(ctx)->idx && !((IMPL(ctx)->idx % 35))) {
					xprintf(ctx, "\n");
				}

				if (!xprintf(ctx, "%02x", IMPL(ctx)->zbuf[i])) {
					break;
				}
#endif
			}

			if (i != have) {
				return false;
			}
		} else {
			URF_SET_ERROR(ctx, "deflate", Z_ERRNO);
			return false;
		}
	} while (strm->avail_out == 0);
#endif

	return true;
}

static bool rast_lines(struct urf_context *ctx)
{
	do {
		if (!rast_line(ctx)) {
			return false;
		}
		++ctx->line_n;
	} while (ctx->line_repeat--);

	return true;
}

static bool page_end(struct urf_context *ctx)
{
	fprintf(stderr, "\npage %u: %zu bytes\n", ctx->page_n, IMPL(ctx)->idx);

	int ret = deflateReset(&IMPL(ctx)->strm);
	if (ret < 0) {
		URF_SET_ERROR(ctx, "deflateReset", ret);
		return false;
	}

	return xprintf(ctx, ">\nrestore\n") && xprintf(ctx, "showpage\n");
}

static bool doc_end(struct urf_context *ctx)
{
	return xprintf(ctx, "%%%%EOF\n");
}

struct urf_conv_ops urf_postscript_ops = {
	.context_setup = &context_setup,
	.context_cleanup = &context_cleanup,
	.doc_begin = &doc_begin,
	.page_begin = &page_begin,
	.rast_begin = &rast_begin,
	//.rast_lines_raw = &rast_lines_raw,
	.rast_lines = &rast_lines,
	.page_end = &page_end,
	.doc_end = &doc_end,
	.id = "postscript"
};
