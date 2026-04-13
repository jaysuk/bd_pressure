/*
 * iousart.c — UART TX/RX for bd_pressure.
 *
 * TX (PA11, bit-bang): drives the USB/CH340 path. Same byte is also sent
 *   via USART2 hardware (PB6) in _fmt_emit() in main.c for the I2C connector.
 *   Delay uses a CPU cycle busy-wait — TIM1 free-running counter no longer needed.
 *
 * RX (PA12, bit-bang via EXTI + TIM3): receives bytes from USB/CH340.
 *   USART2 RX (PB7) also feeds rxData[] via HAL_UART_RxCpltCallback in main.c.
 */

#include "iousart.h"
#include "main.h"

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

extern uint8_t rxData[64];
extern int re_index;

static void iouart1_delayUs(volatile uint32_t nTime)
{
    uint16_t tmp = __HAL_TIM_GET_COUNTER(&htim1);
    if (tmp + nTime <= 65535)
        while ((__HAL_TIM_GET_COUNTER(&htim1) - tmp) < nTime);
    else {
        __HAL_TIM_SET_COUNTER(&htim1, 0);
        while (__HAL_TIM_GET_COUNTER(&htim1) < nTime);
    }
}

void iouart1_SendByte(uint8_t data)
{
    uint8_t i, tmp;

    /* start bit */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);
    iouart1_delayUs(IO_USART_SENDDELAY_TIME);

    /* 8 data bits, LSB first */
    for (i = 0; i < 8; i++) {
        tmp = (data >> i) & 0x01;
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11,
                          tmp ? GPIO_PIN_SET : GPIO_PIN_RESET);
        iouart1_delayUs(IO_USART_SENDDELAY_TIME);
    }

    /* stop bit */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET);
    iouart1_delayUs(IO_USART_SENDDELAY_TIME);
}

/* -----------------------------------------------------------------------
 * Bit-bang RX — EXTI falling edge on PA12 starts TIM3 sampling
 * ----------------------------------------------------------------------- */
static uint8_t recvData = 0;
static uint8_t recvStat = COM_STOP_BIT;

static uint8_t iouart1_RXD(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == RX_Pin) {
        if (iouart1_RXD() == 0) {
            if (recvStat == COM_STOP_BIT) {
                recvStat = COM_START_BIT;
                HAL_TIM_Base_Start_IT(&htim3);
            }
        }
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == htim3.Instance) {
        recvStat++;
        if (recvStat == COM_STOP_BIT) {
            HAL_TIM_Base_Stop_IT(&htim3);
            rxData[re_index++] = recvData;
            if (re_index >= (int)sizeof(rxData))
                re_index = 0;
            recvData = 0;
            recvStat = COM_STOP_BIT;
            return;
        }
        if (iouart1_RXD())
            recvData |= (1 << (recvStat - 1));
        else
            recvData &= ~(1 << (recvStat - 1));
    }
}
