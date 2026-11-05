"""Compose IKEA-style build-manual pages (A4 portrait SVG) from SolidCraft view DXFs.

Usage: python page.py manual.json OUTDIR [--pdf manual.pdf] [--png DPI]

manual.json: {"title": "...", "pages": [page, ...]}; each page:
{
  "step": 7, "title": "Standoffs",
  "parts": [{"dxf": "part_m3x8.dxf", "qty": 8, "id": "B2", "label": "M3×8", "true_scale": true},
            {"qty": 10, "id": "B11", "label": "magnet Ø6×3"}],          # no dxf: label only
  "main": "p07.dxf"  or  [{"dxf": "a.dxf"}, {"dxf": "b.dxf", "dx": 0, "dy": -60}],  # layers, offsets in drawing mm
  "crop": [0, 0.4, 0.6, 1],        # optional window, fractions of the drawing (x from left, y from top)
  "note": "one or two short lines",
  "checks": [{"ok": true, "text": "..."}, {"ok": false, "text": "..."}]
}
DXF paths are relative to the JSON file. Drawings: SolidCraft file.export_view_dxf (see BUILD.md).
"""
import argparse
import html
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dxf2svg import read_entities  # noqa: E402

PAGE_W, PAGE_H, M = 210.0, 297.0, 12.0   # mm
FONT = "'Segoe UI', 'Helvetica Neue', Arial, sans-serif"
INKSCAPE = r"C:\Program Files\Inkscape\bin\inkscape.exe"
PART_SCALE_CAP = 1.5   # parts are drawn at most 1.5:1


def load(path, dx=0.0, dy=0.0):
    return [[(p[0] + dx, p[1] + dy) for p in pts] for _, _, pts in read_entities(path)]


def bbox(polys):
    xs = [p[0] for pts in polys for p in pts]
    ys = [p[1] for pts in polys for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def drawing(polys, x, y, w, h, stroke=0.3, scale=None, crop=None, clip_id=None):
    """Fit polylines (drawing mm) into the page box (x, y, w, h). Returns svg."""
    x0, x1, y0, y1 = bbox(polys)
    if crop:
        fx0, fy0, fx1, fy1 = crop
        x0, x1 = x0 + fx0 * (x1 - x0), x0 + fx1 * (x1 - x0)
        y0, y1 = y1 - fy1 * (y1 - y0), y1 - fy0 * (y1 - y0)
    s = scale or min(w / (x1 - x0), h / (y1 - y0))
    ox = x + (w - (x1 - x0) * s) / 2
    oy = y + (h - (y1 - y0) * s) / 2
    d = " ".join("M" + " L".join(f"{ox + (p[0] - x0) * s:.2f},{oy + (y1 - p[1]) * s:.2f}" for p in pts)
                 for pts in polys)
    path = f'<path class="ln" stroke-width="{stroke}" d="{d}"/>'
    if crop and clip_id:
        cw, ch = (x1 - x0) * s, (y1 - y0) * s
        return (f'<clipPath id="{clip_id}"><rect x="{ox:.2f}" y="{oy:.2f}" width="{cw:.2f}" height="{ch:.2f}"/></clipPath>'
                f'<g clip-path="url(#{clip_id})">{path}</g>')
    return path


def check_icon(cx, cy, ok, r=4.2):
    if ok:
        return (f'<circle cx="{cx}" cy="{cy}" r="{r}" class="ok"/>'
                f'<path d="M{cx - 2},{cy} l1.5,1.7 l3,-3.6" class="mark"/>')
    return (f'<circle cx="{cx}" cy="{cy}" r="{r}" class="bad"/>'
            f'<path d="M{cx - 1.8},{cy - 1.8} l3.6,3.6 M{cx + 1.8},{cy - 1.8} l-3.6,3.6" class="mark"/>')


def text_lines(x, y, s, cls, width_chars, lh):
    """Wrap s into lines of about width_chars; returns (svg, height)."""
    words, lines, cur = s.split(), [], ""
    for w in words:
        if len(cur) + len(w) + 1 > width_chars and cur:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    if cur:
        lines.append(cur)
    out = "".join(f'<text x="{x}" y="{y + i * lh:.2f}" class="{cls}">{html.escape(l)}</text>'
                  for i, l in enumerate(lines))
    return out, len(lines) * lh


STYLE = f"""
  .ln {{ fill: none; stroke: #1a1a1a; stroke-linecap: round; stroke-linejoin: round; }}
  text {{ font-family: {FONT}; fill: #1a1a1a; }}
  .step {{ font-size: 22px; font-weight: 800; }}
  .title {{ font-size: 6px; fill: #555; }}
  .box {{ fill: none; stroke: #1a1a1a; stroke-width: 0.5; }}
  .qty {{ font-size: 6.5px; font-weight: 800; text-anchor: middle; }}
  .pid {{ font-size: 2.6px; fill: #666; text-anchor: middle; }}
  .tag {{ font-size: 4px; fill: #444; text-anchor: middle; }}
  .scale {{ font-size: 2.4px; fill: #666; text-anchor: middle; }}
  .note {{ font-size: 3.6px; fill: #333; }}
  .chk {{ fill: #f4f4f2; stroke: none; }}
  .ctext {{ font-size: 3.4px; }}
  .ok {{ fill: #2e7d32; }} .bad {{ fill: #c62828; }}
  .mark {{ fill: none; stroke: #fff; stroke-width: 1.1; stroke-linecap: round; stroke-linejoin: round; }}
  .foot {{ font-size: 2.6px; fill: #999; }}
  .cover {{ font-size: 16px; font-weight: 800; }}
  .sub {{ font-size: 5px; fill: #555; }}"""


def svg_doc(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{PAGE_W}mm" height="{PAGE_H}mm" '
            f'viewBox="0 0 {PAGE_W} {PAGE_H}"><style>{STYLE}</style>'
            f'<rect width="{PAGE_W}" height="{PAGE_H}" fill="#fff"/>{body}</svg>')


def main_polys(spec, base):
    main = spec["main"]
    layers = [{"dxf": main}] if isinstance(main, str) else main
    polys = []
    for l in layers:
        polys += load(os.path.join(base, l["dxf"]), l.get("dx", 0), l.get("dy", 0))
    return polys


def build_page(spec, base, n_total, doc_title):
    out = []
    out.append(f'<text x="{M}" y="{M + 16}" class="step">{spec["step"]}</text>')
    if spec.get("title"):
        out.append(f'<text x="{M}" y="{M + 23}" class="title">{html.escape(spec["title"])}</text>')

    parts = spec.get("parts", [])
    top = M + 28
    if parts:
        avail = PAGE_W - 2 * M - 42
        cell_w = min(34.0, (avail - 6) / len(parts))
        cell_h = 38.0
        box_w = cell_w * len(parts) + 6
        bx, by = PAGE_W - M - box_w, M
        out.append(f'<rect x="{bx}" y="{by}" width="{box_w}" height="{cell_h + 6}" rx="4" class="box"/>')
        for i, p in enumerate(parts):
            cx = bx + 3 + i * cell_w
            if p.get("dxf"):
                polys = load(os.path.join(base, p["dxf"]))
                x0, x1, y0, y1 = bbox(polys)
                fit = min((cell_w - 6) / (x1 - x0), (cell_h - 18) / (y1 - y0))
                scale = 1.0 if p.get("true_scale") else min(fit, PART_SCALE_CAP)
                out.append(drawing(polys, cx + 3, by + 3, cell_w - 6, cell_h - 18, stroke=0.25, scale=scale))
                if p.get("true_scale"):
                    out.append(f'<text x="{cx + cell_w / 2}" y="{by + cell_h - 13}" class="scale">1:1</text>')
            else:
                out.append(f'<text x="{cx + cell_w / 2}" y="{by + (cell_h - 12) / 2}" class="tag">{html.escape(p.get("tag", p.get("label", "")))}</text>')
            out.append(f'<text x="{cx + cell_w / 2}" y="{by + cell_h - 4.5}" class="qty">{p["qty"]}×</text>')
            out.append(f'<text x="{cx + cell_w / 2}" y="{by + cell_h + 1.8}" class="pid">{p["id"]} · {html.escape(p.get("label", ""))}</text>')
        top = by + cell_h + 12

    # footer area: note + checks
    checks = spec.get("checks", [])
    note_svg, note_h = ("", 0)
    if spec.get("note"):
        note_svg, note_h = text_lines(M, 0, spec["note"], "note", 95, 5)
    bottom = PAGE_H - M - 6 - (22 if checks else 0) - (note_h + 3 if note_h else 0)

    out.append(drawing(main_polys(spec, base), M, top, PAGE_W - 2 * M, bottom - top,
                       stroke=0.3, crop=spec.get("crop"), clip_id=f"c{spec['step']}"))

    y = bottom + 4
    if note_svg:
        out.append(f'<g transform="translate(0,{y + 3})">{note_svg}</g>')
        y += note_h + 3
    if checks:
        cw = (PAGE_W - 2 * M) / len(checks)
        for i, c in enumerate(checks):
            x = M + i * cw
            out.append(f'<rect x="{x}" y="{y}" width="{cw - 4}" height="18" rx="3" class="chk"/>')
            out.append(check_icon(x + 8, y + 9, c["ok"]))
            t, th = text_lines(x + 15, 0, c["text"], "ctext", int((cw - 22) / 1.75), 4.2)
            out.append(f'<g transform="translate(0,{y + 9.8 - (th - 4.2) / 2})">{t}</g>')

    out.append(f'<text x="{M}" y="{PAGE_H - 5}" class="foot">{html.escape(doc_title)}</text>')
    out.append(f'<text x="{PAGE_W - M}" y="{PAGE_H - 5}" class="foot" text-anchor="end">'
               f'{spec["step"]} / {n_total}</text>')
    return svg_doc("".join(out))


def build_cover(spec, base, doc_title):
    out = [f'<text x="{M}" y="{M + 20}" class="cover">{html.escape(spec["title"])}</text>',
           f'<text x="{M}" y="{M + 29}" class="sub">{html.escape(spec.get("subtitle", ""))}</text>']
    out.append(drawing(main_polys(spec, base), M, M + 40, PAGE_W - 2 * M, PAGE_H - 2 * M - 70, stroke=0.3))
    if spec.get("note"):
        t, _ = text_lines(M, PAGE_H - M - 18, spec["note"], "note", 95, 5)
        out.append(t)
    out.append(f'<text x="{M}" y="{PAGE_H - 5}" class="foot">{html.escape(doc_title)}</text>')
    return svg_doc("".join(out))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("outdir")
    ap.add_argument("--pdf")
    ap.add_argument("--png", type=int, default=0, help="also write PNGs at this DPI")
    a = ap.parse_args()
    base = os.path.dirname(os.path.abspath(a.spec))
    with open(a.spec, encoding="utf-8") as f:
        doc = json.load(f)
    os.makedirs(a.outdir, exist_ok=True)
    steps = [p for p in doc["pages"] if "step" in p]
    files = []
    for i, p in enumerate(doc["pages"]):
        name = f"page{p['step']:02d}" if "step" in p else "page00"
        svg = build_page(p, base, len(steps), doc["title"]) if "step" in p else build_cover(p, base, doc["title"])
        fn = os.path.join(a.outdir, name + ".svg")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(svg)
        files.append(fn)
    print(len(files), "pages")
    pdfs = []
    for fn in files:
        if a.pdf:
            pdf = fn[:-4] + ".pdf"
            subprocess.run([INKSCAPE, fn, "--export-type=pdf", f"--export-filename={pdf}"],
                           check=True, capture_output=True)
            pdfs.append(pdf)
        if a.png:
            subprocess.run([INKSCAPE, fn, "--export-type=png", f"--export-filename={fn[:-4]}.png",
                            f"--export-dpi={a.png}"], check=True, capture_output=True)
    if a.pdf:
        from pypdf import PdfWriter
        w = PdfWriter()
        for p in pdfs:
            w.append(p)
        w.write(a.pdf)
        for p in pdfs:
            os.remove(p)
        print("wrote", a.pdf)
