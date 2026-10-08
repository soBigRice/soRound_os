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
HAN = re.compile(r"[\u3400-\u9fff]")
COPY_ATTRIBUTES = {"content", "alt", "aria-label", "data-description"}


class EnglishPage(HTMLParser):
    """Translate a single template; fail when new Chinese copy has no English entry."""
    def __init__(self, messages):
        super().__init__(convert_charrefs=False)
        self.messages = messages
        self.parts = []
        self.language_link = False

    def translate(self, value):
        message = value.strip()
        if message in self.messages:
            start = len(value) - len(value.lstrip())
            end = len(value.rstrip())
            return value[:start] + self.messages[message] + value[end:]
        if HAN.search(value):
            raise ValueError(f"Missing English translation: {message}")
        return value

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        self.language_link = tag == "a" and attrs.get("class") == "language-switch"
        for key, value in attributes:
            if value is None:
                continue
            if key in COPY_ATTRIBUTES:
                attrs[key] = self.translate(value)
            if key in {"href", "src"}:
                if value.startswith("assets/apps/"):
                    attrs[key] = "../assets/apps/en/" + value.removeprefix("assets/apps/")
                elif value.startswith("assets/") or value in {"styles.css", "script.js"}:
                    attrs[key] = "../" + value
        if tag == "html":
            attrs["lang"] = "en"
        if (tag == "link" and attrs.get("rel") == "canonical") or attrs.get("property") == "og:url":
            attrs["href" if tag == "link" else "content"] = SITE_URL + "en/"
        if self.language_link:
            attrs.update(href="../", lang="zh-CN", hreflang="zh-CN", **{"aria-label": "Switch to Chinese"})
        if attrs.get("href") == f"https://github.com/{REPO}#快速开始主线固件":
            attrs["href"] = f"https://github.com/{REPO}/blob/main/README.en.md#quick-start-firmware"
        serialized = "".join(" " + key + ("" if value is None else '="' + html.escape(value, quote=True) + '"')
                             for key, value in attrs.items())
        self.parts.append(f"<{tag}{serialized}>")

    def handle_endtag(self, tag):
        self.parts.append(f"</{tag}>")
        if tag == "a":
            self.language_link = False

    def handle_data(self, data):
        self.parts.append(html.escape("中文" if self.language_link else self.translate(data), quote=False))

    def handle_decl(self, declaration):
        self.parts.append(f"<!{declaration}>")

    def handle_comment(self, comment):
        self.parts.append(f"<!--{comment}-->")

    def handle_entityref(self, name):
        self.parts.append(f"&{name};")

    def handle_charref(self, name):
        self.parts.append(f"&#{name};")


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
    # Fetch the official latest stable endpoint separately: more than 100 beta
    # releases must not hide an older stable release from the site builder.
    stable_request = Request(f"https://api.github.com/repos/{REPO}/releases/latest", headers=headers)
    with urlopen(stable_request, timeout=30) as response:
        stable = json.load(response)
    if stable["draft"] or stable["prerelease"]:
        raise ValueError("Latest stable endpoint returned an unpublished or beta release")
    # A stable publication also replaces the beta OTA object. Both download
    # entries must follow that promotion until a newer beta is published.
    if beta is None or stable["published_at"] >= beta["published_at"]:
        beta = stable
    return {channel: {"tag": r["tag_name"], "url": r["html_url"]}
            for channel, r in (("stable", stable), ("beta", beta))}


class PageCheck(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.fragments = []
        self.local_files = []
        self.h1_count = 0
        self.app_ids = []
        self.copy = []
        self.skip_copy = False

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == "script" or attrs.get("class") == "language-switch":
            self.skip_copy = True
        self.copy.extend(value for key, value in attributes if key in COPY_ATTRIBUTES and value)
        if "data-app" in attrs:
            self.app_ids.append(attrs["data-app"])
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

    def handle_endtag(self, tag):
        if tag in {"script", "a"}:
            self.skip_copy = False

    def handle_data(self, data):
        if not self.skip_copy:
            self.copy.append(data)


def validate_page(output, language):
    directory = output / "en" if language == "en" else output
    page = (directory / "index.html").read_text()
    if "{{" in page:
        raise ValueError("Unresolved website template")
    check = PageCheck()
    check.feed(page)
    if check.h1_count != 1:
        raise ValueError("Website must have exactly one h1")
    for fragment in check.fragments:
        if fragment not in check.ids:
            raise ValueError(f"Missing anchor: {fragment}")
    for file in check.local_files:
        destination = (directory / file).resolve()
        if destination.is_dir():
            destination /= "index.html"
        if not destination.is_relative_to(output) or not destination.is_file():
            raise ValueError(f"Missing public asset: {file}")
    if language == "en" and any(HAN.search(value) for value in check.copy):
        raise ValueError("Untranslated English page copy")
    expected_apps = {"weather", "calendar", "countdown", "stopwatch", "settings", "ota", "wifi",
                     "i2c", "system", "audio", "level", "remote", "twin", "maze", "fluid", "dice",
                     "answers", "zodiac", "merit"}
    if len(check.app_ids) != 19 or set(check.app_ids) != expected_apps:
        raise ValueError("Website must expose all 19 app previews exactly once")
    for app in check.app_ids:
        prefix = "en/" if language == "en" else ""
        data = (output / f"assets/apps/{prefix}{app}.png").read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", data[16:24]) != (466, 466):
            raise ValueError(f"Native {app} preview must preserve 466 × 466 pixels")
    print(f"{language} verified: {len(check.fragments)} anchors, {len(check.local_files)} references, 19 native app previews")


def validate(output):
    for language in ("zh-CN", "en"):
        validate_page(output, language)
    for theme in ("type", "orbit", "shift"):
        data = (output / f"assets/{theme}.png").read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", data[16:24]) != (1680, 1220):
            raise ValueError(f"Native {theme} contact-sheet coordinates no longer match; update web crops")
    css = (output / "styles.css").read_text()
    for asset in re.findall(r'url\("([^\"]+)"\)', css):
        if not (output / asset).is_file():
            raise ValueError(f"Missing CSS asset: {asset}")


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
    messages = json.loads((SOURCE / "locales/en.json").read_text())
    if not all(isinstance(value, str) and value and not HAN.search(value) for value in messages.values()):
        raise ValueError("English messages must be nonempty English strings")
    for message, translation in messages.items():
        if set(re.findall(r"\{\w+\}", message)) != set(re.findall(r"\{\w+\}", translation)):
            raise ValueError(f"Translation placeholders differ: {message}")
    # Dynamic descriptions and accessible names share the same catalog as static HTML.
    for message in re.findall(r'["\']([^"\']*[\u3400-\u9fff][^"\']*)["\']', (SOURCE / "script.js").read_text()):
        if message not in messages:
            raise ValueError(f"Missing English script translation: {message}")
    english = EnglishPage(messages)
    english.feed(page)
    encoded_messages = json.dumps(messages, ensure_ascii=False).replace("<", "\\u003c")
    (output / "index.html").write_text(page.replace("{{site_messages}}", "{}"))
    (output / "en").mkdir()
    (output / "en/index.html").write_text("".join(english.parts).replace("{{site_messages}}", encoded_messages))
    (output / ".nojekyll").touch()
    (output / "sitemap.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
        f'<url><loc>{SITE_URL}</loc></url><url><loc>{SITE_URL}en/</loc></url></urlset>\n')
    (output / "robots.txt").write_text(f"User-agent: *\nAllow: /\nSitemap: {SITE_URL}sitemap.xml\n")
    validate(output)
    print("Release links: " + ", ".join(snapshot[c]["tag"] for c in ("stable", "beta")))
    print(f"Public output: {output}")


if __name__ == "__main__":
    main()
