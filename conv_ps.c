#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <zlib.h>
#include "urf.h"

#define VERSION "0.1"

//#define NODEFLATE

#define ASCII85 0

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

static uint32_t to_word(unsigned char *buf, size_t len)
{
	uint32_t word = 0;

	switch (len) {
		default:
		case 4:
			word |= (buf[3] & 0xff) << 24;
			// fall through
		case 3:
			word |= (buf[2] & 0xff) << 16;
			// fall through
		case 2:
			word |= (buf[1] & 0xff) << 8;
			// fall through
		case 1:
			word |= (buf[0] & 0xff);
			// fall through
		case 0:
			break;
	}

	return ntohl(word);
}

static bool xputc(struct urf_context *ctx, char c)
{
	if (IMPL(ctx)->idx && !(IMPL(ctx)->idx % (ASCII85 ? 72 : 36))) {
		if (!xprintf(ctx, "\n")) {
			return false;
		}
	}

	++IMPL(ctx)->idx;
#if ASCII85 == 1
	return xprintf(ctx, "%c", c);
#else
	return xprintf(ctx, "%02x", c & 0xff);
#endif
}

static bool xprint85(struct urf_context *ctx, unsigned char *buf, size_t len)
{
	char out[5];

	while (len) {
		size_t i, n = len < 4 ? len : 4;
		uint32_t word = to_word(buf, n);

		if (!word && n == 4) {
			if (!xputc(ctx, 'z')) {
				return false;
			}
		} else {
			size_t i = 0;
			for (i = 0; i < 5; ++i) {
				out[i] = '!' + (word % 85);
				word /= 85;
			}

			for (i = 4; i >= (4 - n); --i) {
				if (!xputc(ctx, out[i])) {
					return false;
				}

				if (!i) {
					break;
				}
			}
		}

		len -= n;
		buf += n;
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
			"<<\n"
			"  /ImageType 1\n"
			"  /Width %" PRIu32 "\n"
			"  /Height %" PRIu32 "\n"
			//"  /ImageMatrix [ %" PRIu32 " 0 0 -%" PRIu32 " 0 %" PRIu32 " ]\n"
			"  /ImageMatrix [ 1 0 0 -1 0 %" PRIu32 " ]\n"
			"  /BitsPerComponent 8\n"
			"  /Interpolate true\n"
			"  /Decode [ 0 1 0 1 0 1 ]\n"
			"  /DataSource currentfile\n"
			//"    /ASCIIHexDecode filter\n"
#if ASCII85 == 1
			"  /ASCII85Decode filter\n"
#else
			"  /ASCIIHexDecode filter\n"
#endif
#ifndef NODEFLATE
			"  /FlateDecode filter\n"
#endif
			">> image\n",
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
#if ASCII85 == 1
	return xprint85(ctx, ctx->line_data, ctx->page_line_bytes);
#else
	size_t i = 0;
	for (; i < ctx->page_line_bytes; ++i) {
		if (!xputc(ctx, ctx->line_data[i])) {
			return false;
		}
	}

	return true;
#endif
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
			size_t have = IMPL(ctx)->zlen - strm->avail_out;
			if (have) {
#if ASCII85 == 1
				if (!xprint85(ctx, IMPL(ctx)->zbuf, have)) {
					return false;
				}
#else
				size_t i = 0;
				for (; i < have; ++i) {
					if (!xputc(ctx, IMPL(ctx)->zbuf[i])) {
						return false;
					}
				}
#endif
			}
		} else {
			URF_SET_ERROR(ctx, "deflate", Z_ERRNO);
			return false;
		}
	} while (strm->avail_out == 0);

	return true;
#endif
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

	return xprintf(ctx, "\n%s\nrestore\n", ASCII85 ? "~>" : ">") && xprintf(ctx, "showpage\n");
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
