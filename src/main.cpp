#include <Arduino.h>
#include <dht11.h>
#include <ESP32_Servo.h>
#include <LiquidCrystal_I2C.h>
#include "BuzzerMusic.h"

//To be displayed
#define DHT11PIN        17  //Temperature and humidity sensor pin
#define RAINWATERPIN    35  //Steam sensor pin
#define LIGHTPIN        34  //Photoresistor pin
#define WATERLEVELPIN   33  //Water level sensor pin
#define SOILHUMIDITYPIN 32  //Soil humidity sensor pin
//To be controlled
#define LEDPIN          27  //LED pin
#define RELAYPIN        25  //Relay pin (to control water pump)
#define SERVOPIN        26  //Servo pin
#define FANPIN1         19  //Fan IN+ pin
#define FANPIN2         18  //Fan IN- pin
#define BUZZERPIN       16  //Buzzer pin
#define TRIGPIN         12  //Ultrasonic sensor trig pin
#define ECHOPIN         13  //Ultrasonic sensor echo pin
#define PIRPIN          23  //PIR motion sensor pin
#define BUTTONPIN       5   //Push button pin (toggles LED)

//Initialize LCD1602, 0x27 is I2C address
LiquidCrystal_I2C lcd(0x27, 16, 2);
dht11 DHT11; //Initialize temperature and humidity sensor
Servo myservo; // create servo object to control a servo
// 16 servo objects can be created on the ESP32

//Define variable as detected values
int temperature; //Temperature
int humidity; //Humidity
int soilHumidity; //Soil humidity
int light; //Brightness
int waterLevel; //Water level
int rainwater; //Rainfall
int duration; //Ultrasonic pulse duration
float distance; //Distance measured by ultrasonic sensor
bool isBoxOpen = false; //Feeding box state, to avoid re-writing servo every loop
unsigned long lastFeedingCheck = 0; //Last time the feeding box distance was checked
const unsigned long FEEDING_CHECK_INTERVAL = 2000; //ms between distance checks
unsigned long lastFanCheck = 0; //Last time the temperature was checked for the fan
const unsigned long FAN_CHECK_INTERVAL = 2000; //ms between temperature checks
const int FAN_TEMP_THRESHOLD = 28; //°C, fan turns on at or above this temperature
unsigned long lastLogCheck = 0; //Last time sensor readings were printed to Serial
const unsigned long LOG_INTERVAL = 2000; //ms between human-readable log lines
unsigned long lastRainCheck = 0; //Last time the steam/rainwater sensor was read
const unsigned long RAIN_CHECK_INTERVAL = 2000; //ms between rainwater checks
unsigned long lastLightCheck = 0; //Last time the photoresistor was read
const unsigned long LIGHT_CHECK_INTERVAL = 2000; //ms between light checks
unsigned long lastWaterLevelCheck = 0; //Last time the water level sensor was read
const unsigned long WATER_LEVEL_CHECK_INTERVAL = 2000; //ms between water level checks
unsigned long lastSoilHumidityCheck = 0; //Last time the soil humidity sensor was read
const unsigned long SOIL_HUMIDITY_CHECK_INTERVAL = 2000; //ms between soil humidity checks
unsigned long lastIrrigationCheck = 0; //Last time the irrigation condition was checked
const unsigned long IRRIGATION_CHECK_INTERVAL = 2000; //ms between irrigation checks
const int SOIL_DRY_THRESHOLD = 500; //soilHumidity <= this is considered dry (needs water)
const int WATER_LEVEL_MIN = 1000; //waterLevel >= this is considered enough water in the reservoir
bool isMotionDetected = false; //PIR motion sensor state
bool isLedOn = false; //LED state, kept in sync with both the button and the app commands
int stableButtonState = HIGH; //Last accepted button state (active-low)
int lcdPage = 0; //Which data block is currently shown on the LCD
const int LCD_PAGE_COUNT = 4; //Number of LCD data blocks, cycled by button press

void Music(); //Forward declaration, defined below setup()
void renderLcdPage(); //Forward declaration, defined below logSensorData()

void setup() {
    Serial.begin(9600);

    //Initialize LCD
    lcd.init();
    // Turn the (optional) backlight off/on
    lcd.backlight();
    //lcd.noBacklight();
    lcd.clear();

    //set pins mode
    pinMode(LEDPIN,OUTPUT);
    pinMode(RAINWATERPIN,INPUT);
    pinMode(LIGHTPIN,INPUT);
    pinMode(SOILHUMIDITYPIN,INPUT);
    pinMode(WATERLEVELPIN,INPUT);
    pinMode(RELAYPIN,OUTPUT);
    digitalWrite(RELAYPIN,LOW); //Pump off by default
    pinMode(FANPIN1,OUTPUT);
    pinMode(FANPIN2,OUTPUT);
    pinMode(BUZZERPIN,OUTPUT);
    pinMode(TRIGPIN,OUTPUT);
    pinMode(ECHOPIN,INPUT);
    pinMode(PIRPIN,INPUT);
    pinMode(BUTTONPIN,INPUT);
    delay(1000);

    // attaches the servo on pin 26 to the servo object
    myservo.attach(SERVOPIN);

    Music(); //Boot chime, confirms the buzzer is wired and working
}


void Music() {
    // iterate over the notes of the melody:
    const int noteCount = sizeof(melody2) / sizeof(melody2[0]);
    for (int thisNote = 0; thisNote < noteCount; thisNote++) {
        // to calculate the note duration, take one second
        // divided by the note type.
        //e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
        int noteDuration = 700 / noteDurations2[thisNote];
        tone(BUZZERPIN, melody2[thisNote], noteDuration);

        // to distinguish the notes, set a minimum time between them.
        // the note's duration + 30% seems to work well:
        int pauseBetweenNotes = noteDuration * 1.30;
        delay(pauseBetweenNotes);
        // stop the tone playing:
        noTone(BUZZERPIN);
    }
}

//Get distance from ultrasonic sensor, in cm
float getDistance() {
    digitalWrite(TRIGPIN,LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGPIN,HIGH);
    delayMicroseconds(10); //Trigger the trig pin via a high level lasting at least 10us
    digitalWrite(TRIGPIN,LOW);
    duration = pulseIn(ECHOPIN,HIGH, 30000); //timeout 30ms (~5m range), avoids blocking loop when no echo
    if (duration == 0) {
        return -1; //no echo (out of range or wiring fault): caller must not treat this as "close"
    }
    distance = duration / 58.0; //convert into distance(cm)
    return distance;
}

//Measure distance and open/close feeding box accordingly (hysteresis: open <=10cm, close >15cm)
void updateFeedingBox() {
    if (millis() - lastFeedingCheck < FEEDING_CHECK_INTERVAL) {
        return;
    }
    lastFeedingCheck = millis();

    float dist = getDistance();
    if (dist < 0) {
        return; //no echo this cycle, keep current box state
    }
    if (dist <= 5) {
        myservo.write(70);
        isBoxOpen = true;
    } else if (dist > 8) {
        myservo.write(180);
        isBoxOpen = false;
    }
}

//Read PIR motion sensor state (instant, no throttling needed)
void updateMotion() {
    isMotionDetected = digitalRead(PIRPIN);
}

//Toggle LED and advance the LCD page on each button press (active-low, edge-triggered)
void updateButton() {
    int reading = digitalRead(BUTTONPIN);
    if (reading != stableButtonState) {
        stableButtonState = reading;
        if (stableButtonState == LOW) {
            isLedOn = !isLedOn;
            digitalWrite(LEDPIN, isLedOn ? HIGH : LOW);
            // if (isLedOn) {
            //     Music(); //Play the tune once when turning on
            // } else {
            //     noTone(BUZZERPIN); //Stop the tune immediately when turning off
            // }

            lcdPage = (lcdPage + 1) % LCD_PAGE_COUNT;
            renderLcdPage(); //Redraw immediately so the switch feels instant
        }
    }
}

//Print current sensor readings in one human-readable line, at most once per LOG_INTERVAL
void logSensorData() {
    if (millis() - lastLogCheck < LOG_INTERVAL) {
        return;
    }
    lastLogCheck = millis();

    Serial.print("Temp: ");
    Serial.print(temperature);
    Serial.print(" C | Hum: ");
    Serial.print(humidity);
    Serial.print(" % | Distance: ");
    Serial.print(distance);
    Serial.print(" cm | Box: ");
    Serial.print(isBoxOpen ? "open" : "closed");
    Serial.print(" | Motion: ");
    Serial.print(isMotionDetected ? "yes" : "no");
    Serial.print(" | LED: ");
    Serial.print(isLedOn ? "on" : "off");
    Serial.print(" | Button raw: ");
    Serial.print(digitalRead(BUTTONPIN));
    Serial.print(" | Rain: ");
    Serial.print(rainwater);
    Serial.print(" | Light: ");
    Serial.print(light);
    Serial.print(" | Water: ");
    Serial.print(waterLevel);
    Serial.print(" | Soil: ");
    Serial.println(soilHumidity);

    renderLcdPage(); //Refresh the currently selected block with the latest values
}

//Draw the data block selected by lcdPage on the 16x2 LCD (row addressing hides anything past column 15)
//Page only changes on button press (see updateButton()); this just repaints the current page.
void renderLcdPage() {
    String line0;
    String line1;
    switch (lcdPage) {
        case 0:
            line0 = "T:" + String(temperature) + "C H:" + String(humidity) + "%";
            line1 = "Box:" + String(isBoxOpen ? "open" : "closed");
            break;
        case 1:
            line0 = "Dist:" + String((int)distance) + "cm";
            line1 = "Motion:" + String(isMotionDetected ? "yes" : "no");
            break;
        case 2:
            line0 = "Light:" + String(light);
            line1 = "Rain:" + String(rainwater);
            break;
        default:
            line0 = "Water:" + String(waterLevel);
            line1 = "Soil:" + String(soilHumidity);
            break;
    }
    //Right-align a "page/total" tag on line0, truncating content if it would collide
    String pageTag = String(lcdPage + 1) + "/" + String(LCD_PAGE_COUNT);
    int contentWidth = 16 - pageTag.length() - 1;
    if ((int)line0.length() > contentWidth) line0 = line0.substring(0, contentWidth);
    while ((int)line0.length() < contentWidth) line0 += ' ';
    line0 += ' ';
    line0 += pageTag;

    while (line1.length() < 16) line1 += ' ';
    lcd.setCursor(0, 0);
    lcd.print(line0);
    lcd.setCursor(0, 1);
    lcd.print(line1);
}

//Read temperature and auto-control fan (on >= FAN_TEMP_THRESHOLD, off otherwise)
void updateFan() {
    if (millis() - lastFanCheck < FAN_CHECK_INTERVAL) {
        return;
    }
    lastFanCheck = millis();

    DHT11.read(DHT11PIN);
    temperature = DHT11.temperature;
    humidity = DHT11.humidity;

    if (temperature >= FAN_TEMP_THRESHOLD) {
        digitalWrite(FANPIN1, HIGH);
        digitalWrite(FANPIN2, LOW);
    } else {
        digitalWrite(FANPIN1, LOW);
        digitalWrite(FANPIN2, LOW);
    }
}

//Read steam/rainwater sensor (conductive traces, higher value = wetter)
void updateRainwater() {
    if (millis() - lastRainCheck < RAIN_CHECK_INTERVAL) {
        return;
    }
    lastRainCheck = millis();

    rainwater = analogRead(RAINWATERPIN);
}

//Read photoresistor (higher value = brighter)
void updateLight() {
    if (millis() - lastLightCheck < LIGHT_CHECK_INTERVAL) {
        return;
    }
    lastLightCheck = millis();

    light = analogRead(LIGHTPIN);
}

//Read water level sensor (higher value = more water)
void updateWaterLevel() {
    if (millis() - lastWaterLevelCheck < WATER_LEVEL_CHECK_INTERVAL) {
        return;
    }
    lastWaterLevelCheck = millis();

    waterLevel = analogRead(WATERLEVELPIN);
}

//Read soil humidity sensor (lower value = drier soil, per kit calibration)
void updateSoilHumidity() {
    if (millis() - lastSoilHumidityCheck < SOIL_HUMIDITY_CHECK_INTERVAL) {
        return;
    }
    lastSoilHumidityCheck = millis();

    soilHumidity = analogRead(SOILHUMIDITYPIN);
}

//Auto-irrigation: pulse the water pump relay when soil is dry and the reservoir has enough water
void updateIrrigation() {
    if (millis() - lastIrrigationCheck < IRRIGATION_CHECK_INTERVAL) {
        return;
    }
    lastIrrigationCheck = millis();

    // if (soilHumidity <= SOIL_DRY_THRESHOLD && waterLevel >= WATER_LEVEL_MIN) {
    //     Serial.println("Irrigation: pump pulse");
    //     digitalWrite(RELAYPIN, HIGH);
    //     delay(400); //irrigation pulse
    //     digitalWrite(RELAYPIN, LOW);
    // }
    if (waterLevel >= WATER_LEVEL_MIN) {
        Serial.println("Irrigation: pump pulse");
        digitalWrite(RELAYPIN, HIGH);
        delay(400); //irrigation pulse
        digitalWrite(RELAYPIN, LOW);
    }
}

void loop() {
    updateFeedingBox();
    updateFan();
    updateMotion();
    updateButton();
    updateRainwater();
    updateLight();
    updateWaterLevel();
    updateSoilHumidity();
    updateIrrigation();
    logSensorData();
}
