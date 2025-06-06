// Copyright (C) 2004-2025 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
// CA 94129, USA, for further information.

#ifndef MUPDF_PDF_RESOURCE_H
#define MUPDF_PDF_RESOURCE_H

#include "mupdf/fitz/font.h"
#include "mupdf/fitz/image.h"
#include "mupdf/fitz/shade.h"
#include "mupdf/fitz/store.h"
#include "mupdf/pdf/object.h"

void pdf_store_item(fz_context *ctx, pdf_obj *key, void *val, size_t itemsize);
void *pdf_find_item(fz_context *ctx, fz_store_drop_fn *drop, pdf_obj *key);
void pdf_remove_item(fz_context *ctx, fz_store_drop_fn *drop, pdf_obj *key);
void pdf_empty_store(fz_context *ctx, pdf_document *doc);
void pdf_purge_locals_from_store(fz_context *ctx, pdf_document *doc);
void pdf_purge_object_from_store(fz_context *ctx, pdf_document *doc, int num);

/*
 * Structures used for managing resource locations and avoiding multiple
 * occurrences when resources are added to the document. The search for existing
 * resources will be performed when we are first trying to add an item. Object
 * refs are stored in a fz_hash_table structure using a hash of the md5 sum of
 * the data, enabling rapid lookup.
 */

enum { PDF_SIMPLE_FONT_RESOURCE=1, PDF_CID_FONT_RESOURCE=2, PDF_CJK_FONT_RESOURCE=3 };
enum { PDF_SIMPLE_ENCODING_LATIN, PDF_SIMPLE_ENCODING_GREEK, PDF_SIMPLE_ENCODING_CYRILLIC };

/* The contents of this structure are defined publicly just so we can
 * define this on the stack. */
typedef struct
{
	unsigned char digest[16];
	int type;
	int encoding;
	int local_xref;
} pdf_font_resource_key;

typedef struct
{
	unsigned char digest[16];
	int local_xref;
} pdf_colorspace_resource_key;

pdf_obj *pdf_find_font_resource(fz_context *ctx, pdf_document *doc, int type, int encoding, fz_font *item, pdf_font_resource_key *key);
pdf_obj *pdf_insert_font_resource(fz_context *ctx, pdf_document *doc, pdf_font_resource_key *key, pdf_obj *obj);
pdf_obj *pdf_find_colorspace_resource(fz_context *ctx, pdf_document *doc, fz_colorspace *item, pdf_colorspace_resource_key *key);
pdf_obj *pdf_insert_colorspace_resource(fz_context *ctx, pdf_document *doc, pdf_colorspace_resource_key *key, pdf_obj *obj);
void pdf_drop_resource_tables(fz_context *ctx, pdf_document *doc);
void pdf_purge_local_resources(fz_context *ctx, pdf_document *doc);

typedef struct pdf_function pdf_function;

void pdf_eval_function(fz_context *ctx, pdf_function *func, const float *in, int inlen, float *out, int outlen);
pdf_function *pdf_keep_function(fz_context *ctx, pdf_function *func);
void pdf_drop_function(fz_context *ctx, pdf_function *func);
size_t pdf_function_size(fz_context *ctx, pdf_function *func);
pdf_function *pdf_load_function(fz_context *ctx, pdf_obj *ref, int in, int out);

fz_colorspace *pdf_document_output_intent(fz_context *ctx, pdf_document *doc);
fz_colorspace *pdf_load_colorspace(fz_context *ctx, pdf_obj *obj);
int pdf_is_tint_colorspace(fz_context *ctx, fz_colorspace *cs);

fz_shade *pdf_load_shading(fz_context *ctx, pdf_document *doc, pdf_obj *obj);
void pdf_sample_shade_function(fz_context *ctx, float *samples, int n, int funcs, pdf_function **func, float t0, float t1);

/**
	Function to recolor a single color from a shade.
*/
typedef void (pdf_recolor_vertex)(fz_context *ctx, void *opaque, fz_colorspace *dst_cs, float *d, fz_colorspace *src_cs, const float *src);

/**
	Function to handle recoloring a shade.

	Called with src_cs from the shade. If no recoloring is required, return NULL. Otherwise
	fill in *dst_cs, and return a vertex recolorer.
*/
typedef pdf_recolor_vertex *(pdf_shade_recolorer)(fz_context *ctx, void *opaque, fz_colorspace *src_cs, fz_colorspace **dst_cs);

/**
	Recolor a shade.
*/
pdf_obj *pdf_recolor_shade(fz_context *ctx, pdf_obj *shade, pdf_shade_recolorer *reshade, void *opaque);

fz_image *pdf_load_inline_image(fz_context *ctx, pdf_document *doc, pdf_obj *rdb, pdf_obj *dict, fz_stream *file);
int pdf_is_jpx_image(fz_context *ctx, pdf_obj *dict);

fz_image *pdf_load_image(fz_context *ctx, pdf_document *doc, pdf_obj *obj);

pdf_obj *pdf_add_image(fz_context *ctx, pdf_document *doc, fz_image *image);

pdf_obj *pdf_add_colorspace(fz_context *ctx, pdf_document *doc, fz_colorspace *cs);

typedef struct
{
	fz_storable storable;
	int ismask;
	float xstep;
	float ystep;
	fz_matrix matrix;
	fz_rect bbox;
	pdf_document *document;
	pdf_obj *resources;
	pdf_obj *contents;
	int id; /* unique ID for caching rendered tiles */
} pdf_pattern;

pdf_pattern *pdf_load_pattern(fz_context *ctx, pdf_document *doc, pdf_obj *obj);
pdf_pattern *pdf_keep_pattern(fz_context *ctx, pdf_pattern *pat);
void pdf_drop_pattern(fz_context *ctx, pdf_pattern *pat);

pdf_obj *pdf_new_xobject(fz_context *ctx, pdf_document *doc, fz_rect bbox, fz_matrix matrix, pdf_obj *res, fz_buffer *buffer);
void pdf_update_xobject(fz_context *ctx, pdf_document *doc, pdf_obj *xobj, fz_rect bbox, fz_matrix mat, pdf_obj *res, fz_buffer *buffer);

pdf_obj *pdf_xobject_resources(fz_context *ctx, pdf_obj *xobj);
fz_rect pdf_xobject_bbox(fz_context *ctx, pdf_obj *xobj);
fz_matrix pdf_xobject_matrix(fz_context *ctx, pdf_obj *xobj);
int pdf_xobject_isolated(fz_context *ctx, pdf_obj *xobj);
int pdf_xobject_knockout(fz_context *ctx, pdf_obj *xobj);
int pdf_xobject_transparency(fz_context *ctx, pdf_obj *xobj);
fz_colorspace *pdf_xobject_colorspace(fz_context *ctx, pdf_obj *xobj);

#endif
