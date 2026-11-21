#!/usr/bin/env python3
"""Make a personal French text pack with Xenogears PC and Atelier's patched US discs."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
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
