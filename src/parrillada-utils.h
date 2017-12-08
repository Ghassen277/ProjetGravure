/*
 * Parrillada is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Parrillada is distributed in the hope that it will be useful,
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

/***************************************************************************
 *            utils.h
 *
 *  Wed May 18 16:58:16 2005
 *  Copyright  2005  Philippe Rouquier
 *  <parrillada-app@wanadoo.fr>
 ****************************************************************************/


#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>

#ifndef _UTILS_H
#define _UTILS_H

G_BEGIN_DECLS

#define PARRILLADA_ERROR parrillada_error_quark()

typedef enum {
	PARRILLADA_ERROR_NONE,
	PARRILLADA_ERROR_GENERAL,
	PARRILLADA_ERROR_SYMLINK_LOOP
} ParrilladaErrors;

#define PARRILLADA_DEFAULT_ICON		"text-x-preview"

GQuark parrillada_error_quark (void);

void
parrillada_utils_launch_app (GtkWidget *widget,
			  GSList *list);

gboolean
parrillada_clipboard_selection_may_have_uri (GdkAtom *atoms,
					  gint n_atoms);

G_END_DECLS

#endif				/* _UTILS_H */
