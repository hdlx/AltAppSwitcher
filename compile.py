import os
import shutil

cFiles = ""
for root, subdirs, files in os.walk("./src/MainApp"):
    for file in files:
        if file.endswith(".c"):
            cFiles += (root + "/" + file + " ")

arg = "-march=x86-64"
gccCmd = "gcc {0} -o output/Mac StyleSwitch.exe -Werror -ggdb -mwindows -I ./src -l dwmapi -l User32 -l Gdi32 -l Gdiplus {1}".format(cFiles, arg)
#gccCmd = "gcc -Os -s -I ./src -I {0} -L {1} {2} -l libSDL2 -o output/main.exe".format(sdl2Include, sdl2Lib, cFiles)
os.system(gccCmd)
