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
LFLAGS = -mwindows -static -static-libgcc -Werror
CFLAGS = -Wall -D ARCH_$(ARCH)=1 -target $(ARCH)-mingw64 -Werror

ifeq ($(CONF), Debug)
    CFLAGS += -g3
else
    CFLAGS += -O3
    LFLAGS += -s
endif

SDKHEADERS = $(wildcard $(SDKDIR)/**/*.h) $(wildcard $(SDKDIR)/*.h) 
SOURCEHEADERS = $(wildcard $(SOURCEDIR)/**/*.h) $(wildcard $(SOURCEDIR)/*.h) 

ALLOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/**/*.c))

AASOBJECTS = $(filter $(OBJDIR)/AltAppSwitcher/%, $(ALLOBJECTS))
CONFIGOBJECTS = $(filter $(OBJDIR)/Config/%, $(ALLOBJECTS))
SETTINGSOBJECTS = $(filter $(OBJDIR)/Settings/%, $(ALLOBJECTS))
UPDATEROBJECTS = $(filter $(OBJDIR)/Updater/%, $(ALLOBJECTS))
INSTALLEROBJECTS = $(filter $(OBJDIR)/Installer/%, $(ALLOBJECTS))

AASLIBS = -l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32
SETTINGSLIB = -l Comctl32
UPDATERLIBS = -l ws2_32 -l libzip -l zlib -l bcrypt

AASASSETS = $(patsubst $(ROOTDIR)/Assets/AAS/%, $(AASBUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/AAS/*))

# Do not make a non phony target depend on phony one, otherwise
# it will rebuild every time.
.PHONY: default clean directories deploy

ALL := $(AASBUILDDIR)/AltAppSwitcher.exe
ALL += $(AASBUILDDIR)/Settings.exe
ALL += $(AASBUILDDIR)/Updater.exe
ALL += $(INSTALLERBUILDDIR)/AltAppSwitcherInstaller.exe
ALL += $(AASASSETS)
ALL += $(SOURCEDIR)/compile_commands.json

AASARCHIVE = $(BUILDDIR)/AltAppSwitcher_$(CONF)_$(ARCH).zip

default: directories $(ALL)

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
$(AASBUILDDIR)/AltAppSwitcher.exe: $(AASOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(AASLIBS) $^ -o $@

$(AASBUILDDIR)/Settings.exe: $(SETTINGSOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(SETTINGSLIB) $^ -o $@

$(AASBUILDDIR)/Updater.exe: $(UPDATEROBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(UPDATERLIBS) $^ -o $@

$(INSTALLERBUILDDIR)/AltAppSwitcherInstaller.exe: $(INSTALLEROBJECTS)
	$(CC) $(LFLAGS) $(LDIRS) $(UPDATERLIBS) $^ -o $@

# Assets:
$(AASASSETS): $(AASBUILDDIR)/%: $(ROOTDIR)/Assets/AAS/%
	python ./AAS.py Copy "$<" "$@"

# Make compile_command.json (clangd)
$(SOURCEDIR)/compile_commands.json: $(ALLOBJECTS)
	python ./AAS.py MakeCompileCommands $@ $(subst .o,.o.json, $^)

# Other targets:
clean:
	python ./AAS.py Clean
