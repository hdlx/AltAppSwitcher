ARCH = x86_64
CONF = Debug

# Directories
ROOTDIR = .
OUTPUTDIR = $(ROOTDIR)/Output
BUILDDIR = $(OUTPUTDIR)/$(CONF)_$(ARCH)
SOURCEDIR = $(ROOTDIR)/Sources
OBJDIR = $(BUILDDIR)/Objects
SDKDIR = $(ROOTDIR)/SDK/Headers
LIBDIR = $(ROOTDIR)/SDK/Libs/$(ARCH)
INCLUDEDIR = $(ROOTDIR)/Sources

# Common var
CC = clang
CINCLUDES = -I $(ROOTDIR)/SDK/headers -I $(ROOTDIR)/Sources
CLINK =  -static-libgcc -L $(LIBDIR) -l dwmapi -l User32 -l Gdi32 -l Gdiplus -l shlwapi -l pthread -l Ole32 -l Comctl32 -l ws2_32 -l libzip -l zlib -l bcrypt
CFLAGS = -g -Wall -static -D ARCH_$(ARCH)=1 -target $(ARCH)-mingw64

SDKHEADERS = $(wildcard $(SDKDIR)/**/*.h) $(wildcard $(SDKDIR)/*.h) 
SOURCEHEADERS = $(wildcard $(SOURCEDIR)/**/*.h) $(wildcard $(SOURCEDIR)/*.h) 

AASOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/AltAppSwitcher/*.c))
CONFIGOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/Config/*.c))
SETTINGSOBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SOURCEDIR)/Settings/*.c))

ASSETS = $(patsubst $(ROOTDIR)/Assets/%, $(BUILDDIR)/%, $(wildcard $(ROOTDIR)/Assets/*))

.PHONY: all clean

all: $(BUILDDIR)/AltAppSwitcher.exe $(BUILDDIR)/Settings.exe $(ASSETS)

# Compile object targets:
# AAS
$(AASOBJECTS): $(OBJDIR)/%.o: $(SOURCEDIR)/%.c $(SDKHEADERS) $(SOURCEHEADERS) | $(OBJDIR)/AltAppSwitcher
	$(CC) $(CFLAGS) $(CINCLUDES) -c $< -o $@
# Config
$(CONFIGOBJECTS): $(OBJDIR)/%.o: $(SOURCEDIR)/%.c $(SDKHEADERS) $(SOURCEHEADERS) | $(OBJDIR)/Config
	$(CC) $(CFLAGS) $(CINCLUDES) -c $< -o $@
# Settings
$(SETTINGSOBJECTS): $(OBJDIR)/%.o: $(SOURCEDIR)/%.c $(SDKHEADERS) $(SOURCEHEADERS) | $(OBJDIR)/Settings
	$(CC) $(CFLAGS) $(CINCLUDES) -c $< -o $@

# Build exe targets (link):
$(BUILDDIR)/AltAppSwitcher.exe: $(AASOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(CFLAGS) $(CLINK) $^ -o $@

$(BUILDDIR)/Settings.exe: $(SETTINGSOBJECTS) $(CONFIGOBJECTS)
	$(CC) $(CFLAGS) $(CLINK) $^ -o $@

# Directory targets:
# | defines order-only-prerequisites
$(OBJDIR)/AltAppSwitcher: | $(OBJDIR)
	mkdir "$@"
$(OBJDIR)/Settings: | $(OBJDIR)
	mkdir "$@"
$(OBJDIR)/Config: | $(OBJDIR)
	mkdir "$@"
$(OBJDIR): | $(BUILDDIR)
	mkdir "$@"
$(BUILDDIR): | $(OUTPUTDIR)
	mkdir "$@"
$(OUTPUTDIR):
	mkdir "$@"

# Assets:
$(ASSETS): $(BUILDDIR)/%: $(ROOTDIR)/Assets/% | $(BUILDDIR)
	cp "$<" "$@"

# Other targets:
clean:
	rm -rf "$(OUTPUTDIR)"

# Lib target:
# Config:
#$(BUILDDIR)/Config.lib: $(CONFIGOBJECTS)
#	ar rcs $@ $^