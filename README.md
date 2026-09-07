# Tiny Eden FOV

An adjustable field of view for [Tiny Eden](https://store.steampowered.com/app/3375110/), which ships without one. Change it while the game is running and the camera follows within a second.

Pick your platform below and follow it top to bottom. Each one is complete on its own, so there is nothing else to read first.

- [Windows](#windows)
- [Steam Deck](#steam-deck)
- [Linux](#linux)

The game's own field of view is 90. Most people want 100 to 115. Above about 120 it starts to look like a fisheye lens, especially on an ultrawide monitor.

## Windows

### 1. Download the two files

From the [latest release](https://github.com/Kannamoris/tiny-eden-fov/releases/latest), download:

- `winmm.dll`
- `fov.bat`

Your browser may warn you about the `.dll`. It is a small unsigned file from a stranger on the internet, so that warning is doing its job; choose Keep if you want to continue.

### 2. Open the game's folder

In Steam, right click **Tiny Eden**, then **Manage**, then **Browse local files**. A folder window opens.

Inside it, open `CGH`, then `Binaries`, then `Win64`. You are in the right place when you can see a file called `CGH-Win64-Shipping.exe`.

### 3. Put both files in that folder

Drag `winmm.dll` and `fov.bat` into it, next to `CGH-Win64-Shipping.exe`.

That is the whole installation. Do not rename `winmm.dll`; the name is how the game finds it.

### 4. Choose your field of view

In Steam, right click **Tiny Eden**, then **Properties**. On the **General** page there is a box called **Launch Options**. Type exactly this into it, with no quotes and nothing else:

```
-fov=105
```

Change `105` to whatever you want. Close the window; Steam saves it on its own.

### 5. Start the game

That is it. If you want to change the number later, either edit the launch options again, or use the `fov.bat` from step 1, which also works while the game is running.

### Changing it while playing

Double click `fov.bat` in the game folder, type a number, press Enter. The camera changes within a second. There is no need to restart the game or alt-tab back and forth more than once.

### Did it work?

After the game has started once, a file called `tiny_eden_fov.log` appears in the same folder. If it is there, the mod is loaded and working. If it is not, `winmm.dll` is in the wrong folder; go back to step 2 and check you can see `CGH-Win64-Shipping.exe` next to it.

### Removing it

Delete `winmm.dll`, `fov.bat`, `tiny_eden_fov.txt` and `tiny_eden_fov.log` from that folder, and clear the launch options box. Nothing else to undo: the mod never changes the game's own files, so there is nothing for Steam to repair.

## Steam Deck

The Deck runs the native Linux version of the game, so it needs a different file from Windows. You need Desktop Mode for the first three steps, then Game Mode works normally.

### 1. Switch to Desktop Mode

Hold the **STEAM** button, choose **Power**, then **Switch to Desktop**.

### 2. Download the file

Open a browser, go to the [latest release](https://github.com/Kannamoris/tiny-eden-fov/releases/latest), and download `libtinyeden_fov.so`. It lands in your Downloads folder.

### 3. Move it to your home folder

Open the file manager (the blue folder in the taskbar). Click **Downloads** on the left, then drag `libtinyeden_fov.so` onto **Home**, also on the left.

The file is now at `/home/deck/libtinyeden_fov.so`, which is the path the next step needs.

### 4. Choose your field of view

Still in Desktop Mode, open Steam, right click **Tiny Eden**, then **Properties**. On the **General** page, type exactly this into **Launch Options**:

```
LD_PRELOAD=/home/deck/libtinyeden_fov.so %command% -fov=105
```

Change `105` to whatever you want. Everything else has to be copied exactly, including the `%command%` in the middle.

### 5. Go back to Game Mode and play

Double click **Return to Gaming Mode** on the desktop. Start the game as usual.

To change the number later, repeat step 4 with a different value. Changing it without restarting is possible but needs a terminal, so it is covered under [Linux](#linux) instead.

### Did it work?

After the game has started once, the mod leaves a note in your home folder at `.config/tiny-eden-fov.log`. To read it, open the file manager, press **Ctrl+H** to show hidden folders, open `.config`, and open `tiny-eden-fov.log`. A line reading `FOV 105.0` is the value being applied.

If the file is not there at all, the launch options are wrong: check the path in step 4 matches where the file actually is, and that `%command%` is still in the middle.

### Removing it

Clear the launch options box and delete the file from your home folder.

## Linux

### 1. Build and install

```sh
git clone https://github.com/Kannamoris/tiny-eden-fov
cd tiny-eden-fov
./install.sh
```

`install.sh` builds the library, checks it, and prints the exact line for the next step.

If you would rather not build it, download `libtinyeden_fov.so` from the [latest release](https://github.com/Kannamoris/tiny-eden-fov/releases/latest) and use its path in step 2.

### 2. Choose your field of view

In Steam, right click **Tiny Eden**, then **Properties**. On the **General** page, put this in **Launch Options**, using the path `install.sh` printed:

```
LD_PRELOAD=/home/you/tiny-eden-fov/libtinyeden_fov.so %command% -fov=105
```

Write `-fov=105`, not `-fov 105`. Unreal reads a bare number on the command line as the name of a map to load, so a detached one stops the game booting. The mod warns you rather than letting you find out the hard way.

### 3. Start the game

### Changing it while playing

From the folder you cloned:

```sh
./fov 105     # set the field of view
./fov         # show the current value
```

The camera follows within a second. The value is kept in `~/.config/tiny-eden-fov`, so it survives a restart.

The launch option wins at startup, and the file takes over the moment you actually change it. That means you can keep your normal value in the launch options and still tune live whenever you feel like it.

### Did it work?

The library writes to `~/.config/tiny-eden-fov.log`, and to standard error, so running the game from a terminal shows it live. `FOV 105.0 (N live cameras retuned)` is a value being applied. If the log does not exist at all, the library was never preloaded, so the path in the launch options is wrong.

### Removing it

Clear the launch options box. Nothing else to undo: the mod never changes the game's own files.

## Environment variables

For launchers that pass environment rather than arguments, and for a couple of things there is no other switch for. These work on all three platforms, though on Windows there is no convenient place to set one for a Steam launch, so `-fov=` and `fov.bat` are the practical routes there.

| Variable | Effect |
| --- | --- |
| `TINY_EDEN_FOV=105` | same as `-fov=105` |
| `TINY_EDEN_FOV_LIVE=0` | apply at startup only, no live updates |
| `TINY_EDEN_FOV_CAMERAACTORS=1` | also override camera actors, which includes cutscene cameras |
| `TINY_EDEN_FOV_DEBUG=1` | log what every camera it finds was set to |

Precedence is `-fov=`, then `TINY_EDEN_FOV`, then the saved value.

## Running the Windows version on Linux

If you have forced Proton on and are running the Windows build, use `winmm.dll` rather than the `.so`, and add the override that stops Wine preferring its own winmm:

```
WINEDLLOVERRIDES="winmm=n,b" %command% -fov=105
```

## How it works

Tiny Eden has no field of view setting because nothing in the game ever *sets* a field of view. Across all 11959 cooked packages in the shipped container, no asset references `FieldOfView` or `SetFieldOfView`, so every camera runs at the Unreal Engine default of 90 degrees that `UCameraComponent`'s constructor writes. On Linux that default is a single 16-byte constant in `.rodata`:

```asm
mov    dword [rbx+0x264], 1.777778     ; AspectRatio
movaps xmm0, [rip+disp32]              ; -> {90.0, 90.0, 1.0, 1536.0}
movaps [rbx+0x240], xmm0               ; FieldOfView is the first float
```

The library finds that instruction sequence in the game's own code at startup, follows the displacement to the constant, and rewrites the first float. Every camera built afterwards is born with the new value.

Cameras that already exist keep what they were constructed with, so a background thread watches the saved value and, on a change, walks the process heap for live `UCameraComponent` objects (matched on their vtable pointer, recovered from the same constructor) and writes the new value into each. That is what makes it behave like a slider instead of a launch flag.

Addresses are found by pattern scan rather than hardcoded, so a game update will normally just keep working.

The Windows build needs a different route to the same idea. MSVC does not pool the defaults into a constant, it writes each one as an immediate operand, and it lays the class out 16 bytes wider so `FieldOfView` sits at `+0x250`. There is no single constant to rewrite, so the mod patches the immediate in the places the engine bakes it in: both `UCameraComponent` constructors, and the two camera actors that write 90 back over a component they just built, which are left alone unless you ask for them. It also matches on shape rather than on bytes, which is sturdier than the Linux pattern: a store of `90.0f` to `+0x250` followed within 96 bytes by a store of `1.7777778f` to `+0x274`. Nothing else in 143 MB of code looks like that. Both constructors independently store the `UCameraComponent` vtable, so the mod requires the two to agree before it trusts the pointer for live retuning.

It ships as `winmm.dll` because the game statically imports winmm, so the proxy is loaded and the default is already patched before the engine builds its first camera. The four functions the game actually calls are forwarded to the real winmm in System32, resolved on first use rather than from `DllMain`, since calling the loader while it holds its own lock is how proxy DLLs deadlock.

## When it stops working

A message like this, in the log (`~/.config/tiny-eden-fov.log` on Linux, `tiny_eden_fov.log` next to the DLL on Windows):

```
could not locate the camera FOV default; game updated? mod disabled.
```

means the pattern no longer matches and the mod did nothing at all, which is the safe outcome. Open an issue with your game build number. The anchors to re-derive are the `AspectRatio = 1.777778f` store in the `UCameraComponent` constructor, `FieldOfView` at object offset `+0x240` on Linux or `+0x250` on Windows, and the vtable stored at the top of that same constructor.

## Compatibility

Verified on game build 25169107, Unreal Engine 5.8, on both the native Linux binary and the Windows binary under Proton. Single player game, no anti-cheat.

The Linux library is built against the oldest glibc that matters (2.31, the Steam Linux Runtime) so one binary works on any current distribution. `make check` fails the build if a newer symbol sneaks in. The Windows DLL is built with mingw-w64 (`make windows`, and `make wincheck` to verify the forwarded exports).

## License

MIT, see [LICENSE](LICENSE).
