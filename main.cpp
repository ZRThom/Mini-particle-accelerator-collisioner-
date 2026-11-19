#include <Wire.h>
#include "rgb_lcd.h"
#include <math.h>

rgb_lcd lcd;

// buzzer
const int buzzerPin1 = 7;
const float temperatureThreshold = 12.0;
const int buzzerFrequency = 1000;

const int potentiometerPin1 = A2;
const int potentiometerMax = 650;

// action panel
const int btnPin1 = 4;
const int touchPin1 = 2;
const int tempPin1 = A0;

const int B = 4275;
const long R0 = 100000;

// pin panel (6 panel with 3 pin init each)
const int registerPins[6][3] = {
  {24, 26, 28},
  {34, 36, 38},
  {44, 46, 48},

  {25, 27, 29},
  {35, 37, 39},
  {45, 47, 49}
};

const int ledCount = 16;
const bool commonAnode = false;

const float maxSpeed = 50.0;
float speed = 0.0;

int bluePosition = 0;
int redPosition = ledCount - 1;

// btn state
bool running = false;
bool lastButtonState = LOW;
bool stableButtonState = LOW;

// collision panel
bool collisionRequested = false;
bool collisionActive = false;
bool collisionFinished = false;

bool temperatureValid = false;
float temperature = 0;

int waveStep = 0;
const int waveSize = 3;
const unsigned long flashDuration = 150;

// lcd panel (timers)
unsigned long startTime = 0;
unsigned long lastMove = 0;
unsigned long lastButtonPress = 0;
unsigned long lastLcdUpdate = 0;
unsigned long lastTempUpdate = 0;
unsigned long collisionStart = 0;

const unsigned long debounceInterval = 50;
const unsigned long lcdInterval = 200;
const unsigned long tempInterval = 500;
const unsigned long collisionInterval = 80;

// init all
void setup() {
  // every led with all 6 register
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 3; j++) {
      pinMode(registerPins[i][j], OUTPUT);
    }
  }

  pinMode(btnPin1, INPUT);
  pinMode(touchPin1, INPUT);

  pinMode(buzzerPin1, OUTPUT);

  lcd.begin(16, 2);
  lcd.setRGB(0, 0, 0);
  setRGB(0, 0, 0);
}

void updateRegister(int reg, byte value) {
  if (commonAnode) {
    value = ~value;
  }

  int clock = registerPins[reg][0];
  int latch = registerPins[reg][1];
  int data = registerPins[reg][2];

  digitalWrite(latch, LOW);
  shiftOut(data, clock, MSBFIRST, value);
  digitalWrite(latch, HIGH);
}

// reverse rgb led  (for first panel)
byte reverseBits(byte value) {
  byte result = 0;
  for (int i = 0; i < 8; i++) {
    result <<= 1;
    result |= value & 1;
    value >>= 1;
  }

  return result;
}

void setRGB(uint16_t red, uint16_t green, uint16_t blue) {
  // first panel
  updateRegister(0, reverseBits(red & 0xFF));
  updateRegister(1, reverseBits(green & 0xFF));
  updateRegister(2, reverseBits(blue & 0xFF));

  // second panel
  updateRegister(3, blue >> 8);
  updateRegister(4, green >> 8);
  updateRegister(5, red >> 8);
}

void showParticles() {
  uint16_t red = 1U << redPosition;
  uint16_t blue = 1U << bluePosition;
  setRGB(red, 0, blue);
}

// speed with potentiometer
void updateParticles() {
  unsigned long now = millis();

  int potentiometerValue = analogRead(potentiometerPin1);

  speed = min((float)potentiometerValue / potentiometerMax, 1.0f) * maxSpeed;
  if (speed <= 0) {
    lastMove = now; // stop led
    return;
  }

  if (potentiometerValue >= potentiometerMax && digitalRead(touchPin1) == HIGH) {
    collisionRequested = true;
  }

  unsigned long interval = 1000.0 / speed;

  if (now - lastMove >= interval) {
    lastMove = now;

    // Collision at the center
    if (collisionRequested &&
        bluePosition == 7 &&
        redPosition == 8) {
      collision();
      return;
    }

    bluePosition++;
    redPosition--;

    if (bluePosition >= ledCount) {
      bluePosition = 0;
    }

    if (redPosition < 0) {
      redPosition = ledCount - 1;
    }

    showParticles();
  }
}

void updateButton() {
  bool buttonState = digitalRead(btnPin1);
  if (buttonState != lastButtonState) {
    lastButtonPress = millis();
  }

  if (millis() - lastButtonPress >= debounceInterval) {
    if (buttonState != stableButtonState) {
      stableButtonState = buttonState;

      if (stableButtonState == HIGH) {
        if (running) {
          stopSystem();
        } else {
          startSystem();
        }
      }
    }
  }
  lastButtonState = buttonState;
}

// temp calc
void updateTemperature() {
  if (millis() - lastTempUpdate < tempInterval) {
    return;
  } // fix millis instead of delay
  lastTempUpdate = millis();

  int a = analogRead(tempPin1);

  temperatureValid = a > 0 && a < 1023;
  if (!temperatureValid) {
    return;
  }

  float R = 1023.0 / a - 1.0;
  R = R0 * R;

  temperature = 1.0 / (log(R / R0) / B + 1 / 298.15) - 273.15; // convert to temperature via datasheet

  if (temperature >= temperatureThreshold) {
    tone(buzzerPin1, buzzerFrequency, 100);
  } else {
    noTone(buzzerPin1);
  }
}

void printStatus(const char* message) {
  lcd.setCursor(0, 0);
  lcd.print(message);

  for (int i = strlen(message); i < 16; i++) { // string 
    lcd.print(" ");
  }
}

void updateLCD() {
  if (millis() - lastLcdUpdate < lcdInterval) {
    return;
  }

  lastLcdUpdate = millis();

  // first line
  if (collisionFinished) {
    printStatus("collision end");
  } else if (collisionActive) {
    printStatus("active collision");
  } else if (collisionRequested) {
    printStatus("expect collision");
  } else if (speed >= maxSpeed) {
    printStatus("max touch sensor");
  } else {
    printStatus("adjust speed...");
  }

  // second line
  lcd.setCursor(0, 1);

  if (temperatureValid) {
    size_t count = lcd.print("Temp: ");
    count += lcd.print(temperature, 1);
    count += lcd.print(" C");

    while (count < 16) {
      lcd.print(" ");
      count++;
    }
  } else {
    lcd.print("Temp: -- ");
  }
}

void startSystem() {
  running = true;
  
  collisionRequested = false;
  collisionActive = false;
  collisionFinished = false;

  speed = 0.0;

  bluePosition = 0;
  redPosition = ledCount - 1;
  lastMove = millis();

  // lcd animation for the start
  lcd.setRGB(0, 0, 0);
  lcd.setRGB(255, 255, 255);
  delay(50);
  lcd.setRGB(0, 0, 0);
  delay(50);
  lcd.setRGB(255, 255, 255);
  delay(50);
  lcd.setRGB(0, 0, 0);
  delay(1000);
  lcd.setRGB(12, 12, 12);
  delay(100);
  lcd.setRGB(25, 25, 25);
  delay(100);
  lcd.setRGB(50, 50, 50);
  delay(100);
  lcd.setRGB(75, 75, 75);
  delay(100);
  lcd.setRGB(100, 100, 100);
  delay(100);
  lcd.setRGB(150, 150, 150);
  delay(100);
  lcd.setRGB(200, 200, 200);
  delay(100);
  lcd.setRGB(255, 255, 255);
  showParticles();
}

void stopSystem() {
  running = false;
  
  collisionRequested = false;
  collisionActive = false;
  collisionFinished = false;

  noTone(buzzerPin1);

  setRGB(0, 0, 0);
  lcd.clear();
  lcd.setRGB(0, 0, 0);
}

void showCollisionWave() {
  uint16_t white = 0;
  int half = ledCount / 2;
  int growSteps = half - waveSize;

  if (waveStep <= growSteps) {
    int size = waveSize + waveStep;
    for (int i = 0; i < size; i++) {
      white |= 1U << (7 - i);
      white |= 1U << (8 + i);
    }
  } else {
    white = 0xFFFF;
    int fadeStep = waveStep - growSteps;
    for (int i = 0; i < fadeStep; i++) {
      white &= ~(1U << (7 - i));
      white &= ~(1U << (8 + i));
    }
  }
  setRGB(white, white, white);
}

void collision() {
  collisionRequested = false;
  collisionActive = true;

  collisionStart = millis();
  waveStep = 0;

  showCollisionWave();
}

void updateCollision() {
  unsigned long elapsed = millis() - collisionStart;
  int growSteps = ledCount / 2 - waveSize;
  unsigned long growDuration = growSteps * collisionInterval;
  int step;
  
  if (elapsed < growDuration) {
    step = elapsed / collisionInterval;
  } else if (elapsed < growDuration + flashDuration) {
    step = growSteps;
  } else {
    step = growSteps + 1 + (elapsed - growDuration - flashDuration) / collisionInterval;
  }

  if (step >= growSteps + ledCount / 2) {
    collisionActive = false;
    collisionFinished = true;

    setRGB(0, 0, 0);
    return;
  }

  if (step != waveStep) {
    waveStep = step;
    showCollisionWave();
  }
}

void loop() {
  updateButton();

  if (!running) {
    return;
  }

  updateTemperature();
  if (collisionActive) {
    updateCollision();
  } else if (!collisionFinished) {
    updateParticles();
  }

  updateLCD();
}
