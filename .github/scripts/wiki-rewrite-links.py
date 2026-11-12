#!/usr/bin/env python3
"""Turn the links in the repository's pages into links that work on the GitHub wiki.

    wiki-rewrite-links.py <wiki-dir> <owner/repo> <source-root> <pages-dir>

Run by .github/workflows/wiki.yml on a COPY of the pages, inside the wiki's
checkout; <pages-dir> (docs) is where they live in the repository, where their
relative links work as they are.

The wiki is one flat directory served from its own repository, so three kinds
of link break there:

  img/display.png, ../LICENSE   a file of this repository. Becomes an absolute
                                github.com link: /raw/main for an image (a blob
                                link is a web page, which an image cannot
                                show), /tree/main for a directory, /blob/main
                                for any other file.
  building.md#steps             another page. The wiki serves a ".md" address
                                as the raw file, so the extension goes and the
                                anchor stays.

Absolute links, plain anchors and fenced code are left alone. A relative link
whose target does not exist is reported and fails the run: on the wiki it would
be a dead link nobody sees until a reader follows it.
"""
import os
import re
import sys
from pathlib import Path

# "[text](target)" with an optional "!" in front; the text may not contain "]".
LINK = re.compile(r"(?P<image>!?)\[(?P<text>[^\]]*)\]\((?P<target>[^)\s]+)(?P<title>\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^\s*(```|~~~)")


def rewrite_line(line, repo, root, pages_dir, pages, missing, where):
    def one(match):
        target = match.group("target")
        if re.match(r"[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith(("#", "/")):
            return match.group(0)
        path, _, anchor = target.partition("#")
        anchor = f"#{anchor}" if anchor else ""
        if "/" not in path and path.endswith(".md"):
            if path not in pages:
                missing.append(f"{where}: {target} is not a page")
                return match.group(0)
            new = path[:-3] + anchor
        else:
            rel = os.path.normpath(os.path.join(pages_dir, path))
            if rel.startswith(".."):
                missing.append(f"{where}: {target} points outside the repository")
                return match.group(0)
            there = root / rel
            if not there.exists():
                missing.append(f"{where}: {target} does not exist")
                return match.group(0)
            kind = "tree" if there.is_dir() else ("raw" if match.group("image") else "blob")
            new = f"https://github.com/{repo}/{kind}/main/{rel}{anchor}"
        return f"{match.group('image')}[{match.group('text')}]({new}{match.group('title') or ''})"

    return LINK.sub(one, line)


def rewrite(text, repo, root, pages_dir, pages, missing, name):
    out, fenced = [], False
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        if FENCE.match(line):
            fenced = not fenced
        out.append(line if fenced else rewrite_line(line, repo, root, pages_dir, pages, missing, f"{name}:{number}"))
    return "".join(out)


def main():
    if len(sys.argv) != 5:
        print(f"usage: {sys.argv[0]} <wiki-dir> <owner/repo> <source-root> <pages-dir>", file=sys.stderr)
        return 2
    wiki, repo, root, pages_dir = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]).resolve(), sys.argv[4].strip("/")
    if not wiki.is_dir() or not (root / pages_dir).is_dir():
        print(f"wiki-rewrite-links: {wiki} or {root / pages_dir} is not a directory", file=sys.stderr)
        return 1
    pages = {p.name for p in (root / pages_dir).glob("*.md")}
    missing = []
    changed = 0
    for page in sorted(wiki.glob("*.md")):
        if page.name not in pages:
            continue  # not one of ours: nothing here is rewritten or judged
        before = page.read_text(encoding="utf-8")
        after = rewrite(before, repo, root, pages_dir, pages, missing, page.name)
        if after != before:
            page.write_text(after, encoding="utf-8")
            changed += 1
    for line in missing:
        print(f"wiki-rewrite-links: {line}", file=sys.stderr)
    print(f"wiki-rewrite-links: rewrote links in {changed} page(s), {len(missing)} dead link(s)")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
