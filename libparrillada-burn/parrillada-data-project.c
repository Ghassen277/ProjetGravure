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

#include <string.h>
#include <stdio.h>
#include <libgen.h>
#include <sys/param.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <gio/gio.h>

#include "parrillada-units.h"

#include "parrillada-data-project.h"
#include "libparrillada-marshal.h"

#include "parrillada-misc.h"
#include "parrillada-io.h"

#include "burn-debug.h"
#include "parrillada-track-data.h"

typedef struct _ParrilladaDataProjectPrivate ParrilladaDataProjectPrivate;
struct _ParrilladaDataProjectPrivate
{
	ParrilladaFileNode *root;

	GCompareFunc sort_func;
	GtkSortType sort_type;

	GSList *spanned;

	/**
	 * In this table we record all changes (key = URI, data = list
	 * of nodes) that is:
	 * - files actually grafted (don't have a URI parent in the tree/table)
	 * - name changes for any node (whether it be because of invalid utf8
	 *   or because it was changed by the user.
	 * - files that were removed/moved somewhere else in the tree
	 * - unreadable files
	 * All these URIs/addresses should be excluded first. Then for each node
	 * there is in the list a graft point should be added. If there isn't 
	 * any node then that means that the file/URI will not appear in the 
	 * image */
	GHashTable *grafts;
	GHashTable *reference;

	GHashTable *joliet;

	guint ref_count;

	/* This is a counter for the number of files to be loaded */
	guint loading;

	guint is_loading_contents:1;
};

#define PARRILLADA_DATA_PROJECT_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_DATA_PROJECT, ParrilladaDataProjectPrivate))

#ifdef BUILD_INOTIFY

#include "parrillada-file-monitor.h"

G_DEFINE_TYPE (ParrilladaDataProject, parrillada_data_project, PARRILLADA_TYPE_FILE_MONITOR);

#else

G_DEFINE_TYPE (ParrilladaDataProject, parrillada_data_project, G_TYPE_OBJECT);

#endif


enum {
	JOLIET_RENAME_SIGNAL,
	NAME_COLLISION_SIGNAL,
	SIZE_CHANGED_SIGNAL,
	DEEP_DIRECTORY_SIGNAL,
	G2_FILE_SIGNAL,
	PROJECT_LOADED_SIGNAL,
	VIRTUAL_SIBLING_SIGNAL,
	LAST_SIGNAL
};

static guint parrillada_data_project_signals [LAST_SIGNAL] = {0};

/**
 * This is used in grafts hash table to identify created directories
 */
static const gchar NEW_FOLDER [] = "NewFolder";


typedef gboolean	(*ParrilladaDataNodeAddedFunc)	(ParrilladaDataProject *project,
							 ParrilladaFileNode *node,
							 const gchar *uri);

static void
parrillada_data_project_virtual_sibling (ParrilladaDataProject *self,
				      ParrilladaFileNode *node,
				      ParrilladaFileNode *sibling)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileTreeStats *stats;
	ParrilladaFileNode *children;
	ParrilladaFileNode *iter;

	if (sibling == node)
		return;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	g_signal_emit (self,
		       parrillada_data_project_signals [VIRTUAL_SIBLING_SIGNAL],
		       0,
		       node,
		       sibling);

	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	if (node) {
		/* we remove the virtual node, BUT, we keep its
		 * virtual children that will be appended to the
		 * node being moved in replacement. */
		/* NOTE: children MUST all be virtual */
		children = PARRILLADA_FILE_NODE_CHILDREN (sibling);
		for (iter = children; iter; iter = iter->next)
			parrillada_file_node_add (node, iter, NULL);

		sibling->union2.children = NULL;
	}
	else {
		/* Remove the virtual node. This should never happens */
		g_warning ("Virtual nodes could not be transfered");
	}

	/* Just destroy the node as it has no other 
	 * existence nor goal in existence but to create
	 * a collision. */
	parrillada_file_node_destroy (sibling, stats);
}

static gboolean
parrillada_data_project_node_signal (ParrilladaDataProject *self,
				  guint signal,
				  ParrilladaFileNode *node)
{
	GValue instance_and_params [2];
	GValue return_value;
	GValue *params;

	/* object which signalled */
	instance_and_params->g_type = 0;
	g_value_init (instance_and_params, G_TYPE_FROM_INSTANCE (self));
	g_value_set_instance (instance_and_params, self);

	/* arguments of signal (name) */
	params = instance_and_params + 1;
	params->g_type = 0;
	g_value_init (params, G_TYPE_POINTER);
	g_value_set_pointer (params, node);

	/* default to FALSE */
	return_value.g_type = 0;
	g_value_init (&return_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&return_value, FALSE);

	g_signal_emitv (instance_and_params,
			parrillada_data_project_signals [signal],
			0,
			&return_value);

	g_value_unset (instance_and_params);
	g_value_unset (params);

	/* In this case always remove the sibling */
	if (signal == NAME_COLLISION_SIGNAL && PARRILLADA_FILE_NODE_VIRTUAL (node))
		return FALSE;

	return g_value_get_boolean (&return_value);
}

static gboolean
parrillada_data_project_file_signal (ParrilladaDataProject *self,
				  guint signal,
				  const gchar *name)
{
	GValue instance_and_params [2];
	GValue return_value;
	GValue *params;

	/* object which signalled */
	instance_and_params->g_type = 0;
	g_value_init (instance_and_params, G_TYPE_FROM_INSTANCE (self));
	g_value_set_instance (instance_and_params, self);

	/* arguments of signal (name) */
	params = instance_and_params + 1;
	params->g_type = 0;
	g_value_init (params, G_TYPE_STRING);
	g_value_set_string (params, name);

	/* default to FALSE */
	return_value.g_type = 0;
	g_value_init (&return_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&return_value, FALSE);

	g_signal_emitv (instance_and_params,
			parrillada_data_project_signals [signal],
			0,
			&return_value);

	g_value_unset (instance_and_params);
	g_value_unset (params);

	return g_value_get_boolean (&return_value);
}

/**
 * Manages the references to a node
 */

guint
parrillada_data_project_reference_new (ParrilladaDataProject *self,
				    ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	guint retval;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	retval = priv->ref_count;
	while (g_hash_table_lookup (priv->reference, GINT_TO_POINTER (retval))) {
		retval ++;

		if (retval == G_MAXINT)
			retval = 1;

		/* this means there is no more room for reference */
		if (retval == priv->ref_count)
			return 0;
	}

	g_hash_table_insert (priv->reference,
			     GINT_TO_POINTER (retval),
			     node);
	priv->ref_count = retval + 1;
	if (priv->ref_count == G_MAXINT)
		priv->ref_count = 1;

	return retval;
}

void
parrillada_data_project_reference_free (ParrilladaDataProject *self,
				     guint reference)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	g_hash_table_remove (priv->reference, GINT_TO_POINTER (reference));
}

ParrilladaFileNode *
parrillada_data_project_reference_get (ParrilladaDataProject *self,
				    guint reference)
{
	ParrilladaDataProjectPrivate *priv;

	/* if it was invalidated then the node returned is NULL */
	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	return g_hash_table_lookup (priv->reference, GINT_TO_POINTER (reference));
}

static gboolean
parrillada_data_project_reference_remove_children_cb (gpointer key,
						   gpointer data,
						   gpointer callback_data)
{
	ParrilladaFileNode *node = data;
	ParrilladaFileNode *parent = callback_data;

	if (parrillada_file_node_is_ancestor (parent, node))
		return TRUE;

	return FALSE;
}

static void
parrillada_data_project_reference_invalidate (ParrilladaDataProject *self,
					   ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;

	/* used internally to invalidate reference whose node was removed */
	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	g_hash_table_foreach_remove (priv->reference,
				     (GHRFunc) parrillada_data_project_reference_remove_children_cb,
				     node);
}


/**
 * Manages the Joliet incompatible names 
 */
struct _ParrilladaJolietKey {
	ParrilladaFileNode *parent;
	gchar name [65];
};
typedef struct _ParrilladaJolietKey ParrilladaJolietKey;

static guint
parrillada_data_project_joliet_hash (gconstpointer data)
{
	guint hash_node;
	guint hash_name;
	const ParrilladaJolietKey *key = data;

	hash_node = g_direct_hash (key->parent);	
	hash_name = g_str_hash (key->name);
	return hash_node + hash_name;
}

static gboolean
parrillada_data_project_joliet_equal (gconstpointer a, gconstpointer b)
{
	const ParrilladaJolietKey *key1 = a;
	const ParrilladaJolietKey *key2 = b;

	if (key1->parent != key2->parent)
		return FALSE;

	if (strcmp (key1->name, key2->name))
		return FALSE;

	return TRUE;
}

static void
parrillada_data_project_joliet_set_key (ParrilladaJolietKey *key,
				     ParrilladaFileNode *node)
{
	gchar *dot;
	guint extension_len;

	/* key is equal to the parent path and the 64 first characters
	 * (always including the extension) of the name */
	dot = g_utf8_strrchr (PARRILLADA_FILE_NODE_NAME (node), -1, '.');

	if (dot)
		extension_len = strlen (dot);
	else
		extension_len = 0;

	if (dot && extension_len > 1 && extension_len < 5)
		sprintf (key->name,
			 "%.*s%s",
			 64 - extension_len,
			 PARRILLADA_FILE_NODE_NAME (node),
			 dot);
	else
		sprintf (key->name,
			 "%.64s",
			 PARRILLADA_FILE_NODE_NAME (node));

	key->parent = node->parent;
}

static void
parrillada_data_project_joliet_add_node (ParrilladaDataProject *self,
				      ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaJolietKey key;
	GSList *list;

	if (!node->parent)
		return;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!priv->joliet)
		priv->joliet = g_hash_table_new (parrillada_data_project_joliet_hash,
						 parrillada_data_project_joliet_equal);

	parrillada_data_project_joliet_set_key (&key, node);
	list = g_hash_table_lookup (priv->joliet, &key);
	if (!list) {
		ParrilladaJolietKey *table_key;

		/* create the actual key if it isn't in the hash */
		table_key = g_new0 (ParrilladaJolietKey, 1);
		parrillada_data_project_joliet_set_key (table_key, node);
		g_hash_table_insert (priv->joliet,
				     table_key,
				     g_slist_prepend (NULL, node));
	}
	else {
		list = g_slist_prepend (list, node);
		g_hash_table_insert (priv->joliet,
		                     &key,
		                     list);
	}

	/* Signal that we'll have a collision */
	g_signal_emit (self,
		       parrillada_data_project_signals [JOLIET_RENAME_SIGNAL],
		       0);
}

static gboolean
parrillada_data_project_joliet_remove_node (ParrilladaDataProject *self,
					 ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaJolietKey key;
	gpointer hash_key;
	gboolean success;
	gpointer list;

	if (!node->parent)
		return FALSE;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	parrillada_data_project_joliet_set_key (&key, node);
	success = g_hash_table_lookup_extended (priv->joliet,
						&key,
						&hash_key,
						&list);

	/* remove the exact path if it is a joliet non compliant file */
	if (!success)
		return FALSE;

	list = g_slist_remove (list, node);
	if (!list) {
		/* NOTE: we don't free the hash table now if it's empty,
		 * since this function could have been called by move
		 * function and in this case a path could probably be
		 * re-inserted */
		g_hash_table_remove (priv->joliet, &key);
		g_free (hash_key);
	}
	else
		g_hash_table_insert (priv->joliet,
				     &key,
				     list);

	return TRUE;
}

static gboolean
parrillada_data_project_joliet_remove_children_node_cb (gpointer data_key,
						     gpointer data,
						     gpointer callback_data)
{
	ParrilladaFileNode *parent = callback_data;
	ParrilladaJolietKey *key = data_key;
	GSList *nodes = data;

	if (parrillada_file_node_is_ancestor (parent, key->parent) || parent == key->parent) {
		g_slist_free (nodes);
		return TRUE;
	}

	return FALSE;
}

static void
parrillada_data_project_joliet_remove_children_node (ParrilladaDataProject *self,
						  ParrilladaFileNode *parent)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!parent)
		parent = priv->root;

	g_hash_table_foreach_remove (priv->joliet,
				     parrillada_data_project_joliet_remove_children_node_cb,
				     parent);
}

/**
 * Conversion functions
 */
gchar *
parrillada_data_project_node_to_uri (ParrilladaDataProject *self,
				  ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	GSList *list = NULL;
	gchar *retval;
	guint uri_len;
	GSList *iter;
	gchar *ptr;
	guint len;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (node->is_fake || node->is_imported)
		return NULL;

	if (node->is_grafted)
		return g_strdup (PARRILLADA_FILE_NODE_GRAFT (node)->node->uri);

	/* find the first grafted parent */
	uri_len = 0;
	list = NULL;
	for (; node; node = node->parent) {
		gchar *escaped_name;

		if (node->is_grafted)
			break;

		if (node == priv->root)
			break;

		/* the + 1 is for the separator */
		escaped_name = g_uri_escape_string (PARRILLADA_FILE_NODE_NAME (node),
						    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
						    FALSE);
		uri_len += strlen (escaped_name) + 1;
		list = g_slist_prepend (list, escaped_name);
	}

	/* The node here is the first grafted parent */
	if (!node || node->is_root) {
		g_slist_foreach (list, (GFunc) g_free, NULL);
		g_slist_free (list);
		return NULL;
	}

	/* NOTE: directories URIs shouldn't have a separator at end */
	len = strlen (PARRILLADA_FILE_NODE_GRAFT (node)->node->uri);
	uri_len += len;

	retval = g_new (gchar, uri_len + 1);

	memcpy (retval, PARRILLADA_FILE_NODE_GRAFT (node)->node->uri, len);
	ptr = retval + len;

	for (iter = list; iter; iter = iter->next) {
		gchar *escaped_name;

		escaped_name = iter->data;

		ptr [0] = G_DIR_SEPARATOR;
		ptr ++;

		len = strlen (escaped_name);
		memcpy (ptr, escaped_name, len);
		ptr += len;
	}
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	ptr [0] = '\0';
	return retval;
}
			  
static ParrilladaFileNode *
parrillada_data_project_find_child_node (ParrilladaFileNode *node,
				      const gchar *path)
{
	gchar *end;
	guint len;

	/* skip the separator if any */
	if (path [0] == G_DIR_SEPARATOR)
		path ++;

	/* find the next separator if any */
	end = g_utf8_strchr (path, -1, G_DIR_SEPARATOR);

	if (end)
		len = end - path;
	else
		len = strlen (path);

	/* go through the children nodes and find the name */
	for (node = PARRILLADA_FILE_NODE_CHILDREN (node); node; node = node->next) {
		if (node
		&& !strncmp (PARRILLADA_FILE_NODE_NAME (node), path, len)
		&& (PARRILLADA_FILE_NODE_NAME (node) [len] == G_DIR_SEPARATOR
		||  PARRILLADA_FILE_NODE_NAME (node) [len] == '\0')) {
			if (end)
				return parrillada_data_project_find_child_node (node, end);

			return node;
		}	
	}

	return NULL;
}

static GSList *
parrillada_data_project_uri_to_nodes (ParrilladaDataProject *self,
				   const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft;
	GSList *nodes = NULL;
	gchar *parent;
	GSList *iter;
	gchar *path;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* see if it grafted. If so, return the list */
	graft = g_hash_table_lookup (priv->grafts, uri);
	if (graft)
		return g_slist_copy (graft->nodes);

	/* keep going up until we reach root URI in grafts */
	parent = g_path_get_dirname (uri);
	while (strcmp (parent, G_DIR_SEPARATOR_S) && strchr (parent, G_DIR_SEPARATOR)) {
		graft = g_hash_table_lookup (priv->grafts, parent);
		if (graft)
			break;

		parent = dirname (parent);
	}

	if (!graft) {
		/* no graft point was found; there isn't any node */
		g_free (parent);
		return NULL;
	}

	uri += strlen (parent);
	g_free (parent);

	/* unescape URI */
	path = g_uri_unescape_string (uri, NULL);
	for (iter = graft->nodes; iter; iter = iter->next) {
		ParrilladaFileNode *node;

		node = iter->data;

		/* find the child node starting from the grafted node */
		node = parrillada_data_project_find_child_node (node, path);
		if (node)
			nodes = g_slist_prepend (nodes, node);
	}
	g_free (path);

	return nodes;
}

/**
 * Sorting
 * DataProject must be the one to handle that:
 * - ParrilladaFileNode can't send signal when something was reordered_id
 * - It is the object that adds files as a result of the exploration of a folder
 */

static void
parrillada_data_project_node_changed (ParrilladaDataProject *self,
				   ParrilladaFileNode *node)
{
	gint *array;
	ParrilladaDataProjectClass *klass;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);

	if (klass->node_changed)
		klass->node_changed (self, node);

	array = parrillada_file_node_need_resort (node, priv->sort_func);
	if (!array)
		return;

	if (klass->node_reordered)
		klass->node_reordered (self, node->parent, array);
	g_free (array);
}

static void
parrillada_data_project_reorder_children (ParrilladaDataProject *self,
				       ParrilladaFileNode *parent)
{
	gint *array;
	ParrilladaDataProjectClass *klass;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	array = parrillada_file_node_sort_children (parent, priv->sort_func);

	if (!array)
		return;

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->node_reordered)
		klass->node_reordered (self, parent, array);
	g_free (array);
}

static void
parrillada_data_project_resort_tree (ParrilladaDataProject *self,
				  ParrilladaFileNode *parent)
{
	ParrilladaFileNode *iter;

	for (iter = PARRILLADA_FILE_NODE_CHILDREN (parent); iter; iter = iter->next) {
		if (iter->is_file)
			continue;

		parrillada_data_project_reorder_children (self, iter);
		parrillada_data_project_resort_tree (self, iter);
	}	
}

static void
parrillada_data_project_reverse_children (ParrilladaDataProject *self,
				       ParrilladaFileNode *parent)
{
	gint *array;
	ParrilladaDataProjectClass *klass;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	array = parrillada_file_node_reverse_children (parent);

	if (!array)
		return;

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->node_reordered)
		klass->node_reordered (self, parent, array);
	g_free (array);
}

static void
parrillada_data_project_reverse_tree (ParrilladaDataProject *self,
				   ParrilladaFileNode *parent)
{
	ParrilladaFileNode *iter;

	for (iter = PARRILLADA_FILE_NODE_CHILDREN (parent); iter; iter = iter->next) {
		if (iter->is_file)
			continue;

		parrillada_data_project_reverse_children (self, iter);
		parrillada_data_project_reverse_tree (self, iter);
	}	
}
void
parrillada_data_project_set_sort_function (ParrilladaDataProject *self,
					GtkSortType sort_type,
					GCompareFunc sort_func)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (priv->sort_func != sort_func) {
		priv->sort_func = sort_func;
		priv->sort_type = sort_type;

		/* resort all the tree */
		parrillada_data_project_reorder_children (self, priv->root);
		parrillada_data_project_resort_tree (self, priv->root);
	}
	else if (priv->sort_type != sort_type) {
		priv->sort_type = sort_type;
		parrillada_data_project_reverse_children (self, priv->root);
		parrillada_data_project_reverse_tree (self, priv->root);
	}
}

/**
 *
 */

static gboolean
parrillada_data_project_uri_has_parent (ParrilladaDataProject *self,
				     const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	gchar *parent;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	parent = g_path_get_dirname (uri);

	/* keep going up until we reach a root URI */
	while (strcmp (parent, G_DIR_SEPARATOR_S) && strchr (parent, G_DIR_SEPARATOR)) {
		if (g_hash_table_lookup (priv->grafts, parent)) {
			g_free (parent);
			return TRUE;
		}

		parent = dirname (parent);
	}

	g_free (parent);
	return FALSE;
}

static gboolean
parrillada_data_project_uri_is_graft_needed (ParrilladaDataProject *self,
					  const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft_parent;
	ParrilladaURINode *graft;
	gchar *unescaped;
	gchar *parent;
	GSList *iter;
	gchar *name;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	graft = g_hash_table_lookup (priv->grafts, uri);

	/* get the URI name and parent. NOTE: name is unescaped to fit
	 * the names of nodes that are meant for display and therefore
	 * also unescaped. It's not necessary for parent URI. */
	unescaped = g_uri_unescape_string (uri, NULL);
	name = g_path_get_basename (unescaped);
	g_free (unescaped);

	parent = g_path_get_dirname (uri);
	for (iter = graft->nodes; iter; iter = iter->next) {
		ParrilladaFileNode *node;
		gchar *parent_uri;

		node = iter->data;
		if (node->parent == priv->root) {
			g_free (parent);
			g_free (name);
			return TRUE;
		}

		if (node->parent->is_fake) {
			g_free (parent);
			g_free (name);
			return TRUE;
		}

		/* make sure the node has the right name. */
		if (strcmp (PARRILLADA_FILE_NODE_NAME (node), name)) {
			g_free (parent);
			g_free (name);
			return TRUE;
		}

		/* make sure the node has the right parent. */
		parent_uri = parrillada_data_project_node_to_uri (self, node->parent);
		if (!parent_uri || strcmp (parent_uri, parent)) {
			g_free (parent_uri);
			g_free (parent);
			g_free (name);
			return TRUE;
		}

		g_free (parent_uri);
	}
	g_free (name);

	/* make sure no node is missing/removed. To do this find the 
	 * first parent URI in the hash and see if it has the same 
	 * number of graft point as this one. If not that means one
	 * node is missing. */
	graft_parent = g_hash_table_lookup (priv->grafts, parent);
	while (parent && !graft_parent) {
		parent = dirname (parent);
		graft_parent = g_hash_table_lookup (priv->grafts, parent);
	}
	g_free (parent);

	if (g_slist_length (graft_parent->nodes) != g_slist_length (graft->nodes))
		return TRUE;

	return FALSE;
}

static void
parrillada_data_project_uri_remove_graft (ParrilladaDataProject *self,
				       const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	ParrilladaURINode *graft = NULL;
	gchar *key = NULL;
	GSList *iter;
	GSList *next;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	g_hash_table_lookup_extended (priv->grafts,
				      uri,
				      (gpointer *) &key,
				      (gpointer *) &graft);

	/* Put the nodes in ungrafted state */
	for (iter = graft->nodes; iter; iter = next) {
		ParrilladaFileNode *iter_node;

		next = iter->next;
		iter_node = iter->data;
		parrillada_file_node_ungraft (iter_node);
	}

	/* we have to free the key and data ourselves */
	g_hash_table_remove (priv->grafts, uri);

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->uri_removed)
		klass->uri_removed (self, uri);

	if (key && key != NEW_FOLDER)
		parrillada_utils_unregister_string (key);

	if (graft) {
		/* NOTE: no need to free graft->uri since that's the key */
		g_slist_free (graft->nodes);
		g_free (graft);
	}
}

static gboolean
parrillada_data_project_graft_is_needed (ParrilladaDataProject *self,
				      ParrilladaURINode *uri_node)
{
	if (uri_node->nodes)
		return TRUE;

	/* there aren't any node grafted for this URI. See if we should keep the
	 * URI in the hash; if so, the URI must have parents in the hash */
	if (parrillada_data_project_uri_has_parent (self, uri_node->uri)) {
		/* here that means that this URI is nowhere in the tree but has
		 * parent URIs which are grafted. So keep it in the hash to
		 * signal that URI is not in the tree. */
		return TRUE;
	}

	parrillada_data_project_uri_remove_graft (self, uri_node->uri);
	return FALSE;
}

static ParrilladaURINode *
parrillada_data_project_uri_add_graft (ParrilladaDataProject *self,
				    const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	graft = g_new0 (ParrilladaURINode, 1);
	if (uri != NEW_FOLDER)
		graft->uri = parrillada_utils_register_string (uri);
	else
		graft->uri = (gchar *) NEW_FOLDER;

	g_hash_table_insert (priv->grafts,
			     graft->uri,
			     graft);

	return graft;
}

static ParrilladaURINode *
parrillada_data_project_uri_ensure_graft (ParrilladaDataProject *self,
				       const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	graft = g_hash_table_lookup (priv->grafts, uri);
	if (graft)
		return graft;

	return parrillada_data_project_uri_add_graft (self, uri);
}

static ParrilladaURINode *
parrillada_data_project_uri_graft_nodes (ParrilladaDataProject *self,
				      const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft;
	GSList *nodes;
	GSList *iter;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* Find all the nodes that should be grafted.
	 * NOTE: this must be done before asking for a new graft */
	nodes = parrillada_data_project_uri_to_nodes (self, uri);
	graft = parrillada_data_project_uri_add_graft (self, uri);

	/* NOTE: all nodes should have the exact same size. */

	/* Tell the nodes they are all grafted. */
	for (iter = nodes; iter; iter = iter->next) {
		ParrilladaFileNode *iter_node;

		iter_node = iter->data;
		parrillada_file_node_graft (iter_node, graft);
	}
	g_slist_free (nodes);

	return graft;
}

static void
parrillada_data_project_add_node_and_children (ParrilladaDataProject *self,
					    ParrilladaFileNode *node,
					    ParrilladaDataNodeAddedFunc klass_node_added_func)
{
	ParrilladaFileNode *iter;

	klass_node_added_func (self, node, NULL);

	/* now we probably have to call node_added on every single child */
	for (iter = PARRILLADA_FILE_NODE_CHILDREN (node); iter; iter = iter->next) {
		if (!iter->is_file)
			parrillada_data_project_add_node_and_children (self, iter, klass_node_added_func);
		else
			klass_node_added_func (self, iter, NULL);
	}
}

struct _ParrilladaRemoveChildrenGraftData {
	ParrilladaFileNode *node;
	ParrilladaDataProject *project;
};
typedef struct _ParrilladaRemoveChildrenGraftData ParrilladaRemoveChildrenGraftData;

static gboolean
parrillada_data_project_remove_node_children_graft_cb (const gchar *key,
						    ParrilladaURINode *graft,
						    ParrilladaRemoveChildrenGraftData *data)
{
	GSList *iter;
	GSList *next;

	/* Remove all children nodes of node.
	 * NOTE: here there is nothing to do about the size. */
	for (iter = graft->nodes; iter; iter = next) {
		ParrilladaFileNode *iter_node;

		iter_node = iter->data;
		next = iter->next;

		if (data->node == iter_node)
			continue;

		if (parrillada_file_node_is_ancestor (data->node, iter_node))
			parrillada_file_node_ungraft (iter_node);
	}

	if (graft->nodes)
		return FALSE;

	/* Check if this graft should be removed. If not, it should 
	 * have a parent URI in the graft. */
	return (parrillada_data_project_uri_has_parent (data->project, key) == FALSE);
}

static void
parrillada_data_project_remove_node_children_graft (ParrilladaDataProject *self,
						 ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaRemoveChildrenGraftData callback_data;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	callback_data.project = self;
	callback_data.node = node;
	g_hash_table_foreach_remove (priv->grafts,
				     (GHRFunc) parrillada_data_project_remove_node_children_graft_cb,
				     &callback_data);
}

#ifdef BUILD_INOTIFY

static gboolean
parrillada_data_project_monitor_cancel_foreach_cb (gpointer data,
						gpointer callback_data)
{
	ParrilladaFileNode *node = data;
	ParrilladaFileNode *parent = callback_data;

	if (node == parent)
		return TRUE;

	return parrillada_file_node_is_ancestor (parent, node);
}

#endif

static void
parrillada_data_project_node_removed (ParrilladaDataProject *self,
				   ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

#ifdef BUILD_INOTIFY

	/* remove all monitoring */
	if (node->is_monitored)
		parrillada_file_monitor_foreach_cancel (PARRILLADA_FILE_MONITOR (self),
						     parrillada_data_project_monitor_cancel_foreach_cb,
						     node);
#endif

	/* invalidate possible references (including for children)*/
	parrillada_data_project_reference_invalidate (self, node);

	/* remove all children graft points; do it all at once. */
	parrillada_data_project_remove_node_children_graft (self, node);

	/* remove all children nodes + node from the joliet table */
	parrillada_data_project_joliet_remove_children_node (self, node);

	if (strlen (PARRILLADA_FILE_NODE_NAME (node)) > 64)
		parrillada_data_project_joliet_remove_node (self, node);

	/* See if this node is grafted; if so remove it from the hash.
	 * If not, get the URI and all the nodes with the same URI and
	 * add the list (less this node) to the hash.
	 * NOTE: imported file case should not be addressed here*/
	if (node->is_grafted) {
		ParrilladaGraft *graft;
		ParrilladaURINode *uri_node;

		/* NOTE: in this case there is no size changes to do 
		 * for nodes or grafts. The size change for the whole
		 * project will be made during the addition of all the
		 * graft sizes. If there is no more nodes for this
		 * graft then it won't be taken into account. */

		/* There is already a graft */
		graft = PARRILLADA_FILE_NODE_GRAFT (node);
		uri_node = graft->node;

		/* NOTE: after this function the graft is invalid */
		parrillada_file_node_ungraft (node);

		if (!uri_node->nodes) {
			/* that's the last node grafted for this URI.
			 * There are no more nodes for this URI after.
			 * See if we should keep the URI in the hash;
			 * if so, the URI must have parents in the hash
			 */
			if (!parrillada_data_project_uri_has_parent (self, uri_node->uri))
				parrillada_data_project_uri_remove_graft (self, uri_node->uri);
		}
	}
	else if (!node->is_imported) {
		gchar *uri;

		/* This URI will need a graft if it hasn't one yet */
		uri = parrillada_data_project_node_to_uri (self, node);

		if (!g_hash_table_lookup (priv->grafts, uri))
			parrillada_data_project_uri_graft_nodes (self, uri);

		/* NOTE: since the URI wasn't grafted it has to have a
		 * valid parent that's why we don't check the graft 
		 * validity afterwards */
		g_free (uri);
	}
}

static void
parrillada_data_project_remove_real (ParrilladaDataProject *self,
				  ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	ParrilladaFileNode *former_parent;
	ParrilladaFileTreeStats *stats;
	guint former_position;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	parrillada_data_project_node_removed (self, node);

	/* save parent, unparent it, signal the removal */
	former_parent = node->parent;
	former_position = parrillada_file_node_get_pos_as_child (node);

	parrillada_file_node_unlink (node);

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->node_removed)
		klass->node_removed (self, former_parent, former_position, node);

	/* save imported nodes in their parent structure or destroy it */
	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	if (!node->is_imported)
		parrillada_file_node_destroy (node, stats);
	else
		parrillada_file_node_save_imported (node,
						 stats,
						 former_parent,
						 priv->sort_func);

	g_signal_emit (self,
		       parrillada_data_project_signals [SIZE_CHANGED_SIGNAL],
		       0);
}

static void
parrillada_data_project_convert_to_fake (ParrilladaDataProject *self,
				      ParrilladaFileNode *node)
{
	ParrilladaURINode *graft;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* make it a fake directory not to break order */
	node->is_fake = TRUE;
	node->is_loading = FALSE;
	node->is_tmp_parent = FALSE;

	parrillada_file_node_ungraft (node);
	graft = parrillada_data_project_uri_ensure_graft (self, NEW_FOLDER);
	parrillada_file_node_graft (node, graft);
	parrillada_data_project_node_changed (self, node);

	/* Remove 2 since we're not going to load its contents */
	priv->loading -= 2;
	g_signal_emit (self,
		       parrillada_data_project_signals [PROJECT_LOADED_SIGNAL],
		       0,
		       priv->loading);
}

void
parrillada_data_project_remove_node (ParrilladaDataProject *self,
				  ParrilladaFileNode *node)
{
	ParrilladaFileNode *imported_sibling;
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (node->is_tmp_parent) {
		/* This node was created as a temporary parent, it doesn't exist
		 * so we replace it with a fake one. */

		/* Don't exclude any URI since it doesn't exist apparently */

		/* No need to check for deep directory since that was in the
		 * project as such. Keep it that way. */

		parrillada_data_project_convert_to_fake (self, node);
		return;
	}
	else if (priv->loading && node->is_grafted) {
		/* that means that's a grafted that failed to load */
		parrillada_data_project_convert_to_fake (self, node);
		return;
	}

	/* check for a sibling now (before destruction) */
	imported_sibling = parrillada_file_node_check_imported_sibling (node);
	parrillada_data_project_remove_real (self, node);

	/* add the sibling now (after destruction) */
	if (!imported_sibling)
		return;

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	parrillada_file_node_add (imported_sibling->parent, imported_sibling, priv->sort_func);
	parrillada_data_project_add_node_and_children (self, imported_sibling, klass->node_added);
}

void
parrillada_data_project_destroy_node (ParrilladaDataProject *self,
				   ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	ParrilladaFileNode *former_parent;
	ParrilladaFileTreeStats *stats;
	guint former_position;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	parrillada_data_project_node_removed (self, node);

	/* unlink the node and signal the removal */
	former_parent = node->parent;
	former_position = parrillada_file_node_get_pos_as_child (node);

	parrillada_file_node_unlink (node);

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->node_removed)
		klass->node_removed (self, former_parent, former_position, node);

	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	parrillada_file_node_destroy (node, stats);

	g_signal_emit (self,
		       parrillada_data_project_signals [SIZE_CHANGED_SIGNAL],
		       0);

	/* NOTE: no need to check for imported_sibling here since this function
	 * actually destroys all nodes including imported ones and is mainly 
	 * used to remove imported nodes. */
}

static gboolean
parrillada_data_project_is_deep (ParrilladaDataProject *self,
			      ParrilladaFileNode *parent,
			      const gchar *name,
			      gboolean isfile)
{
	gint parent_depth;
	ParrilladaFileTreeStats *stats;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* if there are already deep files accepts new ones (includes the 
	 * possible imported ones. */
	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	if (stats->num_deep)
		return TRUE;

	/* This node could have been moved beyond the depth 6 only in one case,
	 * which is with imported directories. Otherwise since we check
	 * directories for a depth of 5, its parent would have already been 
	 * detected. */
	parent_depth = parrillada_file_node_get_depth (parent);
	if (!isfile) {
		if (parent_depth < 5)
			return TRUE;
	}
	else {
		if (parent_depth < 6)
			return TRUE;
	}

	if (parrillada_data_project_file_signal (self, DEEP_DIRECTORY_SIGNAL, name))
		return FALSE;

	return TRUE;
}

static void
parrillada_data_project_remove_sibling (ParrilladaDataProject *self,
				     ParrilladaFileNode *sibling,
				     ParrilladaFileNode *replacement)
{
	ParrilladaDataProjectPrivate *priv;

	if (sibling != replacement)
		return;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
		ParrilladaFileTreeStats *stats;
		ParrilladaFileNode *children;
		ParrilladaFileNode *iter;

		stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
		if (replacement) {
			/* we remove the virtual node, BUT, we keep its
			 * virtual children that will be appended to the
			 * node being moved in replacement. */
			/* NOTE: children MUST all be virtual */
			children = PARRILLADA_FILE_NODE_CHILDREN (sibling);
			for (iter = children; iter; iter = iter->next)
				parrillada_file_node_add (replacement, iter, NULL);

			sibling->union2.children = NULL;
		}
		else {
			/* Remove the virtual node. This should never happens */
			g_warning ("Virtual nodes could not be transfered");
		}

		/* Just destroy the node as it has no other 
		 * existence nor goal in existence but to create
		 * a collision. */
		parrillada_file_node_destroy (sibling, stats);
	}
	else {
		/* The node existed and the user wants the existing to 
		 * be replaced, so we delete that node (since the new
		 * one would have the old one's children otherwise). */
		 parrillada_data_project_remove_real (self, sibling);
	 }
}

gboolean
parrillada_data_project_move_node (ParrilladaDataProject *self,
				ParrilladaFileNode *node,
				ParrilladaFileNode *parent)
{
	ParrilladaFileNode *imported_sibling;
	ParrilladaFileNode *target_sibling;
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	ParrilladaFileNode *former_parent;
	ParrilladaFileTreeStats *stats;
	guint former_position;
	gboolean check_graft;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!parent)
		parent = priv->root;
	else if (parent->is_file || parent->is_loading)
		return FALSE;

	/* can't be moved to the same directory */
	if (node->parent == parent)
		return FALSE;

	/* see if node is not a parent of parent */
	if (parrillada_file_node_is_ancestor (node, parent))
		return FALSE;

	/* see if we won't break the max path depth barrier */
	if (!parrillada_data_project_is_deep (self, parent, PARRILLADA_FILE_NODE_NAME (node), node->is_file))
		return FALSE;

	/* One case could make us fail: if there is the same name in
	 * the directory: in that case return FALSE; check now. */
	target_sibling = parrillada_file_node_check_name_existence (parent, PARRILLADA_FILE_NODE_NAME (node));
	if (target_sibling) {
		if (PARRILLADA_FILE_NODE_VIRTUAL (target_sibling)) {
			parrillada_data_project_virtual_sibling (self, node, target_sibling);
			target_sibling = NULL;
		}
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, target_sibling))
			return FALSE;
	}

	/* If node was in the joliet incompatible table, remove it */
	parrillada_data_project_joliet_remove_node (self, node);

	/* check if this file was hiding an imported file. One exception is if
	 * there is a sibling in the target directory which is the parent of our
	 * node. */
	if (!target_sibling || !parrillada_file_node_is_ancestor (target_sibling, node))
		imported_sibling = parrillada_file_node_check_imported_sibling (node);
	else
		imported_sibling = NULL;

	if (!node->is_grafted) {
		gchar *uri;

		/* Get the URI and all the nodes with the same URI and 
		 * add the list to the hash => add a graft.
		 * See note underneath: if it wasn't grafted before the
		 * move it should probably be a graft now.
		 * NOTE: we need to do it now before it gets unparented. */
		uri = parrillada_data_project_node_to_uri (self, node);
		if (!g_hash_table_lookup (priv->grafts, uri))
			parrillada_data_project_uri_graft_nodes (self, uri);
		g_free (uri);

		check_graft = FALSE;
	}
	else
		check_graft = TRUE;

	/* really reparent it; signal:
	 * - old location removal
	 * - new location addition */

	/* unparent node now in case its target sibling is a parent */
	former_parent = node->parent;
	former_position = parrillada_file_node_get_pos_as_child (node);
	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	parrillada_file_node_move_from (node, stats);

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (former_parent && klass->node_removed)
		klass->node_removed (self, former_parent, former_position, node);

	if (target_sibling)
		parrillada_data_project_remove_sibling (self,
						     target_sibling,
						     node);

	parrillada_file_node_move_to (node, parent, priv->sort_func);

	if (klass->node_added)
		klass->node_added (self, node, NULL);

	if (check_graft) {
		ParrilladaGraft *graft;
		ParrilladaURINode *uri_node;

		graft = PARRILLADA_FILE_NODE_GRAFT (node);
		uri_node = graft->node;

		/* check if still need a graft point after the location change. */
		if (!parrillada_data_project_uri_is_graft_needed (self, uri_node->uri))
			parrillada_data_project_uri_remove_graft (self, uri_node->uri);
	}

	/* Check joliet name compatibility; this must be done after move as it
	 * depends on the parent. */
	if (strlen (PARRILLADA_FILE_NODE_NAME (node)) > 64)
		parrillada_data_project_joliet_add_node (self, node);

	if (imported_sibling) {
		ParrilladaDataProjectClass *klass;

		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
		parrillada_file_node_add (imported_sibling->parent, imported_sibling, priv->sort_func);
		if (klass->node_added)
			parrillada_data_project_add_node_and_children (self, imported_sibling, klass->node_added);
	}

	/* NOTE: if it has come back to its original location on the 
	 * file system then it has to be grafted; if it was moved back
	 * to origins and wasn't grafted that means it comes from
	 * another graft of the sames parent, so either it has the same
	 * name as its other copy, which is impossible, or the other
	 * copy was moved and then there are graft points.*/

	return TRUE;
}

gboolean
parrillada_data_project_rename_node (ParrilladaDataProject *self,
				  ParrilladaFileNode *node,
				  const gchar *name)
{
	ParrilladaFileNode *imported_sibling;
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *sibling;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* Don't allow rename to succeed if name is the empty string */
	if (strlen (name) < 1) {
		return FALSE;
	}

	/* make sure there isn't the same name in the directory: if so, that's 
	 * simply not possible to rename. */
	sibling = parrillada_file_node_check_name_existence (node->parent, name);
	if (sibling) {
		if (PARRILLADA_FILE_NODE_VIRTUAL (sibling))
			parrillada_data_project_virtual_sibling (self, node, sibling);
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, sibling))
			return FALSE;
		else if (sibling != node) {
			/* The node existed and the user wants the existing to 
			 * be replaced, so we delete that node (since the new
			 * one would have the old one's children otherwise). */
			parrillada_data_project_remove_real (self, sibling);
		}
	}

	/* If node was in the joliet incompatible table, remove it */
	parrillada_data_project_joliet_remove_node (self, node);

	/* see if this node didn't replace an imported one. If so the old 
	 * imported node must re-appear in the tree. */
	imported_sibling = parrillada_file_node_check_imported_sibling (node);

	if (!node->is_grafted) {
		gchar *uri;

		/* The node URI doesn't exist in URI hash. That's why
		 * we need to add one with all nodes having the same
		 * URI. */
		uri = parrillada_data_project_node_to_uri (self, node);
		if (!g_hash_table_lookup (priv->grafts, uri))
			parrillada_data_project_uri_graft_nodes (self, uri);
		g_free (uri);

		/* now we can change the name */
		parrillada_file_node_rename (node, name);
	}
	else {
		ParrilladaURINode *uri_node;
		ParrilladaGraft *graft;

		/* change the name now so we can check afterwards if a
		 * graft is still needed (the name could have been 
		 * changed back to the original one). */
		graft = PARRILLADA_FILE_NODE_GRAFT (node);
		uri_node = graft->node;

		parrillada_file_node_rename (node, name);
		if (!parrillada_data_project_uri_is_graft_needed (self, uri_node->uri))
			parrillada_data_project_uri_remove_graft (self, uri_node->uri);
	}

	/* Check joliet name compatibility. This must be done after the
	 * node information have been setup. */
	if (strlen (name) > 64)
		parrillada_data_project_joliet_add_node (self, node);

	parrillada_data_project_node_changed (self, node);

	if (imported_sibling) {
		ParrilladaDataProjectClass *klass;

		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);

		parrillada_file_node_add (sibling->parent, imported_sibling, priv->sort_func);
		if (klass->node_added)
			parrillada_data_project_add_node_and_children (self, imported_sibling, klass->node_added);
	}

	return TRUE;
}

static gboolean
parrillada_data_project_add_node_real (ParrilladaDataProject *self,
				    ParrilladaFileNode *node,
				    ParrilladaURINode *graft,
				    const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* See if we should create a graft for the node.
	 * NOTE: if we create a graft we create a graft for all nodes
	 * that have the same URI in the tree too. */
	if (graft) {
		/* If there is already a graft for this URI, then add node */
		parrillada_file_node_graft (node, graft);
	}
	else if (node->parent == priv->root) {
		/* The node is at the root of the project; graft it as well as
		 * all the nodes already in the tree with the same URI */
		graft = parrillada_data_project_uri_graft_nodes (self, uri);
		parrillada_file_node_graft (node, graft);
	}
	else if (node->is_fake) {
		/* The node is a fake directory; graft it as well as all the 
		 * nodes already in the tree with the same URI */
		graft = parrillada_data_project_uri_graft_nodes (self, uri);
	}
	else {
		gchar *parent_uri;
		gchar *name_uri;

		parent_uri = parrillada_data_project_node_to_uri (self, node->parent);
		name_uri = g_path_get_basename (uri);

		/* NOTE: in here use a special function here since that node 
		 * could already be in the tree but under its rightful parent
		 * and then it won't have any graft yet. That's why these nodes
		 * need to be grafted as well. */ 
		if (parent_uri) {
			guint parent_len;

			parent_len = strlen (parent_uri);
			if (strncmp (parent_uri, uri, parent_len)
			||  uri [parent_len] != G_DIR_SEPARATOR
			|| !name_uri
			|| !PARRILLADA_FILE_NODE_NAME (node)
			||  strcmp (name_uri, PARRILLADA_FILE_NODE_NAME (node))) {
				/* The node hasn't been put under its rightful
				 * parent from the original file system. That
				 * means we must add a graft */
				graft = parrillada_data_project_uri_graft_nodes (self, uri);
				parrillada_file_node_graft (node, graft);
			}
			/* NOTE: we don't need to check if the nodes's name
			 * is the same as the one of the URI. This function is
			 * used by two other functions that pass the URI name
			 * as name to the info so that should always be fine. */

			 /* NOTE: for ungrafted nodes the parent graft size is
			 * updated when setting info on node. */
			g_free (parent_uri);
		}
		else {
			/* its father is probably an fake empty directory */
			graft = parrillada_data_project_uri_graft_nodes (self, uri);
			parrillada_file_node_graft (node, graft);
		}
		g_free (name_uri);
	}

	if (!priv->is_loading_contents) {
		ParrilladaDataProjectClass *klass;

		/* Signal that something has changed in the tree */
		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
		if (klass->node_added
		&& !klass->node_added (self, node, uri != NEW_FOLDER? uri:NULL))
			return FALSE;
	}

	/* check joliet compatibility; do it after node was created. */
	if (strlen (PARRILLADA_FILE_NODE_NAME (node)) > 64)
		parrillada_data_project_joliet_add_node (self, node);

	return TRUE;
}

void
parrillada_data_project_restore_uri (ParrilladaDataProject *self,
				  const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft;
	gchar *parent_uri;
	GSList *nodes;
	GSList *iter;
	gchar *name;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	name = parrillada_utils_get_uri_name (uri);

	parent_uri = g_path_get_dirname (uri);
	nodes = parrillada_data_project_uri_to_nodes (self, parent_uri);
	g_free (parent_uri);

	graft = g_hash_table_lookup (priv->grafts, uri);
	for (iter = nodes; iter; iter = iter->next) {
		ParrilladaFileNode *parent;
		ParrilladaFileNode *node;

		parent = iter->data;

		/* restore it if it wasn't already and can (no existing node
		 * with the same name must exist). */
		if (parrillada_file_node_check_name_existence (parent, name))
			continue;

		node = parrillada_file_node_new_loading (name);
		parrillada_file_node_add (parent, node, priv->sort_func);
		parrillada_data_project_add_node_real (self, node, graft, uri);
	}
	g_slist_free (nodes);
	g_free (name);

	/* see if we still need a graft after all that */
	if (graft && !parrillada_data_project_uri_is_graft_needed (self, uri))
		parrillada_data_project_uri_remove_graft (self, uri);
}

void
parrillada_data_project_exclude_uri (ParrilladaDataProject *self,
				  const gchar *uri)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* NOTE: we don't remove the existing nodes in case one was
	 * previously restored. In any case remove all loading nodes.
	 * There is one exception if the status is unreadable */

	/* make sure a graft exists to signal that it is excluded */
	if (!g_hash_table_lookup (priv->grafts, uri)) {
		/* NOTE: if the graft point exists it should be empty */
		parrillada_data_project_uri_add_graft (self, uri);
	}
}

ParrilladaFileNode *
parrillada_data_project_add_imported_session_file (ParrilladaDataProject *self,
						GFileInfo *info,
						ParrilladaFileNode *parent)
{
	ParrilladaFileNode *node;
	ParrilladaFileNode *sibling;
	ParrilladaDataProjectClass *klass;
	ParrilladaDataProjectPrivate *priv;

	g_return_val_if_fail (PARRILLADA_IS_DATA_PROJECT (self), NULL);
	g_return_val_if_fail (info != NULL, NULL);

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!parent)
		parent = priv->root;

	sibling = parrillada_file_node_check_name_existence (parent, g_file_info_get_name (info));
	if (sibling) {
		/* The node exists but it may be that we've loaded the project
		 * before. Then the necessary directories to hold the grafted
		 * files will have been created as fake directories. We need to
		 * replace those whenever we run into one but not lose their 
		 * children. */
		if (PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			node = parrillada_file_node_new_imported_session_file (info);
			parrillada_data_project_virtual_sibling (self, node, sibling);
		}
		else if (sibling->is_fake && sibling->is_tmp_parent) {
			ParrilladaGraft *graft;
			ParrilladaURINode *uri_node;

			graft = PARRILLADA_FILE_NODE_GRAFT (sibling);
			uri_node = graft->node;

			/* NOTE after this function graft is invalid */
			parrillada_file_node_ungraft (sibling);

			/* see if uri_node is still needed */
			if (!uri_node->nodes
			&&  !parrillada_data_project_uri_has_parent (self, uri_node->uri))
				parrillada_data_project_uri_remove_graft (self, uri_node->uri);

			if (sibling->is_file)
				sibling->is_fake = FALSE;
			else
				sibling->union3.imported_address = g_file_info_get_attribute_int64 (info, PARRILLADA_IO_DIR_CONTENTS_ADDR);

			sibling->is_imported = TRUE;
			sibling->is_tmp_parent = FALSE;

			/* Something has changed, tell the tree */
			klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
			if (klass->node_changed)
				klass->node_changed (self, sibling);

			return sibling;
		}
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, sibling))
			return NULL;
		else {
			/* The node existed and the user wants the existing to 
			 * be replaced, so we delete that node (since the new
			 * one would have the old one's children otherwise). */
			parrillada_data_project_remove_real (self, sibling);
			node = parrillada_file_node_new_imported_session_file (info);
		}
	}
	else
		node = parrillada_file_node_new_imported_session_file (info);

	/* Add it (we must add a graft) */
	parrillada_file_node_add (parent, node, priv->sort_func);

	/* In this case, there can be no graft, and furthermore the
	 * lengths of the names are not our problem. Just signal that
	 * something has changed in the tree */
	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->node_added)
		klass->node_added (self, node, NULL);

	return node;
}

ParrilladaFileNode *
parrillada_data_project_add_empty_directory (ParrilladaDataProject *self,
					  const gchar *name,
					  ParrilladaFileNode *parent)
{
	ParrilladaFileNode *node;
	ParrilladaURINode *graft;
	ParrilladaFileNode *sibling;
	ParrilladaDataProjectPrivate *priv;

	g_return_val_if_fail (PARRILLADA_IS_DATA_PROJECT (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!parent)
		parent = priv->root;

	/* check directory_depth */
	if (!parrillada_data_project_is_deep (self, parent, name, FALSE))
		return NULL;

	sibling = parrillada_file_node_check_name_existence (parent, name);
	if (sibling) {
		if (PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			node = parrillada_file_node_new_empty_folder (name);
			parrillada_data_project_virtual_sibling (self, node, sibling);
		}
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, sibling))
			return NULL;
		else {
			/* The node existed and the user wants the existing to 
			 * be replaced, so we delete that node (since the new
			 * one would have the old one's children otherwise). */
			parrillada_data_project_remove_real (self, sibling);
			node = parrillada_file_node_new_empty_folder (name);
		}
	}
	else
		node = parrillada_file_node_new_empty_folder (name);

	parrillada_file_node_add (parent, node, priv->sort_func);

	/* Add it (we must add a graft) */
	graft = g_hash_table_lookup (priv->grafts, NEW_FOLDER);
	if (!parrillada_data_project_add_node_real (self, node, graft, NEW_FOLDER))
		return NULL;

	return node;
}

static void
parrillada_data_project_update_uri (ParrilladaDataProject *self,
				 ParrilladaFileNode *node,
				 const gchar *uri)
{
	gchar *parent_uri;
	ParrilladaGraft *graft;
	ParrilladaURINode *uri_node;
	ParrilladaURINode *former_uri_node;

	graft = PARRILLADA_FILE_NODE_GRAFT (node);
	former_uri_node = graft->node;

	if (!strcmp (former_uri_node->uri, uri)) {
		/* Nothing needs update */
		return;
	}

	/* different URIS; make sure the node still needs a graft:
	 * - if so, update it
	 * - if not, remove it */
	parent_uri = parrillada_data_project_node_to_uri (self, node->parent);
	if (parent_uri) {
		guint parent_len;

		parent_len = strlen (parent_uri);

		if (strncmp (parent_uri, uri, parent_len)
		&&  uri [parent_len] != G_DIR_SEPARATOR) {
			/* The node hasn't been put under its rightful parent
			 * from the original file system. That means we must add
			 * a graft or update the current one. */
			uri_node = parrillada_data_project_uri_add_graft (self, uri);
			parrillada_file_node_graft (node, uri_node);
		}
		else {
			/* rightful parent: ungraft it */
			parrillada_file_node_ungraft (node);
		}

		g_free (parent_uri);
	}
	else {
		uri_node = parrillada_data_project_uri_add_graft (self, uri);
		parrillada_file_node_graft (node, uri_node);
	}

	/* the node was ungrafted, check if the former graft is still needed */
	parrillada_data_project_graft_is_needed (self, former_uri_node);
}

gboolean
parrillada_data_project_node_loaded (ParrilladaDataProject *self,
				  ParrilladaFileNode *node,
				  const gchar *uri,
				  GFileInfo *info)
{
	guint64 size;
	GFileType type;
	gboolean size_changed;
	ParrilladaFileTreeStats *stats;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	type = g_file_info_get_file_type (info);
	if (node->is_tmp_parent) {
		/* we must make sure that this is really a directory */
		if (type != G_FILE_TYPE_DIRECTORY) {
			/* exclude the URI we're replacing */
			parrillada_data_project_exclude_uri (self, uri);
			parrillada_data_project_convert_to_fake (self, node);
			return TRUE;
		}

		priv->loading --;
		g_signal_emit (self,
			       parrillada_data_project_signals [PROJECT_LOADED_SIGNAL],
			       0,
			       priv->loading);

		/* That's indeed a directory. It's going to be loaded. */
	}
	else if (priv->loading && node->is_grafted) {
		priv->loading --;
		if (type != G_FILE_TYPE_DIRECTORY) {
			/* no need to load its contents since it's not a folder */
			priv->loading --;
		}

		g_signal_emit (self,
			       parrillada_data_project_signals [PROJECT_LOADED_SIGNAL],
			       0,
			       priv->loading);
	}

	/* If the node is not grafted because it was put under its original 
	 * parent on the file system it comes from, then its parent URI can't
	 * have changed (the parent it was put under had already its URI cleaned
	 * of any symlink). Its URI may be different though if it's a symlink
	 * but that case is treated somewhere else. */
	if (node->is_grafted) {
		/* The URI of the node could be different from the one we gave
		 * earlier as parrillada-io looks for parent symlinks and replace
		 * them with their target. So since it's a graft, we need to 
		 * update the graft URI just to make sure. */
		parrillada_data_project_update_uri (self, node, uri);
	}

	size = g_file_info_get_size (info);
	if (type != G_FILE_TYPE_DIRECTORY) {
		if (PARRILLADA_BYTES_TO_SECTORS (size, 2048) > PARRILLADA_FILE_2G_LIMIT
		&&  PARRILLADA_FILE_NODE_SECTORS (node) < PARRILLADA_FILE_2G_LIMIT) {
			if (parrillada_data_project_file_signal (self, G2_FILE_SIGNAL, g_file_info_get_name (info))) {
				parrillada_data_project_remove_node (self, node);
				return FALSE;
			}
		}
	}

	/* avoid signalling twice for the same directory */
	if (!parrillada_data_project_is_deep (self, node->parent,  PARRILLADA_FILE_NODE_NAME (node), node->is_file)) {
		parrillada_data_project_remove_node (self, node);
		return FALSE;
	}

	size_changed = (PARRILLADA_BYTES_TO_SECTORS (size, 2048) != PARRILLADA_FILE_NODE_SECTORS (node));
	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	parrillada_file_node_set_from_info (node, stats, info);

	/* Check it that needs a graft: this node has not been moved so we don't
	 * need to check these cases yet it could turn out that it was a symlink
	 * then we need a graft. */
	if (g_file_info_get_is_symlink (info) && g_file_info_get_file_type (info) != G_FILE_TYPE_SYMBOLIC_LINK) {
		ParrilladaURINode *graft;
		gchar *uri;

		/* first we exclude the symlink, then we graft its target. */
		uri = parrillada_data_project_node_to_uri (self, node);
		parrillada_file_node_ungraft (node);
		parrillada_data_project_exclude_uri (self, uri);
		g_free (uri);

		/* NOTE: info has the uri for the target of the symlink.
		 * NOTE 2: all nodes with target URI become grafted. */
		graft = parrillada_data_project_uri_graft_nodes (self, g_file_info_get_symlink_target (info));
		parrillada_file_node_graft (node, graft);
	}

	/* at this point we know all we need to know about our node and in 
	 * particular if it's a file or a directory, if it's grafted or not
	 * That's why we can start monitoring it. */
	if (!node->is_monitored) {
#ifdef BUILD_INOTIFY
		if (node->is_grafted)
			parrillada_file_monitor_single_file (PARRILLADA_FILE_MONITOR (self),
							  uri,
							  node);

		if (!node->is_file)
			parrillada_file_monitor_directory_contents (PARRILLADA_FILE_MONITOR (self),
								 uri,
								 node);
		node->is_monitored = TRUE;
#endif
	}

	/* signal the changes */
	parrillada_data_project_node_changed (self, node);
	if (size_changed)
		g_signal_emit (self,
			       parrillada_data_project_signals [SIZE_CHANGED_SIGNAL],
			       0);

	return TRUE;
}

void
parrillada_data_project_node_reloaded (ParrilladaDataProject *self,
				    ParrilladaFileNode *node,
				    const gchar *uri,
				    GFileInfo *info)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileTreeStats *stats;
	gboolean size_changed;
	const gchar *name;
	guint64 size;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	node->is_reloading = FALSE;

	/* the only thing that can have changed here is size. Readability was 
	 * checked in data-vfs.c. That's why we're only interested in files
	 * since directories don't have size. */ 
	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
		return;

	size = g_file_info_get_size (info);
	name = g_file_info_get_name (info);
	if (PARRILLADA_BYTES_TO_SECTORS (size, 2048) > PARRILLADA_FILE_2G_LIMIT
	&&  PARRILLADA_FILE_NODE_SECTORS (node) < PARRILLADA_FILE_2G_LIMIT) {
		if (parrillada_data_project_file_signal (self, G2_FILE_SIGNAL, name)) {
			parrillada_data_project_remove_node (self, node);
			return;
		}
	}

	size_changed = (PARRILLADA_BYTES_TO_SECTORS (size, 2048) == PARRILLADA_FILE_NODE_SECTORS (node));
	if (PARRILLADA_FILE_NODE_MIME (node) && !size_changed)
		return;

	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	parrillada_file_node_set_from_info (node, stats, info);

	/* no need to check for graft since it wasn't renamed, it wasn't moved
	 * its type hasn't changed (and therefore it can't be a symlink. For 
	 * these reasons it stays as is (whether grafted or not). */

	/* it's probably already watched (through its parent). */

	parrillada_data_project_node_changed (self, node);
	if (size_changed)
		g_signal_emit (self,
			       parrillada_data_project_signals [SIZE_CHANGED_SIGNAL],
			       0);
}

static ParrilladaFileNode *
parrillada_data_project_add_loading_node_real (ParrilladaDataProject *self,
					    const gchar *uri,
					    const gchar *name_arg,
					    gboolean is_hidden,
					    ParrilladaFileNode *parent)
{
	gchar *name;
	ParrilladaFileNode *node;
	ParrilladaURINode *graft;
	ParrilladaFileNode *sibling;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	graft = g_hash_table_lookup (priv->grafts, uri);
	if (!parent)
		parent = priv->root;

	if (!name_arg) {
		/* NOTE: find the name of the node through the URI */
		name = parrillada_utils_get_uri_name (uri);
	}
	else
		name = g_strdup (name_arg);

	/* make sure that name doesn't exist */
	sibling = parrillada_file_node_check_name_existence (parent, name);
	if (sibling) {
		if (PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			node = parrillada_file_node_new_loading (name);
			parrillada_data_project_virtual_sibling (self, node, sibling);
		}
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, sibling)) {
			g_free (name);
			return NULL;
		}
		else {
			/* The node existed and the user wants the existing to 
			 * be replaced, so we delete that node (since the new
			 * one would have the old one's children otherwise). */
			parrillada_data_project_remove_real (self, sibling);
			node = parrillada_file_node_new_loading (name);
			graft = g_hash_table_lookup (priv->grafts, uri);
		}
	}
	else
		node = parrillada_file_node_new_loading (name);

	g_free (name);

	parrillada_file_node_add (parent, node, priv->sort_func);

	node->is_hidden = is_hidden;
	if (!parrillada_data_project_add_node_real (self, node, graft, uri))
		return NULL;

	return node;
}

ParrilladaFileNode *
parrillada_data_project_add_loading_node (ParrilladaDataProject *self,
				       const gchar *uri,
				       ParrilladaFileNode *parent)
{
	g_return_val_if_fail (PARRILLADA_IS_DATA_PROJECT (self), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	return parrillada_data_project_add_loading_node_real (self, uri, NULL, FALSE, parent);
}

ParrilladaFileNode *
parrillada_data_project_add_hidden_node (ParrilladaDataProject *self,
				      const gchar *uri,
				      const gchar *name,
				      ParrilladaFileNode *parent)
{
	g_return_val_if_fail (PARRILLADA_IS_DATA_PROJECT (self), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	return parrillada_data_project_add_loading_node_real (self, uri, name, TRUE, parent);
}

void
parrillada_data_project_directory_node_loaded (ParrilladaDataProject *self,
					    ParrilladaFileNode *parent)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (parent->is_exploring) {
		ParrilladaDataProjectClass *klass;

		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);

		parent->is_exploring = FALSE;
		/* This is to make sure the directory row is
		 * updated in case it is empty. Otherwise, it
		 * would carry on to be displayed as loading
		 * if it were empty.
		 * Don't use parrillada_data_project_node_changed
		 * as we don't reorder the rows. */
		if (klass->node_changed)
			klass->node_changed (self, parent);
	}

	/* Mostly useful at project load time. */
	if (priv->loading) {
		if (parent->is_grafted || parent->is_tmp_parent) {
			priv->loading --;
			g_signal_emit (self,
				       parrillada_data_project_signals [PROJECT_LOADED_SIGNAL],
				       0,
				       priv->loading);
		}
	}
}

/**
 * This function is only used by parrillada-data-vfs.c to add the contents of a 
 * directory. That's why if a node with the same name is already grafted we 
 * can't add it. It means that the node is probable excluded.
 * NOTE: all the files added through this function are not grafted since they
 * are added due to the exploration of their parent. If they collide with
 * anything it can only be with a grafed node.
 */

ParrilladaFileNode *
parrillada_data_project_add_node_from_info (ParrilladaDataProject *self,
					 const gchar *uri,
					 GFileInfo *info,
					 ParrilladaFileNode *parent)
{
	GFileType type;
	const gchar *name;
	ParrilladaFileNode *node;
	ParrilladaURINode *graft;
	ParrilladaFileNode *sibling;
	ParrilladaDataProjectPrivate *priv;

	g_return_val_if_fail (PARRILLADA_IS_DATA_PROJECT (self), NULL);
	g_return_val_if_fail (info != NULL, NULL);

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* Only useful at project load time. In this case if the URI has already
	 * a graft we ignore it since if it existed it has been added through a
	 * graft point and not through directory exploration. */
	graft = g_hash_table_lookup (priv->grafts, uri);
	if (priv->loading && graft) {
		GSList *iter;

		/* This is either :
		 * - a graft node loaded in the beginning (already loaded then)
		 * - an excluded node (and therefore no need to do anything)
		 * - a temporary parent and therefore we just ungraft it (no
		 *   need to set is_tmp_parent to FALSE since it'll be done when
		 *   it is loaded) */
		for (iter = graft->nodes; iter; iter = iter->next) {
			node = iter->data;

			if (parent == node->parent) {
				if (node->is_tmp_parent) {
					if (!parrillada_data_project_uri_is_graft_needed (self, graft->uri))
						parrillada_data_project_uri_remove_graft (self, graft->uri);
					return node;
				}
			}
		}

		return NULL;
	}

	if (!parent)
		parent = priv->root;

	name = g_file_info_get_name (info);

	/* Run a few checks */
	type = g_file_info_get_file_type (info);
	if (type != G_FILE_TYPE_DIRECTORY) {
		guint64 size;

		size = g_file_info_get_size (info);
		if (PARRILLADA_BYTES_TO_SECTORS (size, 2048) > PARRILLADA_FILE_2G_LIMIT)
			if (parrillada_data_project_file_signal (self, G2_FILE_SIGNAL, name)) {
				/* we need to exclude this uri */
				parrillada_data_project_exclude_uri (self, uri);
				return NULL;
			}
	}
	/* This is a special case where we won't try all checks for deep nested
	 * files. Since this function is only used by parrillada-data-vfs.c to 
	 * add the results of its exploration, we only check directories and
	 * just check for a directory to have a depth of 6 (means parent has a
	 * depth of 5. */
	else if (parrillada_file_node_get_depth (parent) == 5) {
		if (parrillada_data_project_file_signal (self, DEEP_DIRECTORY_SIGNAL, name)) {
			/* we need to exclude this uri */
			parrillada_data_project_exclude_uri (self, uri);
			return NULL;
		}
	} 

	/* make sure that name doesn't exist */
	sibling = parrillada_file_node_check_name_existence (parent, name);
	if (sibling) {
		ParrilladaFileTreeStats *stats;

		stats = parrillada_file_node_get_tree_stats (priv->root, NULL);

		if (PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			node = parrillada_file_node_new (g_file_info_get_name (info));
			parrillada_file_node_set_from_info (node, stats, info);
			parrillada_data_project_virtual_sibling (self, node, sibling);
		}
		else if (parrillada_data_project_node_signal (self, NAME_COLLISION_SIGNAL, sibling)) {
			/* we need to exclude this uri */
			parrillada_data_project_exclude_uri (self, uri);
			return NULL;
		}
		else {
			/* The node existed and the user wants the existing to 
			 * be replaced, so we delete that node (since the new
			 * one would have the old one's children otherwise). */
			node = parrillada_file_node_new (g_file_info_get_name (info));
			parrillada_file_node_set_from_info (node, stats, info);

			parrillada_data_project_remove_real (self, sibling);
			graft = g_hash_table_lookup (priv->grafts, uri);
		}
	}
	else {
		ParrilladaFileTreeStats *stats;

		node = parrillada_file_node_new (g_file_info_get_name (info));
		stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
		parrillada_file_node_set_from_info (node, stats, info);
	}

	parrillada_file_node_add (parent, node, priv->sort_func);

	if (g_file_info_get_is_symlink (info)
	&&  g_file_info_get_file_type (info) != G_FILE_TYPE_SYMBOLIC_LINK) {
		/* first we exclude the symlink, then we graft its target */
		parrillada_data_project_exclude_uri (self, uri);

		/* then we add the node */
		if (!parrillada_data_project_add_node_real (self,
		                                         node,
		                                         graft,
		                                         g_file_info_get_symlink_target (info)))
			return NULL;
	}
	else {
		if (!parrillada_data_project_add_node_real (self,
		                                         node,
		                                         graft,
		                                         uri))
			return NULL;
	}

	if (type != G_FILE_TYPE_DIRECTORY)
		g_signal_emit (self,
			       parrillada_data_project_signals [SIZE_CHANGED_SIGNAL],
			       0);

	/* at this point we know all we need to know about our node and in 
	 * particular if it's a file or a directory, if it's grafted or not
	 * That's why we can start monitoring it. */
	if (!node->is_monitored) {

#ifdef BUILD_INOTIFY

		if (node->is_grafted)
			parrillada_file_monitor_single_file (PARRILLADA_FILE_MONITOR (self),
							  uri,
							  node);

		if (!node->is_file)
			parrillada_file_monitor_directory_contents (PARRILLADA_FILE_MONITOR (self),
								 uri,
								 node);
		node->is_monitored = TRUE;

#endif

	}

	return node;
}

/**
 * Export tree internals into a track 
 */
struct _MakeTrackData {
	gboolean append_slash;
	gboolean hidden_nodes;

	GSList *grafts;
	GSList *excluded;

	ParrilladaDataProject *project;
};
typedef struct _MakeTrackData MakeTrackData;

gchar *
parrillada_data_project_node_to_path (ParrilladaDataProject *self,
				   ParrilladaFileNode *node)
{
	guint len;
	GSList *list;
	GSList *iter;
	gchar path [MAXPATHLEN] = {0, };

	if (!node || G_NODE_IS_ROOT (node))
		return g_strdup (G_DIR_SEPARATOR_S);

	/* walk the nodes up to the parent and add them to a list */
	list = NULL;
	while (node->parent) {
		list = g_slist_prepend (list, node);
		node = node->parent;
	}

	len = 0;
	for (iter = list; iter; iter = iter->next) {
		gchar *name;
		guint name_len;

		node = iter->data;

		*(path + len) = G_DIR_SEPARATOR;
		len ++;

		if (len > MAXPATHLEN)
			return NULL;

		/* Make sure path length didn't go over MAXPATHLEN. */
		name = PARRILLADA_FILE_NODE_NAME (node);

		name_len = strlen (name);
		if (len + name_len > MAXPATHLEN)
			return NULL;

		memcpy (path + len, name, name_len);
		len += name_len;
	}
	g_slist_free (list);

	return g_strdup (path);
}

static void
_foreach_grafts_make_list_cb (const gchar *uri,
			      ParrilladaURINode *uri_node,
			      MakeTrackData *data)
{
	GSList *iter;
	gboolean add_to_excluded = (uri_node->nodes == NULL);

	/* add each node */
	for (iter = uri_node->nodes; iter; iter = iter->next) {
		ParrilladaFileNode *node;
		ParrilladaGraftPt *graft;

		node = iter->data;
		if (!data->hidden_nodes && node->is_hidden)
			continue;

		add_to_excluded = TRUE;
		graft = g_new0 (ParrilladaGraftPt, 1);

		/* if URI is a created directory set URI to NULL */
		if (uri && uri != NEW_FOLDER)
			graft->uri = g_strdup (uri);

		graft->path = parrillada_data_project_node_to_path (data->project, node);
		if (!node->is_file && data->append_slash) {
			gchar *tmp;

			/* we need to know if that's a directory or not since if
			 * it is then mkisofs (but not genisoimage) requires the
			 * disc path to end with '/'; if there isn't '/' at the 
			 * end then only the directory contents are added. */
			tmp = graft->path;
			graft->path = g_strconcat (graft->path, "/", NULL);
			g_free (tmp);
		}

		data->grafts = g_slist_prepend (data->grafts, graft);
	}

	/* Each URI in this table must be excluded. Then each node in 
	 * this list will be grafted. That way only those that we are
	 * interested in will be in the tree. */

	/* Add to the unreadable. This could be further improved by 
	 * checking if there is a parent in the hash for this URI. If 
	 * not that's no use adding this URI to unreadable. */
	/* NOTE: if that the created directories URI, then there is no 
	 * need to add it to excluded */
	if (uri != NEW_FOLDER && add_to_excluded)
		data->excluded = g_slist_prepend (data->excluded, g_strdup (uri));
}

static void
_foreach_joliet_incompatible_make_list_cb (ParrilladaJolietKey *key,
					   GSList *nodes,
					   MakeTrackData *data)
{
	GSList *iter;

	/* now exclude all nodes and graft them with a joliet compatible name */
	for (iter = nodes; iter; iter = iter->next) {
		ParrilladaFileNode *node;
		ParrilladaGraftPt *graft;

		node = iter->data;

		/* skip grafted nodes (they were already processed). */
		if (node->is_grafted)
			continue;

		graft = g_new0 (ParrilladaGraftPt, 1);
		graft->path = parrillada_data_project_node_to_path (data->project, node);
		if (!node->is_file && data->append_slash) {
			gchar *tmp;

			/* we need to know if that's a directory or not since if
			 * it is then mkisofs (but not genisoimage) requires the
			 * disc path to end with '/'; if there isn't '/' at the 
			 * end then only the directory contents are added. */
			tmp = graft->path;
			graft->path = g_strconcat (graft->path, "/", NULL);
			g_free (tmp);
		}

		/* NOTE: here it's not possible to get a created folder here 
		 * since it would be grafted */
		graft->uri = parrillada_data_project_node_to_uri (data->project, node);
		data->grafts = g_slist_prepend (data->grafts, graft);

		data->excluded = g_slist_prepend (data->excluded, g_strdup (graft->uri));
	}
}

gboolean
parrillada_data_project_get_contents (ParrilladaDataProject *self,
				   GSList **grafts,
				   GSList **unreadable,
				   gboolean hidden_nodes,
				   gboolean joliet_compat,
				   gboolean append_slash)
{
	MakeTrackData callback_data;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (!g_hash_table_size (priv->grafts))
		return FALSE;

	callback_data.project = self;
	callback_data.grafts = NULL;
	callback_data.excluded = NULL;
	callback_data.hidden_nodes = hidden_nodes;
	callback_data.append_slash = append_slash;

	g_hash_table_foreach (priv->grafts,
			      (GHFunc) _foreach_grafts_make_list_cb,
			      &callback_data);

	/* This is possible even if the GHashTable is empty since there could be
	 * only excluded URI inside or hidden nodes like autorun.inf. */
	if (!callback_data.grafts) {
		g_slist_foreach (callback_data.excluded, (GFunc) g_free, NULL);
		g_slist_free (callback_data.excluded);
		return FALSE;
	}

	if (joliet_compat) {
		/* Make sure that all nodes with incompatible joliet names are
		 * added as graft points. */
		g_hash_table_foreach (priv->joliet,
				      (GHFunc) _foreach_joliet_incompatible_make_list_cb,
				      &callback_data);
	}

	if (!grafts) {
		g_slist_foreach (callback_data.grafts, (GFunc) parrillada_graft_point_free, NULL);
		g_slist_free (callback_data.grafts);
	}
	else
		*grafts = callback_data.grafts;

	if (!unreadable) {
		g_slist_foreach (callback_data.excluded, (GFunc) g_free, NULL);
		g_slist_free (callback_data.excluded);
	}
	else
		*unreadable = callback_data.excluded;

	return TRUE;
}

typedef struct _MakeTrackDataSpan MakeTrackDataSpan;
struct _MakeTrackDataSpan {
	GSList *grafts;
	GSList *joliet_grafts;

	guint64 files_num;
	guint64 dir_num;
	ParrilladaImageFS fs_type;
};

static void
parrillada_data_project_span_set_fs_type (MakeTrackDataSpan *data,
				       ParrilladaFileNode *node)
{
	if (node->is_symlink) {
		data->fs_type |= PARRILLADA_IMAGE_FS_SYMLINK;

		/* UDF won't be possible anymore with symlinks */
		if (data->fs_type & PARRILLADA_IMAGE_ISO_FS_LEVEL_3)
			data->fs_type &= ~(PARRILLADA_IMAGE_FS_UDF|
					   PARRILLADA_IMAGE_FS_JOLIET);
	}

	if (node->is_2GiB) {
		data->fs_type |= PARRILLADA_IMAGE_ISO_FS_LEVEL_3;
		if (!(data->fs_type & PARRILLADA_IMAGE_FS_SYMLINK))
			data->fs_type |= PARRILLADA_IMAGE_FS_UDF;
	}

	if (node->is_deep)
		data->fs_type |= PARRILLADA_IMAGE_ISO_FS_DEEP_DIRECTORY;
}

static void
parrillada_data_project_span_explore_folder_children (MakeTrackDataSpan *data,
						   ParrilladaFileNode *node)
{
	for (node = PARRILLADA_FILE_NODE_CHILDREN (node); node; node = node->next) {
		if (node->is_grafted)
			data->grafts = g_slist_prepend (data->grafts, node);

		if (node->is_file) {
			parrillada_data_project_span_set_fs_type (data, node);
			data->files_num ++;
		}
		else {
			parrillada_data_project_span_explore_folder_children (data, node);
			data->dir_num ++;
		}
	}
}

static void
parrillada_data_project_span_generate (ParrilladaDataProject *self,
				    MakeTrackDataSpan *data,
				    gboolean append_slash,
				    ParrilladaTrackData *track)
{
	GSList *iter;
	gpointer uri_data;
	GHashTableIter hiter;
	GSList *grafts = NULL;
	GSList *excluded = NULL;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	for (iter = data->grafts; iter; iter = iter->next) {
		ParrilladaFileNode *node;
		ParrilladaGraftPt *graft;

		node = iter->data;

		graft = g_new0 (ParrilladaGraftPt, 1);

		/* REMINDER for the developper who's forgetful:
		 * The real joliet compliant path names will be generated later
		 * either by the backends (like libisofs) or by the library (see
		 * burn-mkisofs-base.c). So no need to care about that. */
		graft->path = parrillada_data_project_node_to_path (self, node);
		if (!node->is_file && append_slash) {
			gchar *tmp;

			/* we need to know if that's a directory or not since if
			 * it is then mkisofs (but not genisoimage) requires the
			 * disc path to end with '/'; if there isn't '/' at the 
			 * end then only the directory contents are added. */
			tmp = graft->path;
			graft->path = g_strconcat (graft->path, "/", NULL);
			g_free (tmp);
		}
		graft->uri = parrillada_data_project_node_to_uri (self, node);
		grafts = g_slist_prepend (grafts, graft);
	}

	/* NOTE about excluded file list:
	 * don't try to check for every empty ParrilladaUriNode whether its URI is
	 * a child of one of the grafted node that would take too much time for
	 * almost no win; better add all of them (which includes the above graft
	 * list as well. */
	g_hash_table_iter_init (&hiter, priv->grafts);
	while (g_hash_table_iter_next (&hiter, &uri_data, NULL)) {
		if (uri_data != NEW_FOLDER)
			excluded = g_slist_prepend (excluded, g_strdup (uri_data));
	}

	if (data->fs_type & PARRILLADA_IMAGE_FS_JOLIET) {
		/* Add the joliet grafts */
		for (iter = data->joliet_grafts; iter; iter = iter->next) {
			ParrilladaFileNode *node;
			ParrilladaGraftPt *graft;

			node = iter->data;

			graft = g_new0 (ParrilladaGraftPt, 1);

			/* REMINDER for the developper who's forgetful:
			 * The real joliet compliant path names will be generated later
			 * either by the backends (like libisofs) or by the library (see
			 * burn-mkisofs-base.c). So no need to care about that. */
			graft->path = parrillada_data_project_node_to_path (self, node);
			if (!node->is_file && append_slash) {
				gchar *tmp;

				/* we need to know if that's a directory or not since if
				 * it is then mkisofs (but not genisoimage) requires the
				 * disc path to end with '/'; if there isn't '/' at the 
				 * end then only the directory contents are added. */
				tmp = graft->path;
				graft->path = g_strconcat (graft->path, "/", NULL);
				g_free (tmp);
			}

			grafts = g_slist_prepend (grafts, graft);

			if (graft->uri)
				excluded = g_slist_prepend (excluded, graft->uri);
		}
	}

	parrillada_track_data_set_source (track, grafts, excluded);
}

goffset
parrillada_data_project_improve_image_size_accuracy (goffset sectors,
						  guint64 dir_num,
						  ParrilladaImageFS fs_type)
{
	/* sector number should be increased in the following way to get
	 * a more accurate number:
	 * - the first (empty most of the time) 16 sectors
	 * - primary volume descriptor block
	 * - terminator volume descriptor block
	 * - one sector for root (and one more if there is joliet)
	 * - 4 sectors for the path table (at least!!)
	 * - for every directory add a block (for all entry records)
	 *   and another one if there is joliet on
	 */
	sectors += 23;
	sectors += dir_num * 1;
	
	if (fs_type & PARRILLADA_IMAGE_FS_JOLIET) {
		/* For joliet :
		 * - 1 sector for the volume descriptor
		 * - 1 sector for the root descriptor
		 * - 4 sectors for the path table (at least!!)
		 */
		sectors += 6;

		/* For joliet 2 sectors per directory (at least!!) */
		sectors += dir_num * 2;
	}

	/* Finally there is a 150 pad block at the end (only with mkisofs !!).
	 * That was probably done to avoid getting an image whose size would be
	 * less than 1 sec??? */
	sectors += 150;

	return sectors;
}

goffset
parrillada_data_project_get_max_space (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *children;
	goffset max_sectors = 0;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* When empty this is an error */
	if (!g_hash_table_size (priv->grafts))
		return 0;

	children = PARRILLADA_FILE_NODE_CHILDREN (priv->root);
	while (children) {
		goffset child_sectors;

		if (g_slist_find (priv->spanned, children)) {
			children = children->next;
			continue;
		}

		if (children->is_file)
			child_sectors = PARRILLADA_FILE_NODE_SECTORS (children);
		else
			child_sectors = parrillada_data_project_get_folder_sectors (self, children);

		max_sectors = MAX (max_sectors, PARRILLADA_FILE_NODE_SECTORS (children));

		children = children->next;
	}

	return max_sectors;
}

ParrilladaBurnResult
parrillada_data_project_span (ParrilladaDataProject *self,
			   goffset max_sectors,
			   gboolean append_slash,
			   gboolean joliet,
			   ParrilladaTrackData *track)
{
	MakeTrackDataSpan callback_data;
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *children;
	goffset total_sectors = 0;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* When empty this is an error */
	if (!g_hash_table_size (priv->grafts))
		return PARRILLADA_BURN_ERR;

	callback_data.dir_num = 0;
	callback_data.files_num = 0;
	callback_data.grafts = NULL;
	callback_data.joliet_grafts = NULL;
	callback_data.fs_type = PARRILLADA_IMAGE_FS_ISO;
	if (joliet)
		callback_data.fs_type |= PARRILLADA_IMAGE_FS_JOLIET;

	children = PARRILLADA_FILE_NODE_CHILDREN (priv->root);
	while (children) {
		goffset child_sectors;

		if (g_slist_find (priv->spanned, children)) {
			children = children->next;
			continue;
		}

		if (children->is_file)
			child_sectors = PARRILLADA_FILE_NODE_SECTORS (children);
		else
			child_sectors = parrillada_data_project_get_folder_sectors (self, children);

		/* if the top directory is too large, continue */
		if (child_sectors + total_sectors > max_sectors) {
			children = children->next;
			continue;
		}

		/* FIXME: we need a better algorithm here that would add first
		 * the biggest top folders/files and that would try to fill as
		 * much as possible the disc. */
		total_sectors += child_sectors;

		/* Take care of joliet non compliant nodes */
		if (callback_data.fs_type & PARRILLADA_IMAGE_FS_JOLIET) {
			GHashTableIter iter;
			gpointer value_data;
			gpointer key_data;

			/* Problem is we don't know whether there are symlinks */
			g_hash_table_iter_init (&iter, priv->joliet);
			while (g_hash_table_iter_next (&iter, &key_data, &value_data)) {
				GSList *nodes;
				ParrilladaJolietKey *key;

				/* Is the node a graft a child of a graft */
				key = key_data;
				if (key->parent == children || parrillada_file_node_is_ancestor (children, key->parent)) {
					/* Add all the children to the list of
					 * grafts provided they are not already
					 * grafted. */
					for (nodes = value_data; nodes; nodes = nodes->next) {
						ParrilladaFileNode *node;

						/* skip grafted nodes (they are
						 * already or will be processed)
						 */
						node = nodes->data;
						if (node->is_grafted)
							continue;
						
						callback_data.joliet_grafts = g_slist_prepend (callback_data.joliet_grafts, node);
					}

					break;
				}
			}
		}

		callback_data.grafts = g_slist_prepend (callback_data.grafts, children);
		if (children->is_file) {
			parrillada_data_project_span_set_fs_type (&callback_data, children);
			callback_data.files_num ++;
		}
		else {
			parrillada_data_project_span_explore_folder_children (&callback_data, children);
			callback_data.dir_num ++;
		}

		priv->spanned = g_slist_prepend (priv->spanned, children);
		children = children->next;
	}

	/* This means it's finished */
	if (!callback_data.grafts) {
		PARRILLADA_BURN_LOG ("No graft found for spanning");
		return PARRILLADA_BURN_OK;
	}

	parrillada_data_project_span_generate (self,
					    &callback_data,
					    append_slash,
					    track);

	total_sectors = parrillada_data_project_improve_image_size_accuracy (total_sectors,
									  callback_data.dir_num,
									  callback_data.fs_type);

	parrillada_track_data_set_data_blocks (track, total_sectors);
	parrillada_track_data_add_fs (track, callback_data.fs_type);
	parrillada_track_data_set_file_num (track, callback_data.files_num);

	PARRILLADA_BURN_LOG ("Set object (size %" G_GOFFSET_FORMAT ")", total_sectors);

	g_slist_free (callback_data.grafts);
	g_slist_free (callback_data.joliet_grafts);

	return PARRILLADA_BURN_RETRY;
}

ParrilladaBurnResult
parrillada_data_project_span_possible (ParrilladaDataProject *self,
				    goffset max_sectors)
{
	ParrilladaDataProjectPrivate *priv;
	gboolean has_data_left = FALSE;
	ParrilladaFileNode *children;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* When empty this is an error */
	if (!g_hash_table_size (priv->grafts))
		return PARRILLADA_BURN_ERR;

	children = PARRILLADA_FILE_NODE_CHILDREN (priv->root);
	while (children) {
		goffset child_sectors;

		if (g_slist_find (priv->spanned, children)) {
			children = children->next;
			continue;
		}

		if (children->is_file)
			child_sectors = PARRILLADA_FILE_NODE_SECTORS (children);
		else
			child_sectors = parrillada_data_project_get_folder_sectors (self, children);

		/* Find at least one file or directory that can be spanned */
		if (child_sectors < max_sectors)
			return PARRILLADA_BURN_RETRY;

		/* if the top directory is too large, continue */
		children = children->next;
		has_data_left = TRUE;
	}

	if (has_data_left)
		return PARRILLADA_BURN_ERR;

	return PARRILLADA_BURN_OK;
}

ParrilladaBurnResult
parrillada_data_project_span_again (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *children;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* When empty this is an error */
	if (!g_hash_table_size (priv->grafts))
		return PARRILLADA_BURN_ERR;

	children = PARRILLADA_FILE_NODE_CHILDREN (priv->root);
	while (children) {
		if (!g_slist_find (priv->spanned, children))
			return PARRILLADA_BURN_RETRY;

		children = children->next;
	}

	return PARRILLADA_BURN_OK;
}

void
parrillada_data_project_span_stop (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	g_slist_free (priv->spanned);
	priv->spanned = NULL;
}

gboolean
parrillada_data_project_has_symlinks (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileTreeStats *stats;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	if (stats->num_sym)
		return TRUE;

	return FALSE;
}

gboolean
parrillada_data_project_is_joliet_compliant (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileTreeStats *stats;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
	if (stats->num_sym)
		return FALSE;

	if (!priv->joliet || !g_hash_table_size (priv->joliet))
		return TRUE;

	return FALSE;
}

gboolean
parrillada_data_project_is_video_project (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	gboolean has_video, has_audio;
	ParrilladaFileNode *iter;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* here we check that the selection can be burnt as a video DVD.
	 * It must have :
	 * - a VIDEO_TS and AUDIO_TS at its root
	 * - the VIDEO_TS directory must have VIDEO_TS.IFO, VIDEO_TS.VOB
	     and VIDEO_TS.BUP inside */

	has_audio = has_video = FALSE;

	iter = PARRILLADA_FILE_NODE_CHILDREN (priv->root);
	for (; iter; iter = iter->next) {
		gchar *name;

		name = PARRILLADA_FILE_NODE_NAME (iter);
		if (!name)
			continue;

		if (!strcmp (name, "VIDEO_TS")) {
			ParrilladaFileNode *child;
			gboolean has_ifo, has_bup;

			has_ifo = has_bup = FALSE;
			child = PARRILLADA_FILE_NODE_CHILDREN (iter);

			for (; child; child = child->next) {
				name = PARRILLADA_FILE_NODE_NAME (child);
				if (!name)
					continue;

				if (!strcmp (name, "VIDEO_TS.IFO"))
					has_ifo = TRUE;
				else if (!strcmp (name, "VIDEO_TS.BUP"))
					has_bup = TRUE;
			}

			if (!has_ifo || !has_bup)
				return FALSE;

			has_video = TRUE;
		}
		else if (!strcmp (name, "AUDIO_TS"))
			has_audio = TRUE;
	}

	if (!has_video || !has_audio)
		return FALSE;

	return TRUE;
}

gboolean
parrillada_data_project_is_empty (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	if (g_hash_table_size (priv->grafts))
		return FALSE;

	return TRUE;
}

/**
 * The following functions are mostly useful to load projects
 */

static ParrilladaFileNode *
parrillada_data_project_create_path (ParrilladaDataProject *self,
				  ParrilladaFileNode *parent,
				  const gchar **buffer,
				  GSList **folders)
{
	gchar *end;
	const gchar *path;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	path = *buffer;
	if (path [0] == G_DIR_SEPARATOR)
		path ++;

	end = g_utf8_strchr (path, -1, G_DIR_SEPARATOR);

	while (end && end [1] != '\0') {
		ParrilladaFileNode *node;
		gchar *name;
		gint len;

		/* create the path */
		len = end - path;
		name = g_strndup (path, len);

		node = parrillada_file_node_new_loading (name);
		parrillada_file_node_add (parent, node, priv->sort_func);
		parent = node;
		g_free (name);

		/* check joliet compatibility; do it after node was created. */
		if (strlen (PARRILLADA_FILE_NODE_NAME (parent)) > 64)
			parrillada_data_project_joliet_add_node (self, parent);

		/* Set this directory to be set as a folder if it turns out it 
		 * isn't one. */
		parent->is_tmp_parent = TRUE;

		(*folders) = g_slist_prepend ((*folders), parent);

		/* Go on with the next; skip the separator */
		path += len;
		if (path [0] == G_DIR_SEPARATOR)
			path ++;

		end = g_utf8_strchr (path, -1, G_DIR_SEPARATOR);
	}

	*buffer = path;
	return parent;
}

static ParrilladaFileNode *
parrillada_data_project_skip_existing (ParrilladaDataProject *self,
				    ParrilladaFileNode *parent,
				    const gchar **buffer)
{
	gchar *end;
	const gchar *path;

	path = *buffer;

	if (path [0] == G_DIR_SEPARATOR)
		path ++;

	end = g_utf8_strchr (path, -1, G_DIR_SEPARATOR);

	/* first look for the existing nodes */
	while (end && end [1] != '\0') {
		ParrilladaFileNode *node;
		gboolean found;
		guint len;

		len = end - path;

		/* go through the children nodes and find the name */
		found = FALSE;
		for (node = PARRILLADA_FILE_NODE_CHILDREN (parent); node; node = node->next) {
			if (node
			&& !strncmp (PARRILLADA_FILE_NODE_NAME (node), path, len)
			&& (PARRILLADA_FILE_NODE_NAME (node) [len] == G_DIR_SEPARATOR
			||  PARRILLADA_FILE_NODE_NAME (node) [len] == '\0')) {
				parent = node;
				found = TRUE;
				break;
			}	
		}

		if (!found)
			break;

		/* skip the separator */
		path += len;
		if (path [0] == G_DIR_SEPARATOR)
			path ++;

		end = g_utf8_strchr (path, -1, G_DIR_SEPARATOR);
	}

	*buffer = path;
	return parent;
}

static GSList *
parrillada_data_project_add_path (ParrilladaDataProject *self,
			       const gchar *path,
			       const gchar *uri,
			       GSList *folders)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	ParrilladaFileNode *parent;
	ParrilladaFileNode *node;
	ParrilladaURINode *graft;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);

	/* we don't create the last part (after the last separator) of
	 * the node since we're only interested in the existence of the
	 * parent. */

	/* find the last existing node in the path */
	parent = parrillada_data_project_skip_existing (self, priv->root, &path);

	/* create the missing parents if needed */
	parent = parrillada_data_project_create_path (self,
						   parent,
						   &path,
						   &folders);

	/* Now that we ensured that the parent path exists add the final node */

	/* we're sure that this node needs grafting and there is already
	 * probably a graft in the hash (but no node yet). */
	if (!uri)
		uri = NEW_FOLDER;

	graft = parrillada_data_project_uri_ensure_graft (self, uri);
	node = parrillada_file_node_check_name_existence (parent, path);
	if (node && node->is_tmp_parent) {
		/* This node doesn't need renaming since it was found by name.
		 * No need for joliet check either (done at creation time). 
		 * There is already a graft since it is a fake so remove it from
		 * previous graft and add the new one (it needs to be grafted). */
		node->is_tmp_parent = FALSE;
		parrillada_file_node_graft (node, graft);
		folders = g_slist_remove (folders, node);

		/* NOTE: we can use node_added here since no temporary directory
		 * was explicitely added yet. */
		if (uri == NEW_FOLDER) {
			node->is_fake = TRUE;
			node->is_file = FALSE;
			node->is_loading = FALSE;
			node->is_reloading = FALSE;

			/* Don't signal the node addition yet we'll do it later
			 * when all the nodes are created */
		}
		else {
			node->is_file = FALSE;
			node->is_fake = FALSE;
			node->is_loading = TRUE;
			node->is_reloading = FALSE;

			/* Don't signal the node addition yet we'll do it later
			 * when all the nodes are created */
		}
	}
	else if (node) {
		g_warning ("Already existing node");
		/* error: the path exists twice. That shouldn't happen */
		return folders;
	}
	else {
		/* don't use parrillada_data_project_add_loading_node since that way:
		 * - we don't check for sibling
		 * - we set right from the start the right name */
		if (uri != NEW_FOLDER)
			node = parrillada_file_node_new_loading (path);
		else
			node = parrillada_file_node_new_empty_folder (path);

		parrillada_file_node_add (parent, node, priv->sort_func);

		/* the following function checks for joliet, graft it */
		parrillada_data_project_add_node_real (self,
		                                    node,
		                                    graft,
		                                    uri);
			
	}

	return folders;
}

static GSList *
parrillada_data_project_add_excluded_uri (ParrilladaDataProject *self,
				       const gchar *uri,
				       GSList *folders)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *graft = NULL;
	ParrilladaURINode *uri_graft;
	gchar *unescaped_uri;
	gint parent_uri_len;
	gchar *parent_uri;
	GSList *parents;
	GSList *iter;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* First exclude this URI, that is make sure a graft was created if need
	 * be without node.
	 * NOTE: grafted nodes could already have been added for this URI. Get 
	 * them. */
	parrillada_data_project_exclude_uri (self, uri);
	uri_graft = g_hash_table_lookup (priv->grafts, uri);

	/* Then get all the nodes from the first grafted parent URI.
	 * NOTE: here we don't check the graft uri itself since there is a graft
	 * but there are probably no node. */
	parent_uri = g_path_get_dirname (uri);
	while (strcmp (parent_uri, G_DIR_SEPARATOR_S) && strchr (parent_uri, G_DIR_SEPARATOR)) {
		/* keep going up until we reach root URI in grafts */
		graft = g_hash_table_lookup (priv->grafts, parent_uri);
		if (graft)
			break;

		graft = NULL;
		parent_uri = dirname (parent_uri);
	}
	g_free (parent_uri);

	if (!graft)
		return folders;

	/* Remove from the list the parent nodes which have a grafted child node
	 * with this URI */
	parents = g_slist_copy (graft->nodes);
	for (iter = uri_graft->nodes; iter; iter = iter->next) {
		GSList *next;
		GSList *nodes;
		ParrilladaFileNode *node;

		node = iter->data;
		for (nodes = parents; nodes; nodes = next) {
			ParrilladaFileNode *parent;

			parent = nodes->data;
			next = nodes->next;
			if (parrillada_file_node_is_ancestor (parent, node))
				parents = g_slist_remove (parents, parent);
		}
	}

	if (!parents)
		return folders;

	/* Create the paths starting from these nodes */
	unescaped_uri = g_uri_unescape_string (uri, NULL);

	parent_uri = g_uri_unescape_string (graft->uri, NULL);
	parent_uri_len = strlen (parent_uri);
	g_free (parent_uri);

	for (iter = parents; iter; iter = iter->next) {
		ParrilladaFileNode *parent;
		const gchar *path;

		parent = iter->data;
		path = unescaped_uri + parent_uri_len;

		/* skip the already existing ones */
		parent = parrillada_data_project_skip_existing (self, parent, &path);

		/* check parent needs to be created */
		if (path [0] != G_DIR_SEPARATOR)
			continue;

		/* First create the path */
		parrillada_data_project_create_path (self,
						  parent,
						  &path,
						  &folders);
	}
	g_slist_free (parents);
	g_free (unescaped_uri);

	return folders;
}

static gint
parrillada_data_project_load_contents_notify_directory (ParrilladaDataProject *self,
						     ParrilladaFileNode *parent,
						     ParrilladaDataNodeAddedFunc func)
{
	ParrilladaFileNode *child;
	gint num = 0;

	child = PARRILLADA_FILE_NODE_CHILDREN (parent);
	while (child) {
		gchar *uri;
		gboolean res;
		ParrilladaFileNode *next;

		/* The child could be removed during the process */
		next = child->next;

		/**
		 * This is to get the number of operations remaining before the
		 * whole project is loaded.
		 * +1 for loading information about a file or a directory (that
		 * means they must not be fake).
		 * +1 for loading the directory contents.
		 */
		if (child->is_fake) {
			/* This is a fake directory, there is no operation */
			res = func (self, child, NULL);
		}
		else {
			uri = parrillada_data_project_node_to_uri (self, child);
			res = func (self, child, uri);
			g_free (uri);

			if (res)
				num ++;
		}

		/* for whatever reason the node could have been invalidated */
		if (res && !child->is_file) {
			if (!child->is_fake)
				num ++;

			num += parrillada_data_project_load_contents_notify_directory (self,
										    child,
										    func);
		}

		child = next;
	}

	return num;
}

static gint
parrillada_data_project_load_contents_notify (ParrilladaDataProject *self)
{
	gint num;
	ParrilladaDataProjectClass *klass;
	ParrilladaDataProjectPrivate *priv;

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (!klass->node_added)
		return 0;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* we'll notify for every single node in the tree starting from the top.
	 * NOTE: at this point there are only grafted nodes (fake or not) in the
	 * tree. */
	num = parrillada_data_project_load_contents_notify_directory (self,
								   priv->root,
								   klass->node_added);
	return num;
}

guint
parrillada_data_project_load_contents (ParrilladaDataProject *self,
				    GSList *grafts,
				    GSList *excluded)
{
	GSList *iter;
	GSList *folders = NULL;
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	priv->is_loading_contents = 1;

	for (iter = grafts; iter; iter = iter->next) {
		ParrilladaGraftPt *graft;
		GFile *file;
		gchar *path;
		gchar *uri;

		graft = iter->data;

		if (graft->uri) {
			file = g_file_new_for_uri (graft->uri);
			uri = g_file_get_uri (file);
			g_object_unref (file);
		}
		else
			uri = NULL;

		if (graft->path) {
			/* This might happen if we are loading parrillada projects */
			if (g_str_has_suffix (graft->path, G_DIR_SEPARATOR_S)) {
				int len;

				len = strlen (graft->path);
				path = g_strndup (graft->path, len - 1);
			}
			else
				path = g_strdup (graft->path);
		}
		else
			path = NULL;

		folders = parrillada_data_project_add_path (self,
							 path,
							 uri,
							 folders);
		g_free (path);
		g_free (uri);
	}

	for (iter = excluded; iter; iter = iter->next) {
		gchar *uri;
		GFile *file;

		file = g_file_new_for_uri (iter->data);
		uri = g_file_get_uri (file);
		g_object_unref (file);

		folders = parrillada_data_project_add_excluded_uri (self,
								 uri,
								 folders);
		g_free (uri);
	}

	/* Now load the temporary folders that were created */
	for (iter = folders; iter; iter = iter->next) {
		ParrilladaURINode *graft;
		ParrilladaFileNode *tmp;
		gchar *uri;

		tmp = iter->data;

		/* get the URI for this node. There should be one now that all
		 * graft nodes are in the tree. */
		uri = parrillada_data_project_node_to_uri (self, tmp);
		if (!uri) {
			/* This node has been grafted under a node that was
			 * imported or was itself an imported node. Since there
			 * is no imported nodes any more, then it has to become
			 * fake.
			 * NOTE: it has to be a directory */
			tmp->is_fake = TRUE;
			tmp->is_loading = FALSE;
			tmp->is_reloading = FALSE;

			graft = parrillada_data_project_uri_ensure_graft (self, NEW_FOLDER);
			parrillada_file_node_graft (tmp, graft);

			/* Don't signal the node addition yet we'll do it later
			 * when all the nodes are created */

			continue;
		}

		/* graft it ? */
		graft = parrillada_data_project_uri_ensure_graft (self, uri);
		parrillada_file_node_graft (tmp, graft);
		g_free (uri);

		/* Don't signal the node addition yet we'll do it later when 
		 * all the nodes are created */
	}
	g_slist_free (folders);

	priv->loading = parrillada_data_project_load_contents_notify (self);

	priv->is_loading_contents = 0;
	return priv->loading;
}

/**
 * get the size of the whole tree in sectors 
 */
static void
parrillada_data_project_sum_graft_size_cb (gpointer key,
					ParrilladaURINode *graft,
					guint *sum_value)
{
	ParrilladaFileNode *node;

	if (!graft->nodes)
		return;

	node = graft->nodes->data;
	*sum_value += PARRILLADA_FILE_NODE_SECTORS (node);
}

goffset
parrillada_data_project_get_sectors (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	guint retval = 0;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* make the sum of all graft sizes provided they have nodes */
	g_hash_table_foreach (priv->grafts,
			      (GHFunc) parrillada_data_project_sum_graft_size_cb,
			      &retval);
	return retval;
}

struct _ParrilladaFileSize {
	goffset sum;
	ParrilladaFileNode *node;
};
typedef struct _ParrilladaFileSize ParrilladaFileSize;

static void
parrillada_data_project_folder_size_cb (const gchar *uri,
				     ParrilladaURINode *graft,
				     ParrilladaFileSize *size)
{
	GSList *iter;

	for (iter = graft->nodes; iter; iter = iter->next) {
		ParrilladaFileNode *node;

		node = iter->data;
		if (node == size->node)
			continue;

		if (parrillada_file_node_is_ancestor (size->node, node)) {
			size->sum += PARRILLADA_FILE_NODE_SECTORS (node);
			return;
		}
	}
}

goffset
parrillada_data_project_get_folder_sectors (ParrilladaDataProject *self,
					 ParrilladaFileNode *node)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileSize size;

	if (node->is_file)
		return 0;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	size.node = node;
	size.sum = PARRILLADA_FILE_NODE_SECTORS (node);

	g_hash_table_foreach (priv->grafts,
			      (GHFunc) parrillada_data_project_folder_size_cb,
			      &size);
	return size.sum;
}

static void
parrillada_data_project_init (ParrilladaDataProject *object)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (object);

	/* create the root */
	priv->root = parrillada_file_node_root_new ();

	priv->sort_func = parrillada_file_node_sort_default_cb;
	priv->ref_count = 1;

	/* create the necessary hash tables */
	priv->grafts = g_hash_table_new (g_str_hash,
					 g_str_equal);
	priv->joliet = g_hash_table_new (parrillada_data_project_joliet_hash,
					 parrillada_data_project_joliet_equal);
	priv->reference = g_hash_table_new (g_direct_hash,
					    g_direct_equal);
}

ParrilladaFileNode *
parrillada_data_project_get_root (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);
	return priv->root;
}

/**
 * This is to watch a still empty path and get a warning through the collision
 * name signal when the node is created. If a node is already created for this
 * path, then returns NULL.
 */

ParrilladaFileNode *
parrillada_data_project_watch_path (ParrilladaDataProject *project,
				 const gchar *path)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *parent;
	gchar **array;
	gchar **iter;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (project);
	parent = parrillada_data_project_skip_existing (project, priv->root, &path);

	if (!path || path [0] == '\0')
		return NULL;

	/* Now add the virtual node */
	if (g_str_has_prefix (path, G_DIR_SEPARATOR_S))
		array = g_strsplit (path + 1, G_DIR_SEPARATOR_S, 0);
	else
		array = g_strsplit (path, G_DIR_SEPARATOR_S, 0);

	for (iter = array; iter && *iter && parent; iter ++) {
		ParrilladaFileNode *node;

		node = parrillada_file_node_new_virtual (*iter);
		parrillada_file_node_add (parent, node, NULL);
		parent = node;
	}

	g_strfreev (array);

	/* This function shouldn't fail anyway */
	return parent;
}

static gboolean
parrillada_data_project_clear_grafts_cb (gchar *key,
				      ParrilladaURINode *graft,
				      gpointer NULL_data)
{
	GSList *iter;
	GSList *next;

	/* NOTE: no need to clear the key since it's the same as graft->uri. */
	for (iter = graft->nodes; iter; iter = next) {
		ParrilladaFileNode *node;

		node = iter->data;
		next = iter->next;
		parrillada_file_node_ungraft (node);
	}

	if (graft->uri != NEW_FOLDER)
		parrillada_utils_unregister_string (graft->uri);

	g_free (graft);
	return TRUE;
}

static gboolean
parrillada_data_project_clear_joliet_cb (ParrilladaJolietKey *key,
				      GSList *nodes,
				      gpointer NULL_data)
{
	g_free (key);
	g_slist_free (nodes);
	return TRUE;
}

static void
parrillada_data_project_clear (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	if (priv->spanned) {
		g_slist_free (priv->spanned);
		priv->spanned = NULL;
	}

	/* clear the tables.
	 * NOTE: reference hash doesn't need to be cleared. */
	g_hash_table_foreach_remove (priv->grafts,
				     (GHRFunc) parrillada_data_project_clear_grafts_cb,
				     NULL);

	g_hash_table_foreach_remove (priv->joliet,
				     (GHRFunc) parrillada_data_project_clear_joliet_cb,
				     NULL);

	g_hash_table_destroy (priv->reference);
	priv->reference = g_hash_table_new (g_direct_hash, g_direct_equal);

	/* no need to give a stats since we're destroying it */
	parrillada_file_node_destroy (priv->root, NULL);
	priv->root = NULL;

#ifdef BUILD_INOTIFY

	parrillada_file_monitor_reset (PARRILLADA_FILE_MONITOR (self));

#endif
}

void
parrillada_data_project_reset (ParrilladaDataProject *self)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	guint num_nodes;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

	/* Do it now */
	num_nodes = parrillada_file_node_get_n_children (priv->root);
	parrillada_data_project_clear (self);

	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
	if (klass->reset)
		klass->reset (self, num_nodes);

	priv->loading = 0;
	priv->root = parrillada_file_node_root_new ();
}

static void
parrillada_data_project_finalize (GObject *object)
{
	ParrilladaDataProjectPrivate *priv;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (object);
	parrillada_data_project_clear (PARRILLADA_DATA_PROJECT (object));

	if (priv->grafts) {
		g_hash_table_destroy (priv->grafts);
		priv->grafts = NULL;
	}

	if (priv->joliet) {
		g_hash_table_destroy (priv->joliet);
		priv->joliet = NULL;
	}

	if (priv->reference) {
		g_hash_table_destroy (priv->reference);
		priv->reference = NULL;
	}

	G_OBJECT_CLASS (parrillada_data_project_parent_class)->finalize (object);
}

/**
 * Callbacks for inotify backend
 */

#ifdef BUILD_INOTIFY

static void
parrillada_data_project_file_added (ParrilladaFileMonitor *monitor,
				 gpointer callback_data,
				 const gchar *name)
{
	ParrilladaFileNode *sibling;
	ParrilladaFileNode *parent;
	gchar *escaped_name;
	gchar *parent_uri;
	gchar *uri;

	/* Here the name can't be NULL since it means that the event
	 * happened on the callback_data */
	if (!name)
		return;

	/* NOTE: don't use reference as data-project is expected to 
	 * stop monitoring those nodes when they are removed. */
	if (!callback_data)
		return;

	parent = callback_data;

	/* check the node doesn't exist already (in this case it was grafted) */
	sibling = parrillada_file_node_check_name_existence (parent, name);

	/* get the new URI */
	parent_uri = parrillada_data_project_node_to_uri (PARRILLADA_DATA_PROJECT (monitor), parent);
	escaped_name = g_uri_escape_string (name,
					    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
					    FALSE);
	uri = g_strconcat (parent_uri, G_DIR_SEPARATOR_S, escaped_name, NULL);
	g_free (escaped_name);
	g_free (parent_uri);

	if (!sibling || PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
		/* If there is a virtual node, get rid of it */
		parrillada_data_project_add_loading_node (PARRILLADA_DATA_PROJECT (monitor),
						       uri,
						       parent);
	}
	else {
		/* There is no way we can add the node to tree; so exclude it */
		parrillada_data_project_exclude_uri (PARRILLADA_DATA_PROJECT (monitor), uri);
	}

	g_free (uri);
}

static void
parrillada_data_project_file_update_URI (ParrilladaDataProject *self,
				      ParrilladaFileNode *node,
				      const gchar *parent_uri,
				      const gchar *name)
{
	ParrilladaURINode *uri_node;
	ParrilladaGraft *graft;
	gchar *escaped_name;
	gchar *uri;

	if (!node->is_grafted)
		return;

	/* If the node was grafted then update its URI in the hash */

	/* change the graft now so we can check afterwards if a graft is still
	 * needed (the name could have been changed back to the original one). */
	graft = PARRILLADA_FILE_NODE_GRAFT (node);
	uri_node = graft->node;

	/* get the new uri */
	escaped_name = g_uri_escape_string (name,
					    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
					    FALSE);
	uri = g_build_path (G_DIR_SEPARATOR_S, parent_uri, escaped_name, NULL);
	g_free (escaped_name);

	/* ungraft it */
	parrillada_file_node_ungraft (node);

	/* regraft it with the new correct URI */
	uri_node = parrillada_data_project_uri_ensure_graft (self, uri);
	parrillada_file_node_graft (node, uri_node);
	g_free (uri);

	/* make sure we still need it in case it was moved to the right place */
	if (!parrillada_data_project_uri_is_graft_needed (self, uri_node->uri))
		parrillada_data_project_uri_remove_graft (self, uri_node->uri);
}

static void
parrillada_data_project_file_update_name (ParrilladaDataProject *self,
				       ParrilladaFileNode *node,
				       const gchar *new_name)
{
	ParrilladaFileNode *sibling;

	/* see if the old name was correct or if it had been changed.
	 * If it has been changed it'll be at least grafted but grafted
	 * doesn't mean that the name is not the original one since it
	 * could have been moved. 
	 * We don't want to rename files that were renamed. */

	/* If node was in the joliet incompatible table, remove it */
	parrillada_data_project_joliet_remove_node (self, node);

	/* see if this node didn't replace an imported one. If so the old 
	 * imported node must re-appear in the tree. */
	sibling = parrillada_file_node_check_imported_sibling (node);

	/* the name had not been changed so update it */
	parrillada_file_node_rename (node, new_name);

	/* Check joliet name compatibility. This must be done after the
	 * node information have been setup. */
	if (strlen (new_name) > 64)
		parrillada_data_project_joliet_add_node (self, node);

	parrillada_data_project_node_changed (self, node);

	if (sibling) {
		ParrilladaDataProjectClass *klass;
		ParrilladaDataProjectPrivate *priv;

		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (self);
		priv = PARRILLADA_DATA_PROJECT_PRIVATE (self);

		/* restore the imported node that has the same name as old_name */
		parrillada_file_node_add (sibling->parent, sibling, priv->sort_func);
		if (klass->node_added)
			parrillada_data_project_add_node_and_children (self, sibling, klass->node_added);
	}
}

static void
parrillada_data_project_file_graft (ParrilladaDataProject *self,
				 ParrilladaFileNode *node,
				 const gchar *real_name)
{
	ParrilladaURINode *uri_node;
	gchar *escaped_name;
	gchar *parent;
	gchar *uri;

	/* get the (new) URI */
	uri = parrillada_data_project_node_to_uri (self, node);
	parent = g_path_get_dirname (uri);
	g_free (uri);

	escaped_name = g_uri_escape_string (real_name,
					    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
					    FALSE);
	uri = g_strconcat (parent, G_DIR_SEPARATOR_S, escaped_name, NULL);
	g_free (escaped_name);
	g_free (parent);

	/* create new node and graft */
	uri_node = parrillada_data_project_uri_ensure_graft (self, uri);
	parrillada_file_node_graft (node, uri_node);
	g_free (uri);
}

static void
parrillada_data_project_file_renamed (ParrilladaFileMonitor *monitor,
				   ParrilladaFileMonitorType type,
				   gpointer callback_data,
				   const gchar *old_name,
				   const gchar *new_name)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaFileNode *sibling;
	ParrilladaFileNode *node;

	/* If old_name is NULL then it means the event is against callback.
	 * Otherwise that's against one of the children of callback. */
	if (type == PARRILLADA_FILE_MONITOR_FOLDER)
		node = parrillada_file_node_check_name_existence (callback_data, old_name);
	else
		node = callback_data;

	if (!node)
		return;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (monitor);

	/* make sure there isn't the same name in the directory: if so, that's 
	 * simply not possible to rename. So if node is grafted it keeps its
	 * name if not, it's grafted with the old name. */
	sibling = parrillada_file_node_check_name_existence (node->parent, new_name);
	if (sibling && !PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
		if (!node->is_grafted) {
			parrillada_data_project_file_graft (PARRILLADA_DATA_PROJECT (monitor), node, new_name);
			return;
		}

		/* if that's a grafted, just keep its name (but update URI). */
	}
	else if (!node->is_grafted || !strcmp (old_name, PARRILLADA_FILE_NODE_NAME (node))) {
		/* see if the old name was correct or if it had been changed.
		 * If it has been changed it'll be at least grafted but grafted
		 * doesn't mean that the name is not the original one since it
		 * could have been moved. 
		 * We don't want to rename files that were renamed. */
		parrillada_data_project_file_update_name (PARRILLADA_DATA_PROJECT (monitor), node, new_name);
	}

	if (sibling && PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
		/* Signal collision and remove virtual node but ignore result */
		parrillada_data_project_virtual_sibling (PARRILLADA_DATA_PROJECT (monitor), node, sibling);
	}

	if (node->is_grafted) {
		ParrilladaURINode *uri_node;
		ParrilladaGraft *graft;
		gchar *parent;

		graft = PARRILLADA_FILE_NODE_GRAFT (node);
		uri_node = graft->node;

		/* If the node was grafted then update its URI in the hash */
		parent = g_path_get_dirname (uri_node->uri);
		parrillada_data_project_file_update_URI (PARRILLADA_DATA_PROJECT (monitor),
						      node,
						      parent,
						      new_name);
		g_free (parent);
	}
}

static void
parrillada_data_project_file_moved (ParrilladaFileMonitor *monitor,
				 ParrilladaFileMonitorType type,
				 gpointer callback_src,
				 const gchar *name_src,
				 gpointer callback_dest,
				 const gchar *name_dest)
{
	ParrilladaFileNode *node, *parent;

	if (type == PARRILLADA_FILE_MONITOR_FOLDER)
		node = parrillada_file_node_check_name_existence (callback_src, name_src);
	else
		node = callback_src;

	if (!node)
		return;

	/* callback_dest has to be the new parent from the tree. If 
	 * that node has been moved to a fake node directory then it
	 * won't be returned; besides we wouldn't know where to put it
	 * in the tree even if it were returned. */
	parent = callback_dest;

	if (node->is_grafted) {
		gchar *parent_uri;

		/* simply update its URI in the hash. It was moved here on 
		 * purpose by the user so we don't move it. Remove graft is it
		 * isn't needed.
		 * Also, update the node name if it wasn't changed. */
		if (!strcmp (name_src, PARRILLADA_FILE_NODE_NAME (node))) {
			/* we also need to rename it since the user didn't
			 * change its name. Make sure though that in the new
			 * parent contents a file doesn't exist witht the same
			 * name. If so, then keep the old name. */
			if (!parrillada_file_node_check_name_existence (parent, name_dest))
				parrillada_data_project_file_update_name (PARRILLADA_DATA_PROJECT (monitor), node, name_dest);
		}

		/* update graft URI */
		parent_uri = parrillada_data_project_node_to_uri (PARRILLADA_DATA_PROJECT (monitor), parent);
		parrillada_data_project_file_update_URI (PARRILLADA_DATA_PROJECT (monitor),
						      node,
						      parent_uri,
						      name_dest);
		g_free (parent_uri);
	}
	else {
		guint former_position;
		ParrilladaFileNode *sibling;
		ParrilladaFileTreeStats *stats;
		ParrilladaFileNode *former_parent;
		ParrilladaDataProjectClass *klass;
		ParrilladaDataProjectPrivate *priv;

		klass = PARRILLADA_DATA_PROJECT_GET_CLASS (monitor);
		priv = PARRILLADA_DATA_PROJECT_PRIVATE (monitor);

		/* make sure there isn't the same name in the directory: if so,
		 * that's simply not possible to rename. So if node is grafted
		 * it keeps its name; if not, it's grafted with the old name. */
		sibling = parrillada_file_node_check_name_existence (parent, name_dest);
		if (sibling && !PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			parrillada_data_project_file_graft (PARRILLADA_DATA_PROJECT (monitor), node, name_dest);
			return;
		}

		if (sibling && PARRILLADA_FILE_NODE_VIRTUAL (sibling)) {
			/* Signal collision and remove virtual node but ignore result */
			parrillada_data_project_virtual_sibling (PARRILLADA_DATA_PROJECT (monitor), node, sibling);
		}

		/* If node was in the joliet incompatible table, remove it */
		parrillada_data_project_joliet_remove_node (PARRILLADA_DATA_PROJECT (monitor), node);

		/* see if we won't break the max path depth barrier */
		if (!parrillada_data_project_is_deep (PARRILLADA_DATA_PROJECT (monitor), parent,  PARRILLADA_FILE_NODE_NAME (node), node->is_file)) {
			parrillada_data_project_remove_node (PARRILLADA_DATA_PROJECT (monitor), node);
			return;
		}

		/* see if this node didn't replace an imported one. If so the old 
		 * imported node must re-appear in the tree after the move. */
		sibling = parrillada_file_node_check_imported_sibling (node);

		/* move it */
		former_parent = node->parent;
		former_position = parrillada_file_node_get_pos_as_child (node);

		stats = parrillada_file_node_get_tree_stats (priv->root, NULL);
		parrillada_file_node_move_from (node, stats);
		if (klass->node_removed)
			klass->node_removed (PARRILLADA_DATA_PROJECT (monitor),
					     former_parent,
					     former_position,
					     node);

		if (name_dest && strcmp (name_dest, name_src)) {
			/* the name has been changed so update it */
			parrillada_file_node_rename (node, name_dest);
		}

		/* Check joliet name compatibility. This must be done after the
		 * node information have been setup. */
		if (strlen (name_dest) > 64)
			parrillada_data_project_joliet_add_node (PARRILLADA_DATA_PROJECT (monitor), node);

		parrillada_file_node_move_to (node, parent, priv->sort_func);

		if (klass->node_added)
			klass->node_added (PARRILLADA_DATA_PROJECT (monitor), node, NULL);

		if (sibling) {
			ParrilladaDataProjectClass *klass;

			klass = PARRILLADA_DATA_PROJECT_GET_CLASS (monitor);

			/* restore the imported node that has the same name as old_name */
			parrillada_file_node_add (sibling->parent, sibling, priv->sort_func);
			if (klass->node_added)
				parrillada_data_project_add_node_and_children (PARRILLADA_DATA_PROJECT (monitor), sibling, klass->node_added);
		}
	}
}

static void
parrillada_data_project_file_removed (ParrilladaFileMonitor *monitor,
				   ParrilladaFileMonitorType type,
				   gpointer callback_data,
				   const gchar *name)
{
	ParrilladaDataProjectPrivate *priv;
	ParrilladaURINode *uri_node;
	ParrilladaFileNode *node;
	gchar *uri;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (monitor);

	/* If name is NULL then it means the event is against callback.
	 * Otherwise that's against one of the children of callback. */
	if (type == PARRILLADA_FILE_MONITOR_FOLDER)
		node = parrillada_file_node_check_name_existence (callback_data, name);
	else
		node = callback_data;

	if (!node)
		return;

	uri = parrillada_data_project_node_to_uri (PARRILLADA_DATA_PROJECT (monitor), node);
	parrillada_data_project_remove_node (PARRILLADA_DATA_PROJECT (monitor), node);

	/* a graft must have been created or already existed. */
	uri_node = g_hash_table_lookup (priv->grafts, uri);
	g_free (uri);

	/* check if we can remove it (no more nodes) */
	if (!uri_node || uri_node->nodes)
		return;

	g_hash_table_remove (priv->grafts, uri_node->uri);
	parrillada_utils_unregister_string (uri_node->uri);
	g_free (uri_node);
}

static void
parrillada_data_project_file_modified (ParrilladaFileMonitor *monitor,
				    gpointer callback_data,
				    const gchar *name)
{
	ParrilladaFileNode *node = callback_data;
	ParrilladaDataProjectPrivate *priv;
	ParrilladaDataProjectClass *klass;
	gchar *uri;

	priv = PARRILLADA_DATA_PROJECT_PRIVATE (monitor);

	if (node->is_loading)
		return;

	/* If that's a directory we don't need to reload it since we're watching
	 * it. That event is sent because the inode was written (a new file has
	 * been added to it). Now since we monitor the directory we'd rather
	 * process the added event (or the removed one). */
	if (!node->is_file)
		return;

	/* This is a call for a rescan of the node; flag it as loading */
	node->is_reloading = TRUE;

	/* Signal that something has changed in the tree */
	klass = PARRILLADA_DATA_PROJECT_GET_CLASS (PARRILLADA_DATA_PROJECT (monitor));
	uri = parrillada_data_project_node_to_uri (PARRILLADA_DATA_PROJECT (monitor), node);
	if (klass->node_added)
		klass->node_added (PARRILLADA_DATA_PROJECT (monitor), node, uri);
	g_free (uri);
}

#endif

static void
parrillada_data_project_class_init (ParrilladaDataProjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaDataProjectPrivate));

	object_class->finalize = parrillada_data_project_finalize;

	parrillada_data_project_signals [JOLIET_RENAME_SIGNAL] = 
	    g_signal_new ("joliet-rename",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__VOID,
			  G_TYPE_NONE,
			  0,
			  G_TYPE_NONE);
	parrillada_data_project_signals [NAME_COLLISION_SIGNAL] = 
	    g_signal_new ("name_collision",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  parrillada_marshal_BOOLEAN__POINTER,
			  G_TYPE_BOOLEAN,
			  1,
			  G_TYPE_POINTER);
	parrillada_data_project_signals [SIZE_CHANGED_SIGNAL] = 
	    g_signal_new ("size_changed",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__VOID,
			  G_TYPE_NONE,
			  0,
			  G_TYPE_NONE);
	parrillada_data_project_signals [DEEP_DIRECTORY_SIGNAL] = 
	    g_signal_new ("deep_directory",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  parrillada_marshal_BOOLEAN__STRING,
			  G_TYPE_BOOLEAN,
			  1,
			  G_TYPE_STRING);
	parrillada_data_project_signals [G2_FILE_SIGNAL] = 
	    g_signal_new ("2G_file",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  parrillada_marshal_BOOLEAN__STRING,
			  G_TYPE_BOOLEAN,
			  1,
			  G_TYPE_STRING);
	parrillada_data_project_signals [PROJECT_LOADED_SIGNAL] = 
	    g_signal_new ("project-loaded",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  g_cclosure_marshal_VOID__INT,
			  G_TYPE_NONE,
			  1,
			  G_TYPE_INT);

	parrillada_data_project_signals [VIRTUAL_SIBLING_SIGNAL] = 
	    g_signal_new ("virtual-sibling",
			  G_TYPE_FROM_CLASS (klass),
			  G_SIGNAL_RUN_LAST|G_SIGNAL_NO_RECURSE,
			  0,
			  NULL, NULL,
			  parrillada_marshal_VOID__POINTER_POINTER,
			  G_TYPE_NONE,
			  2,
			  G_TYPE_POINTER,
			  G_TYPE_POINTER);

#ifdef BUILD_INOTIFY

	ParrilladaFileMonitorClass *monitor_class = PARRILLADA_FILE_MONITOR_CLASS (klass);

	monitor_class->file_added = parrillada_data_project_file_added;
	monitor_class->file_moved = parrillada_data_project_file_moved;
	monitor_class->file_removed = parrillada_data_project_file_removed;
	monitor_class->file_renamed = parrillada_data_project_file_renamed;
	monitor_class->file_modified = parrillada_data_project_file_modified;

#endif
}
