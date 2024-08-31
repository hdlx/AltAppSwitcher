import os
import compileCommon
import shutil

def deploy(arch, writeManifest = False):
    srcDir = f"./Output/Release/{arch}"
    if os.path.exists(srcDir):
        shutil.rmtree(srcDir)
    file = compileCommon.CompileRel(arch)
    dir = "./Output/Deploy"
    if not os.path.exists(dir):
        os.makedirs(dir)
    zipFile = f"{dir}/AltAppSwitcher_{arch}"
    shutil.make_archive(zipFile, "zip", srcDir)

deploy("x86_64")
deploy("aarch64")