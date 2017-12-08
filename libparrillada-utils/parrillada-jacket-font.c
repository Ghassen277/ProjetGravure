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

#include <string.h>

#include <gtk/gtk.h>

#include "parrillada-jacket-font.h"

typedef struct _ParrilladaJacketFontPrivate ParrilladaJacketFontPrivate;
struct _ParrilladaJacketFontPrivate
{
	GtkWidget *family;
	GtkWidget *size;

	gint current_size;
	gchar *current_family;
};

#define PARRILLADA_JACKET_FONT_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_JACKET_FONT, ParrilladaJacketFontPrivate))

enum
{
	FONT_CHANGED,
	SIZE_CHANGED,

	LAST_SIGNAL
};

enum {
	FAMILY_STRING_COL,
	FAMILY_COL,
	FAMILY_COL_NB
};

enum {
	SIZE_STRING_COL,
	SIZE_COL,
	SIZE_COL_NB	
};

static const guint16 font_sizes[] = {
  6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 26, 28,
  32, 36, 40, 48, 56, 64, 72
};

static guint jacket_font_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ParrilladaJacketFont, parrillada_jacket_font, GTK_TYPE_BOX);

static void
parrillada_jacket_font_family_changed_cb (GtkComboBox *combo,
				       ParrilladaJacketFont *self)
{
	g_signal_emit (self,
		       jacket_font_signals [FONT_CHANGED],
		       0);
}

static void
parrillada_jacket_font_size_changed_cb (GtkComboBox *combo,
				     ParrilladaJacketFont *self)
{
	g_signal_emit (self,
		       jacket_font_signals [SIZE_CHANGED],
		       0);	
}

void
parrillada_jacket_font_set_name (ParrilladaJacketFont *self,
			      const gchar *string)
{
	ParrilladaJacketFontPrivate *priv;
	PangoFontDescription *desc;
	const gchar *family_name;
	const gchar *name = NULL;
	PangoFontFamily *family;
	GtkTreeModel *model;
	GtkTreeIter iter;
	guint font_size;
	guint size;

	priv = PARRILLADA_JACKET_FONT_PRIVATE (self);

	desc = pango_font_description_from_string (string);
	family_name = pango_font_description_get_family (desc);
	font_size = pango_font_description_get_size (desc);

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->family), &iter)) {
		family = NULL;

		model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->family));
		gtk_tree_model_get (model, &iter,
				    FAMILY_COL, &family,
				    -1);
		if (family)
			name = pango_font_family_get_name (family);
	}

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->size), &iter)) {
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->size));
		gtk_tree_model_get (model, &iter,
				    SIZE_COL, &size,
				    -1);
		if (family_name && name && !strcmp (family_name, name) && size == font_size) {
			pango_font_description_free (desc);
			return;
		}
	}

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->family));
	if (!gtk_tree_model_get_iter_first (model, &iter)) {
		/* No font in the model, no need to continue */
		pango_font_description_free (desc);
		return;
	}

	g_signal_handlers_block_by_func (priv->family,
					 parrillada_jacket_font_family_changed_cb,
					 self);

	do {
		gtk_tree_model_get (model, &iter,
				    FAMILY_COL, &family,
				    -1);

		name = pango_font_family_get_name (family);
		if (!strcmp (family_name, name)) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->family), &iter);
			break;
		}

	} while (gtk_tree_model_iter_next (model, &iter));

	g_signal_handlers_unblock_by_func (priv->family,
					   parrillada_jacket_font_family_changed_cb,
					   self);

	g_signal_handlers_block_by_func (priv->size,
					 parrillada_jacket_font_size_changed_cb,
					 self);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->size));
	gtk_tree_model_get_iter_first (model, &iter);
	do {
		gtk_tree_model_get (model, &iter,
				    SIZE_COL, &size,
				    -1);

		if (size == font_size / PANGO_SCALE) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->size), &iter);
			break;
		}

	} while (gtk_tree_model_iter_next (model, &iter));

	g_signal_handlers_unblock_by_func (priv->size,
					   parrillada_jacket_font_size_changed_cb,
					   self);

	pango_font_description_free (desc);
}

gchar *
parrillada_jacket_font_get_family (ParrilladaJacketFont *self)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	gchar *family = NULL;
	ParrilladaJacketFontPrivate *priv;

	priv = PARRILLADA_JACKET_FONT_PRIVATE (self);

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->family), &iter))
		return NULL;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->family));
	gtk_tree_model_get (model, &iter,
			    FAMILY_STRING_COL, &family,
			    -1);
	return family;
}

guint
parrillada_jacket_font_get_size (ParrilladaJacketFont *self)
{
	guint size;
	GtkTreeIter iter;
	GtkTreeModel *model;
	ParrilladaJacketFontPrivate *priv;

	priv = PARRILLADA_JACKET_FONT_PRIVATE (self);

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->size), &iter))
		return 0;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->size));
	gtk_tree_model_get (model, &iter,
			    SIZE_COL, &size,
			    -1);

	return size * PANGO_SCALE;
}
static void
parrillada_jacket_fill_sizes (ParrilladaJacketFont *self,
                           GtkListStore *store)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (font_sizes); i ++) {
		GtkTreeIter iter;
		gchar *string;

		string = g_strdup_printf ("%i", font_sizes [i]);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    SIZE_STRING_COL, string,
				    SIZE_COL, font_sizes [i],
				    -1);
		g_free (string);
	}
}

static void
parrillada_jacket_fill_families (ParrilladaJacketFont *self,
                              GtkListStore *store)
{
	PangoFontFamily **families;
	gint num = 0;
	gint i;

	pango_context_list_families (gtk_widget_get_pango_context (GTK_WIDGET (self)),
				     &families, &num);

	for (i = 0; i < num; i ++) {
		const gchar *name;
		GtkTreeIter iter;

		name = pango_font_family_get_name (families [i]);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    FAMILY_COL, families [i],
				    FAMILY_STRING_COL, name,
				    -1);
	}
	g_free (families);
}

static void
parrillada_jacket_font_init (ParrilladaJacketFont *object)
{
	GtkListStore *store;
	GtkCellRenderer *renderer;
	ParrilladaJacketFontPrivate *priv;

	priv = PARRILLADA_JACKET_FONT_PRIVATE (object);

	gtk_box_set_homogeneous (GTK_BOX (object), FALSE);

	store = gtk_list_store_new (FAMILY_COL_NB,
				    G_TYPE_STRING,
				    G_TYPE_POINTER);
	parrillada_jacket_fill_families (object, store);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), FAMILY_STRING_COL, GTK_SORT_ASCENDING);
	priv->family = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
	g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->family), renderer, FALSE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (priv->family), renderer,
				       "text", FAMILY_STRING_COL);

	gtk_box_pack_start (GTK_BOX (object), priv->family, FALSE, FALSE, 0);
	gtk_widget_show (priv->family);
	gtk_combo_box_set_focus_on_click (GTK_COMBO_BOX (priv->family), FALSE);

	g_signal_connect (priv->family,
			  "changed",
			  G_CALLBACK (parrillada_jacket_font_family_changed_cb),
			  object);

	store = gtk_list_store_new (SIZE_COL_NB,
				    G_TYPE_STRING,
				    G_TYPE_UINT);
	parrillada_jacket_fill_sizes (object, store);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), SIZE_COL, GTK_SORT_ASCENDING);
	priv->size = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
	g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->size), renderer, FALSE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (priv->size), renderer,
				       "text", SIZE_COL);

	gtk_box_pack_start (GTK_BOX (object), priv->size, FALSE, FALSE, 0);
	gtk_widget_show (priv->size);
	gtk_combo_box_set_focus_on_click (GTK_COMBO_BOX (priv->size), FALSE);

	g_signal_connect (priv->size,
			  "changed",
			  G_CALLBACK (parrillada_jacket_font_size_changed_cb),
			  object);
}

static void
parrillada_jacket_font_finalize (GObject *object)
{
	G_OBJECT_CLASS (parrillada_jacket_font_parent_class)->finalize (object);
}

static void
parrillada_jacket_font_class_init (ParrilladaJacketFontClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaJacketFontPrivate));

	object_class->finalize = parrillada_jacket_font_finalize;

	jacket_font_signals[FONT_CHANGED] =
		g_signal_new ("font_changed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0,
		              G_TYPE_NONE);

	jacket_font_signals[SIZE_CHANGED] =
		g_signal_new ("size_changed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0,
		              G_TYPE_NONE);
}

GtkWidget *
parrillada_jacket_font_new (void)
{
	return g_object_new (PARRILLADA_TYPE_JACKET_FONT, NULL);
}
