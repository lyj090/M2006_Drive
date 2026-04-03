/**
 * @file can_serial.c
 * @author mzy (mzy8329@163.com)
 * @brief can通信线程，直接获取电机反馈信息以及控制电机
 * @version 0.1
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "can_serial.h"
#define ENCODER_MAX_VALUE 8191.0f
#define ENCODER_JUMP_THRESHOLD (ENCODER_MAX_VALUE / 2)

/**
 * @brief 一次性发送四个电机的控制电流
 * 
 * @param motor0_Iq {int16_t}
 * @param motor1_Iq {int16_t}
 * @param motor2_Iq {int16_t}
 * @param motor3_Iq {int16_t}
 */
void CanTransmitMotor0123(int16_t motor0_Iq, int16_t motor1_Iq, int16_t motor2_Iq, int16_t motor3_Iq)
{
    /* HAL 必须写入邮箱掩码；传 NULL 会对地址 0 解引用，导致 HardFault，表现为 CAN 完全无收发 */
    CAN_TxHeaderTypeDef TxMessage = {
        .StdId = 0x200,
        .ExtId = 0,
        .IDE = CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = 8,
        .TransmitGlobalTime = DISABLE,
    };
    uint32_t txMailbox = 0;
    uint8_t TxData[8] = {
        (uint8_t)(motor0_Iq >> 8), (uint8_t)motor0_Iq,
        (uint8_t)(motor1_Iq >> 8), (uint8_t)motor1_Iq,
        (uint8_t)(motor2_Iq >> 8), (uint8_t)motor2_Iq,
        (uint8_t)(motor3_Iq >> 8), (uint8_t)motor3_Iq,
    };

    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0) {
    }
    if (HAL_CAN_AddTxMessage(&hcan1, &TxMessage, TxData, &txMailbox) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief 从 CAN payload 解析转速、力矩、转子机械角（与 MotorCtrl 模式无关）
 * C610 反馈：角 0~8191 为 uint16（大端），转速/力矩为 int16（大端），与官方例程一致。
 */
static void Motor_ApplyCanPayload(DJI_Motor_s *Motor, const uint8_t *CanData)
{
    uint16_t enc = (uint16_t)(((uint16_t)CanData[0] << 8) | (uint16_t)CanData[1]);
    Motor->FdbData.angle = (float)enc;
    Motor->AxisData.axisRpm =
        (float)(int16_t)(((uint16_t)CanData[2] << 8) | (uint16_t)CanData[3]);
    Motor->FdbData.torque =
        (float)(int16_t)(((uint16_t)CanData[4] << 8) | (uint16_t)CanData[5]);
    Motor->FdbData.rpm = Motor->AxisData.axisRpm / 36.0f;
}

/**
 * @brief 多圈角度展开（依赖已写入的 FdbData.angle）
 * M2006 转子单圈刻度 8191，输出轴角度 = 转子累计角 / 36
 */
static void UpdataMotorAngleUnwrap(DJI_Motor_s *Motor)
{
    float delta = Motor->FdbData.angle - Motor->globalAngle.angleLast;
    if (delta > ENCODER_JUMP_THRESHOLD) {
        Motor->globalAngle.round--;
    } else if (delta < -ENCODER_JUMP_THRESHOLD) {
        Motor->globalAngle.round++;
    }
    Motor->AxisData.axisAngleAll =
        (Motor->FdbData.angle + (float)Motor->globalAngle.round * 8191.0f - Motor->globalAngle.angleOffset) / 8191.0f * 360.0f;
    Motor->globalAngle.angleAll = Motor->AxisData.axisAngleAll / 36.0f;
    Motor->globalAngle.angleLast = Motor->FdbData.angle;
}

/**
 * @brief 处理一路电调反馈（中断与轮询共用）
 */
static void Motor_OnEscFeedback(int index, const uint8_t *CanData)
{
    if (index < 0 || index >= USE_MOTOR_NUM) {
        return;
    }
    DJI_Motor_s *m = &motor[index];
    Motor_ApplyCanPayload(m, CanData);
    /* 首帧对齐 offset，之后每帧展开多圈；去掉“第 20 帧再对齐”以免周期跳变 */
    if (m->FdbData.msg_cnt == 0) {
        m->globalAngle.angleOffset = m->FdbData.angle;
        m->globalAngle.angleLast = m->FdbData.angle;
        m->globalAngle.round = 0;
        m->FdbData.msg_cnt = 1;
    }
    UpdataMotorAngleUnwrap(m);
}

/**
 * @brief 轮询 RX FIFO0（补充 NVIC 偶发丢中断、或高负载时积压）
 */
static void CAN_PollRxFifo0(void)
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t d[8];
    while (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0) {
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &RxHeader, d) != HAL_OK) {
            break;
        }
        Motor_OnEscFeedback((int)RxHeader.StdId - 0x201, d);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    (void)hcan;
    CAN_PollRxFifo0();
}


/**
 * @brief 初始化can通信
 * 
 */
void CAN_INIT()
{
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief can通信线程，以固定频率给电机发送控制电流
 * 
 * @param argument 
 */
void CanSerialTask(void const *argument)
{
    CAN_INIT();
    osDelay(200);

    for(;;)
    {
        /* 与 RX0 中断同逻辑，避免仅靠中断时丢帧导致角度/转速不刷新 */
        CAN_PollRxFifo0();
        MotorCtrl();
        //位置保护，输出保护
        for(int i = 0; i < USE_MOTOR_NUM; i++)
        {
            float pos = motor[i].globalAngle.angleAll;
            float ref = motor[i].RefData.angle_ref;
            if (MOTOR_IS_POS[i]) {
                // TODO：已经限制输入在范围内，ref不会超过范围，待修改此处位置限制输出
                if ((pos < MOTOR_MIN[i] && ref <= pos) || (pos > MOTOR_MAX[i] && ref >= pos)) {
                    // 继续远离限位，禁止输出
                    motor[i].current_out = 0;
                }
                // 否则允许输出，让电机回到安全区间
            }
        }
        
        CanTransmitMotor0123(motor[0].current_out, motor[1].current_out,  motor[2].current_out,  motor[3].current_out);
        // CanTransmitMotor0123(0, 0, 0, 0);

        osDelay(1000/(float)CAN_SERIAL_FREQUENCY);
    }

}

/**
 * @brief 注册can通信线程
 * 
 */
void CanSerialTaskStart(void)
{
    osThreadDef(CanSerial, CanSerialTask, osPriorityNormal, 0, 512);
	osThreadCreate(osThread(CanSerial), NULL);
}
