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

#define NOTE_PIN_HIGH() GPIO_SetValue(0, 1 << 26);
#define NOTE_PIN_LOW() GPIO_ClearValue(0, 1 << 26);

uint32_t msTicks = 0;
uint8_t buf[10];
uint8_t ch7seg = '1';

char vkupno[5];

int32_t temperatures[13];
int32_t zapisani[90];
int32_t measures[13];
int32_t lights[13];
int32_t potentiometers[13];
uint8_t btn1 = 0; // SW3
uint8_t btn2 = 0; // SW4
int mode = 1; // 1 - real-time, 2 - save, 3 - show saved
int measurement_option = 1; // 1 - temperature, 2 - light, 3 - potentiometer
//uint8_t * song = (uint8_t*)"G1,G1,G1,G1,G1,G1,G1.G1.G1.G1.G1.G1.G1.G1.G1.G1";

void SysTick_Handler(void) {
  msTicks++;
}

static uint32_t getTicks(void) {
  return msTicks;
}

static void init_ssp(void) {
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
  PINSEL_ConfigPin( & PinCfg);
  PinCfg.Pinnum = 8;
  PINSEL_ConfigPin( & PinCfg);
  PinCfg.Pinnum = 9;
  PINSEL_ConfigPin( & PinCfg);
  PinCfg.Funcnum = 0;
  PinCfg.Portnum = 2;
  PinCfg.Pinnum = 2;
  PINSEL_ConfigPin( & PinCfg);

  SSP_ConfigStructInit( & SSP_ConfigStruct);

  // Initialize SSP peripheral with parameter given in structure above
  SSP_Init(LPC_SSP1, & SSP_ConfigStruct);

  // Enable SSP peripheral
  SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void) {
  PINSEL_CFG_Type PinCfg;

  /* Initialize I2C2 pin connect */
  PinCfg.Funcnum = 2;
  PinCfg.Pinnum = 10;
  PinCfg.Portnum = 0;
  PINSEL_ConfigPin( & PinCfg);
  PinCfg.Pinnum = 11;
  PINSEL_ConfigPin( & PinCfg);

  // Initialize I2C peripheral
  I2C_Init(LPC_I2C2, 100000);

  /* Enable I2C1 operation */
  I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void) {
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
  PINSEL_ConfigPin( & PinCfg);

  /* Configuration for ADC :
   * 	Frequency at 0.2Mhz
   *  ADC channel 0, no Interrupt
   */
  ADC_Init(LPC_ADC, 200000);
  ADC_IntConfig(LPC_ADC, ADC_CHANNEL_0, DISABLE);
  ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE);

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

uint32_t normalize_temperature(uint32_t val) {
  double new_val = (double) val;
  new_val = new_val / 10;
  new_val = (((new_val - 15) / 20) * 43) + 10;
  return (uint32_t) new_val;
}

uint32_t normalize_light(uint32_t val) {
  double new_val = (double) val;
  new_val = ((new_val / 4000) * 43) + 10;
  return (uint32_t) new_val;
}

uint32_t normalize_potentiometer(uint32_t val) {
  double new_val = (double) val;
  new_val = ((new_val / 4095) * 43) + 10;
  return (uint32_t) new_val;
}

void measure_temperature(void) {
  int32_t t = 0;
  int i = 0;
  while (1) {
    if (i >= 13) {
      for (int j = 0; j < 12; j++)
        temperatures[j] = temperatures[j + 1];
      i--;
    }
    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
    if (btn1 == 0)
      break;
    t = temp_read();
    temperatures[i] = normalize_temperature(t);
    if (i < 13)
      i++;
    draw_graph_real_time(temperatures, i, "Temperature");
    Timer0_Wait(200);
  }
  display_working_modes();
  btn1 = 1;
}

void measure_light(void) {
  int32_t l = 0;
  int i = 0;
  while (1) {
    if (i >= 13) {
      for (int j = 0; j < 12; j++)
        lights[j] = lights[j + 1];
      i--;
    }
    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
    if (btn1 == 0)
      break;
    l = light_read();
    lights[i] = normalize_light(l);
    if (i < 13)
      i++;
    draw_graph_real_time(lights, i, "Light");
    Timer0_Wait(200);
  }
  display_working_modes();
  btn1 = 1;
}

void measure_potentiometer(void) {
  int32_t t = 0;
  int i = 0;
  while (1) {
    if (i >= 13) {
      for (int j = 0; j < 12; j++)
        potentiometers[j] = potentiometers[j + 1];
      i--;
    }
    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
    if (btn1 == 0)
      break;
    ADC_StartCmd(LPC_ADC, ADC_START_NOW);
    //Wait conversion complete
    while (!(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)));
    uint32_t p = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
    potentiometers[i] = normalize_potentiometer(p);
    if (i < 13)
      i++;
    draw_graph_real_time(potentiometers, i, "Potentiometer");
    Timer0_Wait(200);
  }
  display_working_modes();
  btn1 = 1;
}

void procitaj(int sto) {
  uint16_t dolzina_zapis = 11;
  int mem = 0;
  int br = 90;
  int i = 0;
  uint16_t offset = 0;
  int l = 0;

  switch (sto) {
  case 1:
    offset = 0;
    l = 3;
    break;
  case 2:
    offset = 3;
    l = 4;
    break;
  case 3:
    offset = 7;
    l = 4;
    break;
  }

  uint8_t buf[11];

  int minute[br];
  for (i = 0; i < br; i++) {
    eeprom_read(buf, i * dolzina_zapis, dolzina_zapis);
    char subbuff[5];
    memcpy(subbuff, & buf[offset], l);
    subbuff[4] = '\0';
    printf("%d. Citanje:%s\n", i, subbuff);
    //		int v = atoi(subbuff);
    char temp[3] = {
      subbuff[2],
      subbuff[3],
      '\0'
    };
    long v = strtol(temp, NULL, 10);
    minute[i] = v;
    zapisani[i] = v;
  }

}

void displaySaved(void) {
  procitaj(measurement_option);
  int c = 0;
  int br = 90;
  int i = 0;

  while (1) {
    printf("c:%d\n", c);

    if (i >= 13) {
      for (int j = 0; j < 12; j++)
        measures[j] = measures[j + 1];
      i--;
    }
    //ovde treba da dodademe vrednost od nizata
    measures[i] = zapisani[c];

    if (c >= br) break;
    c++;
    if (i < 13) {
      i++;
      draw_graph_real_time(measures, i, "Saved data:");
    }
  }
}

void display_menu(void) {
  measurement_option = 1;
  display_measurement_options();
  oled_circle(5, 22, 3, OLED_COLOR_BLACK);
  while (1) {
    btn2 = ((GPIO_ReadValue(1) >> 31) & 0x01);
    Timer0_Wait(200);
    if (btn2 == 0) {
      btn2 = 1;
      switch (measurement_option) {
      case 1:
        if (mode == 1) {
          measure_temperature();
          break;
        } else {
          displaySaved();
        }
      case 2:
        if (mode == 1) {
          measure_light();
          break;
        } else {
          displaySaved();
        }
      case 0:
        if (mode == 1) {
          measure_potentiometer();
          break;
        } else {
          displaySaved();
        }
      }
      break;
    }
    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
    Timer0_Wait(200);
    if (btn1 == 0) {
      measurement_option++;
      btn1 = 1;
      switch (measurement_option) {
      case 1:
        oled_circle(5, 52, 3, OLED_COLOR_WHITE);
        oled_circle(5, 22, 3, OLED_COLOR_BLACK);
        break;
      case 2:
        oled_circle(5, 22, 3, OLED_COLOR_WHITE);
        oled_circle(5, 37, 3, OLED_COLOR_BLACK);
        break;
      case 3:
        measurement_option = 0;
        oled_circle(5, 37, 3, OLED_COLOR_WHITE);
        oled_circle(5, 52, 3, OLED_COLOR_BLACK);
        break;
      }
      Timer0_Wait(2000);
    }
  }
}

static void change7Seg() {
  if (ch7seg > '9')
    ch7seg = '1';
  led7seg_setChar(ch7seg, FALSE);
}

static uint32_t notes[] = {
  2272, // A - 440 Hz
  2024, // B - 494 Hz
  3816, // C - 262 Hz
  3401, // D - 294 Hz
  3030, // E - 330 Hz
  2865, // F - 349 Hz
  2551, // G - 392 Hz
  1136, // a - 880 Hz
  1012, // b - 988 Hz
  1912, // c - 523 Hz
  1703, // d - 587 Hz
  1517, // e - 659 Hz
  1432, // f - 698 Hz
  1275, // g - 784 Hz
};

static void playNote(uint32_t note, uint32_t durationMs) {

  uint32_t t = 0;

  if (note > 0) {

    while (t < (durationMs * 1000)) {
      NOTE_PIN_HIGH();
      Timer0_us_Wait(note / 2);
      //delay32Us(0, note / 2);

      NOTE_PIN_LOW();
      Timer0_us_Wait(note / 2);
      //delay32Us(0, note / 2);

      t += note;
    }

  } else {
    Timer0_Wait(durationMs);
    //delay32Ms(0, durationMs);
  }
}

static uint32_t getNote(uint8_t ch) {
  if (ch >= 'A' && ch <= 'G')
    return notes[ch - 'A'];

  if (ch >= 'a' && ch <= 'g')
    return notes[ch - 'a' + 7];

  return 0;
}

static uint32_t getDuration(uint8_t ch) {
  if (ch < '0' || ch > '9')
    return 400;

  /* number of ms */

  return (ch - '0') * 200;
}

static uint32_t getPause(uint8_t ch) {
  switch (ch) {
  case '+':
    return 0;
  case ',':
    return 5;
  case '.':
    return 20;
  case '_':
    return 30;
  default:
    return 5;
  }
}

static void playSong(uint8_t * song) {
  uint32_t note = 0;
  uint32_t dur = 0;
  uint32_t pause = 0;

  /*
   * A song is a collection of tones where each tone is
   * a note, duration and pause, e.g.
   *
   * "E2,F4,"
   */

  while ( * song != '\0') {
    note = getNote( * song++);
    if ( * song == '\0')
      break;
    dur = getDuration( * song++);
    if ( * song == '\0')
      break;
    pause = getPause( * song++);

    playNote(note, dur);
    //delay32Ms(0, pause);
    Timer0_Wait(pause);

  }
}

static uint8_t * song = (uint8_t * )
"G1,G1,G1";

int p(int n) {
  int i = 0;
  int s = 1;
  for (i = 0; i < n; i++)
    s *= 2;
  return s;
}

char * svetki(int t, int o) {

  //vkupno = "0000";

  vkupno[0] = '0';
  vkupno[1] = '0';
  vkupno[2] = '0';
  vkupno[3] = 'x';
  vkupno[4] = '\0';

  if (t < 4) {
    vkupno[3] = (char)(p(t) - 1 + 48);
  } else if (t == 4) {
    vkupno[3] = 'f';
  } else if (t > 4 && t < 8) {
    vkupno[3] = 'f';
    vkupno[2] = (char)(p(t - 4) - 1 + 48);
  } else {
    vkupno[3] = 'f';
    vkupno[2] = 'f';
  }

  if (o < 4) {
    vkupno[1] = (char)(p(o) - 1 + 48);
  } else if (o == 4) {
    vkupno[1] = 'f';
  } else if (o > 4 && o < 8) {
    vkupno[1] = 'f';
    vkupno[0] = (char)(p(o - 4) - 1 + 48);
  } else {
    vkupno[1] = 'f';
    vkupno[0] = 'f';
  }

  return vkupno;
}

void sveti_temperatra(int32_t temperatura) {
  int temp = temperatura / 5;
  if (temp > 6) {
    //sviri
    playSong(song);
  }
  char rez[4];
  svetki(temp, 0);

  long sv = strtol(vkupno, NULL, 16);
  pca9532_setLeds(sv, 0xffff);
}

void sveti_osvetluvanje(int32_t osvetluvanje) {
  int osv = osvetluvanje / 5;
  svetki(0, osv);
  long sv = strtol(vkupno, NULL, 16);
  pca9532_setLeds(sv, 0xffff);
}

void draw_graph_real_time(int32_t values[13], int n, char * measurements) {
  int i = 0;
  oled_clearScreen(OLED_COLOR_WHITE);
  oled_putString(12, 1, measurements, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
  for (int j = n - 1, k = 96; j > 0; j--, k -= 8)
    oled_line(k - 8, 64 - values[j - 1], k, 64 - values[j], OLED_COLOR_BLACK);

  if (measurement_option == 1) {
    sveti_temperatra(values[0]);
    printf("Temperatura: %d\n", values[0]);
  } else if (measurement_option == 2) {
    sveti_osvetluvanje(values[0]);
    printf("Svetlina: %d\n", values[0]);
  }

}

void zapisi(int32_t temperatura, uint32_t osvetluvanje, uint32_t potenciometar, int i) {
  int dolzina_zapis = 11;
  int mem = 0;
  int br = 90;
  if (i > br) {
    i = 0;
  }

  char b[dolzina_zapis];

  //sostavi zapis
  sprintf(b, "%03d%04d%04d", temperatura, osvetluvanje, potenciometar);

  //ke zapiseme na pozicija i do i+11
  int pozicija = i * dolzina_zapis;
  eeprom_write(b, pozicija, dolzina_zapis);

  i++;
}

int main(void)

{
  init_i2c();
  init_ssp();
  init_adc();

  eeprom_init();
  oled_init();
  light_init();
  temp_init( & getTicks);

  led7seg_init();

  if (SysTick_Config(SystemCoreClock / 1000)) {
    while (1); // Capture error
  }

  light_enable();
  light_setRange(LIGHT_RANGE_4000);

  display_working_modes();
  oled_circle(5, 22, 3, OLED_COLOR_BLACK);
  led7seg_setChar('1', FALSE);
  while (1) {

    btn2 = ((GPIO_ReadValue(1) >> 31) & 0x01);
    Timer0_Wait(200);
    if (btn2 == 0) {
      btn2 = 1;
      switch (mode) {
      case 1: //real time
        display_menu();
        display_working_modes();
        oled_circle(5, 22, 3, OLED_COLOR_BLACK);

        break;
      case 2: //save
        oled_clearScreen(OLED_COLOR_WHITE);
        oled_putString(1, 1, "Choose time!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        while (1) {
          btn2 = ((GPIO_ReadValue(1) >> 31) & 0x01); //enter
          btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01); //++
          //printf("while\n", ch7seg++);

          if (btn1 == 0) {
            Timer0_Wait(200);
            ch7seg++;
            change7Seg();
            printf("ch7seg++: %d\n", ch7seg);
            printf("n: %d\n 0", 0);
          }
          if (btn2 == 0) {
            Timer0_Wait(200);

            int n = 0;
            // oled_clearScreen(OLED_COLOR_WHITE);
            // oled_putString(1,1,  ch7seg, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            // oled_putString(1,8,  " minutes", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

            printf("n: %d\n", 0);

            while (n < 90) {
              printf("n: %d\n", n);

              //zemi merki za temperatura, osvetluvanje i potenciometar
              int32_t t = 0;
              int32_t tn = 0;
              uint32_t lux = 0;
              uint32_t luxn = 0;
              uint32_t p = 0;
              int32_t pn = 0;

              t = temp_read();
              tn = normalize_temperature(t);
              lux = light_read();
              luxn = normalize_light(lux);
              p = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
              pn = normalize_potentiometer(p);

              //zapisi
              zapisi(tn, luxn, pn, n);
              n++;
              //cekaj
              Timer0_Wait(ch7seg * 2 / 3);
            }

            display_working_modes();
            btn1 = 1;
            break;
          }
        }
        break;
      case 0: //read
        printf("Option 3.");

        display_menu();
        display_working_modes();
        oled_circle(5, 52, 3, OLED_COLOR_BLACK);
        break;
      }
    }
    btn1 = ((GPIO_ReadValue(0) >> 4) & 0x01);
    Timer0_Wait(200);
    if (btn1 == 0) {
      mode++;
      btn1 = 1;
      switch (mode) {
      case 1:
        oled_circle(5, 52, 3, OLED_COLOR_WHITE);
        oled_circle(5, 22, 3, OLED_COLOR_BLACK);
        led7seg_setChar('1', FALSE);
        break;
      case 2:
        oled_circle(5, 22, 3, OLED_COLOR_WHITE);
        oled_circle(5, 37, 3, OLED_COLOR_BLACK);
        led7seg_setChar('2', FALSE);
        break;
      case 3:
        mode = 0;
        oled_circle(5, 37, 3, OLED_COLOR_WHITE);
        oled_circle(5, 52, 3, OLED_COLOR_BLACK);
        led7seg_setChar('3', FALSE);
        break;
      }
      Timer0_Wait(2000);
    }

  }
}
