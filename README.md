# M2006_Drive

> 配套上位机：**[motor_serial](https://github.com/mzy8329/motor_serial)**

面向 **大疆 M2006 减速电机**（经 **C610 / RoboMaster 电调协议** 所对应的 CAN 帧格式）的 MCU 下位机：通过 **串口** 接收上位机目标量，在片内做 **PID / 限幅**，经 **CAN** 下发电流并由电调回传反馈。

本仓库的 Cube 工程芯片为 **STM32F427IIHx**（与 **大疆 A 板**常用配置一致，`M2006_Drive.ioc`）。README 中亦提到 **STM32F407**（C 板）——需使用对应 IOC / 工程自行适配引脚与时钟。

---

## 功能概览

| 方向 | 说明 |
|------|------|
| **上位机 → MCU** | **USART1**（默认 **PA9** TX、**PB7** RX，以 `M2006_Drive.ioc` / `Core/Src/usart.c` 为准）中断接收固定格式数据包，解析为各电机的 `angle_ref` / `rpm_ref` / `current_ref`。 |
| **MCU → 电机** | **CAN1** 周期发送 **标准帧 ID `0x200`**、8 字节payload：4 路 `int16` 电流指令（与 C610 类控制帧一致；代码注释按四路编写，默认参与闭环电机数为 `USE_MOTOR_NUM`）。 |
| **电机 → MCU** | CAN 接收 **ID `0x201` 起**，第 *i* 路为 `0x201 + i`，解析角度、转速、力矩等反馈，并结合 M2006 **减速比 36**、反馈角度量程（代码中按 **8191** 作为单圈刻度参与推算，见 `UpdataMotor` 注释）做圈数累积与输出轴角度。 |
| **MCU → 上位机** | **USART1** 按固定频率上行反馈包（电机 id、角度、转速、力矩）；收到合法下行包时也会回传当前状态。 |
| **调试** | `printf` 经 `syscalls.c` 中 weak **`__io_putchar`**；若未在工程中实现，则无实际 UART 输出，可按需接到某一 USART。 |

运行时基于 **FreeRTOS**：`main` 完成 HAL 与 **USART1 / CAN1 / GPIO** 等初始化后启动调度；在 `freertos.c` 的 `USER_INIT()` 里创建用户任务。

---

## 软件结构（任务与文件）

```
USER_INIT()  [user_init.c]
  ├─ MOTOR_INIT()           [user_defination.c]  PID 参数与电机结构体初值
  ├─ lightTaskStart()       指示灯：**GPIOH PIN10** 约 **1 s** 翻转（便于确认板子在跑）
  ├─ CanSerialTaskStart()   CAN 收发与闭环、下发电流
  └─ SerialTaskStart()      USART1 上报反馈、解析控制包
```

| 模块 | 文件 | 作用 |
|------|------|------|
| 电机与 PID | `UserCode/user_defination.c`、`user_defination.h` | 数据结构、`MOTOR_INIT`、`MotorCtrl`（位置/速度/电流三种伺服）、`PID_Cal`、限位数组 |
| CAN 协议与线程 | `UserCode/can_serial.c`、`can_serial.h` | `CAN_INIT`、`CanTransmitMotor0123`、`HAL_CAN_RxFifo0MsgPendingCallback`、`CanSerialTask`（含位置保护） |
| 串口协议与线程 | `UserCode/uart_serial.c`、`uart_serial.h` | 包头 `0x44 0x22`、收包解析写 `motor[id].RefData`、周期 `UartTransmit` |
| 任务入口 | `UserCode/user_init.c`、`user_init.h` | `USER_INIT()`，汇总启动上述线程 |

---

## 控制逻辑摘要

以 **`UserCode/user_defination.h`** 中 **`MOTOR_CTRL_CURRENT_ONLY`** 为准：

- **`MOTOR_CTRL_CURRENT_ONLY == 1`（当前默认）**：下位机仅对 **`current_ref`** 做 ±**`M2006_CURRENT_MAX`** 限幅后作为 `current_out`；位置/速度环由上位机完成。  
- **`MOTOR_CTRL_CURRENT_ONLY == 0`**：下位机三环逻辑如下：  
  - **位置模式**：`angle_ref != -1` 时，先角度环 PID（输出作转速环给定），再转速环 PID，输出作为 `current_out`（内部按输出轴/电机轴做了与减速比相关的换算）。  
  - **速度模式**：`rpm_ref != -1`（且位置指令为 -1）时，仅转速环。  
  - **电流模式**：`current_ref != -1`（且前两者为 -1）时，电流在给定范围内直通（仍受 `rpm_pid` 的 output 限幅约束）。  
  - **无效/空闲**：三者均为 -1 时输出电流为 0。  
- **位置保护**（CAN 任务内）：对 `MOTOR_IS_POS[i] == 1` 的轴，若 `globalAngle.angleAll` 超出 `MOTOR_MIN[i]..MOTOR_MAX[i]`，将 `current_out` 置 0。

**`USE_MOTOR_NUM`** 须与 **`MOTOR_IS_POS` / `MOTOR_MIN` / `MOTOR_MAX`** 数组长度一致（**以 `user_defination.h` 当前值为准**）；`motor[]` 固定 **4** 元素以匹配 **CAN 四路电流帧**，**未使用通道的电流在 `MotorCtrl` 中强制为 0**，避免误控未接电调的路。

---

## 串口包格式（与 `uart_serial.c` 一致）

每帧 **15 字节**：**2 字节帧头** `0x44 0x22` + **13 字节 payload**（紧凑布局）：

- `id`：电机序号  
- 三个 **float**：依次为角度参考、转速参考、电流参考（具体字段名见 `RECV_Bag_u` / `SEND_Bag_u`）

上行反馈结构对称，用于把 `angle_fdb`、`rpm_fdb`、`torque_fdb` 发回上位机。

线程频率相关宏：**`UART_SERIAL_FREQUENCY`**、**`CAN_SERIAL_FREQUENCY`**（见 `user_defination.h`）。

---

## 常用修改入口

| 需求 | 位置 |
|------|------|
| 电机数量、串口/CAN 频率 | `UserCode/user_defination.h`：`USE_MOTOR_NUM`、`UART_SERIAL_FREQUENCY`、`CAN_SERIAL_FREQUENCY` |
| CAN 发送 ID、四路电流打包 | `UserCode/can_serial.c`：`CanTransmitMotor0123`（如 `StdId`、`TxData` 布局） |
| 串口协议 | `UserCode/uart_serial.c` |
| PID 初值 | `UserCode/user_defination.c`：`MOTOR_INIT` |
| 是否启用位置限位、区间 | `UserCode/user_defination.c`：`MOTOR_IS_POS`、`MOTOR_MIN`、`MOTOR_MAX` |

---

## 编译与烧录（Linux）

**编译**以根目录 **CMake**（**`CMakeLists.txt`** + **`cmake/gcc-arm-none-eabi.cmake`**）为准；根目录 **`Makefile`** 仅包装 **`cmake` 配置/构建**，默认产物 **`build/M2006_Drive.elf`**。需要 **`.bin`/`.hex`** 时在成功编译后执行 **`make bin`** / **`make hex`**。**一键烧录**：**`make flash`**（内部先构建再调用 **`openocd/m2006_drive.cfg`** + ST-Link）。完整说明见 **`arm_make.md`**。

```bash
cd /path/to/M2006_Drive
make -j"$(nproc)"
make flash
```

若要用图形化工具改时钟与引脚，可用 **STM32CubeMX** 打开根目录 **`M2006_Drive.ioc`**，生成代码时选择 **CMake** 工具链，并在生成后把 **`UserCode`** 源文件与包含路径合并回根目录 **`CMakeLists.txt`**（与当前 **`target_sources`** 一致）。

---

## 现象：烧录后按复位灯不闪（`lightTask` 使用 **GPIOH PIN10**）

闪灯在 **`USER_INIT()` → `lightTaskStart()`** 里，约 **1 s** 翻转 **GPIOH 10**（另见 `user_init.c` 中对 PH10/11/12 的初始化）。若**完全没有**闪烁，常见原因：

| 原因 | 说明 |
|------|------|
| **HSE 未起振** | 原工程假定 **外置 12 MHz**（见 `M2006_Drive.ioc` / `HSE_VALUE`）。无晶振或焊接异常时，`HAL_RCC_OscConfig` 会失败并进入 **`Error_Handler()`** 死循环，**到不了 FreeRTOS**，灯不会闪。当前 **`main.c`** 已增加 **HSE 失败则自动改用 HSI+PLL（仍为 180 MHz）**，请重新 **`make` 并烧录**再试。 |
| **晶振频率不是 12 MHz** | 若实际是 **8 MHz** 等，应改 `PLL_M` / `HSE_VALUE` 与硬件一致；否则会全时基错误，严重时也可能初始化异常。 |
| **LED 不在 PH10** | 若你用的不是当前 `user_init.c` 所接引脚，灯可能在别的 GPIO；用万用表或原理图核对，或临时在 `lightTask` 里改为自己板子的 LED 脚。 |
| **其它 `Error_Handler`** | 例如 **`HAL_CAN_GetRxMessage` 失败**、**`HAL_CAN_AddTxMessage` 失败** 等仍会进入 **`Error_Handler()`**；CAN 上 **非 `0x201…` 的无关标准帧已改为忽略**，不再因此复位死机。 |

排查建议：`gdb-multiarch` 连 OpenOCD，在 **`Error_Handler`**、`SystemClock_Config` 返回后、`USER_INIT` 处下断点，看卡在哪一步。

---

## 免责声明与致谢

应用层逻辑原作者为仓库内注释所示；使用前请结合你的硬件连接、电调固件版本与 RoboMaster 手册核对 CAN 细节，避免误控设备造成伤害。
