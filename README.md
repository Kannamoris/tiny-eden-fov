# Tiny Eden FOV

An adjustable field of view for [Tiny Eden](https://store.steampowered.com/app/3375110/), which ships without one. Change it while the game is running and the camera follows within a second.

Linux only, including Steam Deck. See [Windows](#windows) below.

## Install

```sh
git clone https://github.com/YOURNAME/tiny-eden-fov
cd tiny-eden-fov
./install.sh
```

`install.sh` builds the library and prints the line to paste into Steam: right click Tiny Eden, Properties, General, Launch Options.

```
LD_PRELOAD=/path/to/tiny-eden-fov/libtinyeden_fov.so %command%
```

On Steam Deck the OS is read only and has no compiler, so download `libtinyeden_fov.so` from the Releases page instead of building, put it anywhere in your home directory, and use the same launch option.

## Use

Set it once, in the launch options, by appending `-fov=` and the value you want:

```
LD_PRELOAD=/path/to/tiny-eden-fov/libtinyeden_fov.so %command% -fov=105
```

Or change it any time, including with the game running, and the camera follows within a second:

```sh
./fov 105     # set the field of view
./fov         # show the current value
```

The game's own value is 90. Useful range is roughly 90 to 115; much above that starts to look like a fisheye lens, especially on ultrawide.

Use `-fov=105`, not `-fov 105`. Unreal reads the first bare token on the command line as a map to open, so a detached number breaks the boot. The mod says so rather than letting you find out the hard way.

The value from `./fov` is stored in `~/.config/tiny-eden-fov`. A launch option wins at startup, and then the file takes over the moment you actually change it, so the two work together: put your normal value in the launch options and still tune live when you feel like it.

Environment variables, if you prefer them:

| Variable | Effect |
| --- | --- |
| `TINY_EDEN_FOV=105` | same as `-fov=105`, for launchers that pass environment rather than arguments |
| `TINY_EDEN_FOV_LIVE=0` | apply at startup only, no live updates |
| `TINY_EDEN_FOV_CAMERAACTORS=1` | also override camera actors, which includes cutscene cameras |
| `TINY_EDEN_FOV_DEBUG=1` | log the field of view of every camera it finds |

Precedence is `-fov=`, then `TINY_EDEN_FOV`, then the config file.

## Uninstall

Remove the launch option. Nothing else to undo: the mod never writes to the game's files, so there is nothing for Steam to repair and nothing left behind when it is not preloaded.

## How it works

Tiny Eden has no field of view setting because nothing in the game ever *sets* a field of view. Across all 11959 cooked packages in the shipped container, no asset references `FieldOfView` or `SetFieldOfView`, so every camera runs at the Unreal Engine default of 90 degrees that `UCameraComponent`'s constructor writes. That default is a single 16-byte constant in `.rodata`:

```asm
mov    dword [rbx+0x264], 1.777778     ; AspectRatio
movaps xmm0, [rip+disp32]              ; -> {90.0, 90.0, 1.0, 1536.0}
movaps [rbx+0x240], xmm0               ; FieldOfView is the first float
```

The library finds that instruction sequence in the game's own code at startup, follows the displacement to the constant, and rewrites the first float. Every camera built afterwards is born with the new value.

Cameras that already exist keep what they were constructed with, so a background thread watches the config file and, on a change, walks the process heap for live `UCameraComponent` objects (matched on their vtable pointer, recovered from the same constructor) and writes the new value into each. That is what makes it behave like a slider instead of a launch flag.

Addresses are found by pattern scan rather than hardcoded, so a game update will normally just keep working.

## Troubleshooting

The library logs to stderr, which Steam captures in its console log, or run the game from a terminal to see it directly.

`located FOV default at ...` means it found what it needed. `FOV 105.0 (N live cameras retuned)` is a value being applied.

```
could not locate the camera FOV default; game updated? mod disabled.
```

means the pattern no longer matches and the mod did nothing at all, which is the safe outcome. Open an issue with your game build number. The anchors to re-derive are the `AspectRatio = 1.777778f` store in the `UCameraComponent` constructor, `FieldOfView` at object offset `+0x240`, and the vtable immediate stored at the top of that same constructor.

## Compatibility

Built against the oldest glibc that matters (2.31, the Steam Linux Runtime) so one binary works on any current distribution. `make check` fails the build if a newer symbol sneaks in.

Verified on game build 25169107, Unreal Engine 5.8, native Linux binary. Single player game, no anti-cheat.

## Windows

Not supported. The technique ports directly, but the Windows build is compiled by a different compiler, so the byte pattern above does not match and the injection method is different: a proxy DLL or something like UE4SS rather than `LD_PRELOAD`. Contributions welcome.

## License

MIT, see [LICENSE](LICENSE).
