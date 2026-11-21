#!/usr/bin/env python3
"""Make a personal French text pack with Xenogears PC and Atelier's patched US discs."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


# SHA-256 of the official 1.1 patcher's output on both US retail BINs.
EDITIONS = {
    "": (
        "c3c3e79d2e99fb5a5e54f2fc86229a586c0e8b21a71a20c9f26c37138a21251c",
        "c720ce69bf3ed02c445e3261e1b6593b2d47d575556a85a27c6f665890f2d35b",
    ),
    "_undub": (
        "ff2d8ea9666032c6c689cd973f8ccdada5465d794ffe62741b37831f041bc57f",
        "64251f336c48f8e140eb53423ae1ab99edd9316521e90b9c49759d7049ceddc3",
    ),
}


def text_copy(source, destination, digest):
    checksum = hashlib.sha256()
    with source.open("rb") as src, destination.open("xb") as out:
        for block in iter(lambda: src.read(1024 * 1024), b""):
            checksum.update(block)
            out.write(block)
    if checksum.hexdigest() != digest:
        raise ValueError(f"{source.name} does not match Atelier Traduction 1.1. Check the patch and input discs.")

    # Slots 280..381 (zero-based) are empty on both US retail discs.
    # Atelier stores other data there. Clear just these slots in the temporary
    # text reader's view. The port reads Mode 2 Form 1 user bytes for export.
    with destination.open("r+b") as out:
        offset = 280 * 7
        end = 382 * 7
        while offset < end:
            sector, within = divmod(offset, 2048)
            count = min(end - offset, 2048 - within)
            out.seek((24 + sector) * 2352 + 24 + within)
            out.write(bytes(count))
            offset += count


def default_packs():
    if os.environ.get("XG_PACK_DIR") not in (None, "", "none"):
        return Path(os.environ["XG_PACK_DIR"])
    if os.name == "nt":
        return Path(os.environ["APPDATA"]) / "xenogears" / "packs"
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/xenogears/packs"
    return Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "xenogears/packs"


def restore_battle_words(folder, image):
    """Repair the v2026.11.17 export from the player's verified Atelier image."""
    old_snapshot = "1a288d74dbf24b4b0e9a4d11828d9d5511e7563a2ecc5f6c0bb105a6d05fdb9f"
    battle = folder / "pictures/battle/common"
    if not (battle / "1/1.member").is_file():
        raise ValueError("The game did not export the battle pictures.")
    legacy = [path for path in battle.glob("[12]/1.member")
              if hashlib.sha256(path.read_bytes()).hexdigest() == old_snapshot]
    if not legacy:
        return
    try:
        from xenofs import Disc, lzss_decompress
    except ImportError as error:
        raise ValueError("Place xenofs.py beside atelier_text_pack.py to fix battle words with this game version.") from error

    # The verified standard and undub pairs share this table across both discs.
    with Disc(image) as disc:
        bundle = disc.read_file(0x0C, 3, whole_sectors=True)
    offset, = struct.unpack_from("<I", bundle, 4)
    sprites = lzss_decompress(bundle[offset:])
    if hashlib.sha256(sprites).hexdigest() != "083417a566ea5c88bc033c090ffdce5b28e22a3032eb4b13c2439259d7af25d8":
        raise ValueError("Unexpected Atelier battle sprite table.")

    # Keep the snapshot bound to the retail member. The placement template
    # refers to the replacement table by its xgspr1 fingerprint.
    fingerprint = 0x0A47AA88894A4F19
    for path in legacy:
        header = bytearray(path.read_bytes()[:32])
        struct.pack_into("<I", header, 12, len(sprites))
        struct.pack_into("<Q", header, 24, fingerprint)
        path.write_bytes(header + sprites)
        layout = path.with_suffix(".layout")
        if layout.is_file():
            rows = [f"xg-sprites-1 {fingerprint:016x}", *layout.read_text().splitlines()[1:3]]
            count, = struct.unpack_from("<I", sprites)
            for index, (start,) in enumerate(struct.iter_unpack("<H", sprites[4:4 + 2 * count])):
                pieces, = struct.unpack_from("<H", sprites, start)
                for piece in range(pieces):
                    w, h, x, y = struct.unpack_from("<4h", sprites, start + 8 + piece * 28)
                    rows.append(f"# {index} {piece} {x} {y} # {w}x{h} pixels")
            layout.write_bytes(("\n".join(rows) + "\n").encode("ascii"))


def make_pack(game, retail, french, packs):
    editions = [suffix for suffix in EDITIONS if all(
        (french / f"Xenogears_fr_cd{disc}{suffix}.bin").is_file() for disc in (1, 2))]
    if len(editions) != 1:
        raise ValueError("Choose Atelier's Patched or Undub_patched folder containing one complete pair of BINs.")
    suffix = editions[0]
    dest = packs / "atelier-fr"
    if dest.exists():
        raise ValueError(f"A pack already exists at {dest}. Choose a different --packs-dir to make another copy.")
    check = subprocess.run([game, "--verify-discs", retail], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if check.returncode or b"US Disc 1, SLUS_006.64" not in check.stdout:
        raise ValueError("Choose your original US Disc 1 CUE or raw BIN for --retail.")

    packs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory(dir=packs) as stage:
        work = Path(tmp)
        env = {key: val for key, val in os.environ.items() if not key.startswith("XG_")}
        if "XG_VK_DEVICE" in os.environ:
            env["XG_VK_DEVICE"] = os.environ["XG_VK_DEVICE"]
        for disc, digest in enumerate(EDITIONS[suffix], 1):
            src = french / f"Xenogears_fr_cd{disc}{suffix}.bin"
            view = work / f"atelier{disc}.bin"
            text_copy(src, view, digest)
            env[f"XG_TEXT_IMAGE{disc}"] = view.as_posix()

        (work / "cards").mkdir()
        env.update(XG_TEXT_EXPORT="image", XG_PACK_DIR=stage, XG_CARD_DIR=str(work / "cards"),
                   XG_SETTINGS="none", XG_HEADLESS="1", XG_CLOCK="step", XG_FIELD="14",
                   XG_QUIT_AT="1900", SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
        run = subprocess.run([game, retail], cwd=tmp, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=300)
        if run.returncode or b"gametext: export done:" not in run.stdout:
            raise ValueError("Game export failed.\n" + run.stdout[-2000:].decode(errors="replace"))
        folder = Path(stage) / "image-atelier1"
        if not (folder / "pack.ini").is_file():
            raise ValueError("The game did not produce a pack for the French Disc 1 image.")
        fields = (folder / "field.txt").read_bytes()
        if (b"@@ field/1/14/" not in fields or b"@@ field/2/618/" not in fields
                or not (folder / "font.bin").stat().st_size):
            raise ValueError("The export is missing field text or the French font.")
        restore_battle_words(folder, work / "atelier1.bin")
        (folder / "pack.ini").write_text(
            "name=Atelier Français 1.1\nlanguage=fr\nrelease=US\nfont=pack\n"
            "print=proportional\nwindows=grow\n", encoding="utf-8")
        folder.rename(dest)
    return dest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="xenogears", help="Original US game executable (default: xenogears on PATH)")
    parser.add_argument("--retail", required=True, type=Path, help="original US Disc 1 CUE or BIN")
    parser.add_argument("--french", required=True, type=Path, help="Atelier 1.1 Patched or Undub_patched folder")
    parser.add_argument("--packs-dir", type=Path, help="pack installation directory (default: the game's packs folder)")
    args = parser.parse_args()
    game = shutil.which(args.game) or args.game
    try:
        dest = make_pack(Path(game).resolve(), args.retail.resolve(), args.french.resolve(),
                         (args.packs_dir or default_packs()).resolve())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"{error}\n")
    print(f"Created {dest}\nSelect Atelier Français 1.1 in the game's text settings.")


if __name__ == "__main__":
    main()
