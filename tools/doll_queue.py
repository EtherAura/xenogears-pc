#!/usr/bin/env python3
"""Preserve blocked dolls and queue an explicit retry, without editing pages, raw images or cells.

    python tools/doll_queue.py queue <set dir> g304 g305 g311
    python tools/doll_queue.py settle <set dir> --park-only
    python tools/doll_queue.py settle <set dir> --restore-only

Only the conveyor calls settle, after every drawing worker of that set has exited. Workers write
blocked/<doll>.json with page, reason and kind (refusal or quality), then continue independent dolls.
Queue requests can be written while a set draws; they take effect after that run and its shipping.
"""
import argparse
import datetime
import fcntl
import json
from pathlib import Path
import re
from types import SimpleNamespace

WORKFLOW = '''Blocked doll workflow (Carl,): a refused page or exhausted quality tries stops that
doll only. Keep every raw image and completed page. Write blocked/<doll>.json in this set, with
{"page": "<page>", "kind": "refusal" or "quality", "reason": "what happened"}. Never change a prompt,
attachment, model or policy to work around a refusal. Do not retry that page again in this run.
Skip dolls with an existing blocked report and any dolls waiting on them in after.json; continue all
independent dolls. A usage limit or unavailable image tool still stops the run. The conveyor parks
blocked dolls and their dependents only after every worker of this set exits; it preserves their work
and lets the healthy set finish and ship. A parked doll waits for an explicit requeue, never an
automatic retry loop. Requeue resumes at next's first un-ingested page with one fresh try and a
conditional second; existing failed raw tries are archived, completed pages stay untouched.
Report blocked/parked, queued, drawn, visually approved and installed separately.'''


def write(path, data):
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(data, indent=2) + '\n')
    tmp.replace(path)


def rows(path):
    return {line.split('|')[0]: line for line in path.read_text().splitlines() if line.strip()} if path.exists() else {}


def valid(g):
    if not re.fullmatch(r'g[0-9]+', g):
        raise ValueError(f'invalid doll: {g}')
    return g


def queue(setdir, dolls):
    parked = setdir / 'parked'
    parked.mkdir(exist_ok=True)
    with (parked / 'queue.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = parked / 'queue.json'
        data = json.loads(path.read_text()) if path.exists() else {}
        table = rows(parked / 'dolls.txt')
        for g in dolls:
            valid(g)
            if g not in table or not (parked / g / 'doll/pages.json').is_file():
                raise ValueError(f'{g}: no preserved parked doll')
            if (setdir / g).exists():
                raise ValueError(f'{g}: active folder already exists')
        for g in dolls:
            if data.get(g, {}).get('status') != 'queued':
                data[g] = {'status': 'queued', 'requested_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        write(path, data)
    return dolls


def settle(setdir, restore=False):
    """Called only with the set idle; rename whole folders back to their original source paths.

    Receipts persist before table/prompt refresh, so an interrupted refresh is retried on the next tick.
    A restore request is consumed once; another block requires another explicit queue request.
    """
    parked = setdir / 'parked'
    parked.mkdir(exist_ok=True)
    with (parked / 'queue.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        manifest = parked / 'queue.json'
        data = json.loads(manifest.read_text()) if manifest.exists() else {}
        active, saved = rows(setdir / 'dolls.txt'), rows(parked / 'dolls.txt')
        for g, entry in data.items():
            if g == '_refresh':
                continue
            if entry['status'] == 'restored' and (setdir / g).exists():
                active[g] = saved[g]
            elif entry['status'] == 'parked':
                active.pop(g, None)
        changed = []
        if restore:
            targets = [g for g, entry in data.items() if entry['status'] == 'queued']
        else:
            reports = {}
            for path in sorted((setdir / 'blocked').glob('g*.json')):
                g = valid(path.stem)
                report = json.loads(path.read_text())
                if not report.get('reason') or report.get('kind') not in ('refusal', 'quality'):
                    raise ValueError(f'{path}: needs reason and refusal/quality kind')
                reports[g] = report
            targets = [g for g in reports if g in active]
            after = json.loads((setdir / 'after.json').read_text()) if (setdir / 'after.json').exists() else {}
            while True:
                extra = [g for g, parent in after.items() if parent in targets and g in active and g not in targets]
                if not extra:
                    break
                targets += extra
        for g in targets:
            valid(g)
            source, dest = (parked / g, setdir / g) if restore else (setdir / g, parked / g)
            if source.is_symlink() or dest.is_symlink() or (source.exists() and dest.exists()):
                raise ValueError(f'{g}: ambiguous folders; no overwrite')
            table = saved if restore else active
            if g not in table:
                raise ValueError(f'{g}: missing description')
            # Save the description before the rename; a second settle can resume either side of it.
            if not restore:
                saved[g] = active[g]
                (parked / 'dolls.txt').write_text('\n'.join(saved.values()) + '\n')
            if source.exists():
                source.rename(dest)
            if not (dest / 'doll/pages.json').is_file():
                raise ValueError(f'{g}: missing preserved pages')
            if restore:
                active[g] = saved[g]
                # Fresh retry filenames must never overwrite the failed masters from the previous run.
                import hashlib
                state = json.loads((dest / 'doll/pages.json').read_text())
                pending = [p['name'] for p in state['pages'] if not p.get('done')]
                archive = dest / 'rejected-tries' / ('requeue-' + datetime.datetime.now().strftime('%Y%m%d%H%M%S%f'))
                masters = [raw for page in pending for raw in (dest / 'doll/raw').glob(f'{page}.try*.png')]
                if masters:
                    hashes = {raw.name: hashlib.sha256(raw.read_bytes()).hexdigest() for raw in masters}
                    archive.mkdir(parents=True, exist_ok=True)
                    write(archive / 'sha256.json', hashes)
                    for raw in masters:
                        raw.rename(archive / raw.name)
            else:
                active.pop(g)
            data[g] = {**data.get(g, {}), 'status': 'restored' if restore else 'parked',
                       'report': data.get(g, {}).get('report', {}) if restore else reports.get(g, {'reason': 'waiting for parked prerequisite', 'kind': 'dependency'})}
            changed.append(g)
        if changed:
            data['_refresh'] = {'status': 'pending'}
        if not changed and '_refresh' not in data:
            return []
        write(manifest, data)
        # extract.tsv retains the original order even when jobs return after later dolls finished.
        order = [line.split('\t')[0] for line in (setdir / 'extract.tsv').read_text().splitlines() if line.strip()]
        order += [g for g in active if g not in order]
        (setdir / 'dolls.txt').write_text(''.join(active[g] + '\n' for g in order if g in active))
        import chapter_prep as prep
        name = setdir.name.removesuffix('-npcs')
        title = (setdir / 'PROMPT.md').read_text().splitlines()[0].removeprefix('# ').split(': paper dolls')[0]
        prep.cmd_reuse(SimpleNamespace(name=name))
        prep.cmd_prompts(SimpleNamespace(name=name, title=title, subjects='the characters described in the table',
                                        note='', dolls_only=True))
        # Keep reports with the parked evidence; don't re-park a restored job from its old report.
        archive = parked / 'reports'
        for g in data:
            report = setdir / 'blocked' / f'{g}.json'
            if report.is_file():
                archive.mkdir(exist_ok=True)
                report.rename(archive / f'{g}-{datetime.datetime.now().strftime("%Y%m%d%H%M%S%f")}.json')
        data.pop('_refresh', None)
        write(manifest, data)
        return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('queue', 'settle'))
    parser.add_argument('setdir', type=Path)
    parser.add_argument('dolls', nargs='*')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--park-only', action='store_true')
    mode.add_argument('--restore-only', action='store_true')
    args = parser.parse_args()
    if not args.setdir.name.endswith('-npcs') or args.setdir.is_symlink():
        parser.error('expected an NPC set directory')
    if args.command == 'queue':
        if not args.dolls:
            parser.error('name the parked dolls to retry')
        changed = queue(args.setdir, args.dolls)
    else:
        if not (args.park_only or args.restore_only):
            parser.error('settle needs --park-only or --restore-only; the set must be idle')
        changed = settle(args.setdir, restore=args.restore_only)
    print(f'{args.setdir.name}: {args.command}: ' + ', '.join(changed))


if __name__ == '__main__':
    main()
