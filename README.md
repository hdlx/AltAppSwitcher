This program brings MacOS-like app switching on Windows. Use alt + tab to select active app, and use alt + ~ to switch between windows of the active app.

## Using MacAppSwitcher:
Get the executable (Win64 only) from the repo release page https://github.com/hdlx/MacAppSwitcher/releases/.

*Run the executable with admin privilege.*

You can kill it from the task manager by searching for "MacAppSwitcher".

## Known issues
- Missing process to launch the MacAppSwitcher on startup. We can't be trivial add a shortcut the startup folder because of admin privilege requirement.
- Some window seems to interfere with keyboard inputs messages, thus disabling MacAppSwitcher when in focus.
