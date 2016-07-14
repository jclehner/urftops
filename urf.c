#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include "urf.h"

#define log fprintf
#define LOG_ERR stderr
#define LOG_DBG stderr

#define SWAP32(x) x = ntohl(x)

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

	SWAP32(hdr->pages);

	return true;
}

static bool read_page_header(struct urf_context *ctx)
{
	if (!xread(ctx, ctx->page_hdr, sizeof(struct urf_page_header))) {
		return false;
	}

	struct urf_page_header *hdr = ctx->page_hdr;

	if (!hdr->bpp || hdr->bpp > 32 || hdr->bpp % 8) {
		URF_SET_ERROR(ctx, "invalid bpp", -hdr->bpp);
		return false;
	}

	if (hdr->bpp != 24) {
		URF_SET_ERROR(ctx, "unsupported bpp", -hdr->bpp);
		return false;
	}

	if (hdr->colorspace != 1) {
		URF_SET_ERROR(ctx, "unsupported colorspace", -hdr->colorspace);
		return false;
	}

	SWAP32(hdr->unknown0);
	SWAP32(hdr->unknown1);
	SWAP32(hdr->width);
	SWAP32(hdr->height);
	SWAP32(hdr->dpi);
	SWAP32(hdr->unknown2);
	SWAP32(hdr->unknown3);

	return true;
}

static bool read_page_line(struct urf_context *ctx, bool raw)
{
	size_t n = 0, k = 0;

	ctx->line_raw_bytes = 0;

#ifdef URF_DEBUG
	fprintf(stderr, ">> line %zu", ctx->line_n - 1);
	if (ctx->line_repeat) {
		fprintf(stderr, " - %zu", ctx->line_n + ctx->line_repeat - 1);
	}
	fprintf(stderr, "\n");	
#endif

	while (n < ctx->page_line_bytes) {
		uint8_t code;
		if (!xread(ctx, &code, 1)) {
			return false;
		}

		if (raw) {
			ctx->line_data[ctx->line_raw_bytes++] = code;
		}

#ifdef URF_DEBUG
		fprintf(stderr, "  % 5zu |", n / 3);
#endif
		size_t ppb = ctx->page_pixel_bytes;

		if (code == 0x80) {
			// fill rest of line with all-white pixels
			size_t bytes = ctx->page_line_bytes - n;
			if (!raw) {
				memset(ctx->line_data + n, ctx->page_fill, bytes);
			}
			n += bytes;
#ifdef URF_DEBUG
			fprintf(stderr, "  %1$ 5zu <%2$02x %2$02x %2$02x>\n", bytes / ppb, ctx->page_fill & 0xff);
#endif
		} else if (code <= 0x7f) {
			char *pixel = ctx->line_data + (!raw ? n : ctx->line_raw_bytes);

			// repeat next pixel (1 + code) times
			if (!xread(ctx, pixel, ppb)) {
				//log(LOG_DBG, "fill (err)\n");
				return false;
			}

			size_t count = 1 + (size_t)code;
			size_t i;
			
#ifdef URF_DEBUG
			
			fprintf(stderr, "  % 5zu <", count);
			for (i = 0; i != ppb; ++i) {
				fprintf(stderr, "%s%02x", i ? " " : "", 
						ctx->line_data[n + i] & 0xff);
			}
			fprintf(stderr, ">\n");
#endif

			if (!raw) {
				for (i = 0; i != count; ++i) {
					char *dest = ctx->line_data + n + ppb;
					// copy the previous pixel to the current one
					memcpy(dest, dest - ppb, ppb);
					n += ppb;
				}
			} else {
				ctx->line_raw_bytes += ppb;
				n += count * ppb;
			}
		} else {
			char *pixels = ctx->line_data + (!raw ? n : ctx->line_raw_bytes);
			// copy next (257 - code) pixels
			size_t count = (257 - (size_t)code);
			if (!xread(ctx, pixels, count * ppb)) {
				return false;
			}

			n += count * ppb;

			if (raw) {
				ctx->line_raw_bytes += ppb * count;
			}

#ifdef URF_DEBUG
			fprintf(stderr, "        ");
			size_t i, k;
			for (i = 0; i != count; ++i) {
				fprintf(stderr, "<");
				for (k = 0; k != ppb; ++k) {
					fprintf(stderr, "%s%02x", k ? " " : "",
							ctx->line_data[i * ppb + k] & 0xff);
				}
				fprintf(stderr, "> ");
			}
			fprintf(stderr, "\n");
#endif
		}
	}

#ifdef URF_DEBUG
	fprintf(stderr, "\n");
#endif

	if (n != ctx->page_line_bytes) {
		log(LOG_ERR, "n (%zu) >= page_line_bytes (%zu)\n", n, ctx->page_line_bytes);
		return false;
	}

	return true;
}

static bool op_call(bool (*func)(struct urf_context *), const char *id,
		const char *name, struct urf_context *ctx, struct urf_error *error)
{
	if (func) {
		ctx->error->code = 0;
		if (!func(ctx)) {
			log(LOG_ERR, "%s: %s: ", id, name);

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

static void cleanup(struct urf_context *ctx, struct urf_conv_ops *ops)
{
	if (ops->context_cleanup) {
		ops->context_cleanup(ctx);
	}
	
	ctx->impl = NULL;
	free(ctx->line_data);
	ctx->line_data = NULL;
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
	ctx.line_data = NULL;
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

	memcpy(&page1_hdr, &page_hdr, sizeof(struct urf_page_header));

	ctx.page_pixel_bytes = page1_hdr.bpp / 8;
	ctx.page_line_bytes = ctx.page_pixel_bytes * page1_hdr.width;

	ctx.page_n = 1;

	ctx.line_data = malloc(ctx.page_line_bytes);
	if (!ctx.line_data) {
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

		ctx.line_n = 1;

		while (ctx.line_n <= ctx.page_hdr->height) {
			if (!xread(&ctx, &ctx.line_repeat, 1)) {
				break;
			}

			if (!read_page_line(&ctx, ops->rast_lines_raw)) {
				goto bailout_rast_end;
			}

			if (!OP_CALL(rast_lines_raw)) {
				goto bailout_rast_end;
			}

			if (!OP_CALL(rast_lines)) {
				goto bailout_rast_end;
			}
		}

		if (!OP_CALL(rast_end)) {
			goto bailout_page_end;
		}

		if (!OP_CALL(page_end)) {
			goto bailout_doc_end;
		}

		if (++ctx.page_n > file_hdr.pages) {
			break;
		}

	} while (read_page_header(&ctx));

	if (!OP_CALL(doc_end)) {
		goto bailout_context_cleanup;
	}

	cleanup(&ctx, ops);
	return 0;

bailout_rast_end:
	OP_CALL_NO_ERR(rast_end);
bailout_page_end:
	OP_CALL_NO_ERR(page_end);
bailout_doc_end:
	OP_CALL_NO_ERR(doc_end);
bailout_context_cleanup:
	cleanup(&ctx, ops);
bailout:
	free(ctx.line_data);

#undef OP_CALL
#undef OP_CALL_NO_ERR

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

