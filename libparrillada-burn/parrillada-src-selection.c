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

#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include "parrillada-src-selection.h"
#include "parrillada-medium-selection.h"

#include "parrillada-track.h"
#include "parrillada-session.h"
#include "parrillada-track-disc.h"

#include "parrillada-drive.h"
#include "parrillada-volume.h"

typedef struct _ParrilladaSrcSelectionPrivate ParrilladaSrcSelectionPrivate;
struct _ParrilladaSrcSelectionPrivate
{
	ParrilladaBurnSession *session;
	ParrilladaTrackDisc *track;
};

#define PARRILLADA_SRC_SELECTION_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_SRC_SELECTION, ParrilladaSrcSelectionPrivate))

enum {
	PROP_0,
	PROP_SESSION
};

G_DEFINE_TYPE (ParrilladaSrcSelection, parrillada_src_selection, PARRILLADA_TYPE_MEDIUM_SELECTION);

static void
parrillada_src_selection_medium_changed (ParrilladaMediumSelection *selection,
				      ParrilladaMedium *medium)
{
	ParrilladaSrcSelectionPrivate *priv;
	ParrilladaDrive *drive = NULL;

	priv = PARRILLADA_SRC_SELECTION_PRIVATE (selection);

	if (priv->session && priv->track) {
		drive = parrillada_medium_get_drive (medium);
		parrillada_track_disc_set_drive (priv->track, drive);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (selection), drive != NULL);

	if (PARRILLADA_MEDIUM_SELECTION_CLASS (parrillada_src_selection_parent_class)->medium_changed)
		PARRILLADA_MEDIUM_SELECTION_CLASS (parrillada_src_selection_parent_class)->medium_changed (selection, medium);
}

GtkWidget *
parrillada_src_selection_new (ParrilladaBurnSession *session)
{
	g_return_val_if_fail (PARRILLADA_IS_BURN_SESSION (session), NULL);
	return GTK_WIDGET (g_object_new (PARRILLADA_TYPE_SRC_SELECTION,
					 "session", session,
					 NULL));
}

static void
parrillada_src_selection_constructed (GObject *object)
{
	G_OBJECT_CLASS (parrillada_src_selection_parent_class)->constructed (object);

	/* only show media with something to be read on them */
	parrillada_medium_selection_show_media_type (PARRILLADA_MEDIUM_SELECTION (object),
						  PARRILLADA_MEDIA_TYPE_AUDIO|
						  PARRILLADA_MEDIA_TYPE_DATA);
}

static void
parrillada_src_selection_init (ParrilladaSrcSelection *object)
{
}

static void
parrillada_src_selection_finalize (GObject *object)
{
	ParrilladaSrcSelectionPrivate *priv;

	priv = PARRILLADA_SRC_SELECTION_PRIVATE (object);

	if (priv->session) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	if (priv->track) {
		g_object_unref (priv->track);
		priv->track = NULL;
	}

	G_OBJECT_CLASS (parrillada_src_selection_parent_class)->finalize (object);
}

static ParrilladaTrack *
_get_session_disc_track (ParrilladaBurnSession *session)
{
	ParrilladaTrack *track;
	GSList *tracks;
	guint num;

	tracks = parrillada_burn_session_get_tracks (session);
	num = g_slist_length (tracks);

	if (num != 1)
		return NULL;

	track = tracks->data;
	if (PARRILLADA_IS_TRACK_DISC (track))
		return track;

	return NULL;
}

static void
parrillada_src_selection_set_property (GObject *object,
				    guint property_id,
				    const GValue *value,
				    GParamSpec *pspec)
{
	ParrilladaSrcSelectionPrivate *priv;
	ParrilladaBurnSession *session;

	priv = PARRILLADA_SRC_SELECTION_PRIVATE (object);

	switch (property_id) {
	case PROP_SESSION:
	{
		ParrilladaMedium *medium;
		ParrilladaDrive *drive;
		ParrilladaTrack *track;

		session = g_value_get_object (value);

		priv->session = session;
		g_object_ref (session);

		if (priv->track)
			g_object_unref (priv->track);

		/* See if there was a track set; if so then use it */
		track = _get_session_disc_track (session);
		if (track) {
			priv->track = PARRILLADA_TRACK_DISC (track);
			g_object_ref (track);
		}
		else {
			priv->track = parrillada_track_disc_new ();
			parrillada_burn_session_add_track (priv->session,
							PARRILLADA_TRACK (priv->track),
							NULL);
		}

		drive = parrillada_track_disc_get_drive (priv->track);
		medium = parrillada_drive_get_medium (drive);
		if (!medium) {
			/* No medium set use set session medium source as the
			 * one currently active in the selection widget */
			medium = parrillada_medium_selection_get_active (PARRILLADA_MEDIUM_SELECTION (object));
			parrillada_src_selection_medium_changed (PARRILLADA_MEDIUM_SELECTION (object), medium);
		}
		else
			parrillada_medium_selection_set_active (PARRILLADA_MEDIUM_SELECTION (object), medium);

		break;
	}

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
parrillada_src_selection_get_property (GObject *object,
				    guint property_id,
				    GValue *value,
				    GParamSpec *pspec)
{
	ParrilladaSrcSelectionPrivate *priv;

	priv = PARRILLADA_SRC_SELECTION_PRIVATE (object);

	switch (property_id) {
	case PROP_SESSION:
		g_value_set_object (value, priv->session);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
parrillada_src_selection_class_init (ParrilladaSrcSelectionClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	ParrilladaMediumSelectionClass *medium_selection_class = PARRILLADA_MEDIUM_SELECTION_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaSrcSelectionPrivate));

	object_class->finalize = parrillada_src_selection_finalize;
	object_class->set_property = parrillada_src_selection_set_property;
	object_class->get_property = parrillada_src_selection_get_property;
	object_class->constructed = parrillada_src_selection_constructed;

	medium_selection_class->medium_changed = parrillada_src_selection_medium_changed;

	g_object_class_install_property (object_class,
					 PROP_SESSION,
					 g_param_spec_object ("session",
							      "The session to work with",
							      "The session to work with",
							      PARRILLADA_TYPE_BURN_SESSION,
							      G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}
