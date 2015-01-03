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
#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include "burn-basics.h"
#include "parrillada-tray.h"

static void parrillada_tray_icon_class_init (ParrilladaTrayIconClass *klass);
static void parrillada_tray_icon_init (ParrilladaTrayIcon *sp);
static void parrillada_tray_icon_finalize (GObject *object);

static void
parrillada_tray_icon_menu_popup_cb (ParrilladaTrayIcon *tray,
				 guint button,
				 guint time,
				 gpointer user_data);
static void
parrillada_tray_icon_activate_cb (ParrilladaTrayIcon *tray,
			       gpointer user_data);

static void
parrillada_tray_icon_cancel_cb (GtkAction *action, ParrilladaTrayIcon *tray);

static void
parrillada_tray_icon_show_cb (GtkAction *action, ParrilladaTrayIcon *tray);

struct ParrilladaTrayIconPrivate {
	ParrilladaBurnAction action;
	gchar *action_string;

	GtkUIManager *manager;

	int rounded_percent;
	int percent;
};

typedef enum {
	CANCEL_SIGNAL,
	CLOSE_AFTER_SIGNAL,
	SHOW_DIALOG_SIGNAL,
	LAST_SIGNAL
} ParrilladaTrayIconSignalType;

static guint parrillada_tray_icon_signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

static GtkActionEntry entries[] = {
	{"ContextualMenu", NULL, N_("Menu")},
	{"Cancel", GTK_STOCK_CANCEL, NULL, NULL, N_("Cancel ongoing burning"),
	 G_CALLBACK (parrillada_tray_icon_cancel_cb)},
};

static GtkToggleActionEntry toggle_entries[] = {
	{"Show", NULL, N_("Show _Dialog"), NULL, N_("Show dialog"),
	 G_CALLBACK (parrillada_tray_icon_show_cb), TRUE,},
};

static const char *description = {
	"<ui>"
	"<popup action='ContextMenu'>"
		"<menuitem action='Show'/>"
		"<separator/>"
		"<menuitem action='Cancel'/>"
	"</popup>"
	"</ui>"
};

GType
parrillada_tray_icon_get_type ()
{
	static GType type = 0;

	if(type == 0) {
		static const GTypeInfo our_info = {
			sizeof (ParrilladaTrayIconClass),
			NULL,
			NULL,
			(GClassInitFunc) parrillada_tray_icon_class_init,
			NULL,
			NULL,
			sizeof (ParrilladaTrayIcon),
			0,
			(GInstanceInitFunc) parrillada_tray_icon_init,
		};

		type = g_type_register_static(GTK_TYPE_STATUS_ICON, 
					      "ParrilladaTrayIcon",
					      &our_info,
					      0);
	}

	return type;
}

static void
parrillada_tray_icon_class_init (ParrilladaTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_peek_parent(klass);
	object_class->finalize = parrillada_tray_icon_finalize;

	parrillada_tray_icon_signals[SHOW_DIALOG_SIGNAL] =
	    g_signal_new ("show_dialog",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			  G_STRUCT_OFFSET (ParrilladaTrayIconClass,
					   show_dialog), NULL, NULL,
			  g_cclosure_marshal_VOID__BOOLEAN,
			  G_TYPE_NONE,
			  1,
			  G_TYPE_BOOLEAN);
	parrillada_tray_icon_signals[CANCEL_SIGNAL] =
	    g_signal_new ("cancel",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			  G_STRUCT_OFFSET (ParrilladaTrayIconClass,
					   cancel), NULL, NULL,
			  g_cclosure_marshal_VOID__VOID,
			  G_TYPE_NONE,
			  0);
	parrillada_tray_icon_signals[CLOSE_AFTER_SIGNAL] =
	    g_signal_new ("close_after",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			  G_STRUCT_OFFSET (ParrilladaTrayIconClass,
					   close_after), NULL, NULL,
			  g_cclosure_marshal_VOID__BOOLEAN,
			  G_TYPE_NONE,
			  1,
			  G_TYPE_BOOLEAN);
}

static void
parrillada_tray_icon_build_menu (ParrilladaTrayIcon *tray)
{
	GtkActionGroup *action_group;
	GError *error = NULL;

	action_group = gtk_action_group_new ("MenuAction");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (action_group,
				      entries,
				      G_N_ELEMENTS (entries),
				      tray);
	gtk_action_group_add_toggle_actions (action_group,
					     toggle_entries,
					     G_N_ELEMENTS (toggle_entries),
					     tray);

	tray->priv->manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (tray->priv->manager,
					    action_group,
					    0);

	if (!gtk_ui_manager_add_ui_from_string (tray->priv->manager,
						description,
						-1,
						&error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}
}

static void
parrillada_tray_icon_init (ParrilladaTrayIcon *obj)
{
	obj->priv = g_new0 (ParrilladaTrayIconPrivate, 1);
	parrillada_tray_icon_build_menu (obj);
	g_signal_connect (obj,
			  "popup-menu",
			  G_CALLBACK (parrillada_tray_icon_menu_popup_cb),
			  NULL);
	g_signal_connect (obj,
			  "activate",
			  G_CALLBACK (parrillada_tray_icon_activate_cb),
			  NULL);

	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (obj), "parrillada-disc-00");
}

static void
parrillada_tray_icon_finalize (GObject *object)
{
	ParrilladaTrayIcon *cobj;

	cobj = PARRILLADA_TRAYICON (object);

	if (cobj->priv->action_string) {
		g_free (cobj->priv->action_string);
		cobj->priv->action_string = NULL;
	}

	g_free (cobj->priv);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

ParrilladaTrayIcon *
parrillada_tray_icon_new ()
{
	ParrilladaTrayIcon *obj;
	
	obj = PARRILLADA_TRAYICON (g_object_new (PARRILLADA_TYPE_TRAYICON, NULL));

	return obj;
}

static void
parrillada_tray_icon_set_tooltip (ParrilladaTrayIcon *tray,
			       glong remaining)
{
	gchar *text;
	const gchar *action_string;

	if (!tray->priv->action_string)
		action_string = parrillada_burn_action_to_string (tray->priv->action);
	else
		action_string = tray->priv->action_string;

	if (remaining > 0) {
		gchar *remaining_string;

		remaining_string = parrillada_units_get_time_string ((double) remaining * 1000000000, TRUE, FALSE);
		/* Translators: the first %s is a string containing a description of the ongoing action
		 *	        the first %d is a number (a percentage) representing how much of the above task has been completed so far
		 *	        the second %s is a string containing the remaining time before completion.
		 */
		text = g_strdup_printf (_("%s, %d%% done, %s remaining"),
					action_string,
					tray->priv->percent,
					remaining_string);
		g_free (remaining_string);
	}
	else if (tray->priv->percent > 0)
		/* Translators: the first %s is a string containing a description of the ongoing action
		 *	        the first %d is a number (a percentage) representing how much of the above task has been completed so far
		 */
		text = g_strdup_printf (_("%s, %d%% done"),
					action_string,
					tray->priv->percent);
	else
		text = g_strdup (action_string);

	gtk_status_icon_set_tooltip_text (GTK_STATUS_ICON (tray), text);
	g_free (text);
}

void
parrillada_tray_icon_set_action (ParrilladaTrayIcon *tray,
			      ParrilladaBurnAction action,
			      const gchar *string)
{
	tray->priv->action = action;
	if (tray->priv->action_string)
		g_free (tray->priv->action_string);

	if (string)
		tray->priv->action_string = g_strdup (string);
	else
		tray->priv->action_string = NULL;

	parrillada_tray_icon_set_tooltip (tray, -1);
}

void
parrillada_tray_icon_set_progress (ParrilladaTrayIcon *tray,
				gdouble fraction,
				glong remaining)
{
	gint percent;
	gint remains;
	gchar *icon_name;

	percent = fraction * 100;
	tray->priv->percent = percent;

	/* set the tooltip */
	parrillada_tray_icon_set_tooltip (tray, remaining);

	/* change image if need be */
	remains = percent % 5;
	if (remains > 3)
		percent += 5 - remains;
	else
		percent -= remains;

	if (tray->priv->rounded_percent == percent
	||  percent < 0 || percent > 100)
		return;

	tray->priv->rounded_percent = percent;

	icon_name = g_strdup_printf ("parrillada-disc-%02i", percent);
	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (tray), icon_name);
	g_free (icon_name);
}

static void
parrillada_tray_icon_change_show_dialog_state (ParrilladaTrayIcon *tray)
{
	GtkAction *action;
	gboolean active;

	/* update menu */
	action = gtk_ui_manager_get_action (tray->priv->manager, "/ContextMenu/Show");
	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

	/* signal show dialog was requested the dialog again */
	g_signal_emit (tray,
		       parrillada_tray_icon_signals [SHOW_DIALOG_SIGNAL],
		       0,
		       active);
}

static void
parrillada_tray_icon_menu_popup_cb (ParrilladaTrayIcon *tray,
				 guint button,
				 guint time,
				 gpointer user_data)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (tray->priv->manager,"/ContextMenu");
	gtk_menu_popup (GTK_MENU (menu),
			NULL,
			NULL,
			gtk_status_icon_position_menu,
			tray,
			button,
			time);
}

static void
parrillada_tray_icon_activate_cb (ParrilladaTrayIcon *tray,
			       gpointer user_data)
{
	GtkAction *action;
	gboolean show;
	
	/* update menu */
	action = gtk_ui_manager_get_action (tray->priv->manager, "/ContextMenu/Show");
	show = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	show = show ? FALSE:TRUE;
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show);
}

static void
parrillada_tray_icon_cancel_cb (GtkAction *action, ParrilladaTrayIcon *tray)
{
	g_signal_emit (tray,
		       parrillada_tray_icon_signals [CANCEL_SIGNAL],
		       0);
}

static void
parrillada_tray_icon_show_cb (GtkAction *action, ParrilladaTrayIcon *tray)
{
	parrillada_tray_icon_change_show_dialog_state (tray);
}

void
parrillada_tray_icon_set_show_dialog (ParrilladaTrayIcon *tray, gboolean show)
{
	GtkAction *action;

	/* update menu */
	action = gtk_ui_manager_get_action (tray->priv->manager, "/ContextMenu/Show");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show);
}
