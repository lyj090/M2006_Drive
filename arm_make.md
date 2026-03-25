# Linux 下编译与烧录说明（ARM / STM32）

本工程最初在 **Windows** 上用 **STM32CubeMX** 生成，并在 **Keil MDK** 下用 **ARM Compiler（ARMCC）** 编译、烧录。仓库中已附带 **STM32CubeIDE** 工程，使用 **GNU Arm Embedded Toolchain（`arm-none-eabi-gcc`）**，与 Keil 的工程文件不通用，但源码与 HAL/FreeRTOS 一致，适合在 **Linux** 上直接编译。

**目标芯片**：STM32F427IIHx（与大疆 A 板一致，见 `M2006_Drive.ioc`）。

---

## 1. 与 Keil / Windows 的差异（简要）

| 项目 | Keil（原流程） | Linux 推荐流程 |
|------|----------------|----------------|
| 工程文件 | `.uvprojx` 等 | `STM32CubeIDE/.project`、`.cproject` |
| 编译器 | ARMCC | GCC（`arm-none-eabi-gcc`） |
| 链接脚本 | Keil 分散加载 | `STM32CubeIDE/STM32F427IIHX_FLASH.ld` |

在 Linux 上请使用 **STM32CubeIDE** 或自行用 **GCC + Makefile/CMake** 复现相同宏与包含路径（见下文），不要期望直接打开 Keil 工程。

---

## 2. 命令行编译（Makefile）

本仓库根目录的 **`Makefile`** 会编译 **Core、Drivers、FreeRTOS、`UserCode`** 及启动文件，产物在 **`build/`** 下。需已安装 **`make`** 与 **`arm-none-eabi-gcc`**（如 `sudo apt install make gcc-arm-none-eabi`）。

### 2.1 编译命令

在与 **`Makefile` 同级**的工程根目录执行：

```bash
cd /path/to/M2006_Drive
make
```

并行编译（加快编译）：

```bash
make -j"$(nproc)"
```

先清理再全量重编：

```bash
make clean && make
```

### 2.2 如何判断编译是否成功

| 检查项 | 说明 |
|--------|------|
| **`make` 退出码** | 执行 `make` 后输入 `echo $?`，为 **`0`** 表示构建成功结束。 |
| **无 `error:`** | **语法错误、链接错误**会打印含 **`error:`** 的行，`make` 中途失败，**不会**得到可用的完整 `elf`。 |
| **产物文件** | 成功时存在 **`build/M2006_Drive.elf`**，并会生成 **`build/M2006_Drive.bin`**、**`build/M2006_Drive.hex`**（由 Makefile 中规则自动生成）。 |
| **体积输出** | 成功时末尾会打印 **`arm-none-eabi-size`** 的 `text / data / bss` 行。 |

可选进一步确认 ELF 有效：

```bash
ls -l build/M2006_Drive.elf
arm-none-eabi-readelf -h build/M2006_Drive.elf | grep -E 'Type:|Machine:|Entry'
```

正常情况下：`Type` 为 **`EXEC`**，`Machine` 为 **`ARM`**，**Entry point** 在 **`0x08xxxxxx`**（内部 Flash 区域）。

### 2.3 警告（warning）与错误（error）

- **`warning:`**：默认**仍可能链接成功**；建议逐步修正。若要把警告视为失败，可：`make CFLAGS+=' -Werror'`。  
- **`error:`**：必须修改源码或 **Makefile** 后重新编译。

### 2.4 在 `UserCode` 增加新的 `.c` 文件

**每新增一个需进固件的 `.c`**，对应路径要手动加入 **`Makefile`** 里的 **`SRCS_C`** 变量，否则该文件**不会被编译**。

---

## 3. 方式 B：命令行 + OpenOCD（无 CubeIDE GUI）

适用于已安装 **`arm-none-eabi-gcc`**、已用 **§2** 中 **`Makefile`** 或 STM32CubeIDE 生成固件的情况。烧录示例中的路径默认为 **`build/M2006_Drive.elf`**。

### 3.1 交叉编译器

任选其一：

- 使用 STM32CubeIDE 自带的 `arm-none-eabi-*`（将其 `bin` 加入 `PATH`），或  
- 安装 **GNU Arm Embedded Toolchain**（ARM 官方提供的 `gcc-arm-none-eabi`）。

### 3.2 生成固件格式

编译得到 `M2006_Drive.elf` 后，可生成 hex/bin 供其它工具烧录：

```bash
arm-none-eabi-objcopy -O ihex M2006_Drive.elf M2006_Drive.hex
arm-none-eabi-objcopy -O binary M2006_Drive.elf M2006_Drive.bin
```

### 3.3 OpenOCD + GDB：双终端烧录与调试（推荐理解流程）

OpenOCD 启动后会占用调试器，并打开两个服务（默认）：

| 端口 | 作用 |
|------|------|
| **3333** | **GDB 远程调试**（`target extended-remote` 连这里） |
| **4444** | **Telnet 控制 OpenOCD**（可发 `reset halt`、`program` 等 TCL 命令） |

**步骤 1：终端 A — 保持 OpenOCD 运行**

**说明**：OpenOCD **0.10.x**（常见 apt 版本）**没有** `interface/stlink.cfg`，请使用 `stlink-v2.cfg`（最常见）或 `stlink-v2-1.cfg` / `stlink-v1.cfg`。若报找不到脚本，加上 **`-s /usr/share/openocd/scripts`**。

```bash
cd /path/to/M2006_Drive
openocd -s /usr/share/openocd/scripts \
  -f interface/stlink-v2.cfg -f target/stm32f4x.cfg
```

看到类似 `stm32f4x.cpu: hardware has ... breakpoints` 即表示已连上芯片，**不要关这个窗口**。

**步骤 2：终端 B — 用 GDB 把程序写进 Flash 并运行**

Ubuntu/Debian 上若 **`arm-none-eabi-gdb` 不存在**，请安装 **`gdb-multiarch`**（`sudo apt install gdb-multiarch`），用它来调试 ARM ELF：

```bash
cd /path/to/M2006_Drive
gdb-multiarch build/M2006_Drive.elf
```

在 **`(gdb)`** 提示符下依次输入（建议顺序如下）：

| 命令 | 含义 |
|------|------|
| `target extended-remote localhost:3333` | 通过 OpenOCD 连到板子；连上后 PC 可能停在任意位置，属正常。 |
| `monitor reset halt` | 让 OpenOCD 发 **复位并停住**（halt），CPU 停在调试状态，便于可靠下载。 |
| `load` | 把 **ELF 里的各段**（`.text`、`.data` 等）按链接地址写入 **Flash/RAM**；即完成“烧录”。成功会打印各段 `lma` 与 `load size`。 |
| `monitor reset halt` | 再次复位并停住，让 PC 指向 **复位向量**（相当于上电后停在入口附近），便于单步或继续。 |
| `continue`（简写 **`c`**） | **全速运行**固件；程序在 MCU 上跑起来。 |

可选：若只想下断点、单步，可在 `load` 之后用 **`break main`**、**`continue`**，再用 **`Ctrl+C`** 暂停。

**如何退出 GDB**

- 输入 **`quit`** 或 **`q`**，若提示确认再输入 **`y`**。  
- GDB **没有** `exit` 命令（会报 `Undefined command`）。  
- 若程序正在 **`continue`** 中运行，可先 **`Ctrl+C`** 暂停，再 **`quit`**，再关终端。

**若在 `continue` 时按了 `Ctrl+C`**

- GDB 会暂停 CPU，栈可能停在 **`HardFault_Handler`**、中断或任意位置，**不一定表示程序有 bug**；也可能是你手动打断时正好落在异常里。  
- 若需结束会话：先 **`quit`** 退出 GDB，再 **`Ctrl+C`** 结束终端 A 里的 OpenOCD。

**一条命令烧录（不常驻 GDB）**

若不需要 GDB 会话，先**关掉**已运行的 OpenOCD，再单独执行（会占用 ST-Link 直到结束）：

```bash
openocd -s /usr/share/openocd/scripts \
  -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
  -c "program build/M2006_Drive.elf verify reset exit"
```

**说明**：`Unable to match requested speed 2000 kHz, using 1800 kHz` 仅为 SWD 时钟微调，一般可忽略。

也可使用 **`STM32CubeProgrammer` 命令行**（若已安装）对 `.hex`/`.bin` 烧录，参数以 ST 文档为准。

### 3.4 使用 st-flash（可选）

若使用 **stlink** 工具集，可将 **bin** 烧到 Flash 起始地址（STM32F4 内部 Flash 通常从 **`0x08000000`** 开始）：

```bash
st-flash write M2006_Drive.bin 0x08000000
```

请先确认本板连接与地址与数据手册一致。

### 3.5 用 GDB 判断「卡在哪」（灯不闪 / 复位无反应）

**准备**：终端 A 启动 OpenOCD（同 §3.3）；终端 B 在工程根目录：

```bash
cd /path/to/M2006_Drive
gdb-multiarch -x gdb_m2006.gdb build/M2006_Drive.elf
```

建议进入 **`(gdb)`** 后先同步 Flash，再跑脚本里的命令：

```text
load
monitor reset halt
run_debug
```

`run_debug` 会 **全速运行** 直到命中某一断点。根据**最先停下的位置**判断：

| 停在此处 | 含义（常见） |
|----------|----------------|
| **`Error_Handler`** | `HAL_RCC_OscConfig`、`HAL UART/CAN` 等返回失败。执行 **`bt`** 看调用栈。 |
| **`HardFault_Handler`** 等 | 非法访问、总线错误。执行 **`bt`**、**`info registers`**。 |
| **`main`** | 用 **`n`**（next）单步，看 **`SystemClock_Config()`** 是否完整返回。 |
| **`SystemClock_Config`** | 用 **`n`** / **`s`** 单步，是否卡在 **`HAL_RCC_OscConfig`**（尤其无 HSE 老固件）。 |
| **`USER_INIT`** | `main` 与基本外设初始化大致通过；再 **`c`** 看是否进 **`lightTask`**。 |
| **`lightTask`** | 闪灯任务已执行；灯仍不亮多属 **GPIO 与硬件 LED 引脚不一致**。 |

常用：**`bt`** 调用栈，**`frame N`** 切换栈帧，**`list`** 看源码，**`c`** 继续，**`q`** 退出。

---

## 4. 使用 STM32CubeMX 重新生成代码时

若你在 Linux 上安装了 **STM32CubeMX**，用 **`M2006_Drive.ioc`** 重新生成代码：

- 选择 **Makefile** 或 **STM32CubeIDE** 作为工具链/IDE，以便在 Linux 下用 GCC 构建。  
- 重新生成后请检查 **`UserCode`** 中自定义文件是否被覆盖；通常应把用户代码放在 CubeMX 标注的 `USER CODE` 区域，或单独维护 `UserCode` 并再次加入工程。

---

## 5. 常见问题

- **找不到 ST-Link**：检查 USB 线、驱动/udev，以及当前用户权限。  
- **编译通过但运行异常**：对比 Keil 与 GCC 的 **优化等级**、**浮点 ABI**（本工程为 **FPU + hard float**）及 **全局宏**（如 `STM32F427xx`、`USE_HAL_DRIVER`）是否与 `STM32CubeIDE/.cproject` 中一致。  
- **仍想使用 Keil**：仅在 Windows 上安装 Keil，用原有 `.uvprojx` 打开；与 Linux 下 GCC 工程并行维护时，注意两边源文件与宏保持一致。

---

更详细的应用逻辑与协议说明见仓库根目录 **`README.md`**。
