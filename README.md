# Tiny Eden FOV

An adjustable field of view for [Tiny Eden](https://store.steampowered.com/app/3375110/), which ships without one. Change it while the game is running and the camera follows within a second.

Linux, Steam Deck and Windows.

## Install

### Linux and Steam Deck

```sh
git clone https://github.com/Kannamoris/tiny-eden-fov
cd tiny-eden-fov
./install.sh
```

`install.sh` builds the library and prints the line to paste into Steam: right click Tiny Eden, Properties, General, Launch Options.

```
LD_PRELOAD=/path/to/tiny-eden-fov/libtinyeden_fov.so %command%
```

On Steam Deck the OS is read only and has no compiler, so download `libtinyeden_fov.so` from the Releases page instead of building, put it anywhere in your home directory, and use the same launch option.

### Windows

Download `winmm.dll` from the Releases page and drop it next to `CGH-Win64-Shipping.exe`, which lives in `Tiny Eden\CGH\Binaries\Win64\` inside your Steam library. Nothing else to set up.

To build it yourself you need mingw-w64 (`make windows`, or `wincheck` to also verify the exports).

Running the Windows build through Proton or Wine works too, but Wine prefers its own winmm, so the launch options need the override:

```
WINEDLLOVERRIDES="winmm=n,b" %command% -fov=105
```

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

Precedence is `-fov=`, then `TINY_EDEN_FOV`, then the config file. The variables work on Windows too, though on Windows there is no convenient place to set one for a Steam launch, so `-fov=` and the file are the practical routes there.

On Windows the same three inputs apply. `-fov=105` goes in the Steam launch options after `%command%` and is passed through the launcher to the game. The live value lives in `tiny_eden_fov.txt` next to the DLL rather than in `~/.config`, and there is no `fov` script: write the number into that file with any editor and the camera follows within a second. The log is `tiny_eden_fov.log` in the same folder.

## Uninstall

On Linux, remove the launch option. On Windows, delete `winmm.dll` from the game's `Binaries\Win64` folder. Nothing else to undo on either: the mod never modifies the game's own files, so there is nothing for Steam to repair.

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

The Windows build needs a different approach to the same idea. MSVC does not pool the defaults into a constant, it writes each one as an immediate operand, and it lays the class out 16 bytes wider so `FieldOfView` sits at `+0x250`. There is no single constant to rewrite, so the mod patches the immediate in all four places the engine bakes it in: both `UCameraComponent` constructors and the two camera actors that set it on a component they just made. It also matches on shape rather than on bytes, which is sturdier than the Linux pattern: a store of `90.0f` to `+0x250` followed within 96 bytes by a store of `1.7777778f` to `+0x274`. Nothing else in 143 MB of code looks like that. Both constructors independently store the `UCameraComponent` vtable, so the mod requires the two to agree before it trusts the pointer for live retuning.

It ships as `winmm.dll` because the game statically imports winmm, so the proxy is loaded and the default is already patched before the engine builds its first camera. The four functions the game actually calls are forwarded to the real winmm in System32, resolved on first use rather than from `DllMain`, since calling the loader while it holds its own lock is how proxy DLLs deadlock.

## Troubleshooting

The library logs to stderr, which Steam captures in its console log, or run the game from a terminal to see it directly. The Windows build also writes `tiny_eden_fov.log` next to the DLL, which is the easier place to look.

A line naming the sites it found means it got what it needed. `FOV 105.0 (N live cameras retuned)` is a value being applied. With `TINY_EDEN_FOV_DEBUG=1` it also lists what each camera was set to before, which is the quickest way to tell an inherited default apart from a value the game set deliberately.

```
could not locate the camera FOV default; game updated? mod disabled.
```

means the pattern no longer matches and the mod did nothing at all, which is the safe outcome. Open an issue with your game build number. The anchors to re-derive are the `AspectRatio = 1.777778f` store in the `UCameraComponent` constructor, `FieldOfView` at object offset `+0x240` on Linux or `+0x250` on Windows, and the vtable stored at the top of that same constructor.

## Compatibility

Built against the oldest glibc that matters (2.31, the Steam Linux Runtime) so one binary works on any current distribution. `make check` fails the build if a newer symbol sneaks in.

Verified on game build 25169107, Unreal Engine 5.8, on both the native Linux binary and the Windows binary under Proton. Single player game, no anti-cheat.

## License

MIT, see [LICENSE](LICENSE).
