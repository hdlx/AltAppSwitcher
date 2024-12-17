import os
import shutil

def CopyAssets(dir):
    for file in os.listdir("./Assets/"):
        shutil.copyfile(f"./Assets/{file}", f"{dir}/{file}")

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

def CompileCommon(dir):
    MakeStaticStr()
    if not os.path.exists(dir):
        os.makedirs(dir)
    CopyAssets(dir)
