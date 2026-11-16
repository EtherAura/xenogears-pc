#!/usr/bin/env python3
"""Field speed in the F1 menu (xg-mrj.6): the Gameplay section's Field speed
row (port/src/ui/sections.cpp) and the setting's entry in the menu's table
(port/src/ui/options.cpp), CPU only, no window.

    python tools/test_field_speed_ui.py --ver us|jp [--build DIR]

Two fixtures, built in <build>/fieldspeed-ui (removed when the run ends,
passed or failed):

  row       #includes sections.cpp whole and calls menu::gameplaySection()
            once per scripted frame;
  registry  #includes options.cpp whole and drives opt::changed, the row
            reset (opt::finishPage over the row's keys) and opt::presetRows
            (a preset chosen and applied, "Save my current setup").

Both link the game's fieldspeed.c, battlespeed.c, settings.c and gameplay.c,
so every value is read, set and saved by the real setters into a settings
file of the run's own (XG_SETTINGS in the scratch directory), which this
script reads back and hands to further processes.

Flags: each file is compiled with the compiler, DEFINES, INCLUDES and FLAGS
of its own statement in the build's build.ninja (targets ui and platform),
plus EXTRA_FLAGS below (the fixture's own; -fno-lto, sections for
--gc-sections, the ui directory for the copies). The link line is the
fixture's own (LINK_FLAGS below) with the libraries of fieldspeedtest.

What is not production code (the output lists it again):
  - HAND_STUBS below: the ImGui calls of the dropdown and the presets'
    rows, scripted; opt::row and the page calls (row fixture); hint,
    overrideNote, textLines, scale; l10nText as English.
  - Every other symbol the kept code refers to and the four C files do not
    define gets a generated stub that returns 0 and records that it ran.
    The script prints the ones that ran: the row fixture's are the page's
    other calls; the registry fixture's are the getters and setters of the
    other settings, which My setup and the presets go through. Their values
    (0) are not evidence of anything and nothing here checks them.
  - fieldSpeedSetChosen and battleSpeedSetChosen are counted by ld --wrap
    and then run as they are.
  - The registry fixture pushes the row's keys into opt's rowsNow (what
    opt::row does in a real page) and puts `asking` back to -1 between
    presets (the popup closing).

Negative controls: the same fixture over a copy of the production file with
one change must build, run to the end, and fail a named behavioural check
(a control that does not build or run is a failure of this script):
  battle-setter    the dropdown calls battleSpeedSetChosen
  swapped-2x-4x    the row's values are 0, 1, 4, 2
  no-registration  options.cpp has no field_speed entry

It proves the wiring and the settings; not the menu's look, not the game.
x86-64 only (the generated stubs are assembly). Exit 0 when everything held.
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "port/src/ui"
SECTIONS = UI / "sections.cpp"
OPTIONS = UI / "options.cpp"
C_FILES = ["fieldspeed.c", "battlespeed.c", "settings.c", "gameplay.c"]
EXTRA_FLAGS = ["-fno-lto", "-ffunction-sections", "-fdata-sections", f"-I{UI}"]
LINK_FLAGS = ["-no-pie", "-Wl,--gc-sections", "-Wl,--no-demangle", "-Wl,--wrap=fieldSpeedSetChosen",
              "-Wl,--wrap=battleSpeedSetChosen"]
HAND_STUBS = {
    "row": "ImGui::BeginCombo, ImGui::Selectable (the scripted dropdown), opt::row (true for Field speed only), "
           "opt::beginPage/endPage/group, menu::hint, menu::overrideNote, menu::scale, l10nText (English)",
    "registry": "ImGui::BeginCombo, Selectable, Button, BeginPopupModal (the scripted presets' rows), menu::hint, "
                "menu::textLines, menu::scale, l10nText (English)",
}
LABELS = [("Off", 0), ("1x", 1), ("2x", 2), ("4x", 4)]
PRESETS = ["Faithful", "Enhanced 60 Hz", "Performance", "Streaming"]

COMMON = r"""
#include "@SRC@"
#include <string>
extern "C" {
static const char* stubNames[2048];
static int stubCount;
long fixtureStub(const char* name) {
    for (int i = 0; i < stubCount; i++) if (stubNames[i] == name) return 0;
    if (stubCount < 2048) stubNames[stubCount++] = name;
    return 0;
}
static int fieldSets, fieldLast = -1, battleSets;
void __real_fieldSpeedSetChosen(int);
void __wrap_fieldSpeedSetChosen(int v) { fieldSets++; fieldLast = v; __real_fieldSpeedSetChosen(v); }
void __real_battleSpeedSetChosen(int);
void __wrap_battleSpeedSetChosen(int v) { battleSets++; __real_battleSpeedSetChosen(v); }
const char* l10nText(const char* english) { return english; }
}
static void stubsOut() {
    for (int i = 0; i < stubCount; i++) printf("stub\t%s\n", stubNames[i]);
    fflush(stdout);
}
namespace menu { float scale() { return 1.0f; } }
"""

ROW_FX = COMMON + r"""
static struct { int click, selects, rows; std::string id, preview, labels, keys, notes, before, last, hints; } fx;
namespace ImGui {
bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags) {
    fx.id = label;
    fx.preview = preview ? preview : "(null)";
    return true;
}
bool Selectable(const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
    if (!fx.labels.empty()) fx.labels += ",";
    fx.labels += label;
    return fx.selects++ == fx.click;
}
}
namespace menu {
void hint(const char* fmt, ...) {
    char t[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    fx.hints += t;
}
void overrideNote(const char* key) { fx.notes += key; fx.notes += ","; }
namespace opt {
void beginPage(const char*, int, bool) {}
void endPage() {}
void group(const char*, int) {}
bool row(const char* label, const char* keys, int) {
    bool on = strcmp(label, "Field speed") == 0;
    if (on) {
        fx.rows++;
        fx.keys = keys ? keys : "";
        fx.before = fx.last;
    }
    fx.last = label;
    return on;
}
}
}
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        fx = {};
        fx.click = atoi(argv[a]);
        fieldSets = battleSets = 0;
        fieldLast = -1;
        menu::gameplaySection();
        printf("frame\tclick=%d\trows=%d\tid=%s\tpreview=%s\tlabels=%s\tkeys=%s\tnotes=%s\tbefore=%s\tfieldSets=%d\t"
               "fieldLast=%d\tbattleSets=%d\tfield=%d\tbattle=%d\thint=%s\n",
               fx.click, fx.rows, fx.id.c_str(), fx.preview.c_str(), fx.labels.c_str(), fx.keys.c_str(),
               fx.notes.c_str(), fx.before.c_str(), fieldSets, fieldLast, battleSets, fieldSpeedChosen(),
               battleSpeedChosen(), fx.hints.c_str());
    }
    stubsOut();
    return 0;
}
"""

REGISTRY_FX = COMMON + r"""
static struct { const char* pick; bool apply, save; std::string lines; } fx;
namespace ImGui {
bool BeginCombo(const char*, const char*, ImGuiComboFlags) { return true; }
bool Selectable(const char* label, bool, ImGuiSelectableFlags, const ImVec2&) {
    return fx.pick != nullptr && strcmp(label, fx.pick) == 0;
}
bool Button(const char* label, const ImVec2&) {
    if (strcmp(label, "Apply") == 0) return fx.apply;
    if (strncmp(label, "Save my current setup", 21) == 0) return fx.save;
    return false;
}
bool BeginPopupModal(const char*, bool*, ImGuiWindowFlags) { return fx.pick != nullptr && menu::opt::asking >= 0; }
}
namespace menu {
void hint(const char*, ...) {}
void textLines(const char* text) { fx.lines += text; fx.lines += ";"; }
}
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        const char* op = argv[a];
        const char* arg = strchr(op, '=') ? strchr(op, '=') + 1 : "";
        int result = 0;
        fx = {};
        fieldSets = battleSets = 0;
        fieldLast = -1;
        menu::opt::asking = -1;
        if (strncmp(op, "rowreset=", 9) == 0) {
            menu::opt::rowsNow.clear();
            menu::opt::rowsNow.push_back({0.0f, menu::opt::specsOf(arg)});
            result = menu::opt::finishPage(1.0f, true);
        } else if (strncmp(op, "preset=", 7) == 0) {
            fx.pick = arg;
            fx.apply = true;
            menu::opt::presetRows();
            result = menu::opt::asking;
        } else if (strcmp(op, "savemine") == 0) {
            fx.save = true;
            menu::opt::presetRows();
        }
        const menu::opt::Spec* s = menu::opt::findSpec("field_speed");
        std::string names = "-", def = "-", label = "-";
        if (s != nullptr) {
            names = s->show(0) + "," + s->show(1) + "," + s->show(2) + "," + s->show(4);
            def = s->showDefault();
            label = s->label;
        }
        printf("op\top=%s\tresult=%d\tfound=%d\tlabel=%s\tnames=%s\tdefault=%s\tchangedF=%d\tchangedB=%d\tfield=%d\t"
               "battle=%d\tfieldSets=%d\tfieldLast=%d\tbattleSets=%d\tlines=%s\n",
               op, result, s != nullptr, label.c_str(), names.c_str(), def.c_str(), menu::opt::changed("field_speed"),
               menu::opt::changed("battle_speed"), fieldSpeedChosen(), battleSpeedChosen(), fieldSets, fieldLast,
               battleSets, fx.lines.c_str());
    }
    stubsOut();
    return 0;
}
"""

# name, fixture, file, the text as it is, the text instead, a check that must fail
MUTANTS = [
    ("battle-setter", "row", SECTIONS, "fieldSpeedSetChosen(fieldSpeeds[picked]);",
     "battleSpeedSetChosen(fieldSpeeds[picked]);", "row: choosing 2x calls fieldSpeedSetChosen(2) once"),
    ("swapped-2x-4x", "row", SECTIONS, "static const int fieldSpeeds[] = {0, 1, 2, 4};",
     "static const int fieldSpeeds[] = {0, 1, 4, 2};", "row: choosing 2x saves field_speed=2"),
    ("no-registration", "registry", OPTIONS,
     '    add("field_speed", N_("Field speed"), [](int) { return fieldSpeedChosen(); },\n'
     "        [](int, int v) { fieldSpeedSetChosen(v); }, 0, battleSpeedNames, 4, battleSpeedValues);\n", "",
     "registry: the Field speed row's reset restores Off"),
]


class Broken(Exception):
    """A fixture did not build or run to its end: never a behavioural result."""


def run(cmd, timeout, **kw) -> subprocess.CompletedProcess:
    try:
        return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=timeout, **kw)
    except subprocess.TimeoutExpired as e:  # (run() has killed and reaped the child)
        raise Broken(f"timed out after {timeout} s: {' '.join(str(c) for c in cmd)[:200]}") from e


# ---- the build's own compile statements ------------------------------------------------------

def variables(block: str) -> dict[str, str]:
    return {k: v for k, v in re.findall(r"^  (\w+) = (.*)$", block, re.M)}


def statement(build_dir: Path, target: str, src: Path) -> tuple[list[str], str]:
    """The compiler and arguments build.ninja has for `src` in `target`, and a line describing them."""
    text = (build_dir / "build.ninja").read_text()
    m = re.search(rf"^build CMakeFiles/{re.escape(target)}\.dir/\S+\.o: (\S+) {re.escape(str(src))}(?: .*)?\n((?:  .*\n)+)",
                  text, re.M)
    if m is None:
        raise Broken(f"no statement for {src} in target {target} of {build_dir}/build.ninja")
    v = variables(m.group(2))
    rules = (build_dir / "CMakeFiles/rules.ninja").read_text()
    r = re.search(rf"^rule {re.escape(m.group(1))}\n(?:  .*\n)*?  command = .*?(\S+) \$DEFINES \$INCLUDES \$FLAGS", rules, re.M)
    if r is None:
        raise Broken(f"no compiler in rule {m.group(1)}")
    compiler = re.sub(r"\$\{\w+\}", "", r.group(1))
    args = shlex.split(" ".join(v.get(k, "") for k in ("DEFINES", "INCLUDES", "FLAGS")).replace("$$", "$"))
    return [compiler, *args], f"{src.name} [{target}]: {compiler} {v.get('DEFINES', '')} {v.get('FLAGS', '')}"


class Fixtures:
    def __init__(self, build_dir: Path, work: Path):
        self.build_dir, self.work = build_dir, work
        self.cmd = {}
        for kind, src in (("row", SECTIONS), ("registry", OPTIONS)):
            self.cmd[kind], line = statement(build_dir, "ui", src)
            print(f"      flags  {line}")
        self.objects = []
        for name in C_FILES:
            cmd, line = statement(build_dir, "platform", ROOT / "port/src/system" / name)
            print(f"      flags  {line}")
            obj = work / (name + ".o")
            self.compile(cmd, ROOT / "port/src/system" / name, obj)
            self.objects.append(obj)
        link = re.search(r"^build fieldspeedtest: .*\n((?:  .*\n)+)", (build_dir / "build.ninja").read_text(), re.M)
        if link is None:
            raise Broken("no fieldspeedtest link statement (its libraries)")
        self.libs = shlex.split(variables(link.group(1)).get("LINK_LIBRARIES", ""))
        print(f"      added to every compile: {' '.join(EXTRA_FLAGS)}")
        print(f"      link (the fixture's own): {' '.join(LINK_FLAGS)} ... {' '.join(self.libs)}")

    def compile(self, cmd: list[str], src: Path, obj: Path) -> None:
        p = run([*cmd, *EXTRA_FLAGS, "-c", src, "-o", obj], 900, cwd=self.build_dir)
        if p.returncode != 0:
            raise Broken(f"compile of {src.name} failed:\n{p.stderr[-3000:]}")

    def build(self, kind: str, name: str, source: Path) -> tuple[Path, int]:
        """The fixture over `source` (the production file or a changed copy): its program and its generated stubs."""
        top = self.work / name
        top.mkdir(parents=True)
        fixture = top / "fixture.cpp"
        fixture.write_text((ROW_FX if kind == "row" else REGISTRY_FX).replace("@SRC@", str(source)))
        self.compile(self.cmd[kind], fixture, top / "fixture.o")
        exe, stubs = top / "fixture", []
        for _ in range(8):
            objs = [top / "fixture.o", *self.objects]
            if stubs:
                asm = top / "stubs.s"
                asm.write_text("".join(
                    f"\t.text\n\t.globl\t{s}\n\t.type\t{s}, @function\n{s}:\n\tleaq\t.Lname{i}(%rip), %rdi\n"
                    f"\tjmp\tfixtureStub\n\t.section\t.rodata\n.Lname{i}:\n\t.asciz\t\"{s}\"\n"
                    for i, s in enumerate(stubs)) + '\t.section\t.note.GNU-stack,"",@progbits\n')
                p = run([self.cmd[kind][0], "-c", asm, "-o", top / "stubs.o"], 300)
                if p.returncode != 0:
                    raise Broken(f"stubs did not assemble:\n{p.stderr[-2000:]}")
                objs.append(top / "stubs.o")
            p = run([self.cmd[kind][0], *LINK_FLAGS, *objs, "-o", exe, *self.libs], 600, cwd=self.build_dir)
            if p.returncode == 0:
                return exe, len(stubs)
            new = sorted(set(re.findall(r"undefined (?:reference to|symbol:) [`']?([\w.$]+)", p.stderr)) - set(stubs))
            if not new:
                raise Broken(f"link of {name} failed:\n{p.stderr[-3000:]}")
            stubs += new
        raise Broken(f"link of {name}: still undefined symbols after 8 passes")


# ---- running a fixture -------------------------------------------------------------------------

reached: dict[str, set[str]] = {"row": set(), "registry": set()}


def play(exe: Path, ini: Path, args: list[str], kind: str | None = None, **env: str) -> list[dict[str, str]]:
    clean = {k: v for k, v in os.environ.items() if not k.startswith("XG_")}
    clean.update(XG_SETTINGS=str(ini), **env)
    p = run([exe, *args], 60, env=clean)
    lines = p.stdout.splitlines()
    records = [dict(f.partition("=")[::2] for f in line.split("\t")[1:]) for line in lines if line[:2] in ("fr", "op")]
    if p.returncode != 0 or len(records) != len(args):
        raise Broken(f"{exe.parent.name} {args}: exit {p.returncode}, {len(records)} records\n{p.stdout[-800:]}{p.stderr[-800:]}")
    if kind is not None:
        reached[kind].update(line.split("\t")[1] for line in lines if line.startswith("stub\t"))
    return records


def keys(ini: Path) -> dict[str, str]:
    out = {}
    if ini.is_file():
        for line in ini.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith(("#", ";")):
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


def num(record: dict[str, str], *names: str) -> tuple[int, ...]:
    return tuple(int(record[n]) for n in names)


def row_checks(exe: Path, work: Path, check, kind: str | None) -> str:
    ini = work / "row.ini"
    ini.unlink(missing_ok=True)
    f = play(exe, ini, ["-1"], kind)[0]
    check(f["rows"] == "1" and f["keys"] == "field_speed" and f["before"] == "Battle speed",
          "row: Field speed is drawn once, right after Battle speed, with the key field_speed", str(f))
    check(f["id"] == "##field_speed" and f["labels"] == "Off,1x,2x,4x", "row: the dropdown ##field_speed lists Off, 1x, 2x, 4x",
          f"{f['id']} {f['labels']}")
    check(f["preview"] == "Off" and num(f, "fieldSets", "battleSets", "field") == (0, 0, 0) and "field_speed" not in keys(ini),
          "row: with no settings it shows Off, calls no setter and writes no field_speed")
    check(f["notes"] == "field_speed,", "row: overrideNote(field_speed) under it", f["notes"])
    text = f["hint"].lower()
    missing = [w for w in ("field", "world map", "scenes", "dialogue", "menus", "movies", "battles", "off and 1x",
                           "original speed") if w not in text]
    check(not missing, "row: the help names the field, the world map, scenes, dialogue, menus, movies, battles and Off/1x",
          f"missing {missing}: {f['hint']}")
    promised = [w for w in ("tab", "select", "r2", "l1", "r1", "key", "chord", "button", "pitch", "tempo", "sound", "music")
                if re.search(rf"\b{w}\b", text)]
    check(not promised, "row: the help promises no key, chord, sound or hold", f"has {promised}")

    body = "# a comment\nbattle_speed=4\nfield_speed=0\nother_key=kept\n"
    ini.write_text(body)
    a, b = play(exe, ini, ["-1", "0"], kind)
    check(num(a, "fieldSets", "battleSets") == (0, 0) and num(b, "fieldSets", "battleSets") == (0, 0)
          and ini.read_text() == body,
          "row: an untouched frame and a click on the entry shown call no setter; the file is byte-identical")
    for click, (label, value) in list(enumerate(LABELS))[1:] + [(0, LABELS[0])]:
        f, after = play(exe, ini, [str(click), "-1"], kind)
        check(num(f, "fieldSets", "fieldLast", "battleSets") == (1, value, 0),
              f"row: choosing {label} calls fieldSpeedSetChosen({value}) once and no battle setter",
              f"field setter {f['fieldSets']} time(s), last {f['fieldLast']}; battle setter {f['battleSets']}")
        k = keys(ini)
        check(k.get("field_speed") == str(value) and k.get("battle_speed") == "4" and k.get("other_key") == "kept"
              and num(f, "field", "battle") == (value, 4),
              f"row: choosing {label} saves field_speed={value}; battle_speed=4 and other_key kept", str(k))
        check(after["preview"] == label and num(after, "fieldSets", "battleSets") == (0, 0),
              f"row: the next frame shows {label} and calls no setter", after["preview"])
        saved = ini.read_text()
        again = play(exe, ini, ["-1"], kind)[0]
        check(again["preview"] == label and ini.read_text() == saved and num(again, "fieldSets", "battleSets") == (0, 0),
              f"row: a new process shows {label} from the file and writes nothing", again["preview"])
    saved = ini.read_text()
    f = play(exe, ini, ["-1"], kind, XG_FIELD_SPEED="4")[0]
    check(f["preview"] == "4x" and num(f, "fieldSets", "battleSets") == (0, 0) and ini.read_text() == saved,
          "row: XG_FIELD_SPEED=4 shows 4x for the run, calls no setter and leaves field_speed=0 in the file", f["preview"])
    return f["keys"]


def registry_checks(exe: Path, work: Path, check, kind: str | None, row_keys: str) -> None:
    ini = work / "registry.ini"
    ini.unlink(missing_ok=True)
    s = play(exe, ini, ["state"], kind)[0]
    check(s["found"] == "1" and s["label"] == "Field speed" and s["names"] == "Off,1x,2x,4x" and s["default"] == "Off",
          "registry: field_speed is known: label Field speed, 0/1/2/4 shown as Off, 1x, 2x, 4x, default Off", str(s))
    check(num(s, "changedF", "field") == (0, 0) and "field_speed" not in keys(ini),
          "registry: unset, it is Off, not marked changed, and nothing is written")
    s = play(exe, ini, [f"preset={p}" for p in PRESETS], kind)
    check(all(num(r, "field", "battle", "fieldSets", "battleSets") == (0, 0, 0, 0) for r in s)
          and keys(ini).get("field_speed", "0") == "0" and keys(ini).get("battle_speed", "0") == "0",
          "registry: from the defaults, the four presets applied leave field speed and battle speed Off")

    body = "battle_speed=2\nfield_speed=4\nother_key=kept\n"
    ini.write_text(body)
    s, r = play(exe, ini, ["state", f"rowreset={row_keys}"], kind)
    check(num(s, "changedF", "field") == (1, 4), "registry: field_speed=4 is marked changed")
    k = keys(ini)
    check(num(r, "result", "field", "changedF", "fieldSets", "fieldLast") == (1, 0, 0, 1, 0) and k.get("field_speed") == "0",
          "registry: the Field speed row's reset restores Off through fieldSpeedSetChosen(0) and saves field_speed=0",
          f"reset done {r['result']}, field {r['field']}, setter {r['fieldSets']} time(s), file {k.get('field_speed')}")
    check(num(r, "battle", "battleSets") == (2, 0) and k.get("battle_speed") == "2" and k.get("other_key") == "kept",
          "registry: that reset leaves battle_speed=2 and other_key", str(k))
    ini.write_text(body)
    r = play(exe, ini, ["rowreset=battle_speed battle_speed_key"], kind)[0]
    check(num(r, "result", "battle", "field", "fieldSets") == (1, 0, 4, 0) and keys(ini).get("field_speed") == "4",
          "registry: the Battle speed row's reset leaves field_speed=4")
    ini.write_text(body)
    for name, r in zip(PRESETS, play(exe, ini, [f"preset={p}" for p in PRESETS], kind)):
        check(int(r["result"]) >= 0 and num(r, "field", "battle", "fieldSets", "battleSets") == (4, 2, 0, 0)
              and "Field speed" not in r["lines"] and "Battle speed" not in r["lines"],
              f"registry: preset {name} applied: field 4x and battle 2x kept, neither listed, no speed setter", r["lines"])
    k = keys(ini)
    check(k.get("field_speed") == "4" and k.get("battle_speed") == "2" and k.get("other_key") == "kept",
          "registry: after the four presets the file still has field_speed=4, battle_speed=2, other_key", str(k))

    ini.write_text("battle_speed=2\nfield_speed=2\nother_key=kept\n")
    play(exe, ini, ["savemine"], kind)
    mine = keys(ini).get("preset_mine", "").split(";")
    check("field_speed=2" in mine and "battle_speed=2" in mine, "registry: My setup saves field_speed=2 (and battle_speed=2)",
          f"preset_mine has {[m for m in mine if 'speed' in m]}")
    ini.write_text(ini.read_text().replace("\nfield_speed=2\n", "\nfield_speed=4\n"))
    r = play(exe, ini, ["preset=My setup"], kind)[0]
    check("Field speed: 4x -> 2x" in r["lines"] and num(r, "field", "fieldSets", "fieldLast", "battleSets") == (2, 1, 2, 0)
          and keys(ini).get("field_speed") == "2" and keys(ini).get("battle_speed") == "2",
          "registry: applying My setup lists Field speed: 4x -> 2x, sets it through fieldSpeedSetChosen(2) and saves it",
          f"field {r['field']}, setter {r['fieldSets']} time(s); {r['lines'][:300]}")
    s = play(exe, ini, ["state"], kind)[0]
    check(num(s, "field", "battle", "changedF") == (2, 2, 1), "registry: a new process reads field 2x, battle 2x, marked changed")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--build", help="a configured build of that version (default build/pc/gates[-jp])")
    args = ap.parse_args()
    build_dir = Path(args.build).resolve() if args.build else ROOT / "build/pc" / ("gates" if args.ver == "us" else "gates-jp")
    failed: list[str] = []

    def check(ok: bool, what: str, detail: str = "") -> None:
        print(f"ok    {what}" if ok else f"FAIL  {what}" + (f"\n      {detail.strip()}" if detail.strip() else ""))
        if not ok:
            failed.append(what)

    work = build_dir / "fieldspeed-ui"
    shutil.rmtree(work, ignore_errors=True)
    try:
        if platform.machine() not in ("x86_64", "AMD64"):
            raise Broken("the generated stubs are x86-64 assembly")
        cache = (build_dir / "CMakeCache.txt").read_text() if (build_dir / "CMakeCache.txt").is_file() else ""
        if re.search(rf"^XG_VERSION:\w+={args.ver.upper()}$", cache, re.M) is None:
            raise Broken(f"{build_dir} is not a configured {args.ver.upper()} build")
        work.mkdir(parents=True)
        fixtures = Fixtures(build_dir, work)
        row, n = fixtures.build("row", "row", SECTIONS)
        print(f"      row fixture: production sections.cpp, {n} generated stubs; by hand: {HAND_STUBS['row']}")
        row_keys = row_checks(row, work, check, "row")
        registry, n = fixtures.build("registry", "registry", OPTIONS)
        print(f"      registry fixture: production options.cpp, {n} generated stubs; by hand: {HAND_STUBS['registry']}")
        registry_checks(registry, work, check, "registry", row_keys)
        for kind in ("row", "registry"):
            print(f"      generated stubs that ran in the {kind} fixture ({len(reached[kind])}, each returned 0): "
                  + " ".join(sorted(reached[kind])))

        for name, kind, path, old, new, must in MUTANTS:
            text = path.read_text()
            if text.count(old) != 1:
                check(False, f"control {name}: its line is in {path.relative_to(ROOT)} once")
                continue
            try:
                copy = work / f"{name}-{path.name}"
                copy.write_text(text.replace(old, new))
                exe, _ = fixtures.build(kind, name, copy)
                missed: list[str] = []
                quiet = lambda ok, what, detail="": None if ok else missed.append(what)
                if kind == "row":
                    row_checks(exe, work, quiet, None)
                else:
                    registry_checks(exe, work, quiet, None, row_keys)
                hit = [m for m in missed if m.startswith(must)]
                check(bool(hit), f"control {name}: built, ran to the end and failed {len(missed)} behavioural check(s), "
                                 f"among them: {hit[0] if hit else must + ' (NOT failed)'}", "; ".join(missed))
                for m in missed:
                    print(f"        failed there: {m}")
            except Broken as e:
                check(False, f"control {name}: the changed copy builds and runs to the end (a break is not a catch)", str(e))
    except Broken as e:
        check(False, "the fixtures build and run", str(e))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print(f"field speed in the menu ({args.ver}): " + ("passed" if not failed else f"{len(failed)} FAILED"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
