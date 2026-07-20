#!/usr/bin/env python3
"""Render docs/keymap/keymap.{svg,png} from config/prototype_mk1.keymap.

parse -> classify key categories -> inject encoder knobs -> draw -> png
Run from the repo root (locally or in CI); see .github/workflows/draw-keymap.yml.
"""
import os
import re
import string
import subprocess
import sys
import tempfile
from pathlib import Path

ENV = {**os.environ, "PYTHONUTF8": "1"}  # unicode legends vs windows cp1252
BG_COLOR = "#10131a"  # swap to the user's exact background when provided

import yaml

ROOT = Path(__file__).resolve().parent.parent
KEYMAP = ROOT / "config" / "prototype_mk1.keymap"
CONFIG = ROOT / "docs" / "keymap" / "keymap_drawer.config.yaml"
LAYOUT = ROOT / "boards" / "shields" / "prototype_mk1" / "layouts" / "prototype_mk1_layout.dtsi"
OUT_SVG = ROOT / "docs" / "keymap" / "keymap.svg"
OUT_PNG = ROOT / "docs" / "keymap" / "keymap.png"

# knob widgets drawn in the top of the split gap (drawing-only, not real keys)
KNOB_ATTRS = ["<&key_physical_attrs 100 100  630   70       0     0     0>",
              "<&key_physical_attrs 100 100  920   70       0     0     0>"]

MODS = {"Ctrl", "Alt", "CAPS"}
NAV = {"↑", "↓", "←", "→", "HOME", "END", "PG_UP", "PG_DN", "INS"}
SYSTEMISH = ("BT", "USB/BT", "BOOT", "RESET", "Studio")


def legend_of(key):
    if key is None:
        return ""
    if isinstance(key, dict):
        return str(key.get("t", ""))
    return str(key)


def classify(key, layer=""):
    t = legend_of(key)
    hold = key.get("h", "") if isinstance(key, dict) else ""
    if layer == "SETTINGS":
        if t.startswith("BT") or t == "USB/BT":
            return "bt"
        if hold == "NV":
            return "nv"
    if layer == "GAMES" and t in {"W", "A", "S", "D"}:
        return "wasd"
    if not t or t == "▽":
        return None
    if t.startswith(("⇧", "⊞", "⌥", "$$win$$")) or t in MODS:
        return "mod"
    if t in NAV:
        return "nav"
    if re.fullmatch(r"F\d{1,2}", t):
        return "fkey"
    if re.fullmatch(r"\d", t) or t in {"KP_DOT", "."} and False:
        return "number"
    if t in {"Vol +", "Vol -", "Mute", "Play", "Next", "Prev", "Shuf"}:
        return "media"
    if t.startswith(SYSTEMISH) or t in {"Unlock", "BL", "BL +", "BL -"}:
        return "system"
    if len(t) == 1 and t in string.ascii_uppercase:
        return None  # letters keep the plain face
    if len(t) <= 2 and all(c in string.punctuation for c in t):
        return "sym"
    return "macro"


def keycode_disp(code, kc_map):
    code = code.strip()
    return kc_map.get(code, code.replace("_", " "))


SPECIAL_CODES = {
    "LC(KP_PLUS)": "Zoom +", "LC(KP_MINUS)": "Zoom -",
    "LC(Z)": "Undo", "LC(Y)": "Redo",
    "LC(LG(RIGHT))": "Desk →", "LG(LC(LEFT))": "Desk ←",
}


def code_disp(code, kc_map):
    code = code.strip()
    return SPECIAL_CODES.get(code) or keycode_disp(code, kc_map)


def sensor_behaviors(keymap_src):
    """{&name: (cw_code, ccw_code)} from zmk,behavior-sensor-rotate definitions"""
    out = {}
    for m in re.finditer(
            r"(\w+): \w+ \{[^}]*?behavior-sensor-rotate[^}]*?"
            r"bindings =\s*<&kp ([^>]+)>\s*,\s*<&kp ([^>]+)>", keymap_src, re.S):
        out["&" + m.group(1)] = (m.group(2).strip(), m.group(3).strip())
    return out


def encoder_legends(binding, kc_map, behaviors):
    """sensor binding -> (cw, ccw) display strings"""
    b = binding.strip()
    if b.startswith("&inc_dec_kp"):
        cw, ccw = b.split()[1], b.split()[2]
        return code_disp(cw, kc_map), code_disp(ccw, kc_map)
    if b in behaviors:
        cw, ccw = behaviors[b]
        return code_disp(cw, kc_map), code_disp(ccw, kc_map)
    return (b.lstrip("&"), "")


def sensor_bindings_per_layer(keymap_src):
    """{layer_label: [binding0, binding1]} straight from the keymap source
    (keymap-drawer names layers by the zmk `label`, which follows sensor-bindings)"""
    out = {}
    for block in re.finditer(
            r"sensor-bindings =\s*(.*?);.*?label = \"([^\"]+)\"", keymap_src, re.S):
        raw, name = block.group(1), block.group(2)
        out[name] = [m.group(1).strip() for m in re.finditer(r"<([^>]+)>", raw)]
    return out


def main():
    cfg = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))
    kc_map = {k: v for k, v in cfg["parse_config"]["zmk_keycode_map"].items()}

    parsed = subprocess.run(
        ["keymap", "-c", str(CONFIG), "parse", "-z", str(KEYMAP)],
        capture_output=True, text=True, check=True, env=ENV, encoding="utf-8")
    km = yaml.safe_load(parsed.stdout)

    # category classes
    for lname, layer in km["layers"].items():
        for i, key in enumerate(layer):
            cls = classify(key, lname)
            if cls is None:
                continue
            if isinstance(key, dict):
                key.setdefault("type", cls)
            else:
                layer[i] = {"t": key, "type": cls}

    layer_lists = list(km["layers"].values())
    base = layer_lists[0]

    # held keys: the layer's own key stays red with its name; combo-held keys
    # (e.g. LOWER+RAISE while on SETTINGS) render as ghosts of what they are
    for lname, layer in km["layers"].items():
        for i, key in enumerate(layer):
            if not (isinstance(key, dict) and key.get("type") == "held" and not key.get("t")):
                continue
            under = base[i] if i < len(base) else None
            if legend_of(under) == lname:
                key["t"] = lname
            elif isinstance(under, dict):
                ghost = dict(under)
                ghost["type"] = "ghost"
                layer[i] = ghost
            else:
                layer[i] = {"t": legend_of(under), "type": "ghost"}

    # trans keys: show the BASE key underneath, ghosted
    for layer in layer_lists[1:]:
        for i, key in enumerate(layer):
            is_trans = key is None or key == "▽" or (
                isinstance(key, dict) and (key.get("type") == "trans" or key.get("t") == "▽"))
            if not is_trans:
                continue
            under = base[i] if i < len(base) else None
            if isinstance(under, dict):
                ghost = dict(under)
                ghost["type"] = "ghost"
            elif under:
                ghost = {"t": str(under), "type": "ghost"}
            else:
                ghost = {"t": "", "type": "ghost"}
            layer[i] = ghost

    # knob widgets from sensor-bindings (left knob, right knob appended per layer)
    keymap_src = KEYMAP.read_text(encoding="utf-8")
    sensors = sensor_bindings_per_layer(keymap_src)
    behaviors = sensor_behaviors(keymap_src)
    layer_names = list(km["layers"])
    for name in layer_names:
        bindings = sensors.get(name, [])
        for idx in range(2):
            if idx < len(bindings):
                cw, ccw = encoder_legends(bindings[idx], kc_map, behaviors)
                cw, ccw = cw.replace(" ", ""), ccw.replace(" ", "")
                legend = f"{cw} {ccw}".strip()
                km["layers"][name].append({"t": legend, "type": "encoder"})
            else:
                km["layers"][name].append({"t": "", "type": "encoder"})

    # drawing layout = real physical layout + the two knob positions
    dts = LAYOUT.read_text(encoding="utf-8")
    dts = dts.replace("            ;", "            , " + "\n            , ".join(KNOB_ATTRS) + "\n            ;")
    with tempfile.NamedTemporaryFile("w", suffix=".dtsi", delete=False,
                                     newline="\n", encoding="utf-8") as f:
        f.write(dts)
        tmp_dts = f.name
    km["layout"] = {"dts_layout": tmp_dts}

    with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False,
                                     newline="\n", encoding="utf-8") as f:
        yaml.safe_dump(km, f, allow_unicode=True, sort_keys=False)
        tmp_yaml = f.name

    drawn = subprocess.run(["keymap", "-c", str(CONFIG), "draw", tmp_yaml],
                           capture_output=True, text=True, check=True, env=ENV,
                           encoding="utf-8")
    svg = drawn.stdout
    # explicit background rect (CSS background-color is ignored by raster renderers)
    m = re.search(r'<svg[^>]*width="(\d+)"[^>]*height="(\d+)"[^>]*>', svg)
    if m:
        bg = f'<rect x="0" y="0" width="{m.group(1)}" height="{m.group(2)}" fill="{BG_COLOR}"/>'
        svg = svg[:m.end()] + bg + svg[m.end():]

    # encoder keys draw as circles: corner radius must be an attribute for resvg
    def circleify(mm):
        tag = mm.group(0)
        w = re.search(r'width="([\d.]+)"', tag)
        radius = float(w.group(1)) / 2 if w else 26
        tag = re.sub(r'r[xy]="[\d.]+" ?', "", tag)
        return tag.replace("<rect", f'<rect rx="{radius}" ry="{radius}" ', 1)

    svg = re.sub(r'<rect[^>]*class="key encoder[^"]*"[^>]*/>', circleify, svg)
    OUT_SVG.write_text(svg, encoding="utf-8", newline="\n")

    import resvg_py
    png = resvg_py.svg_to_bytes(svg_path=str(OUT_SVG), zoom=2.0)
    OUT_PNG.write_bytes(bytes(png))
    print(f"wrote {OUT_SVG.name} + {OUT_PNG.name}")


if __name__ == "__main__":
    sys.exit(main())
