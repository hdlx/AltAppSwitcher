ARCH = x86_64
CONF = Debug

# Directories
ROOTDIR = $(CURDIR)
OUTPUTDIR = $(ROOTDIR)/Output
BUILDDIR = $(OUTPUTDIR)/$(CONF)_$(ARCH)
SOURCEDIR = $(ROOTDIR)/Sources
OBJDIR = $(BUILDDIR)/Objects
SDKDIR = $(ROOTDIR)/SDK/Headers
LIBDIR = $(ROOTDIR)/SDK/Libs/$(ARCH)
INCLUDEDIR = $(ROOTDIR)/Sources

# Common var
CC = $(ARCH)-w64-mingw32-clang
CINCLUDES = -I $(ROOTDIR)/SDK/headers -I $(ROOTDIR)/Sources
CLINK =  -mwindows -static-libgcc -L $(LIBDIR) -l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32 -l ws2_32 -l libzip -l zlib -l bcrypt
CFLAGS = -g -Wall -static -D ARCH_$(ARCH)=1 -target $(ARCH)-mingw64

SDKHEADERS = $(wildcard $(SDKDIR)/**/*.h) $(wildcard $(SDKDIR)/*.h) 
SOURCEHEADERS = $(wildcard $(SOURCEDIR)/**/*.h) $(wildcard $(SOURCEDIR)/*.h) 

ALLOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/**/*.c))

AASOBJECTS = $(filter $(OBJDIR)/AltAppSwitcher/%, $(ALLOBJECTS))
CONFIGOBJECTS = $(filter $(OBJDIR)/Config/%, $(ALLOBJECTS))
SETTINGSOBJECTS = $(filter $(OBJDIR)/Settings/%, $(ALLOBJECTS))
UPDATEROBJECTS = $(filter $(OBJDIR)/Updater/%, $(ALLOBJECTS))

ASSETS = $(patsubst $(ROOTDIR)/Assets/%, $(BUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/*))

.PHONY: all clean directories

ALL := directories
ALL += $(BUILDDIR)/AltAppSwitcher.exe
ALL += $(BUILDDIR)/Settings.exe
ALL += $(BUILDDIR)/Updater.exe
ALL += $(ASSETS)
ALL += $(SOURCEDIR)/compile_commands.json

#ALL := $(ALLOBJECTS)

All: $(ALL)

# Compile object targets:
# see 4.12.1 Syntax of Static Pattern Rules
$(ALLOBJECTS): $(OBJDIR)/%.o: $(SOURCEDIR)/%.c $(SDKHEADERS) $(SOURCEHEADERS)
	$(CC) $(CFLAGS) $(CINCLUDES) -MJ $@.json -c $< -o $@

# Build exe targets (link):
$(BUILDDIR)/AltAppSwitcher.exe: $(AASOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(CFLAGS) $(CLINK) $^ -o $@

$(BUILDDIR)/Settings.exe: $(SETTINGSOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(CFLAGS) $(CLINK) $^ -o $@

$(BUILDDIR)/Updater.exe: $(UPDATEROBJECTS)
	$(CC) $(CFLAGS) $(CLINK) $^ -o $@

# Directory targets:
directories:
	python ./AAS.py MakeDirs $(CONF) $(ARCH)

# Assets:
$(ASSETS): $(BUILDDIR)/%: $(ROOTDIR)/Assets/%
	python ./AAS.py Copy "$<" "$@"

# Compile command
$(SOURCEDIR)/compile_commands.json: $(ALLOBJECTS)
	python ./AAS.py MakeCompileCommands $@ $(subst .o,.o.json, $^)
#	echo($(ALLJSON))

# Other targets:
clean:
	python ./AAS.py Clean

# Lib target:
# Config:
#$(BUILDDIR)/Config.lib: $(CONFIGOBJECTS)
#	ar rcs $@ $^