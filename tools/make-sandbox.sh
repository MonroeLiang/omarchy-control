#!/bin/bash
# Builds a synthetic machine under $1: fake commands on PATH, fake config, fictional
# values. The screenshot harness and manual runs point at this, never the real desktop.
set -eu
ROOT=$1
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,run,omarchy/default/i18n,omarchy/config/omarchy,sys-root}
H=$ROOT/home
mkdir -p "$H"/.config/{omarchy,hypr,autostart} "$H"/.local/state/omarchy/{current/theme/backgrounds,toggles/hypr,indicators,powerprofiles,defaults}

cat >"$H/.local/state/omarchy/current/theme/colors.toml" <<'T'
mode = "dark"
accent = "#ff9f0a"
selection = "#3a3a3c"
muted = "#8e8e93"
background = "#1c1c1e"
dark_background = "#161618"
darker_background = "#0e0e10"
lighter_background = "#2c2c2e"
foreground = "#f5f5f7"
dark_foreground = "#98989d"
red = "#ff5f4a"
yellow = "#ffc27a"
green = "#ff9f0a"
T
printf 'obsidian-circuit\n' >"$H/.local/state/omarchy/current/theme.name"
printf '[font]\nbase-size = 14\n' >"$H/.config/omarchy/shell.toml"

# Fictional themes and generated backgrounds, so the Appearance tiles have something to show.
sandbox_theme() {
  mkdir -p "$ROOT/omarchy/themes/$1"
  printf 'mode = "%s"\naccent = "%s"\nselection = "%s"\nbackground = "%s"\ndark_background = "%s"\nforeground = "%s"\n' "$2" "$3" "$4" "$5" "$6" "$7" >"$ROOT/omarchy/themes/$1/colors.toml"
}
sandbox_theme catppuccin dark "#cba6f7" "#45475a" "#1e1e2e" "#181825" "#cdd6f4"
sandbox_theme everforest dark "#a7c080" "#475258" "#2d353b" "#232a2e" "#d3c6aa"
sandbox_theme obsidian-circuit dark "#ff9f0a" "#3a3a3c" "#1c1c1e" "#161618" "#f5f5f7"
sandbox_theme tokyo-night dark "#7aa2f7" "#414868" "#1a1b26" "#16161e" "#c0caf5"
python3 - "$H/.local/state/omarchy/current/theme/backgrounds" <<'PY'
import struct, sys, zlib
def png(path, top, bottom, w=320, h=200):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + bytes(int(a + (b - a) * ((y + x / 3) / (h + w / 3))) for a, b in zip(top, bottom) for x in [0]) * w for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
out = sys.argv[1]
png(out + "/1-circuit.png", (28, 28, 30), (120, 72, 16))
png(out + "/2-ember.png", (58, 22, 14), (214, 120, 30))
png(out + "/3-dusk.png", (20, 24, 54), (140, 60, 110))
PY
ln -sf "$H/.local/state/omarchy/current/theme/backgrounds/1-circuit.png" "$H/.local/state/omarchy/current/background"
printf 'balanced\n' >"$H/.local/state/omarchy/powerprofiles/ac"
printf 'power-saver\n' >"$H/.local/state/omarchy/powerprofiles/battery"
printf 'claude\n' >"$H/.local/state/omarchy/defaults/agent"
cat >"$H/.config/omarchy/shell.json" <<'T'
{
  "version": 1,
  "bar": {"position": "top", "background": "opaque", "layout": {"left": [{"id": "omarchy.menu"}, {"id": "omarchy.workspaces"}], "centerBegin": [], "center": [{"id": "omarchy.clock"}], "centerEnd": [{"id": "omarchy.weather"}], "right": [{"id": "omarchy.tray"}, {"id": "omarchy.audio"}, {"id": "omarchy.network"}, {"id": "omarchy.power"}], "tray": []}},
  "dock": {"pinned": ["firefox", "foot", "org.gnome.Nautilus"]},
  "idle": {"screensaver": 150, "lock": 300},
  "plugins": []
}
T
cp "$H/.config/omarchy/shell.json" "$ROOT/omarchy/config/omarchy/shell.json"
printf -- 'dofile("x")\nrequire("default.hypr.omarchy")\nrequire("hypr.monitors")\nrequire("hypr.autostart")\nrequire("default.hypr.toggles")\n' >"$H/.config/hypr/hyprland.lua"
printf '{"en":{"name":"English","englishName":"English","formatLocale":"en-US"},"da":{"name":"Dansk","englishName":"Danish","formatLocale":"da-DK"},"ar":{"name":"العربية","englishName":"Arabic","formatLocale":"ar-AE"},"de":{"name":"Deutsch","englishName":"German","formatLocale":"de-DE"}}\n' >"$ROOT/omarchy/default/i18n/locales.json"
printf '[Desktop Entry]\nType=Application\nName=Sync Client\nComment=Keeps a folder in sync\nExec=sync-client\n' >"$H/.config/autostart/sync-client.desktop"
mkdir -p "$ROOT"/sys-root/sys/class/power_supply/BAT0 "$ROOT"/sys-root/sys/class/net/wlan0/wireless "$ROOT"/sys-root/sys/class/bluetooth/hci0 "$ROOT"/sys-root/sys/class/dmi/id
printf 'Battery\n' >"$ROOT/sys-root/sys/class/power_supply/BAT0/type"
printf '86\n' >"$ROOT/sys-root/sys/class/power_supply/BAT0/capacity"
printf 'Discharging\n' >"$ROOT/sys-root/sys/class/power_supply/BAT0/status"
printf '10\n' >"$ROOT/sys-root/sys/class/dmi/id/chassis_type"
printf 'Example Laptop 14\n' >"$ROOT/sys-root/sys/class/dmi/id/product_name"
printf 'Example\n' >"$ROOT/sys-root/sys/class/dmi/id/sys_vendor"
cp "$HERE/fake-omarchy" "$ROOT/bin/omarchy"
cp "$HERE/fake-hyprctl" "$ROOT/bin/hyprctl"
for tool in pactl nmcli bluetoothctl systemctl timedatectl xdg-mime; do
  ln -sf "$ROOT/bin/omarchy" "$ROOT/bin/$tool"
done
printf '#!/bin/sh\nprintf "Filesystem Type Size Used Avail Use%% Mounted\n/dev/nvme0n1p2 btrfs 1000000000000 420000000000 580000000000 42%% /\n/dev/nvme0n1p1 vfat 2000000000 400000000 1600000000 20%% /boot\n"\n' >"$ROOT/bin/df"
chmod +x "$ROOT"/bin/*
for tool in powerprofilesctl; do printf '#!/bin/sh\nexit 0\n' >"$ROOT/bin/$tool"; chmod +x "$ROOT/bin/$tool"; done
