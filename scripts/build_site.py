#!/usr/bin/env python3
"""Build only the public website; optionally refresh public GitHub Release links."""
import argparse
import html
import json
import os
from pathlib import Path
import re
import shutil
import struct
from html.parser import HTMLParser
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site"
REPO = "soBigRice/soRound_os"
RELEASE_URL = f"https://github.com/{REPO}/releases/tag/"
SITE_URL = "https://sobigrice.github.io/soRound_os/"


def release_snapshot():
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "soRound-site-builder"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = Request(f"https://api.github.com/repos/{REPO}/releases?per_page=100", headers=headers)
    with urlopen(request, timeout=30) as response:
        releases = json.load(response)
    # GitHub returns creation order, which may differ from publication order.
    published = sorted((r for r in releases if not r["draft"] and r["published_at"]),
                       key=lambda r: r["published_at"], reverse=True)
    beta = next((r for r in published if r["prerelease"]), None)
    if beta is None:
        raise ValueError("No published beta release found")
    # Fetch the official latest stable endpoint separately: more than 100 beta
    # releases must not hide an older stable release from the site builder.
    stable_request = Request(f"https://api.github.com/repos/{REPO}/releases/latest", headers=headers)
    with urlopen(stable_request, timeout=30) as response:
        stable = json.load(response)
    if stable["draft"] or stable["prerelease"]:
        raise ValueError("Latest stable endpoint returned an unpublished or beta release")
    return {channel: {"tag": r["tag_name"], "url": r["html_url"]}
            for channel, r in (("stable", stable), ("beta", beta))}


class PageCheck(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.fragments = []
        self.local_files = []
        self.h1_count = 0

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if "id" in attrs:
            if attrs["id"] in self.ids:
                raise ValueError(f"Duplicate element id: {attrs['id']}")
            self.ids.add(attrs["id"])
        if tag == "h1":
            self.h1_count += 1
        for key in ("src", "href"):
            value = attrs.get(key, "")
            if value.startswith("#") and len(value) > 1:
                self.fragments.append(value[1:])
            elif value and not value.startswith(("https://", "#")):
                self.local_files.append(value)
        if attrs.get("target") == "_blank" and "noopener" not in attrs.get("rel", "").split():
            raise ValueError("External tab link lacks noopener")


def validate(output):
    page = (output / "index.html").read_text()
    if "{{" in page:
        raise ValueError("Unresolved release template")
    check = PageCheck()
    check.feed(page)
    if check.h1_count != 1:
        raise ValueError("Website must have exactly one h1")
    for fragment in check.fragments:
        if fragment not in check.ids:
            raise ValueError(f"Missing anchor: {fragment}")
    for file in check.local_files:
        if not (output / file).is_file():
            raise ValueError(f"Missing public asset: {file}")
    for theme in ("type", "orbit", "shift"):
        data = (output / f"assets/{theme}.png").read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", data[16:24]) != (1680, 1220):
            raise ValueError(f"Native {theme} contact-sheet coordinates no longer match; update web crops")
    css = (output / "styles.css").read_text()
    for asset in re.findall(r'url\("([^\"]+)"\)', css):
        if not (output / asset).is_file():
            raise ValueError(f"Missing CSS asset: {asset}")
    print(f"Website verified: {len(check.fragments)} anchors, {len(check.local_files)} references, 15 native previews")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/site")
    parser.add_argument("--refresh-releases", action="store_true", help="Read public release metadata from GitHub")
    args = parser.parse_args()
    output = args.out.resolve()
    # Only our dedicated ignored output is owned by this script. Avoid accidental
    # deletion when a caller supplies the firmware build directory or source tree.
    if output != ROOT / "build/site" or (ROOT / "build").is_symlink() or (ROOT / "build/site").is_symlink():
        parser.error("--out must point to this repository's build/site directory")
    snapshot = release_snapshot() if args.refresh_releases else json.loads((SOURCE / "releases.json").read_text())
    replacements = {}
    for channel in ("stable", "beta"):
        release = snapshot[channel]
        if not re.fullmatch(r"v[0-9][A-Za-z0-9.+-]*", release["tag"]):
            raise ValueError(f"Invalid {channel} release tag")
        if release["url"] != RELEASE_URL + release["tag"]:
            raise ValueError(f"Unexpected {channel} release destination")
        replacements[channel + "_tag"] = release["tag"]
        replacements[channel + "_url"] = release["url"]
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    shutil.copytree(SOURCE / "assets", output / "assets")
    for file in ("styles.css", "script.js"):
        shutil.copy2(SOURCE / file, output / file)
    page = (SOURCE / "index.html").read_text()
    for key, value in replacements.items():
        page = page.replace("{{" + key + "}}", html.escape(value, quote=True))
    (output / "index.html").write_text(page)
    (output / ".nojekyll").touch()
    (output / "sitemap.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
        f'<url><loc>{SITE_URL}</loc></url></urlset>\n')
    (output / "robots.txt").write_text(f"User-agent: *\nAllow: /\nSitemap: {SITE_URL}sitemap.xml\n")
    validate(output)
    print("Release links: " + ", ".join(snapshot[c]["tag"] for c in ("stable", "beta")))
    print(f"Public output: {output}")


if __name__ == "__main__":
    main()
