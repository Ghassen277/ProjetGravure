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

#ifndef _PARRILLADA_MEDIUM_SELECTION_PRIV_H_
#define _PARRILLADA_MEDIUM_SELECTION_PRIV_H_

#include "parrillada-medium-selection.h"

G_BEGIN_DECLS

typedef gboolean	(*ParrilladaMediumSelectionFunc)	(ParrilladaMedium *medium,
							 gpointer callback_data);

guint
parrillada_medium_selection_get_media_num (ParrilladaMediumSelection *selection);

void
parrillada_medium_selection_foreach (ParrilladaMediumSelection *selection,
				  ParrilladaMediumSelectionFunc function,
				  gpointer callback_data);

void
parrillada_medium_selection_update_used_space (ParrilladaMediumSelection *selection,
					    ParrilladaMedium *medium,
					    guint used_space);

void
parrillada_medium_selection_update_media_string (ParrilladaMediumSelection *selection);

G_END_DECLS

#endif /* _PARRILLADA_MEDIUM_SELECTION_PRIV_H_ */
