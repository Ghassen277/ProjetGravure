/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libparrillada-burn
 * Copyright (C) Luis Medinas 2008 <lmedinas@gmail.com>
 * Copyright (C) Philippe Rouquier 2008 <bonfire-app@wanadoo.fr>
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

#include <sys/utsname.h>
#include <string.h>

#include <glib.h>
#include <gdk/gdk.h>

#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>
#include <gst/pbutils/missing-plugins.h>

#include "parrillada-misc.h"
#include "parrillada-pk.h"

static GSList *already_tested = NULL;

typedef struct _ParrilladaPKPrivate ParrilladaPKPrivate;
struct _ParrilladaPKPrivate
{
	GDBusConnection *connection;
	GDBusProxy *proxy;

	GVariant *values;
	GAsyncResult *result;
	GMainLoop *loop;
	gboolean res;
};

#define PARRILLADA_PK_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_PK, ParrilladaPKPrivate))

G_DEFINE_TYPE (ParrilladaPK, parrillada_pk, G_TYPE_OBJECT);

static void
parrillada_pk_install_missing_files_result (GObject *source_object,
					 GAsyncResult *result,
                                         gpointer user_data)
{
	GError *error = NULL;
	ParrilladaPKPrivate *priv = PARRILLADA_PK_PRIVATE (user_data);

	priv->proxy = G_DBUS_PROXY (source_object);

	priv->values = g_dbus_proxy_call_finish (priv->proxy, 
						 result, 
						 &error);

	if (priv->values == NULL) {
		PARRILLADA_UTILS_LOG ("%s", error->message);
		g_error_free (error);
	}

	if (priv->values != NULL)
		g_variant_unref (priv->values);
	g_object_unref (priv->proxy);
}

static void
parrillada_pk_cancelled (GCancellable *cancel,
                      ParrilladaPK *package)
{
	GError *error = NULL;
	ParrilladaPKPrivate *priv = PARRILLADA_PK_PRIVATE (package);

	priv->res = FALSE;

	if (priv->proxy)
		g_dbus_proxy_call_finish (priv->proxy, 
					  priv->result,
					  &error);

	if (priv->loop)
		g_main_loop_quit (priv->loop);
}

static gboolean
parrillada_pk_wait_for_call_end (ParrilladaPK *package,
                              GCancellable *cancel)
{
	ParrilladaPKPrivate *priv;
	GMainLoop *loop;
	gulong sig_int;

	priv = PARRILLADA_PK_PRIVATE (package);

	loop = g_main_loop_new (NULL, FALSE);
	priv->loop = loop;

	sig_int = g_signal_connect (cancel,
	                            "cancelled",
	                            G_CALLBACK (parrillada_pk_cancelled),
	                            package);

	GDK_THREADS_LEAVE ();
	g_main_loop_run (loop);
	GDK_THREADS_ENTER ();

	g_signal_handler_disconnect (cancel, sig_int);

	g_main_loop_unref (loop);
	priv->loop = NULL;

	return priv->res;
}

static gboolean
parrillada_pk_connect (ParrilladaPK *package)
{
	ParrilladaPKPrivate *priv;
	GError *error = NULL;

	priv = PARRILLADA_PK_PRIVATE (package);

	/* check dbus connections, exit if not valid */
	priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (priv->connection == NULL) {
		PARRILLADA_UTILS_LOG ("%s", error->message);
		return FALSE;
	}

	/* get a connection */
	priv->proxy = g_dbus_proxy_new_sync (priv->connection,
					     			  G_DBUS_PROXY_FLAGS_NONE,
					                          NULL,
	                                                          "org.freedesktop.PackageKit",
	                                                          "/org/freedesktop/PackageKit",
	                                                          "org.freedesktop.PackageKit.Modify",
						                  NULL,
						                 &error);
	if (priv->proxy == NULL) {
		PARRILLADA_UTILS_LOG ("Cannot connect to session service");
		return FALSE;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	g_dbus_proxy_set_default_timeout (priv->proxy, INT_MAX);

	return TRUE;
}

#if 0

/**
 * This would be the proper way to do it except
 * it has two faults:
 * - it cannot be cancelled
 * - it does not work for elements
 **/
 
static void
parrillada_pk_install_gst_plugin_result (GstInstallPluginsReturn res,
                                      gpointer user_data)
{
	ParrilladaPKPrivate *priv = PARRILLADA_PK_PRIVATE (user_data);

	switch (res) {
	case GST_INSTALL_PLUGINS_SUCCESS:
		priv->res = TRUE;
		break;

	case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:
	case GST_INSTALL_PLUGINS_USER_ABORT:

	case GST_INSTALL_PLUGINS_NOT_FOUND:
	case GST_INSTALL_PLUGINS_ERROR:
	case GST_INSTALL_PLUGINS_CRASHED:
	default:
		priv->res = FALSE;
		break;
	}

	g_main_loop_quit (priv->loop);
}

gboolean
parrillada_pk_install_gstreamer_plugin (ParrilladaPK *package,
                                     const gchar *element_name,
                                     int xid,
				     GCancellable *cancel)
{
	GstInstallPluginsContext *context;
	GPtrArray *gst_plugins = NULL;
	GstInstallPluginsReturn status;
	gboolean res = FALSE;
	gchar *detail;

	/* The problem with this is that we can't 
	 * cancel a search */
	gst_plugins = g_ptr_array_new ();
	detail = gst_missing_element_installer_detail_new (element_name);
	g_ptr_array_add (gst_plugins, detail);
	g_ptr_array_add (gst_plugins, NULL);

	context = gst_install_plugins_context_new ();
	gst_install_plugins_context_set_xid (context, xid);
	status = gst_install_plugins_async ((gchar **) gst_plugins->pdata,
	                                    context,
	                                    parrillada_pk_install_gst_plugin_result,
	                                    package);

	if (status == GST_INSTALL_PLUGINS_STARTED_OK)
		res = parrillada_pk_wait_for_call_end (package, cancel);

	gst_install_plugins_context_free (context);
	g_strfreev ((gchar **) gst_plugins->pdata);
	g_ptr_array_free (gst_plugins, FALSE);

	return res;
}

#endif

static gboolean
parrillada_pk_install_file_requirement (ParrilladaPK *package,
                                     GPtrArray *missing_files,
                                     int xid,
				     GCancellable *cancel)
{
	ParrilladaPKPrivate *priv;

	priv = PARRILLADA_PK_PRIVATE (package);

	if (!parrillada_pk_connect (package))
		return FALSE;

	g_dbus_proxy_call (priv->proxy,
				      "InstallProvideFiles",
				      g_variant_new ("(uass)",
						     xid,
						     package,
						     "hide-confirm-search,hide-finished,hide-warning"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      parrillada_pk_install_missing_files_result,
				      package);

	return parrillada_pk_wait_for_call_end (package, cancel);
}

gboolean
parrillada_pk_install_missing_app (ParrilladaPK *package,
                                const gchar *file_name,
                                int xid,
                                GCancellable *cancel)
{
	gchar *path;
	gboolean res;
	GPtrArray *missing_files;

	path = g_build_path (G_DIR_SEPARATOR_S,
	                     "/usr/bin/",
	                     file_name,
	                     NULL);

	if (g_slist_find_custom (already_tested, path, (GCompareFunc) g_strcmp0)) {
		g_free (path);
		return FALSE;
	}
	already_tested = g_slist_prepend (already_tested, g_strdup (path));

	missing_files = g_ptr_array_new ();
	g_ptr_array_add (missing_files, path);
	g_ptr_array_add (missing_files, NULL);

	res = parrillada_pk_install_file_requirement (package, missing_files, xid, cancel);

	g_strfreev ((gchar **) missing_files->pdata);
	g_ptr_array_free (missing_files, FALSE);

	return res;
}

/**
 * pk_gst_get_arch_suffix:
 *
 * Return value: something other than blank if we are running on 64 bit.
 **/
static gboolean
pk_gst_is_x64_arch (void)
{
	gint retval;
	struct utsname buf;

	retval = uname (&buf);

	/* did we get valid value? */
	if (retval != 0 || buf.machine == NULL) {
		g_warning ("PackageKit: cannot get machine type");
		return FALSE;
	}

	/* 64 bit machines */
	if (g_strcmp0 (buf.machine, "x86_64") == 0)
		return TRUE;

	/* 32 bit machines and unrecognized arch */
	return FALSE;
}

gboolean
parrillada_pk_install_missing_library (ParrilladaPK *package,
                                    const gchar *library_name,
                                    int xid,
                                    GCancellable *cancel)
{
	gchar *path;
	gboolean res;
	GPtrArray *missing_files;

	if (pk_gst_is_x64_arch ())
		path = g_strdup_printf ("/usr/lib64/%s", library_name);
	else
		path = g_strdup_printf ("/usr/lib/%s", library_name);

	if (g_slist_find_custom (already_tested, path, (GCompareFunc) g_strcmp0)) {
		g_free (path);
		return FALSE;
	}
	already_tested = g_slist_prepend (already_tested, g_strdup (path));

	missing_files = g_ptr_array_new ();
	g_ptr_array_add (missing_files, path);
	g_ptr_array_add (missing_files, NULL);

	res = parrillada_pk_install_file_requirement (package, missing_files, xid, cancel);

	g_strfreev ((gchar **) missing_files->pdata);
	g_ptr_array_free (missing_files, FALSE);

	return res;
}

gboolean
parrillada_pk_install_gstreamer_plugin (ParrilladaPK *package,
                                     const gchar *element_name,
                                     int xid,
                                     GCancellable *cancel)
{
	gchar *resource;
	const gchar *name;
	ParrilladaPKPrivate *priv;
	GPtrArray *missing_files;

	priv = PARRILLADA_PK_PRIVATE (package);

	/* The whole function is gross but it works:
	 * - on fedora */

	/* This is a special case for ffmpeg plugin. It
	 * comes as a single library for all elements
	 * so we have to workaround this */
	if (!strncmp (element_name, "ff", 2))
		name = "ffmpeg";
	else
		name = element_name;

	if (pk_gst_is_x64_arch ())
		resource = g_strdup_printf ("/usr/lib64/gstreamer-0.10/libgst%s.so", name);
	else
		resource = g_strdup_printf ("/usr/lib/gstreamer-0.10/libgst%s.so", name);

	if (g_slist_find_custom (already_tested, resource, (GCompareFunc) g_strcmp0)) {
		g_free (resource);
		return FALSE;
	}
	already_tested = g_slist_prepend (already_tested, g_strdup (resource));

	missing_files = g_ptr_array_new ();
	g_ptr_array_add (missing_files, resource);
	g_ptr_array_add (missing_files, NULL);

	priv->res = parrillada_pk_install_file_requirement (package, missing_files, xid, cancel);

	if (priv->res)
		 priv->res = gst_update_registry ();

	g_strfreev ((gchar **) missing_files->pdata);
	g_ptr_array_free (missing_files, FALSE);

	return priv->res;
}

static void
parrillada_pk_init (ParrilladaPK *object)
{}

static void
parrillada_pk_finalize (GObject *object)
{
	GError *error = NULL;
	ParrilladaPKPrivate *priv;

	priv = PARRILLADA_PK_PRIVATE (object);

	if (priv->proxy)
		g_dbus_proxy_call_finish (priv->proxy, priv->result, &error);

	if (priv->loop)
		g_main_loop_quit (priv->loop);

	if (priv->proxy) {
		g_object_unref (priv->proxy);
		priv->proxy = NULL;
	}

	if (priv->connection) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	G_OBJECT_CLASS (parrillada_pk_parent_class)->finalize (object);
}

static void
parrillada_pk_class_init (ParrilladaPKClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaPKPrivate));

	object_class->finalize = parrillada_pk_finalize;
}

ParrilladaPK *
parrillada_pk_new (void)
{
	return g_object_new (PARRILLADA_TYPE_PK, NULL);
}
