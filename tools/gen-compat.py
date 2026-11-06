#!/usr/bin/env python3
"""Records which Omarchy command each typed setting's write was reviewed against.

Writes src/data/compat.json from the catalog, the discovery inventory, and the
commands installed here. Run this only after re-reading a changed command's source
and confirming its adapter still fits. It is the reviewer's signature.
"""
import hashlib, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
inventory = json.load(open(ROOT + "/docs/inventory-2026-11-05.json"))
routes = sorted(((c["route"], c["binary"]) for c in inventory["commands"]), key=lambda r: -len(r[0]))
settings = json.load(open(ROOT + "/src/data/settings.json"))

out = {"omarchy": subprocess.run(["omarchy", "version"], capture_output=True, text=True).stdout.strip(), "hyprland": "0.56", "settings": {}, "commands": {}}
for entry in settings:
    write = entry.get("write") or {}
    if write.get("kind") != "cmd":
        continue
    text = " ".join(part for part in write["argv"] if "{" not in part)
    binary = next((b for route, b in routes if (text + " ").startswith(route + " ")), None)
    if not binary:
        continue
    out["settings"][entry["id"]] = binary
    with open("/usr/bin/" + binary, "rb") as f:
        out["commands"][binary] = hashlib.sha256(f.read()).hexdigest()
json.dump(out, open(ROOT + "/src/data/compat.json", "w"), indent=1, sort_keys=True)
open(ROOT + "/src/data/compat.json", "a").write("\n")
print(f"{len(out['settings'])} settings pinned to {len(out['commands'])} commands, on Omarchy {out['omarchy']}")
