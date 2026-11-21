#!/bin/bash
# Pack a fully drawn glyph sheet batch (tools/glyph_batch.py) onto the installed glyph library and install the result
# in every PC build that has a glyph-packs link (xg-g0ra.2; the art conveyor runs this when the batch is drawn).
#
#   bash tools/ship_glyphs.sh NAME [--no-install]
#
# The new library is work/art/glyph-set/<NAME>-pack<N> (the next free N), a copy of the installed one with the batch's
# cells packed by tools/glyph_sheet_pack.py (batch.json `pack` flags) into both version folders (Japanese text on the
# US build reads the US one); provenance-sheets.json keeps the base's record and chain, and batches.txt lists every
# batch the library holds (tools/art_status.py reads it). The install swaps each build's glyph-packs link in one rename; the library it
# replaced stays where it is.
set -euo pipefail
cd "$(dirname "$0")/.."
NAME=${1:?usage: ship_glyphs.sh NAME [--no-install]}; INSTALL=1; [[ ${2:-} == --no-install ]] && INSTALL=0
B=work/art/glyph-set/$NAME
PY=${PY:-.venv/bin/python}
[[ -f $B/batch.json ]] || { echo "ship_glyphs: $B/batch.json missing" >&2; exit 1; }
DRAWN=$($PY tools/glyph_batch.py status --dir "$B")
echo "$DRAWN"
[[ $DRAWN =~ :\ ([0-9]+)/([0-9]+)\ sheets ]] && [[ ${BASH_REMATCH[1]} == "${BASH_REMATCH[2]}" ]] || { echo "ship_glyphs: $NAME is not fully drawn" >&2; exit 1; }
BASE=$(readlink -f build/pc/enhanced/glyph-packs)
[[ -d $BASE ]] || { echo "ship_glyphs: no installed glyph library (build/pc/enhanced/glyph-packs)" >&2; exit 1; }
N=1; while [[ -e $B-pack$N ]]; do N=$((N + 1)); done
OUT=$B-pack$N
readarray -t FLAGS < <($PY -c 'import json,sys; [print(f) for f in json.load(open(sys.argv[1]))["pack"]]' "$B/batch.json")
DISC=$($PY -c 'import json,sys; print(json.load(open(sys.argv[1]))["disc"])' "$B/batch.json")
TITLE=$($PY -c 'import json,sys; print(json.load(open(sys.argv[1]))["title"])' "$B/batch.json")
$PY tools/glyph_sheet_pack.py "$DISC" --prepared "$B/prepared" --masters "$B/drawn" --base "$BASE" --out "$OUT" \
    --check "$OUT-check" --name "$TITLE" "${FLAGS[@]}"
$PY - "$B" "$BASE" "$OUT" "$NAME" <<'PY'
import json, sys
from pathlib import Path
batch, base, out, name = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
# the packer writes only this batch's glyphs: keep the base library's record of every other one, and its chain
old = json.loads((base / 'provenance-sheets.json').read_text()) if (base / 'provenance-sheets.json').exists() else {}
new = json.loads((out / 'provenance-sheets.json').read_text())
mine = {g['key'] for g in new['glyphs']}
glyphs = [g for g in old.get('glyphs', []) if g['key'] not in mine] + new['glyphs']
chain = old.get('chain', []) + ([old['base']] if old.get('base') else []) + [str(base)]
(out / 'provenance-sheets.json').write_text(json.dumps(dict(format=1, base=str(base), chain=chain, glyphs=glyphs), indent=1) + '\n')
held = (base / 'batches.txt').read_text().split() if (base / 'batches.txt').exists() else []
(out / 'batches.txt').write_text('\n'.join(held + [name]) + '\n')
print(f'ship_glyphs: {len(mine)} glyphs packed (both version folders), {len(glyphs)} in the record')
PY
# batch.json `faithful_fill`: what the disc's fonts still lack after the drawn sheets (its coloured battle word art,
# which glyph_prep leaves out, as the English round did) is built by tools/glyph_art_pack.py --faithful-cel, the method
# the English counterparts were made with, and copied in where the library has no picture (xg-g0ra.3)
if $PY -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("faithful_fill") else 1)' "$B/batch.json"; then
    rm -rf "$OUT-faithful"
    $PY tools/glyph_art_pack.py "$OUT-faithful" --disc "$DISC" --faithful-cel | tail -1
    $PY - "$OUT" "$OUT-faithful" <<'PY'
import json, shutil, sys
from pathlib import Path
out, faithful = Path(sys.argv[1]), Path(sys.argv[2])
filled = []
for src in sorted((faithful / 'SLUS-00664/glyphs').glob('*.indices.png')):
    if not (out / 'SLUS-00664/glyphs' / src.name).exists():
        for v in ('SLUS-00664', 'SLPS-01160'):
            (out / v / 'glyphs').mkdir(parents=True, exist_ok=True)
            shutil.copy2(faithful / v / 'glyphs' / src.name, out / v / 'glyphs' / src.name)
        filled.append(src.name.split('.')[0])
(out / 'provenance-faithful-fill.json').write_text(json.dumps(dict(format=1, method='glyph_art_pack.py --faithful-cel', keys=filled), indent=1) + '\n')
print(f'ship_glyphs: {len(filled)} glyphs filled by the faithful method')
PY
    rm -rf "$OUT-faithful"
fi
# a translation that prints proportionally (batch.json `proportional`: the discs whose dialogue fonts it prints from)
# gets its glyphs' proportional crops cut from the redrawn cells (tools/glyph_crops.py, xg-g0ra.6)
readarray -t PROP < <($PY -c 'import json,sys; [print(d) for d in json.load(open(sys.argv[1])).get("proportional", [])]' "$B/batch.json")
if (( ${#PROP[@]} )); then $PY tools/glyph_crops.py "$OUT" "${PROP[@]}"; fi
if [[ $INSTALL == 0 ]]; then echo "ship_glyphs: built $OUT (not installed)"; exit 0; fi
for b in release enhanced release-jp enhanced-jp; do
    L=build/pc/$b/glyph-packs
    [[ -L $L ]] || continue
    ln -sfn "$(readlink -f "$OUT")" "$L.new" && mv -T "$L.new" "$L"
    echo "ship_glyphs: $L -> $(readlink "$L")"
done
