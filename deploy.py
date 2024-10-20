import os
import compileCommon
import shutil

def deploy(arch):
    srcDir = f"./Output/Release/{arch}"
    if os.path.exists(srcDir):
        shutil.rmtree(srcDir)
    dstDir = "./Output/Deploy"
    if not os.path.exists(dstDir):
        os.makedirs(dstDir)
    tempDir = "./Output/Deploy/Temp"
    if os.path.exists(tempDir):
        shutil.rmtree(tempDir)

    compileCommon.CompileRel("CheckUpdate", arch)
    compileCommon.CompileRel("AltAppSwitcher", arch)

    shutil.copytree(srcDir, tempDir)

    os.system(f"mt.exe -manifest \"{tempDir}/AltAppSwitcher.exe.manifest\" -outputresource:\"{tempDir}/AltAppSwitcher.exe\"")
    os.remove(f"{tempDir}/AltAppSwitcher.exe.manifest")

    zipFile = f"{dstDir}/AltAppSwitcher_{arch}"
    shutil.make_archive(zipFile, "zip", tempDir)

deploy("x86_64")
deploy("aarch64")