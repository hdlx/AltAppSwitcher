## AltAppSwitcher: alternative application switcher for Windows

Main features:
- Minimalist icon based interface
- MacOS-like or Gnome-like application switching. Use `alt + tab` to select the active app (not window) and `alt + ~` to switch between windows of the active app.
- MacOS-like application switching can be disabled if you prefer Windows style.
- Customizable keybindings and display options

**Tested on Windows 10 and Windows 11.**

![](./Assets/ScreenshotWin11.png)

## Launching AltAppSwitcher:
Get the archive from the [release page](https://github.com/hdlx/AltAppSwitcher/releases/).

**x86_64** and **ARM64** (AArch64) architectures are available. Please note that I was not able to test the ARM64 one myself.

**Run `AltAppSwitcher.exe` (preferably as an administator).**

A can be closed from its tray icon.

## Run at startup:
From the tray icon:

Click "Add to startup" to add to Windows startup apps.
Click "Remove from startup" to add to revert this.

**Startup task will be added with the current elevation settings.**

## Customization
**"Settings"** (executable or from tray icon) lets you change settings such as **key bindings**, **theme**, **scale** and **mouse support**.
Restore default settings by removing `AltAppSwitcherConfig.txt`.

## Known issues
- Alt tab popup is behind start menu.
- Releases might be flagged as malicious by antiviruses. This is a false positive (detection relies on machine learning).

## Building from source
> [!WARNING]
The C runtime used by this project is not **MSVCRT** anymore. Please use **UCRT** version of llvm-mingw.

This is a C project relying on C standard library and Windows API. I'm using [Clang (mingw ucrt)](https://github.com/mstorsjo/llvm-mingw) and VS Code / VS Codium.
Here is my setup:
### Dependencies:
- **Clang and Windows libraries:**\
Download **UCRT Clang MinGW**, unzip and add `Bin` subfolder to "Path" so `clang` (and other tools from MinGW) can be called from a terminal.\
I'm using `llvm-mingw-20260224-ucrt-x86_64` from [Clang (mingw)](https://github.com/mstorsjo/llvm-mingw) at the time of writing.
- **Python:**\
Download **Python** and install or add to "Path" so `python` can be called from a terminal.\
Alternatively, you can use MinGW-provided Python under `Python/bin`.
- **Make**\
Make is part of MinGW.
### Building:
- Clone the repository.
- Open a terminal at the root of the repository.
- Run `mingw32-make.exe` (MinGW make must be used)
- Binaries are found in `/Output/Debug_x86_64`
### Debugging:
I'm using VSCodium/VSCode to write and debug code, with 2 extensions:
- https://open-vsx.org/extension/vadimcn/vscode-lldb for debugging
- https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd for language server integration (code completion, errors...)
  
With those, you should be able to build and run inside VSCode using the configurations defined in `.vscode\launch.json` (at the moment, configurations are written for x86_64 but you can easily create arm64 equivalent)

Configurations refers to tasks defined in `/.vscode/tasks.json`
