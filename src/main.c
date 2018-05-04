/*****************************************************************************
 *   Peripherals such as temp sensor, light sensor, accelerometer,
 *   and trim potentiometer are monitored and values are written to
 *   the OLED display.
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


static uint32_t msTicks = 0;
static uint8_t buf[10];

int32_t temperatures[13];

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base)
{
    static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
    int pos = 0;
    int tmpValue = value;

    // the buffer must not be null and at least have a length of 2 to handle one
    // digit and null-terminator
    if (pBuf == NULL || len < 2)
    {
        return;
    }

    // a valid base cannot be less than 2 or larger than 36
    // a base value of 2 means binary representation. A value of 1 would mean only zeros
    // a base larger than 36 can only be used if a larger alphabet were used.
    if (base < 2 || base > 36)
    {
        return;
    }

    // negative value
    if (value < 0)
    {
        tmpValue = -tmpValue;
        value    = -value;
        pBuf[pos++] = '-';
    }

    // calculate the required length of the buffer
    do {
        pos++;
        tmpValue /= base;
    } while(tmpValue > 0);


    if (pos > len)
    {
        // the len parameter is invalid.
        return;
    }

    pBuf[pos] = '\0';

    do {
        pBuf[--pos] = pAscii[value % base];
        value /= base;
    } while(value > 0);

    return;

}

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

void display_menu(void) {
	//
}

void draw_graph_real_time(int32_t values[13], int n) {
	oled_clearScreen(OLED_COLOR_WHITE);
	int j = n-1;
	int k = 96;
	for(j = n-1, k = 96; j > 0; j--, k-=8) {
		int32_t val1 = k-8;
		int32_t val2 = 64 - values[j-1];
		int32_t val3 = k;
		int32_t val4 = 64 - values[j];
		oled_line(val1, val2, val3, val4, OLED_COLOR_BLACK);
	}
}

uint32_t normalize_temperature(uint32_t val) {
	double new_val = (double)val;
	new_val = new_val / 10;
	new_val = (((new_val - 15) / 20) * 62) + 1;
	return (uint32_t)new_val;
}

int main (void)
{
	// display_menu();

    int32_t t = 0;
    // uint32_t lux = 0;
    // uint32_t trim = 0;

    init_i2c();
    init_ssp();
    init_adc();

    oled_init();
    // light_init();
    temp_init (&getTicks);


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

    int i = 0;
    while(1) {
        /* Temperature */
    	if(i >= 13) {
    		for(int j = 0; j < 12; j++)
    			temperatures[j] = temperatures[j+1];
    		i--;
    	}
        t = temp_read();
        temperatures[i] = normalize_temperature(t);
        if(i < 13)
        	i++;
        draw_graph_real_time(temperatures, i);

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
}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}