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

#include "mupdf/fitz.h"
#include "pdf-imp.h"

#include <string.h>

/* Scan file for objects and reconstruct xref table */

struct entry
{
	int num;
	int gen;
	int64_t ofs;
	int64_t stm_ofs;
	int64_t stm_len;
};

typedef struct
{
	int max;
	int len;
	pdf_obj **roots;
} pdf_root_list;

static void
add_root(fz_context *ctx, pdf_root_list *roots, pdf_obj *obj)
{
	if (roots->max == roots->len)
	{
		int new_max_roots = roots->max * 2;
		if (new_max_roots == 0)
			new_max_roots = 4;
		roots->roots = fz_realloc(ctx, roots->roots, new_max_roots * sizeof(roots->roots[0]));
		roots->max = new_max_roots;
	}
	roots->roots[roots->len] = pdf_keep_obj(ctx, obj);
	roots->len++;
}

static pdf_root_list *
fz_new_root_list(fz_context *ctx)
{
	return fz_malloc_struct(ctx, pdf_root_list);
}

static void
pdf_drop_root_list(fz_context *ctx, pdf_root_list *roots)
{
	int i, n;

	if (roots == NULL)
		return;

	n = roots->len;
	for (i = 0; i < n; i++)
		pdf_drop_obj(ctx, roots->roots[i]);
	fz_free(ctx, roots->roots);
	fz_free(ctx, roots);
}

int
pdf_repair_obj(fz_context *ctx, pdf_document *doc, pdf_lexbuf *buf, int64_t *stmofsp, int64_t *stmlenp, pdf_obj **encrypt, pdf_obj **id, pdf_obj **page, int64_t *tmpofs, pdf_obj **root)
{
	fz_stream *file = doc->file;
	pdf_token tok;
	int64_t stm_len;
	int64_t local_ofs;

	if (tmpofs == NULL)
		tmpofs = &local_ofs;
	if (stmofsp == NULL)
		stmofsp = &local_ofs;

	*stmofsp = 0;
	if (stmlenp)
		*stmlenp = -1;

	stm_len = 0;

	*tmpofs = fz_tell(ctx, file);
	if (*tmpofs < 0)
		fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");

	/* On entry to this function, we know that we've just seen
	 * '<int> <int> obj'. We expect the next thing we see to be a
	 * pdf object. Regardless of the type of thing we meet next
	 * we only need to fully parse it if it is a dictionary. */
	tok = pdf_lex(ctx, file, buf);

	/* Don't let a truncated object at EOF overwrite a good one */
	if (tok == PDF_TOK_EOF)
		fz_throw(ctx, FZ_ERROR_SYNTAX, "truncated object");

	if (tok == PDF_TOK_OPEN_DICT)
	{
		pdf_obj *obj, *dict = NULL;

		fz_try(ctx)
		{
			dict = pdf_parse_dict(ctx, doc, file, buf);
		}
		fz_catch(ctx)
		{
			fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
			fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
			/* Don't let a broken object at EOF overwrite a good one */
			if (file->eof)
				fz_rethrow(ctx);
			/* Silently swallow the error */
			fz_report_error(ctx);
			dict = pdf_new_dict(ctx, doc, 2);
		}

		/* We must be careful not to try to resolve any indirections
		 * here. We have just read dict, so we know it to be a non
		 * indirected dictionary. Before we look at any values that
		 * we get back from looking up in it, we need to check they
		 * aren't indirected. */

		if (encrypt || id || root)
		{
			obj = pdf_dict_get(ctx, dict, PDF_NAME(Type));
			if (!pdf_is_indirect(ctx, obj) && pdf_name_eq(ctx, obj, PDF_NAME(XRef)))
			{
				if (encrypt)
				{
					obj = pdf_dict_get(ctx, dict, PDF_NAME(Encrypt));
					if (obj)
					{
						pdf_drop_obj(ctx, *encrypt);
						*encrypt = pdf_keep_obj(ctx, obj);
					}
				}

				if (id)
				{
					obj = pdf_dict_get(ctx, dict, PDF_NAME(ID));
					if (obj)
					{
						pdf_drop_obj(ctx, *id);
						*id = pdf_keep_obj(ctx, obj);
					}
				}

				if (root)
					*root = pdf_keep_obj(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Root)));
			}
		}

		obj = pdf_dict_get(ctx, dict, PDF_NAME(Length));
		if (!pdf_is_indirect(ctx, obj) && pdf_is_int(ctx, obj))
			stm_len = pdf_to_int64(ctx, obj);

		if (doc->file_reading_linearly && page)
		{
			obj = pdf_dict_get(ctx, dict, PDF_NAME(Type));
			if (!pdf_is_indirect(ctx, obj) && pdf_name_eq(ctx, obj, PDF_NAME(Page)))
			{
				pdf_drop_obj(ctx, *page);
				*page = pdf_keep_obj(ctx, dict);
			}
		}

		pdf_drop_obj(ctx, dict);
	}

	while ( tok != PDF_TOK_STREAM &&
		tok != PDF_TOK_ENDOBJ &&
		tok != PDF_TOK_ERROR &&
		tok != PDF_TOK_EOF &&
		tok != PDF_TOK_INT )
	{
		*tmpofs = fz_tell(ctx, file);
		if (*tmpofs < 0)
			fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");
		tok = pdf_lex(ctx, file, buf);
	}

	if (tok == PDF_TOK_STREAM)
	{
		int c = fz_read_byte(ctx, file);
		if (c == '\r') {
			c = fz_peek_byte(ctx, file);
			if (c == '\n')
				fz_read_byte(ctx, file);
		}

		*stmofsp = fz_tell(ctx, file);
		if (*stmofsp < 0)
			fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");

		if (stm_len > 0)
		{
			fz_seek(ctx, file, *stmofsp + stm_len, 0);
			fz_try(ctx)
			{
				tok = pdf_lex(ctx, file, buf);
			}
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "cannot find endstream token, falling back to scanning");
			}
			if (tok == PDF_TOK_ENDSTREAM)
				goto atobjend;
			fz_seek(ctx, file, *stmofsp, 0);
		}

		(void)fz_read(ctx, file, (unsigned char *) buf->scratch, 9);

		while (memcmp(buf->scratch, "endstream", 9) != 0)
		{
			c = fz_read_byte(ctx, file);
			if (c == EOF)
				break;
			memmove(&buf->scratch[0], &buf->scratch[1], 8);
			buf->scratch[8] = c;
		}

		if (stmlenp)
			*stmlenp = fz_tell(ctx, file) - *stmofsp - 9;

atobjend:
		*tmpofs = fz_tell(ctx, file);
		if (*tmpofs < 0)
			fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");
		tok = pdf_lex(ctx, file, buf);
		if (tok != PDF_TOK_ENDOBJ)
			fz_warn(ctx, "object missing 'endobj' token");
		else
		{
			/* Read another token as we always return the next one */
			*tmpofs = fz_tell(ctx, file);
			if (*tmpofs < 0)
				fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");
			tok = pdf_lex(ctx, file, buf);
		}
	}
	return tok;
}

static int64_t
entry_offset(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref_entry *entry = pdf_get_populating_xref_entry(ctx, doc, num);

	if (entry->type == 0 || entry->type == 'f')
		return 0;
	if (entry->type == 'n')
		return entry->ofs;
	assert(entry->type == 'o');

	/* It must be in a stream. Return the entry of that stream. */
	entry = pdf_get_populating_xref_entry(ctx, doc, entry->ofs);
	/* If it's NOT in a stream, then we'll invalidate this entry in a moment.
	 * For now, just return an illegal offset. */
	if (entry->type != 'n')
		return -1;

	return entry->ofs;
}

static void
pdf_repair_obj_stm(fz_context *ctx, pdf_document *doc, int stm_num)
{
	pdf_obj *obj;
	fz_stream *stm = NULL;
	pdf_token tok;
	int i, n, count;
	pdf_lexbuf buf;

	fz_var(stm);

	pdf_lexbuf_init(ctx, &buf, PDF_LEXBUF_SMALL);

	fz_try(ctx)
	{
		obj = pdf_load_object(ctx, doc, stm_num);

		count = pdf_dict_get_int(ctx, obj, PDF_NAME(N));

		pdf_drop_obj(ctx, obj);

		stm = pdf_open_stream_number(ctx, doc, stm_num);

		for (i = 0; i < count; i++)
		{
			pdf_xref_entry *entry;
			int replace;

			tok = pdf_lex(ctx, stm, &buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, FZ_ERROR_FORMAT, "corrupt object stream (%d 0 R)", stm_num);

			n = buf.i;
			if (n < 0)
			{
				fz_warn(ctx, "ignoring object with invalid object number (%d %d R)", n, i);
				continue;
			}
			else if (n >= PDF_MAX_OBJECT_NUMBER)
			{
				fz_warn(ctx, "ignoring object with invalid object number (%d %d R)", n, i);
				continue;
			}

			entry = pdf_get_populating_xref_entry(ctx, doc, n);

			/* Bug 708286: Do not allow an object from an ObjStm to override an object
			 * that isn't in an ObjStm that we've already read, that occurs after it
			 * in the file. */
			replace = 1;
			if (entry->type != 0 && entry->type != 'f')
			{
				int64_t existing_entry_offset = entry_offset(ctx, doc, n);

				if (existing_entry_offset < 0)
				{
					/* The existing entry is invalid. Anything must be better than that! */
				}
				else
				{
					int64_t this_entry_offset = entry_offset(ctx, doc, stm_num);

					if (existing_entry_offset > this_entry_offset)
						replace = 0;
				}
			}

			if (replace)
			{
				entry->ofs = stm_num;
				entry->gen = i;
				entry->num = n;
				entry->stm_ofs = 0;
				pdf_drop_obj(ctx, entry->obj);
				entry->obj = NULL;
				entry->type = 'o';
			}

			tok = pdf_lex(ctx, stm, &buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, FZ_ERROR_FORMAT, "corrupt object stream (%d 0 R)", stm_num);
		}
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
		pdf_lexbuf_fin(ctx, &buf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void
orphan_object(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	if (doc->orphans_count == doc->orphans_max)
	{
		int new_max = (doc->orphans_max ? doc->orphans_max*2 : 32);

		fz_try(ctx)
		{
			doc->orphans = fz_realloc_array(ctx, doc->orphans, new_max, pdf_obj*);
			doc->orphans_max = new_max;
		}
		fz_catch(ctx)
		{
			pdf_drop_obj(ctx, obj);
			fz_rethrow(ctx);
		}
	}
	doc->orphans[doc->orphans_count++] = obj;
}

static int is_white(int c)
{
	return c == '\x00' || c == '\x09' || c == '\x0a' || c == '\x0c' || c == '\x0d' || c == '\x20';
}

static pdf_root_list *
pdf_repair_xref_base(fz_context *ctx, pdf_document *doc)
{
	pdf_obj *dict, *obj = NULL;
	pdf_obj *length;

	pdf_obj *encrypt = NULL;
	pdf_obj *id = NULL;
	pdf_obj *info = NULL;
	pdf_root_list *roots = NULL;

	struct entry *list = NULL;
	int listlen;
	int listcap;
	int maxnum = 0;

	int num = 0;
	int gen = 0;
	int64_t tmpofs, stm_ofs, numofs = 0, genofs = 0;
	int64_t stm_len;
	pdf_token tok;
	int next;
	int i;
	size_t j, n;
	int c;
	pdf_lexbuf *buf = &doc->lexbuf.base;

	fz_var(encrypt);
	fz_var(id);
	fz_var(info);
	fz_var(list);
	fz_var(obj);
	fz_var(roots);

	if (!doc->is_fdf)
		fz_warn(ctx, "repairing PDF document");

	if (doc->repair_attempted)
		fz_throw(ctx, FZ_ERROR_FORMAT, "Repair failed already - not trying again");

	doc->bias = 0; // reset bias!

	doc->repair_attempted = 1;
	doc->repair_in_progress = 1;

	pdf_drop_page_tree_internal(ctx, doc);
	doc->page_tree_broken = 0;
	pdf_forget_xref(ctx, doc);

	fz_seek(ctx, doc->file, 0, 0);

	fz_try(ctx)
	{
		pdf_xref_entry *entry;
		listlen = 0;
		listcap = 1024;
		list = fz_malloc_array(ctx, listcap, struct entry);

		roots = fz_new_root_list(ctx);

		/* look for '%PDF' version marker within first kilobyte of file */
		n = fz_read(ctx, doc->file, (unsigned char *)buf->scratch, fz_minz(buf->size, 1024));

		fz_seek(ctx, doc->file, 0, 0);
		if (n >= 5)
		{
			for (j = 0; j < n - 5; j++)
			{
				if (memcmp(&buf->scratch[j], "%PDF-", 5) == 0 || memcmp(&buf->scratch[j], "%FDF-", 5) == 0)
				{
					fz_seek(ctx, doc->file, (int64_t)(j + 8), 0); /* skip "%PDF-X.Y" */
					break;
				}
			}
		}

		/* skip comment line after version marker since some generators
		 * forget to terminate the comment with a newline */
		c = fz_read_byte(ctx, doc->file);
		while (c >= 0 && (c == ' ' || c == '%'))
			c = fz_read_byte(ctx, doc->file);
		if (c != EOF)
			fz_unread_byte(ctx, doc->file);

		while (1)
		{
			tmpofs = fz_tell(ctx, doc->file);
			if (tmpofs < 0)
				fz_throw(ctx, FZ_ERROR_SYSTEM, "cannot tell in file");

			fz_try(ctx)
				tok = pdf_lex_no_string(ctx, doc->file, buf);
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "skipping ahead to next token");
				do
					c = fz_read_byte(ctx, doc->file);
				while (c != EOF && !is_white(c));
				if (c == EOF)
					tok = PDF_TOK_EOF;
				else
					continue;
			}

			/* If we have the next token already, then we'll jump
			 * back here, rather than going through the top of
			 * the loop. */
		have_next_token:

			if (tok == PDF_TOK_INT)
			{
				if (buf->i < 0)
				{
					num = 0;
					gen = 0;
					continue;
				}
				numofs = genofs;
				num = gen;
				genofs = tmpofs;
				gen = buf->i;
			}

			else if (tok == PDF_TOK_OBJ)
			{
				pdf_obj *root = NULL;

				fz_try(ctx)
				{
					stm_len = 0;
					stm_ofs = 0;
					tok = pdf_repair_obj(ctx, doc, buf, &stm_ofs, &stm_len, &encrypt, &id, NULL, &tmpofs, &root);
					if (root)
						add_root(ctx, roots, root);
				}
				fz_always(ctx)
				{
					pdf_drop_obj(ctx, root);
				}
				fz_catch(ctx)
				{
					int errcode = fz_caught(ctx);
					/* If we haven't seen a root yet, there is nothing
					 * we can do, but give up. Otherwise, we'll make
					 * do. */
					if (roots->len == 0 ||
						errcode == FZ_ERROR_TRYLATER ||
						errcode == FZ_ERROR_SYSTEM)
					{
						pdf_drop_root_list(ctx, roots);
						roots = NULL;
						fz_rethrow(ctx);
					}
					fz_report_error(ctx);
					fz_warn(ctx, "cannot parse object (%d %d R) - ignoring rest of file", num, gen);
					break;
				}

				if (num <= 0 || num > PDF_MAX_OBJECT_NUMBER)
				{
					fz_warn(ctx, "ignoring object with invalid object number (%d %d R)", num, gen);
					goto have_next_token;
				}

				gen = fz_clampi(gen, 0, 65535);

				if (listlen + 1 == listcap)
				{
					listcap = (listcap * 3) / 2;
					list = fz_realloc_array(ctx, list, listcap, struct entry);
				}

				list[listlen].num = num;
				list[listlen].gen = gen;
				list[listlen].ofs = numofs;
				list[listlen].stm_ofs = stm_ofs;
				list[listlen].stm_len = stm_len;
				listlen ++;

				if (num > maxnum)
					maxnum = num;

				goto have_next_token;
			}

			/* If we find a dictionary it is probably the trailer,
			 * but could be a stream (or bogus) dictionary caused
			 * by a corrupt file. */
			else if (tok == PDF_TOK_OPEN_DICT)
			{
				pdf_obj *dictobj;

				fz_try(ctx)
				{
					dict = pdf_parse_dict(ctx, doc, doc->file, buf);
				}
				fz_catch(ctx)
				{
					fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
					fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
					/* If this was the real trailer dict
					 * it was broken, in which case we are
					 * in trouble. Keep going though in
					 * case this was just a bogus dict. */
					fz_report_error(ctx);
					continue;
				}

				fz_try(ctx)
				{
					dictobj = pdf_dict_get(ctx, dict, PDF_NAME(Encrypt));
					if (dictobj)
					{
						pdf_drop_obj(ctx, encrypt);
						encrypt = pdf_keep_obj(ctx, dictobj);
					}

					dictobj = pdf_dict_get(ctx, dict, PDF_NAME(ID));
					if (dictobj && (!id || !encrypt || pdf_dict_get(ctx, dict, PDF_NAME(Encrypt))))
					{
						pdf_drop_obj(ctx, id);
						id = pdf_keep_obj(ctx, dictobj);
					}

					dictobj = pdf_dict_get(ctx, dict, PDF_NAME(Root));
					if (dictobj)
						add_root(ctx, roots, dictobj);

					dictobj = pdf_dict_get(ctx, dict, PDF_NAME(Info));
					if (dictobj)
					{
						pdf_drop_obj(ctx, info);
						info = pdf_keep_obj(ctx, dictobj);
					}
				}
				fz_always(ctx)
					pdf_drop_obj(ctx, dict);
				fz_catch(ctx)
					fz_rethrow(ctx);
			}

			else if (tok == PDF_TOK_EOF)
			{
				break;
			}

			else
			{
				num = 0;
				gen = 0;
			}
		}

		if (listlen == 0)
			fz_throw(ctx, FZ_ERROR_FORMAT, "no objects found");

		/* make xref reasonable */

		/*
			Dummy access to entry to assure sufficient space in the xref table
			and avoid repeated reallocs in the loop
		*/
		/* Ensure that the first xref table is a 'solid' one from
		 * 0 to maxnum. */
		pdf_ensure_solid_xref(ctx, doc, maxnum);

		for (i = 1; i < maxnum; i++)
		{
			entry = pdf_get_populating_xref_entry(ctx, doc, i);
			if (entry->obj != NULL)
				continue;
			entry->type = 'f';
			entry->ofs = 0;
			entry->gen = 0;
			entry->num = 0;

			entry->stm_ofs = 0;
		}

		for (i = 0; i < listlen; i++)
		{
			entry = pdf_get_populating_xref_entry(ctx, doc, list[i].num);
			entry->type = 'n';
			entry->ofs = list[i].ofs;
			entry->gen = list[i].gen;
			entry->num = list[i].num;

			entry->stm_ofs = list[i].stm_ofs;

			/* correct stream length for unencrypted documents */
			if (!encrypt && list[i].stm_len >= 0)
			{
				pdf_obj *old_obj = NULL;
				dict = pdf_load_object(ctx, doc, list[i].num);

				fz_try(ctx)
				{
					length = pdf_new_int(ctx, list[i].stm_len);
					pdf_dict_get_put_drop(ctx, dict, PDF_NAME(Length), length, &old_obj);
					if (old_obj)
						orphan_object(ctx, doc, old_obj);
				}
				fz_always(ctx)
					pdf_drop_obj(ctx, dict);
				fz_catch(ctx)
					fz_rethrow(ctx);
			}
		}

		entry = pdf_get_populating_xref_entry(ctx, doc, 0);
		entry->type = 'f';
		entry->ofs = 0;
		entry->gen = 65535;
		entry->num = 0;
		entry->stm_ofs = 0;

		next = 0;
		for (i = pdf_xref_len(ctx, doc) - 1; i >= 0; i--)
		{
			entry = pdf_get_populating_xref_entry(ctx, doc, i);
			if (entry->type == 'f')
			{
				entry->ofs = next;
				if (entry->gen < 65535)
					entry->gen ++;
				next = i;
			}
		}

		/* create a repaired trailer, Root will be added later */

		obj = pdf_new_dict(ctx, doc, 5);
		/* During repair there is only a single xref section */
		pdf_set_populating_xref_trailer(ctx, doc, obj);
		pdf_drop_obj(ctx, obj);
		obj = NULL;

		pdf_dict_put_int(ctx, pdf_trailer(ctx, doc), PDF_NAME(Size), maxnum + 1);

		if (info)
		{
			pdf_dict_put(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info), info);
			pdf_drop_obj(ctx, info);
			info = NULL;
		}

		if (encrypt)
		{
			if (pdf_is_indirect(ctx, encrypt))
			{
				/* create new reference with non-NULL xref pointer */
				obj = pdf_new_indirect(ctx, doc, pdf_to_num(ctx, encrypt), pdf_to_gen(ctx, encrypt));
				pdf_drop_obj(ctx, encrypt);
				encrypt = obj;
				obj = NULL;
			}
			pdf_dict_put(ctx, pdf_trailer(ctx, doc), PDF_NAME(Encrypt), encrypt);
			pdf_drop_obj(ctx, encrypt);
			encrypt = NULL;
		}

		if (id)
		{
			if (pdf_is_indirect(ctx, id))
			{
				/* create new reference with non-NULL xref pointer */
				obj = pdf_new_indirect(ctx, doc, pdf_to_num(ctx, id), pdf_to_gen(ctx, id));
				pdf_drop_obj(ctx, id);
				id = obj;
				obj = NULL;
			}
			pdf_dict_put(ctx, pdf_trailer(ctx, doc), PDF_NAME(ID), id);
			pdf_drop_obj(ctx, id);
			id = NULL;
		}
	}
	fz_always(ctx)
	{
		fz_free(ctx, list);
		doc->repair_in_progress = 0;
	}
	fz_catch(ctx)
	{
		pdf_drop_root_list(ctx, roots);
		pdf_drop_obj(ctx, encrypt);
		pdf_drop_obj(ctx, id);
		pdf_drop_obj(ctx, obj);
		pdf_drop_obj(ctx, info);
		if (ctx->throw_on_repair)
			fz_throw(ctx, FZ_ERROR_REPAIRED, "Error during repair attempt");
		fz_rethrow(ctx);
	}

	if (ctx->throw_on_repair)
	{
		pdf_drop_root_list(ctx, roots);
		fz_throw(ctx, FZ_ERROR_REPAIRED, "File repaired");
	}

	return roots;
}

static void
pdf_repair_obj_stms(fz_context *ctx, pdf_document *doc)
{
	pdf_obj *dict;
	int i;
	int xref_len = pdf_xref_len(ctx, doc);

	for (i = 0; i < xref_len; i++)
	{
		pdf_xref_entry *entry = pdf_get_populating_xref_entry(ctx, doc, i);

		if (entry->stm_ofs)
		{
			dict = pdf_load_object(ctx, doc, i);
			fz_try(ctx)
			{
				if (pdf_name_eq(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Type)), PDF_NAME(ObjStm)))
					pdf_repair_obj_stm(ctx, doc, i);
			}
			fz_always(ctx)
				pdf_drop_obj(ctx, dict);
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "ignoring broken object stream (%d 0 R)", i);
			}
		}
	}

	/* Ensure that streamed objects reside inside a known non-streamed object */
	for (i = 0; i < xref_len; i++)
	{
		pdf_xref_entry *entry = pdf_get_populating_xref_entry(ctx, doc, i);

		if (entry->type == 'o' && pdf_get_populating_xref_entry(ctx, doc, entry->ofs)->type != 'n')
		{
			fz_warn(ctx, "invalid reference to non-object-stream: %d, assuming %d 0 R is a freed object", (int)entry->ofs, i);
			entry->type = 'f';
		}
	}
}

static void
pdf_repair_roots(fz_context *ctx, pdf_document *doc, pdf_root_list *roots)
{
	int i;

	for (i = roots->len-1; i >= 0; i--)
	{
		if (pdf_is_indirect(ctx, roots->roots[i]) && pdf_is_dict(ctx, roots->roots[i]))
		{
			pdf_dict_put(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root), roots->roots[i]);
			break;
		}
	}
}

static void
pdf_repair_trailer(fz_context *ctx, pdf_document *doc)
{
	int hasroot, hasinfo;
	pdf_obj *obj, *nobj;
	pdf_obj *dict = NULL;
	int i;

	int xref_len = pdf_xref_len(ctx, doc);

	hasroot = (pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root)) != NULL);
	hasinfo = (pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info)) != NULL);

	fz_var(dict);

	fz_try(ctx)
	{
		/* Scan from the end so we have a better chance of finding
		 * newer objects if there are multiple instances of Info and
		 * Root objects.
		 */
		for (i = xref_len - 1; i > 0 && (!hasinfo || !hasroot); --i)
		{
			pdf_xref_entry *entry = pdf_get_xref_entry_no_null(ctx, doc, i);
			if (entry->type == 0 || entry->type == 'f')
				continue;

			fz_try(ctx)
			{
				dict = pdf_load_object(ctx, doc, i);
			}
			fz_catch(ctx)
			{
				fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
				fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
				fz_report_error(ctx);
				fz_warn(ctx, "ignoring broken object (%d 0 R)", i);
				continue;
			}

			if (!hasroot)
			{
				obj = pdf_dict_get(ctx, dict, PDF_NAME(Type));
				if (obj == PDF_NAME(Catalog))
				{
					nobj = pdf_new_indirect(ctx, doc, i, 0);
					pdf_dict_put_drop(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root), nobj);
					hasroot = 1;
				}
			}

			if (!hasinfo)
			{
				if (pdf_dict_get(ctx, dict, PDF_NAME(Creator)) || pdf_dict_get(ctx, dict, PDF_NAME(Producer)))
				{
					nobj = pdf_new_indirect(ctx, doc, i, 0);
					pdf_dict_put_drop(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info), nobj);
					hasinfo = 1;
				}
			}

			pdf_drop_obj(ctx, dict);
			dict = NULL;
		}
	}
	fz_always(ctx)
	{
		/* ensure that strings are not used in their repaired, non-decrypted form */
		if (doc->crypt)
		{
			pdf_crypt *tmp;
			pdf_clear_xref(ctx, doc);

			/* ensure that Encryption dictionary and ID are cached without decryption,
			   otherwise a decrypted Encryption dictionary and ID may be used when saving
			   the PDF causing it to be inconsistent (since strings/streams are encrypted
			   with the actual encryption key, not the decrypted encryption key). */
			tmp = doc->crypt;
			doc->crypt = NULL;
			fz_try(ctx)
			{
				(void) pdf_resolve_indirect(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Encrypt)));
				(void) pdf_resolve_indirect(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(ID)));
			}
			fz_always(ctx)
				doc->crypt = tmp;
			fz_catch(ctx)
			{
				fz_rethrow(ctx);
			}
		}
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, dict);
		fz_rethrow(ctx);
	}
}

void pdf_repair_xref_aux(fz_context *ctx, pdf_document *doc, void (*mid)(fz_context *ctx, pdf_document *doc))
{
	pdf_root_list *roots = NULL;

	fz_var(roots);

	fz_try(ctx)
	{
		roots = pdf_repair_xref_base(ctx, doc);
		if (mid)
			mid(ctx, doc);
		pdf_repair_obj_stms(ctx, doc);
		pdf_repair_roots(ctx, doc, roots);
		pdf_repair_trailer(ctx, doc);
	}
	fz_always(ctx)
		pdf_drop_root_list(ctx, roots);
	fz_catch(ctx)
		fz_rethrow(ctx);
}
