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
    MakeDirIfNeeded(f"./Output/{conf}_{arch}/Objects/Sources")
    MakeDirIfNeeded(f"./Output/{conf}_{arch}/Objects/SDK")
    MakeDirIfNeeded(f"./Output/{conf}_{arch}/AAS")
    MakeDirIfNeeded(f"./Output/Deploy")
    CopyDirStructure("./Sources", f"./Output/{conf}_{arch}/Objects/Sources")
    CopyDirStructure("./SDK", f"./Output/{conf}_{arch}/Objects/SDK")

def Copy(src, dst):
    if os.path.exists(dst):
        os.remove(dst)
    shutil.copy(src, dst)

def MakeCompileCommands(file, args):
    outf = open(file, 'w')
    outf.write("[\n")
    for fname in args:
        inf = open(fname, 'r')
        content = inf.read()
        # if last element removes ",\n"
        if fname == args[-1]:
            outf.write(content[0:-2])
        else:
            outf.write(content)
    outf.write("\n]")
    outf.close()

def EmbedAndDeleteManifest(exePath):
    if not os.path.exists(f"{exePath}.manifest"):
        return
    os.system(f"mt.exe -manifest \"{exePath}.manifest\" -outputresource:\"{exePath}\"")
    os.remove(f"{exePath}.manifest")

def MakeArchive(srcDir, dstZip):
    tempDir = f"{dstZip}".replace(".zip", "")
    if os.path.exists(tempDir):
        shutil.rmtree(tempDir)
    shutil.copytree(srcDir, tempDir)
    for x in os.listdir(tempDir):
        if x.endswith(".exe"):
            EmbedAndDeleteManifest(os.path.join(tempDir, x))
    shutil.make_archive(tempDir, "zip", tempDir)
    shutil.rmtree(tempDir)

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
    elif fn == "MakeCompileCommands":
        MakeCompileCommands(args[1], args[2:])
    elif fn == "MakeArchive":
        MakeArchive(args[1], args[2])

