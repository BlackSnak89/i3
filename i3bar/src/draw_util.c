/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2015 Ingo Bürk and contributors (see also: LICENSE)
 *
 * draw.c: Utility for drawing.
 *
 */
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#ifdef I3BAR_CAIRO
#include <cairo/cairo-xcb.h>
#endif

#include "common.h"
#include "libi3.h"

xcb_connection_t *xcb_connection;
xcb_visualtype_t *visual_type;

/* Forward declarations */
static void draw_util_set_source_color(surface_t *surface, color_t color);

/*
 * Initialize the surface to represent the given drawable.
 *
 */
void draw_util_surface_init(surface_t *surface, xcb_drawable_t drawable, int width, int height) {
    surface->id = drawable;
    surface->width = width;
    surface->height = height;

    surface->gc = xcb_generate_id(xcb_connection);
    xcb_void_cookie_t gc_cookie = xcb_create_gc_checked(xcb_connection, surface->gc, surface->id, 0, NULL);
    if (xcb_request_failed(gc_cookie, "Could not create graphical context"))
        exit(EXIT_FAILURE);

#ifdef I3BAR_CAIRO
    surface->surface = cairo_xcb_surface_create(xcb_connection, surface->id, visual_type, width, height);
    surface->cr = cairo_create(surface->surface);
#endif
}

/*
 * Destroys the surface.
 *
 */
void draw_util_surface_free(surface_t *surface) {
    xcb_free_gc(xcb_connection, surface->gc);
#ifdef I3BAR_CAIRO
    cairo_surface_destroy(surface->surface);
    cairo_destroy(surface->cr);
#endif
}

/*
 * Parses the given color in hex format to an internal color representation.
 * Note that the input must begin with a hash sign, e.g., "#3fbc59".
 *
 */
color_t draw_util_hex_to_color(const char *color) {
    char alpha[2];
    if (strlen(color) == strlen("#rrggbbaa")) {
        alpha[0] = color[7];
        alpha[1] = color[8];
    } else {
        alpha[0] = alpha[1] = 'F';
    }

    char groups[4][3] = {
        {color[1], color[2], '\0'},
        {color[3], color[4], '\0'},
        {color[5], color[6], '\0'},
        {alpha[0], alpha[1], '\0'}};

    return (color_t){
        .red = strtol(groups[0], NULL, 16) / 255.0,
        .green = strtol(groups[1], NULL, 16) / 255.0,
        .blue = strtol(groups[2], NULL, 16) / 255.0,
        .alpha = strtol(groups[3], NULL, 16) / 255.0,
        .colorpixel = get_colorpixel(color)};
}

/*
 * Set the given color as the source color on the surface.
 *
 */
static void draw_util_set_source_color(surface_t *surface, color_t color) {
#ifdef I3BAR_CAIRO
    cairo_set_source_rgba(surface->cr, color.red, color.green, color.blue, color.alpha);
#else
    uint32_t colorpixel = color.colorpixel;
    xcb_change_gc(xcb_connection, surface->gc, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                  (uint32_t[]){colorpixel, colorpixel});
#endif
}

/**
 * Draw the given text using libi3.
 * This function also marks the surface dirty which is needed if other means of
 * drawing are used. This will be the case when using XCB to draw text.
 *
 */
void draw_util_text(i3String *text, surface_t *surface, color_t fg_color, color_t bg_color, int x, int y, int max_width) {
#ifdef I3BAR_CAIRO
    /* Flush any changes before we draw the text as this might use XCB directly. */
    CAIRO_SURFACE_FLUSH(surface->surface);
#endif

    set_font_colors(surface->gc, fg_color.colorpixel, bg_color.colorpixel);
    draw_text(text, surface->id, surface->gc, visual_type, x, y, max_width);

#ifdef I3BAR_CAIRO
    /* Notify cairo that we (possibly) used another way to draw on the surface. */
    cairo_surface_mark_dirty(surface->surface);
#endif
}

/**
 * Draws a filled rectangle.
 * This function is a convenience wrapper and takes care of flushing the
 * surface as well as restoring the cairo state.
 *
 */
void draw_util_rectangle(surface_t *surface, color_t color, double x, double y, double w, double h) {
#ifdef I3BAR_CAIRO
    cairo_save(surface->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);
    draw_util_set_source_color(surface, color);

    cairo_rectangle(surface->cr, x, y, w, h);
    cairo_fill(surface->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(surface->surface);

    cairo_restore(surface->cr);
#else
    draw_util_set_source_color(surface, color);

    xcb_rectangle_t rect = {x, y, w, h};
    xcb_poly_fill_rectangle(xcb_connection, surface->id, surface->gc, 1, &rect);
#endif
}

/**
 * Clears a surface with the given color.
 *
 */
void draw_util_clear_surface(surface_t *surface, color_t color) {
#ifdef I3BAR_CAIRO
    cairo_save(surface->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);
    draw_util_set_source_color(surface, color);

    cairo_paint(surface->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(surface->surface);

    cairo_restore(surface->cr);
#else
    draw_util_set_source_color(surface, color);

    xcb_rectangle_t rect = {0, 0, surface->width, surface->height};
    xcb_poly_fill_rectangle(xcb_connection, surface->id, surface->gc, 1, &rect);
#endif
}

/**
 * Copies a surface onto another surface.
 *
 */
void draw_util_copy_surface(surface_t *src, surface_t *dest, double src_x, double src_y,
                            double dest_x, double dest_y, double width, double height) {
#ifdef I3BAR_CAIRO
    cairo_save(dest->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(dest->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(dest->cr, src->surface, dest_x - src_x, src_y);

    cairo_rectangle(dest->cr, dest_x, dest_y, width, height);
    cairo_fill(dest->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(src->surface);
    CAIRO_SURFACE_FLUSH(dest->surface);

    cairo_restore(dest->cr);
#else
    xcb_copy_area(xcb_connection, src->id, dest->id, dest->gc, (int16_t)src_x, (int16_t)src_y,
                  (int16_t)dest_x, (int16_t)dest_y, (uint16_t)width, (uint16_t)height);
#endif
}