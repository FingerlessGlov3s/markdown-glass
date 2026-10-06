#!/usr/bin/env python3
"""Reports distribution releases that the package targets should follow.

Reads the targets from the table in scripts/package.sh and the development
image from scripts/Containerfile, asks endoflife.date about each
distribution, and prints a markdown report of:

  - releases newer than the newest one targeted (a target to add),
  - targets past or near their end of life (a target to drop or move on).

Exit status: 0 when there is nothing to do, 3 when the report lists
something, anything else on error. Run by .github/workflows/distro-watch.yml
once a month; it can also be run by hand. It needs network access.
"""

import datetime
import json
import pathlib
import re
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = "https://endoflife.date/api/v1/products/{}"
WARN_DAYS = 60  # mention an end of life this close
FINDINGS = 3

# Image name -> endoflife.date product. Tumbleweed is rolling and has no releases.
PRODUCTS = {
    "fedora": "fedora",
    "almalinux": "almalinux",
    "debian": "debian",
    "ubuntu": "ubuntu",
    "leap": "opensuse",
}


def targets():
    """(target, image) pairs from scripts/package.sh, plus the dev image."""
    table = (ROOT / "scripts/package.sh").read_text()
    found = re.findall(r"^(\S+)\s+(\S+)\s+build-[a-z]+\.sh\s*$", table, re.MULTILINE)
    containerfile = (ROOT / "scripts/Containerfile").read_text()
    dev = re.search(r"^FROM\s+(\S+)", containerfile, re.MULTILINE)
    if dev:
        found.append(("development image (scripts/Containerfile)", dev.group(1)))
    if not found:
        sys.exit("no targets found in scripts/package.sh")
    return found


def split_image(image):
    """('ubuntu', '22.04') from 'docker.io/library/ubuntu:22.04'."""
    name, _, tag = image.rpartition("/")[2].partition(":")
    return name, tag


_RELEASES = {}  # product -> its releases, so each product is fetched once


def releases(product):
    if product not in _RELEASES:
        with urllib.request.urlopen(API.format(product), timeout=30) as reply:
            _RELEASES[product] = json.load(reply)["result"]["releases"]
    return _RELEASES[product]


def date(text):
    return datetime.date.fromisoformat(text) if text else None


def main():
    today = datetime.date.today()
    lines = []
    notes = []  # worth printing, but nothing to act on yet
    newest = {}  # product -> newest targeted release date
    for target, image in targets():
        name, version = split_image(image)
        product = PRODUCTS.get(name)
        if not product:
            if name == "tumbleweed":
                continue  # rolling: always current
            lines.append(f"- **{target}**: `{image}` is not a distribution this script knows.")
            continue
        info = next((r for r in releases(product) if r["name"] == version), None)
        if info is None:
            # Usually a release targeted ahead of its date, such as an Ubuntu
            # interim release in the weeks before it comes out.
            notes.append(f"- {target}: {product} {version} is not listed on endoflife.date yet"
                         " (not released?).")
            continue
        released = date(info.get("releaseDate"))
        if released and (product not in newest or released > newest[product]):
            newest[product] = released
        eol = date(info.get("eolFrom"))
        if info.get("isEol"):
            lines.append(f"- **{target}**: {product} {version} reached end of life on {eol}. "
                         "Drop the target or move it to a newer release.")
        elif eol and (eol - today).days <= WARN_DAYS:
            lines.append(f"- **{target}**: {product} {version} reaches end of life on {eol}.")

    for product, since in sorted(newest.items()):
        for r in releases(product):
            released = date(r.get("releaseDate"))
            if released and since < released <= today and not r.get("isEol"):
                lines.append(f"- **New release**: {product} {r['name']} came out on {released}"
                             " and has no target yet.")

    if not lines:
        print("Every package target is a supported release, and none is missing.")
        if notes:
            print("\nNotes:\n" + "\n".join(notes))
        return 0
    print(f"Distribution check of {today} against endoflife.date. Targets live in the table at the"
          " top of `scripts/package.sh` and in `.github/workflows/build.yml`.\n")
    print("\n".join(lines))
    if notes:
        print("\nNotes:\n" + "\n".join(notes))
    return FINDINGS


if __name__ == "__main__":
    sys.exit(main())
