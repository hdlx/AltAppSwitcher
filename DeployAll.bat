mingw32-make.exe clean
mingw32-make.exe ARCH=x86_64 CONF=Release
mingw32-make.exe ARCH=aarch64 CONF=Release
python AAS.py MakeArchive Output/Release_x86_64/AAS
python AAS.py MakeArchive Output/Release_aarch64/AAS