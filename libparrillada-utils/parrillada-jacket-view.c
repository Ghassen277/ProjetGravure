/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libparrillada-misc
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libparrillada-misc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libparrillada-misc authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libparrillada-misc. This permission is above and beyond the permissions granted
 * by the GPL license by which Libparrillada-burn is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libparrillada-misc is distributed in the hope that it will be useful,
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

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include <cairo.h>

#include "parrillada-jacket-view.h"
#include "parrillada-jacket-buffer.h"

#include "parrillada-misc.h"

typedef struct _ParrilladaJacketViewPrivate ParrilladaJacketViewPrivate;
struct _ParrilladaJacketViewPrivate
{
	ParrilladaJacketSide side;

	GtkWidget *edit;
	GtkWidget *sides;

	GdkColor b_color;
	GdkColor b_color2;
	ParrilladaJacketColorStyle color_style;

	cairo_pattern_t *pattern;

	GdkPixbuf *image;
	GdkPixbuf *scaled;

	gchar *image_path;
	ParrilladaJacketImageStyle image_style;
};

#define PARRILLADA_JACKET_VIEW_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), PARRILLADA_TYPE_JACKET_VIEW, ParrilladaJacketViewPrivate))

enum
{
	PRINTED,
	TAGS_CHANGED,
	LAST_SIGNAL
};


static guint jacket_view_signals[LAST_SIGNAL] = { 0 };


G_DEFINE_TYPE (ParrilladaJacketView, parrillada_jacket_view, GTK_TYPE_CONTAINER);

#define PARRILLADA_JACKET_VIEW_MARGIN		20


static GSList *
parrillada_jacket_view_tag_begins (GtkTextIter *iter,
				GtkTextAttributes *attributes)
{
	gint index;
	PangoAttribute *attr;
	GSList *open_attr = NULL;

	index = gtk_text_iter_get_visible_line_index (iter);
	attr = pango_attr_foreground_new (attributes->appearance.fg_color.red,
					  attributes->appearance.fg_color.green,
					  attributes->appearance.fg_color.blue);
	attr->start_index = index;
	open_attr = g_slist_prepend (open_attr, attr);

	attr = pango_attr_font_desc_new (attributes->font);
	attr->start_index = index;
	open_attr = g_slist_prepend (open_attr, attr);

	attr = pango_attr_underline_new (attributes->appearance.underline);
	attr->start_index = index;
	open_attr = g_slist_prepend (open_attr, attr);

	return open_attr;
}

static void
parrillada_jacket_view_tag_ends (GtkTextIter *iter,
			      PangoAttrList *attributes,
			      GSList *open_attr)
{
	GSList *list;

	for (list = open_attr; list; list = list->next) {
		PangoAttribute *attr;

		attr = list->data;
		attr->end_index = gtk_text_iter_get_visible_line_index (iter);
		pango_attr_list_insert (attributes, attr);
	}
}

static void
parrillada_jacket_view_set_line_attributes (GtkTextView *view,
					 PangoLayout *layout,
					 GtkTextIter *start,
                                         GtkTextIter *end)
{
	PangoAttrList *attributes = NULL;
	GtkTextAttributes *text_attr;
	GSList *open_attr = NULL;
	PangoAlignment alignment;
	GtkTextBuffer *buffer;
	GtkTextIter iter;

	attributes = pango_attr_list_new ();

	buffer = gtk_text_view_get_buffer (view);
	iter = *start;

	text_attr = gtk_text_view_get_default_attributes (view);
	gtk_text_iter_get_attributes (&iter, text_attr);

	switch (text_attr->justification) {
	case GTK_JUSTIFY_CENTER:
		alignment = PANGO_ALIGN_CENTER;
		break;
	case GTK_JUSTIFY_LEFT:
		alignment = PANGO_ALIGN_LEFT;
		break;
	case GTK_JUSTIFY_RIGHT:
		alignment = PANGO_ALIGN_RIGHT;
		break;
	default:
		alignment = PANGO_ALIGN_LEFT;
		break;
	};

	open_attr = parrillada_jacket_view_tag_begins (&iter, text_attr);
	gtk_text_attributes_unref (text_attr);

	while (gtk_text_iter_forward_to_tag_toggle (&iter, NULL) &&
	       gtk_text_iter_compare (&iter, end) < 0 &&
	      !gtk_text_iter_is_end (&iter)) {

		parrillada_jacket_view_tag_ends (&iter, attributes, open_attr);
		g_slist_free (open_attr);

		text_attr = gtk_text_view_get_default_attributes (view);
		gtk_text_iter_get_attributes (&iter, text_attr);

		switch (text_attr->justification) {
		case GTK_JUSTIFY_CENTER:
			alignment = PANGO_ALIGN_CENTER;
			break;
		case GTK_JUSTIFY_LEFT:
			alignment = PANGO_ALIGN_LEFT;
			break;
		case GTK_JUSTIFY_RIGHT:
			alignment = PANGO_ALIGN_RIGHT;
			break;
		default:
			alignment = PANGO_ALIGN_LEFT;
			break;
		};
		open_attr = parrillada_jacket_view_tag_begins (&iter, text_attr);
		gtk_text_attributes_unref (text_attr);
	}

	/* go through all still opened attributes */
	parrillada_jacket_view_tag_ends (end, attributes, open_attr);
	g_slist_free (open_attr);

	pango_layout_set_attributes (layout, attributes);
	pango_attr_list_unref (attributes);

	pango_layout_set_alignment (layout, alignment);
}

static void
parrillada_jacket_view_render_side_text (ParrilladaJacketView *self,
				      cairo_t *ctx,
				      PangoLayout *layout,
				      gdouble resolution,
				      gdouble x,
				      gdouble y)
{
	gdouble y_left;
	gdouble x_left;
	gdouble x_right;
	gdouble y_right;
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	PangoContext *pango_ctx;
	ParrilladaJacketViewPrivate *priv;
	cairo_font_options_t *font_options;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	cairo_set_source_rgb (ctx, 0.0, 0.0, 0.0);

	/* This is vital to get the exact same layout when printing. By default
	 * this is off for printing and on for screen display */
	font_options = cairo_font_options_create ();
	cairo_font_options_set_antialias (font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_metrics (font_options, CAIRO_HINT_METRICS_ON);
	cairo_font_options_set_hint_style (font_options, CAIRO_HINT_STYLE_SLIGHT);
	cairo_set_font_options (ctx, font_options);

	pango_ctx = pango_layout_get_context (layout);
	pango_cairo_context_set_font_options (pango_ctx, font_options);
	cairo_font_options_destroy (font_options);

	pango_layout_set_width (layout, resolution * COVER_HEIGHT_SIDE_INCH * PANGO_SCALE);

	x_left = x + 0.5;
	y_left = y + COVER_HEIGHT_SIDE_INCH * resolution - 0.5;

	x_right = x + COVER_WIDTH_BACK_INCH * resolution - 0.5;
	y_right = y + 0.5;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->sides));
	gtk_text_buffer_get_start_iter (buffer, &start);
	end = start;

	while (!gtk_text_iter_is_end (&start)) {
		gchar *text;
		PangoRectangle rect;

		gtk_text_view_forward_display_line_end (GTK_TEXT_VIEW (priv->sides), &end);

		text = parrillada_jacket_buffer_get_text (PARRILLADA_JACKET_BUFFER (buffer), &start, &end, FALSE, FALSE);
		if (text && text [0] != '\0' && text [0] != '\n') {
			pango_layout_set_text (layout, text, -1);
			g_free (text);
		}
		else
			pango_layout_set_text (layout, " ", -1);

		parrillada_jacket_view_set_line_attributes (GTK_TEXT_VIEW (priv->sides), layout, &start, &end);

		cairo_save (ctx);

		cairo_move_to (ctx, x_left, y_left);
		pango_cairo_update_layout (ctx, layout);
		cairo_rotate (ctx, - G_PI_2);
		pango_cairo_show_layout (ctx, layout);

		cairo_restore (ctx);

		cairo_save (ctx);

		cairo_move_to (ctx, x_right, y_right);
		pango_cairo_update_layout (ctx, layout);
		cairo_rotate (ctx, G_PI_2);
		pango_cairo_show_layout (ctx, layout);

		cairo_restore (ctx);

		pango_layout_get_pixel_extents (layout, NULL, &rect);

		x_right -= rect.height;
		x_left += rect.height;

		gtk_text_view_forward_display_line (GTK_TEXT_VIEW (priv->sides), &end);
		start = end;
	}
}

static void
parrillada_jacket_view_render_body (ParrilladaJacketView *self,
				 cairo_t *ctx,
                                 PangoLayout *layout,
				 gdouble resolution_x,
				 gdouble resolution_y,
				 guint x,
				 guint y,
				 gboolean render_if_empty)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	PangoContext *pango_ctx;
	ParrilladaJacketViewPrivate *priv;
	cairo_font_options_t *font_options;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	/* This is vital to get the exact same layout when printing. By default
	 * this is off for printing and on for screen display */
	font_options = cairo_font_options_create ();
	cairo_font_options_set_antialias (font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_metrics (font_options, CAIRO_HINT_METRICS_OFF);
	cairo_font_options_set_hint_style (font_options, CAIRO_HINT_STYLE_SLIGHT);
	cairo_set_font_options (ctx, font_options);

	pango_ctx = pango_layout_get_context (layout);
	pango_cairo_context_set_font_options (pango_ctx, font_options);
	cairo_font_options_destroy (font_options);

	/* This is necessary for the alignment of text */
	if (priv->side == PARRILLADA_JACKET_BACK)
		pango_layout_set_width (layout, (COVER_WIDTH_BACK_INCH - (COVER_WIDTH_SIDE_INCH + COVER_TEXT_MARGIN) * 2.0) * resolution_x * PANGO_SCALE);
	else
		pango_layout_set_width (layout, (COVER_WIDTH_FRONT_INCH - COVER_TEXT_MARGIN * 2.0) * resolution_x * PANGO_SCALE);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->edit));
	gtk_text_buffer_get_start_iter (buffer, &start);
	end = start;

	while (!gtk_text_iter_is_end (&start)) {
		gchar *text;
		PangoRectangle rect;

		gtk_text_view_forward_display_line_end (GTK_TEXT_VIEW (priv->edit), &end);

		text = parrillada_jacket_buffer_get_text (PARRILLADA_JACKET_BUFFER (buffer), &start, &end, FALSE, render_if_empty);
		if (text && text [0] != '\0' && text [0] != '\n') {
			pango_layout_set_text (layout, text, -1);
			g_free (text);
		}
		else
			pango_layout_set_text (layout, " ", -1);

		parrillada_jacket_view_set_line_attributes (GTK_TEXT_VIEW (priv->edit), layout, &start, &end);
		pango_cairo_update_layout (ctx, layout);

		if (priv->side == PARRILLADA_JACKET_BACK)
			cairo_move_to (ctx,
				       x + (COVER_WIDTH_SIDE_INCH + COVER_TEXT_MARGIN) * resolution_x + 0.5,
				       y + COVER_TEXT_MARGIN * resolution_y);
		else
			cairo_move_to (ctx,
				       x + COVER_TEXT_MARGIN * resolution_x + 0.5,
				       y + COVER_TEXT_MARGIN * resolution_y);

		pango_cairo_show_layout (ctx, layout);
		pango_layout_get_pixel_extents (layout, NULL, &rect);
		y += rect.height;

		gtk_text_view_forward_display_line (GTK_TEXT_VIEW (priv->edit), &end);
		start = end;
	}
}

static void
parrillada_jacket_view_render (ParrilladaJacketView *self,
			    cairo_t *ctx,
			    PangoLayout *layout,
			    GdkPixbuf *scaled,
			    gdouble resolution_x,
			    gdouble resolution_y,
			    guint x,
			    guint y,
			    GdkRectangle *area,
			    gboolean render_if_empty)
{
	ParrilladaJacketViewPrivate *priv;
	int height, width;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (priv->side == PARRILLADA_JACKET_BACK) {
		width = COVER_WIDTH_BACK_INCH * resolution_x;
		height = COVER_HEIGHT_BACK_INCH * resolution_y;
	}
	else {
		width = COVER_WIDTH_FRONT_INCH * resolution_x;
		height = COVER_HEIGHT_FRONT_INCH * resolution_y;
	}

	/* set clip */
	cairo_reset_clip (ctx);
	cairo_rectangle (ctx, area->x, area->y, area->width, area->height);
	cairo_clip (ctx);

	/* draw white surroundings */
	cairo_set_source_rgb (ctx, 1.0, 1.0, 1.0);
	cairo_paint (ctx);

	/* draw background */
	cairo_rectangle (ctx, x, y, width + 2.0, height + 2.0);
	cairo_clip (ctx);

	if (priv->pattern) {
		cairo_set_source (ctx, priv->pattern);
		cairo_paint (ctx);
	}

	if (scaled) {
		/* The problem is the resolution here. The one for the screen
		 * may not be the one for the printer. So do not use our private
		 * scaled image. */
		if (priv->image_style == PARRILLADA_JACKET_IMAGE_CENTER)
			gdk_cairo_set_source_pixbuf (ctx,
						     scaled,
						     x + (width - gdk_pixbuf_get_width (scaled)) / 2.0,
						     y + (height - gdk_pixbuf_get_height (scaled)) / 2.0);
		else
			gdk_cairo_set_source_pixbuf (ctx, scaled, x, y);

		if (priv->image_style == PARRILLADA_JACKET_IMAGE_TILE) {
			cairo_pattern_t *pattern;

			pattern = cairo_get_source (ctx);
			cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
		}

		cairo_paint (ctx);
	}

	if (priv->side == PARRILLADA_JACKET_BACK) {
		gdouble line_x, line_y;

		cairo_save (ctx);

		/* Draw the rectangle */
		cairo_set_source_rgb (ctx, 0.0, 0.0, 0.0);
		cairo_set_line_width (ctx, 1.0);

		line_y = y + (COVER_HEIGHT_SIDE_INCH * resolution_y) - 0.5;

		line_x = (int) (x + (COVER_WIDTH_SIDE_INCH * resolution_x)) + 0.5;
		cairo_move_to (ctx,
			       line_x,
			       y + 0.5);
		cairo_line_to (ctx,
			       line_x,
			       line_y);

		line_x = (int) (x + ((COVER_WIDTH_BACK_INCH - COVER_WIDTH_SIDE_INCH) * resolution_x)) + 0.5;
		cairo_move_to (ctx,
			       line_x,
			       y + 0.5);
		cairo_line_to (ctx,
			       line_x,
			       line_y);

		cairo_stroke (ctx);

		cairo_restore (ctx);
		cairo_save (ctx);

		/* also render text in the sides */
		parrillada_jacket_view_render_side_text (self,
						      ctx,
						      layout,
						      resolution_y,
						      x,
						      y);

		cairo_restore (ctx);
	}

	/* Draw the rectangle */
	cairo_set_source_rgb (ctx, 0.0, 0.0, 0.0);
	cairo_set_line_width (ctx, 1.0);

	cairo_rectangle (ctx,
			 x + 0.5,
			 y + 0.5,
			 width,
			 height);
	cairo_stroke (ctx);
}

static GdkPixbuf *
parrillada_jacket_view_scale_image (ParrilladaJacketView *self,
				 gdouble resolution_x,
				 gdouble resolution_y)
{
	ParrilladaJacketViewPrivate *priv;
	guint width, height;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (priv->side == PARRILLADA_JACKET_BACK) {
		height = resolution_y * COVER_HEIGHT_BACK_INCH;
		width = resolution_x * COVER_WIDTH_BACK_INCH;
	}
	else {
		height = resolution_y * COVER_HEIGHT_FRONT_INCH;
		width = resolution_x * COVER_WIDTH_FRONT_INCH;
	}

	return gdk_pixbuf_scale_simple (priv->image,
					width,
					height,
					GDK_INTERP_HYPER);
}

guint
parrillada_jacket_view_print (ParrilladaJacketView *self,
			   GtkPrintContext *context,
			   gdouble x,
			   gdouble y)
{
	cairo_t *ctx;
	GdkRectangle rect;
	PangoLayout *layout;
	gdouble resolution_x;
	gdouble resolution_y;
	GdkPixbuf *scaled = NULL;
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	ctx = gtk_print_context_get_cairo_context (context);

	/* set clip */
	resolution_x = gtk_print_context_get_dpi_x (context);
	resolution_y = gtk_print_context_get_dpi_y (context);
	rect.x = x;
	rect.y = y;

	if (priv->side == PARRILLADA_JACKET_BACK) {
		rect.width = (resolution_x * COVER_WIDTH_BACK_INCH) + 1.0;
		rect.height = (resolution_y * COVER_HEIGHT_BACK_INCH) + 1.0;
	}
	else {
		rect.width = (resolution_x * COVER_WIDTH_FRONT_INCH) + 1.0;
		rect.height = (resolution_y * COVER_HEIGHT_FRONT_INCH) + 1.0;
	}

	/* Make sure we scale the image with the correct resolution */
	if (priv->image_style == PARRILLADA_JACKET_IMAGE_STRETCH)
		scaled = parrillada_jacket_view_scale_image (self,
							  resolution_x,
							  resolution_y);
	else if (priv->scaled)
		scaled = g_object_ref (priv->scaled);

	layout = gtk_print_context_create_pango_layout (context);
	parrillada_jacket_view_render (self,
				    ctx,
				    layout,
				    scaled,
				    resolution_x,
				    resolution_y,
				    x,
				    y,
				    &rect,
				    FALSE);

	/* Now let's render the text in main buffer */
	parrillada_jacket_view_render_body (self,
					 ctx,
	                                 layout,
					 resolution_x,
					 resolution_y,
					 x,
					 y,
					 FALSE);

	g_object_unref (layout);

	if (scaled)
		g_object_unref (scaled);

	return rect.height;
}

static void
parrillada_jacket_view_cursor_position_changed_cb (GObject *buffer,
						GParamSpec *spec,
						ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);
	g_signal_emit (self,
		       jacket_view_signals [TAGS_CHANGED],
		       0);
}

static void
parrillada_jacket_view_apply_tag (GtkTextBuffer *buffer,
			       GtkTextTag *tag,
			       GtkTextIter *start,
			       GtkTextIter *end,
			       ParrilladaJacketView *self)
{
	g_signal_emit (self,
		       jacket_view_signals [TAGS_CHANGED],
		       0);
	gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
parrillada_jacket_view_side_buffer_changed (GtkTextBuffer *buffer,
					 ParrilladaJacketView *self)
{
	gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
parrillada_jacket_view_focus_in_cb (GtkWidget *view,
				 GdkEventFocus *event,
				 ParrilladaJacketView *self)
{
	GtkTextView *text_view = GTK_TEXT_VIEW (view);
	GtkTextBuffer *buffer;
	
	buffer = gtk_text_view_get_buffer (text_view);
	parrillada_jacket_buffer_show_default_text (PARRILLADA_JACKET_BUFFER (buffer), FALSE);

	g_signal_emit (self,
		       jacket_view_signals [TAGS_CHANGED],
		       0);
}

static void
parrillada_jacket_view_focus_out_cb (GtkWidget *view,
				  GdkEventFocus *event,
				  ParrilladaJacketView *self)
{
	GtkTextView *text_view = GTK_TEXT_VIEW (view);
	GtkTextBuffer *buffer;
	
	buffer = gtk_text_view_get_buffer (text_view);
	parrillada_jacket_buffer_show_default_text (PARRILLADA_JACKET_BUFFER (buffer), TRUE);

	g_signal_emit (self,
		       jacket_view_signals [TAGS_CHANGED],
	               0);
}

static void
parrillada_jacket_view_scrolled_cb (GtkAdjustment *adj,
				 GtkTextView *view)
{
	gint trailing;
	GtkTextIter end;
	GtkTextIter start;
	GdkRectangle rect;
	GtkTextBuffer *buffer;

	if (gtk_adjustment_get_value (adj) == 0.0)
		return;

	g_signal_stop_emission_by_name (adj, "value-changed");

	buffer = gtk_text_view_get_buffer (view);

	gtk_text_buffer_get_end_iter (buffer, &end);

	gtk_text_view_get_visible_rect (view, &rect);
	gtk_text_view_get_iter_at_position (view, &start, &trailing, rect.x + rect.width, rect.y + rect.height - gtk_adjustment_get_value (adj));
	gtk_text_buffer_delete (buffer, &start, &end);

	gtk_adjustment_set_value (adj, 0.0);
}

void
parrillada_jacket_view_configure_background (ParrilladaJacketView *self)
{
	ParrilladaJacketImageStyle image_style;
	ParrilladaJacketColorStyle color_style;
	ParrilladaJacketViewPrivate *priv;
	GtkWidget *dialog;
	GdkColor color2;
	GdkColor color;
	gchar *path;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	dialog = parrillada_jacket_background_new ();

	parrillada_jacket_background_set_image_path (PARRILLADA_JACKET_BACKGROUND (dialog), priv->image_path);
	parrillada_jacket_background_set_image_style (PARRILLADA_JACKET_BACKGROUND (dialog), priv->image_style);
	parrillada_jacket_background_set_color (PARRILLADA_JACKET_BACKGROUND (dialog),
					     &priv->b_color,
					     &priv->b_color2);
	parrillada_jacket_background_set_color_style (PARRILLADA_JACKET_BACKGROUND (dialog), priv->color_style);

	gtk_dialog_run (GTK_DIALOG (dialog));

	image_style = parrillada_jacket_background_get_image_style (PARRILLADA_JACKET_BACKGROUND (dialog));
	path = parrillada_jacket_background_get_image_path (PARRILLADA_JACKET_BACKGROUND (dialog));
	parrillada_jacket_view_set_image_style (self, image_style);
	parrillada_jacket_view_set_image (self, path);
	g_free (path);

	parrillada_jacket_background_get_color (PARRILLADA_JACKET_BACKGROUND (dialog), &color, &color2);
	parrillada_jacket_view_set_color_background (self, &color, &color2);

	color_style = parrillada_jacket_background_get_color_style (PARRILLADA_JACKET_BACKGROUND (dialog));
	parrillada_jacket_view_set_color_style (self, color_style);

	gtk_widget_destroy (dialog);
}

static void
parrillada_jacket_view_change_image_activated_cb (GtkMenuItem *item,
					       ParrilladaJacketView *self)
{
	parrillada_jacket_view_configure_background (self);
}

static void
parrillada_jacket_view_populate_popup_cb (GtkTextView *view,
				       GtkMenu *menu,
				       ParrilladaJacketView *self)
{
	GtkWidget *item;

	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("Set Bac_kground Properties"));
	gtk_widget_show (item);
	g_signal_connect (item,
			  "activate",
			  G_CALLBACK (parrillada_jacket_view_change_image_activated_cb),
			  self);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

void
parrillada_jacket_view_set_side (ParrilladaJacketView *self,
			      ParrilladaJacketSide side)
{
	ParrilladaJacketViewPrivate *priv;
	GtkTextBuffer *buffer;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	priv->side = side;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->edit));

	if (priv->side == PARRILLADA_JACKET_BACK) {
		GtkTextBuffer *sides_buffer;
		GtkObject *vadj;
		GtkObject *hadj;

		sides_buffer = GTK_TEXT_BUFFER (parrillada_jacket_buffer_new ());
		g_signal_connect (sides_buffer,
				  "changed",
				  G_CALLBACK (parrillada_jacket_view_side_buffer_changed),
				  self);
		g_signal_connect (sides_buffer,
				  "notify::cursor-position",
				  G_CALLBACK (parrillada_jacket_view_cursor_position_changed_cb),
				  self);
		g_signal_connect_after (sides_buffer,
					"apply-tag",
					G_CALLBACK (parrillada_jacket_view_apply_tag),
					self);
		parrillada_jacket_buffer_set_default_text (PARRILLADA_JACKET_BUFFER (sides_buffer), _("SIDES"));

		priv->sides = gtk_text_view_new_with_buffer (sides_buffer);
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (priv->sides), GTK_WRAP_CHAR);
		gtk_widget_set_parent (priv->sides, GTK_WIDGET (self));
		gtk_widget_show (priv->sides);

		g_signal_connect (priv->sides,
				  "focus-in-event",
				  G_CALLBACK (parrillada_jacket_view_focus_in_cb),
				  self);
		g_signal_connect_after (priv->sides,
					"focus-out-event",
					G_CALLBACK (parrillada_jacket_view_focus_out_cb),
					self);

		parrillada_jacket_buffer_set_default_text (PARRILLADA_JACKET_BUFFER (buffer), _("BACK COVER"));

		hadj = gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
		vadj = gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

		g_signal_connect (hadj,
				  "value-changed",
				  G_CALLBACK (parrillada_jacket_view_scrolled_cb),
				  priv->sides);
		g_signal_connect (vadj,
				  "value-changed",
				  G_CALLBACK (parrillada_jacket_view_scrolled_cb),
				  priv->sides);

		gtk_widget_set_scroll_adjustments (priv->sides, GTK_ADJUSTMENT (hadj), GTK_ADJUSTMENT (vadj));
	}
	else
		parrillada_jacket_buffer_set_default_text (PARRILLADA_JACKET_BUFFER (buffer), _("FRONT COVER"));
}

static void
parrillada_jacket_view_update_edit_image (ParrilladaJacketView *self)
{
	cairo_t *ctx;
	guint resolution;
	GdkWindow *window;
	GdkPixmap *pixmap;
	GtkWidget *toplevel;
	GtkAllocation allocation;
	gint width, height, x, y;
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (!priv->pattern && !priv->scaled)
		return;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
	if (!GTK_IS_WINDOW (toplevel))
		return;

	resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));
	window = gtk_text_view_get_window (GTK_TEXT_VIEW (priv->edit), GTK_TEXT_WINDOW_TEXT);

	if (!window)
		return;

	x = COVER_TEXT_MARGIN * resolution;
	y = COVER_TEXT_MARGIN * resolution;
	gtk_widget_get_allocation (priv->edit, &allocation);
	width = allocation.width;
	height = allocation.height;

	if (priv->side == PARRILLADA_JACKET_BACK)
		x += COVER_WIDTH_SIDE_INCH * resolution;

	pixmap = gdk_pixmap_new (GDK_DRAWABLE (window),
				 width,
				 height,
				 -1);

	ctx = gdk_cairo_create (GDK_DRAWABLE (pixmap));

	cairo_rectangle (ctx,
			 0,
			 0,
			 width,
			 height);

	if (priv->pattern)
		cairo_set_source (ctx, priv->pattern);

	cairo_clip (ctx);
	cairo_paint (ctx);

	if (priv->scaled) {
		if (priv->image_style == PARRILLADA_JACKET_IMAGE_CENTER) {
			gdk_cairo_set_source_pixbuf (ctx,
			                             priv->scaled,
			                             (width - gdk_pixbuf_get_width (priv->scaled)) / 2,
			                             (height - gdk_pixbuf_get_height (priv->scaled)) / 2);
		}
		else if (priv->image_style == PARRILLADA_JACKET_IMAGE_TILE) {
			cairo_pattern_t *pattern;

			gdk_cairo_set_source_pixbuf (ctx, priv->scaled, 0, 0);
			pattern = cairo_get_source (ctx);
			cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
		}
		else
			gdk_cairo_set_source_pixbuf (ctx, priv->scaled, x, y);

		cairo_paint (ctx);
	}

	cairo_destroy (ctx);

	gdk_window_set_back_pixmap (window, pixmap, FALSE);
	g_object_unref (pixmap);
}

static GdkPixbuf *
parrillada_jacket_view_crop_image (ParrilladaJacketView *self,
				guint width,
                                guint height)
{
	ParrilladaJacketViewPrivate *priv;
	gint x, y;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (gdk_pixbuf_get_width (priv->image) > width) {
		x = 0;
		width = gdk_pixbuf_get_width (priv->image);
	}
	else
		x = (gdk_pixbuf_get_width (priv->image) - width) / 2;

	if (gdk_pixbuf_get_height (priv->image) > height) {
		y = 0;
		height = gdk_pixbuf_get_height (priv->image);
	}
	else
		y = (gdk_pixbuf_get_height (priv->image) - height) / 2;

	return gdk_pixbuf_new_subpixbuf (priv->image,
	                                 x,
	                                 y,
	                                 width,
	                                 height);
}

static void
parrillada_jacket_view_update_image (ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (!priv->image)
		return;

	if (priv->image_style == PARRILLADA_JACKET_IMAGE_CENTER) {
		GtkAllocation allocation;

		gtk_widget_get_allocation (priv->edit, &allocation);
		if (allocation.width < gdk_pixbuf_get_width (priv->image)
		||  allocation.height < gdk_pixbuf_get_height (priv->image))
			priv->scaled = parrillada_jacket_view_crop_image (self,
			                                               allocation.width,
			                                               allocation.height);
		else
			priv->scaled = g_object_ref (priv->image);
	}
	else if (priv->image_style == PARRILLADA_JACKET_IMAGE_STRETCH) {
		guint resolution;
		GtkWidget *toplevel;

		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
		if (!GTK_IS_WINDOW (toplevel))
			return;

		resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));
		priv->scaled = parrillada_jacket_view_scale_image (self, resolution, resolution);
	}
	else if (priv->image_style == PARRILLADA_JACKET_IMAGE_TILE)
		priv->scaled = g_object_ref (priv->image);

	parrillada_jacket_view_update_edit_image (self);
	gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
parrillada_jacket_view_set_image_style (ParrilladaJacketView *self,
				     ParrilladaJacketImageStyle style)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (priv->scaled) {
		g_object_unref (priv->scaled);
		priv->scaled = NULL;
	}

	priv->image_style = style;
	parrillada_jacket_view_update_image (self);
}

static void
parrillada_jacket_view_update_color (ParrilladaJacketView *self)
{
	guint resolution;
	GtkWidget *toplevel;
	guint width, height;
	cairo_pattern_t *pattern;
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (priv->pattern) {
		cairo_pattern_destroy (priv->pattern);
		priv->pattern = NULL;
	}

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
	if (!GTK_IS_WINDOW (toplevel))
		return;

	resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));
	if (priv->side == PARRILLADA_JACKET_BACK) {
		height = resolution * COVER_HEIGHT_BACK_INCH;
		width = resolution * COVER_WIDTH_BACK_INCH;
	}
	else {
		height = resolution * COVER_HEIGHT_FRONT_INCH;
		width = resolution * COVER_WIDTH_FRONT_INCH;
	}

	if (priv->color_style == PARRILLADA_JACKET_COLOR_SOLID) {
		pattern = cairo_pattern_create_rgb (priv->b_color.red/G_MAXINT16,
						    priv->b_color.green/G_MAXINT16,
						    priv->b_color.blue/G_MAXINT16);
	}
	else {
		if (priv->color_style == PARRILLADA_JACKET_COLOR_HGRADIENT)
			pattern = cairo_pattern_create_linear (0.0,
							       0.0,
							       width,
							       0.0);
		else /* if (priv->color_style == PARRILLADA_JACKET_COLOR_VGRADIENT) */
			pattern = cairo_pattern_create_linear (0.0,
							       0.0,
							       0.0,
							       height);

		cairo_pattern_add_color_stop_rgb (pattern,
						  0.0,
						  priv->b_color.red/G_MAXINT16,
						  priv->b_color.green/G_MAXINT16,
						  priv->b_color.blue/G_MAXINT16);

		cairo_pattern_add_color_stop_rgb (pattern,
						  1.0,
						  priv->b_color2.red/G_MAXINT16,
						  priv->b_color2.green/G_MAXINT16,
						  priv->b_color2.blue/G_MAXINT16);
	}

	priv->pattern = pattern;

	parrillada_jacket_view_update_edit_image (self);
	gtk_widget_queue_draw (GTK_WIDGET (self));
}

const gchar *
parrillada_jacket_view_get_image (ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);
	return priv->image_path;
}

const gchar *
parrillada_jacket_view_set_image (ParrilladaJacketView *self,
			       const gchar *path)
{
	ParrilladaJacketViewPrivate *priv;
	GdkPixbuf *image = NULL;
	GError *error = NULL;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (!path)
		return priv->image_path;

	image = gdk_pixbuf_new_from_file (path, &error);
	if (error) {
		parrillada_utils_message_dialog (gtk_widget_get_toplevel (GTK_WIDGET (self)),
					      /* Translators: This is an image,
					       * a picture, not a "Disc Image" */
					      _("The image could not be loaded."),
					      error->message,
					      GTK_MESSAGE_ERROR);
		g_error_free (error);
		return priv->image_path;
	}

	if (priv->image_path) {
		g_free (priv->image_path);
		priv->image_path = NULL;
	}

	if (priv->scaled) {
		g_object_unref (priv->scaled);
		priv->scaled = NULL;
	}

	if (priv->image) {
		g_object_unref (priv->image);
		priv->image = NULL;
	}

	priv->image_path = g_strdup (path);
	priv->image = image;

	parrillada_jacket_view_update_image (self);
	return priv->image_path;
}

void
parrillada_jacket_view_set_color_background (ParrilladaJacketView *self,
					  GdkColor *color,
					  GdkColor *color2)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);
	priv->b_color = *color;
	priv->b_color2 = *color2;
	parrillada_jacket_view_update_color (self);
}

void
parrillada_jacket_view_set_color_style (ParrilladaJacketView *self,
				     ParrilladaJacketColorStyle style)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);
	priv->color_style = style;
	parrillada_jacket_view_update_color (self);
}

GtkTextAttributes *
parrillada_jacket_view_get_attributes (ParrilladaJacketView *self,
				    GtkTextIter *iter)
{
	ParrilladaJacketViewPrivate *priv;
	GtkTextAttributes *attributes;
	GtkTextBuffer *buffer;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	attributes = gtk_text_view_get_default_attributes (GTK_TEXT_VIEW (priv->edit));

	if (iter)
		gtk_text_iter_get_attributes (iter, attributes);

	/* Now also merge changes that are 'on hold', that is non applied tags */
	buffer = parrillada_jacket_view_get_active_buffer (self);
	if (!buffer)
		return attributes;

	parrillada_jacket_buffer_get_attributes (PARRILLADA_JACKET_BUFFER (buffer), attributes);
	return attributes;
}

GtkTextBuffer *
parrillada_jacket_view_get_active_buffer (ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;
	GtkWidget *current;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (priv->sides && gtk_widget_is_focus (priv->sides))
		current = priv->sides;
	else if (gtk_widget_is_focus (priv->edit))
		current = priv->edit;
	else
		return NULL;

	return gtk_text_view_get_buffer (GTK_TEXT_VIEW (current));
}

GtkTextBuffer *
parrillada_jacket_view_get_body_buffer (ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	return gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->edit));	
}

GtkTextBuffer *
parrillada_jacket_view_get_side_buffer (ParrilladaJacketView *self)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (self);

	if (!priv->sides)
		return NULL;

	return gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->sides));
}

static void
parrillada_jacket_expose_textview (GtkWidget *widget,
                                GtkWidget *textview,
                                GdkEventExpose *event)
{
	GdkRectangle child_area;

	if (gtk_widget_intersect (textview, &event->area, &child_area)) {
		GdkWindow *window;

		window = gtk_text_view_get_window (GTK_TEXT_VIEW (textview), GTK_TEXT_WINDOW_WIDGET);

		g_object_ref (window);
		gdk_window_invalidate_rect (window, &child_area, TRUE);
		gdk_window_process_updates (window, TRUE);
		g_object_unref (window);

		/* Reminder: the following would not work...
		 * gtk_container_propagate_expose (GTK_CONTAINER (widget), textview, &child_event); */
	}
}

static gboolean
parrillada_jacket_view_expose (GtkWidget *widget,
			    GdkEventExpose *event)
{
	guint x;
	guint y;
	cairo_t *ctx;
	gdouble resolution;
	GtkWidget *toplevel;
	PangoLayout *layout;
	GtkAllocation allocation, sides_allocation;
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (widget);

	ctx = gdk_cairo_create (GDK_DRAWABLE (gtk_widget_get_window (widget)));
	toplevel = gtk_widget_get_toplevel (widget);
	if (!GTK_IS_WINDOW (toplevel))
		return FALSE;

	resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));
	layout = gtk_widget_create_pango_layout (widget, NULL);
	gtk_widget_get_allocation (widget, &allocation);
	if (priv->side == PARRILLADA_JACKET_BACK) {
		x = (allocation.width - resolution * COVER_WIDTH_BACK_INCH) / 2;
		y = (allocation.height - resolution * COVER_HEIGHT_BACK_INCH) - PARRILLADA_JACKET_VIEW_MARGIN;

		parrillada_jacket_view_render (PARRILLADA_JACKET_VIEW (widget),
					    ctx,
					    layout,
					    priv->scaled,
					    resolution,
					    resolution,
					    x,
					    y,
					    &event->area,
					    TRUE);

		/* rectangle for side text */

		/* set clip */
		cairo_reset_clip (ctx);
		cairo_rectangle (ctx, event->area.x, event->area.y, event->area.width, event->area.height);
		cairo_clip (ctx);

		cairo_move_to (ctx, 0., 0.);

		cairo_set_antialias (ctx, CAIRO_ANTIALIAS_DEFAULT);
		cairo_set_source_rgb (ctx, 0.0, 0.0, 0.0);
		cairo_set_line_width (ctx, 1.0);
		cairo_set_line_cap (ctx, CAIRO_LINE_CAP_ROUND);

		gtk_widget_get_allocation (priv->sides, &sides_allocation);
		cairo_rectangle (ctx,
				 sides_allocation.x - 1 + 0.5,
				 sides_allocation.y - 1 + 0.5,
				 sides_allocation.width + 2,
				 sides_allocation.height + 2);
		cairo_stroke (ctx);
	}
	else {
		x = (allocation.width - resolution * COVER_WIDTH_FRONT_INCH) / 2;
		y = (allocation.height - resolution * COVER_HEIGHT_FRONT_INCH) / 2;

		parrillada_jacket_view_render (PARRILLADA_JACKET_VIEW (widget),
					    ctx,
					    layout,
					    priv->scaled,
					    resolution,
					    resolution,
					    x,
					    y,
					    &event->area,
					    TRUE);
	}

	if (priv->sides)
		parrillada_jacket_expose_textview (widget, priv->sides, event);

	parrillada_jacket_expose_textview (widget, priv->edit, event);
	
	g_object_unref (layout);
	cairo_destroy (ctx);

	return FALSE;
}

static void
parrillada_jacket_view_realize (GtkWidget *widget)
{
	ParrilladaJacketViewPrivate *priv;
	GtkAllocation allocation;
	GdkWindow *window;
	GdkWindowAttr attributes;
	gint attributes_mask;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (widget);

	attributes.window_type = GDK_WINDOW_CHILD;
	gtk_widget_get_allocation (widget, &allocation);
	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = gtk_widget_get_events (widget);
	attributes.event_mask |= GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK|GDK_LEAVE_NOTIFY_MASK;
	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_COLORMAP;

	gtk_widget_set_window (widget, gdk_window_new (gtk_widget_get_parent_window (widget),
						       &attributes,
						       attributes_mask));
	window = gtk_widget_get_window (widget);
	gdk_window_set_user_data (window, widget);

	gtk_widget_set_realized (widget, TRUE);

	gtk_widget_style_attach (widget);

	gdk_window_show (gtk_widget_get_window (widget));
}

static void
parrillada_jacket_view_size_request (GtkWidget *widget,
				  GtkRequisition *request)
{
	ParrilladaJacketViewPrivate *priv;
	GtkWidget *toplevel;
	gdouble resolution;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (widget);

	if (!gtk_widget_get_parent (widget))
		return;

	toplevel = gtk_widget_get_toplevel (widget);
	if (!GTK_IS_WINDOW (toplevel))
		return;

	resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));

	if (priv->side == PARRILLADA_JACKET_FRONT) {
		request->width = COVER_WIDTH_FRONT_INCH * resolution + PARRILLADA_JACKET_VIEW_MARGIN * 2.0;
		request->height = COVER_HEIGHT_FRONT_INCH * resolution + PARRILLADA_JACKET_VIEW_MARGIN * 2.0;
	}
	else if (priv->side == PARRILLADA_JACKET_BACK) {
		request->width = COVER_WIDTH_BACK_INCH * resolution +
				 PARRILLADA_JACKET_VIEW_MARGIN * 2.0;
		request->height = COVER_HEIGHT_BACK_INCH * resolution +
				  COVER_WIDTH_SIDE_INCH * resolution +
				  PARRILLADA_JACKET_VIEW_MARGIN * 3.0;
	}
}

static void
parrillada_jacket_view_size_allocate (GtkWidget *widget,
				   GtkAllocation *allocation)
{
	ParrilladaJacketViewPrivate *priv;
	GtkAllocation view_alloc;
	GtkWidget *toplevel;
	gdouble resolution;

	toplevel = gtk_widget_get_toplevel (widget);
	if (!GTK_IS_WINDOW (toplevel))
		return;

	resolution = gdk_screen_get_resolution (gtk_window_get_screen (GTK_WINDOW (toplevel)));
	priv = PARRILLADA_JACKET_VIEW_PRIVATE (widget);

	if (priv->image && priv->image_style == PARRILLADA_JACKET_IMAGE_STRETCH) {
		if (priv->scaled) {
			g_object_unref (priv->scaled);
			priv->scaled = NULL;
		}

		/* scale pixbuf */
		parrillada_jacket_view_update_image (PARRILLADA_JACKET_VIEW (widget));
	}

	view_alloc.x = PARRILLADA_JACKET_VIEW_MARGIN + COVER_TEXT_MARGIN * resolution;
	view_alloc.y = PARRILLADA_JACKET_VIEW_MARGIN + COVER_TEXT_MARGIN * resolution;

	if (priv->side == PARRILLADA_JACKET_BACK) {
		view_alloc.x = (allocation->width - COVER_HEIGHT_SIDE_INCH * resolution) / 2;
		view_alloc.y = PARRILLADA_JACKET_VIEW_MARGIN;
		view_alloc.width = COVER_HEIGHT_SIDE_INCH * resolution;
		view_alloc.height = COVER_WIDTH_SIDE_INCH * resolution;

		gtk_widget_size_allocate (priv->sides, &view_alloc);

		view_alloc.x = (allocation->width - COVER_WIDTH_BACK_INCH * resolution) / 2 +
			       (COVER_TEXT_MARGIN + COVER_WIDTH_SIDE_INCH) * resolution;

		view_alloc.y = (allocation->height - resolution * COVER_HEIGHT_BACK_INCH) -
				PARRILLADA_JACKET_VIEW_MARGIN +
				COVER_TEXT_MARGIN * resolution;

		view_alloc.width = (COVER_WIDTH_BACK_INCH - (COVER_TEXT_MARGIN + COVER_WIDTH_SIDE_INCH) * 2.0) * resolution;
		view_alloc.height = (COVER_HEIGHT_BACK_INCH - COVER_TEXT_MARGIN * 2.0) * resolution;
	}
	else {
		view_alloc.x = (allocation->width - COVER_WIDTH_FRONT_INCH * resolution) / 2.0 +
				COVER_TEXT_MARGIN * resolution;
		view_alloc.y = (allocation->height - resolution * COVER_HEIGHT_FRONT_INCH) / 2.0 +
				COVER_TEXT_MARGIN * resolution;

		view_alloc.width = (gdouble) (COVER_WIDTH_FRONT_INCH - COVER_TEXT_MARGIN * 2.0) * resolution;
		view_alloc.height = (gdouble) (COVER_HEIGHT_FRONT_INCH - COVER_TEXT_MARGIN * 2.0) * resolution;
	}

	parrillada_jacket_view_update_edit_image (PARRILLADA_JACKET_VIEW (widget));
	gtk_widget_size_allocate (priv->edit, &view_alloc);

	gtk_widget_set_allocation (widget, allocation);
	if (gtk_widget_get_realized (widget) && gtk_widget_get_has_window (widget)) {
		gdk_window_move_resize (gtk_widget_get_window (widget),
					allocation->x,
					allocation->y,
					allocation->width,
					allocation->height);
	}
}

static void
parrillada_jacket_view_container_forall (GtkContainer *container,
				      gboolean include_internals,
				      GtkCallback callback,
				      gpointer callback_data)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (container);
	if (priv->edit)
		callback (priv->edit, callback_data);
	if (priv->sides)
		callback (priv->sides, callback_data);
}

static void
parrillada_jacket_view_container_remove (GtkContainer *container,
				      GtkWidget *widget)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (container);
	if (priv->edit == widget)
		priv->edit = NULL;

	if (priv->sides == widget)
		priv->sides = NULL;
}

static void
parrillada_jacket_view_init (ParrilladaJacketView *object)
{
	ParrilladaJacketViewPrivate *priv;
	GtkTextBuffer *buffer;
	GtkObject *vadj;
	GtkObject *hadj;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (object);

	buffer = GTK_TEXT_BUFFER (parrillada_jacket_buffer_new ());
	g_signal_connect (buffer,
			  "notify::cursor-position",
			  G_CALLBACK (parrillada_jacket_view_cursor_position_changed_cb),
			  object);
	g_signal_connect_after (buffer,
				"apply-tag",
				G_CALLBACK (parrillada_jacket_view_apply_tag),
				object);

	priv->edit = gtk_text_view_new_with_buffer (buffer);
	g_object_unref (buffer);

	priv->b_color = gtk_widget_get_style (priv->edit)->bg [0];
	priv->color_style = PARRILLADA_JACKET_COLOR_SOLID;

	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (priv->edit), GTK_WRAP_CHAR);
	gtk_widget_set_parent (priv->edit, GTK_WIDGET (object));
	gtk_widget_show (priv->edit);

	g_signal_connect (priv->edit,
			  "focus-in-event",
			  G_CALLBACK (parrillada_jacket_view_focus_in_cb),
			  object);
	g_signal_connect_after (priv->edit,
				"focus-out-event",
				G_CALLBACK (parrillada_jacket_view_focus_out_cb),
				object);
	g_signal_connect_after (priv->edit,
				"populate-popup",
				G_CALLBACK (parrillada_jacket_view_populate_popup_cb),
				object);
	hadj = gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
	vadj = gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

	g_signal_connect (hadj,
			  "value-changed",
			  G_CALLBACK (parrillada_jacket_view_scrolled_cb),
			  priv->edit);
	g_signal_connect (vadj,
			  "value-changed",
			  G_CALLBACK (parrillada_jacket_view_scrolled_cb),
			  priv->edit);

	gtk_container_set_focus_child (GTK_CONTAINER (object), priv->edit);
	gtk_widget_set_scroll_adjustments (priv->edit,
					   GTK_ADJUSTMENT (hadj),
					   GTK_ADJUSTMENT (vadj));
}

static void
parrillada_jacket_view_finalize (GObject *object)
{
	ParrilladaJacketViewPrivate *priv;

	priv = PARRILLADA_JACKET_VIEW_PRIVATE (object);
	if (priv->image) {
		g_object_unref (priv->image);
		priv->image = NULL;
	}

	if (priv->scaled) {
		g_object_unref (priv->scaled);
		priv->scaled = NULL;
	}

	if (priv->pattern) {
		cairo_pattern_destroy (priv->pattern);
		priv->pattern = NULL;
	}

	if (priv->image_path) {
		g_free (priv->image_path);
		priv->image_path = NULL;
	}

	G_OBJECT_CLASS (parrillada_jacket_view_parent_class)->finalize (object);
}

static void
parrillada_jacket_view_class_init (ParrilladaJacketViewClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass* widget_class = GTK_WIDGET_CLASS (klass);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ParrilladaJacketViewPrivate));

	object_class->finalize = parrillada_jacket_view_finalize;

	widget_class->expose_event = parrillada_jacket_view_expose;
	widget_class->realize = parrillada_jacket_view_realize;
	widget_class->size_allocate = parrillada_jacket_view_size_allocate;
	widget_class->size_request = parrillada_jacket_view_size_request;

	container_class->forall = parrillada_jacket_view_container_forall;
	container_class->remove = parrillada_jacket_view_container_remove;

	jacket_view_signals[PRINTED] =
		g_signal_new ("printed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0,
		              G_TYPE_NONE);
	jacket_view_signals[TAGS_CHANGED] =
		g_signal_new ("tags_changed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0,
		              G_TYPE_NONE);
}

GtkWidget *
parrillada_jacket_view_new (void)
{
	return g_object_new (PARRILLADA_TYPE_JACKET_VIEW, NULL);
}
