## AltAppSwitcher: Alternative app switcher for Windows

This program brings MacOS-like or Gnome-like application switching to Windows. Use `alt + tab` to select the active app (not window) and `alt + ~` to switch between windows of the active app. Key bindings are customizable.

**Tested on Windows 10 and Windows 11.**

![](./Assets/ScreenshotWin10.png)

## Using AltAppSwitcher:
Get the executable from the release page https://github.com/hdlx/AltAppSwitcher/releases/. **x86_64** and **ARM64** (AArch64) architectures are available. Please note that I was not able to test the ARM64 one myself.

**Run it as an administator.**

You can kill it from the task manager (look for "AltAppSwitcher") or using CloseAltAppSwitcher.bat.

A config file lets you change key bindings, theme, and disable mouse support. App restart is required for the changes to take effect.

## Known issues
- Missing installer or instructions to have AltAppSwitcher launching on system startup.
- Alt tab popup is behind start menu.

## Technology
This is a C project relying on C standard library and Windows API. I'm using [Clang (mingw)](https://github.com/mstorsjo/llvm-mingw) and VS Code / VS Codium.
