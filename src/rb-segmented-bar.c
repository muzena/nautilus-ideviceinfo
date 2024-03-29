/*
 * Initial Author:
 *   Aaron Bockover <abockover@novell.com>
 *
 * Ported to C from Banshee's SegmentedBar.cs widget
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 Christophe Fergeau <teuf@gnome.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <cairo/cairo.h>
#include <gtk/gtk.h>
#include "rb-segmented-bar.h"

#define MINIMUM_HEIGHT 26

static void rb_segmented_bar_finalize (GObject *object);
static void rb_segmented_bar_size_request (GtkWidget *widget,
					   GtkRequisition *requisition);
static void rb_segmented_bar_size_allocate(GtkWidget *widget,
					   GtkAllocation *allocation);
static gboolean rb_segmented_bar_expose (GtkWidget *widget,
					 GdkEventExpose *event);
static void rb_segmented_bar_get_property (GObject *object, guint param_id,
					   GValue *value, GParamSpec *pspec);
static void rb_segmented_bar_set_property (GObject *object, guint param_id,
					   const GValue *value, GParamSpec *pspec);

static gchar *rb_segmented_bar_default_value_formatter (gdouble percent,
						       	gpointer data);

static void compute_layout_size (RBSegmentedBar *bar);

enum
{
	PROP_0,
	PROP_SHOW_REFLECTION,
	PROP_SHOW_LABELS,
	PROP_BAR_HEIGHT
};

struct _RBSegmentedBarPrivate {
	GList *segments;
	guint layout_width;
	guint layout_height;

	guint bar_height;
	guint bar_label_spacing;
	guint segment_label_spacing;
	guint segment_box_size;
	guint segment_box_spacing;
	guint h_padding;
	
	gboolean show_labels;
	gboolean reflect;

	RBSegmentedBarValueFormatter value_formatter;
	gpointer value_formatter_data;
};

G_DEFINE_TYPE (RBSegmentedBar, rb_segmented_bar, GTK_TYPE_WIDGET)
#define RB_SEGMENTED_BAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SEGMENTED_BAR, RBSegmentedBarPrivate))

struct _Color {
	gdouble red;
	gdouble green;
	gdouble blue;
	gdouble alpha;
};
typedef struct _Color Color;

struct _Segment {
	gchar *label;
	gdouble percent;
	Color color;

	guint layout_width;
	guint layout_height;
};
typedef struct _Segment Segment;

static Segment *rb_segment_new (const gchar *label, gdouble percent, Color *color)
{
	Segment *segment;

	segment = g_new0 (Segment, 1);
	segment->label = g_strdup (label);
	segment->percent = percent;
	segment->color.red = color->red;
	segment->color.green = color->green;
	segment->color.blue = color->blue;
	segment->color.alpha = color->alpha;
	
	return segment;
}

static void rb_segment_free (Segment *segment)
{
	g_return_if_fail (segment != NULL);
	g_free (segment->label);
	g_free (segment);
}

static void
rb_segmented_bar_init (RBSegmentedBar *bar)
{
	RBSegmentedBarPrivate *priv;

	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (bar));
	priv->bar_label_spacing = 8;
	priv->segment_label_spacing = 16;
	priv->segment_box_size = 12;
	priv->segment_box_spacing = 6;
	priv->value_formatter = rb_segmented_bar_default_value_formatter;
	GTK_WIDGET_SET_FLAGS (GTK_WIDGET (bar), GTK_NO_WINDOW);
}

static void
rb_segmented_bar_class_init (RBSegmentedBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = rb_segmented_bar_finalize;
	object_class->get_property = rb_segmented_bar_get_property;
	object_class->set_property = rb_segmented_bar_set_property;

	widget_class->expose_event = rb_segmented_bar_expose;
	widget_class->size_request = rb_segmented_bar_size_request;
	widget_class->size_allocate = rb_segmented_bar_size_allocate;

        /**
         * RBSegmentedBar::show-reflection
         *
         * Set to TRUE if you want a reflection to be shown below the segmented
	 * bar.
         */
        g_object_class_install_property (object_class,
                                         PROP_SHOW_REFLECTION,
                                         g_param_spec_boolean ("show-reflection",
                                                               "show-reflection",
                                                               "Whether there will be a reflection below the segmented bar",
                                                               TRUE,
                                                               G_PARAM_READWRITE));

        /**
         * RBSegmentedBar::show-labels
         *
         * Set to TRUE if you want labels describing the various segments
	 * to be shown.
         */
        g_object_class_install_property (object_class,
                                         PROP_SHOW_LABELS,
                                         g_param_spec_boolean ("show-labels",
                                                               "show-labels",
                                                               "Whether the labels describing the various segments should be shown",
                                                               TRUE,
                                                               G_PARAM_READWRITE));
        /**
         * RBSegmentedBar::bar-height
         *
         * Height of the segmented bar
         */
	g_object_class_install_property (object_class,
					 PROP_BAR_HEIGHT,
					 g_param_spec_uint ("bar-height",
							    "bar-height",
							    "height of the segmented bar",
							    MINIMUM_HEIGHT, G_MAXUINT, MINIMUM_HEIGHT,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (klass, sizeof (RBSegmentedBarPrivate));
}

static void
rb_segmented_bar_finalize (GObject *object)
{
	RBSegmentedBarPrivate *priv;
	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (object));
	g_list_foreach (priv->segments, (GFunc)rb_segment_free, NULL);
	g_list_free (priv->segments);
	G_OBJECT_CLASS (rb_segmented_bar_parent_class)->finalize (object);
} 

static void
rb_segmented_bar_get_property (GObject *object,
			       guint param_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	RBSegmentedBarPrivate *priv;
	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (object));

	switch (param_id) {
	    case PROP_SHOW_REFLECTION:
		g_value_set_boolean (value, priv->reflect);
		break;
	    case PROP_SHOW_LABELS:
		g_value_set_boolean (value, priv->show_labels);
		break;
	    case PROP_BAR_HEIGHT:	
		g_value_set_uint (value, priv->bar_height);
		break;
	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
rb_segmented_bar_set_property (GObject *object,
			       guint param_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	RBSegmentedBarPrivate *priv;
	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (object));

	switch (param_id) {
	    case PROP_SHOW_REFLECTION:
		priv->reflect = g_value_get_boolean (value);
		break;
	    case PROP_SHOW_LABELS:
		priv->show_labels = g_value_get_boolean (value);
		break;
	    case PROP_BAR_HEIGHT:	
		priv->bar_height = g_value_get_uint (value);
		break;
	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static gchar *
rb_segmented_bar_default_value_formatter (gdouble percent,
					  G_GNUC_UNUSED gpointer data)
{
	return g_strdup_printf ("%.2f%%", percent*100.0);
}

static void
rb_segmented_bar_size_request (GtkWidget *widget,
			       GtkRequisition *requisition)
{
	RBSegmentedBarPrivate *priv;
	guint real_height;

	g_return_if_fail (requisition != NULL);

	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (widget));

	if (priv->reflect) {
		real_height = MINIMUM_HEIGHT*1.75;
	} else {
		real_height = MINIMUM_HEIGHT;
	}
	if (priv->show_labels) {
		compute_layout_size (RB_SEGMENTED_BAR (widget));
		requisition->height = MAX (MINIMUM_HEIGHT + priv->bar_label_spacing + priv->layout_height,
						 real_height);
	} else {
		requisition->height = real_height;
	}
	requisition->width = MAX (priv->layout_width, 200);
}

static PangoLayout *create_adapt_layout (GtkWidget *widget, PangoLayout *layout,
					 gboolean small, gboolean bold)
{
	const PangoFontDescription *desc;
	PangoFontDescription *new_desc;

	int normal_font_size;
	if (layout == NULL) {
		layout = gtk_widget_create_pango_layout (GTK_WIDGET (widget), 
							 NULL);
	}
	desc = pango_context_get_font_description (gtk_widget_get_pango_context (widget));
	g_assert (desc != NULL);
	normal_font_size = pango_font_description_get_size (desc);

	desc = pango_context_get_font_description (pango_layout_get_context (layout));
	g_assert (desc != NULL);
	new_desc = pango_font_description_copy (desc);

	if (small) {
		pango_font_description_set_size (new_desc,
						 normal_font_size * PANGO_SCALE_SMALL);
	} else {
		pango_font_description_set_size (new_desc, normal_font_size);
	}

	if (bold) {
		pango_font_description_set_weight (new_desc,
						   PANGO_WEIGHT_BOLD);
	} else {
		pango_font_description_set_weight (new_desc,
						   PANGO_WEIGHT_NORMAL);
	}
	pango_layout_set_font_description (layout, new_desc);
	pango_font_description_free (new_desc);
	return layout;
}

static void 
compute_layout_size (RBSegmentedBar *bar)
{
	RBSegmentedBarPrivate *priv = RB_SEGMENTED_BAR_GET_PRIVATE (bar);
	PangoLayout *layout = NULL;
	GList *it;

	if (priv->segments == NULL) {
		return;
	}

	priv->layout_width = 0;
	priv->layout_height = 0;

	for (it = priv->segments; it != NULL; it = it->next) {
		Segment *segment = (Segment *)it->data;
		gint label_width;
		gint label_height;
		gint value_width;
		gint value_height;
		gint width;
		gint height;
		gchar *value_str;

		layout = create_adapt_layout (GTK_WIDGET (bar), layout,
					      FALSE, TRUE);
		pango_layout_set_text (layout, segment->label, -1);
		pango_layout_get_pixel_size (layout, 
					     &label_width,
					     &label_height);

		layout = create_adapt_layout (GTK_WIDGET (bar), layout,
					      TRUE, FALSE);
		g_assert (priv->value_formatter != NULL);
		value_str = priv->value_formatter (segment->percent,
						   priv->value_formatter_data);
		pango_layout_set_text (layout, value_str, -1);
		g_free (value_str);
		pango_layout_get_pixel_size (layout, 
					     &value_width,
					     &value_height);

		width = MAX (label_width, value_width);
		height = label_height + value_height;

		segment->layout_width = width;
		segment->layout_height = MAX ((guint)height, priv->segment_box_size*2);

		priv->layout_width += segment->layout_width + priv->segment_box_size + priv->segment_box_spacing;
		if (it->next != NULL) {
			priv->layout_width += priv->segment_label_spacing;
		}
		priv->layout_height = MAX (priv->layout_height, segment->layout_height);
	}

	g_object_unref (G_OBJECT (layout));
}

static void 
rb_segmented_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation) 
{ 
	guint real_height;
	RBSegmentedBarPrivate *priv = RB_SEGMENTED_BAR_GET_PRIVATE (widget);

	g_return_if_fail(RB_IS_SEGMENTED_BAR(widget)); 
	g_return_if_fail(allocation != NULL); 

	if (priv->reflect) {
		real_height = priv->bar_height*1.75;
	} else {
		real_height = priv->bar_height;
	}
	widget->allocation = *allocation; 
	if (priv->show_labels) {
		compute_layout_size (RB_SEGMENTED_BAR (widget));
		widget->allocation.height = MAX (priv->bar_height + priv->bar_label_spacing + priv->layout_height,
						 real_height);
	} else {
		widget->allocation.height = real_height;
	}
	widget->allocation.width = priv->layout_width + 2*(priv->h_padding);
	GTK_WIDGET_CLASS(rb_segmented_bar_parent_class)->size_allocate(widget, allocation); 
}


guint rb_segmented_bar_add_segment (RBSegmentedBar *bar,
				    const gchar *title, gdouble percent,
				    gdouble red, gdouble green,
				    gdouble blue, gdouble alpha)
{
	Color color = { red, green, blue, alpha };
	RBSegmentedBarPrivate *priv = RB_SEGMENTED_BAR_GET_PRIVATE (bar);
	guint index;
	Segment *segment = rb_segment_new (title, percent, &color);
	priv->segments = g_list_append (priv->segments, segment);
	index = g_list_index (priv->segments, segment);
	gtk_widget_queue_draw (GTK_WIDGET (bar));
	gtk_widget_queue_resize (GTK_WIDGET (bar));

	return index;
}

guint rb_segmented_bar_add_segment_default_color (RBSegmentedBar *bar,
						  const gchar *title,
						  gdouble percent)
{
	return rb_segmented_bar_add_segment (bar, title, percent, 0.9, 0.9, 0.9, 1.0);
}

void rb_segmented_bar_update_segment (RBSegmentedBar *bar,
				      guint segment_index,
				      gdouble percent)
{
	RBSegmentedBarPrivate *priv = RB_SEGMENTED_BAR_GET_PRIVATE (bar);
	Segment *segment = g_list_nth_data (priv->segments, segment_index);
	if (segment != NULL) {
		segment->percent = percent;
		gtk_widget_queue_draw (GTK_WIDGET (bar));
	}
}

static void draw_rounded_rectangle (cairo_t *context,
				    guint x, guint y,
				    guint width, guint height,
				    guint radius)
{
	if (radius < 0.0001) {
		cairo_rectangle (context, x, y, width, height);
		return;
	}
	cairo_move_to (context, x+radius, y);
	cairo_arc (context, x+width-radius, y+radius, radius, G_PI*1.5, G_PI*2);
	cairo_arc (context, x+width-radius, y+height-radius, radius, 0, G_PI*0.5);
	cairo_arc (context, x+radius, y+height-radius, radius, G_PI*0.5, G_PI);
	cairo_arc (context, x+radius, y+radius, radius, G_PI, G_PI*1.5);
}

static void rb_segmented_bar_render_segments (RBSegmentedBar *bar,
					      cairo_t *context,
					      guint width, guint height,
					      guint radius)
{
	cairo_pattern_t *grad;
	gdouble last;
	GList *it;
	RBSegmentedBarPrivate *priv;
	gboolean is_rtl = (gtk_widget_get_direction(GTK_WIDGET(bar)) == GTK_TEXT_DIR_RTL);

	last = 0.0;
	priv = RB_SEGMENTED_BAR_GET_PRIVATE (bar);
	grad = cairo_pattern_create_linear (0, 0, width, 0);
	for (it = is_rtl ? g_list_last(priv->segments) : priv->segments; it != NULL; it = is_rtl ? it->prev : it->next) {
		Segment *segment = (Segment *)it->data;
		if (segment->percent > 0) {
			gdouble percent = (segment->percent < 0.009) ? 0.009 : segment->percent;
			cairo_pattern_add_color_stop_rgba (grad, last,
							   segment->color.red,
							   segment->color.green,
							   segment->color.blue,
							   segment->color.alpha);
			last += percent;
			cairo_pattern_add_color_stop_rgba (grad, last,
							   segment->color.red,
							   segment->color.green,
							   segment->color.blue,
							   segment->color.alpha);
		}
	}

	draw_rounded_rectangle (context, 0, 0, width, height, radius);
	cairo_set_source (context, grad);
	cairo_fill_preserve (context);
	cairo_pattern_destroy (grad);

	grad = cairo_pattern_create_linear (0, 0, 0, height);
	cairo_pattern_add_color_stop_rgba (grad, 0.0, 1, 1, 1, 0.125);
	cairo_pattern_add_color_stop_rgba (grad, 0.35, 1, 1, 1, 0.255);
	cairo_pattern_add_color_stop_rgba (grad, 1, 0, 0, 0, 0.4);
	cairo_set_source (context, grad);
	cairo_fill (context);
	cairo_pattern_destroy (grad);

}

static void hsb_from_color (Color *color, gdouble *hue,
			    gdouble *saturation, gdouble *brightness)
{
	gtk_rgb_to_hsv (color->red, color->green, color->blue,
			hue, saturation, brightness);
}

static Color *color_from_hsb (gdouble hue, gdouble saturation, gdouble brightness)
{
	Color *color;

	color = g_new0 (Color, 1);
	gtk_hsv_to_rgb (hue, saturation, brightness,
			&color->red, &color->green, &color->blue);

	return color;
}

static Color *color_shade (Color *base, gdouble ratio)
{
	gdouble h;
	gdouble s;
	gdouble b;
	Color *color;

	hsb_from_color (base, &h, &s, &b);

	b = MAX (MIN (b*ratio, 1), 0);
	s = MAX (MIN (s*ratio, 1), 0);

	color = color_from_hsb (h, s, b);
	color->alpha = base->alpha;

	return color;
}

static cairo_pattern_t *make_segment_gradient (guint height,
					       gdouble red, gdouble green, 
					       gdouble blue, gdouble alpha)
{
	cairo_pattern_t *grad;
	Color *shade;
	Color color = { red, green, blue, alpha };

	grad = cairo_pattern_create_linear (0, 0, 0, height);

	shade = color_shade (&color, 1.1);
	cairo_pattern_add_color_stop_rgba (grad, 0,
					   shade->red, shade->green,
					   shade->blue, shade->alpha);
	g_free (shade);

	shade = color_shade (&color, 1.2);
	cairo_pattern_add_color_stop_rgba (grad, 0.35,
					   shade->red, shade->green,
					   shade->blue, shade->alpha);
	g_free (shade);

	shade = color_shade (&color, 0.8);
	cairo_pattern_add_color_stop_rgba (grad, 1,
					   shade->red, shade->green,
					   shade->blue, shade->alpha);
	g_free (shade);

	return grad;
}

static void rb_segmented_bar_render_strokes (RBSegmentedBar *bar,
					     cairo_t *context,
					     guint width, guint height,
					     guint radius)
{
	cairo_pattern_t *stroke = make_segment_gradient (height,
							 0, 0, 0, 0.25);
	cairo_pattern_t *seg_sep_light = make_segment_gradient (height,
								1, 1, 1, 0.125);
	cairo_pattern_t *seg_sep_dark = make_segment_gradient (height,
							       0, 0, 0, 0.125);
	gdouble seg_w = 20;
	gdouble x;
	if (seg_w > radius) {
		x = seg_w;
	} else {
		seg_w = radius;
	}
	cairo_set_line_width (context, 1);

	while (x <= width-radius) {
		cairo_move_to (context, x - 0.5, 1);
		cairo_line_to (context, x - 0.5, height - 1);
		cairo_set_source (context, seg_sep_light);
		cairo_stroke (context);

		cairo_move_to (context, x + 0.5, 1);
		cairo_line_to (context, x + 0.5, height - 1);
		cairo_set_source (context, seg_sep_dark);
		cairo_stroke (context);

		x += seg_w;
	}

	draw_rounded_rectangle (context, 0.5, 0.5,
			       	width - 1, height - 1, radius);
	cairo_set_source (context, stroke);
	cairo_stroke (context);

	cairo_pattern_destroy (stroke);
	cairo_pattern_destroy (seg_sep_light);
	cairo_pattern_destroy (seg_sep_dark);
}

static cairo_pattern_t *rb_segmented_bar_render (RBSegmentedBar *bar,
						 guint width, guint height)
{
	cairo_surface_t *surface;
	cairo_t *context;
	cairo_pattern_t *pattern;

	surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					      width, height);
	context = cairo_create (surface);
	rb_segmented_bar_render_segments (bar, context,
					  width, height, height/2);
	rb_segmented_bar_render_strokes (bar, context, width, height, height/2);
	pattern = cairo_pattern_create_for_surface (surface);
	cairo_surface_destroy (surface);
	cairo_destroy (context);

	return pattern;
}

static void rb_segmented_bar_render_labels (RBSegmentedBar *bar,
					    cairo_t *context)
{
	RBSegmentedBarPrivate *priv;
	PangoLayout *layout;
	Color text_color;
	GdkColor *gdk_color;
	int x = 0;
	GList *it;
	gboolean is_rtl = (gtk_widget_get_direction(GTK_WIDGET(bar)) == GTK_TEXT_DIR_RTL);

	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (bar));

	if (priv->segments == NULL) {
		return;
	}
	gdk_color = &GTK_WIDGET (bar)->style->fg[GTK_WIDGET_STATE (GTK_WIDGET (bar))];
	text_color.red = gdk_color->red / 65535.0;
	text_color.green = gdk_color->green / 65535.0;
	text_color.blue = gdk_color->blue / 65535.0;
	text_color.alpha = 1.0;
	layout = NULL;
	if (is_rtl) {
		x = priv->layout_width - priv->segment_box_size;
	}
	for (it = priv->segments; it != NULL; it = it->next) {
		cairo_pattern_t *grad;
		int layout_width;
		int layout_height;
		gchar *value_str;
		Segment *segment;

		segment = (Segment *)it->data;
		cairo_set_line_width (context, 1);
		cairo_rectangle (context, x + 0.5, 2 + 0.5, 
				 priv->segment_box_size - 1,
				 priv->segment_box_size - 1);
		grad = make_segment_gradient (priv->segment_box_size,
					      segment->color.red,
					      segment->color.green,
					      segment->color.blue,
					      segment->color.alpha);
		cairo_set_source (context, grad);
		cairo_fill_preserve (context);
		cairo_set_source_rgba (context, 0, 0, 0, 0.6);
		cairo_stroke (context);
		cairo_pattern_destroy (grad);

		if (is_rtl)
			x -= (segment->layout_width + priv->segment_box_spacing);
		else
			x += priv->segment_box_size + priv->segment_box_spacing;

		layout = create_adapt_layout (GTK_WIDGET (bar), layout,
					      FALSE, TRUE);
		pango_layout_set_text (layout, segment->label, -1);
		pango_layout_get_pixel_size (layout,
					     &layout_width, &layout_height);

		cairo_move_to (context, (is_rtl) ? x+((int)segment->layout_width - (int)layout_width) : x, 0);
		cairo_set_source_rgba (context,
				       text_color.red, text_color.green,
				       text_color.blue, 0.9);
		pango_cairo_show_layout (context, layout);
		cairo_fill (context);

		layout = create_adapt_layout (GTK_WIDGET (bar), layout,
					      TRUE, FALSE);
		g_assert (priv->value_formatter != NULL);
		value_str = priv->value_formatter (segment->percent,
						   priv->value_formatter_data);
		pango_layout_set_text (layout, value_str, -1);
		g_free (value_str);

		if (is_rtl && ((int)layout_width >= (int)segment->layout_width)) {
			int value_layout_width = layout_width;
			int value_layout_height = layout_height;
			pango_layout_get_pixel_size (layout,
					     &value_layout_width, &value_layout_height);
			cairo_move_to (context, x+(segment->layout_width-value_layout_width), layout_height);
		} else {
			cairo_move_to (context, x, layout_height);
		}
		cairo_set_source_rgba (context,
				       text_color.red, text_color.green,
				       text_color.blue, 0.75);
		pango_cairo_show_layout (context, layout);
		cairo_fill (context);

		if (is_rtl)
			x -= (priv->segment_box_size + priv->segment_label_spacing);
		else
			x += segment->layout_width + priv->segment_label_spacing;
	}
	g_object_unref (G_OBJECT (layout));
}

static gboolean
rb_segmented_bar_expose (GtkWidget *widget,
			 GdkEventExpose *event)
{
	RBSegmentedBar *bar;
	RBSegmentedBarPrivate *priv;
	cairo_t *context;
	cairo_pattern_t *bar_pattern;

	g_return_val_if_fail (RB_IS_SEGMENTED_BAR (widget), FALSE);
	if (GTK_WIDGET_DRAWABLE (widget) == FALSE) {
		return FALSE;
	}

	bar = RB_SEGMENTED_BAR (widget);
	priv = RB_SEGMENTED_BAR_GET_PRIVATE (bar);

	context = gdk_cairo_create (GDK_DRAWABLE (widget->window));
	
	if (priv->reflect) {
		cairo_push_group (context);
	}

	cairo_set_operator (context, CAIRO_OPERATOR_OVER);
	cairo_translate (context, widget->allocation.x + priv->h_padding,
			 widget->allocation.y);
	cairo_rectangle (context, 0, 0,
			 widget->allocation.width - priv->h_padding,
			 MAX (2*priv->bar_height, priv->bar_height + priv->bar_label_spacing + priv->layout_height));
	cairo_clip (context);

	bar_pattern = rb_segmented_bar_render (bar, 
					       widget->allocation.width - 2*priv->h_padding,
					       priv->bar_height);

	cairo_save (context);
	cairo_set_source (context, bar_pattern);
	cairo_paint (context);
	cairo_restore (context);

	if (priv->reflect) {
		cairo_matrix_t matrix;
		cairo_pattern_t *mask;

		cairo_save (context);

		cairo_rectangle (context, 0, priv->bar_height,
				 widget->allocation.width - priv->h_padding,
				 priv->bar_height);
		cairo_clip (context);
		cairo_matrix_init_scale (&matrix, 1, -1);
		cairo_matrix_translate (&matrix, 0, -(2*priv->bar_height) + 1);
		cairo_transform (context, &matrix);

		cairo_set_source (context, bar_pattern);

		mask = cairo_pattern_create_linear (0, 0, 0, priv->bar_height);
		cairo_pattern_add_color_stop_rgba (mask, 0.25, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba (mask, 0.5, 0, 0, 0, 0.125);
		cairo_pattern_add_color_stop_rgba (mask, 0.75, 0, 0, 0, 0.4);
		cairo_pattern_add_color_stop_rgba (mask, 1.0, 0, 0, 0, 0.7);

		cairo_mask (context, mask);
		cairo_pattern_destroy (mask);

		cairo_restore (context);

		cairo_pop_group_to_source (context);
		cairo_paint (context);
	}

	if (priv->show_labels) {
		if (priv->reflect) {
			cairo_translate (context,
					 widget->allocation.x + (widget->allocation.width - priv->layout_width)/2,
					 widget->allocation.y + priv->bar_height + priv->bar_label_spacing);
		} else {
			cairo_translate (context,
					 -priv->h_padding + (widget->allocation.width - priv->layout_width)/2,
					 priv->bar_height + priv->bar_label_spacing);
		}
		rb_segmented_bar_render_labels (bar, context);
	}
	cairo_pattern_destroy (bar_pattern);
	cairo_destroy (context);

	return TRUE;
}

GtkWidget *rb_segmented_bar_new (void)
{
	return g_object_new (RB_TYPE_SEGMENTED_BAR, NULL);
}

void rb_segmented_bar_set_value_formatter (RBSegmentedBar *bar,
					   RBSegmentedBarValueFormatter formatter,
					   gpointer data)
{
	RBSegmentedBarPrivate *priv;

	priv = RB_SEGMENTED_BAR_GET_PRIVATE (RB_SEGMENTED_BAR (bar));

	priv->value_formatter = formatter;
	priv->value_formatter_data = data;
}
