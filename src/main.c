/*****************************************************************************
 *   Peripherals such as temperature sensor, light sensor
 *   and trim potentiometer are monitored and values are
 *   displayed as graphics to the OLED display.
 *
 *   Copyright(C) 2010, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"

#include "light.h"
#include "oled.h"
#include "temp.h"
#include "acc.h"
#include "led7seg.h"


static uint32_t msTicks = 0;
static uint8_t buf[10];

int32_t temperatures[13];
uint8_t btn1 = 0; // SW3
uint8_t btn2 = 0; // SW4
int mode = 1; // 1 - real-time, 2 - save, 3 - show saved
int measurement_option = 1; // 1 - temperature, 2 - light, 3 - potentiometer

void SysTick_Handler(void) {
    msTicks++;
}

static uint32_t getTicks(void)
{
    return msTicks;
}

static void init_ssp(void)
{
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void)
{
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void)
{
	PINSEL_CFG_Type PinCfg;

	/*
	 * Init ADC pin connect
	 * AD0.0 on P0.23
	 */
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 23;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);

	/* Configuration for ADC :
	 * 	Frequency at 0.2Mhz
	 *  ADC channel 0, no Interrupt
	 */
	ADC_Init(LPC_ADC, 200000);
	ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
	ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);

}

void display_working_modes(void) {
	oled_clearScreen(OLED_COLOR_WHITE);
	oled_putString(12, 1, "=== MENU ===", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(20, 19, "Real time", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(20, 34, "Save", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(20, 49, "Show saved", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

void display_measurement_options(void) {
	oled_clearScreen(OLED_COLOR_WHITE);
	oled_putString(5, 1, "=== SELECT ===", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(15, 19, "Temperature", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(15, 34, "Light", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_putString(15, 49, "Potentiometer", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

void draw_graph_real_time(int32_t values[13], int n, char* measurements) {
	oled_clearScreen(OLED_COLOR_WHITE);
	oled_putString(12, 1, measurements, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	for(int j = n-1, k = 96; j > 0; j--, k-=8)
		oled_line(k-8, 64 - values[j-1], k, 64 - values[j], OLED_COLOR_BLACK);
}

uint32_t normalize_temperature(uint32_t val) {
	double new_val = (double)val;
	new_val = new_val / 10;
	new_val = (((new_val - 15) / 20) * 43) + 10;
	return (uint32_t)new_val;
}

void measure_temperature(void) {
	int32_t t = 0;
	int i = 0;
	while(1) {
	    /* Temperature */
		if(i >= 13) {
			for(int j = 0; j < 12; j++)
				temperatures[j] = temperatures[j+1];
			i--;
		}
		btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
		if(btn1 == 0)
			break;
	    t = temp_read();
	    temperatures[i] = normalize_temperature(t);
	    if(i < 13)
	    	i++;
	    draw_graph_real_time(temperatures, i, "Temperature");

	    /* light */
	    // lux = light_read();

	    /* trimpot */
		// ADC_StartCmd(LPC_ADC,ADC_START_NOW);
		//Wait conversion complete
		// while (!(ADC_ChannelGetStatus(LPC_ADC,ADC_CHANNEL_0,ADC_DATA_DONE)));
		// trim = ADC_ChannelGetData(LPC_ADC,ADC_CHANNEL_0);

	    /* output values to OLED display */

	    // intToString(t, buf, 10, 10);
	    // oled_fillRect((1+9*6),1, 80, 8, OLED_COLOR_WHITE);
	    // oled_putString((1+9*6),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

	    /*intToString(lux, buf, 10, 10);
	    oled_fillRect((1+9*6),9, 80, 16, OLED_COLOR_WHITE);
	    oled_putString((1+9*6),9, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

	    intToString(trim, buf, 10, 10);
	    oled_fillRect((1+9*6),17, 80, 24, OLED_COLOR_WHITE);
	    oled_putString((1+9*6),17, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE); */

	    /* delay */
	    Timer0_Wait(200);
	}
	display_working_modes();
	btn2 = 1;
}

int main (void)
{
    // uint32_t lux = 0;
    // uint32_t trim = 0;

    init_i2c();
    init_ssp();
    init_adc();

    oled_init();
    // light_init();
    temp_init (&getTicks);

    led7seg_init();

	if (SysTick_Config(SystemCoreClock / 1000)) {
		while (1);  // Capture error
	}

    /*
     * Assume base board in zero-g position when reading first value.
     */

    // light_enable();
    // light_setRange(LIGHT_RANGE_4000);

    // oled_line(0,0,95,63, OLED_COLOR_BLACK);
    // oled_putString(1,9,  (uint8_t*)"Light  : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    // oled_putString(1,17, (uint8_t*)"Trimpot: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	display_working_modes();
	oled_circle(5, 22, 3, OLED_COLOR_BLACK);
	led7seg_setChar('1', FALSE);
	while(1) {
		btn2 = ((GPIO_ReadValue(1) >> 31) & 0x01);
		if(btn2 == 0) {
			switch(mode) {
			case 1:
				display_measurement_options();
				oled_circle(5, 22, 3, OLED_COLOR_BLACK);
				break;
			case 2:
				display_working_modes();
				oled_circle(5, 37, 3, OLED_COLOR_BLACK);
				break;
			case 3:
				display_working_modes();
				oled_circle(5, 52, 3, OLED_COLOR_BLACK);
				break;
			}
		}
		btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
		Timer0_Wait(200);
		if(btn1 == 0) {
			mode++;
			switch(mode) {
				case 1:
					oled_circle(5, 52, 3, OLED_COLOR_WHITE);
					oled_circle(5, 22, 3, OLED_COLOR_BLACK);
					btn1 = 1;
					led7seg_setChar('1', FALSE);
					break;
				case 2:
					oled_circle(5, 22, 3, OLED_COLOR_WHITE);
					oled_circle(5, 37, 3, OLED_COLOR_BLACK);
					btn1 = 1;
					led7seg_setChar('2', FALSE);
					break;
				case 3:
					mode = 0;
					oled_circle(5, 37, 3, OLED_COLOR_WHITE);
					oled_circle(5, 52, 3, OLED_COLOR_BLACK);
					btn1 = 1;
					led7seg_setChar('3', FALSE);
					break;
				}
			Timer0_Wait(2000);
		}
	}
}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}
