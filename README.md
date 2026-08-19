# UwUClient

External overlay utility for a Windows target process. Renders an ImGui HUD over the target window using a DX11 swapchain, reads / writes target memory over standard usermode APIs, exposes a scriptable Lua executor and a set of per-game presets.

Written in C++20. Single-file exe, static CRT, no runtime dependencies to install.

---

## Requirements

- **Windows 10 / 11**, x64.
- **Visual Studio 2022** with the `Desktop development with C++` workload (or standalone MSVC v143 Build Tools + Windows 10/11 SDK).
- **CMake 3.20+** either the CMake bundled with VS 2022 or `winget install Kitware.CMake`.
- **Git** for cloning.

No external libraries need to be installed. `vendor/` already ships:
- imgui (Docking branch)
- MinHook
- Ultralight (used by the executor)

---

## Build

```powershell
git clone https://github.com/kapaisu/UwUClient.git
cd UwUClient
cmake -S . -B build -A x64
cmake --build build --config Release --target UwUClient
```

Output: `build/Release/UwUClient.exe`.

The build target is `UwUClient`. `Debug` builds work too but the static-CRT link cost balloons use `Release` unless you're debugging.

---

## Run

Copy the exe out of `build/Release/` and place these files next to it (all optional):

| File | Purpose |
|---|---|
| `bg.jpg` | background image used on the mode picker / launcher |
| `poppop.ai - uwu meme sound.mp3` | boot sound played on entering the terminal (also accepted as `poppop.mp3`) |

The exe also searches `%APPDATA%\UwUClient\` and `%TEMP%\uwuclient\` for the MP3, so you can drop it there instead.

**Startup flow:**
1. Mode picker  `Usermode` (works) or `Kernel Mode` (falls back to usermode with a warning).
2. Boot terminal CRT-style loading screen while the offset scanner runs.
3. Game selector pick a game preset (Universal / Arsenal / Rivals / Da Hood / …).
4. Main menu  press `RShift` in-game to toggle. `END` to save + quit.

---

## Repo layout

```
src/            main.cpp, menu.cpp, aimbot.cpp, esp.cpp, ...
include/        headers (memory, offsets, roblox, scanner, ...)
offsets/        cached target offsets + tools
Framwork/       fonts + secondary imgui copy
vendor/         imgui, minhook, ultralight
resources.rc    PE metadata + manifest
CMakeLists.txt  build config
```

---

## Notes

- `PROCESS_ALL_ACCESS` is not used the exe opens the target with
  `PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION`.
- Memory reads / writes go through `NtReadVirtualMemory` / `NtWriteVirtualMemory` resolved from ntdll at first call; there are no static `ReadProcessMemory` / `WriteProcessMemory` imports in the IAT.
- Configs auto-save to `config.bin` next to the exe. With `per_game_profiles` enabled, per-game presets save to `preset_<GameName>.bin`.

---

## Credits

Built by **q3c** (discord).
