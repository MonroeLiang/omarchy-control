---
name: omarchy-control
description: Read and change Omarchy desktop settings through the verified `omarchy-control` CLI. Use for appearance, displays, input, power, default apps, and other machine settings, and to find which Omarchy tool handles a task.
---

# Omarchy Control

`omarchy-control` is a reviewed interface to Omarchy's settings. Each change is
validated, journaled, and verified by reading the value back. Prefer it to
editing Omarchy's files.

```bash
omarchy-control search "stop sleeping"   # Find the setting or tool; runs nothing
omarchy-control get power.idle.lock      # Current value
omarchy-control plan power.idle.lock 600 # Validate without changing anything
omarchy-control apply power.idle.lock 600
omarchy-control history                  # What changed, with transaction ids
omarchy-control undo <transaction>       # Refuses if something edited it since
omarchy-control doctor                   # Compatibility and read checks
```

Add `--json` for machine-readable output. Commands exit 0 or 1, with errors on
stderr. `apply` exits 1 and explains itself when a change needs `--yes`: show that
consequence to the user before passing `--yes`.

A nonzero `apply` means the value was not confirmed. Report the message rather
than retrying with a different mechanism. Workflows such as installs, fingerprint
setup, and updates are interactive helpers: tell the user which one to open.
