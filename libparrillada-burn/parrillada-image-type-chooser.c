/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libparrillada-burn
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libparrillada-burn is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libparrillada-burn authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libparrillada-burn. This permission is above and beyond the permissions granted
 * by the GPL license by which Libparrillada-burn is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libparrillada-burn is distributed in the hope that it will be useful,
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include "burn-basics.h"
#include "parrillada-image-type-chooser.h"

#define PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_IMAGE_TYPE_CHOOSER, ParrilladaImageTypeChooserPrivate))

G_DEFINE_TYPE (ParrilladaImageTypeChooser, parrillada_image_type_chooser, GTK_TYPE_HBOX);

enum {
	FORMAT_TEXT,
	FORMAT_TYPE,
	FORMAT_SVCD,
	FORMAT_LAST
};

enum {
	CHANGED_SIGNAL,
	LAST_SIGNAL
};
static guint parrillada_image_type_chooser_signals [LAST_SIGNAL] = { 0 };

struct _ParrilladaImageTypeChooserPrivate {
	GtkWidget *combo;

	ParrilladaImageFormat format;
	gboolean is_svcd;

	guint updating:1;
};

static GtkHBoxClass *parent_class = NULL;

guint
parrillada_image_type_chooser_set_formats (ParrilladaImageTypeChooser *self,
				        ParrilladaImageFormat formats,
                                        gboolean show_autodetect,
                                        gboolean is_video)
{
	guint format_num;
	GtkTreeIter iter;
	GtkTreeModel *store;
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (self);

	priv->updating = TRUE;

	format_num = 0;

	/* clean */
	store = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->combo));
	gtk_list_store_clear (GTK_LIST_STORE (store));

	/* now we get the targets available and display them */
	if (show_autodetect) {
		gtk_list_store_prepend (GTK_LIST_STORE (store), &iter);
		gtk_list_store_set (GTK_LIST_STORE (store), &iter,
				    FORMAT_TEXT, _("Autodetect"),
				    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_NONE,
				    -1);
	}

	if (formats & PARRILLADA_IMAGE_FORMAT_BIN) {
		format_num ++;
		gtk_list_store_append (GTK_LIST_STORE (store), &iter);
		gtk_list_store_set (GTK_LIST_STORE (store), &iter,
				    FORMAT_TEXT, is_video? _("Video DVD image"):_("ISO9660 image"),
				    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_BIN,
				    -1);
	}

	if (formats & PARRILLADA_IMAGE_FORMAT_CLONE) {
		format_num ++;
		gtk_list_store_append (GTK_LIST_STORE (store), &iter);
		gtk_list_store_set (GTK_LIST_STORE (store), &iter,
				    FORMAT_TEXT, _("Readcd/Readom image"),
				    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_CLONE,
				    -1);
	}

	if (formats & PARRILLADA_IMAGE_FORMAT_CUE) {
		format_num ++;
		if (is_video) {
			format_num ++;
	
			gtk_list_store_append (GTK_LIST_STORE (store), &iter);
			gtk_list_store_set (GTK_LIST_STORE (store), &iter,
					    FORMAT_TEXT, _("VCD image"),
					    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_CUE,
					    -1);

			gtk_list_store_append (GTK_LIST_STORE (store), &iter);
			gtk_list_store_set (GTK_LIST_STORE (store), &iter,
					    FORMAT_TEXT, _("SVCD image"),
					    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_CUE,
			                    FORMAT_SVCD, TRUE,
					    -1);
		}
		else {
			gtk_list_store_append (GTK_LIST_STORE (store), &iter);
			gtk_list_store_set (GTK_LIST_STORE (store), &iter,
					    FORMAT_TEXT, _("Cue image"),
					    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_CUE,
					    -1);
		}
	}

	if (formats & PARRILLADA_IMAGE_FORMAT_CDRDAO) {
		format_num ++;
		gtk_list_store_append (GTK_LIST_STORE (store), &iter);
		gtk_list_store_set (GTK_LIST_STORE (store), &iter,
				    FORMAT_TEXT, _("Cdrdao image"),
				    FORMAT_TYPE, PARRILLADA_IMAGE_FORMAT_CDRDAO,
				    -1);
	}

	priv->updating = FALSE;

	/* Make sure the selected format is still supported */
	if (priv->format & formats)
		parrillada_image_type_chooser_set_format (self, priv->format);
	else
		parrillada_image_type_chooser_set_format (self, PARRILLADA_IMAGE_FORMAT_NONE);

	return format_num;
}

void
parrillada_image_type_chooser_set_format (ParrilladaImageTypeChooser *self,
				       ParrilladaImageFormat format)
{
	GtkTreeIter iter;
	GtkTreeModel *store;
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (self);

	store = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->combo));
	
	if (format == PARRILLADA_IMAGE_FORMAT_NONE) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combo), 0);
		return;
	}

	if (!gtk_tree_model_get_iter_first (store, &iter))
		return;

	do {
		ParrilladaImageFormat iter_format;

		gtk_tree_model_get (store, &iter,
				    FORMAT_TYPE, &iter_format,
				    -1);

		if (iter_format == format) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->combo), &iter);
			return;
		}

	} while (gtk_tree_model_iter_next (store, &iter));

	/* just to make sure we see if there is a line which is active. It can 
	 * happens that the last time it was a CD and the user chose RAW. If it
	 * is now a DVD it can't be raw any more */
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (priv->combo)) == -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combo), 0);
}

void
parrillada_image_type_chooser_set_VCD_type (ParrilladaImageTypeChooser *chooser,
                                         gboolean is_SVCD)
{
	GtkTreeIter iter;
	GtkTreeModel *store;
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (chooser);

	store = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->combo));
	
	if (!gtk_tree_model_get_iter_first (store, &iter))
		return;

	do {
		ParrilladaImageFormat iter_format;
		gboolean is_svcd;

		gtk_tree_model_get (store, &iter,
				    FORMAT_TYPE, &iter_format,
		                    FORMAT_SVCD, &is_svcd,
				    -1);

		if (iter_format == PARRILLADA_IMAGE_FORMAT_CUE && is_SVCD == is_svcd) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->combo), &iter);
			return;
		}

	} while (gtk_tree_model_iter_next (store, &iter));

	/* just to make sure we see if there is a line which is active. It can 
	 * happens that the last time it was a CD and the user chose RAW. If it
	 * is now a DVD it can't be raw any more */
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (priv->combo)) == -1)
		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combo), 0);
}

void
parrillada_image_type_chooser_get_format (ParrilladaImageTypeChooser *self,
				       ParrilladaImageFormat *format)
{
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (self);
	*format = priv->format;
}

gboolean
parrillada_image_type_chooser_get_VCD_type (ParrilladaImageTypeChooser *chooser)
{
	ParrilladaImageTypeChooserPrivate *priv;
	GtkTreeModel *model;
	gboolean value;
	GtkTreeIter iter;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (chooser);
	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->combo), &iter))
		return FALSE;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->combo));
	gtk_tree_model_get (model, &iter,
	                    FORMAT_SVCD, &value,
	                    -1);

	return value;
}

static void
parrillada_image_type_chooser_changed_cb (GtkComboBox *combo,
				       ParrilladaImageTypeChooser *self)
{
	GtkTreeIter iter;
	gboolean is_svcd;
	GtkTreeModel *store;
	ParrilladaImageFormat current;
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (self);

	if (priv->updating)
		return;

	store = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->combo));
	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->combo), &iter))
		gtk_tree_model_get (store, &iter,
				    FORMAT_TYPE, &current,
		                    FORMAT_SVCD, &is_svcd,
				    -1);
	else
		current = PARRILLADA_IMAGE_FORMAT_NONE;

	if (current == priv->format
	&& is_svcd == priv->is_svcd)
		return;

	priv->format = current;
	priv->is_svcd = is_svcd;

	g_signal_emit (self,
		       parrillada_image_type_chooser_signals [CHANGED_SIGNAL],
		       0);
}

static void
parrillada_image_type_chooser_init (ParrilladaImageTypeChooser *obj)
{
	GtkListStore *store;
	GtkCellRenderer *renderer;
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (obj);

	store = gtk_list_store_new (FORMAT_LAST,
				    G_TYPE_STRING,
				    G_TYPE_INT,
	                            G_TYPE_BOOLEAN);

	priv->combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
	g_signal_connect (priv->combo,
			  "changed",
			  G_CALLBACK (parrillada_image_type_chooser_changed_cb),
			  obj);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (priv->combo), renderer,
					"text", FORMAT_TEXT,
					NULL);

	gtk_widget_show (priv->combo);
	gtk_box_pack_end (GTK_BOX (obj), priv->combo, TRUE, TRUE, 0);
}

static void
parrillada_image_type_chooser_finalize (GObject *object)
{
	ParrilladaImageTypeChooserPrivate *priv;

	priv = PARRILLADA_IMAGE_TYPE_CHOOSER_PRIVATE (object);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
parrillada_image_type_chooser_class_init (ParrilladaImageTypeChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	g_type_class_add_private (klass, sizeof (ParrilladaImageTypeChooserPrivate));

	parent_class = g_type_class_peek_parent(klass);
	object_class->finalize = parrillada_image_type_chooser_finalize;

	parrillada_image_type_chooser_signals [CHANGED_SIGNAL] = 
	    g_signal_new ("changed",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_FIRST,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__VOID,
			  G_TYPE_NONE,
			  0);
}


GtkWidget *
parrillada_image_type_chooser_new ()
{
	ParrilladaImageTypeChooser *obj;

	obj = PARRILLADA_IMAGE_TYPE_CHOOSER (g_object_new (PARRILLADA_TYPE_IMAGE_TYPE_CHOOSER, NULL));
	
	return GTK_WIDGET (obj);
}
