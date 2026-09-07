# kpmhook1

Hook library for *LovePlus MEDAL Happy Daily Life* (愛相随 MEDAL Happy Daily Life, KPM / KT_SKELETON_ST_DUAL.EXE).

## Features

- **Direct3D 9 Windowed Mode**:
  Patches the game's fullscreen state machine (`g_FullscreenAllowed`) to keep the engine in native windowed mode (1024x768). This bypasses the crash caused by `D3DCREATE_ADAPTERGROUP_DEVICE` and multi-head exclusive fullscreen initialization on Windows 11 WDDM drivers.
- **Path Redirection**:
  Hooks `KERNEL32.dll` IAT file operations (`CreateFileA/W`, `GetFileAttributesA/W`, `FindFirstFileA/W`, `CreateDirectoryA/W`, etc.) to redirect:
  - `d:/kpm/...` -> `<game_root>/...`
  - `e:/...` -> `<game_root>/e/...`
- **Automatic Configuration Bootstrapping**:
  Generates a working `game.conf` if not present, ensuring vital bypass flags are active:
  - `NO_SUBBOARD=1` (bypasses PLX9030 PCI subboard detection)
  - `NO_TOUCHPANEL=1` (enables mouse cursor, skips touch panel driver hook)
  - `IGNORE_IRCOM=1` (skips IrDA infrared link)
  - `NO_CARDREADER=1` (skips card reader)
  - `DISABLE_ALL_ERROR=1` (bypasses hardware error halts)
  - `IGNORE_FILE_CHECK=1` (skips file integrity check)
  - `rotate=1` (rotates subscreen 90 deg CCW)

## Usage

Place `kpmhook1.dll` and `inject.exe` in the game's `contents` directory, then launch:

```cmd
inject.exe kpmhook1.dll KT_SKELETON_ST_DUAL.EXE
```
