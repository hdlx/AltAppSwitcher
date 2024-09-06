## AltAppSwitcher: alternative application switcher for Windows

This program brings MacOS-like or Gnome-like application switching to Windows. Use `alt + tab` to select the active app (not window) and `alt + ~` to switch between windows of the active app. Key bindings are customizable.

**Tested on Windows 10 and Windows 11.**

![](./Assets/ScreenshotWin11.png)

## Launching AltAppSwitcher:
Get the archive from the [release page](https://github.com/hdlx/AltAppSwitcher/releases/).

**x86_64** and **ARM64** (AArch64) architectures are available. Please note that I was not able to test the ARM64 one myself.

**Run the `AltAppSwitcher.exe` (preferably as an administator).**

Close the application using `CloseAltAppSwitcher.bat` file.

## Run at startup:
Run `AddToStartup.bat` to add AltAppSwitcher to Windows startup apps.

Run `RemoveFromStartup.bat` to revert this.

## Configuration
The configuration file, `AltAppSwitcherConfig.txt` lets you change **key bindings**, **theme**, and **mouse support**. App restart is required for the changes to take effect.

## Known issues
- Alt tab popup is behind start menu.

## Technology
This is a C project relying on C standard library and Windows API. I'm using [Clang (mingw)](https://github.com/mstorsjo/llvm-mingw) and VS Code / VS Codium.
