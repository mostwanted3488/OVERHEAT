#!/usr/bin/env python3
"""Builds ui/index.html (single self-contained file embedded in the plugin)
from ui/src/template.html by inlining the image and the font as data URIs.

    python3 tools/build_ui.py
"""
import base64
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "ui" / "src"
html = (src / "template.html").read_text(encoding="utf-8")


def data_uri(path: pathlib.Path, mime: str) -> str:
    return f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode()


html = html.replace("__DSR50__", data_uri(src / "assets" / "dsr50.png", "image/png"))
html = re.sub(r"__FONT_([A-Za-z0-9\-]+)__",
              lambda m: data_uri(src / "fonts" / f"{m.group(1)}.ttf", "font/ttf"), html)

leftover = [p for p in re.findall(r"__[A-Z0-9_]+__", html) if p != "__JUCE__"]
if leftover:
    raise SystemExit(f"unreplaced placeholders: {sorted(set(leftover))}")

out = root / "ui" / "index.html"
out.write_text(html, encoding="utf-8")
print(f"wrote {out.relative_to(root)} ({out.stat().st_size // 1024} KB)")
