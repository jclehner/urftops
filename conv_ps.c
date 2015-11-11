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

#define log fprintf
#define LOG_DBG stderr
#define LOG_ERR stderr

#define CHUNK (16 * 1024)

#define IMPL(ctx) ((struct impl *)ctx->impl)

struct impl
{
	FILE *fp;
	unsigned char *buf_z;
	unsigned char *buf_85;
	size_t len_85;
	z_stream strm;
};

static size_t encode_85(unsigned char *dest, const unsigned char *src, size_t n)
{
	size_t s = 0;

	while (n) {
		unsigned acc = 0;
		int cnt;
		for (cnt = 24; cnt >= 0; cnt -= 8) {
			unsigned ch = *src++;
			acc |= ch << cnt;
			if (!--n) {
				break;
			}
		}

		if (acc) {
			for (cnt = 4; cnt >= 0; --cnt) {
				int val = acc % 85;
				acc /= 85;
				(dest + s)[cnt] = '!' + val;
			}
			s += 5;
		} else {
			*(dest + s) = 'z';
			++s;
		}
	}

	for (; s % 5; ++s) {
		*(dest + s) = 'u';
	}

	*(dest + s) = '\0';

	return s;
}

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

static bool write85(struct urf_context *ctx, unsigned char *buffer, size_t size)
{
	FILE *fp = IMPL(ctx)->fp;
	
	while (size) {
		size_t bytes = MIN(size, 69 - IMPL(ctx)->len_85);
		if (!bytes) {
			if (fwrite("\n ", 1, 2, fp) != 2) {
				URF_SET_ERRNO(ctx, "fwrite");
				return false;
			}

			IMPL(ctx)->len_85 = 0;
			continue;
		}

		if (fwrite(buffer, 1, bytes, fp) != bytes || ferror(fp)) {
			URF_SET_ERRNO(ctx, "fwrite");
			return false;
		}

		size -= bytes;
		buffer += bytes;
		IMPL(ctx)->len_85 += bytes;
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

	impl->buf_z = impl->buf_85 = NULL;

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

static bool context_cleanup(struct urf_context *ctx)
{
	struct impl *impl = IMPL(ctx);

	if (impl) {
		deflateEnd(&impl->strm);
		fclose(impl->fp);
		free(impl->buf_z);
		free(impl->buf_85);
		free(impl);
	}

	return true;
}

static bool doc_begin(struct urf_context *ctx)
{
	return xprintf(ctx,
			"%%!PS-Adobe-2.0\n"
			"%%%%LanguageLevel: 2\n"
			"%%%%Creator: urftops " VERSION "\n"
			"%%%%Title: unknown\n"
			"%%%%Pages: %u\n"
			"%%%%DocumentData: Clean7Bit\n"
			"%%%%EndComments\n" 
			"%%%%EndProlog\n",
			ctx->file_hdr->pages, ctx->page1_hdr->height, 
			ctx->page1_hdr->width, ctx->page1_hdr->dpi);
}

static bool page_begin(struct urf_context *ctx)
{
	if (ctx->page_hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "unsupported bpp", -ctx->page_hdr->bpp);
		return false;
	}

	if (!buf_realloc(ctx, &IMPL(ctx)->buf_z, ctx->page_line_bytes)) {
		return false;
	}

	if (!buf_realloc(ctx, &IMPL(ctx)->buf_85, ctx->page_line_bytes)) {
		return false;
	}

	IMPL(ctx)->len_85 = 0;

	return xprintf(ctx, 
			"%%%%Page: %" PRIu32 " %" PRIu32 "\n"
			"/DeviceRGB setcolorspace\n"
			"500 500 scale\n"
			"300 300 translate\n"
			"8 dict dup begin\n"
			"  /ImageType 1 def\n"
			"  /Width %" PRIu32 " def\n"
			"  /Height %" PRIu32 " def\n"
			"  /Interpolate true def\n"
			"  /BitsPerComponent 8 def\n"
			"  /Decode [ 0 1 0 1 0 1 ] def\n"
			"  /DataSource currentfile /ASCII85Decode filter /FlateDecode filter def\n"
			"  /ImageMatrix [ 1 0 0 -1 0 %" PRIu32 " ] def\n"
			"end\n"
			"image\n"
			" ",
			ctx->page_n, ctx->page_n, ctx->page_hdr->width,
			ctx->page_hdr->height, ctx->page_hdr->height);
}

static bool rast_begin(struct urf_context *ctx)
{
	return true;
}

static bool rast_lines(struct urf_context *ctx)
{
	z_stream *strm = &IMPL(ctx)->strm;
	size_t last = ctx->line_n + ctx->line_repeat;
	int flush = Z_NO_FLUSH;

	do {
		strm->avail_in = ctx->page_line_bytes;
		strm->next_in = ctx->line_data;

		if (ctx->line_n == ctx->page_hdr->height) {
			if (ctx->page_n == ctx->file_hdr->pages) {
				flush = Z_FINISH;
			} else {
				flush = Z_FULL_FLUSH;
			}
		} else if (ctx->line_n == last) {
			//flush = Z_PARTIAL_FLUSH;
			flush = Z_FULL_FLUSH;
		}

		do {
			strm->avail_out = ctx->page_line_bytes;
			strm->next_out = IMPL(ctx)->buf_z;

			int ret = deflate(strm, flush);
			if (ret < 0 && ret != Z_BUF_ERROR) {
				URF_SET_ERROR(ctx, "deflate", ret);
				return false;
			}

			size_t have = ctx->page_line_bytes - strm->avail_out;
			if (have) {
				have = encode_85(IMPL(ctx)->buf_85, IMPL(ctx)->buf_z, have);
				if (!write85(ctx, IMPL(ctx)->buf_85, have)) {
					return false;
				}
			}
		} while (strm->avail_out == 0);

		++ctx->line_n;
	} while (ctx->line_repeat--);

	return true;
}

static bool rast_lines_(struct urf_context *ctx)
{
	do {
		size_t k;
		for (k = 0; k != ctx->page_line_bytes; k += ctx->page_pixel_bytes) {
			int r = ctx->line_data[k] & 0xff;
			int g = ctx->line_data[k + 1] & 0xff;
			int b = ctx->line_data[k + 2] & 0xff;

			if (k && k % (ctx->page_pixel_bytes * 10) == 0) {
				if (!xprintf(ctx, "\n")) {
					return false;
				}
			}

			if (!xprintf(ctx, "%02x%02x%02x", r, g, b)) {
				return false;
			}
		}
		++ctx->line_n;
	} while (ctx->line_repeat--);

	return xprintf(ctx, "\n");
}

static bool rast_lines_raw(struct urf_context *ctx)
{
	size_t i, k;

	for (i = 0; i <= ctx->line_repeat; ++i, ++ctx->line_n) {
		size_t bytes = 0;
		for (k = 0; k < ctx->line_raw_bytes;) {
			uint8_t code = ctx->line_data[k++];
			if (code == 0x80) {
				ssize_t remaining = ctx->page_line_bytes - bytes;
				if (remaining % ctx->page_pixel_bytes) {
					URF_SET_ERROR(ctx, "invalid remaining pixel count", -remaining);
				}

				remaining /= ctx->page_pixel_bytes;

				while (remaining > 0) {
					size_t count = MIN(remaining, 128);
					if (!xprintf(ctx, "%02x ", PACK_CODE_REPEAT(count) & 0xff)) {
						return false;
					}

					size_t l = 0;
					for (; l != ctx->page_pixel_bytes; ++l) {
						if (!xprintf(ctx, "%02x", ctx->page_fill & 0xff)) {
							return false;
						}
					}

					if (!xprintf(ctx, "\n")) {
						return false;
					}

					remaining -= count;
				}

				break;
			} else {
				int8_t pack_code;

				fprintf(stderr, "%02" PRIx8 ": ", code);

				if (code <= 0x7f) {
					size_t count = 1 + (size_t)code;
					fprintf(stderr, "rep %zu", count);
					pack_code = PACK_CODE_REPEAT(count);
				} else {
					size_t count = 257 - (size_t)code;
					fprintf(stderr, "cpy %zu", count);
					pack_code = PACK_CODE_COPY(count);
				}

				fprintf(stderr, " -> %" PRId8 "\n", pack_code);

				size_t end = k + ctx->page_pixel_bytes * 
						(code <= 0x7f ? 1 : 257 - (size_t)code);

				bytes += (code <= 0x7f ? 1 + (size_t)code : 257 - (size_t)code);

				//fprintf(stderr, "code=%" PRIu8 "", code);
				// Codes are reversed in the original PackBits algorithm

				if (!xprintf(ctx, "%02x ", pack_code & 0xff)) {
					return false;
				}

				for (; k != end; ++k) {
					if (!xprintf(ctx, "%02x", ctx->line_data[k] & 0xff)) {
						return false;
					}
				}

				if (!xprintf(ctx, "\n")) {
					return false;
				}
			}
		}

		if (!xprintf(ctx, "\n")) {
			return false;
		}
	}

	return true;

}

static bool rast_end(struct urf_context *ctx)
{
	return xprintf(ctx, "\n~>\n");
}

static bool page_end(struct urf_context *ctx)
{
	return xprintf(ctx, "\n\nshowpage\n");
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
	.rast_end = &rast_end,
	.page_end = &page_end,
	.doc_end = &doc_end,
	.id = "postscript"
};
