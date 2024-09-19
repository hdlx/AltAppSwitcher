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
    return "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32"

def Common():
    return "-static -static-libgcc"

def WarningOptions():
    return "-Werror -Wall -Wextra -Wno-unused-function -Wno-used-but-marked-unused"

def CopyAssets(dir):
    for file in os.listdir("./Assets/"):
        shutil.copyfile(f"./Assets/{file}", f"{dir}/{file}")

def MakeStaticStr():
    fsrc = open("./Assets/AltAppSwitcherConfig.txt", "r")
    if not os.path.exists("./Sources/_Generated"):
        os.makedirs("./Sources/_Generated")
    fdst = open("./Sources/_Generated/ConfigStr.h", "w")
    fdst.write("static const char ConfigStr[] =\n" )
    for line in fsrc:
        fdst.write("\"")
        fdst.write(line.strip("\n").replace('"', r'\"'))
        fdst.write(r"\n")
        fdst.write("\"")
        fdst.write("\n")
    fdst.write(";")


def CompileDbg():
    MakeStaticStr()
    dir = "./Output/Debug"
    if not os.path.exists(dir):
        os.makedirs(dir)
    CopyAssets(dir)
    file = f"{dir}/AltAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} {WarningOptions()} {Common()} -g -glldb -target x86_64-mingw64 -D DEBUG=1"
    os.system(cmd)
    #os.system(f"mt.exe -manifest \"./Manifest.xml\" -outputresource:\"{file}\"")
    return file

def CompileRel(arch = "x86_64"):
    MakeStaticStr()
    dir = f"./Output/Release/{arch}"
    if not os.path.exists(dir):
        os.makedirs(dir)
    CopyAssets(dir)
    file = f"{dir}/AltAppSwitcher.exe"
    cmd = f"clang {CFiles()} -I ./Sources {LinkArgs()} -o {file} -mwindows {WarningOptions()} {Common()} -s -Os -Oz -target {arch}-mingw64"
    os.system(cmd)
    return file
