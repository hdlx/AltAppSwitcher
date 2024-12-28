ARCH = x86_64
CONF = Debug

# Argument check
ifeq ($(CONF), Debug)
else ifeq ($(CONF), Release)
else
$(error Bad CONF argument)
endif
ifeq ($(ARCH), x86_64)
else ifeq ($(ARCH), aarch64)
else
$(error Bad ARCH argument)
endif

# Directories
ROOTDIR = $(CURDIR)
OUTPUTDIR = $(ROOTDIR)/Output
BUILDDIR = $(OUTPUTDIR)/$(CONF)_$(ARCH)
SOURCEDIR = $(ROOTDIR)/Sources
OBJDIR = $(BUILDDIR)/Objects
AASBUILDDIR = $(BUILDDIR)/AAS
INSTALLERBUILDDIR = $(BUILDDIR)/Installer
SDKDIR = $(ROOTDIR)/SDK/Headers
LIBDIR = $(ROOTDIR)/SDK/Libs/$(ARCH)
INCLUDEDIR = $(ROOTDIR)/Sources

# Common var
CC = $(ARCH)-w64-mingw32-clang
IDIRS = -I $(ROOTDIR)/SDK/headers -I $(ROOTDIR)/Sources
LDIRS = -L $(LIBDIR)
LFLAGS = -static -static-libgcc -Werror
CFLAGS = -Wall -D ARCH_$(ARCH)=1 -target $(ARCH)-mingw64 -Werror

ifeq ($(CONF), Debug)
CFLAGS += -g3
else
CFLAGS += -O3
CFLAGS += -mwindows
LFLAGS += -s
endif

SDKHEADERS = $(wildcard $(SDKDIR)/**/*.h) $(wildcard $(SDKDIR)/*.h) 
SOURCEHEADERS = $(wildcard $(SOURCEDIR)/**/*.h) $(wildcard $(SOURCEDIR)/*.h) 

# Objects:
# All, for compilation.
ALLOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/**/*.c))
# Subsets, for link.
AASOBJECTS = $(filter $(OBJDIR)/AltAppSwitcher/%, $(ALLOBJECTS))
CONFIGOBJECTS = $(filter $(OBJDIR)/Config/%, $(ALLOBJECTS))
SETTINGSOBJECTS = $(filter $(OBJDIR)/Settings/%, $(ALLOBJECTS))
UPDATEROBJECTS = $(filter $(OBJDIR)/Updater/%, $(ALLOBJECTS))
INSTALLEROBJECTS = $(filter $(OBJDIR)/Installer/%, $(ALLOBJECTS))
ERROROBJECTS = $(filter $(OBJDIR)/Utils/Error%, $(ALLOBJECTS))
FILEOBJECTS = $(filter $(OBJDIR)/Utils/File%, $(ALLOBJECTS))
COMMONOBJECTS = $(ERROROBJECTS) $(FILEOBJECTS)
GUIOBJECTS = $(filter $(OBJDIR)/Utils/GUI%, $(ALLOBJECTS))

AASLIBS = -l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32
SETTINGSLIB = -l Comctl32 -l Gdi32
UPDATERLIBS = -l ws2_32 -l libzip -l zlib -l bcrypt
INSTALLERLIBS = -l Gdi32 -l Comctl32

AASASSETS = $(patsubst $(ROOTDIR)/Assets/AAS/%, $(AASBUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/AAS/*))
INSTALLERASSETS = $(patsubst $(ROOTDIR)/Assets/Installer/%, $(INSTALLERBUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/Installer/*))

# Do not make a non phony target depend on phony one, otherwise
# the target will rebuild every time.
.PHONY: default clean directories deploy

ALLAAS = $(AASBUILDDIR)/AltAppSwitcher.exe
ALLAAS += $(AASBUILDDIR)/Settings.exe
ALLAAS += $(AASBUILDDIR)/Updater.exe
ALLAAS += $(AASASSETS)

AASARCHIVE = $(BUILDDIR)/AltAppSwitcher_$(CONF)_$(ARCH).zip
AASARCHIVEOBJ = $(INSTALLERBUILDDIR)/AASZip.o

INSTALLER = $(INSTALLERBUILDDIR)/AltAppSwitcherInstaller.exe
INSTALLER += $(INSTALLERASSETS)

COMPILECOMMANDS = $(SOURCEDIR)/compile_commands.json

default: directories $(ALLAAS) $(INSTALLER) $(COMPILECOMMANDS)

deploy: default $(AASARCHIVE)

# Directory targets:
directories:
	python ./AAS.py MakeDirs $(CONF) $(ARCH)

# Archive targets:
$(AASARCHIVE): $(ALL)
	python ./AAS.py MakeArchive $(BUILDDIR)/AAS $@

# Compile object targets:
# see 4.12.1 Syntax of Static Pattern Rules
$(ALLOBJECTS): $(OBJDIR)/%.o: $(SOURCEDIR)/%.c $(SDKHEADERS) $(SOURCEHEADERS)
	$(CC) $(CFLAGS) $(IDIRS) -MJ $@.json -c $< -o $@

# Build exe targets (link):
$(AASBUILDDIR)/AltAppSwitcher.exe: $(AASOBJECTS) $(CONFIGOBJECTS) $(COMMONOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(AASLIBS) $^ -o $@

$(AASBUILDDIR)/Settings.exe: $(SETTINGSOBJECTS) $(CONFIGOBJECTS) $(COMMONOBJECTS) $(GUIOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(SETTINGSLIB) $^ -o $@

$(AASBUILDDIR)/Updater.exe: $(UPDATEROBJECTS) $(COMMONOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(UPDATERLIBS) $^ -o $@

$(INSTALLERBUILDDIR)/AltAppSwitcherInstaller.exe: $(INSTALLEROBJECTS) $(AASARCHIVEOBJ) $(COMMONOBJECTS) $(GUIOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(INSTALLERLIBS) $^ -o $@

# Assets:
$(AASASSETS): $(AASBUILDDIR)/%: $(ROOTDIR)/Assets/AAS/%
	python ./AAS.py Copy "$<" "$@"

# Assets:
$(INSTALLERASSETS): $(INSTALLERBUILDDIR)/%: $(ROOTDIR)/Assets/Installer/%
	python ./AAS.py Copy "$<" "$@"

# Make compile_command.json (clangd)
$(SOURCEDIR)/compile_commands.json: $(ALLOBJECTS)
	python ./AAS.py MakeCompileCommands $@ $(subst .o,.o.json, $^)

# Make archive obj.
$(AASARCHIVEOBJ): $(AASARCHIVE)
	python ./AAS.py BinToC $^ $(INSTALLERBUILDDIR)/AASZip.c
	$(CC) $(CFLAGS) -c $(INSTALLERBUILDDIR)/AASZip.c -o $@

# Other targets:
clean:
	python ./AAS.py Clean
