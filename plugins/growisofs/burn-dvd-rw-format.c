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

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include <gmodule.h>

#include "burn-basics.h"
#include "parrillada-plugin.h"
#include "parrillada-plugin-registration.h"
#include "burn-job.h"
#include "burn-process.h"
#include "parrillada-medium.h"
#include "burn-growisofs-common.h"


#define PARRILLADA_TYPE_DVD_RW_FORMAT         (parrillada_dvd_rw_format_get_type ())
#define PARRILLADA_DVD_RW_FORMAT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PARRILLADA_TYPE_DVD_RW_FORMAT, ParrilladaDvdRwFormat))
#define PARRILLADA_DVD_RW_FORMAT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PARRILLADA_TYPE_DVD_RW_FORMAT, ParrilladaDvdRwFormatClass))
#define PARRILLADA_IS_DVD_RW_FORMAT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PARRILLADA_TYPE_DVD_RW_FORMAT))
#define PARRILLADA_IS_DVD_RW_FORMAT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PARRILLADA_TYPE_DVD_RW_FORMAT))
#define PARRILLADA_DVD_RW_FORMAT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PARRILLADA_TYPE_DVD_RW_FORMAT, ParrilladaDvdRwFormatClass))

PARRILLADA_PLUGIN_BOILERPLATE (ParrilladaDvdRwFormat, parrillada_dvd_rw_format, PARRILLADA_TYPE_PROCESS, ParrilladaProcess);

static GObjectClass *parent_class = NULL;

static ParrilladaBurnResult
parrillada_dvd_rw_format_read_stderr (ParrilladaProcess *process, const gchar *line)
{
	int perc_1 = 0, perc_2 = 0;
	float percent;

	if (strstr (line, "unable to proceed with format")
	||  strstr (line, "media is not blank")
	||  strstr (line, "media is already formatted")
	||  strstr (line, "you have the option to re-run")) {
		parrillada_job_error (PARRILLADA_JOB (process),
				   g_error_new (PARRILLADA_BURN_ERROR,
						PARRILLADA_BURN_ERROR_MEDIUM_INVALID,
						_("The disc is not supported")));
		return PARRILLADA_BURN_OK;
	}
	else if (strstr (line, "unable to umount")) {
		parrillada_job_error (PARRILLADA_JOB (process),
				   g_error_new (PARRILLADA_BURN_ERROR,
						PARRILLADA_BURN_ERROR_DRIVE_BUSY,
						_("The drive is busy")));
		return PARRILLADA_BURN_OK;
	}

	if ((sscanf (line, "* blanking %d.%1d%%,", &perc_1, &perc_2) == 2)
	||  (sscanf (line, "* formatting %d.%1d%%,", &perc_1, &perc_2) == 2)
	||  (sscanf (line, "* relocating lead-out %d.%1d%%,", &perc_1, &perc_2) == 2))
		parrillada_job_set_dangerous (PARRILLADA_JOB (process), TRUE);
	else 
		sscanf (line, "%d.%1d%%", &perc_1, &perc_2);

	percent = (float) perc_1 / 100.0 + (float) perc_2 / 1000.0;
	if (percent) {
		parrillada_job_start_progress (PARRILLADA_JOB (process), FALSE);
		parrillada_job_set_progress (PARRILLADA_JOB (process), percent);
	}

	return PARRILLADA_BURN_OK;
}

static ParrilladaBurnResult
parrillada_dvd_rw_format_set_argv (ParrilladaProcess *process,
				GPtrArray *argv,
				GError **error)
{
	ParrilladaMedia media;
	ParrilladaBurnFlag flags;
	gchar *device;

	g_ptr_array_add (argv, g_strdup ("dvd+rw-format"));

	/* undocumented option to show progress */
	g_ptr_array_add (argv, g_strdup ("-gui"));

	parrillada_job_get_media (PARRILLADA_JOB (process), &media);
	parrillada_job_get_flags (PARRILLADA_JOB (process), &flags);
        if (!PARRILLADA_MEDIUM_IS (media, PARRILLADA_MEDIUM_BDRE)
	&&  !PARRILLADA_MEDIUM_IS (media, PARRILLADA_MEDIUM_DVDRW_PLUS)
	&&  !PARRILLADA_MEDIUM_IS (media, PARRILLADA_MEDIUM_DVDRW_RESTRICTED)
	&&  (flags & PARRILLADA_BURN_FLAG_FAST_BLANK)) {
		gchar *blank_str;

		/* This creates a sequential DVD-RW */
		blank_str = g_strdup_printf ("-blank%s",
					     (flags & PARRILLADA_BURN_FLAG_FAST_BLANK) ? "" : "=full");
		g_ptr_array_add (argv, blank_str);
	}
	else {
		gchar *format_str;

		/* This creates a restricted overwrite DVD-RW or reformat a + */
		format_str = g_strdup ("-force");
		g_ptr_array_add (argv, format_str);
	}

	parrillada_job_get_device (PARRILLADA_JOB (process), &device);
	g_ptr_array_add (argv, device);

	parrillada_job_set_current_action (PARRILLADA_JOB (process),
					PARRILLADA_BURN_ACTION_BLANKING,
					NULL,
					FALSE);
	return PARRILLADA_BURN_OK;
}

static void
parrillada_dvd_rw_format_class_init (ParrilladaDvdRwFormatClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ParrilladaProcessClass *process_class = PARRILLADA_PROCESS_CLASS (klass);

	parent_class = g_type_class_peek_parent(klass);
	object_class->finalize = parrillada_dvd_rw_format_finalize;

	process_class->set_argv = parrillada_dvd_rw_format_set_argv;
	process_class->stderr_func = parrillada_dvd_rw_format_read_stderr;
	process_class->post = parrillada_job_finished_session;
}

static void
parrillada_dvd_rw_format_init (ParrilladaDvdRwFormat *obj)
{ }

static void
parrillada_dvd_rw_format_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
parrillada_dvd_rw_format_export_caps (ParrilladaPlugin *plugin)
{
	/* NOTE: sequential and restricted are added later on demand */
	const ParrilladaMedia media = PARRILLADA_MEDIUM_DVD|
				   PARRILLADA_MEDIUM_DUAL_L|
				   PARRILLADA_MEDIUM_REWRITABLE|
				   PARRILLADA_MEDIUM_APPENDABLE|
				   PARRILLADA_MEDIUM_CLOSED|
				   PARRILLADA_MEDIUM_HAS_DATA|
				   PARRILLADA_MEDIUM_UNFORMATTED|
				   PARRILLADA_MEDIUM_BLANK;
	GSList *output;

	parrillada_plugin_define (plugin,
			       "dvd-rw-format",
	                       NULL,
			       _("Blanks and formats rewritable DVDs and BDs"),
			       "Philippe Rouquier",
			       4);

	output = parrillada_caps_disc_new (media|
					PARRILLADA_MEDIUM_BDRE|
					PARRILLADA_MEDIUM_PLUS|
					PARRILLADA_MEDIUM_RESTRICTED|
					PARRILLADA_MEDIUM_SEQUENTIAL);
	parrillada_plugin_blank_caps (plugin, output);
	g_slist_free (output);

	parrillada_plugin_set_blank_flags (plugin,
					media|
					PARRILLADA_MEDIUM_BDRE|
					PARRILLADA_MEDIUM_PLUS|
					PARRILLADA_MEDIUM_RESTRICTED,
					PARRILLADA_BURN_FLAG_NOGRACE,
					PARRILLADA_BURN_FLAG_NONE);
	parrillada_plugin_set_blank_flags (plugin,
					media|
					PARRILLADA_MEDIUM_SEQUENTIAL,
					PARRILLADA_BURN_FLAG_NOGRACE|
					PARRILLADA_BURN_FLAG_FAST_BLANK,
					PARRILLADA_BURN_FLAG_NONE);

	parrillada_plugin_register_group (plugin, _(GROWISOFS_DESCRIPTION));
}

G_MODULE_EXPORT void
parrillada_plugin_check_config (ParrilladaPlugin *plugin)
{
	gint version [3] = { 5, 0, -1};
	parrillada_plugin_test_app (plugin,
	                         "dvd+rw-format",
	                         "-v",
	                         "* BD/DVD±RW/-RAM format utility by <appro@fy.chalmers.se>, version %d.%d",
	                         version);
}
