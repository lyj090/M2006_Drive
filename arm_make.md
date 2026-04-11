# Linux 下编译与烧录（本仓库：M2006_Drive）

根目录 **`Makefile`** 调用 **CMake**（**`cmake/gcc-arm-none-eabi.cmake`**），默认产物 **`build/M2006_Drive.elf`**。烧录走 **OpenOCD** + **`openocd/m2006_drive.cfg`**（ST-Link SWD）。

**芯片与硬件**：Cube 工程见根目录 **`M2006_Drive.ioc`**（README 说明为 **STM32F427** 类 A 板常用配置）；当前 CMake 使用 **`STM32F407XX_FLASH.ld`** 等 **F407** 侧配置。板子型号、引脚与时钟须与固件一致，否则需自行改 IOC / 链接脚本并重新生成。

---

## 1. 环境（Ubuntu / Debian 示例）

```bash
sudo apt update
sudo apt install -y make cmake gcc-arm-none-eabi openocd
cmake --version   # 需 ≥ 3.22（与根目录 CMakeLists.txt 一致）
```

- **调试**：可选 `gdb-multiarch`；**`make openocd`** 后 GDB 连 **`localhost:3333`**。
- **ST-Link 权限**：若设备不可访问，检查 USB 与 **udev** 规则；勿同时被 CubeIDE 等占用。

---

## 2. 全流程（克隆 → 编译 → 烧录）

```bash
git clone <你的仓库地址> M2006_Drive
cd M2006_Drive
make -j"$(nproc)"
make flash
```

- **`make`**：首次自动 **`cmake -S . -B build`**，再编译；末尾打印 **`arm-none-eabi-size`**。
- **`make flash`**：用 OpenOCD 对 **`build/M2006_Drive.elf`** 执行 **program / verify / reset / exit**。

若本机 OpenOCD 脚本不在默认路径：

```bash
make flash OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

**仅清构建**：`make distclean`（删整个 **`build/`**）。**改 CMake 后强刷配置**：`make configure`。

---

## 3. 用 STM32CubeMX「生成」代码（改 IOC 时）

1. 用 **STM32CubeMX** 打开 **`M2006_Drive.ioc`**。
2. 生成工程时工具链选 **CMake**（与根目录 **`CMakeLists.txt`** 一致）。
3. 生成后核对 **`UserCode`** 与根 **`CMakeLists.txt`** 里 **`target_sources`** 是否仍包含你的自定义 `.c`（Cube 有时会覆盖列表）。

日常只改 **`UserCode/`** 且不跑 Cube 时，可跳过本节，直接 **`make`**。

---

## 4. 常用 `make` 目标

| 目标 | 作用 |
|------|------|
| **`make`** / **`all`** | 配置（若需）并编译，生成 **`build/M2006_Drive.elf`** |
| **`make flash`** | 编译后烧录 ELF |
| **`make openocd`** | 仅启动 OpenOCD（供 GDB） |
| **`make bin`** / **`hex`** | 由 ELF 生成 **`.bin`** / **`.hex`** |
| **`make clean`** / **`distclean`** | 清理对象文件 / 删除 **`build/`** |

换构建目录：`make BUILD_DIR=build-other flash`（会烧 **`build-other/M2006_Drive.elf`**）。

---

## 5. 纯 CMake（不用 Makefile）

```bash
cd M2006_Drive
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

烧录与 **`make flash`** 等价示例：

```bash
openocd -s "$PWD/openocd" -s /usr/share/openocd/scripts \
  -f openocd/m2006_drive.cfg \
  -c "program build/M2006_Drive.elf verify reset exit"
```

**OpenOCD 0.10.x**：配置使用 **`adapter_khz`**；**ST-Link V2-1** 时把 **`openocd/m2006_drive.cfg`** 里的 **`stlink-v2.cfg`** 改成 **`stlink-v2-1.cfg`**。

---

## 7. 常见问题（极简）

| 现象 | 处理方向 |
|------|----------|
| **`make flash` 找不到设备** | USB、udev、是否被其它 OpenOCD/IDE 占用 |
| **`reent.h` 缺失等** | 重装 **`gcc-arm-none-eabi`**，或 **`make distclean`** 后重配 |
| **链接脚本 `(READONLY)` 报错** | 根 **`CMakeLists.txt`** 会在配置时生成已处理的 **`build/STM32F407XX_FLASH.ld`**；删 **`build/`** 重新 **`cmake`** |
| **编译过但灯不亮 / 卡死** | HSE/HSI、芯片型号与 **`M2006_Drive.ioc`** 是否一致；见 **`README.md`** |

更完整的协议与模块说明见 **`README.md`**。
