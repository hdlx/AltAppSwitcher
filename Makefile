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
IDIRS = -I $(ROOTDIR)/SDK/Headers -I $(ROOTDIR)/Sources -I $(ROOTDIR)/SDK/Sources
LDIRS = -L $(LIBDIR) -L $(LIBDIR)/curl
LFLAGS = -static -static-libgcc -Werror
CFLAGS = -Wall -D ARCH_$(ARCH)=1 -target $(ARCH)-mingw64 -Werror

ifeq ($(CONF), Debug)
CFLAGS += -g3
else
CFLAGS += -O3
LFLAGS += -mwindows
LFLAGS += -s
endif

# All .h, for dependency recompilation. No granularity.
ALLH = $(wildcard $(ROOTDIR)/*/*.h $(ROOTDIR)/*/*/*.h $(ROOTDIR)/*/*/*/*.h)

# Objects:
# All, for compilation.
ALLC = $(wildcard $(ROOTDIR)/*/*.c $(ROOTDIR)/*/*/*.c $(ROOTDIR)/*/*/*/*.c)
ALLOBJECTS = $(patsubst $(ROOTDIR)/%.c, $(OBJDIR)/%.o, $(ALLC))

# Subsets, for link.
AASOBJECTS = $(filter $(OBJDIR)/Sources/AltAppSwitcher/%, $(ALLOBJECTS))
CONFIGOBJECTS = $(filter $(OBJDIR)/Sources/Config/%, $(ALLOBJECTS))
SETTINGSOBJECTS = $(filter $(OBJDIR)/Sources/Settings/%, $(ALLOBJECTS))
UPDATEROBJECTS = $(filter $(OBJDIR)/Sources/Updater/%, $(ALLOBJECTS))
ERROROBJECTS = $(filter $(OBJDIR)/Sources/Utils/Error%, $(ALLOBJECTS))
FILEOBJECTS = $(filter $(OBJDIR)/Sources/Utils/File%, $(ALLOBJECTS))
MSGOBJECTS = $(filter $(OBJDIR)/Sources/Utils/Message%, $(ALLOBJECTS))
GUIOBJECTS = $(filter $(OBJDIR)/Sources/Utils/GUI%, $(ALLOBJECTS))
SDKOBJECTS = $(filter $(OBJDIR)/SDK%, $(ALLOBJECTS))
COMMONOBJECTS = $(ERROROBJECTS) $(FILEOBJECTS) $(MSGOBJECTS) $(SDKOBJECTS)

AASLIBS = -l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32
SETTINGSLIB = -l Comctl32 -l Gdi32
UPDATERLIBS = -l zip -l zlibstatic -l bcrypt -l curl -l curl.dll

AASASSETS = $(patsubst $(ROOTDIR)/Assets/AAS/%, $(AASBUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/AAS/*))
DLL = $(patsubst $(ROOTDIR)/SDK/Dll/$(ARCH)/%, $(AASBUILDDIR)/%, $(wildcard $(ROOTDIR)/SDK/Dll/$(ARCH)/*))

# Do not make a non phony target depend on phony one, otherwise
# the target will rebuild every time.
.PHONY: default clean directories deploy

ALLAAS = $(AASBUILDDIR)/AltAppSwitcher.exe
ALLAAS += $(AASBUILDDIR)/Settings.exe
ALLAAS += $(AASBUILDDIR)/Updater.exe
ALLAAS += $(AASASSETS)
ALLAAS += $(DLL)

AASARCHIVE = $(OUTPUTDIR)/Deploy/AltAppSwitcher_$(ARCH).zip

COMPILECOMMANDS = $(SOURCEDIR)/compile_commands.json

default: directories $(ALLAAS) $(COMPILECOMMANDS)

deploy: default $(AASARCHIVE)

# Directory targets:
directories:
	python ./AAS.py MakeDirs $(CONF) $(ARCH)

# Deploy targets:
$(AASARCHIVE): $(ALLAAS)
	python ./AAS.py MakeArchive $(BUILDDIR)/AAS $@

# Compile object targets:
# see 4.12.1 Syntax of Static Pattern Rules
$(ALLOBJECTS): $(OBJDIR)/%.o: $(ROOTDIR)/%.c $(ALLH)
	$(CC) $(CFLAGS) $(IDIRS) -MJ $@.json -c $< -o $@

# Build exe targets (link):
$(AASBUILDDIR)/AltAppSwitcher.exe: $(AASOBJECTS) $(CONFIGOBJECTS) $(COMMONOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(AASLIBS) $^ -o $@

$(AASBUILDDIR)/Settings.exe: $(SETTINGSOBJECTS) $(CONFIGOBJECTS) $(COMMONOBJECTS) $(GUIOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(SETTINGSLIB) $^ -o $@

$(AASBUILDDIR)/Updater.exe: $(UPDATEROBJECTS) $(COMMONOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(UPDATERLIBS) $^ -o $@

# Assets:
$(AASASSETS): $(AASBUILDDIR)/%: $(ROOTDIR)/Assets/AAS/%
	python ./AAS.py Copy "$<" "$@"

# Dll:
$(DLL): $(AASBUILDDIR)/%: $(ROOTDIR)/SDK/Dll/$(ARCH)/%
	python ./AAS.py Copy "$<" "$@"

# Make compile_command.json (clangd)
$(SOURCEDIR)/compile_commands.json: $(ALLOBJECTS)
	python ./AAS.py MakeCompileCommands $@ $(subst .o,.o.json, $^)

# Other targets:
clean:
	python ./AAS.py Clean
