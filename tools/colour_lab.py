"""Colour helpers for the art gates (field_sprite_doll.py and field_texture_redraw.py ingest, xg-jqe8): CIE Lab and a
colour's name in plain words, for the lines that tell the image model what it changed."""
import numpy as np


def lab(rgb):
    """sRGB (..., 3) 0-255 -> CIE Lab (D65)."""
    c = np.asarray(rgb, float) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    xyz = c @ np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]]).T
    xyz = xyz / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def colour_name(rgb):
    """A colour in plain words (the RGB beside it is the authority): 'white', 'dark brown', 'cream', 'navy'."""
    L, a, b = (float(v) for v in lab(np.array(rgb, float)))
    ch = float(np.hypot(a, b)); h = float(np.degrees(np.arctan2(b, a)) % 360)
    if ch < 10:
        tone = 'black' if L < 15 else 'dark grey' if L < 40 else 'grey' if L < 65 else 'light grey' if L < 88 else 'white'
        if L >= 15 and ch >= 5:
            tone = ('greenish ' if 100 <= h < 200 else 'bluish ' if 200 <= h < 330 else 'warm ') + tone
        return tone
    for top, hue in ((20, 'pink'), (50, 'red'), (75, 'orange'), (95, 'gold'), (115, 'yellow'), (130, 'yellow-green'),
                     (170, 'green'), (230, 'teal'), (275, 'sky blue'), (315, 'blue'), (345, 'purple'), (361, 'pink')):
        if h < top: break
    if hue in ('orange', 'gold', 'yellow') and ch < 30:
        return 'cream' if L > 85 else 'beige' if L > 55 else 'dark brown' if L < 30 else 'brown'
    if ch < 22:   # greyed colours
        grey = {'pink': 'mauve', 'red': 'mauve', 'purple': 'lavender grey', 'blue': 'slate', 'sky blue': 'slate',
                'teal': 'slate', 'green': 'sage', 'yellow-green': 'sage', 'orange': 'beige', 'gold': 'khaki'}.get(hue, hue)
        return ('dark ' if L < 30 else 'light ' if L > 75 else '') + grey
    if hue in ('orange', 'gold') and L > 70 and ch < 50: return 'peach' if L > 82 else 'tan'
    if hue in ('orange', 'gold') and L < 55: hue = 'rust' if ch > 45 and L > 35 else 'brown'
    if hue in ('pink', 'red') and L < 45: hue = 'maroon'
    if hue in ('yellow', 'yellow-green') and L < 50: hue = 'olive'
    if hue == 'blue' and L < 35: hue = 'navy'
    mod = 'dark ' if L < 30 and hue not in ('navy', 'maroon') else 'pale ' if L > 75 and ch < 35 else 'light ' if L > 75 else 'vivid ' if ch > 60 else ''
    return mod + hue


HUES = ((20, 'pink'), (45, 'red'), (70, 'orange'), (90, 'gold'), (105, 'yellow'), (125, 'yellow-green'), (160, 'green'),
        (200, 'teal'), (240, 'cyan'), (275, 'sky blue'), (300, 'blue'), (330, 'violet'), (361, 'pink'))


def shift_words(was, now):
    """How a colour moved, in words: 'lighter, more saturated, toward blue' ('' when it barely moved)."""
    a, b = lab(np.array(was, float)), lab(np.array(now, float))
    out = []
    if b[0] - a[0] > 5: out.append('lighter')
    elif b[0] - a[0] < -5: out.append('darker')
    ca, cb = float(np.hypot(a[1], a[2])), float(np.hypot(b[1], b[2]))
    if cb - ca > 6: out.append('more saturated')
    elif cb - ca < -6: out.append('duller')
    if ca > 10 and cb > 10:
        ha, hb = (float(np.degrees(np.arctan2(v[2], v[1]))) % 360 for v in (a, b))
        dh = (hb - ha + 180) % 360 - 180
        if abs(dh) > 8:
            out.append('toward ' + next(n for top, n in HUES if hb < top))
    return ', '.join(out)
