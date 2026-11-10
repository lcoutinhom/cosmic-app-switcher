#!/usr/bin/python3
"""Render immutable ARGB frames for the native Wayland application switcher."""
import math
import sys

import cairo
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gio, Gtk


def rounded(ctx, x, y, width, height, radius):
    ctx.new_sub_path()
    for cx, cy, start in ((x + width - radius, y + radius, -90),
                          (x + width - radius, y + height - radius, 0),
                          (x + radius, y + height - radius, 90),
                          (x + radius, y + radius, 180)):
        ctx.arc(cx, cy, radius, math.radians(start), math.radians(start + 90))
    ctx.close_path()


def label(ctx, text, x, y, width, size=11, color=(0.95, 0.95, 0.97)):
    ctx.select_font_face('Sans', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    ctx.set_font_size(size * 1.33)
    if ctx.text_extents(text).width > width:
        while text and ctx.text_extents(text + '…').width > width:
            text = text[:-1]
        text += '…'
    extent = ctx.text_extents(text)
    ctx.set_source_rgb(*color)
    ctx.move_to(x + (width - extent.width) / 2 - extent.x_bearing, y + size * 1.33)
    ctx.show_text(text)


def icon_surface(pixbuf):
    width, height = pixbuf.get_width(), pixbuf.get_height()
    pixels = pixbuf.get_pixels()
    channels, stride = pixbuf.get_n_channels(), pixbuf.get_rowstride()
    data = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            src, dest = y * stride + x * channels, (y * width + x) * 4
            r, g, b = pixels[src:src + 3]
            a = pixels[src + 3] if channels == 4 else 255
            word = (a << 24) | ((r * a // 255) << 16) | ((g * a // 255) << 8) | (b * a // 255)
            data[dest:dest + 4] = word.to_bytes(4, sys.byteorder)
    return cairo.ImageSurface.create_for_data(data, cairo.FORMAT_ARGB32,
                                               width, height, width * 4)


def app_info(app_id, theme, applications):
    app = applications.get(app_id.casefold())
    name = app.get_display_name() if app else app_id
    icon = app.get_icon() if app else Gio.ThemedIcon.new('application-x-executable')
    pixbuf = None
    try:
        info = theme.lookup_by_gicon(icon, 56, Gtk.IconLookupFlags.FORCE_SIZE)
        if info:
            pixbuf = icon_surface(info.load_icon())
    except Exception:
        pass
    return name, pixbuf


def render(output, width, height, ids, preview=False):
    theme = Gtk.IconTheme.new()
    theme.set_custom_theme('Pop')
    apps = {(app.get_id() or '').removesuffix('.desktop').casefold(): app
            for app in Gio.AppInfo.get_all()}
    for app in Gio.AppInfo.get_all():
        if isinstance(app, Gio.DesktopAppInfo):
            wmclass = app.get_startup_wm_class()
            if wmclass:
                apps.setdefault(wmclass.casefold(), app)
    entries = [app_info(app_id, theme, apps) for app_id in ids]
    with open(output, 'wb') as stream:
        for selected in range(len(ids)):
            surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
            ctx = cairo.Context(surface)
            rounded(ctx, 2, 2, width - 4, height - 4, 22)
            ctx.set_source_rgba(0.10, 0.11, 0.14, 0.98)
            ctx.fill_preserve()
            ctx.set_source_rgba(1, 1, 1, 0.13)
            ctx.set_line_width(1)
            ctx.stroke()
            visible = min(len(ids), 7)
            start = min(max(0, selected - visible // 2), len(ids) - visible)
            for slot, index in enumerate(range(start, start + visible)):
                x = 24 + slot * 112
                if index == selected:
                    rounded(ctx, x + 3, 24, 106, 116, 14)
                    ctx.set_source_rgba(0.27, 0.57, 0.94, 0.25)
                    ctx.fill_preserve()
                    ctx.set_source_rgb(0.42, 0.72, 1)
                    ctx.set_line_width(2)
                    ctx.stroke()
                name, pixbuf = entries[index]
                if pixbuf:
                    ctx.set_source_surface(pixbuf, x + 28, 40)
                    ctx.paint()
                else:
                    label(ctx, name[:1].upper(), x, 42, 112, 30)
                label(ctx, name, x + 6, 108, 100, 10)
            name = entries[selected][0]
            label(ctx, f'{name}   ·   {selected + 1}/{len(ids)}', 24, 151,
                  width - 48, 11)
            label(ctx, 'Solte Alt para abrir · Esc para cancelar', 24, 178,
                  width - 48, 9, (0.63, 0.65, 0.70))
            surface.flush()
            if preview:
                surface.write_to_png(output + '.png')
                break
            stream.write(surface.get_data())


if __name__ == '__main__':
    preview = sys.argv[1] == '--preview'
    args = sys.argv[2:] if preview else sys.argv[1:]
    render(args[0], int(args[1]), int(args[2]), args[3:], preview=preview)
