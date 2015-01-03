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

#include <glib.h>

#include "scsi-device.h"
#include "scsi-error.h"
#include "scsi-utils.h"
#include "scsi-base.h"

#ifndef _BURN_SCSI_COMMAND_H
#define _BURN_SCSI_COMMAND_H

G_BEGIN_DECLS

/* Most scsi commands are <= 16 (apparently some of the new mmc can be longer) */
#define PARRILLADA_SCSI_CMD_MAX_LEN	16
	
typedef enum {
	PARRILLADA_SCSI_WRITE	= 1,
	PARRILLADA_SCSI_READ	= 1 << 1
} ParrilladaScsiDirection;

struct _ParrilladaScsiCmdInfo {
	int size;
	uchar opcode;
	ParrilladaScsiDirection direction;
};
typedef struct _ParrilladaScsiCmdInfo ParrilladaScsiCmdInfo;

#define PARRILLADA_SCSI_COMMAND_DEFINE(cdb, name, direction)			\
static const ParrilladaScsiCmdInfo info =						\
{	/* SCSI commands always end by 1 byte of ctl */				\
	G_STRUCT_OFFSET (cdb, ctl) + 1, 					\
	PARRILLADA_##name##_OPCODE,						\
	direction								\
}

gpointer
parrillada_scsi_command_new (const ParrilladaScsiCmdInfo *info,
			  ParrilladaDeviceHandle *handle);

ParrilladaScsiResult
parrillada_scsi_command_free (gpointer command);

ParrilladaScsiResult
parrillada_scsi_command_issue_sync (gpointer command,
				 gpointer buffer,
				 int size,
				 ParrilladaScsiErrCode *error);
G_END_DECLS

#endif /* _BURN_SCSI_COMMAND_H */

 
