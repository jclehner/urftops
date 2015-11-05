#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "urf.h"

#define log fprintf
#define LOG_ERR stderr
#define LOG_DBG stderr

/*
struct urf_file_header {
	char magic[8];
	uint32_t pages;
} __attribute__((packed));

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
} __attribute__((packed));
*/

#define BSWAP32(x) x = ntohl(x)

#define DUMP_32(x) log(LOG_DBG, "%s=%" PRIu32 "\n", #x, (x))
#define DUMP_8(x) log(LOG_DBG, "%s=%" PRIu8 "\n", #x, (x))

static bool xread(struct urf_context *ctx, void *buffer, size_t size)
{
	ssize_t bytes = read(ctx->ifd, buffer, size);
	if (bytes == size) {
		return true;
	} else if (bytes < 0) {
		URF_SET_ERRNO(ctx, "read: read error");
	} else {
		URF_SET_ERROR(ctx, "read: short read", -1);
	}

	return false;
}

static bool read_file_header(struct urf_context *ctx)
{
	if (!xread(ctx, ctx->file_hdr, sizeof(struct urf_file_header))) {
		return false;
	}

	struct urf_file_header *hdr = ctx->file_hdr;

	if (strncmp(hdr->magic, "UNIRAST", 7)) {
		URF_SET_ERROR(ctx, "unsupported file format", -1);
		return false;
	}

	BSWAP32(hdr->pages);
	DUMP_32(hdr->pages);

	return true;
}

static bool read_page_header(struct urf_context *ctx)
{
	if (!xread(ctx, ctx->page_hdr, sizeof(struct urf_page_header))) {
		return false;
	}

	struct urf_page_header *hdr = ctx->page_hdr;

	DUMP_8(hdr->bpp);
	DUMP_8(hdr->colorspace);
	DUMP_8(hdr->duplex);
	DUMP_8(hdr->quality);

	if (!hdr->bpp || hdr->bpp > 32 || hdr->bpp % 8) {
		URF_SET_ERROR(ctx, "invalid bpp", -1);
		return false;
	}

	if (hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "bpp != 24", -1);
		return false;
	}

	BSWAP32(hdr->unknown0);
	BSWAP32(hdr->unknown1);
	BSWAP32(hdr->width);
	BSWAP32(hdr->height);
	BSWAP32(hdr->dpi);
	BSWAP32(hdr->unknown2);
	BSWAP32(hdr->unknown3);

	DUMP_32(hdr->unknown0);
	DUMP_32(hdr->unknown1);
	DUMP_32(hdr->width);
	DUMP_32(hdr->height);
	DUMP_32(hdr->dpi);
	DUMP_32(hdr->unknown2);
	DUMP_32(hdr->unknown3);

	return true;
}

static bool read_page_line(struct urf_context *ctx)
{
	size_t n = 0;
	while (n < ctx->page_line_bytes) {
		int8_t code;
		if (!xread(ctx, &code, 1)) {
			return false;
		}

		size_t ppb = ctx->page_pixel_bytes;

		if (code >= 0 && code <= 127) {
			// repeat next pixel (1 + code) times
			if (!xread(ctx, ctx->page_line + n, ppb)) {
				return false;
			}

			n += ppb;

			size_t repeat = 1 + (size_t) code;
			size_t i = 0;

			for (; i != repeat; ++i) {
				char *dest = ctx->page_line + n;
				// copy the previous pixel to the current one
				memcpy(dest, dest - ppb, ppb);
				n += ppb;
			}
		} else if (code >= -127 && code < 0) {
			// copy next (-code + 1) pixels
			size_t count = ((size_t)-code + 1) * ppb;
			if (!xread(ctx, ctx->page_line + n, count)) {
				return false;
			}
		} else {
			// fill rest of line with all-white pixels
			size_t count = (ctx->page_line_bytes - n) * ppb;
			log(LOG_DBG, "fill: count=%zu\n", count);
			memset(ctx->page_line + n, ctx->page_fill, count);
			n += count;
		} 
	}

	return true;
}

static bool op_call(bool (*func)(struct urf_context *), const char *id,
		const char *name, struct urf_context *ctx, struct urf_error *error)
{
	if (func) {
		ctx->error->code = 0;
		if (!func(ctx)) {
			log(LOG_ERR, "%16s: %s: ", id, name);

			if (ctx->error->code > 0) {
				log(LOG_ERR, "%s\n", strerror(ctx->error->code));
			} else {
				log(LOG_ERR, "error %d\n", ctx->error->code);
			}

			if (error) {
				memcpy(error, ctx->error, sizeof(struct urf_error));
			}
			
			return false;
		}
	}

	return true;
}

int urf_convert(int ifd, int ofd, struct urf_conv_ops *ops, void *arg)
{
	struct urf_context ctx;
	struct urf_file_header file_hdr;
	struct urf_page_header page1_hdr;
	struct urf_page_header page_hdr;
	struct urf_error error, saved_error;

	ops->id[15] = '\0';

	error.code = saved_error.code = 0;
	error.msg = saved_error.msg = NULL;

	ctx.ifd = ifd;
	ctx.ofd = ofd;
	ctx.error = &error;
	ctx.page_line = NULL;
	ctx.page_fill = 0xff;
	ctx.file_hdr = &file_hdr;
	ctx.page1_hdr = &page1_hdr;
	ctx.page_hdr = &page_hdr;

	if(!read_file_header(&ctx)) {
		goto bailout;
	}

	if (!read_page_header(&ctx)) {
		goto bailout;
	}

	fprintf(stderr, "** 3\n");

	memcpy(&page1_hdr, &page_hdr, sizeof(struct urf_page_header));

	ctx.page_pixel_bytes = page1_hdr.bpp / 8;
	ctx.page_line_bytes = ctx.page_pixel_bytes * page1_hdr.width;
	ctx.page_lines = page1_hdr.height;

	ctx.page_n = 1;

	ctx.page_line = malloc(ctx.page_line_bytes);
	if (!ctx.page_line) {
		URF_SET_ERRNO(&ctx, "malloc");
		goto bailout;
	}

	if (ops->context_setup) {
		if (!ops->context_setup(&ctx, arg)) {
			goto bailout;
		}
	}

#define OP_CALL(func) op_call(ops->func, ops->id, #func, &ctx, &saved_error)
#define OP_CALL_NO_ERR(func) op_call(ops->func, ops->id, #func, &ctx, NULL)

	if (!OP_CALL(doc_begin)) {
		goto bailout_context_cleanup;
	}

	do {
		if (!OP_CALL(page_begin)) {
			goto bailout_doc_end;
		}

		if (!OP_CALL(rast_begin)) {
			goto bailout_page_end;
		}

		size_t line = 0;
		while (line < ctx.page_lines) {
			uint8_t repeat;
			if (!xread(&ctx, &repeat, 1)) {
				break;
			}

			do {
				if (!read_page_line(&ctx)) {
					goto bailout_rast_end;
				}

				if (!OP_CALL(rast_line)) {
					goto bailout_rast_end;
				}
			} while (repeat--);
		}

		if (!OP_CALL(rast_end)) {
			goto bailout_page_end;
		}

		if (!OP_CALL(page_end)) {
			goto bailout_doc_end;
		}

		if (++ctx.page_n == file_hdr.pages) {
			break;
		}

	} while (read_page_header(&ctx));

	if (ctx.page_n == file_hdr.pages) {
		if (!OP_CALL(doc_end)) {
			goto bailout_context_cleanup;
		}

		if (OP_CALL(context_cleanup)) {
			return 0;
		}
	}

	goto bailout;

bailout_rast_end:
	OP_CALL_NO_ERR(rast_end);
bailout_page_end:
	OP_CALL_NO_ERR(page_end);
bailout_doc_end:
	OP_CALL_NO_ERR(doc_end);
bailout_context_cleanup:
	OP_CALL_NO_ERR(context_cleanup);
bailout:
	;

	struct urf_error *last_error = saved_error.code ?
			&saved_error : &error;

	if (last_error->code > 0) {
		log(LOG_ERR, "%s: %s: %s\n", ops->id, last_error->msg,
				strerror(last_error->code));
	} else {
		log(LOG_ERR, "%s: %s: error %d\n", ops->id, last_error->msg,
				last_error->code);
	}

	return last_error->code;
}

