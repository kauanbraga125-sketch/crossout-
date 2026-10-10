TITLE := PS4 USB Link Fixer
VERSION := 09.00
TITLE_ID := ULFX00001
CONTENT_ID := IV0000-ULFX00001_00-USBLINKFIXER0001
LIBS := -lc -lkernel -lc++ -lSDL2 -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService -lSceSysmodule -lSceSystemService
TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
PROJDIR := source
INTDIR := build
MODULE_DATA := $(TOOLCHAIN)/src/modules
CPPFILES := $(wildcard $(PROJDIR)/*.cpp)
OBJS := $(patsubst $(PROJDIR)/%.cpp,$(INTDIR)/%.o,$(CPPFILES))
CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -D_GNU_SOURCE -DUSB_FIXER_PS4 -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS := $(CFLAGS) -std=c++11 -isystem $(TOOLCHAIN)/include/c++/v1
LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o
CCX ?= clang++-18
LD ?= ld.lld-18
CDIR := linux
RUNTIME_MODULES := sce_module/libc.prx sce_module/libSceFios2.prx
PACKAGE_FILES := eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(RUNTIME_MODULES)
.PHONY: all clean prepare
all: $(CONTENT_ID).pkg
$(INTDIR):
	mkdir -p $(INTDIR)
$(INTDIR)/%.o: $(PROJDIR)/%.cpp | $(INTDIR)
	$(CCX) $(CXXFLAGS) -o $@ $<
eboot.bin: $(OBJS)
	$(LD) $(OBJS) -o $(INTDIR)/usb-link-fixer.elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/usb-link-fixer.elf -out=$(INTDIR)/usb-link-fixer.oelf --eboot "eboot.bin" --paid 0x3800000000000011
sce_sys/about/right.sprx:
	mkdir -p sce_sys/about
	cp $(MODULE_DATA)/right.sprx $@
sce_module/libc.prx:
	mkdir -p sce_module
	cp $(MODULE_DATA)/libc.prx $@
sce_module/libSceFios2.prx:
	mkdir -p sce_module
	cp $(MODULE_DATA)/libSceFios2.prx $@
sce_sys/param.sfo: Makefile
	mkdir -p sce_sys
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'
prepare: eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(RUNTIME_MODULES)
pkg.gp4: prepare
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$(PACKAGE_FILES)"
$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .
clean:
	rm -rf $(INTDIR) eboot.bin pkg.gp4 $(CONTENT_ID).pkg sce_sys sce_module
