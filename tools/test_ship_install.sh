#!/bin/bash
# Regression test for tools/ship_textures.sh's install step (xg-aa3), on synthetic files only:
#   bash tools/test_ship_install.sh
# Each case runs a copy of the real script in a fake repository under $TMPDIR, with a stub build script and a fake
# mv first on PATH (this same file, run with --fake-mv). The fake mv treats the first path part under the fake root (work/
# or build/) as the filesystem and refuses an --exchange across it with EXDEV, as rename(2) does across mounts;
# FAKE_MV_FAIL=<text> makes an exchange whose target contains <text> fail. Other mv calls go to the real mv.

if [ "${1:-}" = --fake-mv ]; then
  shift; ex=0; args=()
  for a in "$@"; do case $a in --exchange) ex=1 ;; -*) ;; *) args+=("$a") ;; esac; done
  [ "$ex" -eq 1 ] || exec "$REAL_MV" "$@"
  a=${args[0]}; b=${args[1]}
  side() { local p; p=$(realpath -m "$1"); p=${p#"$FAKE_ROOT"/}; echo "${p%%/*}"; }
  echo "$a <-> $b" >> "$FAKE_MV_LOG"
  if [ "$(side "$a")" != "$(side "$b")" ]; then
    echo "mv: cannot exchange '$a' and '$b': Invalid cross-device link" >&2; exit 1
  fi
  if [ -n "${FAKE_MV_FAIL:-}" ] && [[ $b == *"$FAKE_MV_FAIL"* ]]; then
    echo "mv: cannot exchange '$a' and '$b': Input/output error (injected)" >&2; exit 1
  fi
  [ -e "$a" ] && [ -e "$b" ] || { echo "mv: cannot exchange '$a' and '$b': No such file or directory" >&2; exit 1; }
  t=$a.fake-exchange
  "$REAL_MV" -T "$a" "$t" && "$REAL_MV" -T "$b" "$a" && "$REAL_MV" -T "$t" "$b"
  exit
fi

set -u
here=$(cd "$(dirname "$0")/.." && pwd) || exit 1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ship-install.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT
REAL_MV=$(command -v mv)
mkdir "$tmp/bin" || exit 1
printf '#!/bin/bash\nexec bash %q --fake-mv "$@"\n' "$here/tools/test_ship_install.sh" > "$tmp/bin/mv" && chmod +x "$tmp/bin/mv" || exit 1
export REAL_MV PATH="$tmp/bin:$PATH"
fails=0

check() {  # check <what> <actual> <expected>
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (got '$2', want '$3')"; fails=$((fails + 1)); fi
}
yes_no() { if "$@"; then echo yes; else echo no; fi; }

# setup <case>: a fake repository with the real script, a stub build and an installed pack per build.
setup() {
  root=$tmp/$1
  mkdir -p "$root/tools" "$root/work/art/field-tex" || exit 1
  root=$(realpath "$root")
  cp "$here/tools/ship_textures.sh" "$root/tools/" || exit 1
  cat > "$root/work/art/field-tex/build-demo.sh" <<'EOF'
F=work/art/field-tex
OUT=$F/demo-v2
echo "building demo" > $F/demo-v2.build.log
[ -z "${STUB_NO_PACK:-}" ] || { echo "not every canvas is ingested" >> $F/demo-v2.build.log; exit 1; }
mkdir -p $OUT/SLUS-00664 && echo new > $OUT/SLUS-00664/a.png && echo "merged 1 file" >> $F/demo-v2.build.log
EOF
  for b in enhanced release; do
    mkdir -p "$root/build/pc/$b/texture-packs/SLUS-00664" || exit 1
    echo "old-$b" > "$root/build/pc/$b/texture-packs/SLUS-00664/a.png"
  done
  export FAKE_ROOT=$root FAKE_MV_LOG=$root/mv.log
  : > "$FAKE_MV_LOG"
}

# ship [env...]: run the real script in the fake repository; sets rc and out.
ship() {
  out=$(cd "$root" && env "$@" bash tools/ship_textures.sh demo 2>&1); rc=$?
  printf '%s\n' "$out" | sed 's/^/     | /'
}

pack() { cat "$root/$1/SLUS-00664/a.png" 2>/dev/null || echo missing; }

echo "== 1: the old install (copy next to OUT, exchange into build/) crosses the boundary"
setup old
(cd "$root" && bash work/art/field-tex/build-demo.sh)
o=work/art/field-tex/demo-v2
(cd "$root" && cp -a "$o" "$o.copy-enhanced" && mv -T --exchange "$o.copy-enhanced" build/pc/enhanced/texture-packs) 2> "$root/err"
check "old exchange refused" "$?" 1
check "old exchange says EXDEV" "$(grep -c 'Invalid cross-device link' "$root/err")" 1
check "old pack still installed" "$(pack build/pc/enhanced/texture-packs)" old-enhanced

echo "== 2: fixed install, with a stale texture-packs.new in enhanced"
setup fixed
mkdir -p "$root/build/pc/enhanced/texture-packs.new/stale" && echo stale > "$root/build/pc/enhanced/texture-packs.new/stale/x"
ship
check "exit status" "$rc" 0
check "staged on the target's side" "$(head -1 "$FAKE_MV_LOG")" "build/pc/enhanced/texture-packs.new <-> build/pc/enhanced/texture-packs"
check "restart line printed" "$(printf '%s\n' "$out" | grep -c 'restart the game')" 1
for b in enhanced release; do
  check "$b: new pack installed" "$(pack "build/pc/$b/texture-packs")" new
  check "$b: old pack kept" "$(pack "work/art/field-tex/installed-pre-demo-v2-$b")" "old-$b"
  check "$b: no staging left" "$(yes_no test -e "$root/build/pc/$b/texture-packs.new")" no
  check "$b: installed pack not nested" "$(ls "$root/build/pc/$b/texture-packs")" SLUS-00664
  check "$b: archive not nested" "$(ls "$root/work/art/field-tex/installed-pre-demo-v2-$b")" SLUS-00664
done
check "no .copy-* folders" "$(find "$root" -name '*.copy-*' | wc -l)" 0

echo "== 3: enhanced (the first build) fails to install, release succeeds"
setup fail-first
ship FAKE_MV_FAIL=enhanced/texture-packs
check "exit status non-zero" "$([ "$rc" -ne 0 ] && echo yes)" yes
check "failure reported" "$(printf '%s\n' "$out" | grep -c 'install into enhanced FAILED')" 1
check "no restart line" "$(printf '%s\n' "$out" | grep -c 'restart the game')" 0
check "enhanced keeps its pack" "$(pack build/pc/enhanced/texture-packs)" old-enhanced
check "release got the new pack" "$(pack build/pc/release/texture-packs)" new
check "release's old pack kept" "$(pack work/art/field-tex/installed-pre-demo-v2-release)" old-release

echo "== 4: release (the last build) fails to install"
setup fail-last
ship FAKE_MV_FAIL=release/texture-packs
check "exit status non-zero" "$([ "$rc" -ne 0 ] && echo yes)" yes
check "release keeps its pack" "$(pack build/pc/release/texture-packs)" old-release
check "enhanced got the new pack" "$(pack build/pc/enhanced/texture-packs)" new

echo "== 5: the build leaves no merged pack"
setup no-pack
ship STUB_NO_PACK=1
check "exit status non-zero" "$([ "$rc" -ne 0 ] && echo yes)" yes
check "no merged pack reported" "$(printf '%s\n' "$out" | grep -c 'no merged pack')" 1
check "no exchange tried" "$(wc -l < "$FAKE_MV_LOG")" 0
check "enhanced untouched" "$(pack build/pc/enhanced/texture-packs)" old-enhanced

if [ "$fails" -eq 0 ]; then echo "test_ship_install: all passed"; else echo "test_ship_install: $fails FAILED"; exit 1; fi
