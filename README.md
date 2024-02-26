This program brings MacOS-like app switching to Windows. Use alt + tab to switch active app and alt + ~ to switch between windows of the active app.

## Using MacAppSwitcher:
Get the executable (Win64 only) from the repo release page https://github.com/hdlx/MacAppSwitcher/releases/.

**Run the executable with admin privilege.**

You can kill it from the task manager. Search for "MacAppSwitcher".

## Known issues
- Missing installer or instructions to have MacAppSwitcher launching on system startup. We can't trivially add a shortcut to Windows startup directory because of admin privilege requirement.
- Some window seems to interfere with keyboard inputs messages, thus disabling MacAppSwitcher when in focus.
- UI is behind start menu.

## Technology
This is a C project relying only on Windows C API. I'm using GCC (mingw) and vscode.
