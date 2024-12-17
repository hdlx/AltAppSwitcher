import os
import shutil

def MakeStaticStr():
    fsrc = open("./Assets/AltAppSwitcherConfig.txt", "r")
    if not os.path.exists("./Sources/Config/_Generated"):
        os.makedirs("./Sources/Config/_Generated")
    fdst = open("./Sources/Config/_Generated/ConfigStr.h", "w")
    fdst.write("static const char ConfigStr[] =\n" )
    for line in fsrc:
        fdst.write("\"")
        fdst.write(line.strip("\n").replace('"', r'\"'))
        fdst.write(r"\n")
        fdst.write("\"")
        fdst.write("\n")
    fdst.write(";")

def MakeDirIfNeeded(dir):
    if not os.path.exists(dir):
        os.makedirs(dir)

def Clean():
    if os.path.exists("./Output"):
        shutil.rmtree("./Output");

def CopyDirStructure(src, dst):
    for x in os.listdir(src):
        px = os.path.join(src, x)
        if not os.path.isdir(px):
            continue
        dstpx = os.path.join(dst, x)
        MakeDirIfNeeded(dstpx)
        CopyDirStructure(px, dstpx)

def MakeDirs(conf, arch):
    MakeDirIfNeeded("./Output")
    MakeDirIfNeeded(f"./Output/{conf}_{arch}")
    MakeDirIfNeeded(f"./Output/{conf}_{arch}/Objects")
    CopyDirStructure("./Sources", f"./Output/{conf}_{arch}/Objects")

def Copy(src, dst):
    if os.path.exists(dst):
        os.remove(dst)
    shutil.copy(src, dst)

import sys
if __name__ == "__main__": 
    args = sys.argv[1:]
    fn = args[0]
    if fn == "Copy":
        Copy(args[1], args[2])
    elif fn == "MakeDirs":
        MakeDirs(args[1], args[2])
    elif fn == "Clean":
        Clean()
