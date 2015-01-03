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

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include "parrillada-track-image-cfg.h"
#include "parrillada-track-image.h"
#include "parrillada-track.h"

#include "burn-image-format.h"


typedef struct _ParrilladaTrackImageInfo ParrilladaTrackImageInfo;
struct _ParrilladaTrackImageInfo {
	gchar *uri;
	guint64 blocks;
	GCancellable *cancel;
	ParrilladaImageFormat format;
};

typedef struct _ParrilladaTrackImageCfgPrivate ParrilladaTrackImageCfgPrivate;
struct _ParrilladaTrackImageCfgPrivate
{
	GCancellable *cancel;
	GError *error;

	ParrilladaImageFormat format;
};

#define PARRILLADA_TRACK_IMAGE_CFG_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_TRACK_IMAGE_CFG, ParrilladaTrackImageCfgPrivate))

G_DEFINE_TYPE (ParrilladaTrackImageCfg, parrillada_track_image_cfg, PARRILLADA_TYPE_TRACK_IMAGE);


static void
parrillada_track_image_cfg_set_uri (ParrilladaTrackImageCfg *track,
				 const gchar *uri,
				 ParrilladaImageFormat format)
{
	switch (format) {
	case PARRILLADA_IMAGE_FORMAT_NONE:
	case PARRILLADA_IMAGE_FORMAT_BIN:
		PARRILLADA_TRACK_IMAGE_CLASS (parrillada_track_image_cfg_parent_class)->set_source (PARRILLADA_TRACK_IMAGE (track),
											      uri,
											      NULL,
											      format);
		break;
	case PARRILLADA_IMAGE_FORMAT_CLONE:
	case PARRILLADA_IMAGE_FORMAT_CUE:
	case PARRILLADA_IMAGE_FORMAT_CDRDAO:
		PARRILLADA_TRACK_IMAGE_CLASS (parrillada_track_image_cfg_parent_class)->set_source (PARRILLADA_TRACK_IMAGE (track),
											      NULL,
											      uri,
											      format);
		break;

	default:
		break;
	}
}

static void
parrillada_track_image_cfg_get_info_cb (GObject *object,
				     GAsyncResult *result,
				     gpointer user_data)
{
	ParrilladaTrackImageInfo *info;
	ParrilladaTrackImageCfgPrivate *priv;

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (object);

	info = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
	if (priv->cancel == info->cancel) {
		g_object_unref (priv->cancel);
		priv->cancel = NULL;
	}

	if (g_cancellable_is_cancelled (info->cancel)) {
		parrillada_track_changed (PARRILLADA_TRACK (object));
		return;
	}

	if (info->format == PARRILLADA_IMAGE_FORMAT_NONE
	||  info->blocks == 0) {
		GError *error = NULL;

		g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error);
		priv->error = error;

		parrillada_track_changed (PARRILLADA_TRACK (object));
		return;
	}

	parrillada_track_image_cfg_set_uri (PARRILLADA_TRACK_IMAGE_CFG (object),
	                                 info->uri,
	                                 priv->format != PARRILLADA_IMAGE_FORMAT_NONE? priv->format:info->format);

	PARRILLADA_TRACK_IMAGE_CLASS (parrillada_track_image_cfg_parent_class)->set_block_num (PARRILLADA_TRACK_IMAGE (object), info->blocks);
	parrillada_track_changed (PARRILLADA_TRACK (object));
}

static void
parrillada_track_image_cfg_get_info_thread (GSimpleAsyncResult *result,
					 GObject *object,
					 GCancellable *cancel)
{
	ParrilladaTrackImageInfo *info;
	GError *error = NULL;

	info = g_simple_async_result_get_op_res_gpointer (result);
	if (info->format == PARRILLADA_IMAGE_FORMAT_NONE) {
		GFile *file;
		const gchar *mime;
		GFileInfo *file_info;

		/* check if we do have a URI */
		file = g_file_new_for_commandline_arg (info->uri);
		file_info = g_file_query_info (file,
					       G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
					       G_FILE_QUERY_INFO_NONE,
					       cancel,
					       &error);
		g_object_unref (file);

		if (!file_info) {
			if (error && !g_cancellable_is_cancelled (cancel))
				g_simple_async_result_set_from_error (result, error);

			if (error)
				g_error_free (error);

			return;
		}

		mime = g_file_info_get_content_type (file_info);
		if (mime
		&& (!strcmp (mime, "application/x-toc")
		||  !strcmp (mime, "application/x-cdrdao-toc")
		||  !strcmp (mime, "application/x-cue"))) {
			info->format = parrillada_image_format_identify_cuesheet (info->uri, cancel, &error);

			if (error) {
				if (!g_cancellable_is_cancelled (cancel))
					g_simple_async_result_set_from_error (result, error);

				g_error_free (error);
				g_object_unref (file_info);
				return;
			}

			if (info->format == PARRILLADA_IMAGE_FORMAT_NONE
			&&  g_str_has_suffix (info->uri, ".toc"))
				info->format = PARRILLADA_IMAGE_FORMAT_CLONE;
		}
		else if (mime && !strcmp (mime, "application/octet-stream")) {
			/* that could be an image, so here is the deal:
			 * if we can find the type through the extension, fine.
			 * if not default to BIN */
			if (g_str_has_suffix (info->uri, ".bin"))
				info->format = PARRILLADA_IMAGE_FORMAT_CDRDAO;
			else if (g_str_has_suffix (info->uri, ".raw"))
				info->format = PARRILLADA_IMAGE_FORMAT_CLONE;
			else
				info->format = PARRILLADA_IMAGE_FORMAT_BIN;
		}
		else if (mime && !strcmp (mime, "application/x-cd-image"))
			info->format = PARRILLADA_IMAGE_FORMAT_BIN;

		g_object_unref (file_info);
	}

	if (info->format == PARRILLADA_IMAGE_FORMAT_NONE)
		return;

	if (info->format == PARRILLADA_IMAGE_FORMAT_BIN)
		parrillada_image_format_get_iso_size (info->uri, &info->blocks, NULL, cancel, &error);
	else if (info->format == PARRILLADA_IMAGE_FORMAT_CLONE) {
		gchar *complement;

		complement = parrillada_image_format_get_complement (PARRILLADA_IMAGE_FORMAT_CLONE, info->uri);
		parrillada_image_format_get_clone_size (complement, &info->blocks, NULL, cancel, &error);
	}
	else if (info->format == PARRILLADA_IMAGE_FORMAT_CDRDAO)
		parrillada_image_format_get_cdrdao_size (info->uri, &info->blocks, NULL, cancel, &error);
	else if (info->format == PARRILLADA_IMAGE_FORMAT_CUE)
		parrillada_image_format_get_cue_size (info->uri, &info->blocks, NULL, cancel, &error);

	if (error && !g_cancellable_is_cancelled (cancel))
		g_simple_async_result_set_from_error (result, error);

	if (error)
		g_error_free (error);
}

static void
parrillada_track_image_info_free (gpointer data)
{
	ParrilladaTrackImageInfo *info = data;

	g_object_unref (info->cancel);
	g_free (info->uri);
	g_free (info);
}

static void
parrillada_track_image_cfg_get_info (ParrilladaTrackImageCfg *track,
				  const gchar *uri)
{
	ParrilladaTrackImageCfgPrivate *priv;
	ParrilladaTrackImageInfo *info;
	GSimpleAsyncResult *res;

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (track);

	/* Cancel a possible ongoing thread */
	if (priv->cancel) {
		g_cancellable_cancel (priv->cancel);
		g_object_unref (priv->cancel);
		priv->cancel = NULL;
	}

	if (priv->error) {
		g_error_free (priv->error);
		priv->error = NULL;
	}

	res = g_simple_async_result_new (G_OBJECT (track),
					 parrillada_track_image_cfg_get_info_cb,
					 NULL,
					 parrillada_track_image_cfg_get_info);

	priv->cancel = g_cancellable_new ();

	info = g_new0 (ParrilladaTrackImageInfo, 1);
	info->uri = g_strdup (uri);
	info->format = priv->format;
	info->cancel = g_object_ref (priv->cancel);

	g_simple_async_result_set_op_res_gpointer (res, info, parrillada_track_image_info_free);
	g_simple_async_result_run_in_thread (res,
					     parrillada_track_image_cfg_get_info_thread,
					     G_PRIORITY_LOW,
					     priv->cancel);
	g_object_unref (res);
}

/**
 * parrillada_track_image_cfg_set_source:
 * @track: a #ParrilladaTrackImageCfg
 * @uri: a #gchar
 *
 * Sets the image uri or path (absolute or relative). @track will then identify its format and retrieve its size.
 *
 * Return value: a #ParrilladaBurnResult. PARRILLADA_BURN_OK if it is successful.
 **/

ParrilladaBurnResult
parrillada_track_image_cfg_set_source (ParrilladaTrackImageCfg *track,
				    const gchar *uri)
{
	GFile *file;
	gchar *uri_arg;
	gchar *current_uri;
	ParrilladaTrackImageCfgPrivate *priv;

	g_return_val_if_fail (PARRILLADA_IS_TRACK_IMAGE_CFG (track), PARRILLADA_BURN_NOT_SUPPORTED);
	g_return_val_if_fail (uri != NULL, PARRILLADA_BURN_NOT_SUPPORTED);

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (track);

	/* See if it has changed */
	file = g_file_new_for_commandline_arg (uri);
	uri_arg = g_file_get_uri (file);
	g_object_unref (file);

	if (!uri_arg)
		return PARRILLADA_BURN_ERR;

	current_uri = parrillada_track_image_get_source (PARRILLADA_TRACK_IMAGE (track), TRUE);
	if (current_uri && !strcmp (current_uri, uri_arg)) {
		g_free (current_uri);
		g_free (uri_arg);
		return PARRILLADA_BURN_OK;
	}
	g_free (current_uri);

	/* Do it before to update our status first then update track info */
	parrillada_track_image_cfg_get_info (track, uri_arg);

	/* Update the image info container values. If it was invalid then */
	/* NOTE: this resets the size as well */
	PARRILLADA_TRACK_IMAGE_CLASS (parrillada_track_image_cfg_parent_class)->set_block_num (PARRILLADA_TRACK_IMAGE (track), 0);
	parrillada_track_image_cfg_set_uri (track, uri_arg, priv->format);
	parrillada_track_changed (PARRILLADA_TRACK (track));

	g_free (uri_arg);

	return PARRILLADA_BURN_OK;
}

/**
 * parrillada_track_image_cfg_get_forced_format:
 * @track: a #ParrilladaTrackImageCfg
 *
 * This function returns the #ParrilladaImageFormat that was set for the image.
 * See parrillada_track_image_cfg_force_format ().
 *
 * Return value: a #ParrilladaBurnResult. PARRILLADA_BURN_OK if it is successful.
 **/

ParrilladaImageFormat
parrillada_track_image_cfg_get_forced_format (ParrilladaTrackImageCfg *track)
{
	ParrilladaTrackImageCfgPrivate *priv;

	g_return_val_if_fail (PARRILLADA_IS_TRACK_IMAGE_CFG (track), PARRILLADA_IMAGE_FORMAT_NONE);

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (track);
	return priv->format;
}

/**
 * parrillada_track_image_cfg_force_format:
 * @track: a #ParrilladaTrackImageCfg
 * @format: a #ParrilladaImageFormat
 *
 * This function allows to prevents the identification of the format of the image.
 * It does not cancel size retrieval.
 * If @format is PARRILLADA_IMAGE_FORMAT_NONE then the format of the image
 * will be retrieved.
 *
 * Return value: a #ParrilladaBurnResult. PARRILLADA_BURN_OK if it is successful.
 **/

ParrilladaBurnResult
parrillada_track_image_cfg_force_format (ParrilladaTrackImageCfg *track,
				      ParrilladaImageFormat format)
{
	ParrilladaTrackImageCfgPrivate *priv;
	ParrilladaImageFormat current_format;
	gchar *uri = NULL;

	g_return_val_if_fail (PARRILLADA_IS_TRACK_IMAGE_CFG (track), PARRILLADA_BURN_NOT_SUPPORTED);

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (track);

	current_format = parrillada_track_image_get_format (PARRILLADA_TRACK_IMAGE (track));
	if (format != PARRILLADA_IMAGE_FORMAT_NONE) {
		if (current_format == format)
			return PARRILLADA_BURN_OK;
	}
	else if (format == priv->format)
		return PARRILLADA_BURN_OK;

	priv->format = format;

	switch (current_format) {
	case PARRILLADA_IMAGE_FORMAT_NONE:
	case PARRILLADA_IMAGE_FORMAT_BIN:
		uri = parrillada_track_image_get_source (PARRILLADA_TRACK_IMAGE (track), TRUE);
		break;
	case PARRILLADA_IMAGE_FORMAT_CLONE:
	case PARRILLADA_IMAGE_FORMAT_CUE:
	case PARRILLADA_IMAGE_FORMAT_CDRDAO:
		uri = parrillada_track_image_get_toc_source (PARRILLADA_TRACK_IMAGE (track), TRUE);
		break;

	default:
		break;
	}

	if (!uri)
		return PARRILLADA_BURN_NOT_READY;

	/* Do it before to update our status first then update track info */
	parrillada_track_image_cfg_get_info (track, uri);

	uri = parrillada_track_image_get_source (PARRILLADA_TRACK_IMAGE (track), TRUE);
	parrillada_track_image_cfg_set_uri (track, uri, priv->format);
	g_free (uri);

	return PARRILLADA_BURN_OK;
}

static ParrilladaBurnResult
parrillada_track_image_cfg_get_status (ParrilladaTrack *track,
				    ParrilladaStatus *status)
{
	ParrilladaTrackImageCfgPrivate *priv;

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (track);

	if (priv->cancel) {
		if (status)
			parrillada_status_set_not_ready (status, -1.0, _("Retrieving image format and size"));

		return PARRILLADA_BURN_NOT_READY;
	}

	if (priv->error) {
		if (status)
			parrillada_status_set_error (status, g_error_copy (priv->error));

		return PARRILLADA_BURN_ERR;
	}

	/* See if we managed to set a format (all went well then) */
	if (parrillada_track_image_get_format (PARRILLADA_TRACK_IMAGE (track)) == PARRILLADA_IMAGE_FORMAT_NONE) {
		if (status)
			parrillada_status_set_error (status,
						  g_error_new (PARRILLADA_BURN_ERROR,
							       PARRILLADA_BURN_ERROR_GENERAL,
							       "%s.\n%s",
							       /* Translators: This is a disc image */
							       _("The format of the disc image could not be identified"),
							       _("Please set it manually")));

		return PARRILLADA_BURN_ERR;
	}

	if (status)
		parrillada_status_set_completed (status);

	return PARRILLADA_BURN_OK;
}

static void
parrillada_track_image_cfg_init (ParrilladaTrackImageCfg *object)
{ }

static void
parrillada_track_image_cfg_finalize (GObject *object)
{
	ParrilladaTrackImageCfgPrivate *priv;

	priv = PARRILLADA_TRACK_IMAGE_CFG_PRIVATE (object);

	if (priv->cancel) {
		g_cancellable_cancel (priv->cancel);
		g_object_unref (priv->cancel);
		priv->cancel = NULL;
	}

	if (priv->error) {
		g_error_free (priv->error);
		priv->error = NULL;
	}

	G_OBJECT_CLASS (parrillada_track_image_cfg_parent_class)->finalize (object);
}

static void
parrillada_track_image_cfg_class_init (ParrilladaTrackImageCfgClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	ParrilladaTrackClass *track_class = PARRILLADA_TRACK_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaTrackImageCfgPrivate));

	object_class->finalize = parrillada_track_image_cfg_finalize;

	track_class->get_status = parrillada_track_image_cfg_get_status;
}

/**
 * parrillada_track_image_cfg_new:
 *
 *  Creates a new #ParrilladaTrackImageCfg object.
 *
 * Return value: a #ParrilladaTrackImageCfg object.
 **/

ParrilladaTrackImageCfg *
parrillada_track_image_cfg_new (void)
{
	return g_object_new (PARRILLADA_TYPE_TRACK_IMAGE_CFG, NULL);
}
