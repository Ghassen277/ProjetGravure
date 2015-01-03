/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/***************************************************************************
*            play-list.c
*
*  mer mai 25 22:22:53 2005
*  Copyright  2005  Philippe Rouquier
*  brasero-app@wanadoo.fr
****************************************************************************/

/*
 *  Parrillada is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Parrillada is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include <gio/gio.h>

#include <gtk/gtk.h>

#include <totem-pl-parser.h>

#include "parrillada-misc.h"

#include "parrillada-units.h"

#include "parrillada-app.h"
#include "parrillada-playlist.h"
#include "parrillada-search-engine.h"
#include "parrillada-utils.h"
#include "parrillada-metadata.h"
#include "parrillada-io.h"
#include "eggtreemultidnd.h"

#include "parrillada-uri-container.h"
#include "parrillada-layout-object.h"


struct ParrilladaPlaylistPrivate {
	ParrilladaSearchEngine *engine;
	int id;

	GtkWidget *tree;
	GtkWidget *button_add;
	GtkWidget *button_remove;
	guint activity_counter;

	ParrilladaIOJobBase *parse_type;

	gint searched:1;
};

enum {
	PARRILLADA_PLAYLIST_DISPLAY_COL,
	PARRILLADA_PLAYLIST_NB_SONGS_COL,
	PARRILLADA_PLAYLIST_LEN_COL,
	PARRILLADA_PLAYLIST_GENRE_COL,
	PARRILLADA_PLAYLIST_URI_COL,
	PARRILLADA_PLAYLIST_DSIZE_COL,
	PARRILLADA_PLAYLIST_NB_COL,
};

enum {
	TARGET_URIS_LIST,
};

static GtkTargetEntry ntables[] = {
	{"text/uri-list", 0, TARGET_URIS_LIST}
};
static guint nb_ntables = sizeof (ntables) / sizeof (ntables[0]);

static void parrillada_playlist_iface_uri_container_init (ParrilladaURIContainerIFace *iface);
static void parrillada_playlist_iface_layout_object_init (ParrilladaLayoutObjectIFace *iface);

G_DEFINE_TYPE_WITH_CODE (ParrilladaPlaylist,
			 parrillada_playlist,
			 GTK_TYPE_VBOX,
			 G_IMPLEMENT_INTERFACE (PARRILLADA_TYPE_URI_CONTAINER,
					        parrillada_playlist_iface_uri_container_init)
			 G_IMPLEMENT_INTERFACE (PARRILLADA_TYPE_LAYOUT_OBJECT,
					        parrillada_playlist_iface_layout_object_init));

#define PARRILLADA_PLAYLIST_SPACING 6

static void
parrillada_playlist_get_proportion (ParrilladaLayoutObject *object,
				 gint *header,
				 gint *center,
				 gint *footer)
{
	GtkRequisition requisition;

	gtk_widget_size_request (gtk_widget_get_parent (PARRILLADA_PLAYLIST (object)->priv->button_add),
				 &requisition);
	(*footer) = requisition.height + PARRILLADA_PLAYLIST_SPACING;
}

static void
parrillada_playlist_increase_activity_counter (ParrilladaPlaylist *playlist)
{
	GdkWindow *window;

	window = gtk_widget_get_window (GTK_WIDGET (playlist->priv->tree));
	if (!window)
		return;

	if (playlist->priv->activity_counter == 0) {
		GdkCursor *cursor;

		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (window,
				       cursor);
		gdk_cursor_unref (cursor);
	}
	playlist->priv->activity_counter++;
}

static void
parrillada_playlist_decrease_activity_counter (ParrilladaPlaylist *playlist)
{
	GdkWindow *window;

	if (playlist->priv->activity_counter > 0)
		playlist->priv->activity_counter--;

	window = gtk_widget_get_window (GTK_WIDGET (playlist->priv->tree));
	if (!window)
		return;

	if (playlist->priv->activity_counter == 0)
		gdk_window_set_cursor (window, NULL);
}

static void
parrillada_playlist_search_playlists_rhythmbox (ParrilladaPlaylist *playlist)
{
/*	RBSource *source;

	manager = rb_playlist_manager_new ();
	lists = rb_playlist_manager_get_playlists (manager);
*/
}

static void
parrillada_playlist_search (ParrilladaPlaylist *playlist)
{
	const gchar* mimes [] = {"audio/x-ms-asx",
		"audio/x-mpegurl",
		"audio/x-scpls",
		"audio/x-mp3-playlist",
		NULL};

	parrillada_search_engine_new_query (playlist->priv->engine, NULL);
	parrillada_search_engine_set_query_mime (playlist->priv->engine, mimes);
	parrillada_search_engine_start_query (playlist->priv->engine);
	parrillada_playlist_increase_activity_counter (playlist);
}

static gboolean
parrillada_playlist_try_again (ParrilladaPlaylist *playlist)
{
	if (!parrillada_search_engine_is_available (playlist->priv->engine))
		return TRUE;

	parrillada_playlist_search (playlist);

	playlist->priv->id = 0;
	return FALSE;
}

static void
parrillada_playlist_start_search (ParrilladaPlaylist *playlist)
{
	if (!playlist->priv->engine)
		return;

	if (!parrillada_search_engine_is_available (playlist->priv->engine)) {
		/* we will retry in 10 seconds */
		playlist->priv->id = g_timeout_add_seconds (10,
							    (GSourceFunc) parrillada_playlist_try_again,
							    playlist);
		return;
	}

	parrillada_playlist_search (playlist);
}

static gboolean
parrillada_playlist_expose_event_cb (GtkWidget *widget,
				  gpointer event,
				  gpointer null_data)
{
	ParrilladaPlaylist *playlist = PARRILLADA_PLAYLIST (widget);

	/* we only want to load playlists if the user is going to use them that
	 * is if they become apparent. That will avoid overhead */
	if (!playlist->priv->searched) {
		playlist->priv->searched = 1;
		parrillada_playlist_start_search (playlist);
		parrillada_playlist_search_playlists_rhythmbox (playlist);
	}

	return FALSE;
}

static gchar **
parrillada_playlist_get_selected_uris_real (ParrilladaPlaylist *playlist)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;
	GList *rows, *iter;
	GtkTreeIter row;
	gchar **uris = NULL, *uri;
	GPtrArray *array;
	gboolean valid;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (playlist->priv->tree));
	rows = gtk_tree_selection_get_selected_rows (selection, &model);

	if (rows == NULL)
		return NULL;

	array = g_ptr_array_sized_new (g_list_length (rows) + 1);
	for (iter = rows; iter; iter = iter->next) {
		path = iter->data;
		valid = gtk_tree_model_get_iter (model, &row, path);
		gtk_tree_path_free (path);

		if (valid == FALSE)
			continue;

		/* FIXME : we must find a way to reverse the list */
		/* check if it is a list or not */
		if (gtk_tree_model_iter_has_child (model, &row)) {
			GtkTreeIter child;

			if (gtk_tree_model_iter_children (model, &child, &row) == FALSE)
				continue;

			do {
				/* first check if the row is selected to prevent to put it in the list twice */
				if (gtk_tree_selection_iter_is_selected (selection, &child) == TRUE)
					continue;

				gtk_tree_model_get (model, &child,
						    PARRILLADA_PLAYLIST_URI_COL, &uri,
						    -1);
				g_ptr_array_add (array, uri);
			} while (gtk_tree_model_iter_next (model, &child));

			continue;
		}

		gtk_tree_model_get (model, &row,
				    PARRILLADA_PLAYLIST_URI_COL, &uri,
				    -1);
		g_ptr_array_add (array, uri);
	}

	g_list_free (rows);

	g_ptr_array_set_size (array, array->len + 1);
	uris = (gchar **) array->pdata;
	g_ptr_array_free (array, FALSE);
	return uris;
}

static gchar **
parrillada_playlist_get_selected_uris (ParrilladaURIContainer *container)
{
	ParrilladaPlaylist *playlist;

	playlist = PARRILLADA_PLAYLIST (container);
	return parrillada_playlist_get_selected_uris_real (playlist);
}

static gchar *
parrillada_playlist_get_selected_uri (ParrilladaURIContainer *container)
{
	ParrilladaPlaylist *playlist;
	gchar **uris = NULL;
	gchar *uri;

	playlist = PARRILLADA_PLAYLIST (container);
	uris = parrillada_playlist_get_selected_uris_real (playlist);

	if (uris) {
		uri = g_strdup (uris [0]);
		g_strfreev (uris);
		return uri;
	}

	return NULL;
}

static void
parrillada_playlist_remove_cb (GtkButton *button, ParrilladaPlaylist *playlist)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter row;
	GList *rows, *iter;
	gboolean valid;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (playlist->priv->tree));
	rows = gtk_tree_selection_get_selected_rows (selection, &model);

	if (rows == NULL)
		return;

	/* we just remove the lists removing particular songs would be a nonsense */
	/* we must reverse the list otherwise the last paths wouldn't be valid */
	for (iter = g_list_last (rows); iter; iter = iter->prev) {
		path = iter->data;
		valid = gtk_tree_model_get_iter (model, &row, path);
		gtk_tree_path_free (path);

		if (valid == FALSE)	/* if we remove the whole list it could happen that we try to remove twice a song */
			continue;

		if (gtk_tree_model_iter_has_child (model, &row)) {
			GtkTreeIter child;

			/* we remove the songs if it's a list */
			gtk_tree_model_iter_children (model, &child, &row);
			while (gtk_tree_store_remove (GTK_TREE_STORE (model), &child) == TRUE);
			gtk_tree_store_remove (GTK_TREE_STORE (model),
					       &row);
		}
	}

	g_list_free (rows);
}

static void
parrillada_playlist_drag_data_get_cb (GtkTreeView *tree,
				   GdkDragContext *drag_context,
				   GtkSelectionData *selection_data,
				   guint info,
				   guint time,
				   ParrilladaPlaylist *playlist)
{
	gchar **uris;

	uris = parrillada_playlist_get_selected_uris_real (playlist);
	gtk_selection_data_set_uris (selection_data, uris);
	g_strfreev (uris);
}

struct _ParrilladaPlaylistParseData {
	GtkTreeRowReference *reference;

	guint title:1;
	guint quiet:1;
};
typedef struct _ParrilladaPlaylistParseData ParrilladaPlaylistParseData;

static void
parrillada_playlist_dialog_error (ParrilladaPlaylist *playlist, const gchar *uri)
{
	gchar *name;
	gchar *primary;

	PARRILLADA_GET_BASENAME_FOR_DISPLAY (uri, name);

	primary = g_strdup_printf (_("Error parsing playlist \"%s\"."), name);
	parrillada_app_alert (parrillada_app_get_default (),
			   primary,
			   _("An unknown error occurred"),
			   GTK_MESSAGE_ERROR);
	g_free (primary);
	g_free (name);
}

static void
parrillada_playlist_parse_end (GObject *object,
			    gboolean cancelled,
			    gpointer callback_data)
{
	ParrilladaPlaylistParseData *data = callback_data;
	ParrilladaPlaylist *playlist = PARRILLADA_PLAYLIST (object);

	parrillada_playlist_decrease_activity_counter (playlist);

	gtk_tree_row_reference_free (data->reference);
	g_free (data);
}

static void
parrillada_playlist_parse_result (GObject *object,
			       GError *error,
			       const gchar *uri,
			       GFileInfo *info,
			       gpointer callback_data)
{
	gint num;
	gint64 total_length;
	GtkTreeModel *model;
	GtkTreePath *treepath;
	GtkTreeIter parent, row;
	gchar *len_string, *num_string;
	ParrilladaPlaylistParseData *data = callback_data;
	ParrilladaPlaylist *playlist = PARRILLADA_PLAYLIST (object);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (playlist->priv->tree));
	treepath = gtk_tree_row_reference_get_path (data->reference);
	gtk_tree_model_get_iter (model, &parent, treepath);
	gtk_tree_path_free (treepath);

	if (info && g_file_info_get_attribute_boolean (info, PARRILLADA_IO_IS_PLAYLIST)) {
		const gchar *playlist_title = NULL;

		/* The first entry returned is always the playlist as a whole:
		 * if it was successfully parsed uri is the title if any. If not
		 * it's simply the URI */

		/* this is for the playlist as a whole */
		if (error) {
			if (!data->quiet)
				parrillada_playlist_dialog_error (playlist, uri);

			gtk_list_store_remove (GTK_LIST_STORE (model), &parent);
			data->title = 1;
			return;
		}

		playlist_title = g_file_info_get_attribute_string (info, PARRILLADA_IO_PLAYLIST_TITLE);
		if (playlist_title)
			gtk_tree_store_set (GTK_TREE_STORE (model), &parent,
					    PARRILLADA_PLAYLIST_DISPLAY_COL, playlist_title,
					    -1);

		data->title = 1;
		return;
	}

    	/* See if the song can be added */
	if (!error && info && g_file_info_get_attribute_boolean (info, PARRILLADA_IO_HAS_AUDIO)) {
		gchar *name;
		guint64 len;
		const gchar *title;
		const gchar *genre;

		gtk_tree_store_append (GTK_TREE_STORE (model), &row, &parent);

		len = g_file_info_get_attribute_uint64 (info, PARRILLADA_IO_LEN);
		title = g_file_info_get_attribute_string (info, PARRILLADA_IO_TITLE);
		genre = g_file_info_get_attribute_string (info, PARRILLADA_IO_GENRE);

		if (len > 0)
			len_string = parrillada_units_get_time_string (len, TRUE, FALSE);
		else
			len_string = NULL;

		PARRILLADA_GET_BASENAME_FOR_DISPLAY (uri, name);
		gtk_tree_store_set (GTK_TREE_STORE (model), &row,
				    PARRILLADA_PLAYLIST_DISPLAY_COL, title ? title : name,
				    PARRILLADA_PLAYLIST_LEN_COL, len_string,
				    PARRILLADA_PLAYLIST_GENRE_COL, genre,
				    PARRILLADA_PLAYLIST_URI_COL, uri,
				    PARRILLADA_PLAYLIST_DSIZE_COL, len,
				    -1);
		g_free (name);
		g_free (len_string);

		if (len)
			total_length += len;
	}

	/* update the playlist information */
	num = gtk_tree_model_iter_n_children (model, &parent);
	if (!num)
		num_string = g_strdup (_("Empty"));
	else	/* Translators: %d is the number of songs */
		num_string = g_strdup_printf (ngettext ("%d song", "%d songs", num), num);

	/* get total length in time of the playlist */
	gtk_tree_model_get (model, &parent,
			    PARRILLADA_PLAYLIST_DSIZE_COL, &total_length,
			    -1);

  	if (total_length > 0)
		len_string = parrillada_units_get_time_string (total_length, TRUE, FALSE);
	else
		len_string = NULL;

	gtk_tree_store_set (GTK_TREE_STORE (model), &parent,
			    PARRILLADA_PLAYLIST_NB_SONGS_COL, num_string,
			    PARRILLADA_PLAYLIST_LEN_COL, len_string,
			    PARRILLADA_PLAYLIST_DSIZE_COL, total_length,
			    -1);
	g_free (len_string);
	g_free (num_string);
}

static GtkTreeRowReference *
parrillada_playlist_insert (ParrilladaPlaylist *playlist, const gchar *uri)
{
	gchar *name;
	GtkTreeIter parent;
	GtkTreeModel *model;
	GtkTreePath *treepath;
	GtkTreeRowReference *reference;

	/* actually add it */
	PARRILLADA_GET_BASENAME_FOR_DISPLAY (uri, name);
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (playlist->priv->tree));
	gtk_tree_store_append (GTK_TREE_STORE (model), &parent, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (model), &parent,
			    PARRILLADA_PLAYLIST_DISPLAY_COL, name,
			    PARRILLADA_PLAYLIST_URI_COL, uri,
			    PARRILLADA_PLAYLIST_NB_SONGS_COL, _("(loading…)"),
			    -1);
	g_free (name);

	treepath = gtk_tree_model_get_path (model, &parent);
	reference = gtk_tree_row_reference_new (model, treepath);
	gtk_tree_path_free (treepath);

	return reference;
}

static void
parrillada_playlist_add_uri_playlist (ParrilladaPlaylist *playlist,
				   const gchar *uri,
				   gboolean quiet)
{
	ParrilladaPlaylistParseData *data;

	data = g_new0 (ParrilladaPlaylistParseData, 1);
	data->reference = parrillada_playlist_insert (playlist, uri);

	if (!playlist->priv->parse_type)
		playlist->priv->parse_type = parrillada_io_register (G_OBJECT (playlist),
								  parrillada_playlist_parse_result,
								  parrillada_playlist_parse_end, 
								  NULL);

	parrillada_io_parse_playlist (uri,
				   playlist->priv->parse_type,
				   PARRILLADA_IO_INFO_PERM|
				   PARRILLADA_IO_INFO_MIME|
				   PARRILLADA_IO_INFO_METADATA,
				   data);
	parrillada_playlist_increase_activity_counter (playlist);
}

static void
parrillada_playlist_add_cb (GtkButton *button, ParrilladaPlaylist *playlist)
{
	GtkWidget *dialog, *toplevel;
	gchar *uri;
	GSList *uris, *iter;
	gint result;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (playlist));
	if (!gtk_widget_is_toplevel (toplevel))
		return;

	dialog = gtk_file_chooser_dialog_new (_("Select Playlist"),
					      GTK_WINDOW (toplevel),
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
					      NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog),
						 g_get_home_dir ());

	gtk_widget_show_all (dialog);
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_CANCEL) {
		gtk_widget_destroy (dialog);
		return;
	}

	uris = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dialog));
	gtk_widget_destroy (dialog);

	for (iter = uris; iter; iter = iter->next) {
		uri = iter->data;
		parrillada_playlist_add_uri_playlist (playlist, uri, FALSE);
		g_free (uri);
	}
	g_slist_free (uris);
}

static void
parrillada_playlist_search_hit_added_cb (ParrilladaSearchEngine *engine,
                                      gpointer hit,
                                      ParrilladaPlaylist *playlist)
{
	const char *uri;

	uri = parrillada_search_engine_uri_from_hit (engine, hit);
	parrillada_playlist_add_uri_playlist (playlist, uri, TRUE);
	parrillada_playlist_decrease_activity_counter (playlist);
}

static void
parrillada_playlist_search_hit_substracted_cb (ParrilladaSearchEngine *engine,
                                            gpointer hit,
                                            ParrilladaPlaylist *playlist)
{
	GtkTreeModel *model;
	GtkTreeIter row;
	const gchar *uri;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (playlist->priv->tree));
	uri = parrillada_search_engine_uri_from_hit (engine, hit);

	if (!gtk_tree_model_get_iter_first (model, &row))
		return;

	do {
		char *row_uri;

		gtk_tree_model_get (model, &row,
				    PARRILLADA_PLAYLIST_URI_COL, &row_uri,
				    -1);

		if (!strcmp (row_uri, uri)) {
			gtk_tree_store_remove (GTK_TREE_STORE (model), &row);
			g_free (row_uri);
			return;
		}

		g_free (row_uri);
	} while (gtk_tree_model_iter_next (model, &row));
}

static void
parrillada_playlist_search_finished_cb (ParrilladaSearchEngine *engine,
                                     ParrilladaPlaylist *playlist)
{
	parrillada_playlist_decrease_activity_counter (playlist);
}

static void
parrillada_playlist_search_error_cb (ParrilladaSearchEngine *engine,
                                  ParrilladaPlaylist *playlist)
{

}

static void
parrillada_playlist_row_activated_cb (GtkTreeView *tree,
				   GtkTreeIter *row,
				   GtkTreeViewColumn *column,
				   ParrilladaPlaylist *playlist)
{
	parrillada_uri_container_uri_activated (PARRILLADA_URI_CONTAINER (playlist));
}

static void
parrillada_playlist_selection_changed_cb (GtkTreeSelection *selection,
				       ParrilladaPlaylist *playlist)
{
	parrillada_uri_container_uri_selected (PARRILLADA_URI_CONTAINER (playlist));
}

static void
parrillada_playlist_init (ParrilladaPlaylist *obj)
{
	GtkWidget *hbox, *scroll;
	GtkTreeStore *store = NULL;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	obj->priv = g_new0 (ParrilladaPlaylistPrivate, 1);
	gtk_box_set_spacing (GTK_BOX (obj), PARRILLADA_PLAYLIST_SPACING);

	hbox = gtk_hbox_new (FALSE, 8);
	gtk_widget_show (hbox);

	obj->priv->button_add = gtk_button_new_from_stock (GTK_STOCK_ADD);
	gtk_widget_show (obj->priv->button_add);
	gtk_box_pack_end (GTK_BOX (hbox),
			  obj->priv->button_add,
			  FALSE,
			  FALSE,
			  0);
	g_signal_connect (G_OBJECT (obj->priv->button_add),
			  "clicked",
			  G_CALLBACK (parrillada_playlist_add_cb),
			  obj);

	obj->priv->button_remove = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	gtk_widget_show (obj->priv->button_remove);
	gtk_box_pack_end (GTK_BOX (hbox),
			  obj->priv->button_remove,
			  FALSE,
			  FALSE,
			  0);
	g_signal_connect (G_OBJECT (obj->priv->button_remove),
			  "clicked",
			  G_CALLBACK (parrillada_playlist_remove_cb),
			  obj);

	gtk_box_pack_end (GTK_BOX (obj), hbox, FALSE, FALSE, 0);

	store = gtk_tree_store_new (PARRILLADA_PLAYLIST_NB_COL,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING, 
				    G_TYPE_STRING,
				    G_TYPE_INT64);

	obj->priv->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	egg_tree_multi_drag_add_drag_support (GTK_TREE_VIEW (obj->priv->tree));

	gtk_tree_view_set_enable_tree_lines (GTK_TREE_VIEW (obj->priv->tree), TRUE);
	gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (obj->priv->tree), TRUE);
	gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW (obj->priv->tree), TRUE);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (obj->priv->tree), TRUE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (obj->priv->tree), TRUE);
	gtk_tree_view_set_expander_column (GTK_TREE_VIEW (obj->priv->tree),
					   PARRILLADA_PLAYLIST_DISPLAY_COL);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Playlists"),
							   renderer, "text",
							   PARRILLADA_PLAYLIST_DISPLAY_COL,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column,
						 PARRILLADA_PLAYLIST_DISPLAY_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (obj->priv->tree),
				     column);
	gtk_tree_view_column_set_expand (column, TRUE);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Number of Songs"),
							   renderer, "text",
							   PARRILLADA_PLAYLIST_NB_SONGS_COL,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column,
						 PARRILLADA_PLAYLIST_NB_SONGS_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (obj->priv->tree),
				     column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Length"),
							   renderer, "text",
							   PARRILLADA_PLAYLIST_LEN_COL,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column,
						 PARRILLADA_PLAYLIST_LEN_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (obj->priv->tree),
				     column);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Genre"), renderer,
							   "text",
							   PARRILLADA_PLAYLIST_GENRE_COL,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column,
						 PARRILLADA_PLAYLIST_GENRE_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (obj->priv->tree),
				     column);

	gtk_tree_view_set_search_column (GTK_TREE_VIEW (obj->priv->tree),
					 PARRILLADA_PLAYLIST_DISPLAY_COL);
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (obj->priv->tree),
						GDK_BUTTON1_MASK, ntables,
						nb_ntables,
						GDK_ACTION_COPY |
						GDK_ACTION_MOVE);

	g_signal_connect (G_OBJECT (obj->priv->tree), "drag_data_get",
			  G_CALLBACK (parrillada_playlist_drag_data_get_cb),
			  obj);

	g_signal_connect (G_OBJECT (obj->priv->tree), "row_activated",
			  G_CALLBACK (parrillada_playlist_row_activated_cb),
			  obj);

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (obj->priv->tree))),
			  "changed",
			  G_CALLBACK (parrillada_playlist_selection_changed_cb),
			  obj);

	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (obj->priv->tree)),
				     GTK_SELECTION_MULTIPLE);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
					     GTK_SHADOW_IN);

	gtk_container_add (GTK_CONTAINER (scroll), obj->priv->tree);
	gtk_box_pack_start (GTK_BOX (obj), scroll, TRUE, TRUE, 0);

	obj->priv->engine = parrillada_search_engine_new_default ();
	if (!obj->priv->engine)
		return;

	g_signal_connect (G_OBJECT (obj->priv->engine), "hit-added",
			  G_CALLBACK (parrillada_playlist_search_hit_added_cb),
			  obj);
	g_signal_connect (G_OBJECT (obj->priv->engine), "hit-removed",
			  G_CALLBACK (parrillada_playlist_search_hit_substracted_cb),
			  obj);
	g_signal_connect (G_OBJECT (obj->priv->engine), "search-finished",
			  G_CALLBACK (parrillada_playlist_search_finished_cb),
			  obj);
	g_signal_connect (G_OBJECT (obj->priv->engine), "search-error",
			  G_CALLBACK (parrillada_playlist_search_error_cb),
			  obj);
}

static void
parrillada_playlist_destroy (GtkObject *object)
{
	ParrilladaPlaylist *playlist = PARRILLADA_PLAYLIST (object);

	if (playlist->priv->id) {
		g_source_remove (playlist->priv->id);
		playlist->priv->id = 0;
	}

	if (playlist->priv->engine) {
		g_object_unref (playlist->priv->engine);
		playlist->priv->engine = NULL;
	}

	/* NOTE: we must do it here since cancel could call parrillada_playlist_end
	 * itself calling decrease_activity_counter. In finalize the latter will
	 * raise problems since the GdkWindow has been destroyed */
	if (playlist->priv->parse_type) {
		parrillada_io_cancel_by_base (playlist->priv->parse_type);
		parrillada_io_job_base_free (playlist->priv->parse_type);
		playlist->priv->parse_type = NULL;
	}

	if (GTK_OBJECT_CLASS (parrillada_playlist_parent_class)->destroy)
		GTK_OBJECT_CLASS (parrillada_playlist_parent_class)->destroy (object);
}

static void
parrillada_playlist_finalize (GObject *object)
{
	ParrilladaPlaylist *cobj;

	cobj = PARRILLADA_PLAYLIST (object);

	g_free (cobj->priv);

	G_OBJECT_CLASS (parrillada_playlist_parent_class)->finalize (object);
}

static void
parrillada_playlist_class_init (ParrilladaPlaylistClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkObjectClass *gtkobject_class = GTK_OBJECT_CLASS (klass);

	object_class->finalize = parrillada_playlist_finalize;
	gtkobject_class->destroy = parrillada_playlist_destroy;
}

static void
parrillada_playlist_iface_uri_container_init (ParrilladaURIContainerIFace *iface)
{
	iface->get_selected_uri = parrillada_playlist_get_selected_uri;
	iface->get_selected_uris = parrillada_playlist_get_selected_uris;
}

static void
parrillada_playlist_iface_layout_object_init (ParrilladaLayoutObjectIFace *iface)
{
	iface->get_proportion = parrillada_playlist_get_proportion;
}

GtkWidget *
parrillada_playlist_new ()
{
	ParrilladaPlaylist *obj;

	obj = PARRILLADA_PLAYLIST (g_object_new (PARRILLADA_TYPE_PLAYLIST, NULL));

	g_signal_connect (obj,
			  "expose-event",
			  G_CALLBACK (parrillada_playlist_expose_event_cb),
			  NULL);

	return GTK_WIDGET (obj);
}
