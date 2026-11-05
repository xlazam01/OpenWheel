"""Write LCSC part numbers (from docs/bom/electronics.csv) into the KiCad schematic, and set DNP.

Usage: python -I tools/set_lcsc.py hw/filter_wheel.kicad_sch docs/bom/electronics.csv [--dnp J3,J4]
KiCad must be closed. Adds or updates a hidden "LCSC" property on every placed symbol whose
reference is in the CSV, next to its Footprint property. Line endings and tabs are preserved.
"""
import argparse
import csv
import re

ap = argparse.ArgumentParser()
ap.add_argument("sch")
ap.add_argument("csv")
ap.add_argument("--dnp", default="")
a = ap.parse_args()

lcsc = {}
with open(a.csv, encoding="utf-8") as f:
    for r in csv.DictReader(f):
        for ref in r["Designator"].split(","):
            lcsc[ref.strip()] = r["LCSC"].strip()
dnp = {x for x in a.dnp.split(",") if x}

text = open(a.sch, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)

# placed symbols are top-level "(symbol" blocks at one tab of indentation that have a lib_id
out, i, done, dnp_done = [], 0, set(), set()
while i < len(lines):
    line = lines[i]
    if line == "\t(symbol" and i + 1 < len(lines) and lines[i + 1].startswith("\t\t(lib_id"):
        j = i + 1
        while lines[j] != "\t)":
            j += 1
        block = lines[i:j + 1]
        ref = next((re.search(r'"Reference" "([^"]+)"', l).group(1) for l in block
                    if l.startswith('\t\t(property "Reference"')), None)
        if ref in dnp:
            block = [("\t\t(dnp yes)" if l == "\t\t(dnp no)" else l) for l in block]
            dnp_done.add(ref)
        if ref in lcsc:
            k = next((n for n, l in enumerate(block) if l.startswith('\t\t(property "LCSC"')), None)
            if k is not None:
                block[k] = f'\t\t(property "LCSC" "{lcsc[ref]}"'
            else:
                fp = next(n for n, l in enumerate(block) if l.startswith('\t\t(property "Footprint"'))
                end = fp + 1
                while block[end] != "\t\t)":
                    end += 1
                prop = [f'\t\t(property "LCSC" "{lcsc[ref]}"'] + block[fp + 1:end + 1]
                prop = [l for l in prop if l.strip() != "(hide yes)"]
                prop.insert(2, "\t\t\t(hide yes)")
                block = block[:end + 1] + prop + block[end + 1:]
            done.add(ref)
        out += block
        i = j + 1
        continue
    out.append(line)
    i += 1

open(a.sch, "w", encoding="utf-8", newline="").write(nl.join(out))
missing = sorted(set(lcsc) - done)
print(f"LCSC set on {len(done)} symbols; DNP set on {sorted(dnp_done)}; not found: {missing}")
