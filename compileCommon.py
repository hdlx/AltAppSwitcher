import os
import shutil

def CFiles():
    cFiles = ""
    for root, subdirs, files in os.walk("./Sources"):
        for file in files:
            if file.endswith(".c"):
                cFiles += (root + "/" + file + " ")

    if not os.path.exists("./Output"):
        os.makedirs("./Output")
    if not os.path.exists("./Output/Release"):
        os.makedirs("./Output/Release")
    if not os.path.exists("./Output/Debug"):
        os.makedirs("./Output/Debug")
    return cFiles

def LinkArgs():
    return "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi"

def CompileDbg():
    cmd = "clang {0}  -I ./Sources {1} -o Output/Debug/MacAppSwitcher.exe -Werror -g -glldb -target x86_64-mingw64".format(CFiles(), LinkArgs())
    os.system(cmd)

def CompileRel():
    cmd = "clang {0} -I ./Sources {1} -o Output/Release/MacAppSwitcher.exe -mwindows -s -Os -Oz -target x86_64-mingw64".format(CFiles(), LinkArgs())
    os.system(cmd)
