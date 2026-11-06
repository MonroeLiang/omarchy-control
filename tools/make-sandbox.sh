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
touch "$H/.local/state/omarchy/current/theme/backgrounds/1-circuit.png" "$H/.local/state/omarchy/current/theme/backgrounds/2-dusk.png"
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
