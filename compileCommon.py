import os
import shutil

def CFiles(dir):
    cFiles = ""
    for root, subdirs, files in os.walk(dir):
        for file in files:
            if file.endswith(".c"):
                cFiles += (root + "/" + file + " ")
    return cFiles

def LinkArgs():
    return "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32 -l ws2_32"

def Common():
    return "-static -static-libgcc"

def WarningOptions():
    return "-Werror -Wall -Wextra -Wno-unused-function -Wno-used-but-marked-unused -Wno-nonportable-include-path"

def Includes():
    return "-I ./Sources -I ./SDK"

def CopyAssets(dir):
    for file in os.listdir("./Assets/"):
        shutil.copyfile(f"./Assets/{file}", f"{dir}/{file}")

def MakeStaticStr():
    fsrc = open("./Assets/AltAppSwitcherConfig.txt", "r")
    if not os.path.exists("./Sources/AltAppSwitcher/_Generated"):
        os.makedirs("./Sources/AltAppSwitcher/_Generated")
    fdst = open("./Sources/AltAppSwitcher/_Generated/ConfigStr.h", "w")
    fdst.write("static const char ConfigStr[] =\n" )
    for line in fsrc:
        fdst.write("\"")
        fdst.write(line.strip("\n").replace('"', r'\"'))
        fdst.write(r"\n")
        fdst.write("\"")
        fdst.write("\n")
    fdst.write(";")

def CompileCommon(dir):
    MakeStaticStr()
    if not os.path.exists(dir):
        os.makedirs(dir)
    CopyAssets(dir)

def CompileDbg(prj, arch):
    outputDir = "./Output/Debug"
    CompileCommon(outputDir)
    file = f"{outputDir}/{prj}.exe"
    cFiles = CFiles(f"Sources/{prj}")
    cmd = f"clang {cFiles} {Includes()} {LinkArgs()} -o {file} {WarningOptions()} {Common()} -g -glldb -target {arch}-mingw64 -D DEBUG=1"
    os.system(cmd)

def CompileRel(prj, arch):
    outputDir = f"./Output/Release/{arch}"
    CompileCommon(outputDir)
    file = f"{outputDir}/{prj}.exe"
    cFiles = CFiles(f"Sources/{prj}")
    cmd = f"clang {cFiles} {Includes()} {LinkArgs()} -o {file} -mwindows {WarningOptions()} {Common()} -s -Os -Oz -target {arch}-mingw64"
    os.system(cmd)

