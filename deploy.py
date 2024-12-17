import os
import shutil

def EmbedAndDeleteManifest(exePath):
    if not os.path.exists(f"{exePath}.manifest"):
        return
    os.system(f"mt.exe -manifest \"{exePath}.manifest\" -outputresource:\"{exePath}\"")
    os.remove(f"{exePath}.manifest")

def deploy(arch):
    srcDir = f"./Output/Release_{arch}"
    if os.path.exists(srcDir):
        shutil.rmtree(srcDir)
    dstDir = "./Output/Deploy"
    if not os.path.exists(dstDir):
        os.makedirs(dstDir)
    tempDir = f"./Output/Deploy/Temp_{arch}"
    if os.path.exists(tempDir):
        shutil.rmtree(tempDir)

    os.system(f"mingw32-make ARCH={arch} CONF=Release")

    shutil.copytree(srcDir, tempDir)
    shutil.rmtree(f"{tempDir}/Objects");
    EmbedAndDeleteManifest(f"{tempDir}/AltAppSwitcher.exe")
    EmbedAndDeleteManifest(f"{tempDir}/Updater.exe")
    EmbedAndDeleteManifest(f"{tempDir}/Settings.exe")

    zipFile = f"{dstDir}/AltAppSwitcher_{arch}"
    shutil.make_archive(zipFile, "zip", tempDir)

deploy("x86_64")
deploy("aarch64")