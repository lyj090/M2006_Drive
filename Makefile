# 薄封装：编译走 CMake（CubeMX 工程），本文件仅提供常用入口与 OpenOCD 烧录。
# 默认构建目录与 CMake -B 一致，可通过 BUILD_DIR=... 覆盖。

TARGET           := M2006_Drive
BUILD_DIR        ?= build
CMAKE            ?= cmake
TOOLCHAIN        ?= cmake/gcc-arm-none-eabi.cmake
CMAKE_BUILD_TYPE ?= Debug

OPENOCD          ?= openocd
OPENOCD_SCRIPTS  ?= /usr/share/openocd/scripts
OPENOCD_CFG      := openocd/m2006_drive.cfg

PREFIX   ?= arm-none-eabi-
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

ELF := $(BUILD_DIR)/$(TARGET).elf
BIN := $(BUILD_DIR)/$(TARGET).bin
HEX := $(BUILD_DIR)/$(TARGET).hex

NPROC := $(shell nproc 2>/dev/null || echo 4)

.PHONY: all configure clean distclean flash openocd bin hex size

all: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --parallel $(NPROC)
	$(SIZE) $(ELF)

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

# 强制重新配置（例如改了 CMakeLists / 工具链）
configure:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean 2>/dev/null || true

distclean:
	rm -rf $(BUILD_DIR)

bin: all
	$(OBJCOPY) -O binary $(ELF) $(BIN)

hex: all
	$(OBJCOPY) -O ihex $(ELF) $(HEX)

size: all
	$(SIZE) $(ELF)

openocd:
	$(OPENOCD) -s $(CURDIR)/openocd -s $(OPENOCD_SCRIPTS) -f $(OPENOCD_CFG)

flash: all
	$(OPENOCD) -s $(CURDIR)/openocd -s $(OPENOCD_SCRIPTS) -f $(OPENOCD_CFG) \
		-c "program $(ELF) verify reset exit"
