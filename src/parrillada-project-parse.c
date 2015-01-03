/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/***************************************************************************
 *            parrillada-project-parse.c
 *
 *  dim nov 27 14:58:13 2008
 *  Copyright  2005-2008  Rouquier Philippe
 *  brasero-app@wanadoo.fr
 ***************************************************************************/

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

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <libxml/xmlerror.h>
#include <libxml/xmlwriter.h>
#include <libxml/parser.h>
#include <libxml/xmlstring.h>
#include <libxml/uri.h>

#ifdef BUILD_PLAYLIST
#include <totem-pl-parser.h>
#endif

#include "parrillada-project-parse.h"
#include "parrillada-app.h"

#include "parrillada-units.h"
#include "parrillada-track-stream-cfg.h"
#include "parrillada-track-data-cfg.h"
#include "parrillada-session.h"
#include "parrillada-tags.h"

#define PARRILLADA_PROJECT_VERSION "0.2"

static void
parrillada_project_invalid_project_dialog (const char *reason)
{
	parrillada_app_alert (parrillada_app_get_default (),
			   _("Error while loading the project."),
			   reason,
			   GTK_MESSAGE_ERROR);
}

static GSList *
_read_graft_point (xmlDocPtr project,
		   xmlNodePtr graft,
		   GSList *grafts)
{
	ParrilladaGraftPt *retval;

	retval = g_new0 (ParrilladaGraftPt, 1);
        grafts = g_slist_prepend (grafts, retval);
	while (graft) {
		if (!xmlStrcmp (graft->name, (const xmlChar *) "uri")) {
			xmlChar *uri;

			if (retval->uri)
				goto error;

			uri = xmlNodeListGetString (project,
						    graft->xmlChildrenNode,
						    1);
			retval->uri = g_uri_unescape_string ((char *)uri, NULL);
			g_free (uri);
			if (!retval->uri)
				goto error;
		}
		else if (!xmlStrcmp (graft->name, (const xmlChar *) "path")) {
			if (retval->path)
				goto error;

			retval->path = (char *) xmlNodeListGetString (project,
								      graft->xmlChildrenNode,
								      1);
			if (!retval->path)
				goto error;
		}
		else if (graft->type == XML_ELEMENT_NODE)
			goto error;

		graft = graft->next;
	}

	return grafts;

error:

        g_slist_foreach (grafts, (GFunc) parrillada_graft_point_free, NULL);
        g_slist_free (grafts);

	return NULL;
}

static ParrilladaTrack *
_read_data_track (xmlDocPtr project,
		  xmlNodePtr item)
{
	ParrilladaTrackDataCfg *track;
        GSList *grafts= NULL;
        GSList *excluded = NULL;

	track = parrillada_track_data_cfg_new ();

	while (item) {
		if (!xmlStrcmp (item->name, (const xmlChar *) "graft")) {
			if (!(grafts = _read_graft_point (project, item->xmlChildrenNode, grafts)))
				goto error;
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "icon")) {
			xmlChar *icon_path;

			icon_path = xmlNodeListGetString (project,
							  item->xmlChildrenNode,
							  1);
			if (!icon_path)
				goto error;

			parrillada_track_data_cfg_set_icon (track, (gchar *) icon_path, NULL);
                        g_free (icon_path);
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "restored")) {
			xmlChar *restored;

			restored = xmlNodeListGetString (project,
							 item->xmlChildrenNode,
							 1);
			if (!restored)
				goto error;

                        parrillada_track_data_cfg_dont_filter_uri (track, (gchar *) restored);
                        g_free (restored);
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "excluded")) {
			xmlChar *excluded_uri;

			excluded_uri = xmlNodeListGetString (project,
							     item->xmlChildrenNode,
							     1);
			if (!excluded_uri)
				goto error;

			excluded = g_slist_prepend (excluded, xmlURIUnescapeString ((char*) excluded_uri, 0, NULL));
			g_free (excluded_uri);
		}
		else if (item->type == XML_ELEMENT_NODE)
			goto error;

		item = item->next;
	}

        grafts = g_slist_reverse (grafts);
        excluded = g_slist_reverse (excluded);
        parrillada_track_data_set_source (PARRILLADA_TRACK_DATA (track),
                                                           grafts,
                                                           excluded);
	return PARRILLADA_TRACK (track);

error:

        g_slist_foreach (grafts, (GFunc) parrillada_graft_point_free, NULL);
        g_slist_free (grafts);

        g_slist_foreach (excluded, (GFunc) g_free, NULL);
        g_slist_free (excluded);

	g_object_unref (track);

	return NULL;
}

static ParrilladaTrack *
_read_audio_track (xmlDocPtr project,
		   xmlNodePtr uris,
                   gboolean is_video)
{
	ParrilladaTrackStreamCfg *track;

	track = parrillada_track_stream_cfg_new ();

	while (uris) {
		if (!xmlStrcmp (uris->name, (const xmlChar *) "uri")) {
			xmlChar *uri;
                        gchar *unescaped_uri;

			uri = xmlNodeListGetString (project,
						    uris->xmlChildrenNode,
						    1);
			if (!uri)
				goto error;

                        unescaped_uri = g_uri_unescape_string ((char *) uri, NULL);
                        g_free (uri);

			/* Note: this must come before parrillada_track_stream_set_boundaries ()
			 * or we will reset the end point to 0 */
			parrillada_track_stream_set_source (PARRILLADA_TRACK_STREAM (track), unescaped_uri);

			/* For the moment pretend it is a video file. Since it is ParrilladaTrackStreamCfg, that
			 * will be set properly afterwards. */
			if (is_video)
				parrillada_track_stream_set_format (PARRILLADA_TRACK_STREAM (track),
				                                 PARRILLADA_VIDEO_FORMAT_UNDEFINED);

                        g_free (unescaped_uri);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "silence")) {
			gchar *silence;

			/* impossible to have two gaps in a row */
			if (parrillada_track_stream_get_gap (PARRILLADA_TRACK_STREAM (track)) > 0)
				goto error;

			silence = (gchar *) xmlNodeListGetString (project,
								  uris->xmlChildrenNode,
								  1);
			if (!silence)
				goto error;

                        parrillada_track_stream_set_boundaries (PARRILLADA_TRACK_STREAM (track),
                                                             -1,
                                                             -1,
                                                             g_ascii_strtoull (silence, NULL, 10));
			g_free (silence);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "start")) {
			gchar *start;

			start = (gchar *) xmlNodeListGetString (project,
								uris->xmlChildrenNode,
								1);
			if (!start)
				goto error;

                        parrillada_track_stream_set_boundaries (PARRILLADA_TRACK_STREAM (track),
                                                             g_ascii_strtoull (start, NULL, 10),
                                                             -1,
                                                             -1);
			g_free (start);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "end")) {
			gchar *end;

			end = (gchar *) xmlNodeListGetString (project,
							      uris->xmlChildrenNode,
							      1);
			if (!end)
				goto error;

                        parrillada_track_stream_set_boundaries (PARRILLADA_TRACK_STREAM (track),
                                                             -1,
                                                             g_ascii_strtoull (end, NULL, 10),
                                                             -1);
			g_free (end);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "title")) {
			xmlChar *title;
			gchar *unescaped_title;

			title = xmlNodeListGetString (project,
						      uris->xmlChildrenNode,
						      1);
			if (!title)
				goto error;

                        unescaped_title = g_uri_unescape_string ((char *) title, NULL);
                        g_free (title);

                        parrillada_track_tag_add_string (PARRILLADA_TRACK (track),
                                                      PARRILLADA_TRACK_STREAM_TITLE_TAG,
                                                      unescaped_title);
        		g_free (unescaped_title);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "artist")) {
			xmlChar *artist;
                        gchar *unescaped_artist;

			artist = xmlNodeListGetString (project,
						      uris->xmlChildrenNode,
						      1);
			if (!artist)
				goto error;

			unescaped_artist = g_uri_unescape_string ((char *) artist, NULL);
			g_free (artist);

                        parrillada_track_tag_add_string (PARRILLADA_TRACK (track),
                                                      PARRILLADA_TRACK_STREAM_ARTIST_TAG,
                                                      unescaped_artist);
        		g_free (unescaped_artist);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "composer")) {
			xmlChar *composer;
                        gchar *unescaped_composer;

			composer = xmlNodeListGetString (project,
							 uris->xmlChildrenNode,
							 1);
			if (!composer)
				goto error;

			unescaped_composer = g_uri_unescape_string ((char *) composer, NULL);
			g_free (composer);

                        parrillada_track_tag_add_string (PARRILLADA_TRACK (track),
                                                      PARRILLADA_TRACK_STREAM_COMPOSER_TAG,
                                                      unescaped_composer);
        		g_free (unescaped_composer);
		}
		else if (!xmlStrcmp (uris->name, (const xmlChar *) "isrc")) {
			gchar *isrc;

			isrc = (gchar *) xmlNodeListGetString (project,
							       uris->xmlChildrenNode,
							       1);
			if (!isrc)
				goto error;

                        parrillada_track_tag_add_int (PARRILLADA_TRACK (track),
                                                   PARRILLADA_TRACK_STREAM_ISRC_TAG,
                                                   (gint) g_ascii_strtod (isrc, NULL));
			g_free (isrc);
		}
		else if (uris->type == XML_ELEMENT_NODE)
			goto error;

		uris = uris->next;
	}

	return PARRILLADA_TRACK (track);

error:

	g_object_unref (track);

	return NULL;
}

static gboolean
_get_tracks (xmlDocPtr project,
	     xmlNodePtr track_node,
	     ParrilladaBurnSession *session)
{
	GSList *tracks = NULL;
	GSList *iter;

	track_node = track_node->xmlChildrenNode;

	while (track_node) {
		ParrilladaTrack *newtrack;

		if (!xmlStrcmp (track_node->name, (const xmlChar *) "audio")) {
			newtrack = _read_audio_track (project, track_node->xmlChildrenNode, FALSE);
			if (!newtrack)
				goto error;

			tracks = g_slist_append (tracks, newtrack);
		}
		else if (!xmlStrcmp (track_node->name, (const xmlChar *) "data")) {
			newtrack = _read_data_track (project, track_node->xmlChildrenNode);

			if (!newtrack)
				goto error;

			tracks = g_slist_append (tracks, newtrack);
		}
		else if (!xmlStrcmp (track_node->name, (const xmlChar *) "video")) {
			newtrack = _read_audio_track (project, track_node->xmlChildrenNode, TRUE);

			if (!newtrack)
				goto error;

			tracks = g_slist_append (tracks, newtrack);
		}
		else if (track_node->type == XML_ELEMENT_NODE)
			goto error;

		track_node = track_node->next;
	}

	if (!tracks)
		goto error;

	for (iter = tracks; iter; iter = iter->next) {
		ParrilladaTrack *newtrack;

		newtrack = iter->data;
		parrillada_burn_session_add_track (session, newtrack, NULL);
		g_object_unref (newtrack);
	}

	g_slist_free (tracks);

	return TRUE;

error :

	if (tracks) {
		g_slist_foreach (tracks, (GFunc) g_object_unref, NULL);
		g_slist_free (tracks);
	}

	return FALSE;
}

gboolean
parrillada_project_open_project_xml (const gchar *uri,
				  ParrilladaBurnSession *session,
				  gboolean warn_user)
{
	xmlNodePtr track_node = NULL;
	gchar *label = NULL;
	gchar *cover = NULL;
	xmlDocPtr project;
	xmlNodePtr item;
	gboolean retval;
	GFile *file;
	gchar *path;

	file = g_file_new_for_commandline_arg (uri);
	path = g_file_get_path (file);
	g_object_unref (file);
	if (!path)
		return FALSE;

	/* start parsing xml doc */
	project = xmlParseFile (path);
    	g_free (path);

	if (!project) {
	    	if (warn_user)
			parrillada_project_invalid_project_dialog (_("The project could not be opened"));

		return FALSE;
	}

	/* parses the "header" */
	item = xmlDocGetRootElement (project);
	if (!item) {
	    	if (warn_user)
			parrillada_project_invalid_project_dialog (_("The file is empty"));

		xmlFreeDoc (project);
		return FALSE;
	}

	if (xmlStrcmp (item->name, (const xmlChar *) "parrilladaproject")
	||  item->next)
		goto error;

	item = item->children;
	while (item) {
		if (!xmlStrcmp (item->name, (const xmlChar *) "version")) {
			/* simply ignore it */
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "label")) {
			label = (gchar *) xmlNodeListGetString (project,
								item->xmlChildrenNode,
								1);
			if (!(label))
				goto error;
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "cover")) {
			xmlChar *escaped;

			escaped = xmlNodeListGetString (project,
							item->xmlChildrenNode,
							1);
			if (!escaped)
				goto error;

			cover = g_uri_unescape_string ((char *) escaped, NULL);
			g_free (escaped);
		}
		else if (!xmlStrcmp (item->name, (const xmlChar *) "track")) {
			if (track_node)
				goto error;

			track_node = item;
		}
		else if (item->type == XML_ELEMENT_NODE)
			goto error;

		item = item->next;
	}

	retval = _get_tracks (project, track_node, session);
	if (!retval)
		goto error;

	xmlFreeDoc (project);

        parrillada_burn_session_set_label (session, label);
        g_free (label);

        if (cover) {
                GValue *value;

                value = g_new0 (GValue, 1);
                g_value_init (value, G_TYPE_STRING);
                g_value_set_string (value, cover);
                parrillada_burn_session_tag_add (session,
                                               PARRILLADA_COVER_URI,
                                               value);

                g_free (cover);
        }

        return retval;

error:

	if (cover)
		g_free (cover);
	if (label)
		g_free (label);

	xmlFreeDoc (project);
    	if (warn_user)
		parrillada_project_invalid_project_dialog (_("It does not seem to be a valid Parrillada project"));

	return FALSE;
}

#ifdef BUILD_PLAYLIST

static void
parrillada_project_playlist_playlist_started (TotemPlParser *parser,
					   const gchar *uri,
					   GHashTable *metadata,
					   gpointer user_data)
{
        ParrilladaBurnSession *session = user_data;

        parrillada_burn_session_set_label (session, g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_TITLE));
}

static void
parrillada_project_playlist_entry_parsed (TotemPlParser *parser,
				       const gchar *uri,
				       GHashTable *metadata,
				       gpointer user_data)
{
	ParrilladaBurnSession *session = user_data;
        ParrilladaTrackStreamCfg *track;

        track = parrillada_track_stream_cfg_new ();
        parrillada_track_stream_set_source (PARRILLADA_TRACK_STREAM (track), uri);
        parrillada_burn_session_add_track (session, PARRILLADA_TRACK (track), NULL);
}

gboolean
parrillada_project_open_audio_playlist_project (const gchar *uri,
					     ParrilladaBurnSession *session,
					     gboolean warn_user)
{
	TotemPlParser *parser;
	TotemPlParserResult result;
	GFile *file;
	char *_uri;

	file = g_file_new_for_commandline_arg (uri);
	_uri = g_file_get_uri (file);
	g_object_unref (file);

	parser = totem_pl_parser_new ();
	g_object_set (parser,
		      "recurse", FALSE,
		      "disable-unsafe", TRUE,
		      NULL);

	g_signal_connect (parser,
			  "playlist-started",
			  G_CALLBACK (parrillada_project_playlist_playlist_started),
			  session);

	g_signal_connect (parser,
			  "entry-parsed",
			  G_CALLBACK (parrillada_project_playlist_entry_parsed),
			  session);

	result = totem_pl_parser_parse (parser, _uri, FALSE);
	if (result != TOTEM_PL_PARSER_RESULT_SUCCESS) {
		if (warn_user)
			parrillada_project_invalid_project_dialog (_("It does not seem to be a valid Parrillada project"));
	}

	g_free (_uri);
	g_object_unref (parser);

	return (result == TOTEM_PL_PARSER_RESULT_SUCCESS);
}

#endif

/**
 * Project saving
 */

static gboolean
_save_audio_track_xml (xmlTextWriter *project,
		       ParrilladaTrackStream *track)
{
	xmlChar *escaped;
	gchar *start;
	gint success;
	gchar *isrc;
	gchar *uri;
	gchar *end;

	uri = parrillada_track_stream_get_source (track, TRUE);
	escaped = (unsigned char *) g_uri_escape_string (uri, NULL, FALSE);
	g_free (uri);

	success = xmlTextWriterWriteElement (project,
					    (xmlChar *) "uri",
					     escaped);
	g_free (escaped);

	if (success == -1)
		return FALSE;

	if (parrillada_track_stream_get_gap (track) > 0) {
		gchar *silence;

		silence = g_strdup_printf ("%"G_GINT64_FORMAT, parrillada_track_stream_get_gap (track));
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "silence",
						     (xmlChar *) silence);

		g_free (silence);
		if (success == -1)
			return FALSE;
	}

	if (parrillada_track_stream_get_end (track) > 0) {
		/* start of the song */
		start = g_strdup_printf ("%"G_GINT64_FORMAT, parrillada_track_stream_get_start (track));
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "start",
						     (xmlChar *) start);

		g_free (start);
		if (success == -1)
			return FALSE;

		/* end of the song */
		end = g_strdup_printf ("%"G_GINT64_FORMAT, parrillada_track_stream_get_end (track));
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "end",
						     (xmlChar *) end);

		g_free (end);
		if (success == -1)
			return FALSE;
	}

	if (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_TITLE_TAG)) {
		escaped = (unsigned char *) g_uri_escape_string (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_TITLE_TAG), NULL, FALSE);
		success = xmlTextWriterWriteElement (project,
						    (xmlChar *) "title",
						     escaped);
		g_free (escaped);

		if (success == -1)
			return FALSE;
	}

	if (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_ARTIST_TAG)) {
		escaped = (unsigned char *) g_uri_escape_string (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_ARTIST_TAG), NULL, FALSE);
		success = xmlTextWriterWriteElement (project,
						    (xmlChar *) "artist",
						     escaped);
		g_free (escaped);

		if (success == -1)
			return FALSE;
	}

	if (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_COMPOSER_TAG)) {
		escaped = (unsigned char *) g_uri_escape_string (parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_COMPOSER_TAG), NULL, FALSE);
		success = xmlTextWriterWriteElement (project,
						    (xmlChar *) "composer",
						     escaped);
		g_free (escaped);
		if (success == -1)
			return FALSE;
	}

	if (parrillada_track_tag_lookup_int (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_ISRC_TAG)) {
		isrc = g_strdup_printf ("%d", parrillada_track_tag_lookup_int (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_ISRC_TAG));
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "isrc",
						     (xmlChar *) isrc);

		g_free (isrc);
		if (success == -1)
			return FALSE;
	}

	return TRUE;
}

static gboolean
_save_data_track_xml (xmlTextWriter *project,
		      ParrilladaBurnSession *session)
{
	gchar *uri;
	gint success;
	GSList *iter;
	GSList *tracks;
	GSList *grafts;
	gchar *filename;
	ParrilladaTrackDataCfg *track;

	tracks = parrillada_burn_session_get_tracks (session);
	track = PARRILLADA_TRACK_DATA_CFG (tracks->data);

	filename = parrillada_track_data_cfg_get_icon_path (track);
	if (filename) {
		/* Write the icon if any */
		success = xmlTextWriterWriteElement (project, (xmlChar *) "icon", (xmlChar *) filename);
		g_free (filename);
		if (success < 0)
			return FALSE;
	}

	grafts = parrillada_track_data_get_grafts (PARRILLADA_TRACK_DATA (track));
	for (; grafts; grafts = grafts->next) {
		ParrilladaGraftPt *graft;

		graft = grafts->data;

		success = xmlTextWriterStartElement (project, (xmlChar *) "graft");
		if (success < 0)
			return FALSE;

		success = xmlTextWriterWriteElement (project, (xmlChar *) "path", (xmlChar *) graft->path);
		if (success < 0)
			return FALSE;

		if (graft->uri) {
			xmlChar *escaped;

			escaped = (unsigned char *) g_uri_escape_string (graft->uri, NULL, FALSE);
			success = xmlTextWriterWriteElement (project, (xmlChar *) "uri", escaped);
			g_free (escaped);
			if (success < 0)
				return FALSE;
		}

		success = xmlTextWriterEndElement (project); /* graft */
		if (success < 0)
			return FALSE;
	}

	/* save excluded uris */
	iter = parrillada_track_data_get_excluded_list (PARRILLADA_TRACK_DATA (track));
	for (; iter; iter = iter->next) {
		xmlChar *escaped;

		escaped = xmlURIEscapeStr ((xmlChar *) iter->data, NULL);
		success = xmlTextWriterWriteElement (project, (xmlChar *) "excluded", (xmlChar *) escaped);
		g_free (escaped);
		if (success < 0)
			return FALSE;
	}

	/* save restored uris */
	iter = parrillada_track_data_cfg_get_restored_list (track);
	for (; iter; iter = iter->next) {
		uri = iter->data;
		success = xmlTextWriterWriteElement (project, (xmlChar *) "restored", (xmlChar *) uri);
		if (success < 0)
			return FALSE;
	}

	/* NOTE: we don't write symlinks and unreadable they are useless */
	return TRUE;
}

gboolean 
parrillada_project_save_project_xml (ParrilladaBurnSession *session,
				  const gchar *uri)
{
	ParrilladaTrackType *track_type = NULL;
	xmlTextWriter *project;
	gboolean retval;
	GSList *tracks;
	GValue *value;
	gint success;
	gchar *path;

	path = g_filename_from_uri (uri, NULL, NULL);
	if (!path)
		return FALSE;

	project = xmlNewTextWriterFilename (path, 0);
	if (!project) {
		g_free (path);
		return FALSE;
	}

	xmlTextWriterSetIndent (project, 1);
	xmlTextWriterSetIndentString (project, (xmlChar *) "\t");

	success = xmlTextWriterStartDocument (project,
					      NULL,
					      "UTF-8",
					      NULL);
	if (success < 0)
		goto error;

	success = xmlTextWriterStartElement (project, (xmlChar *) "parrilladaproject");
	if (success < 0)
		goto error;

	/* write the name of the version */
	success = xmlTextWriterWriteElement (project,
					     (xmlChar *) "version",
					     (xmlChar *) PARRILLADA_PROJECT_VERSION);
	if (success < 0)
		goto error;

	if (parrillada_burn_session_get_label (session)) {
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "label",
						     (xmlChar *) parrillada_burn_session_get_label (session));

		if (success < 0)
			goto error;
	}

	value = NULL;
	parrillada_burn_session_tag_lookup (session,
					 PARRILLADA_COVER_URI,
					 &value);
	if (value) {
		gchar *escaped;

		escaped = g_uri_escape_string (g_value_get_string (value), NULL, FALSE);
		success = xmlTextWriterWriteElement (project,
						     (xmlChar *) "cover",
						     (xmlChar *) escaped);
		g_free (escaped);

		if (success < 0)
			goto error;
	}

	success = xmlTextWriterStartElement (project, (xmlChar *) "track");
	if (success < 0)
		goto error;

	track_type = parrillada_track_type_new ();
	tracks = parrillada_burn_session_get_tracks (session);

	for (; tracks; tracks = tracks->next) {
		ParrilladaTrack *track;

		track = tracks->data;

		parrillada_track_get_track_type (track, track_type);
		if (parrillada_track_type_get_has_stream (track_type)) {
			if (PARRILLADA_STREAM_FORMAT_HAS_VIDEO (parrillada_track_type_get_stream_format (track_type)))
				success = xmlTextWriterStartElement (project, (xmlChar *) "video");
			else
				success = xmlTextWriterStartElement (project, (xmlChar *) "audio");

			if (success < 0)
				goto error;

			retval = _save_audio_track_xml (project, PARRILLADA_TRACK_STREAM (track));
			if (!retval)
				goto error;

			success = xmlTextWriterEndElement (project); /* audio/video */
			if (success < 0)
				goto error;
		}
		else if (parrillada_track_type_get_has_data (track_type)) {
			success = xmlTextWriterStartElement (project, (xmlChar *) "data");
			if (success < 0)
				goto error;

			retval = _save_data_track_xml (project, session);
			if (!retval)
				goto error;

			success = xmlTextWriterEndElement (project); /* data */
			if (success < 0)
				goto error;
		}
		else
			retval = FALSE;
	}

	success = xmlTextWriterEndElement (project); /* track */
	if (success < 0)
		goto error;

	parrillada_track_type_free (track_type);

	success = xmlTextWriterEndElement (project); /* parrilladaproject */
	if (success < 0)
		goto error;

	xmlTextWriterEndDocument (project);
	xmlFreeTextWriter (project);
	g_free (path);
	return TRUE;

error:

	if (track_type)
		parrillada_track_type_free (track_type);

	xmlTextWriterEndDocument (project);
	xmlFreeTextWriter (project);

	g_remove (path);
	g_free (path);

	return FALSE;
}

gboolean
parrillada_project_save_audio_project_plain_text (ParrilladaBurnSession *session,
					       const gchar *uri)
{
	const gchar *title;
	guint written;
	GSList *iter;
	gchar *path;
	FILE *file;

    	path = g_filename_from_uri (uri, NULL, NULL);
    	if (!path)
		return FALSE;

	file = fopen (path, "w+");
	g_free (path);
	if (!file)
		return FALSE;

	/* write title */
	title = parrillada_burn_session_get_label (session);
	written = fwrite (title, strlen (title), 1, file);
	if (written != 1)
		goto error;

	written = fwrite ("\n", 1, 1, file);
	if (written != 1)
		goto error;

	iter = parrillada_burn_session_get_tracks (session);
	for (; iter; iter = iter->next) {
		ParrilladaTrackStream *track;
		const gchar *text;
		gchar *time;
		guint64 len;
		gchar *uri;

		track = iter->data;

		text = parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_TITLE_TAG);
		written = fwrite (title, 1, strlen (title), file);
		if (written != strlen (title))
			goto error;

		len = 0;
		parrillada_track_stream_get_length (track, &len);
		time = parrillada_units_get_time_string (len, TRUE, FALSE);
		if (time) {
			written = fwrite ("\t", 1, 1, file);
			if (written != 1)
				goto error;

			written = fwrite (time, 1, strlen (time), file);
			if (written != strlen (time)) {
				g_free (time);
				goto error;
			}
			g_free (time);
		}

		text = parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_ARTIST_TAG);
		if (text) {
			gchar *string;

			written = fwrite ("\t", 1, 1, file);
			if (written != 1)
				goto error;

			/* Translators: %s is an artist */
			string = g_strdup_printf (" by %s", text);
			written = fwrite (string, 1, strlen (string), file);
			if (written != strlen (string)) {
				g_free (string);
				goto error;
			}
			g_free (string);
		}

		written = fwrite ("\n(", 1, 2, file);
		if (written != 2)
			goto error;

		uri = parrillada_track_stream_get_source (track, TRUE);
		written = fwrite (uri, 1, strlen (uri), file);
		if (written != strlen (uri)) {
			g_free (uri);
			goto error;
		}

		g_free (uri);

		written = fwrite (")", 1, 1, file);
		if (written != 1)
			goto error;

		written = fwrite ("\n\n", 1, 2, file);
		if (written != 2)
			goto error;
	}

	fclose (file);
	return TRUE;
	
error:

	fclose (file);

	return FALSE;
}

#ifdef BUILD_PLAYLIST

gboolean
parrillada_project_save_audio_project_playlist (ParrilladaBurnSession *session,
					     const gchar *uri,
					     ParrilladaProjectSave type)
{
	TotemPlParserType pl_type;
	TotemPlParser *parser;
	TotemPlPlaylist *playlist;
	TotemPlPlaylistIter pl_iter;
	gboolean result;
	GFile *file;
	GSList *iter;

	file = g_file_new_for_uri (uri);
	parser = totem_pl_parser_new ();
	playlist = totem_pl_playlist_new ();

	/* populate playlist */
	iter = parrillada_burn_session_get_tracks (session);
	for (; iter; iter = iter->next) {
		ParrilladaTrackStream *track;
		const gchar *title;
		gchar *uri;

		track = iter->data;

		uri = parrillada_track_stream_get_source (track, TRUE);
		title = parrillada_track_tag_lookup_string (PARRILLADA_TRACK (track), PARRILLADA_TRACK_STREAM_TITLE_TAG);

		totem_pl_playlist_append (playlist, &pl_iter);
		totem_pl_playlist_set (playlist, &pl_iter,
				       TOTEM_PL_PARSER_FIELD_URI, uri,
				       TOTEM_PL_PARSER_FIELD_TITLE, title,
				       NULL);
		g_free (uri);
	}

	switch (type) {
		case PARRILLADA_PROJECT_SAVE_PLAYLIST_M3U:
			pl_type = TOTEM_PL_PARSER_M3U;
			break;
		case PARRILLADA_PROJECT_SAVE_PLAYLIST_XSPF:
			pl_type = TOTEM_PL_PARSER_XSPF;
			break;
		case PARRILLADA_PROJECT_SAVE_PLAYLIST_IRIVER_PLA:
			pl_type = TOTEM_PL_PARSER_IRIVER_PLA;
			break;

		case PARRILLADA_PROJECT_SAVE_PLAYLIST_PLS:
		default:
			pl_type = TOTEM_PL_PARSER_PLS;
			break;
	}

	result = totem_pl_parser_save (parser, playlist, file,
				       parrillada_burn_session_get_label (session),
				       type, NULL);

	g_object_unref (playlist);
	g_object_unref (parser);
	g_object_unref (file);

	return result;
}

#endif
