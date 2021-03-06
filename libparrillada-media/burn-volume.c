/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libparrillada-media
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libparrillada-media is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libparrillada-media authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libparrillada-media. This permission is above and beyond the permissions granted
 * by the GPL license by which Libparrillada-media is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libparrillada-media is distributed in the hope that it will be useful,
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

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "burn-volume-source.h"
#include "burn-volume.h"
#include "burn-iso9660.h"
#include "parrillada-media.h"
#include "parrillada-media-private.h"
#include "parrillada-units.h"

struct _ParrilladaTagDesc {
	guint16 id;
	guint16 version;
	guchar checksum;
	guchar reserved;
	guint16 serial;
	guint16 crc;
	guint16 crc_len;
	guint32 location;
};
typedef struct _ParrilladaTagDesc ParrilladaTagDesc;

struct _ParrilladaAnchorDesc {
	ParrilladaTagDesc tag;

	guchar main_extent		[8];
	guchar reserve_extent		[8];
};
typedef struct _ParrilladaAnchorDesc ParrilladaAnchorDesc;

#define SYSTEM_AREA_SECTORS		16
#define ANCHOR_AREA_SECTORS		256


void
parrillada_volume_file_free (ParrilladaVolFile *file)
{
	if (!file)
		return;

	if (file->isdir) {
		GList *iter;

		if (file->isdir_loaded) {
			for (iter = file->specific.dir.children; iter; iter = iter->next)
				parrillada_volume_file_free (iter->data);

			g_list_free (file->specific.dir.children);
		}
	}
	else {
		g_slist_foreach (file->specific.file.extents,
				 (GFunc) g_free,
				 NULL);
		g_slist_free (file->specific.file.extents);
	}

	g_free (file->rr_name);
	g_free (file->name);
	g_free (file);
}

static gboolean
parrillada_volume_get_primary_from_file (ParrilladaVolSrc *vol,
				      gchar *primary_vol,
				      GError **error)
{
	ParrilladaVolDesc *desc;

	/* skip the first 16 blocks */
	if (PARRILLADA_VOL_SRC_SEEK (vol, SYSTEM_AREA_SECTORS, SEEK_CUR, error) == -1)
		return FALSE;

	if (!PARRILLADA_VOL_SRC_READ (vol, primary_vol, 1, error))
		return FALSE;

	/* make a few checks to ensure this is an ECMA volume */
	desc = (ParrilladaVolDesc *) primary_vol;
	if (memcmp (desc->id, "CD001", 5)
	&&  memcmp (desc->id, "BEA01", 5)
	&&  memcmp (desc->id, "BOOT2", 5)
	&&  memcmp (desc->id, "CDW02", 5)
	&&  memcmp (desc->id, "NSR02", 5)	/* usually UDF */
	&&  memcmp (desc->id, "NSR03", 5)	/* usually UDF */
	&&  memcmp (desc->id, "TEA01", 5)) {
		g_set_error (error,
			     PARRILLADA_MEDIA_ERROR,
			     PARRILLADA_MEDIA_ERROR_IMAGE_INVALID,
			     _("It does not appear to be a valid ISO image"));
		PARRILLADA_MEDIA_LOG ("Wrong volume descriptor, got %.5s", desc->id);
		return FALSE;
	}

	return TRUE;
}

gboolean
parrillada_volume_get_size (ParrilladaVolSrc *vol,
			 gint64 block,
			 gint64 *nb_blocks,
			 GError **error)
{
	gboolean result;
	gchar buffer [ISO9660_BLOCK_SIZE];

	if (block && PARRILLADA_VOL_SRC_SEEK (vol, block, SEEK_SET, error) == -1)
		return FALSE;

	result = parrillada_volume_get_primary_from_file (vol, buffer, error);
	if (!result)
		return FALSE;

	if (!parrillada_iso9660_is_primary_descriptor (buffer, error))
		return FALSE;

	return parrillada_iso9660_get_size (buffer, nb_blocks, error);
}

ParrilladaVolFile *
parrillada_volume_get_files (ParrilladaVolSrc *vol,
			  gint64 block,
			  gchar **label,
			  gint64 *nb_blocks,
			  gint64 *data_blocks,
			  GError **error)
{
	gchar buffer [ISO9660_BLOCK_SIZE];

	if (PARRILLADA_VOL_SRC_SEEK (vol, block, SEEK_SET, error) == -1)
		return FALSE;

	if (!parrillada_volume_get_primary_from_file (vol, buffer, error))
		return NULL;

	if (!parrillada_iso9660_is_primary_descriptor (buffer, error))
		return NULL;

	if (label
	&& !parrillada_iso9660_get_label (buffer, label, error))
		return NULL;

	if (nb_blocks
	&& !parrillada_iso9660_get_size (buffer, nb_blocks, error))
		return NULL;

	return parrillada_iso9660_get_contents (vol,
					     buffer,
					     data_blocks,
					     error);
}

GList *
parrillada_volume_load_directory_contents (ParrilladaVolSrc *vol,
					gint64 session_block,
					gint64 block,
					GError **error)
{
	gchar buffer [ISO9660_BLOCK_SIZE];

	if (PARRILLADA_VOL_SRC_SEEK (vol, session_block, SEEK_SET, error) == -1)
		return FALSE;

	if (!parrillada_volume_get_primary_from_file (vol, buffer, error))
		return NULL;

	if (!parrillada_iso9660_is_primary_descriptor (buffer, error))
		return NULL;

	return parrillada_iso9660_get_directory_contents (vol,
						       buffer,
						       block,
						       error);
}

ParrilladaVolFile *
parrillada_volume_get_file (ParrilladaVolSrc *vol,
			 const gchar *path,
			 gint64 volume_start_block,
			 GError **error)
{
	gchar buffer [ISO9660_BLOCK_SIZE];

	if (PARRILLADA_VOL_SRC_SEEK (vol, volume_start_block, SEEK_SET, error) == -1)
		return NULL;

	if (!parrillada_volume_get_primary_from_file (vol, buffer, error))
		return NULL;

	if (!parrillada_iso9660_is_primary_descriptor (buffer, error))
		return NULL;

	return parrillada_iso9660_get_file (vol, path, buffer, error);
}

gchar *
parrillada_volume_file_to_path (ParrilladaVolFile *file)
{
	GString *path;
	ParrilladaVolFile *parent;
	GSList *components = NULL, *iter, *next;

	if (!file)
		return NULL;

	/* make a list of all the components of the path by going up to root */
	parent = file->parent;
	while (parent && parent->name) {
		components = g_slist_prepend (components, PARRILLADA_VOLUME_FILE_NAME (parent));
		parent = parent->parent;
	}

	if (!components)
		return NULL;

	path = g_string_new (NULL);
	for (iter = components; iter; iter = next) {
		gchar *name;

		name = iter->data;
		next = iter->next;
		components = g_slist_remove (components, name);

		g_string_append_c (path, G_DIR_SEPARATOR);
		g_string_append (path, name);
	}

	g_slist_free (components);
	return g_string_free (path, FALSE);
}

ParrilladaVolFile *
parrillada_volume_file_from_path (const gchar *ptr,
			       ParrilladaVolFile *parent)
{
	GList *iter;
	gchar *next;
	gint len;

	/* first determine the name of the directory / file to look for */
	if (!ptr || ptr [0] != '/' || !parent)
		return NULL;

	ptr ++;
	next = g_utf8_strchr (ptr, -1, G_DIR_SEPARATOR);
	if (!next)
		len = strlen (ptr);
	else
		len = next - ptr;

	for (iter = parent->specific.dir.children; iter; iter = iter->next) {
		ParrilladaVolFile *file;

		file = iter->data;
		if (!strncmp (ptr, PARRILLADA_VOLUME_FILE_NAME (file), len)) {
			/* we've found it seek for the next if any */
			if (!next)
				return file;

			ptr = next;
			return parrillada_volume_file_from_path (ptr, file);
		}
	}

	return NULL;
}

gint64
parrillada_volume_file_size (ParrilladaVolFile *file)
{
	GList *iter;
	gint64 size = 0;

	if (!file->isdir) {
		GSList *extents;

		for (extents = file->specific.file.extents; extents; extents = extents->next) {
			ParrilladaVolFileExtent *extent;

			extent = extents->data;
			size += extent->size;
		}
		return PARRILLADA_BYTES_TO_SECTORS (size, 2048);
	}

	for (iter = file->specific.dir.children; iter; iter = iter->next) {
		file = iter->data;

		if (file->isdir)
			size += parrillada_volume_file_size (file);
		else
			size += PARRILLADA_BYTES_TO_SECTORS (file->specific.file.size_bytes, 2048);
	}

	return size;
}

ParrilladaVolFile *
parrillada_volume_file_merge (ParrilladaVolFile *file1,
			   ParrilladaVolFile *file2)
{
	file1->specific.file.size_bytes += file2->specific.file.size_bytes;
	file1->specific.file.extents = g_slist_concat (file1->specific.file.extents,
							     file2->specific.file.extents);

	file2->specific.file.extents = NULL;
	parrillada_volume_file_free (file2);

	return file1;
}

