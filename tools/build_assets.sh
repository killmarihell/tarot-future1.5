#!/usr/bin/env bash
# Gera romfs/gfx/{cards,ui}.t3x. Requer python3 + Pillow e tex3ds (devkitPro: dkp-pacman -S 3ds-dev).
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/make_assets.py --out build_assets --allow-missing
mkdir -p romfs/gfx
tex3ds -f rgba5551 -z auto -o romfs/gfx/cards.t3x build_assets/cards.png
tex3ds -f rgba8888 -z auto -o romfs/gfx/ui.t3x build_assets/ui.png
echo "romfs/gfx pronto."
