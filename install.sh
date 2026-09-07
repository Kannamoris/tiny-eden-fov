#!/bin/sh
# Build the Tiny Eden FOV mod and print the Steam launch options to paste.
set -e
cd "$(dirname "$0")"
DIR=$(pwd -P)

command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || {
    echo "No C compiler found. Install gcc or clang and run this again." >&2
    echo "Steam Deck: the read-only OS has no compiler, download the prebuilt" >&2
    echo ".so from the Releases page instead of building." >&2
    exit 1
}

make --no-print-directory
make --no-print-directory check

if [ -d "$HOME/.local/bin" ] && [ ! -e "$HOME/.local/bin/tiny-eden-fov" ]; then
    ln -s "$DIR/fov" "$HOME/.local/bin/tiny-eden-fov"
    echo "Linked: ~/.local/bin/tiny-eden-fov"
fi

cat <<TXT

Built $DIR/libtinyeden_fov.so

1. In Steam, right click Tiny Eden, Properties, General, Launch Options, paste
   this, with the field of view you want on the end:

   LD_PRELOAD=$DIR/libtinyeden_fov.so %command% -fov=105

2. Or change it any time, including while the game is running:

   $DIR/fov 105

The game's own value is 90. Delete the launch option to turn the mod off.
TXT
