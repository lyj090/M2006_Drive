/**
 * @file uart_serial.c
 * @author mzy (mzy8329@163.com)
 * @supporter lyj（1774526989@qq.com）
 * @brief 进行串口消息的收发，传输电机的控制量和反馈量
 * @version 0.1
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "uart_serial.h"
#include "main.h"

// 通信包各个部分长度定义
#define HEAD_LENGTH 2
#define DATA_NUM_LENGTH 1
#define MOTOR_DATA_LENGTH 13
#define SLIDE_INIT_LENGTH 1 //上行数据有，下行数据无
#define CRC_LENGTH 1
// crc校验不包括数据头
#define SEND_BAG_LENGTH (HEAD_LENGTH + DATA_NUM_LENGTH + MOTOR_DATA_LENGTH * USE_MOTOR_NUM + SLIDE_INIT_LENGTH + CRC_LENGTH)
#define RECV_BAG_LENGTH (HEAD_LENGTH + DATA_NUM_LENGTH + MOTOR_DATA_LENGTH * USE_MOTOR_NUM + CRC_LENGTH)

uint8_t HEADER[2] = {0x44, 0x22};
uint8_t RxBuffer[1] = {0};
#define SLIDE_INIT_SIGNAL_LENGTH 3
uint8_t slide_init_signal[SLIDE_INIT_SIGNAL_LENGTH] = {0};
// 环形缓冲区定义
#define PIPE_SIZE 64
typedef struct __attribute__((packed))
{
    uint8_t buffer[PIPE_SIZE];
    uint8_t write_idx;
    uint8_t read_idx;
}DataPipe_t;

DataPipe_t data_pipe;

/**
 * @brief 单个电机数据结构
 */
typedef struct __attribute__((packed))
{
    uint8_t id;
    float angle_fdb;
    float rpm_fdb;
    float torque_fdb;
} MotorData_t;

/**
 * @brief 全数据反馈包，发送给上位机
 * 
 */
typedef union
{
    uint8_t data[SEND_BAG_LENGTH];
    struct 
    {
        uint8_t header[HEAD_LENGTH];
        uint8_t data_num;
        union
        {
            uint8_t motor_data[MOTOR_DATA_LENGTH * USE_MOTOR_NUM];
            struct
            {
                MotorData_t motor_data_struct[USE_MOTOR_NUM];
            }__attribute__((packed));
        };
        uint8_t slide_init;
        uint8_t crc;
    }__attribute__((packed));
}__attribute__((packed)) SEND_Bag_u;

/**
 * @brief 将所有数据发送给上位机
 * 
 * @param Motor
 */
void UartTransmitAll(DJI_Motor_s *Motor)
{
    SEND_Bag_u UartBag;

    UartBag.header[0] = HEADER[0];
    UartBag.header[1] = HEADER[1];
    UartBag.data_num = MOTOR_DATA_LENGTH * USE_MOTOR_NUM + SLIDE_INIT_LENGTH + CRC_LENGTH;

    /* 关中断快照，避免与 CAN 接收回调并发读写 float 撕裂 */
    for (int i = 0; i < USE_MOTOR_NUM; ++i) {
        float ang, rpm, tq;
        uint8_t mid;
        __disable_irq();
        mid = Motor[i].id;
        ang = Motor[i].globalAngle.angleAll;
        rpm = Motor[i].FdbData.rpm;
        tq = Motor[i].FdbData.torque;
        __enable_irq();
        UartBag.motor_data_struct[i].id = mid;
        UartBag.motor_data_struct[i].angle_fdb = ang;
        UartBag.motor_data_struct[i].rpm_fdb = rpm;
        UartBag.motor_data_struct[i].torque_fdb = tq;
    }

    UartBag.slide_init = slide_init_signal[0];

    UartBag.crc = 0;
    for(int i = 2; i < 2 + UartBag.data_num-1; ++i)
    {
        UartBag.crc += UartBag.data[i];
    }
    UartBag.crc += UartBag.slide_init;

    HAL_UART_Transmit(&huart1, (uint8_t *)&UartBag, SEND_BAG_LENGTH, 10);
}

void UartTransmitDEBUG(DJI_Motor_s *Motor,float num)
{
    SEND_Bag_u UartBag;

    UartBag.header[0] = HEADER[0];
    UartBag.header[1] = HEADER[1];
    UartBag.data_num = MOTOR_DATA_LENGTH * USE_MOTOR_NUM + SLIDE_INIT_LENGTH + CRC_LENGTH;

    for(int i = 0; i < USE_MOTOR_NUM; ++i)
    {
        UartBag.motor_data_struct[i].id = Motor[i].id;
        if(num)
        {
            UartBag.motor_data_struct[i].angle_fdb = num;
        }
        else
        {
            UartBag.motor_data_struct[i].angle_fdb = Motor[i].RefData.angle_ref;
        }
        UartBag.motor_data_struct[i].rpm_fdb = Motor[i].RefData.rpm_ref;
        UartBag.motor_data_struct[i].torque_fdb = Motor[i].RefData.current_ref;
    }

    UartBag.slide_init = slide_init_signal[0];
    UartBag.crc = 0;
    for(int i = 2; i < 2 + UartBag.data_num-1; ++i)
    {
        UartBag.crc += UartBag.data[i];
    }
    UartBag.crc += UartBag.slide_init;
    HAL_UART_Transmit(&huart1, (uint8_t *)&UartBag, SEND_BAG_LENGTH, 10);
}

/**
 * @brief 数据管道处理函数
 */
// 计算环形缓冲区可用数据长度
static int DataPipeAvailable(DataPipe_t *data_pipe) {
    return (data_pipe->write_idx >= data_pipe->read_idx) ? (data_pipe->write_idx - data_pipe->read_idx) : (PIPE_SIZE - data_pipe->read_idx + data_pipe->write_idx);
}

/**
 * @brief 数据管道处理函数
 */
// 计算环形缓冲区可用数据长度
void DataPipePop(DataPipe_t *data_pipe,uint8_t pop_step) {
    for (int i = 0; i < pop_step; ++i) {
        data_pipe->buffer[data_pipe->read_idx] = 0;
        data_pipe->read_idx = (data_pipe->read_idx + 1) % PIPE_SIZE;
    }
}

void DataPipeProcess()
{
    while (DataPipeAvailable(&data_pipe) >= RECV_BAG_LENGTH) {
        // 查找包头
        if (data_pipe.buffer[data_pipe.read_idx] == HEADER[0] && data_pipe.buffer[(data_pipe.read_idx+1)%PIPE_SIZE] == HEADER[1]) {
            uint16_t idx_data_num = (data_pipe.read_idx + 2) % PIPE_SIZE;
            uint8_t data_num = data_pipe.buffer[idx_data_num];
            uint8_t motor_data_num = data_num - CRC_LENGTH;
            if (motor_data_num < MOTOR_DATA_LENGTH || motor_data_num % MOTOR_DATA_LENGTH != 0) {
                DataPipePop(&data_pipe,1);
                continue;
            }
            int total_packet_len = HEAD_LENGTH + DATA_NUM_LENGTH + data_num;
            if (DataPipeAvailable(&data_pipe) < total_packet_len - 1) {
                // 数据不够，等待更多
                break;
            }
            // 计算CRC
            uint8_t crc = 0;
            for (int i = 0; i < data_num; ++i) {
                crc += data_pipe.buffer[(data_pipe.read_idx + 2 + i) % PIPE_SIZE];
            }
            // 校验CRC
            uint8_t crc_recv = data_pipe.buffer[(data_pipe.read_idx + 2 + data_num) % PIPE_SIZE];
            if (crc == crc_recv) {
                // 处理电机数据
                int motor_count = (data_num - CRC_LENGTH) / MOTOR_DATA_LENGTH;
                for (int k = 0; k < motor_count; ++k) {
                    int base = (data_pipe.read_idx + HEAD_LENGTH + DATA_NUM_LENGTH + k * MOTOR_DATA_LENGTH) % PIPE_SIZE;
                    // 复制出一份连续的电机数据
                    uint8_t temp[MOTOR_DATA_LENGTH];
                    for (int m = 0; m < MOTOR_DATA_LENGTH; ++m) {
                        temp[m] = data_pipe.buffer[(base + m) % PIPE_SIZE];
                    }
                    MotorData_t *motorData = (MotorData_t *)temp;
                    if (motorData->id < USE_MOTOR_NUM) {
                        /* 仅当使用位置参考（≠-1）时校验软限位；速度/电流模式角度槽应发 -1，避免误拒收 */
                        if (motorData->angle_fdb != -1.f &&
                            (motorData->angle_fdb > MOTOR_MAX[motorData->id] ||
                             motorData->angle_fdb < MOTOR_MIN[motorData->id])) {
                            continue;
                        }
                        motor[motorData->id].RefData.angle_ref = motorData->angle_fdb;
                        motor[motorData->id].RefData.rpm_ref = motorData->rpm_fdb;
                        motor[motorData->id].RefData.current_ref = motorData->torque_fdb;
                    }
                }
                /* 反馈由 SerialTask 固定周期上送，避免在中断路径里 HAL_UART_Transmit 阻塞 */
                // 跳过已处理的数据包
                DataPipePop(&data_pipe,total_packet_len);
                // 清空串口缓存区

            } else {
                // CRC错误，丢弃一个字节
                DataPipePop(&data_pipe,1);
            }
        } else {
            // 不是包头，丢弃一个字节
            DataPipePop(&data_pipe,1);
        }
    }
}

/**
 * @brief uart的Callback函数
 * 
 * @param huart 
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* 与上位机协议：USART1（引脚见 Core/Src/usart.c / .ioc） */
    if(huart == &huart1)
    {
        // 将接收到的数据写入环形缓冲区
        for (int i = 0; i < sizeof(RxBuffer); ++i)
        {
            data_pipe.buffer[data_pipe.write_idx] = RxBuffer[i];
            data_pipe.write_idx = (data_pipe.write_idx + 1) % PIPE_SIZE;
        }
        // 处理pipe数据并反馈当前位置信息
        DataPipeProcess();
        HAL_UART_Receive_IT(&huart1, RxBuffer, sizeof(RxBuffer));
    }
}

/**
 * @brief uart 初始化，开启 USART1 中断接收
 * 
 */
void UART_INIT()
{
    HAL_UART_Receive_IT(&huart1, RxBuffer, sizeof(RxBuffer));
    HAL_UART_Transmit(&huart1, (uint8_t *)"UART_INIT", sizeof("UART_INIT"),1000);
}

/**
 * @brief uart线程，以固定频率发送电机的反馈信号
 * 
 */
void SerialTask()
{
    UART_INIT();

    for (;;) {
        /* 滑块/限位历史，供上行包 slide_init 使用 */
        for (int i = SLIDE_INIT_SIGNAL_LENGTH - 1; i > 0; --i) {
            slide_init_signal[i] = slide_init_signal[i - 1];
        }
        uint8_t limit_reached = GPIO_PIN_SET - HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);
        slide_init_signal[0] = limit_reached;
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_12, limit_reached);

        /* 固定频率上送：位置/转速/力矩均来自 CAN 反馈，与控制模式无关 */
        UartTransmitAll(motor);

        osDelay(1000 / (float)UART_SERIAL_FREQUENCY);
    }
}

/**
 * @brief 开启uart线程
 * 
 */
void SerialTaskStart(void)
{
    osThreadDef(Serial, SerialTask, osPriorityNormal, 0, 512);
	osThreadCreate(osThread(Serial), NULL);
}
