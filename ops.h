#ifndef URFTOPS_OPS_H
#define URFTOPS_OPS_H
#include "urf.h"

typedef int (*urf_op_doc_begin)(struct urf_header *hdr, void *context);
typedef int (*urf_op_page_begin)(struct urf_page_header *hdr, unsigned pagenum,
		void *context);
typedef int (*urf_op_rast_begin)(struct urf_page_header *hdr, unsigned pagenum,
		void *context);
/** 
 * callback for pixel data.
 *
 * @param bpp      bits per pixel
 * @param linelen  line length
 * @param linerep  line repeat count
 * @param pixrep   pixel repeat count
 * @param data     pixel data
 * @param pixcount pixel count in 'data'
 */
typedef int (*urf_op_rast_pixels)(struct urf_page_header *hdr, 
		unsigned linelen, unsigned linerep, unsigned pixrep, uint8_t *data,
		unsigned pixcount, void *context);
typedef int (*urf_op_rast_end)(struct urf_page_header *hdr, unsigned pagenum,
		void *context);
typedef int (*urf_op_page_end)(struct urf_page_header *hdr, unsigned pagenum,
		void *context);
typedef int (*urf_op_doc_end)(struct urf_header *hdr, void *context);

struct urf_ops
{
	urf_op_doc_begin doc_begin;
	urf_op_page_begin page_begin;
	urf_op_rast_begin rast_begin;
	urf_op_rast_pixels rast_pixels;
	urf_op_page_end page_end;
	urf_op_doc_end doc_end;
	char id[32];
};

#endif
