#!/usr/bin/env python3
"""Replay first-person field and world-map controls on a player's US Disc 1 and compare rendered frames."""

import argparse
from pathlib import Path

import statecheck


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="Enhanced US build directory")
    parser.add_argument("output", type=Path, help="fresh directory for inputs, frames and private cards")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)

    def play(name, field, steps, *, allow_look=True, navigate=False, **options):
        dest = output / name
        dest.mkdir()
        pad = dest / "inputs.pad"
        pad.write_text("0 0000\n" + steps, encoding="ascii")
        settings = dest / "settings.ini"
        settings.write_text(f"first_person_look={int(allow_look)}\nfirst_person_navigation={int(navigate)}\n", encoding="ascii")
        env = dict(XG_SETTINGS=str(settings), XG_FIRST_PERSON_LOOK="", XG_FIRST_PERSON_NAVIGATION="", XG_PAD_SCRIPT=str(pad),
                   XG_PACK_DIR="none", XG_DUMP_RANGE="700-950/10", **options)
        if field is None:
            env.update(XG_BOOT_MODE="3", XG_CD_SPEED="8", XG_ENCOUNTER_RATE="0", XG_SWEEP_LOG=str(dest / "world.log"))
        else:
            env.update(XG_FIELD=str(field), XG_PARTY="0,255,255")
        if statecheck.run_port(build, dest, "us1", 961, env) != 0:
            raise RuntimeError(f"{name} failed; see {dest / 'port.log'}")
        return dest

    def compare(a, b, frame, same):
        path = f"frames/frame_{frame:06d}.png"
        if (statecheck.png_pixels_hash(a / path) == statecheck.png_pixels_hash(b / path)) != same:
            raise AssertionError(f"{a.name} and {b.name} at frame {frame}: expected equal={same}")

    idle = play("idle", 1, "")
    presses = "720 key V\n890 key V\n"
    disabled = play("default-off", 1, presses, allow_look=False)
    for frame in range(700, 951, 10):
        compare(idle, disabled, frame, True)
    toggle = play("toggle", 1, presses)
    compare(idle, toggle, 710, True)
    compare(idle, toggle, 760, False)
    for frame in range(900, 951, 10):
        compare(idle, toggle, frame, True)
    if (idle / "audio.pcm").read_bytes() != (toggle / "audio.pcm").read_bytes():
        raise AssertionError("stationary look changed field audio")

    # Leave an already rotated and zoomed third-person camera exactly as it was.
    orbit = play("orbit", 1, "", XG_FREECAM="32,0.85,30")
    look = play("orbit-look", 1, "720 key V\n890 key V\n", XG_FREECAM="32,0.85,30")
    orbit_only = play("orbit-only", 1, presses, allow_look=False, XG_FREECAM="32,0.85,30")
    compare(orbit, orbit_only, 760, True)
    compare(orbit, look, 760, False)
    compare(orbit, look, 910, True)

    movement = "860 1000\n885 0000\n"
    walk = play("walk", 1, movement)
    leave = play("walk-from-look", 1, "720 key V\n" + movement)
    compare(walk, leave, 760, False)
    compare(walk, leave, 930, True)
    moving = "680 1000\n"
    walking = play("walking", 1, moving + "750 0000\n")
    refused = play("walking-request", 1, moving + "720 key V\n750 0000\n")
    compare(walking, refused, 740, True)
    if "First-person look needs a stationary player in control" not in (refused / "port.log").read_text():
        raise AssertionError("missing movement refusal")

    # Approach the NPC in the Lahan tavern, look, then talk. A second
    # toggle during the open dialogue must leave the dialogue camera alone.
    approach = "630 4000\n659 0000\n"
    talk = "800 0060\n805 0000\n"
    native = play("talk", 5, approach + talk)
    looked = play("talk-from-look", 5, approach + "720 key V\n" + talk + "880 key V\n")
    compare(native, looked, 760, False)
    compare(native, looked, 840, True)
    compare(native, looked, 940, True)
    if "First-person look needs a stationary player in control" not in (looked / "port.log").read_text():
        raise AssertionError("missing dialogue refusal")

    fixed = play("fixed", 14, "")
    refused = play("fixed-request", 14, "720 key V\n")
    compare(fixed, refused, 760, True)
    if "First-person look unavailable in this room" not in (refused / "port.log").read_text():
        raise AssertionError("missing fixed-room refusal")
    # With navigation enabled the same movement leaves the camera at the
    # actor's eyes. The shortcut still returns to the normal view after walking.
    nav = play("navigate", 1, "720 key V\n" + movement, navigate=True)
    exit_nav = play("navigate-exit", 1, "720 key V\n" + movement + "900 key V\n", navigate=True)
    compare(nav, exit_nav, 890, True)
    compare(nav, exit_nav, 930, False)
    compare(walk, nav, 880, False)

    def world_pose(run, vblank):
        for line in (run / "world.log").read_text().splitlines():
            if line.startswith(f"R {vblank} "):
                fields = [int(x) for x in line.split()[1:]]
                return fields[3:6], fields[6:9], fields[9:12]
        raise AssertionError(f"Missing rendered world-map pose at {vblank} in {run}")

    on_foot = dict(XG_WM_VEHICLE="0", XG_WM_SCENARIO="38", XG_WM_POS="5", XG_WM_PARTY="0,2,1")
    for name, opts, move in (
        ("foot", on_foot, "1000"),
        ("gear", dict(on_foot, XG_WM_GEAR="1", XG_WM_PARTY="4,255,255"), "1000"),
        ("ship", {}, "0010"),  # the Yggdrasil's throttle
    ):
        walk_pad = f"810 {move}\n840 0000\n"
        inputs = "720 key V\n" + walk_pad
        nav = play(f"world-{name}", None, inputs + "900 key V\n", navigate=True, **opts)
        stationary = play(f"world-{name}-look", None, inputs, **opts)
        game = play(f"world-{name}-disabled", None, inputs, allow_look=False, navigate=True, **opts)
        compare(nav, stationary, 760, True)
        compare(nav, game, 760, False)
        compare(stationary, game, 880, True)
        pos = world_pose(nav, 760)[2]
        eye, angles, moved = world_pose(nav, 840)
        if pos == moved or angles[0] != 0 or world_pose(game, 840)[0] == eye:
            raise AssertionError(f"{name}: first-person travel did not retain a moving eye-level view")
        if world_pose(nav, 930) != world_pose(game, 930):
            raise AssertionError(f"{name}: the normal camera did not return after exit")
    print("First-person controls passed in fields, on foot, in a Gear and on the Yggdrasil.")


if __name__ == "__main__":
    main()
