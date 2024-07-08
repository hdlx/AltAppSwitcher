This program brings MacOS-like application switching to Windows. Use `alt + tab` to select the active app (not window) and `alt + ~` to switch between windows of the active app.

**Tested on Windows 10 and Windows 11.**

![](./Assets/ScreenshotWin10.png)

## Using MacAppSwitcher:
Get the executable from the release page https://github.com/hdlx/MacAppSwitcher/releases/. x86_64 and ARM64 (AArch64) architectures are available. Please note that I was not able to test the ARM64 one myself.

**Run it as an administator.**

You can kill it from the task manager. Search for "MacAppSwitcher".

## Known issues
- Missing installer or instructions to have MacAppSwitcher launching on system startup.
- Alt tab popup is behind start menu.

## Technology
This is a C project relying on C standard library and Windows API. I'm using [Clang (mingw)](https://github.com/mstorsjo/llvm-mingw) and VS Code / VS Codium.
