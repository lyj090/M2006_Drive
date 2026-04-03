# M2006_Drive — 配合 OpenOCD 排查启动/灯不闪
#
# 终端 A（保持运行），在工程根目录任选其一:
#   make openocd
#   openocd -s $PWD/openocd -s /usr/share/openocd/scripts -f openocd/m2006_drive.cfg
#
# 终端 B:
#   cd /path/to/M2006_Drive
#   gdb-multiarch -x gdb_m2006.gdb build/M2006_Drive.elf
#
# 进入 (gdb) 后先确保 Flash 与 ELF 一致（可选）:
#   load
#   monitor reset halt
# 然后:
#   run_debug
#
# 或直接多次输入 c（continue），观察最先停在哪个断点。

set pagination off

target extended-remote localhost:3333
monitor reset halt

# STM32F4 硬件断点数量有限，默认只下 3 个高价值断点
break Error_Handler
break HardFault_Handler
break USER_INIT

# 需要时手工追加（每次最多再加 1~2 个）：
# break main
# break SystemClock_Config
# break MX_FREERTOS_Init
# break lightTask

define run_debug
  printf "\n=== 已连接 OpenOCD，已复位暂停。即将全速运行到下一断点 ===\n"
  printf "若停在 Error_Handler ：时钟/外设失败，执行 bt 看栈。\n"
  printf "若停在 HardFault_*    ：异常，执行 bt 与 info registers。\n"
  printf "若停在 main          ：可 n (next) 单步，看是否从 SystemClock 返回。\n"
  printf "若停在 USER_INIT     ：说明 main 与外设初始化大致通过。\n"
  printf "若停在 lightTask     ：闪灯线程已调度到，用 n/finish 观察。\n\n"
  continue
end

printf "\nGDB 已连接。建议先 load 再 monitor reset halt，然后执行: run_debug\n\n"
