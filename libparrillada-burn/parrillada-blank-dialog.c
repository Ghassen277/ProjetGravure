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
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include <canberra-gtk.h>

#include "parrillada-misc.h"

#include "burn-basics.h"

#include "parrillada-session.h"
#include "parrillada-session-helper.h"
#include "parrillada-burn.h"

#include "burn-plugin-manager.h"
#include "parrillada-tool-dialog.h"
#include "parrillada-tool-dialog-private.h"
#include "parrillada-blank-dialog.h"

G_DEFINE_TYPE (ParrilladaBlankDialog, parrillada_blank_dialog, PARRILLADA_TYPE_TOOL_DIALOG);

struct ParrilladaBlankDialogPrivate {
	ParrilladaBurnSession *session;

	GtkWidget *fast;

	guint caps_sig;
	guint output_sig;

	guint fast_saved;
	guint dummy_saved;
};
typedef struct ParrilladaBlankDialogPrivate ParrilladaBlankDialogPrivate;

#define PARRILLADA_BLANK_DIALOG_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_BLANK_DIALOG, ParrilladaBlankDialogPrivate))

static ParrilladaToolDialogClass *parent_class = NULL;

static guint
parrillada_blank_dialog_set_button (ParrilladaBurnSession *session,
				 guint saved,
				 GtkWidget *button,
				 ParrilladaBurnFlag flag,
				 ParrilladaBurnFlag supported,
				 ParrilladaBurnFlag compulsory)
{
	if (flag & supported) {
		if (compulsory & flag) {
			if (gtk_widget_get_sensitive (button))
				saved = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

			gtk_widget_set_sensitive (button, FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

			parrillada_burn_session_add_flag (session, flag);
		}
		else {
			if (!gtk_widget_get_sensitive (button)) {
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), saved);

				if (saved)
					parrillada_burn_session_add_flag (session, flag);
				else
					parrillada_burn_session_remove_flag (session, flag);
			}

			gtk_widget_set_sensitive (button, TRUE);
		}
	}
	else {
		if (gtk_widget_get_sensitive (button))
			saved = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

		gtk_widget_set_sensitive (button, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);

		parrillada_burn_session_remove_flag (session, flag);
	}

	return saved;
}

static void
parrillada_blank_dialog_device_opts_setup (ParrilladaBlankDialog *self)
{
	ParrilladaBurnFlag supported;
	ParrilladaBurnFlag compulsory;
	ParrilladaBlankDialogPrivate *priv;

	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (self);

	/* set the options */
	parrillada_burn_session_get_blank_flags (priv->session,
					      &supported,
					      &compulsory);

	priv->fast_saved = parrillada_blank_dialog_set_button (priv->session,
							    priv->fast_saved,
							    priv->fast,
							    PARRILLADA_BURN_FLAG_FAST_BLANK,
							    supported,
							    compulsory);

	/* This must be done afterwards, once the session flags were updated */
	parrillada_tool_dialog_set_valid (PARRILLADA_TOOL_DIALOG (self),
				       (parrillada_burn_session_can_blank (priv->session) == PARRILLADA_BURN_OK));
}

static void
parrillada_blank_dialog_caps_changed (ParrilladaPluginManager *manager,
				   ParrilladaBlankDialog *dialog)
{
	parrillada_blank_dialog_device_opts_setup (dialog);
}

static void
parrillada_blank_dialog_output_changed (ParrilladaBurnSession *session,
				     ParrilladaMedium *former,
				     ParrilladaBlankDialog *dialog)
{
	parrillada_blank_dialog_device_opts_setup (dialog);
}

static void
parrillada_blank_dialog_fast_toggled (GtkToggleButton *toggle,
				   ParrilladaBlankDialog *self)
{
	ParrilladaBlankDialogPrivate *priv;

	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (self);
	if (gtk_toggle_button_get_active (toggle))
		parrillada_burn_session_add_flag (priv->session, PARRILLADA_BURN_FLAG_FAST_BLANK);
	else
		parrillada_burn_session_remove_flag (priv->session, PARRILLADA_BURN_FLAG_FAST_BLANK);
}

static void
parrillada_blank_dialog_drive_changed (ParrilladaToolDialog *dialog,
				    ParrilladaMedium *medium)
{
	ParrilladaBlankDialogPrivate *priv;
	ParrilladaDrive *drive;

	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (dialog);

	if (medium)
		drive = parrillada_medium_get_drive (medium);
	else
		drive = NULL;

	/* it can happen that the drive changed while initializing and that
	 * session hasn't been created yet. */
	if (priv->session)
		parrillada_burn_session_set_burner (priv->session, drive);
}

static gboolean
parrillada_blank_dialog_activate (ParrilladaToolDialog *dialog,
			       ParrilladaMedium *medium)
{
	ParrilladaBlankDialogPrivate *priv;
	ParrilladaBlankDialog *self;
	ParrilladaBurnResult result;
	GError *error = NULL;
	ParrilladaBurn *burn;

	self = PARRILLADA_BLANK_DIALOG (dialog);
	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (self);

	burn = parrillada_tool_dialog_get_burn (dialog);
	parrillada_burn_session_start (priv->session);
	result = parrillada_burn_blank (burn,
				     priv->session,
				     &error);

	/* Tell the user the result of the operation */
	if (result == PARRILLADA_BURN_ERR || error) {
		GtkResponseType answer;
		GtkWidget *message;
		GtkWidget *button;

		message =  gtk_message_dialog_new (GTK_WINDOW (self),
						   GTK_DIALOG_DESTROY_WITH_PARENT|
						   GTK_DIALOG_MODAL,
						   GTK_MESSAGE_ERROR,
						   GTK_BUTTONS_CLOSE,
						   _("Error while blanking."));

		gtk_window_set_icon_name (GTK_WINDOW (message),
					  gtk_window_get_icon_name (GTK_WINDOW (self)));

		button = parrillada_utils_make_button (_("Blank _Again"),
						    NULL,
						    "media-optical-blank",
						    GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (button);
		gtk_dialog_add_action_widget (GTK_DIALOG (message),
					      button,
					      GTK_RESPONSE_OK);

		if (error) {
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message),
								  "%s.",
								  error->message);
			g_error_free (error);
		}
		else
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message),
								  _("Unknown error."));

		answer = gtk_dialog_run (GTK_DIALOG (message));
		gtk_widget_destroy (message);

		if (answer == GTK_RESPONSE_OK) {
			parrillada_blank_dialog_device_opts_setup (self);
			return FALSE;
		}
	}
	else if (result == PARRILLADA_BURN_OK) {
		GtkResponseType answer;
		GtkWidget *message;
		GtkWidget *button;

		message = gtk_message_dialog_new (GTK_WINDOW (self),
						  GTK_DIALOG_DESTROY_WITH_PARENT|
						  GTK_DIALOG_MODAL,
						  GTK_MESSAGE_INFO,
						  GTK_BUTTONS_NONE,
						  _("The disc was successfully blanked."));

		gtk_window_set_icon_name (GTK_WINDOW (message),
					  gtk_window_get_icon_name (GTK_WINDOW (self)));

		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message),
							  _("The disc is ready for use."));

		button = parrillada_utils_make_button (_("Blank _Again"),
						    NULL,
						    "media-optical-blank",
						    GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (button);
		gtk_dialog_add_action_widget (GTK_DIALOG (message),
					      button,
					      GTK_RESPONSE_OK);

		gtk_dialog_add_button (GTK_DIALOG (message),
				       GTK_STOCK_CLOSE,
				       GTK_RESPONSE_CLOSE);

		gtk_widget_show (GTK_WIDGET (message));
		ca_gtk_play_for_widget (GTK_WIDGET (message), 0,
					CA_PROP_EVENT_ID, "complete-media-format",
					CA_PROP_EVENT_DESCRIPTION, _("The disc was successfully blanked."),
					NULL);

		answer = gtk_dialog_run (GTK_DIALOG (message));
		gtk_widget_destroy (message);

		if (answer == GTK_RESPONSE_OK) {
			parrillada_blank_dialog_device_opts_setup (self);
			return FALSE;
		}
	}
	else if (result == PARRILLADA_BURN_NOT_SUPPORTED) {
		g_warning ("operation not supported");
	}
	else if (result == PARRILLADA_BURN_NOT_READY) {
		g_warning ("operation not ready");
	}
	else if (result == PARRILLADA_BURN_NOT_RUNNING) {
		g_warning ("job not running");
	}
	else if (result == PARRILLADA_BURN_RUNNING) {
		g_warning ("job running");
	}

	return TRUE;
}

static void
parrillada_blank_dialog_finalize (GObject *object)
{
	ParrilladaBlankDialogPrivate *priv;

	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (object);

	if (priv->caps_sig) {
		ParrilladaPluginManager *manager;

		manager = parrillada_plugin_manager_get_default ();
		g_signal_handler_disconnect (manager, priv->caps_sig);
		priv->caps_sig = 0;
	}

	if (priv->output_sig) {
		g_signal_handler_disconnect (priv->session, priv->output_sig);
		priv->output_sig = 0;
	}

	if (priv->session) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
parrillada_blank_dialog_class_init (ParrilladaBlankDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ParrilladaToolDialogClass *tool_dialog_class = PARRILLADA_TOOL_DIALOG_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaBlankDialogPrivate));

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = parrillada_blank_dialog_finalize;

	tool_dialog_class->activate = parrillada_blank_dialog_activate;
	tool_dialog_class->medium_changed = parrillada_blank_dialog_drive_changed;
}

static void
parrillada_blank_dialog_init (ParrilladaBlankDialog *obj)
{
	ParrilladaBlankDialogPrivate *priv;
	ParrilladaPluginManager *manager;
	ParrilladaMedium *medium;
	ParrilladaDrive *drive;

	priv = PARRILLADA_BLANK_DIALOG_PRIVATE (obj);

	parrillada_tool_dialog_set_button (PARRILLADA_TOOL_DIALOG (obj),
					/* Translators: This is a verb, an action */
					_("_Blank"),
					NULL,
					"media-optical-blank");

	/* only media that can be rewritten with or without data */
	parrillada_tool_dialog_set_medium_type_shown (PARRILLADA_TOOL_DIALOG (obj),
						   PARRILLADA_MEDIA_TYPE_REWRITABLE);

	medium = parrillada_tool_dialog_get_medium (PARRILLADA_TOOL_DIALOG (obj));
	drive = parrillada_medium_get_drive (medium);

	priv->session = parrillada_burn_session_new ();
	parrillada_burn_session_set_flags (priv->session,
				        PARRILLADA_BURN_FLAG_EJECT|
				        PARRILLADA_BURN_FLAG_NOGRACE);
	parrillada_burn_session_set_burner (priv->session, drive);

	if (medium)
		g_object_unref (medium);

	priv->output_sig = g_signal_connect (priv->session,
					     "output-changed",
					     G_CALLBACK (parrillada_blank_dialog_output_changed),
					     obj);

	manager = parrillada_plugin_manager_get_default ();
	priv->caps_sig = g_signal_connect (manager,
					   "caps-changed",
					   G_CALLBACK (parrillada_blank_dialog_caps_changed),
					   obj);

	priv->fast = gtk_check_button_new_with_mnemonic (_("_Fast blanking"));
	gtk_widget_set_tooltip_text (priv->fast, _("Activate fast blanking, as opposed to a longer, thorough blanking"));
	g_signal_connect (priv->fast,
			  "clicked",
			  G_CALLBACK (parrillada_blank_dialog_fast_toggled),
			  obj);

	parrillada_tool_dialog_pack_options (PARRILLADA_TOOL_DIALOG (obj),
					  priv->fast,
					  NULL);

	parrillada_blank_dialog_device_opts_setup (obj);

	/* if fast blank is supported check it by default */
	if (gtk_widget_is_sensitive (priv->fast))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->fast), TRUE);
}

/**
 * parrillada_blank_dialog_new:
 *
 * Creates a new #ParrilladaBlankDialog object
 *
 * Return value: a #ParrilladaBlankDialog. Unref when it is not needed anymore.
 **/
ParrilladaBlankDialog *
parrillada_blank_dialog_new ()
{
	ParrilladaBlankDialog *obj;

	obj = PARRILLADA_BLANK_DIALOG (g_object_new (PARRILLADA_TYPE_BLANK_DIALOG,
						  "title", _("Disc Blanking"),
						  NULL));
	return obj;
}
