import os
import build
import shutil

def EmbedAndDeleteManifest(exePath):
    if not os.path.exists(f"{exePath}.manifest"):
        return
    os.system(f"mt.exe -manifest \"{exePath}.manifest\" -outputresource:\"{exePath}\"")
    os.remove(f"{exePath}.manifest")

def deploy(arch):
    srcDir = f"./Output/Release/{arch}"
    if os.path.exists(srcDir):
        shutil.rmtree(srcDir)
    dstDir = "./Output/Deploy"
    if not os.path.exists(dstDir):
        os.makedirs(dstDir)
    tempDir = f"./Output/Deploy/Temp_{arch}"
    if os.path.exists(tempDir):
        shutil.rmtree(tempDir)

    build.BuildRel("CheckForUpdates", arch)
    build.BuildRel("AltAppSwitcher", arch)
    build.BuildRel("Settings", arch)

    shutil.copytree(srcDir, tempDir)

    EmbedAndDeleteManifest(f"{tempDir}/AltAppSwitcher.exe")
    EmbedAndDeleteManifest(f"{tempDir}/CheckForUpdates.exe")
    EmbedAndDeleteManifest(f"{tempDir}/Settings.exe")

    zipFile = f"{dstDir}/AltAppSwitcher_{arch}"
    shutil.make_archive(zipFile, "zip", tempDir)

deploy("x86_64")
deploy("aarch64")