import os
import compileCommon
import shutil

def deploy(arch):
    file = compileCommon.CompileRel(arch)
    os.system(f"mt.exe -manifest \"./Manifest.txt\" -outputresource:\"{file}\"")
    dir = "./Output/Deploy"
    if not os.path.exists(dir):
        os.makedirs(dir)
    zipFile = f"{dir}/MacAppSwitcher_{arch}"
    srcDir = f"./Output/Release/{arch}"
    shutil.make_archive(zipFile, "zip", srcDir)

deploy("x86_64")
deploy("aarch64")
