import os
import shutil

def CFiles():
    cFiles = ""
    for root, subdirs, files in os.walk("./Sources"):
        for file in files:
            if file.endswith(".c"):
                cFiles += (root + "/" + file + " ")
    return cFiles

def LinkArgs():
    return "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l uxtheme"

def Common():
    return "-static -static-libgcc"

def WarningOptions():
    return "-Werror -Wall -Wextra -Wno-unused-function -Wno-used-but-marked-unused"

def CompileDbg():
    dir = "./Output/Debug"
    if not os.path.exists(dir):
        os.makedirs(dir)
    file = f"{dir}/MacAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} {WarningOptions()} {Common()} -g -glldb -target x86_64-mingw64"
    os.system(cmd)
    #os.system(f"mt.exe -manifest \"./Manifest.xml\" -outputresource:\"{file}\"")
    return file

def CompileRel(arch = "x86_64"):
    dir = f"./Output/Release/{arch}"
    if not os.path.exists(dir):
        os.makedirs(dir)
    file = f"{dir}/MacAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} -mwindows {WarningOptions()} {Common()} -s -Os -Oz -target {arch}-mingw64"
    os.system(cmd)
    return file
