/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 pandapi3d.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>    /* memset, memcpy, strlen, strcpy */
#include <stdlib.h>    /* abs */


#include "stm32c0xx_hal.h"

#include "iousart.h"
#include "pa.h"
#include "ADS1220.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim14;
UART_HandleTypeDef huart1;   /* USART1 — PB6 TX / PB7 RX, AF0, configurable baud 8N1 (WAFER/I2C connector → printer board) */

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM14_Init(void);
static void MX_USART1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define USART_TXBUFF_SIZE   128                                 
  char usart_txBuff[USART_TXBUFF_SIZE];    



#define PA_OSR      7  // CLOCK_OSR_16384
#define ENDSTOP_OSR  2 //  2:CLOCK_OSR_512      1:CLOCK_OSR_256
unsigned char status_clk_old=0;
typedef struct 
{

	unsigned char version[16];// pandapi3d v1.0
	unsigned char measue_data[32]; //W:1.722;D:0310;Y:-005;R:0;
	unsigned char status_clk; // endstop mode(e;)  pa mode(l;) 
	unsigned char out_data_mode; // out raw data(d;), disable raw data output (D;)
	unsigned char THRHOLD_Z; // xx; E.g  12;
	unsigned char range;
	unsigned char set_normal; // 0:auto find normal_z ; 1:use current data as the normal_z ; 2: disable auto find normal_z
	unsigned char invert_data;
	unsigned char log_mode;  // 0: silent (default), 1: log trigger/open events
	// other variable

} Receive_D;

Receive_D  R_CMD;

#define RAW_DATE_LEN 256*2	

//unsigned char THRHOLD_Z=9;

 
// int out_data_mode = 0; 

 extern int r_index;
 extern int normal;

 int raw_dat[RAW_DATE_LEN];
 int r_index=0;
 int edge,read_pa=-1;
 unsigned char pa_result[128];
 unsigned char pa_list=0;

uint8_t rxData[64],tmp_r;   
int re_index=0,re_end=0;
int normal_tmp[5]={0,0,0,0,0},normal_z=0,z_cnt=0;
unsigned int tim14_n =0 ;
int end_z=0;
void find_normal_endstop(unsigned int *r_data,int length,char force);
void USART2_printf(char *fmt,...);
static void rrf_log(char *fmt,...);
void set_open(int log);
static void _u32_to_dec(char *buf, uint32_t v, uint8_t *len_out);
static void _fmt_emit(char c);
static void _fmt_emit_str(const char *s);

/* Baud rate table — index 0 is the factory default (115200).
 * Defined here so process_cmd() can reference it before the flash config block. */
static const uint32_t s_baud_table[] = { 115200, 57600, 38400, 230400 };
#define BAUD_TABLE_LEN    (sizeof(s_baud_table) / sizeof(s_baud_table[0]))
#define BAUD_IDX_DEFAULT  0u   /* 115200 */
static uint32_t s_baud_idx = BAUD_IDX_DEFAULT;

/* pa.lib references ram_i2c internally — I2C is removed but symbol must exist */
uint8_t *ram_i2c;

/* Stubs for HAL_UARTEx weak symbols — avoids needing stm32c0xx_hal_uart_ex.c */
void HAL_UARTEx_WakeupCallback(UART_HandleTypeDef *h)     { (void)h; }
void HAL_UARTEx_RxFifoFullCallback(UART_HandleTypeDef *h) { (void)h; }
void HAL_UARTEx_TxFifoEmptyCallback(UART_HandleTypeDef *h){ (void)h; }

/* USART1 RX callback — feeds received bytes into shared rxData[] buffer */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rxData[re_index++] = tmp_r;
        if (re_index >= (int)sizeof(rxData))
            re_index = 0;
        HAL_UART_Receive_IT(&huart1, &tmp_r, 1);
    }
}

/* Forward declarations for flash config helpers defined later in this file */
static void config_load(void);
static void config_save(void);

 unsigned char process_cmd(void)
 {
	 int j=0;
	 unsigned char cmd=0;
	 if(sizeof(rxData)<1)
		 return 1;
   for(j=1;j<sizeof(rxData);j++)
	 {
	    if(rxData[j]==';')
			{
			  cmd = rxData[j-1];

			  if(cmd=='x'){ // reserved — unused
				}
								else if(rxData[0]=='s' && rxData[1]=='c' && rxData[2]=='o' &&
				        rxData[3]=='r' && rxData[4]=='e'){ // score query
						/* 'score;' — return latest pa_result as a single raw byte.
						 * Value is 0-255 (unsigned char); lower = better PA fit.
						 * Returns 0x00 if no result is available yet (pa_list == 0).
						 *
						 * Sending a single binary byte lets RRF read it with
						 * M261.2 B1 — no ASCII parsing needed in the macro. */
						uint8_t score = (pa_list > 0) ? pa_result[pa_list - 1] : 0;
						iouart1_SendByte(score);
						HAL_UART_Transmit(&huart1, &score, 1, 10);
				}
				else if(cmd=='e'){ // in probe mode
						rrf_log("endstop mode");
						R_CMD.status_clk= ENDSTOP_OSR;
						find_normal_endstop((unsigned int *)raw_dat,r_index,1);

				}
				else if(cmd=='d'){ // out data
						R_CMD.out_data_mode=1;
				}
				else if(cmd=='D'){ // disable data out
						R_CMD.out_data_mode=0;
				}
				else if(cmd=='L'){ // enable trigger/open logging
						R_CMD.log_mode=1;
						rrf_log("bd_pressure: trigger logging enabled");
				}
				else if(cmd=='l'){ // disable trigger/open logging (default)
						R_CMD.log_mode=0;
						rrf_log("bd_pressure: trigger logging disabled");
				}
				else if(cmd=='v'){ // report firmware version
						rrf_log(FIRMWARE_VERSION);
				}
				
				else if(cmd=='c'){ // enter RRF-controlled PA sampling mode
						/* 'c;' arms the sensor for RRF-controlled PA calibration.
						 * The sensor switches to PA_OSR (high-res ADC), resets pa.lib
						 * state, and starts continuously sampling.  RRF controls all
						 * movement; after each line it sends 'score;' to retrieve the
						 * latest pa_result value as ASCII decimal + '\n'.
						 * Send 'e;' to return to endstop mode when done. */
						R_CMD.status_clk = PA_OSR;
						r_index  = 0;
						pa_list  = 0;
						memset(raw_dat,   0, RAW_DATE_LEN * sizeof(int));
						memset(pa_result, 0, 128);
						rrf_log("bd_pressure: PA sampling armed");
				}
				else if(cmd=='r'){ // reboot sensor
						rrf_log("bd_pressure: rebooting");
						HAL_Delay(10);
						NVIC_SystemReset();
				}
				else if(cmd=='b'){ // set baud rate by index
						/* 'b<n>;' — select baud rate by index into s_baud_table[]:
						 *   b0; = 115200 (default)
						 *   b1; = 57600
						 *   b2; = 38400
						 *   b3; = 230400
						 * Saves to flash then reboots. The RRF macro must send
						 * M575 with the new baud rate after the reboot delay. */
						uint32_t new_idx = (uint32_t)(rxData[j-2] - '0');
						if (new_idx < BAUD_TABLE_LEN) {
							s_baud_idx = new_idx;
							config_save();
							rrf_log("bd_pressure: baud rate set to %u — rebooting",
							        (uint32_t)s_baud_table[s_baud_idx]);
							HAL_Delay(200);
							NVIC_SystemReset();
						} else {
							rrf_log("bd_pressure: invalid baud index %u (0-%u valid)",
							        new_idx, (uint32_t)(BAUD_TABLE_LEN - 1));
						}
				}
				else if(cmd=='s'){ // status query
						char nbuf[12]; uint8_t nlen;
						_fmt_emit_str("M291 P\"<b>Mode:</b> ");
						_fmt_emit_str((R_CMD.status_clk == PA_OSR) ? "pa" : "endstop");
						_fmt_emit_str("<br><b>Trigger:</b> ");
						_u32_to_dec(nbuf, (uint32_t)R_CMD.THRHOLD_Z, &nlen);
						_fmt_emit_str(nbuf);
						_fmt_emit_str("<br><b>Invert:</b> ");
						_fmt_emit_str(R_CMD.invert_data ? "yes" : "no");
						_fmt_emit_str("<br><b>Version:</b> " FIRMWARE_VERSION);
						_fmt_emit_str("<br><b>Baud:</b> ");
						_u32_to_dec(nbuf, (uint32_t)s_baud_table[s_baud_idx], &nlen);
						_fmt_emit_str(nbuf);
						_fmt_emit_str("<br><b>Logging:</b> ");
						_fmt_emit_str(R_CMD.log_mode ? "on" : "off");
						_fmt_emit_str("<br><b>ADC output:</b> ");
						_fmt_emit_str(R_CMD.out_data_mode ? "on" : "off");
						_fmt_emit_str("<br><b>Baseline:</b> ");
						_u32_to_dec(nbuf, (uint32_t)normal_z, &nlen);
						_fmt_emit_str(nbuf);
						_fmt_emit_str("\" R\"bd_pressure status\" S2\n");
				}
				else if(cmd=='i'){ // invert the raw data
						R_CMD.invert_data=0;
				}
				else if(cmd=='I'){ //
						R_CMD.invert_data=1;
				}
				else if(cmd=='N'){ // use the current data
						R_CMD.set_normal=1;
					  set_open(0);
				}
				else if(cmd=='n'){ // auto find normal_z by default
						R_CMD.set_normal=0;
				}
				else if(cmd=='q'){ // query current threshold
						rrf_log("bd_pressure: threshold = %d", (int)R_CMD.THRHOLD_Z);
				}
				else if(cmd=='Q'){ // query threshold -- single raw binary byte for M261.2 B1 reads
						uint8_t thr = R_CMD.THRHOLD_Z;
						iouart1_SendByte(thr);
						HAL_UART_Transmit(&huart1, &thr, 1, 10);
				}
				else if(cmd=='z'){ // query baseline (normal_z)
						rrf_log("bd_pressure: baseline = %d", (int)normal_z);
				}
				else if((cmd>='0')&&(cmd<='9')){ // set threshold or baud rate
					 if(j>=2 && rxData[j-2]=='b'){
						 /* 'b<n>;' — set baud rate by index:
						  *   b0; = 115200 (default)
						  *   b1; = 57600
						  *   b2; = 38400
						  *   b3; = 230400
						  * Saves to flash then reboots. The RRF macro must send
						  * M575 with the new baud rate after the reboot delay. */
						 uint32_t new_idx = (uint32_t)(cmd - '0');
						 if (new_idx < BAUD_TABLE_LEN) {
							 s_baud_idx = new_idx;
							 config_save();
							 rrf_log("bd_pressure: baud %u — rebooting",
							         (uint32_t)s_baud_table[s_baud_idx]);
							 HAL_Delay(20);
							 NVIC_SystemReset();
						 } else {
							 rrf_log("bd_pressure: invalid baud index (0-%u valid)",
							         (uint32_t)(BAUD_TABLE_LEN - 1));
						 }
					 } else {
						 /* <n>; or <nn>; — set threshold (minimum 1) */
						 R_CMD.THRHOLD_Z=cmd-'0';
						 if(j>=2){
						     if(rxData[j-2]>'0'&&rxData[j-2]<='9'){
								    R_CMD.THRHOLD_Z+=(rxData[j-2]-'0')*10;
								 }
						 }
						 if(R_CMD.THRHOLD_Z < 1) R_CMD.THRHOLD_Z = 1;
						 rrf_log("bd_pressure: threshold set to %d", R_CMD.THRHOLD_Z);
						 config_save();   /* persist new threshold to flash */
					 }
				}
				else if(cmd!='x'){ // unknown command — skip reserved no-op
						rrf_log("bd_pressure: unknown command '%c'", cmd);
				}
				 memset(rxData,0,sizeof(rxData));
				 re_index=0;
				 break;
			}

	 }
	 return cmd;
 }

/* -----------------------------------------------------------------------
 * Minimal printf-style formatter — supports %d, %u, %f/%.Nf only.
 * Does NOT use vsprintf/printf from the ARM C library, which would pull
 * in _printf_s.o / libspace.o and overflow the 32 KB MDK-Lite limit.
 * ----------------------------------------------------------------------- */
static void _u32_to_dec(char *buf, uint32_t v, uint8_t *len_out)
{
    char tmp[12]; uint8_t i = 0, j = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; *len_out = 1; return; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    *len_out = i;
    while (i) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void _fmt_emit(char c)
{
    iouart1_SendByte((uint8_t)c);                        /* bit-bang PA11 (CH340E → USB) */
    HAL_UART_Transmit(&huart1, (uint8_t *)&c, 1, 10);   /* USART1 PB6 (WAFER/I2C connector → printer) */
}

static void _fmt_emit_str(const char *s)
{
    while (*s) _fmt_emit(*s++);
}


void USART2_printf(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt)
    {
        if (*fmt != '%') { _fmt_emit(*fmt++); continue; }
        fmt++; /* skip '%' */

        /* optional precision: .N */
        uint8_t prec = 6;
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = (uint8_t)(prec * 10 + (*fmt - '0')); fmt++; }
        }

        switch (*fmt++)
        {
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            char buf[12]; uint8_t l;
            if (v < 0) { _fmt_emit('-'); _u32_to_dec(buf, (uint32_t)(-v), &l); }
            else        {               _u32_to_dec(buf, (uint32_t)v,    &l); }
            _fmt_emit_str(buf);
            break;
        }
        case 'u': {
            char buf[12]; uint8_t l;
            _u32_to_dec(buf, va_arg(ap, uint32_t), &l);
            _fmt_emit_str(buf);
            break;
        }
        case 'f': {
            /* float args are promoted to double in va_arg */
            float fv = (float)va_arg(ap, double);
            if (fv < 0.0f) { _fmt_emit('-'); fv = -fv; }
            uint32_t ipart = (uint32_t)fv;
            /* scale fractional part by 10^prec */
            float frac = fv - (float)ipart;
            uint8_t p = prec; uint32_t scale = 1;
            while (p--) scale *= 10;
            uint32_t frac_i = (uint32_t)(frac * (float)scale + 0.5f);
            /* carry if rounded up */
            if (frac_i >= scale) { ipart++; frac_i = 0; }
            char buf[12]; uint8_t l;
            _u32_to_dec(buf, ipart, &l); _fmt_emit_str(buf);
            if (prec) {
                _fmt_emit('.');
                /* zero-pad fractional part */
                uint32_t tmp_scale = scale / 10;
                while (tmp_scale > frac_i && tmp_scale > 1) { _fmt_emit('0'); tmp_scale /= 10; }
                _u32_to_dec(buf, frac_i, &l); _fmt_emit_str(buf);
            }
            break;
        }
        case 'c':
            _fmt_emit((char)va_arg(ap, int));
            break;
        case '%':
            _fmt_emit('%');
            break;
        default:
            break;
        }
    }
    va_end(ap);
}

static void rrf_log(char *fmt, ...)
{
    va_list ap;
    _fmt_emit_str("M118 P0 S\"");
    va_start(ap, fmt);
    while (*fmt)
    {
        if (*fmt != '%') { _fmt_emit(*fmt++); continue; }
        fmt++;
        uint8_t prec = 6;
        if (*fmt == '.') {
            fmt++; prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = (uint8_t)(prec * 10 + (*fmt - '0')); fmt++; }
        }
        switch (*fmt++)
        {
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            char buf[12]; uint8_t l;
            if (v < 0) { _fmt_emit('-'); _u32_to_dec(buf, (uint32_t)(-v), &l); }
            else        {               _u32_to_dec(buf, (uint32_t)v,    &l); }
            _fmt_emit_str(buf);
            break;
        }
        case 'u': {
            char buf[12]; uint8_t l;
            _u32_to_dec(buf, va_arg(ap, uint32_t), &l);
            _fmt_emit_str(buf);
            break;
        }
        case 'c':
            _fmt_emit((char)va_arg(ap, int));
            break;
        case '%':
            _fmt_emit('%');
            break;
        default:
            break;
        }
    }
    va_end(ap);
    _fmt_emit_str("\"\n");
}


/*
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)	// 2ms
{
	if (htim->Instance == htim14.Instance)
	{
		//USART2_printf("HAL_TIM_PeriodElapsedCallback \n" );
		tim14_n++;		
	}
}
*/
uint8_t offset;


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        if( HAL_UART_Receive_IT(&huart2, &tmp_r, 1)==HAL_OK ){
					rxData[re_index++]=tmp_r;
					if(re_index>=sizeof(rxData))
						re_index = 0;
					  
			 }
				
				//rxData[re_index]=0;
    }
}
*/

int get_ix(int i){
  if (i<0)
		i = RAW_DATE_LEN + i;
	return i;
}


void set_trigered(int log)
{
	end_z=1;
	//tim14_n=0;
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_8,GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_13,GPIO_PIN_SET);
	if(log==0 || R_CMD.log_mode==0)
    return;
	rrf_log("bd_pressure: triggered normal=%d thr=%d", normal_z, R_CMD.THRHOLD_Z);
}

void set_open(int log)
{
	end_z=0;
	//tim14_n=0;
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_8,GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_13,GPIO_PIN_RESET);
	if(log==0 || R_CMD.log_mode==0)
    return;
	rrf_log("bd_pressure: probe open val=%d", raw_dat[r_index-1]);

}

///////////////////////////

void find_normal_endstop(unsigned int *r_data,int length,char force)
{	

    int s_avt = 0,i,min=0,max=0;
	/*  if (normal_z==0&&length>=4)
		{
			normal_z = r_data[length-1]+r_data[length-2]+r_data[length-3]+r_data[length-4];
			normal_z = normal_z/4;
			USART2_printf("n:%d\n",s_avt);
			return;
		}*/
	 // USART2_printf("find_normal_endstop\n");
		if(length<=31)
			return;
   
	  
   // for i in range(length-1,length-1-20,-1):
		min=r_data[get_ix(length-1)];
		max=r_data[get_ix(length-1)];
		for(i=length-1;i>length-1-30;i--)	
		{
			 if(min>r_data[get_ix(i)])
				 min=r_data[get_ix(i)];
			 if(max<r_data[get_ix(i)])
				 max=r_data[get_ix(i)];
		}
		//for(i=length-1;i>length-1-20;i--)	
		//{
		//   if (abs(r_data[get_ix(i)] - s_avt) >= R_CMD.THRHOLD_Z)
    //         return;
		//}
		R_CMD.range=max-min;
		
		//if(R_CMD.range>R_CMD.THRHOLD_Z)
		// avoid the vibration
		if((R_CMD.range>R_CMD.THRHOLD_Z || R_CMD.range>20)&&force==0)
			return;
     //   if (abs(r_data[get_ix(i)] - s_avt) >= 5)
    // for i in range(length-1,length-1-20,-1):
		for(i=length-1;i>length-1-30;i--)	
        s_avt = s_avt+r_data[get_ix(i)];
    s_avt = s_avt/30;
		if(normal_z!= s_avt)
			rrf_log("bd_pressure: normal_z updated to %d", s_avt);
		normal_z= s_avt;
		tim14_n=0;
		 
}	
#define AVT_CN 4
int process_triggered(void)
{ 
    int s_avt = 0,i,tmp_cn=0,vibration;
   // if(r_index<20)
	//		return 0;
		if (normal_z==0)
		    return 0;
		if (r_index>=RAW_DATE_LEN){
				  r_index=0;
			//	  memset(raw_dat,0,sizeof(raw_dat));	
		}
		tmp_cn=2500;//5000-end_z*4500; // 10s  1s
		
		if((tim14_n%tmp_cn)==(tmp_cn-1))
		
		{			 
			 tim14_n=0;
			 find_normal_endstop((unsigned int *)raw_dat,r_index,0);
		}
	// raw_dat[r_index++] normal_z)>=THRHOLD_Z 
   // for i in range(length-1,length-1-20,-1):
		//// check the average
		
		for(i=r_index-1;i>r_index-1-AVT_CN;i--)	
        s_avt = s_avt+raw_dat[get_ix(i)];
    s_avt = s_avt/AVT_CN;

		///// filter the vibration
		vibration =1;
	/*	for(i=r_index-1;i>r_index-1-AVT_CN/4;i--)	
		{
			// if(abs(raw_dat[get_ix(i)]-normal_z)<=(R_CMD.THRHOLD_Z*2/3))
			 if(raw_dat[get_ix(i)]<normal_z)
			 {
				  if(raw_dat[get_ix(i)]>raw_dat[get_ix(i-1)])
					{
						 vibration =0;
						 set_open(0);	
						 return 0;
					}
			 }
			 else if(raw_dat[get_ix(i)]>normal_z)
			 {
				  if(raw_dat[get_ix(i)]<raw_dat[get_ix(i-1)])
					{
						 vibration =0;
						 set_open(0);	
						 return 0;
					}
			 }
		
		}*/
		///
		 
	//	s_avt=raw_dat[get_ix(i)];
		if(abs(s_avt-normal_z)>=R_CMD.THRHOLD_Z
			   &&end_z==0 ){
			set_trigered(0);
		  return 1;
		}
	//	else if ((normal_z-s_avt)>=4&&end_z==0 ){
	//	   set_trigered(1);
	//	   return 1;
	//	}
		else if(abs(s_avt-normal_z)<=(R_CMD.THRHOLD_Z/2)&&end_z==1){
			set_open(0);
			return 0;
		}
	//	else if((s_avt-normal_z)<=-R_CMD.THRHOLD_Z/2){
	//		find_normal_endstop(raw_dat,r_index);
	//	}
	return 0;
}


void Pressure_advance(void)
{
	if (r_index>(3*SAMPLES+1)){
		 if (r_index >= RAW_DATE_LEN){
				r_index = 0;
			//	for r_index in range(0,2*samples,1):
				for(r_index=0;r_index<2*SAMPLES;r_index++) 	
						raw_dat[r_index] = raw_dat[RAW_DATE_LEN-2*SAMPLES-1+r_index];
				r_index = 2*SAMPLES;
				return;
		 }
		 find_normal((unsigned int *)raw_dat,r_index);
		 edge =has_plus((unsigned int *)raw_dat,r_index-1*SAMPLES-5);
		 if (edge > 2*SAMPLES){                        
			 // USART2_printf("edge:%d\n",edge) ; 
				if (edge > 0){
					//#  print(raw_dat)
						int ret = get_low_value((unsigned int *)raw_dat,edge);
						if(ret>0){
							pa_result[pa_list] = ret;
						//	USART2_printf("Result:%d\n",pa_result[pa_list]) ; 
							pa_list++;
							r_index = 0;
							memset(raw_dat,0,sizeof(raw_dat));
						}
				}
			}
	} 

}

/* -----------------------------------------------------------------------
 * User config flash storage
 *
 * Uses the last 2KB page of flash (0x08007800) to persist user settings
 * across power cycles.
 *
 * Layout (64-bit double-word aligned as required by STM32C0 HAL):
 *   [0x08007800] uint32_t magic     (0xBD1235BD — incremented from v1 to
 *                                    invalidate old single-baud layouts)
 *   [0x08007804] uint32_t thrhold   (THRHOLD_Z value, 0–99)
 *   [0x08007808] uint32_t baud_idx  (index into s_baud_table[], default 0)
 *   [0x0800780C] uint32_t reserved  (0 — padding for future use)
 *
 * The page is erased and rewritten each time any setting is updated.
 * STM32C011 flash endurance: 10,000 erase cycles — more than sufficient.
 * --------------------------------------------------------------------- */
#define CFG_FLASH_PAGE_ADDR  0x08007800UL
#define CFG_FLASH_PAGE_NUM   15            /* last page of 32KB flash (16 × 2KB pages, 0-indexed) */
#define CFG_MAGIC            0xBD1235BDUL  /* incremented from 0xBD1234BD to invalidate old layouts */

/* Baud rate table and s_baud_idx are defined near the top of the file,
 * before process_cmd(), so they are visible throughout. */

static void config_load(void)
{
    uint32_t magic    = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR));
    uint32_t thrhold  = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR + 4));
    uint32_t baud_idx = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR + 8));

    if (magic == CFG_MAGIC) {
        if (thrhold >= 1 && thrhold <= 99)
            R_CMD.THRHOLD_Z = (unsigned char)thrhold;
        if (baud_idx < BAUD_TABLE_LEN)
            s_baud_idx = baud_idx;
    }
    /* else: old or blank flash — firmware defaults remain */
}

static void config_save(void)
{
    /* Wear check: skip erase/write if values already match flash */
    uint32_t magic_now    = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR));
    uint32_t thrhold_now  = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR + 4));
    uint32_t baud_idx_now = *((volatile uint32_t *)(CFG_FLASH_PAGE_ADDR + 8));
    if (magic_now    == CFG_MAGIC &&
        thrhold_now  == (uint32_t)R_CMD.THRHOLD_Z &&
        baud_idx_now == s_baud_idx)
        return;

    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Page      = CFG_FLASH_PAGE_NUM;
    erase.NbPages   = 1;
    HAL_FLASHEx_Erase(&erase, &page_error);

    /* First double-word: magic | thrhold */
    uint64_t dw0 = ((uint64_t)(uint32_t)R_CMD.THRHOLD_Z << 32) | CFG_MAGIC;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CFG_FLASH_PAGE_ADDR, dw0);

    /* Second double-word: baud_idx | reserved(0) */
    uint64_t dw1 = (uint64_t)s_baud_idx;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CFG_FLASH_PAGE_ADDR + 8, dw1);

    HAL_FLASH_Lock();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  unsigned int cn_t=0,i=0;
	unsigned char cmd=0;
	int adctmp;
	int tempA;
	
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_TIM14_Init();
  /* Read baud index from flash before USART1 init.
   * config_load() is called again after the R_CMD memset below to restore
   * all other settings — s_baud_idx is a module-level static so it survives. */
  config_load();
  MX_USART1_Init();                /* uses s_baud_table[s_baud_idx] */
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim1);
  HAL_TIM_Base_Start_IT(&htim14);
	re_index=0;
	re_end=0;
	offset=0;
	memset(&R_CMD.version[0],0,sizeof(R_CMD));

	ram_i2c = &R_CMD.version[0];
	strcpy((char *)R_CMD.version,"pandapi3dv1\n");
	R_CMD.THRHOLD_Z=4;          /* default — may be overridden by config_load() */
	config_load();              /* restore thrhold and baud_idx from flash */
	R_CMD.status_clk=ENDSTOP_OSR;

	normal_z = 0;
	end_z = 0;

	/* Arm USART1 RX (PB7, printer board) */
	if (HAL_UART_Receive_IT(&huart1, &tmp_r, 1) != HAL_OK)
		rrf_log("bd_pressure: USART1 RX arm failed");

	rrf_log("bd_pressure: " FIRMWARE_VERSION " baud:%u", (uint32_t)s_baud_table[s_baud_idx]);

	//40Hz=0X14 90Hz=0x34 2K=0XD4
	ADS1220_Init(4,0xD4);
	R_CMD.invert_data=1;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

		/* ------------------------------------------------------------------
		 * Normal operation (endstop or legacy Klipper PA mode)
		 * ---------------------------------------------------------------- */
		  tempA = GetAD(4,1);//get ADC data from channel 0
		  if(PolarFlag==R_CMD.invert_data)
			 tempA=-tempA;
		  tempA=tempA/1000  + 6000;  // noise filter and make sure the data is alway positive
		  raw_dat[r_index++]=tempA;  // store the ADC data into buffer
		  if(R_CMD.out_data_mode==1) // output the ADC data via uart/usb port
			{
				USART2_printf("%d;\n",tempA);
			}
			if(R_CMD.status_clk==PA_OSR)
				Pressure_advance();
			else
				process_triggered();
		  process_cmd();
			if(R_CMD.set_normal==1||normal_z==0)
			{
			   R_CMD.set_normal=2;
				 find_normal_endstop((unsigned int *)raw_dat,r_index,1);
				 set_open(0);
			}
			if(status_clk_old!=R_CMD.status_clk)
			{
						status_clk_old=R_CMD.status_clk;
						if(R_CMD.status_clk==PA_OSR){
							ADS1220_Init(4,0x34);
							rrf_log("PA mode");
						}
						else{
							ADS1220_Init(4,0xB4); //// 40Hz=0X14 90Hz=0x34 1.2K=0XB4 2K=0XD4
							rrf_log("endstop mode");
						}
			}
			
			
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}


/**
  * @brief TIM14 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 48-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535-1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK) { Error_Handler(); }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM3_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 48-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = IO_USART_SENDDELAY_TIME;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK) { Error_Handler(); }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM14_Init(void)
{

  /* USER CODE BEGIN TIM14_Init 0 */

  /* USER CODE END TIM14_Init 0 */

  /* USER CODE BEGIN TIM14_Init 1 */

  /* USER CODE END TIM14_Init 1 */
  htim14.Instance = TIM14;
  htim14.Init.Prescaler = 1;
  htim14.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim14.Init.Period = 48000;
  htim14.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM14_Init 2 */

  /* USER CODE END TIM14_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_11|GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA2 PA4 PA5 PA7
                           PA8 PA11 PA13 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_11|GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA3 PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : RX_Pin (PA12) — bit-bang UART RX from USB/CH340 */
  GPIO_InitStruct.Pin = RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(RX_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init */
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief USART1 Initialization — configurable baud rate, 8N1 on PB6 (TX, AF0) / PB7 (RX, AF0).
  * PB6/PB7 are direct physical pads on UFQFPN20 (pins 18/19) → WAFER/I2C connector → printer board.
  * Baud rate is read from s_baud_table[s_baud_idx] — call config_load() before this function.
  */
static void MX_USART1_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = s_baud_table[s_baud_idx];
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Blink PA8 at ~10 Hz so a failed init is visible on the endstop LED */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  {
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8; g.Mode = GPIO_MODE_OUTPUT_PP; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
  }
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_8);
    HAL_Delay(100);
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
