/***************************************************************************
 *            parrillada-file-chooser.h
 *
 *  lun mai 29 08:53:18 2006
 *  Copyright  2006  Rouquier Philippe
 *  parrillada-app@wanadoo.fr
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

#ifndef PARRILLADA_FILE_CHOOSER_H
#define PARRILLADA_FILE_CHOOSER_H

#include <glib.h>
#include <glib-object.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PARRILLADA_TYPE_FILE_CHOOSER         (parrillada_file_chooser_get_type ())
#define PARRILLADA_FILE_CHOOSER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PARRILLADA_TYPE_FILE_CHOOSER, ParrilladaFileChooser))
#define PARRILLADA_FILE_CHOOSER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), PARRILLADA_TYPE_FILE_CHOOSER, ParrilladaFileChooserClass))
#define PARRILLADA_IS_FILE_CHOOSER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PARRILLADA_TYPE_FILE_CHOOSER))
#define PARRILLADA_IS_FILE_CHOOSER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PARRILLADA_TYPE_FILE_CHOOSER))
#define PARRILLADA_FILE_CHOOSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PARRILLADA_TYPE_FILE_CHOOSER, ParrilladaFileChooserClass))

typedef struct ParrilladaFileChooserPrivate ParrilladaFileChooserPrivate;

typedef struct {
	GtkAlignment parent;
	ParrilladaFileChooserPrivate *priv;
} ParrilladaFileChooser;

typedef struct {
	GtkAlignmentClass parent_class;
} ParrilladaFileChooserClass;

GType parrillada_file_chooser_get_type (void);
GtkWidget *parrillada_file_chooser_new (void);

void
parrillada_file_chooser_customize (GtkWidget *widget,
				gpointer null_data);

G_END_DECLS

#endif /* PARRILLADA_FILE_CHOOSER_H */
