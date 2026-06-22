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

.PHONY: all configure clean distclean flash flash-slow flash-stlink openocd bin hex size

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

# OpenOCD 烧录：禁用 gdb/tcl 端口；halt 后 program；reset-init 已在 cfg 中限速
OPENOCD_FLASH_CMDS := gdb_port disabled; tcl_port disabled; adapter_khz 480; init; halt; adapter_khz 480; program $(ELF) verify reset exit

flash: all
	$(OPENOCD) -s $(CURDIR)/openocd -s $(OPENOCD_SCRIPTS) -f $(OPENOCD_CFG) \
		-c "$(OPENOCD_FLASH_CMDS)"

flash-slow: all
	$(OPENOCD) -s $(CURDIR)/openocd -s $(OPENOCD_SCRIPTS) -f $(OPENOCD_CFG) \
		-c "gdb_port disabled; tcl_port disabled; adapter_khz 200; init; halt; program $(ELF) verify reset exit"

# 备选：st-flash（不经过 OpenOCD reset-init 提速，运行中固件时往往更稳）
flash-stlink: bin
	st-flash --reset write $(BIN) 0x08000000
