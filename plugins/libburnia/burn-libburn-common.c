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
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include "burn-basics.h"
#include "burn-debug.h"
#include "burn-job.h"
#include "burn-libburn-common.h"

#include <libburn/libburn.h>

static void
parrillada_libburn_common_ctx_free_real (ParrilladaLibburnCtx *ctx)
{
	PARRILLADA_BURN_LOG ("Drive stopped");

	if (ctx->disc) {
		burn_disc_free (ctx->disc);
		ctx->disc = NULL;
	}

	/* This must be done in this order since:
	 * ctx->drive = ctx->drive_info->drive */

	if (ctx->drive) {
		burn_drive_release (ctx->drive, 0);
		ctx->drive = NULL;
	}

	if (ctx->drive_info) {
		burn_drive_info_free (ctx->drive_info);
		ctx->drive_info = NULL;
	}

	g_free (ctx);

	/* Since the library is not needed any more call burn_finish ().
	 * NOTE: it itself calls burn_abort (). */
	burn_finish ();
}

static gboolean
parrillada_libburn_common_ctx_wait_for_idle_drive (gpointer data)
{
	ParrilladaLibburnCtx *ctx = data;
	enum burn_drive_status status;

	/* try to properly cancel the drive */
	status = burn_drive_get_status (ctx->drive, NULL);
	if (status == BURN_DRIVE_WRITING || status == BURN_DRIVE_READING) {
		PARRILLADA_BURN_LOG ("Cancelling operation");
		burn_drive_cancel (ctx->drive);
	}

	if (status == BURN_DRIVE_GRABBING) {
		/* This should probably never happen */
		PARRILLADA_BURN_LOG ("Grabbing state, try to forget");
		burn_drive_info_forget (ctx->drive_info, 1);
	}

	if (status != BURN_DRIVE_IDLE) {
		PARRILLADA_BURN_LOG ("Drive not idle yet");
		return TRUE;
	}

	parrillada_libburn_common_ctx_free_real (ctx);
	return FALSE;
}

void
parrillada_libburn_common_ctx_free (ParrilladaLibburnCtx *ctx)
{
	enum burn_drive_status status;

	if (ctx->op_start) {
		g_timer_destroy (ctx->op_start);
		ctx->op_start = NULL;
	}

	PARRILLADA_BURN_LOG ("Stopping Drive");

	/* try to properly cancel the drive */
	status = burn_drive_get_status (ctx->drive, NULL);
	if (status == BURN_DRIVE_WRITING || status == BURN_DRIVE_READING) {
		PARRILLADA_BURN_LOG ("Cancelling operation");
		burn_drive_cancel (ctx->drive);
	}

	if (status == BURN_DRIVE_GRABBING) {
		/* This should probably never happen */
		PARRILLADA_BURN_LOG ("Grabbing state, try to forget");
		burn_drive_info_forget (ctx->drive_info, 1);
	}
	
	if (status != BURN_DRIVE_IDLE) {
		/* otherwise wait for the drive to calm down */
		PARRILLADA_BURN_LOG ("Drive not idle yet");
		g_timeout_add (200,
			       parrillada_libburn_common_ctx_wait_for_idle_drive,
			       ctx);
		return;
	}

	parrillada_libburn_common_ctx_free_real (ctx);
}

ParrilladaLibburnCtx *
parrillada_libburn_common_ctx_new (ParrilladaJob *job,
                                gboolean is_burning,
				GError **error)
{
	gchar libburn_device [BURN_DRIVE_ADR_LEN];
	ParrilladaLibburnCtx *ctx = NULL;
	gchar *device;
	int res;

	/* initialize the library */
	if (!burn_initialize ()) {
		g_set_error (error,
			     PARRILLADA_BURN_ERROR,
			     PARRILLADA_BURN_ERROR_GENERAL,
			     _("libburn library could not be initialized"));
		return NULL;
	}

	/* We want all types of messages but not them printed */
	burn_msgs_set_severities ("ALL", "NEVER", "");

	/* we just want to scan the drive proposed by drive */
	parrillada_job_get_device (job, &device);
	res = burn_drive_convert_fs_adr (device, libburn_device);
	g_free (device);
	if (res <= 0) {
		g_set_error (error,
			     PARRILLADA_BURN_ERROR,
			     PARRILLADA_BURN_ERROR_GENERAL,
			     _("The drive address could not be retrieved"));
		return NULL;
	}

	ctx = g_new0 (ParrilladaLibburnCtx, 1);
	ctx->is_burning = is_burning;
	res = burn_drive_scan_and_grab (&ctx->drive_info, libburn_device, 0);
	PARRILLADA_JOB_LOG (job, "Drive (%s) init result = %d", libburn_device, res);
	if (res <= 0) {
		g_free (ctx);
		g_set_error (error,
			     PARRILLADA_BURN_ERROR,
			     PARRILLADA_BURN_ERROR_DRIVE_BUSY,
			     _("The drive is busy"));
		return NULL;
	}

	ctx->drive = ctx->drive_info->drive;
	return ctx;	
}

static gboolean
parrillada_libburn_common_process_message (ParrilladaJob *self)
{
	int ret;
	GError *error;
	int err_code = 0;
	int err_errno = 0;
	char err_sev [80];
	char err_txt [BURN_MSGS_MESSAGE_LEN] = {0};

	/* Get all messages, indicating an error */
	memset (err_txt, 0, sizeof (err_txt));
	ret = burn_msgs_obtain ("ALL",
				&err_code,
				err_txt,
				&err_errno,
				err_sev);
	if (ret == 0)
		return TRUE;

	if (strcmp ("FATAL", err_sev)
	&&  strcmp ("ABORT", err_sev)) {
		/* libburn didn't reported any FATAL message but maybe it did
		 * report some debugging output */
		PARRILLADA_JOB_LOG (self, err_txt);
	        return TRUE;
	}

	PARRILLADA_JOB_LOG (self, "Libburn reported an error %s", err_txt);
	error = g_error_new (PARRILLADA_BURN_ERROR,
			     PARRILLADA_BURN_ERROR_GENERAL,
			     "%s",
			     err_txt);
	parrillada_job_error (PARRILLADA_JOB (self), error);
	return FALSE;
}

static gboolean
parrillada_libburn_common_status_changed (ParrilladaJob *self,
				       ParrilladaLibburnCtx *ctx,
				       enum burn_drive_status status,
				       struct burn_progress *progress)
{
	ParrilladaBurnAction action = PARRILLADA_BURN_ACTION_NONE;

	switch (status) {
		case BURN_DRIVE_WRITING:
			PARRILLADA_JOB_LOG (self, "Writing");
			/* we ignore it if it happens after leadout */
			if (ctx->status == BURN_DRIVE_WRITING_LEADOUT
			||  ctx->status == BURN_DRIVE_CLOSING_SESSION)
				return TRUE;

			if (!ctx->track_sectors) {
				/* This is for when we just start writing 
				 * the first bytes of the first tracks */
				ctx->track_sectors = progress->sectors;
				ctx->track_num = progress->track;
			}

			action = PARRILLADA_BURN_ACTION_RECORDING;
			parrillada_job_set_dangerous (PARRILLADA_JOB (self), TRUE);
			break;

		case BURN_DRIVE_WRITING_LEADIN:		/* DAO */
		case BURN_DRIVE_WRITING_PREGAP:		/* TAO */
			PARRILLADA_JOB_LOG (self, "Pregap/leadin");
			ctx->has_leadin = 1;
			action = PARRILLADA_BURN_ACTION_START_RECORDING;
			parrillada_job_set_dangerous (PARRILLADA_JOB (self), FALSE);
			break;

		case BURN_DRIVE_CLOSING_TRACK:		/* TAO */
		case BURN_DRIVE_WRITING_LEADOUT: 	/* DAO */
		case BURN_DRIVE_CLOSING_SESSION:	/* Multisession end */
			PARRILLADA_JOB_LOG (self, "Closing");
			ctx->sectors += ctx->track_sectors;
			ctx->track_sectors = progress->sectors;

			action = PARRILLADA_BURN_ACTION_FIXATING;
			parrillada_job_set_dangerous (PARRILLADA_JOB (self), FALSE);
			break;

		case BURN_DRIVE_ERASING:
		case BURN_DRIVE_FORMATTING:
			PARRILLADA_JOB_LOG (self, "Blanking/Formatting");
			if (!ctx->is_burning) {
				action = PARRILLADA_BURN_ACTION_BLANKING;
				parrillada_job_set_dangerous (PARRILLADA_JOB (self), TRUE);
			}
			else {
				/* DVD+RW need a preformatting before being written.
				 * Adapt the message to "start recording". */
				action = PARRILLADA_BURN_ACTION_START_RECORDING;
				parrillada_job_set_dangerous (PARRILLADA_JOB (self), FALSE);
			}
			break;

		case BURN_DRIVE_IDLE:
			/* That's the end of activity */
			return FALSE;

		case BURN_DRIVE_SPAWNING:
			PARRILLADA_JOB_LOG (self, "Starting");
			if (ctx->status == BURN_DRIVE_IDLE)
				action = PARRILLADA_BURN_ACTION_START_RECORDING;
			else
				action = PARRILLADA_BURN_ACTION_FIXATING;
			parrillada_job_set_dangerous (PARRILLADA_JOB (self), FALSE);
			break;

		case BURN_DRIVE_READING:
			PARRILLADA_JOB_LOG (self, "Reading");
			action = PARRILLADA_BURN_ACTION_DRIVE_COPY;
			parrillada_job_set_dangerous (PARRILLADA_JOB (self), FALSE);
			break;

		default:
			PARRILLADA_JOB_LOG (self, "Unknown drive state (%i)", status);
			return TRUE;
	}

	ctx->status = status;
	parrillada_job_set_current_action (self,
					action,
					NULL,
					FALSE);
	return TRUE;
}

ParrilladaBurnResult
parrillada_libburn_common_status (ParrilladaJob *self,
			       ParrilladaLibburnCtx *ctx)
{
	enum burn_drive_status status;
	struct burn_progress progress;

	/* see if there is any pending message */
	if (!parrillada_libburn_common_process_message (self))
		return PARRILLADA_BURN_ERR;

	if (!ctx->drive)
		return PARRILLADA_BURN_ERR;

	status = burn_drive_get_status (ctx->drive, &progress);

	/* For some operations that libburn can't perform
	 * the drive stays idle and we've got no way to tell
	 * that kind of use cases. For example, this 
	 * happens when fast blanking a blank DVD-RW */
	if (ctx->status == BURN_DRIVE_IDLE && status == BURN_DRIVE_IDLE) {
		PARRILLADA_JOB_LOG (self, "Waiting for operation to start");
		if (ctx->op_start == NULL) {
			/* wait for action for 2 seconds until we timeout */
			ctx->op_start = g_timer_new ();
			g_timer_start (ctx->op_start);
		}
		else {
			gdouble elapsed = 0.0;

			/* See how long elapsed since we started.
			 * NOTE: we do not consider this as an error. 
			 * since it can be because of an unneeded 
			 * operation like blanking on a blank disc. */
			elapsed = g_timer_elapsed (ctx->op_start, NULL);
			if (elapsed > 2.0)
				return PARRILLADA_BURN_OK;
		}
	}
	else if (ctx->op_start) {
		PARRILLADA_JOB_LOG (self, "Operation started");
		g_timer_destroy (ctx->op_start);
		ctx->op_start = NULL;
	}

	if (ctx->status != status) {
		gboolean running;

		running = parrillada_libburn_common_status_changed (self,
								 ctx,
								 status,
								 &progress);
		if (!running)
			return PARRILLADA_BURN_OK;
	}

	if (status == BURN_DRIVE_IDLE
	||  status == BURN_DRIVE_SPAWNING
	||  !progress.sectors
	||  !progress.sector) {
		ctx->sectors = 0;
		ctx->track_num = progress.track;
		ctx->track_sectors = progress.sectors;
		return PARRILLADA_BURN_RETRY;
	}

	if (ctx->status == BURN_DRIVE_WRITING) {
		gint64 cur_sector;

		if (ctx->track_num != progress.track) {
			/* This is when we change tracks */
			ctx->sectors += ctx->track_sectors;
			ctx->track_sectors = progress.sectors;
			ctx->track_num = progress.track;
		}

		cur_sector = progress.sector + ctx->sectors;

		/* With some media libburn writes only 16 blocks then wait
		 * which disrupt the whole process of time reporting */
		if (cur_sector > 32) {
			goffset total_sectors;

			parrillada_job_get_session_output_size (self, &total_sectors, NULL);

			/* Sometimes we have to wait for a long
			 * time while libburn sync the cache.
			 * Tell the use we haven't given up. */
			if (cur_sector < total_sectors) {
				gchar *string;

				parrillada_job_set_written_session (self, (gint64) ((gint64) cur_sector * 2048ULL));
				parrillada_job_start_progress (self, FALSE);

				string = g_strdup_printf (_("Writing track %02i"), progress.track + 1);
				parrillada_job_set_current_action (self,
								PARRILLADA_BURN_ACTION_RECORDING,
								string,
								TRUE);
				g_free (string);
			}
			else
				parrillada_job_set_current_action (self,
				                                PARRILLADA_BURN_ACTION_FIXATING,
								NULL,
								FALSE);
		}
		else
			     parrillada_job_set_current_action (self,
							PARRILLADA_BURN_ACTION_START_RECORDING,
							NULL,
							FALSE);
	}
	else if ((ctx->status == BURN_DRIVE_ERASING || ctx->status == BURN_DRIVE_FORMATTING)
	     &&  progress.sector > 0) {
		gdouble fraction;

		/* NOTE: there is a strange behaviour which
		 * leads to progress being reset after 30%
		 * approx when blanking seq DVD-RW */

		/* when erasing only set progress */
		fraction = (gdouble) (progress.sector) /
			   (gdouble) (progress.sectors);

		parrillada_job_set_progress (self, fraction);
		parrillada_job_start_progress (self, FALSE);
	}

	return PARRILLADA_BURN_RETRY;
}
