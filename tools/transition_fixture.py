#!/usr/bin/env python3
"""Select the actual field transition renderers for the GPU fixture.

Keep definitions and declarations verbatim. The field translation unit also
contains movie and field-loading code, which a standalone GPU test cannot
link. COFF's reference-pointer sections prevent ordinary dead section removal
from discarding all those unrelated references, so select the functions before
compiling on every platform. Missing or duplicate required bodies fail closed.
"""
from pathlib import Path
import sys
from splice import top_level_items, function_name

FUNCTIONS = {
    'wideCaptureReady', 'wideEffectFrame', 'wideEffectDone',
    'wideTransitionStrips', 'wideFadeGrid', 'renderScreenTransitionPoly',
    'setupScreenTransitionPoly', 'fadeTransitionGridCorner',
    'renderTransitionFadeGrid', 'initTransitionFadeGrid', 'freeTransitionFadeGrid',
}


def main():
    source, target = map(Path, sys.argv[1:])
    items = []
    found = set()
    for item in top_level_items(source.read_text()):
        name = function_name(item)
        if name and name not in FUNCTIONS:
            continue
        if name:
            if name in found:
                raise SystemExit(f'duplicate fixture function: {name}')
            found.add(name)
        items.append(item)
    if found != FUNCTIONS:
        raise SystemExit(f'missing fixture functions: {sorted(FUNCTIONS-found)}')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('/* Generated from the actual game source; do not edit. */\n' + '\n\n'.join(items) + '\n')


if __name__ == '__main__':
    main()
