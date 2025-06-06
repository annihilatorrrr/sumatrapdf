// Copyright (C) 2004-2021 Artifex Software, Inc.
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

/*
 * PDF creation tool: Tool for creating pdf content.
 *
 * Simple test bed to work with adding content and creating PDFs
 */

#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int usage(void)
{
	fprintf(stderr,
		"usage: mutool create [-o output.pdf] [-O options] page.txt [page2.txt ...]\n"
		"\t-o -\tname of PDF file to create\n"
		"\t-O -\tcomma separated list of output options\n"
		"\tpage.txt\tcontent stream with annotations for creating resources\n\n"
		"Content stream special commands:\n"
		"\t%%%%MediaBox LLX LLY URX URY\n"
		"\t%%%%Rotate Angle\n"
		"\t%%%%Font Name Filename Encoding\n"
		"\t\tFilename is either a file or a base 14 font name\n"
		"\t\tEncoding=Latin|Greek|Cyrillic\n"
		"\t%%%%CJKFont Name Language WMode Style\n"
		"\t\tLanguage=zh-Hant|zh-Hans|ja|ko\n"
		"\t\tWMode=H|V\n"
		"\t\tStyle=serif|sans)\n"
		"\t%%%%Image Name Filename\n\n"
		);
	fputs(fz_pdf_write_options_usage, stderr);
	return 1;
}

static fz_context *ctx = NULL;
static pdf_document *doc = NULL;

static void add_font_res(pdf_obj *resources, char *name, char *path, char *encname)
{
	const unsigned char *data;
	int size, enc;
	fz_font *font;
	pdf_obj *subres, *ref;

	data = fz_lookup_base14_font(ctx, path, &size);
	if (data)
		font = fz_new_font_from_memory(ctx, path, data, size, 0, 0);
	else
		font = fz_new_font_from_file(ctx, NULL, path, 0, 0);

	subres = pdf_dict_get(ctx, resources, PDF_NAME(Font));
	if (!subres)
	{
		subres = pdf_new_dict(ctx, doc, 10);
		pdf_dict_put_drop(ctx, resources, PDF_NAME(Font), subres);
	}

	enc = PDF_SIMPLE_ENCODING_LATIN;
	if (encname)
	{
		if (!strcmp(encname, "Latin") || !strcmp(encname, "Latn"))
			enc = PDF_SIMPLE_ENCODING_LATIN;
		else if (!strcmp(encname, "Greek") || !strcmp(encname, "Grek"))
			enc = PDF_SIMPLE_ENCODING_GREEK;
		else if (!strcmp(encname, "Cyrillic") || !strcmp(encname, "Cyrl"))
			enc = PDF_SIMPLE_ENCODING_CYRILLIC;
	}

	ref = pdf_add_simple_font(ctx, doc, font, enc);
	pdf_dict_puts(ctx, subres, name, ref);
	pdf_drop_obj(ctx, ref);

	fz_drop_font(ctx, font);
}

static void add_cjkfont_res(pdf_obj *resources, char *name, char *lang, char *wm, char *style)
{
	const unsigned char *data;
	int size, index, ordering, wmode, serif;
	fz_font *font;
	pdf_obj *subres, *ref;

	ordering = fz_lookup_cjk_ordering_by_language(lang);

	if (wm && !strcmp(wm, "V"))
		wmode = 1;
	else
		wmode = 0;

	if (style && (!strcmp(style, "sans") || !strcmp(style, "sans-serif")))
		serif = 0;
	else
		serif = 1;

	data = fz_lookup_cjk_font(ctx, ordering, &size, &index);
	font = fz_new_font_from_memory(ctx, NULL, data, size, index, 0);

	subres = pdf_dict_get(ctx, resources, PDF_NAME(Font));
	if (!subres)
	{
		subres = pdf_new_dict(ctx, doc, 10);
		pdf_dict_put_drop(ctx, resources, PDF_NAME(Font), subres);
	}

	ref = pdf_add_cjk_font(ctx, doc, font, ordering, wmode, serif);
	pdf_dict_puts(ctx, subres, name, ref);
	pdf_drop_obj(ctx, ref);

	fz_drop_font(ctx, font);
}

static void add_image_res(pdf_obj *resources, char *name, char *path)
{
	fz_image *image;
	pdf_obj *subres, *ref;

	image = fz_new_image_from_file(ctx, path);

	subres = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
	if (!subres)
	{
		subres = pdf_new_dict(ctx, doc, 10);
		pdf_dict_put_drop(ctx, resources, PDF_NAME(XObject), subres);
	}

	ref = pdf_add_image(ctx, doc, image);
	pdf_dict_puts(ctx, subres, name, ref);
	pdf_drop_obj(ctx, ref);

	fz_drop_image(ctx, image);
}

/*
The input is a raw content stream, with commands embedded in comments:

%%MediaBox LLX LLY URX URY
%%Rotate Angle
%%Font Name Filename (or base 14 font name) [Encoding (Latin, Greek or Cyrillic)]
%%CJKFont Name Language WMode Style (Language=zh-Hant|zh-Hans|ja|ko, WMode=H|V, Style=serif|sans)
%%Image Name Filename
*/
static void create_page(char *input)
{
	fz_rect mediabox = { 0, 0, 595, 842 };
	int rotate = 0;

	char line[4096];
	char *s, *p;
	fz_stream *stm = NULL;

	fz_buffer *contents = NULL;
	pdf_obj *resources;
	pdf_obj *page = NULL;

	resources = pdf_new_dict(ctx, doc, 2);

	fz_var(stm);
	fz_var(page);
	fz_var(contents);

	fz_try(ctx)
	{
		contents = fz_new_buffer(ctx, 1024);

		stm = fz_open_file(ctx, input);
		while (fz_read_line(ctx, stm, line, sizeof line))
		{
			if (line[0] == '%' && line[1] == '%')
			{
				p = line;
				s = fz_strsep(&p, " ");
				if (!strcmp(s, "%%MediaBox"))
				{
					mediabox.x0 = fz_atoi(fz_strsep(&p, " "));
					mediabox.y0 = fz_atoi(fz_strsep(&p, " "));
					mediabox.x1 = fz_atoi(fz_strsep(&p, " "));
					mediabox.y1 = fz_atoi(fz_strsep(&p, " "));
				}
				else if (!strcmp(s, "%%Rotate"))
				{
					rotate = fz_atoi(fz_strsep(&p, " "));
				}
				else if (!strcmp(s, "%%Font"))
				{
					char *name = fz_strsep(&p, " ");
					char *path = fz_strsep(&p, " ");
					char *enc = fz_strsep(&p, " ");
					if (!name || !path)
						fz_throw(ctx, FZ_ERROR_ARGUMENT, "Font directive missing arguments");
					add_font_res(resources, name, path, enc);
				}
				else if (!strcmp(s, "%%CJKFont"))
				{
					char *name = fz_strsep(&p, " ");
					char *lang = fz_strsep(&p, " ");
					char *wmode = fz_strsep(&p, " ");
					char *style = fz_strsep(&p, " ");
					if (!name || !lang)
						fz_throw(ctx, FZ_ERROR_ARGUMENT, "CJKFont directive missing arguments");
					add_cjkfont_res(resources, name, lang, wmode, style);
				}
				else if (!strcmp(s, "%%Image"))
				{
					char *name = fz_strsep(&p, " ");
					char *path = fz_strsep(&p, " ");
					if (!name || !path)
						fz_throw(ctx, FZ_ERROR_ARGUMENT, "Image directive missing arguments");
					add_image_res(resources, name, path);
				}
			}
			else
			{
				fz_append_string(ctx, contents, line);
				fz_append_byte(ctx, contents, '\n');
			}
		}

		page = pdf_add_page(ctx, doc, mediabox, rotate, resources, contents);
		pdf_insert_page(ctx, doc, -1, page);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
		pdf_drop_obj(ctx, page);
		fz_drop_buffer(ctx, contents);
		pdf_drop_obj(ctx, resources);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

int pdfcreate_main(int argc, char **argv)
{
	pdf_write_options opts = pdf_default_write_options;
	char *output = "out.pdf";
	char *flags = "compress";
	int i, c;
	int error = 0;

	while ((c = fz_getopt(argc, argv, "o:O:")) != -1)
	{
		switch (c)
		{
		case 'o': output = fz_optpath(fz_optarg); break;
		case 'O': flags = fz_optarg; break;
		default: return usage();
		}
	}

	if (fz_optind == argc)
		return usage();

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}

	pdf_parse_write_options(ctx, &opts, flags);

	fz_var(doc);

	fz_try(ctx)
	{
		doc = pdf_create_document(ctx);

		for (i = fz_optind; i < argc; ++i)
			create_page(argv[i]);

		pdf_save_document(ctx, doc, output, &opts);
	}
	fz_always(ctx)
		pdf_drop_document(ctx, doc);
	fz_catch(ctx)
	{
		fz_report_error(ctx);
		error = 1;
	}

	fz_flush_warnings(ctx);
	fz_drop_context(ctx);
	return error;
}
