/**
 * @file user_defination.c
 * @author mzy (mzy8329@163.com)
 * @brief  定义了各线程运行频率及电机相关结构体
 * @version 0.1
 * 
 * @copyright Copyright (c) 2022
 * 
 */


#include "user_defination.h"



/**
 * @brief 初始化所有电机
 * 
 */
void MOTOR_INIT()
{
    for(int i = 0; i < USE_MOTOR_NUM; i++)
    {
        motor[i].id = i;
        motor[i].globalAngle.round = 0;
        motor[i].FdbData.msg_cnt = 0;
        motor[i].RefData.angle_ref = -1.f;
        motor[i].RefData.rpm_ref = -1.f;
        motor[i].RefData.current_ref = -1.f;

        motor[i].PID.angle_pid.Kp = 5.5;
        motor[i].PID.angle_pid.Ki = 0.35;
        motor[i].PID.angle_pid.Kd = 25;
        motor[i].PID.angle_pid.output = 0;
        motor[i].PID.angle_pid.outputMax = 15000;
        motor[i].PID.angle_pid.outputMin = -15000;
        motor[i].PID.angle_pid.err[0] = 0;
        motor[i].PID.angle_pid.err[1] = 0;
        
        motor[i].PID.rpm_pid.Kp = 2.5;
        motor[i].PID.rpm_pid.Ki = 0.08;
        motor[i].PID.rpm_pid.Kd = 1.0;
        motor[i].PID.rpm_pid.output = 0;
        motor[i].PID.rpm_pid.outputMax = 4000;
        motor[i].PID.rpm_pid.outputMin = -4000;        
        motor[i].PID.rpm_pid.err[0] = 0;
        motor[i].PID.rpm_pid.err[1] = 0;
    }
}


/**
 * @brief 进行PID计算，将输出结果保存到pid.output
 * 
 * @param pid {PID_s}
 * @param ref {float}
 * @param fdb {float}
 */
void PID_Cal(PID_s *pid, float ref, float fdb)
{
    float err_now = ref - fdb;
    pid->output += pid->Kp*(err_now - pid->err[1]) + pid->Ki*err_now + pid->Kd*(err_now - 2*pid->err[0] + pid->err[1]);
    if(pid->output > pid->outputMax)
    {
        pid->output = pid->outputMax;
    }
    if(pid->output < pid->outputMin)
    {
        pid->output = pid->outputMin;
    } 

    pid->err[1] = pid->err[0];
    pid->err[0] = err_now;
}


/**
 * @brief 将电机期望值转化为电流输出值
 * 
 */
void MotorCtrl()
{
    int i;
    for (i = 0; i < USE_MOTOR_NUM; i++)
    {
        /* 按串口下发的参考量自动选模式：位置 > 速度 > 电流；未使用字段填 -1 */
        if (motor[i].RefData.angle_ref != -1.f) {
            PID_Cal(&motor[i].PID.angle_pid, motor[i].RefData.angle_ref * 36.0f, motor[i].AxisData.axisAngleAll);
            PID_Cal(&motor[i].PID.rpm_pid, motor[i].PID.angle_pid.output, motor[i].AxisData.axisRpm);
            motor[i].current_out = motor[i].PID.rpm_pid.output;
        } else if (motor[i].RefData.rpm_ref != -1.f) {
            PID_Cal(&motor[i].PID.rpm_pid, motor[i].RefData.rpm_ref * 36.0f, motor[i].AxisData.axisRpm);
            motor[i].current_out = motor[i].PID.rpm_pid.output;
        } else if (motor[i].RefData.current_ref != -1.f) {
            float i_ref = motor[i].RefData.current_ref;
            if (i_ref > M2006_CURRENT_MAX) {
                motor[i].current_out = (float)M2006_CURRENT_MAX;
            } else if (i_ref < -(float)M2006_CURRENT_MAX) {
                motor[i].current_out = -(float)M2006_CURRENT_MAX;
            } else {
                motor[i].current_out = i_ref;
            }
        } else {
            motor[i].current_out = 0;
        }
    }
    /* CAN 帧固定四路 int16：未接入电机的通道强制 0，避免误控 */
    for (; i < 4; i++)
    {
        motor[i].current_out = 0;
    }
}

int MOTOR_IS_POS[USE_MOTOR_NUM] = {1, 1};
float MOTOR_MIN[USE_MOTOR_NUM] = {- 60 * 2, - 90 * 1.5};
float MOTOR_MAX[USE_MOTOR_NUM] = {60 * 2, 90 * 1.5};

DJI_Motor_s motor[4];