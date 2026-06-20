"""Fetch vendored dependencies (sqlite3 amalgamation + Dear ImGui) into third_party/.
Run once: py game/third_party/_fetch_deps.py
"""
import io
import json
import re
import urllib.request
import zipfile
from pathlib import Path

TP = Path(__file__).resolve().parent


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "gns-fetch"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def fetch_sqlite():
    page = get("https://sqlite.org/download.html").decode("utf-8", "replace")
    m = re.search(r"(\d{4}/sqlite-amalgamation-\d+\.zip)", page)
    if not m:
        raise SystemExit("sqlite amalgamation link not found")
    rel = m.group(1)
    print("sqlite:", rel)
    data = get("https://sqlite.org/" + rel)
    out = TP / "sqlite3"
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name in z.namelist():
            base = name.split("/")[-1]
            if base in ("sqlite3.c", "sqlite3.h"):
                (out / base).write_bytes(z.read(name))
                print("  wrote", base)


def fetch_imgui():
    tag = json.loads(get("https://api.github.com/repos/ocornut/imgui/releases/latest")
                     .decode("utf-8"))["tag_name"]
    print("imgui:", tag)
    base = f"https://raw.githubusercontent.com/ocornut/imgui/{tag}/"
    core = ["imgui.cpp", "imgui.h", "imgui_demo.cpp", "imgui_draw.cpp", "imgui_tables.cpp",
            "imgui_widgets.cpp", "imgui_internal.h", "imconfig.h",
            "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"]
    backends = ["imgui_impl_sdl2.cpp", "imgui_impl_sdl2.h",
                "imgui_impl_sdlrenderer2.cpp", "imgui_impl_sdlrenderer2.h"]
    idir = TP / "imgui"
    (idir / "backends").mkdir(parents=True, exist_ok=True)
    for f in core:
        (idir / f).write_bytes(get(base + f))
    for f in backends:
        (idir / "backends" / f).write_bytes(get(base + "backends/" + f))
    print(f"  wrote {len(core)} core + {len(backends)} backend files")
    (idir / "LICENSE.txt").write_bytes(get(base + "LICENSE.txt"))


if __name__ == "__main__":
    fetch_sqlite()
    fetch_imgui()
    print("done.")
