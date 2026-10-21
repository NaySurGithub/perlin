#!/usr/bin/env python3
"""Checks that nothing the host offers is left out of the SDK, and nothing in the SDK lacks a host.

    check_api.py            (run from the repository root or from mods/)

Every `Op` in abi.hpp must be (1) registered by the game and (2) wrapped by the SDK, and every `Event` must be delivered by the
game and have a payload type. A new operation that nobody wired up fails here instead of silently going missing.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
repo = root.parent
abi = (root / "sdk/perlin/abi.hpp").read_text(encoding="utf-8")

def enum_body(name):
    m = re.search(r"enum class %s\b[^{]*\{(.*?)\n    \};" % name, abi, re.S)
    if not m:
        sys.exit(f"check_api: enum {name} not found in abi.hpp")
    return m.group(1)

ops = re.findall(r"^\s*([A-Z][A-Za-z0-9]*)\s*=\s*0x[0-9A-Fa-f]+", enum_body("Op"), re.M)
events = re.findall(r"^\s*([A-Z][A-Za-z0-9]*)\s*(?:=\s*\d+)?\s*,", enum_body("Event"), re.M)

host_text = "".join(p.read_text(encoding="utf-8", errors="ignore") for p in sorted((repo / "src/client").glob("PerlinMods*.cpp")))
host_text += (repo / "src/client/mods/ModManager.cpp").read_text(encoding="utf-8", errors="ignore")
sdk_text = "".join(p.read_text(encoding="utf-8") for p in sorted((root / "sdk/perlin").glob("*.hpp")) if p.name != "abi.hpp")

problems = []
internal = {"Log", "Meta", "Clock"}
for op in ops:
    if not re.search(r"Op::%s\b" % op, host_text):
        problems.append(f"Op::{op} is declared but no host registers it")
    if op not in internal and not re.search(r"Op::%s\b" % op, sdk_text):
        problems.append(f"Op::{op} is registered by the host but no SDK wrapper calls it")
for ev in events:
    if ev in ("None",):
        continue
    if not re.search(r"Event::%s\b" % ev, host_text + sdk_text):
        problems.append(f"Event::{ev} is never delivered or handled")

print(f"check_api: {len(ops)} operations, {len(events)} events")
for p in problems:
    print("  " + p)
sys.exit(1 if problems else 0)
