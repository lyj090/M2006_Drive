# Linux 下编译与烧录说明（ARM / STM32）

**主构建方式**：根目录 **`CMakeLists.txt`**（CubeMX 生成）+ 工具链 **`cmake/gcc-arm-none-eabi.cmake`**。根目录 **`Makefile`** 仅为薄封装：内部执行 **`cmake -S . -B <构建目录>`** 与 **`cmake --build`**，并提供 **`flash`** / **`openocd`** 等入口。

| 项目 | 说明 |
|------|------|
| **默认构建目录** | **`build/`**（可用 **`make BUILD_DIR=其他目录`** 覆盖） |
| **当前 CMake 目标芯片** | **STM32F407**（启动文件、**`STM32F407XX_FLASH.ld`**、**`cmake/stm32cubemx`**） |
| **烧录的 ELF** | 默认 **`build/M2006_Drive.elf`**（与 **`make flash`** 一致） |

**硬件须与固件一致**：若板子为 **F427** 等其它型号，请用 **CubeMX / STM32CubeIDE** 生成对应工程并调整链接脚本与宏，勿直接假定本仓库 CMake 产物可烧录到你的板子。

---

## 1. 推荐：烧录流程（`make` 包装 CMake + ST-Link）

### 1.1 依赖

- **`make`**、**`arm-none-eabi-gcc`**（如 `sudo apt install make gcc-arm-none-eabi`）
- **`openocd`**（如 `sudo apt install openocd`，常见为 **0.10.x**）
- 调试器：**ST-Link**（USB），目标板供电正常

### 1.2 编译并烧录

在**工程根目录**（与 `Makefile` 同级）执行：

```bash
cd /path/to/M2006_Drive
make -j"$(nproc)"
make flash
```

含义简述：

- **`make`**：若尚无 **`build/CMakeCache.txt`** 则先 **`cmake` 配置**，再 **`cmake --build`**，得到 **`build/M2006_Drive.elf`**；末尾打印 **`arm-none-eabi-size`**。默认不生成 **`.bin` / `.hex`**，需要时可执行 **`make bin`**、**`make hex`**。
- **`make flash`**：先保证 ELF 已构建，再调用 **OpenOCD**，加载 **`openocd/m2006_drive.cfg`**，执行 **`program …/M2006_Drive.elf verify reset exit`**。

若系统脚本不在默认路径，可覆盖变量，例如：

```bash
make flash OPENOCD_SCRIPTS=/usr/share/openocd/scripts
```

### 1.3 仅启动 OpenOCD（供 GDB 连接）

终端 A 保持运行：

```bash
cd /path/to/M2006_Drive
make openocd
```

等价于使用 **`openocd/m2006_drive.cfg`**（ST-Link V2 + `hla_swd` + `stm32f4x.cfg`）。连接成功后 GDB 使用 **`localhost:3333`**。

### 1.4 OpenOCD 版本说明

- **0.10.x**：配置里须使用 **`adapter_khz`**；**`adapter speed`** 会报错 `invalid command name "adapter"`。本仓库 **`openocd/m2006_drive.cfg`** 已按 0.10 写法配置。
- 日志中 **`Unable to match requested speed … using … kHz`** 表示 ST-Link 将 SWD 频率取到邻近可用值，**一般可忽略**。

---

## 2. 命令行编译（`make` 与 CMake）

源文件列表由 **CMake**（**`CMakeLists.txt`** / **`cmake/stm32cubemx/CMakeLists.txt`**）维护；根 **`Makefile`** 不列举 **`.c`**。

### 2.1 常用目标

| 目标 | 作用 |
|------|------|
| **`make`** / **`make all`** | 配置（若需要）并编译，生成 **`$(BUILD_DIR)/M2006_Drive.elf`** |
| **`make configure`** | 仅重新运行 **`cmake` 配置**（改了 **`CMakeLists.txt`** 等时可用） |
| **`make clean`** | **`cmake --build … --target clean`**（清理对象文件，保留 **`CMakeCache.txt`**） |
| **`make distclean`** | 删除整个 **`$(BUILD_DIR)`** |
| **`make bin`** / **`make hex`** | 在已有 ELF 上用 **`objcopy`** 生成 **`.bin`** / **`.hex`** |
| **`make size`** | 打印 **`arm-none-eabi-size`** |

并行度由 **`Makefile`** 内 **`cmake --build -j$(nproc)`** 决定；也可完全不用 **`make`**，只用下一节的 **`cmake`** 命令。

### 2.2 如何判断编译是否成功

| 检查项 | 说明 |
|--------|------|
| **退出码** | **`0`** 表示成功。 |
| **产物** | 存在 **`build/M2006_Drive.elf`**（或你指定的 **`BUILD_DIR`**）。 |
| **体积** | **`make`** 成功时末尾有 **`arm-none-eabi-size`**。 |

可选：

```bash
ls -l build/M2006_Drive.elf
arm-none-eabi-readelf -h build/M2006_Drive.elf | grep -E 'Type:|Machine:|Entry'
```

正常情况下：`Type` 为 **`EXEC`**，`Machine` 为 **`ARM`**，**Entry point** 在 **`0x08xxxxxx`**。

### 2.3 在 `UserCode` 增加新的 `.c` 文件

在根目录 **`CMakeLists.txt`** 的 **`target_sources(${CMAKE_PROJECT_NAME} …)`** 中加入新文件；**CubeMX 重生成**后若覆盖了 **`CMakeLists.txt`**，需再次合并 **`UserCode`** 相关条目。

### 2.4 变量覆盖示例

```bash
make BUILD_DIR=build-cmake flash
```

会先在该目录下 **`cmake` 配置并编译**，再烧录 **`build-cmake/M2006_Drive.elf`**。

---

## 3. OpenOCD / GDB 手动命令（与 `make flash` 等价关系）

适用于想理解流程或脚本自定义的场景。烧录对象默认为 **`build/M2006_Drive.elf`**（与 **`make`** 的 CMake 产物一致）。

### 3.1 工程配置与系统脚本

推荐使用仓库 **`openocd/m2006_drive.cfg`**（已含 `transport select hla_swd`、**`adapter_khz`**、**`stm32f4x`**）。若调试器为 **ST-Link V2-1**，请编辑该文件，将 **`stlink-v2.cfg`** 改为 **`stlink-v2-1.cfg`**。

### 3.2 一条命令烧录（无需 `make flash`）

先关闭已占用的 OpenOCD，在工程根目录：

```bash
cd /path/to/M2006_Drive
openocd -s "$PWD/openocd" -s /usr/share/openocd/scripts \
  -f openocd/m2006_drive.cfg \
  -c "program build/M2006_Drive.elf verify reset exit"
```

### 3.3 双终端：OpenOCD + GDB

**终端 A**（保持运行）：

```bash
cd /path/to/M2006_Drive
make openocd
```

或手写：

```bash
openocd -s "$PWD/openocd" -s /usr/share/openocd/scripts -f openocd/m2006_drive.cfg
```

**终端 B**（无 `arm-none-eabi-gdb` 时可用 **`gdb-multiarch`**）：

```bash
cd /path/to/M2006_Drive
gdb-multiarch build/M2006_Drive.elf
```

在 **`(gdb)`** 中建议顺序：

| 命令 | 含义 |
|------|------|
| `target extended-remote localhost:3333` | 连接 OpenOCD |
| `monitor reset halt` | 复位并暂停 |
| `load` | 将 ELF 写入 Flash（烧录） |
| `monitor reset halt` | 再次复位暂停 |
| `continue`（`c`） | 全速运行 |

**说明**：OpenOCD **0.10.x** 没有 **`interface/stlink.cfg`**，请使用 **`interface/stlink-v2.cfg`** 等；本工程已封装在 **`m2006_drive.cfg`** 中。

### 3.4 使用 st-flash（可选）

将 **`build/M2006_Drive.bin`** 写到内部 Flash 起始地址（STM32F4 一般为 **`0x08000000`**）：

```bash
st-flash write build/M2006_Drive.bin 0x08000000
```

### 3.5 用 GDB 脚本排查启动问题

终端 A：`make openocd`。终端 B：

```bash
cd /path/to/M2006_Drive
gdb-multiarch -x gdb_m2006.gdb build/M2006_Drive.elf
```

进入 **`(gdb)`** 后可先 **`load`**、**`monitor reset halt`**，再 **`run_debug`**（见 `gdb_m2006.gdb` 注释）。

| 停在此处 | 常见含义 |
|----------|-----------|
| **`Error_Handler`** | 时钟、外设初始化失败等，用 **`bt`** 看栈 |
| **`HardFault_Handler`** | 非法访问等 |
| **`USER_INIT`** / **`lightTask`** | 调度与闪灯逻辑；灯不亮多为 **GPIO 与硬件不一致** |

---

## 4. 直接使用 CMake（不经过根 `Makefile`）

与 **`make`** 等价的核心命令如下（默认 **`build/`**；换目录可避免与已有构建缓存混用）：

```bash
cd /path/to/M2006_Drive
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

烧录示例（路径与 **`BUILD_DIR`** 一致即可）：

```bash
openocd -s "$PWD/openocd" -s /usr/share/openocd/scripts \
  -f openocd/m2006_drive.cfg \
  -c "program build/M2006_Drive.elf verify reset exit"
```

### 4.1 常见问题（CMake）

- **`reent.h` 找不到**：与 **`configUSE_NEWLIB_REENTRANT`** 有关；**`cmake/gcc-arm-none-eabi.cmake`** 已尝试加入 newlib 的 **`arm-none-eabi/include`**。仍失败可重装 **`gcc-arm-none-eabi`** 或 **`make distclean`** / 删除构建目录后重新配置。
- **链接脚本 `.ARM.extab` / `(READONLY)` 报错**：**GCC 9/10** 的 `ld` 不支持 CubeMX 在 **`.ld`** 里写的 **`(READONLY)`**。本仓库 **`CMakeLists.txt`** 在每次 **`cmake` 配置**时从 **`STM32F407XX_FLASH.ld`** 生成已剥离该关键字的 **`${CMAKE_BINARY_DIR}/STM32F407XX_FLASH.ld`** 并用于 **`-T`**；重新 **`cmake`** 或删构建目录即可。

---

## 5. 使用 STM32CubeMX 重新生成代码时

若用 **`M2006_Drive.ioc`** 重新生成：

- 生成工具选 **CMake**（与仓库根目录 **`CMakeLists.txt`** 一致）。  
- 注意 **`UserCode`** 是否被覆盖；自定义文件宜放在 **USER CODE** 区或单独目录，并在根 **`CMakeLists.txt`** 的 **`target_sources`** 中登记。

---

## 6. 常见问题

- **找不到 ST-Link**：USB、**udev** 权限、是否被其它 OpenOCD/CubeIDE 占用。  
- **`make flash` 失败**：确认 **`build/M2006_Drive.elf` 已生成**；**`openocd` 已安装**；**`OPENOCD_SCRIPTS`** 指向本机脚本目录。  
- **`stm32f4xx.h` 相关型号头缺失**：当前 CMake 工程以 **F407** 为主，需 **`stm32f407xx.h`** 等。若你改为 **F427** 等型号，须用 **CubeMX** 生成对应 CMSIS 设备头，或从 ST **[cmsis-device-f4](https://github.com/STMicroelectronics/cmsis-device-f4)** 补全 **`stm32f427xx.h`** 等到 **`Drivers/CMSIS/Device/ST/STM32F4xx/Include/`**。  
- **编译通过但运行异常**：核对芯片宏（**`STM32F427xx`** vs **`STM32F407xx`**）、链接脚本与 **浮点 ABI**（**`-mfloat-abi=hard`**）是否与硬件一致。  
- **仍想使用 Keil**：在 Windows 用 **`.uvprojx`**；与 Linux GCC 工程并行时注意源文件与宏一致。

---

更详细的应用逻辑与协议说明见仓库根目录 **`README.md`**。
