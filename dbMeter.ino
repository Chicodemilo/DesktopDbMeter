#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>



// ADC_SOUND_REF is the reference ADC (Analog-to-Digital Converter) value corresponding to a known sound level.
// It represents the ADC output when the input sound level is at the reference level (e.g., 80 dB).
// Changing this value will affect the calibration of the sound level measurement, making it more or less sensitive.
#define ADC_SOUND_REF 130

// DB_SOUND_REF is the reference sound level in decibels (dB) that corresponds to the ADC_SOUND_REF value.
// It sets the baseline for the relationship between ADC values and sound levels.
// Changing this value will change the reference point for sound level calculations, effectively shifting the entire dB scale.
#define DB_SOUND_REF 80

#define MAX_HISTORY 50

double get_abs_db(int input_level) {
    double db_value = 20 * log((double)input_level / (double)ADC_SOUND_REF) + DB_SOUND_REF;
    return (int)db_value;
}

String millisToTime(long millis) {
    int seconds = millis / 1000;
    int minutes = seconds / 60;
    int hours = minutes / 60;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    char buffer[9];
    snprintf(buffer, 9, "%02d:%02d:%02d", hours, minutes, seconds);
    
    return String(buffer);
}

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


//******************** MAIN VARIABLES ***********************//


int sound = A0;
int new_db;
const int sampleWindow = 50;
unsigned int sample;
String overSeventy = "00:00:00";
String testMe = "hey";
unsigned int millOverSeventy = 0;
int modeSwitchPin = 0; // D3 GPIO for mode switch
int modeSwitchState = HIGH; // initialize the mode switch state to HIGH
int displayMode = 1;// Initial Display Mode

uint8_t history[MAX_HISTORY] = {0};
int buttonPin = 2; // D4 GPIO OverSeventy Reset
int buttonState = HIGH;
unsigned long lastButtonPressTime = 0;
unsigned long debounceDelay = 50;


//******************** MODE 1 ***********************//
// Scrolling R to L Db vert Db lines centered horz in the blue area
// Db values in the yellow.

void drawMode1() {
  uint8_t textHeight = 16 + 8; // Text size 2 height (16) + Text size 1 height (8)
  uint8_t middle = (SCREEN_HEIGHT - textHeight) / 2 + textHeight;
  uint8_t graphStartX = 48; // Start drawing the graph after the text width

  for (int8_t i = MAX_HISTORY - 1; i >= 0; i--) {
    uint8_t height;
    if (history[i] >= 0 && history[i] <= 27) {
      height = 1;
    } else {
      height = map(history[i], 28, 100, 2, middle - textHeight);
    }
    display.fillRect(graphStartX + (SCREEN_WIDTH - graphStartX - 2 - ((MAX_HISTORY - 1 - i) * 3)), middle - height, 2, height, WHITE);
    display.fillRect(graphStartX + (SCREEN_WIDTH - graphStartX - 2 - ((MAX_HISTORY - 1 - i) * 3)), middle, 2, height, WHITE);
  }
}

//******************** MODE 2 ***********************//
// Scrolling R to L Db vert Db lines. High levels reach the yellow.
// Db values at the bottom.


void drawMode2() {
  uint8_t textHeight = 16;
  uint8_t padding = 2;
  for (int8_t i = MAX_HISTORY - 1; i >= 0; i--) {
    uint8_t height;
    if (history[i] >= 0 && history[i] <= 27) {
      height = 1;
    } else {
      height = map(history[i], 28, 100, 2, SCREEN_HEIGHT - textHeight - 2 * padding);
    }
    display.fillRect(SCREEN_WIDTH - 2 - ((MAX_HISTORY - 1 - i) * 3), SCREEN_HEIGHT - height - textHeight - padding, 2, height, WHITE);
  }
  display.fillRect(0, SCREEN_HEIGHT - textHeight - padding * 2, SCREEN_WIDTH, padding, WHITE); // Add 2px padding above the text
  display.fillRect(0, SCREEN_HEIGHT - padding, SCREEN_WIDTH, padding, WHITE); // Add 2px padding below the text
}


//******************** MODE 3 ***********************//
// One big bouncy level reader.
// Db values at the bottom.

void drawMode3() {
  uint8_t height;
  if (new_db >= 0 && new_db <= 27) {
    height = 1;
  } else {
    height = map(new_db, 28, 100, 2, SCREEN_HEIGHT - 8 - 18); // Adjust the maximum height to account for the lower portion
  }
  display.fillRect(0, SCREEN_HEIGHT - height - 18, SCREEN_WIDTH, height, WHITE); // Shift the starting position of the bar up by 18 pixels
  display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - height - 18, BLACK);
}


//******************** PIXEL BURST MODE 4 ***********************//
// More volume more stars. Trippy. 


unsigned long lastPixelBurstTime = 0;
const unsigned long pixelBurstDuration = 1000;
const int maxPixelBursts = 4;

struct PixelBurst {
  int x;
  int y;
  unsigned long timestamp;
};

PixelBurst pixelBursts[maxPixelBursts];

void addPixelBurst(int x, int y) {
  unsigned long currentTime = millis();
  for (int i = 0; i < maxPixelBursts; i++) {
    if (currentTime - pixelBursts[i].timestamp >= pixelBurstDuration) {
      pixelBursts[i].x = x;
      pixelBursts[i].y = y;
      pixelBursts[i].timestamp = currentTime;
      break;
    }
  }
}

void drawPixelBurst(int numPixels) {

  // Add new pixel bursts
  for (int i = 0; i < numPixels; i++) {
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);
    display.drawPixel(x, y, WHITE);
  }

  display.display();
}


//******************** RAIN STUFF ***********************//

// Can't get this one working right - take a crack if you wish.
// Issuse 1: It only makes the amount of drops for the inital DB reading - then it doesn't update.
// Issue 2: Either it can't break out of the loop (It stays in mode5() when it gets into it.) Or it just jumps over the mode.
// Whatever. Moving on.


#define RAIN_DROP_WIDTH 2
#define RAIN_DROP_HEIGHT 6
#define MAX_RAIN_DROPS 60

struct RainDrop {
  int x;
  int y;
  int speed;
  int length;
};

RainDrop rainDrops[MAX_RAIN_DROPS];

void initializeRaindrops(int numDrops) {
  for (int i = 0; i < numDrops; i++) {
    rainDrops[i].x = random(0, SCREEN_WIDTH - RAIN_DROP_WIDTH);
    rainDrops[i].y = random(-SCREEN_HEIGHT, 0);
    rainDrops[i].speed = random(1, 5);
    rainDrops[i].length = random(2, 10);
  }
}

void updateRaindrops(int numDrops) {
  for (int i = 0; i < numDrops; i++) {
    // Clear the raindrop
    display.fillRect(rainDrops[i].x, rainDrops[i].y, RAIN_DROP_WIDTH, rainDrops[i].length, BLACK);

    // Move the raindrop
    rainDrops[i].y += rainDrops[i].speed;

    // Draw the raindrop
    if (rainDrops[i].y > SCREEN_HEIGHT) {
      rainDrops[i].x = random(0, SCREEN_WIDTH - RAIN_DROP_WIDTH);
      rainDrops[i].y = random(-SCREEN_HEIGHT, 0);
      rainDrops[i].speed = random(1, 5);
      rainDrops[i].length = random(2, 10);
    } else {
      display.fillRect(rainDrops[i].x, rainDrops[i].y, RAIN_DROP_WIDTH, rainDrops[i].length, WHITE);
    }

    // Check for mode switch
    modeSwitchState = digitalRead(modeSwitchPin);
    if (modeSwitchState == LOW) {
      displayMode = 1;
      return; // break out of the function - not breaking out
    }
  }
}



void drawRaindrops(int numDrops) {
  initializeRaindrops(numDrops);
  updateRaindrops(numDrops);
  display.display();
}


//******************** UPDATE VISUALIZATION  ***********************//

void updateVisualization() {

  display.clearDisplay();
  display.setTextColor(WHITE);

  if (displayMode == 1) {
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.print("dB: ");
    display.print((int)round(new_db));
    display.setCursor(0, 16);
    display.setTextSize(1);
    display.print("75+ ");
    display.print(overSeventy.c_str());
    drawMode1();
  } else if (displayMode == 2) {
    display.fillRect(0, SCREEN_HEIGHT - 18, SCREEN_WIDTH, 18, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(2, SCREEN_HEIGHT - 16 - 2);
    display.setTextSize(1);
    display.print("dB:");
    display.setCursor(2, SCREEN_HEIGHT - 8 - 2);
    display.print((int)round(new_db));
    display.setCursor(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 16 - 2);
    display.print("75+");
    display.setCursor(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 8 - 2);
    display.print(overSeventy.c_str());
    drawMode2();
  } else if (displayMode == 3) {
    display.fillRect(0, SCREEN_HEIGHT - 18, SCREEN_WIDTH, 18, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(2, SCREEN_HEIGHT - 16 - 2);
    display.setTextSize(1);
    display.print("dB:");
    display.setCursor(2, SCREEN_HEIGHT - 8 - 2);
    display.print((int)round(new_db));
    display.setCursor(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 16 - 2);
    display.print("75+");
    display.setCursor(SCREEN_WIDTH - 54, SCREEN_HEIGHT - 8 - 2);
    display.print(overSeventy.c_str());
    drawMode3();
  }else if (displayMode == 4) {

        int numPixels;
        if (new_db <= 20) {
            numPixels = 1;
        } else if (new_db >= 21 && new_db <= 30) {
            numPixels = 13;
        } else if (new_db >= 31 && new_db <= 40) {
            numPixels = 23;
        } else if (new_db >= 41 && new_db <= 50) {
            numPixels = 40;
        } else if (new_db >= 51 && new_db <= 60) {
            numPixels = 58;
        } else if (new_db >= 61 && new_db <= 70) {
            numPixels = 80;
        } else if (new_db >= 71 && new_db <= 80) {
            numPixels = 110;
        } else if (new_db >= 81 && new_db <= 90) {
            numPixels = 145;
        } else if (new_db >= 91 && new_db <= 100) {
            numPixels = 170;
        } else {
            numPixels = 210;
        }
        drawPixelBurst(numPixels);
    } else if (displayMode == 5) {
      int numRainDrops;
      if (new_db <= 20) {
          numRainDrops = 5;
      } else if (new_db >= 21 && new_db <= 30) {
          numRainDrops = 5;
      } else if (new_db >= 31 && new_db <= 40) {
          numRainDrops = 12;
      } else if (new_db >= 41 && new_db <= 50) {
          numRainDrops = 20;
      } else if (new_db >= 51 && new_db <= 60) {
          numRainDrops = 35;
      } else if (new_db >= 61 && new_db <= 70) {
          numRainDrops = 60;
      } else if (new_db >= 71 && new_db <= 80) {
          numRainDrops = 80;
      } else if (new_db >= 81 && new_db <= 90) {
          numRainDrops = 120;
      } else if (new_db >= 91 && new_db <= 100) {
          numRainDrops = 130;
      } else {
          numRainDrops = 190;
      }

      if (displayMode == 5) {
//  display.clearDisplay();
//  display.setCursor(0,0);
//  display.setTextSize(1);
//  display.setTextColor(WHITE);
//  display.println("Mode 5: Raindrops");
//  display.println();
//  display.print("Current mode: ");
//  display.println(displayMode);
//  display.display();
          drawRaindrops(numRainDrops);
          displayMode = 1; // Breaking out because I can't get it working
      } else {
          updateVisualization();
      }
    }
  
  display.display();
}


//******************** SET UP ***********************//

void setup() {
    Serial.begin(115200);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.display();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("D'Waantu"); // Don't want to
    display.display();
    delay(500);

    display.setCursor(0, 22);
    display.println("B'Guantu"); // But I'm going to
    // Remember when D'waantu B'Guantu is the catch phrase sweeping the nation... you saw it here first.
    // "Hey can you take the trash out?" "D'Waantu B'Guantu" See pretty useful.
    // Another fine mantra from D'Waantu B'Guantu© Industries!
    display.display();
    delay(2000);

    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(modeSwitchPin, INPUT_PULLUP);
}


//******************** MAIN LOOP   ***********************//

void loop() {
    unsigned long currentMillis = millis();
    display.clearDisplay();
    unsigned long startMillis = millis();
    double peakToPeak = 0;
    unsigned int signalMax = 0;
    unsigned int signalMin = 1024;
  
    int modeSwitchState = digitalRead(modeSwitchPin);
    int buttonState = digitalRead(buttonPin);

    while (millis() - startMillis < sampleWindow) {
        sample = analogRead(0);
        if (sample < 1024) {
            if (sample > signalMax) {
                signalMax = sample;
            } else if (sample < signalMin) {
                signalMin = sample;
            }
        }
    }

    peakToPeak = signalMax - signalMin;
    new_db = get_abs_db(peakToPeak);

    // Ok this is a total hack & I'm sorry about that
    // Basically I couldn't get close enough adjusting the variables at the top.
    // Either the lows were good but the highs were off or oppsi-doopsy.
    // So fuck it... make them what matches your chosen DB meter - this got me close.
    switch ((int)new_db) 
        {
        case 1 ... 4:
            new_db += 2;
            break;
        case 5 ... 9:
            new_db += 11;
            break;
        case 20 ... 29:
            new_db += 12;
            break;
        case 30 ... 39:
            new_db += 12;
            break;
        case 40 ... 44:
            new_db += 12;
            break;
        case 45 ... 49:
            new_db += 13;
            break;
        case 50 ... 59:
            new_db += 13;
            break;
        case 60 ... 69:
            new_db += 13;
            break;
        case 70 ... 79:
            new_db += 14;
            break;
        case 80 ... 89:
            new_db += 14;
            break;
        case 90 ... 99:
            new_db += 14;
            break;
        default:
            if (new_db >= 90) {
                new_db += 14;
            }
            if (new_db < 0) {
                new_db = 0;
            }
            break;
    }


    if (new_db >= 75) {   
      // testMe = "high";
      millOverSeventy += 100;
      overSeventy = millisToTime(millOverSeventy);
    }else{
      // testMe = "low";
    }


    if (buttonState == LOW) {
        // testMe = "PUSH!";
        millOverSeventy = 0;
        overSeventy = millisToTime(millOverSeventy);
    }else{
        // testMe = "no push";
    }

    if (modeSwitchState == LOW) {
        displayMode++;
        if (displayMode > 5) {
            displayMode = 1;
        }
        delay(300);
    }

    memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof(uint8_t));
    history[MAX_HISTORY - 1] = (uint8_t)new_db;

    updateVisualization();
}
