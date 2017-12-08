/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libparrillada-misc
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libparrillada-misc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libparrillada-misc authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libparrillada-misc. This permission is above and beyond the permissions granted
 * by the GPL license by which Libparrillada-burn is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libparrillada-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#ifndef _PARRILLADA_JACKET_FONT_H_
#define _PARRILLADA_JACKET_FONT_H_

#include <glib-object.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PARRILLADA_TYPE_JACKET_FONT             (parrillada_jacket_font_get_type ())
#define PARRILLADA_JACKET_FONT(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), PARRILLADA_TYPE_JACKET_FONT, ParrilladaJacketFont))
#define PARRILLADA_JACKET_FONT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), PARRILLADA_TYPE_JACKET_FONT, ParrilladaJacketFontClass))
#define PARRILLADA_IS_JACKET_FONT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PARRILLADA_TYPE_JACKET_FONT))
#define PARRILLADA_IS_JACKET_FONT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), PARRILLADA_TYPE_JACKET_FONT))
#define PARRILLADA_JACKET_FONT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), PARRILLADA_TYPE_JACKET_FONT, ParrilladaJacketFontClass))

typedef struct _ParrilladaJacketFontClass ParrilladaJacketFontClass;
typedef struct _ParrilladaJacketFont ParrilladaJacketFont;

struct _ParrilladaJacketFontClass
{
	GtkBoxClass parent_class;
};

struct _ParrilladaJacketFont
{
	GtkBox parent_instance;
};

GType parrillada_jacket_font_get_type (void) G_GNUC_CONST;

GtkWidget *
parrillada_jacket_font_new (void);

void
parrillada_jacket_font_set_name (ParrilladaJacketFont *font,
			      const gchar *name);

gchar *
parrillada_jacket_font_get_family (ParrilladaJacketFont *font);

guint
parrillada_jacket_font_get_size (ParrilladaJacketFont *font);

G_END_DECLS

#endif /* _PARRILLADA_JACKET_FONT_H_ */
