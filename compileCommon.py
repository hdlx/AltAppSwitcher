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
    return "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi"

def CompileDbg():
    dir = "./Output/Debug"
    if not os.path.exists(dir):
        os.makedirs(dir)
    file = f"{dir}/MacAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} -Werror -g -glldb -target x86_64-mingw64"
    os.system(cmd)
    return file

def CompileRel(arch = "x86_64"):
    dir = f"./Output/Release/{arch}"
    if not os.path.exists(dir):
        os.makedirs(dir)
    file = f"{dir}/MacAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} -mwindows -s -Os -Oz -target {arch}-mingw64"
    os.system(cmd)
    return file
