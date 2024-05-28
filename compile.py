import os
import shutil

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

link = "-l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi"
gccCmdDbg = "gcc {0} -o Output/Debug/MacAppSwitcher.exe -Werror -ggdb -I ./Sources {1} -march=x86-64".format(cFiles, link)
gccCmdRel = "gcc {0} -o Output/Release/MacAppSwitcher.exe -mwindows -s -I ./Sources {1} -march=x86-64".format(cFiles, link)

os.system(gccCmdDbg)
os.system(gccCmdRel)
