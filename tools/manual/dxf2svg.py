"""Convert a SolidCraft view DXF (LINE / LWPOLYLINE on VISIBLE / HIDDEN layers) into a line-art SVG.

Usage: python dxf2svg.py in.dxf out.svg [--width 800] [--stroke 1.2] [--margin 10]
The drawing is scaled to the given pixel width; hidden lines (if exported) are drawn dashed.
"""
import argparse


def read_entities(path):
    """Yield (kind, layer, points) for LINE and LWPOLYLINE entities."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [l.strip() for l in f]
    pairs = list(zip(lines[0::2], lines[1::2]))
    i = 0
    while i < len(pairs):
        code, val = pairs[i]
        if code == "0" and val in ("LINE", "LWPOLYLINE"):
            kind, layer, pts, closed = val, "0", [], False
            x = None
            i += 1
            while i < len(pairs) and pairs[i][0] != "0":
                c, v = pairs[i]
                if c == "8":
                    layer = v
                elif c in ("10", "11"):
                    x = float(v)
                elif c in ("20", "21"):
                    pts.append((x, float(v)))
                elif c == "70" and kind == "LWPOLYLINE":
                    closed = int(v) & 1 == 1
                i += 1
            if closed and pts:
                pts.append(pts[0])
            if len(pts) >= 2:
                yield kind, layer, pts
            continue
        i += 1


def convert(src, dst, width=800, stroke=1.2, margin=10):
    ents = list(read_entities(src))
    xs = [p[0] for _, _, pts in ents for p in pts]
    ys = [p[1] for _, _, pts in ents for p in pts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    s = (width - 2 * margin) / (x1 - x0)
    height = (y1 - y0) * s + 2 * margin
    tx = lambda p: f"{(p[0] - x0) * s + margin:.2f},{(y1 - p[1]) * s + margin:.2f}"
    vis, hid = [], []
    for _, layer, pts in ents:
        d = "M" + " L".join(tx(p) for p in pts)
        (hid if layer.upper().startswith("HIDDEN") else vis).append(d)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height:.0f}" '
           f'viewBox="0 0 {width} {height:.0f}">',
           f'<g fill="none" stroke="#111" stroke-linecap="round" stroke-linejoin="round">',
           f'<path stroke-width="{stroke}" d="{" ".join(vis)}"/>']
    if hid:
        out.append(f'<path stroke-width="{stroke * 0.6}" stroke-dasharray="3 2" stroke="#777" d="{" ".join(hid)}"/>')
    out += ["</g>", "</svg>"]
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    return len(vis), len(hid), width, height, 1 / s


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--width", type=int, default=800)
    ap.add_argument("--stroke", type=float, default=1.2)
    ap.add_argument("--margin", type=int, default=10)
    a = ap.parse_args()
    v, h, w, ht, mm_per_px = convert(a.src, a.dst, a.width, a.stroke, a.margin)
    print(f"{v} visible, {h} hidden paths -> {w}x{ht:.0f} px ({mm_per_px:.3f} mm/px)")
