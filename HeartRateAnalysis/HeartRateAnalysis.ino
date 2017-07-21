#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Switch.h>
#include <Fonts/FreeSans12pt7b.h>

//  VARIABLES
int pulsePin = 0;                 // Pulse Sensor purple wire connected to analog pin 0
int blinkPin = 13;                // pin to blink led at each beat

// these variables are volatile because they are used during the interrupt service routine!
volatile int BPM;                   // used to hold the pulse rate
volatile int Signal;                // holds the incoming raw data
volatile int IBI = 600;             // holds the time between beats, the Inter-Beat Interval
volatile boolean Pulse = false;     // true when pulse wave is high, false when it's low
volatile boolean QS = false;        // becomes true when Arduoino finds a beat.

//#define USE_SERIAL
#define USE_OLED

#define OLED_RESET 10
Adafruit_SSD1306 display(OLED_RESET);
Switch mode = Switch(7, INPUT_PULLUP, LOW, 1);

void setup(void) {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  interruptSetup();                 // sets up to read Pulse Sensor signal every 2mS 
  display.setFont(&FreeSans12pt7b);
  display.setTextColor(WHITE);
  display.setTextSize(1);
#if defined(USE_SERIAL)
  Serial.begin(115200);
#endif
}

#define READINGS_NUM 64
#define RR_NUM 64
#define MAX_RR_VAL 1400.0
#define MIN_RR_VAL 100.0
#define RR_VAL_SCALE (64.0 / (MAX_RR_VAL - MIN_RR_VAL))

byte vals[READINGS_NUM] = {};
short rr[RR_NUM] = {};
byte rr_count = 0, rr_index = 0;
byte mi = 255, ma = 0;
short min_rr_val = MIN_RR_VAL;
float rr_val_scale = RR_VAL_SCALE;

enum show_enum {
  VARIABILITY = 0,
  PULSE,
  PULSE_VARIABILITY,
  HR,
};
show_enum show = PULSE;

void drawPulse(byte offset)
{
  for (register byte i = 0; i < READINGS_NUM - 1; i++)
    display.drawLine(offset + i, 64 - (byte)(64 * (vals[i] - mi) / (float)(ma - mi)), 
                  offset + i + 1, 64 - (byte)(64 * (vals[i+1] - mi) / (float)(ma - mi)), WHITE);
}

void drawVariability(byte offset)
{
    for (register byte i = 0; i < min(rr_count, READINGS_NUM) - 1; i++)
    {
      int index1 = ((int)rr_index - i - 1)%RR_NUM;
      int index2 = ((int)rr_index - i - 2)%RR_NUM;
      if (index1 < 0)
        index1 += rr_count;
      if (index2 < 0)
        index2 += rr_count;
      display.drawLine(offset - i, 64 - (rr[index1] - MIN_RR_VAL) * RR_VAL_SCALE, 
                    offset - i - 1, 64 - (rr[index2] - MIN_RR_VAL) * RR_VAL_SCALE, WHITE);
    }  
}

boolean not_long_press = true;

void loop(void) 
{
  mode.poll();
  if (mode.pushed())
  {
    not_long_press = true;
  }
  else if (mode.longPress())
  {
    show = (show_enum)(show == HR ? VARIABILITY : (int)show + 1);
    not_long_press = false;
  }
  else if (mode.released() && not_long_press)
  {
    if (min_rr_val == (short)MIN_RR_VAL)
    {
      float rr_ma = MIN_RR_VAL;
      float rr_mi = MAX_RR_VAL;
      for (register byte i = 0; i < rr_count; i++)
      {
        if (rr[i] > rr_ma)
          rr_ma = rr[i];
        if (rr[i] < rr_mi)
          rr_mi = rr[i];
      }
      min_rr_val = rr_mi;
      rr_val_scale = (64.0 / (rr_ma - rr_mi));
    }
    else
    {
      min_rr_val = MIN_RR_VAL;
      rr_val_scale = RR_VAL_SCALE;
    }
  }
  if (mode.doubleClick())
  {
    rr_count = 0;
    rr_index = 0;
    min_rr_val = MIN_RR_VAL;
    rr_val_scale = RR_VAL_SCALE;    
    memset(&rr, 0, sizeof(rr));
  }
  
  for (register byte i = 0; i < READINGS_NUM - 1; i++)
    vals[i] = vals[i + 1];
  vals[READINGS_NUM - 1] = min(600, max(400, Signal)) - 400;

  mi = 255;
  ma = 0;
  for (register byte i = 0; i < READINGS_NUM; i++)
  {
    if (vals[i] > ma)
      ma = vals[i];
    if (vals[i] < mi)
      mi = vals[i];
  }
  if (ma - mi < 64)
  {
    register byte mid = (ma + mi) >> 1;
    ma = min(224, mid) + 31;
    mi = max(31, mid) - 31;
  }

#if defined(USE_SERIAL)
  Serial.print(vals[READINGS_NUM -1]);
  Serial.print(' ');
  Serial.print(mi);
  Serial.print(' ');
  Serial.println(ma);
#endif
  
  if (QS)
  {
      rr[rr_index] = IBI;
      rr_index = (rr_index + 1)%RR_NUM;
      if (rr_index > rr_count)
        rr_count = rr_index;
      QS = false;
  }

#if defined(USE_OLED)
  display.clearDisplay();         // clear the internal memory

  switch (show)
  {
    case HR:
      display.setCursor(0, 40);
      display.print(BPM);
      break;
    case VARIABILITY:
      drawVariability(64);
      break;
    case PULSE_VARIABILITY:
      drawVariability(128);
    case PULSE:
      drawPulse(0);
      break;
  }

  if (show != PULSE_VARIABILITY)
  {
    for (short i = 0; i < MAX_RR_VAL;)
    {
      byte pos = i * rr_val_scale;
      if (pos > 64)
        break; 
      display.drawPixel(63, 64 - pos, WHITE);
      i += 100;
    }
    for (register byte i = 0; i < rr_count - 1; i++)
    {
      int index1 = ((int)rr_index - i - 1)%RR_NUM;
      int index2 = ((int)rr_index - i - 2)%RR_NUM;
      if (index1 < 0)
        index1 += rr_count;
      if (index2 < 0)
        index2 += rr_count;      
      display.drawPixel(64 + (rr[index1] - min_rr_val) * rr_val_scale, 
                        64 - (rr[index2] - min_rr_val) * rr_val_scale, WHITE);
    }
  }

  display.display();          // transfer internal memory to the display
#else
  delay(20);
#endif
}

