#!/usr/bin/env python3
import sys
import tempfile
from pathlib import Path


def gm_panel_parts(repo_root):
    cmake_path = repo_root / "components" / "web_ui" / "CMakeLists.txt"
    in_parts = False
    parts = []
    for raw in cmake_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line == "set(GM_PANEL_PARTS":
            in_parts = True
            continue
        if in_parts and line == ")":
            break
        if not in_parts or not line:
            continue
        path = line.replace("${CMAKE_CURRENT_SOURCE_DIR}", "components/web_ui")
        parts.append(repo_root / Path(path))
    return parts


def bundled_text(parts):
    chunks = []
    for part in parts:
        if not part.exists():
            raise FileNotFoundError(f"GM panel part not found: {part}")
        text = part.read_text(encoding="utf-8")
        chunks.append(text if text.endswith("\n") else text + "\n")
    return "".join(chunks)


def main():
    repo_root = Path(__file__).resolve().parents[3]
    bundle = repo_root / "components" / "web_ui" / "assets" / "gm_panel.js"
    expected = bundled_text(gm_panel_parts(repo_root))
    actual = bundle.read_text(encoding="utf-8") if bundle.exists() else ""
    if actual != expected:
        with tempfile.NamedTemporaryFile("w", delete=False, suffix=".gm_panel.js", encoding="utf-8") as tmp:
            tmp.write(expected)
            tmp_path = tmp.name
        print("gm_panel.js is stale.", file=sys.stderr)
        print(f"Expected bundle written to: {tmp_path}", file=sys.stderr)
        print("Rebuild with: python components\\web_ui\\assets\\build_gm_panel.py components\\web_ui\\assets\\gm_panel.js <GM_PANEL_PARTS from CMakeLists.txt>", file=sys.stderr)
        return 1
    print("gm_panel.js is up to date.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
