PKG = net.siguza.hsp4
TARGET = hsp4
KFWK = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.12.sdk/System/Library/Frameworks/Kernel.framework
FLG ?= -O2 -Wall -g -fno-builtin -fno-common -nostdinc -mkernel -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -I$(KFWK)/Headers -nostdlib -Wl,-kext -lkmod -lkmodc++ -lcc_kext $(CFLAGS)

.PHONY: all clean

all: $(PKG).kext/Contents/MacOS/$(TARGET) $(PKG).kext/Contents/Info.plist

$(PKG).kext/Contents/MacOS/$(TARGET): src/*.c | $(PKG).kext/Contents/MacOS
	$(CC) -o $@ $^ $(FLG)

$(PKG).kext/Contents/Info.plist: misc/Info.plist | $(PKG).kext/Contents/MacOS
	cp -f $< $@

$(PKG).kext/Contents/MacOS:
	mkdir -p $@

clean:
	rm -rf $(PKG).kext
