#include <Arduino_FreeRTOS.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <string.h>

void TaskCommOpenHAB(void *pvParameters);

//#define DEBUG_PRINT_LIVING

#define LED_NUM_PIXEL_SB 112
#define LED_STRIP_RGBW_DATA_PIN_SB 3
#define LED_NUM_PIXEL_CB 103
#define LED_STRIP_RGBW_DATA_PIN_CB 2
volatile int iStripSB;
volatile int iStripCB;
Adafruit_NeoPixel stripSideboardRGBW(LED_NUM_PIXEL_SB, LED_STRIP_RGBW_DATA_PIN_SB, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel stripCabinetRGBW(LED_NUM_PIXEL_CB, LED_STRIP_RGBW_DATA_PIN_CB, NEO_GRBW + NEO_KHZ800);
void TaskUdateLED(void *pvParameters);
// Parse input from OpenHAB
enum motorState
{
  STOP,
  UP,
  DOWN
};
enum LEDState
{
  OFF,
  ON
};
struct inputState
{
  motorState liftTVMotorState;
  LEDState   sideboardLEDState;
  LEDState   cupBoardLEDState;
};
volatile struct inputState inputOpenHABState = {.liftTVMotorState = STOP, .sideboardLEDState = OFF, .cupBoardLEDState = OFF}; // make STOP default state, OFF default state
// Use the parsed input data
void TaskUdateStatesThroughPushing(void *pvParameters);
// FAN
#define FAN_TACHO 20
#define FAN_PWM 8
volatile unsigned long elapsedTimeInMicrosecondsFAN = 0;
volatile unsigned long elapsedTimeInMicrosecondsFANHistory[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
volatile uint8_t indexElapsedTimeInMicrosecondsFANHistory = 0;
volatile unsigned int targetTempAVR = 25;
volatile unsigned long fanRPM = 0;
// MOTOR TV lift
#define MOTOR_CTRL_RPWM 9
#define MOTOR_CTRL_LPWM 10
#define MOTOR_CTRL_R_EN 48
#define MOTOR_CTRL_L_EN 49
#define MOTOR_CTRL_TACHO 21
#define MOTOR_UP 0
#define MOTOR_DOWN 1
#define TV_LIFT_BOTTOM 0
#define TV_LIFT_TOP 940
#define TV_LIFT_DOWN_SLOW 80
#define TV_LIFT_UP_SLOW 860
volatile unsigned long elapsedPulesMOTOR = 0;
volatile byte activeDirectionMOTOR = 0;
volatile boolean calibrationDone = false;
// DS18B20
#define TEMP_1 43
#define TEMP_2 42
OneWire myWire1(TEMP_1);
OneWire myWire2(TEMP_2);
DallasTemperature sensorDS18B20_1(&myWire1);
DallasTemperature sensorDS18B20_2(&myWire2);
float tempSensorDS18B20_1 = 0;
float tempSensorDS18B20_2 = 0;
// DHT22
#define TEMP_HUM 40
#define TEMP_HUM_TYP DHT22 // DHT11, DHT21, DHT22
DHT myDht(TEMP_HUM, TEMP_HUM_TYP);
String stringTemp1;
String stringTemp2;
String stringTemp3;
String stringHumi1;
// 
uint16_t controlBitVector =0;
void setupFAN()
{
  // Use Atemga Timer 4 PWM for FAN
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4 = 0;
  // Mode 10: phase correct PWM with ICR4 as Top (= F_CPU/2/25000)
  // OC4C as Non-Inverted PWM output
  ICR4 = (F_CPU / 25000) / 2;
  OCR4C = ICR4 / 2; // default: about 50:50
  TCCR4A = _BV(COM4C1) | _BV(WGM41);
  TCCR4B = _BV(WGM43) | _BV(CS40);
  pinMode(FAN_PWM, OUTPUT);
  pinMode(FAN_TACHO, INPUT);
  attachInterrupt(digitalPinToInterrupt(FAN_TACHO), ISR_FAN_TACHO, RISING);
}
//
void setFanSpeed(uint16_t value)
{
  OCR4C = value;
}
//
void ISR_FAN_TACHO()
{
  static unsigned long previousTimeInMicroseconds = 0;
  unsigned long time = micros();
  elapsedTimeInMicrosecondsFAN = time - previousTimeInMicroseconds;
  previousTimeInMicroseconds = time;
  elapsedTimeInMicrosecondsFAN++;
}
//
unsigned long getMean(unsigned long *val, uint8_t arrayCount)
{
  unsigned long total = 0;
  for (uint8_t i = 0; i < arrayCount; i++)
  {
    total = total + val[i];
  }
  unsigned long avg = total / (unsigned long)arrayCount;
  return avg;
}
//
void setupTVLift()
{
  // Motor controller
  pinMode(MOTOR_CTRL_RPWM, OUTPUT);    // Pin ist Ausgang
  pinMode(MOTOR_CTRL_LPWM, OUTPUT);    // Pin ist Ausgang
  pinMode(MOTOR_CTRL_R_EN, OUTPUT);    // Pin ist Ausgang
  pinMode(MOTOR_CTRL_L_EN, OUTPUT);    // Pin ist Ausgang
  digitalWrite(MOTOR_CTRL_R_EN, HIGH); // Treiber aktivieren (Enable)
  digitalWrite(MOTOR_CTRL_L_EN, HIGH); // Treiber aktivieren (Enable)
  pinMode(MOTOR_CTRL_TACHO, INPUT);
  attachInterrupt(digitalPinToInterrupt(MOTOR_CTRL_TACHO), ISR_MOTOR_CTRL_TACHO, RISING);
  TCCR2B = TCCR2B & 0b11111000 | 0x01; // sets Arduino Mega's pin 10 and 9 to frequency 31250.
}
//
void driveMotor(byte upDown, byte velocity)
{
  if (upDown == MOTOR_UP)
  {
    activeDirectionMOTOR = MOTOR_UP;
    digitalWrite(MOTOR_CTRL_RPWM, LOW);
    digitalWrite(MOTOR_CTRL_LPWM, HIGH);
    analogWrite(MOTOR_CTRL_RPWM, 0);
    analogWrite(MOTOR_CTRL_LPWM, velocity);
  }
  if (upDown == MOTOR_DOWN)
  {
    activeDirectionMOTOR = MOTOR_DOWN;
    digitalWrite(MOTOR_CTRL_RPWM, HIGH);
    digitalWrite(MOTOR_CTRL_LPWM, LOW);
    analogWrite(MOTOR_CTRL_RPWM, velocity);
    analogWrite(MOTOR_CTRL_LPWM, 0);
  }
}
//
void ISR_MOTOR_CTRL_TACHO()
{
  if (activeDirectionMOTOR == MOTOR_UP)
  {
    elapsedPulesMOTOR++;
    // reduce speed up
    if (elapsedPulesMOTOR > TV_LIFT_UP_SLOW)
      driveMotor(MOTOR_UP, 120);
    // stop up
    if (elapsedPulesMOTOR >= TV_LIFT_TOP)
      driveMotor(MOTOR_UP, 0);
  }
  if (activeDirectionMOTOR == MOTOR_DOWN)
  {
    elapsedPulesMOTOR--;
    // reduce speed down
    if (elapsedPulesMOTOR < TV_LIFT_DOWN_SLOW)
      driveMotor(MOTOR_DOWN, 100);
    // stop down
    if (elapsedPulesMOTOR <= TV_LIFT_BOTTOM)
      driveMotor(MOTOR_DOWN, 0);
  }
}
//
void setupSensors(void)
{
  // DS18B20
  sensorDS18B20_1.begin();
  sensorDS18B20_1.setResolution(12); // Genauigkeit auf 12-Bit setzen
  sensorDS18B20_2.begin();
  sensorDS18B20_2.setResolution(12); // Genauigkeit auf 12-Bit setzen
  // DHT
  myDht.begin();
}
void setup()
{
  cli();
  myDht.begin();
  // Setup SerialCommand port
  Serial.begin(115200);
  Serial.setTimeout(15); // 5ms for readString otherwise the call takes 250ms
  xTaskCreate(TaskCommOpenHAB, "TaskOpenHAB", 256, NULL, 3, NULL);
  xTaskCreate(TaskUdateStatesThroughPushing, "TaskUdateStatesThroughPushing", 256, NULL, 2, NULL);
  xTaskCreate(TaskUdateLED, "TaskUdateLed", 256, NULL, 2, NULL);
  xTaskCreate(TaskUdateStatesThroughPulling, "TaskUdateStatesThroughPulling", 512, NULL, 1, NULL);
  setupFAN();
  setFanSpeed(320);
  setupTVLift();
  setupSensors();
  stripSideboardRGBW.begin();
  stripCabinetRGBW.begin();
  pinMode(LED_STRIP_RGBW_DATA_PIN_SB, OUTPUT);
  pinMode(LED_STRIP_RGBW_DATA_PIN_CB, OUTPUT);
  sei();
  vTaskStartScheduler(); // start scheduler
}

void loop() {}

// Perform an action every 40 ms.
void TaskCommOpenHAB(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  String strTmp = "0123456789";
  while (1)
  {
    int numCharsReadyToRead = Serial.available();
    if (numCharsReadyToRead > 9)
    {
      for (uint8_t i = 0; i < 9; i++)
      {
        strTmp[i] = (char)Serial.read();
      }
      // this flushes remaining artifacts in the receive buffer
      while (Serial.available())
      {
        char inChar = (char)Serial.read();
      }
      strTmp[9] = '\0';
      // Check if received string is a valid command
      if (strTmp[0] == '#' && strTmp[8] == '$')
      {
#ifdef DEBUG_PRINT_LIVING
        Serial.print("TaskCommOpenHAB: We received valid:");
        Serial.println(strTmp);
        Serial.print("Total buffer sizes was: ");
        Serial.println(numCharsReadyToRead);
#endif
        if (strTmp == "#MO=UPWA$")
        {
          inputOpenHABState.liftTVMotorState = UP;
          controlBitVector ^= 1 << 0;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #MO=UPWA$");
#endif
        }
        else if (strTmp == "#MO=STOP$")
        {
          inputOpenHABState.liftTVMotorState = STOP;
          controlBitVector ^= 1 << 1;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #MO=STOP$");
#endif
        }
        else if (strTmp == "#MO=DOWN$")
        {
          inputOpenHABState.liftTVMotorState = DOWN;
          controlBitVector ^= 1 << 2;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #MO=DOWN$");
#endif
        }
        else if (strTmp == "#LED1=OF$")
        {
          inputOpenHABState.sideboardLEDState = OFF;
          controlBitVector ^= 1 << 3;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #LED1=OF$");
#endif
        }
        else if (strTmp == "#LED1=ON$")
        {
          inputOpenHABState.sideboardLEDState = ON;
          controlBitVector ^= 1 << 4;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #LED1=ON$");
#endif
        }
        else if (strTmp == "#LED2=OF$")
        {
          inputOpenHABState.cupBoardLEDState = OFF;
          controlBitVector ^= 1 << 5;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #LED1=OF$");
#endif
        }
        else if (strTmp == "#LED2=ON$")
        {
          inputOpenHABState.cupBoardLEDState = ON;
          controlBitVector ^= 1 << 6;
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #LED1=ON$");
#endif
        }
        else if (strTmp.substring(0, 5) == "#CLB=")
        {
#ifdef DEBUG_PRINT_LIVING
          Serial.println("Update: #CLB=");
#endif
          elapsedPulesMOTOR = strTmp.substring(5, 8).toInt();
          calibrationDone = true;
          controlBitVector ^= 1 << 7;
        }
      }
      // otherwise do nothing gets overwritten anyway
      else
      {
        controlBitVector ^= 1 << 15;
#ifdef DEBUG_PRINT_LIVING
        Serial.print("TaskCommOpenHAB: We received not valid: ");
        Serial.println(strTmp);
        Serial.print("Total buffer sizes was: ");
        Serial.println(numCharsReadyToRead);
        strTmp = "----------";
#endif
      }
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
  }
}

// Perform state update upon parsed input every 20ms.
void TaskUdateStatesThroughPushing(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  boolean changedStateMotor = false;
  unsigned long mean = 0;
  motorState oldMotorState = inputOpenHABState.liftTVMotorState;
  boolean changedStateCupboardLED = false;
  boolean changedStateSideboardLED = false; 
  LEDState oldCupboardLEDState = inputOpenHABState.cupBoardLEDState;
  LEDState oldSideboardLEDState = inputOpenHABState.sideboardLEDState;
  while (1)
  {
    if (oldMotorState != inputOpenHABState.liftTVMotorState)
    {
      changedStateMotor = true;
    }
    oldMotorState = inputOpenHABState.liftTVMotorState;
    switch (inputOpenHABState.liftTVMotorState)
    {
    case 0: // STOP
#ifdef DEBUG_PRINT_LIVING
      Serial.println("Motor STOP");
#endif
      if (changedStateMotor)
      {
        stripCabinetRGBW.setPixelColor(1, stripCabinetRGBW.Color(0, 255, 0, 0));
        driveMotor(MOTOR_UP, 0);
        driveMotor(MOTOR_DOWN, 0);
        stripCabinetRGBW.show();
        changedStateMotor = false;
      }
      break;
    case 1: // UP
#ifdef DEBUG_PRINT_LIVING
      Serial.println("MOTOR UP");
#endif
      if (changedStateMotor)
      {
        stripCabinetRGBW.setPixelColor(1, stripCabinetRGBW.Color(255, 0, 0, 0));
        driveMotor(MOTOR_UP, 250);
        stripCabinetRGBW.show();
        changedStateMotor = false;
      }
      break;
    case 2: // DOWN
#ifdef DEBUG_PRINT_LIVING
      Serial.println("MOTOR DOWN");
#endif
      if (changedStateMotor)
      {
        stripCabinetRGBW.setPixelColor(1, stripCabinetRGBW.Color(0, 0, 255, 0));
        driveMotor(MOTOR_DOWN, 190);
        stripCabinetRGBW.show();
        changedStateMotor = false;
      }
      break;
    }
    // Compute FAN rpm
    elapsedTimeInMicrosecondsFANHistory[indexElapsedTimeInMicrosecondsFANHistory] = elapsedTimeInMicrosecondsFAN;
    indexElapsedTimeInMicrosecondsFANHistory++;
    if (indexElapsedTimeInMicrosecondsFANHistory > 19)
      indexElapsedTimeInMicrosecondsFANHistory = 0;
    mean = getMean(elapsedTimeInMicrosecondsFANHistory, 20);
    fanRPM = 0;
    if (mean != elapsedTimeInMicrosecondsFAN && elapsedTimeInMicrosecondsFAN != 0)
    {
      fanRPM = 30000000 / elapsedTimeInMicrosecondsFAN;
    }
    //Led
    if (oldCupboardLEDState != inputOpenHABState.cupBoardLEDState)
    {
      changedStateCupboardLED = true;
    }
    if (oldSideboardLEDState != inputOpenHABState.sideboardLEDState)
    {
      changedStateSideboardLED = true;
    }
    oldCupboardLEDState = inputOpenHABState.cupBoardLEDState;
    oldSideboardLEDState = inputOpenHABState.sideboardLEDState;
    switch (inputOpenHABState.sideboardLEDState)
    {
    case 0: // OFF
#ifdef DEBUG_PRINT_LIVING
      Serial.println("State: #LED1=OF$");
#endif
      if (changedStateSideboardLED)
      {
        clearAllPixelSB();
        stripSideboardRGBW.show();
        changedStateSideboardLED = false;
      }
      break;
    case 1: // ON
#ifdef DEBUG_PRINT_LIVING
      Serial.println("State: #LED1=ON$");
#endif
      if (changedStateSideboardLED)
        {
        for (uint16_t i = 0; i < LED_NUM_PIXEL_SB; i++)
        {
          stripSideboardRGBW.setPixelColor(i, stripSideboardRGBW.Color(0, 0, 0, 128));
        }
        stripSideboardRGBW.show();
        changedStateSideboardLED = false;
      }
      break;
    }

    switch (inputOpenHABState.cupBoardLEDState)
    {
    case 0: // OFF
#ifdef DEBUG_PRINT_LIVING
      Serial.println("State: #LED2=OF$");
#endif
      if (changedStateCupboardLED)
      {
        clearAllPixelCB();
        stripCabinetRGBW.show();
        changedStateCupboardLED = false;
      }
      break;
    case 1: // ON
#ifdef DEBUG_PRINT_LIVING
        Serial.println("State: #LED2=ON$");
#endif
      if (changedStateCupboardLED)
      {
        for (uint16_t i = 0; i < LED_NUM_PIXEL_CB; i++)
        {
          stripCabinetRGBW.setPixelColor(i, stripCabinetRGBW.Color(0, 0, 0, 128));
        }
        stripCabinetRGBW.show();
        changedStateCupboardLED = false;
      }
      break;
    }
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(80));
  }
}
///
void clearAllPixelSB()
{
  int i;
  for (i = 0; i < LED_NUM_PIXEL_SB; i++)
  {
    stripSideboardRGBW.setPixelColor(i, stripSideboardRGBW.Color(0, 0, 0, 0));
  }
}
///
void clearAllPixelCB()
{
  int i;
  for (i = 0; i < LED_NUM_PIXEL_CB; i++)
  {
    stripCabinetRGBW.setPixelColor(i, stripCabinetRGBW.Color(0, 0, 0, 0));
  }
}
// Perform an action every 50 ms.
void TaskUdateLED(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  // Init RGBW strip
  iStripCB = 0;
  iStripSB = 0;
  while (1)
  {
/*
    clearAllPixelCB();
    clearAllPixelSB();
    stripCabinetRGBW.show();
    stripSideboardRGBW.show();
    stripSideboardRGBW.setPixelColor(iStripSB, stripSideboardRGBW.Color(5, 5, 5, 128));
    stripCabinetRGBW.setPixelColor(iStripCB, stripCabinetRGBW.Color(5, 5, 5, 128));
    iStripSB++;
    iStripCB++;
    stripCabinetRGBW.show();
    stripSideboardRGBW.show();
    if (iStripSB == LED_NUM_PIXEL_SB - 1)
    {
      iStripSB = 0;
      clearAllPixelSB();
      stripSideboardRGBW.show();
    }
    if (iStripCB == LED_NUM_PIXEL_CB - 1)
    {
      iStripCB = 0;
      clearAllPixelCB();
      stripCabinetRGBW.show();
    }
*/
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(80));
  }
}
// Perform an action every 2000 ms.
void TaskUdateStatesThroughPulling(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    // Get Sensor values
    sensorDS18B20_1.requestTemperatures();
    sensorDS18B20_2.requestTemperatures();
    tempSensorDS18B20_1 = sensorDS18B20_1.getTempCByIndex(0);
    tempSensorDS18B20_2 = sensorDS18B20_2.getTempCByIndex(0);
    stringTemp1 = String(tempSensorDS18B20_1, 2);
    stringTemp2 = String(tempSensorDS18B20_2, 2);
    stringHumi1 = String(myDht.readHumidity(), 2);
    stringTemp3 = String(myDht.readTemperature(), 2);
    // Temp control for FAN
    if (tempSensorDS18B20_1 > targetTempAVR)
    {
      setFanSpeed(320);   // 320 max
      targetTempAVR = 24; // ca. 1500rpm
    }
    else
    {
      setFanSpeed(100); // ca. 500rpm
      targetTempAVR = 25;
    }
    // Otput to OpenHab
    Serial.print("TP1=");
    Serial.print(stringTemp1);
    Serial.print(";TP2=");
    Serial.print(stringTemp2);
    Serial.print(";TP3=");
    Serial.print(stringTemp3);
    Serial.print(";HU3=");
    Serial.print(stringHumi1);
    Serial.print(";TF=");
    Serial.print(fanRPM, DEC);
    Serial.print(";");
    if (calibrationDone)
    {
      Serial.print("TL=");
      Serial.print(elapsedPulesMOTOR, DEC);
      Serial.print(";");
    }
    Serial.print("CTR=");
    Serial.print(controlBitVector, DEC);
    Serial.println(";");
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(3230));
  }
}