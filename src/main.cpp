#include <Arduino.h>
#include <dht11.h>
#include <analogWrite.h>
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
bool isMotionDetected = false; //PIR motion sensor state
bool isLedOn = false; //LED state, kept in sync with both the button and the app commands
int lastRawButtonReading = HIGH; //Raw button reading from the previous loop(), for bounce detection
int stableButtonState = HIGH; //Debounced, accepted button state (active-low)
unsigned long lastButtonChange = 0; //Last time the raw button reading changed
const unsigned long BUTTON_DEBOUNCE_MS = 50; //ms to ignore bouncing after a change

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
    if (dist <= 5) {
        myservo.write(70);
        isBoxOpen = true;
    } else if (dist > 7) {
        myservo.write(180);
        isBoxOpen = false;
    }
}

//Read PIR motion sensor state (instant, no throttling needed)
void updateMotion() {
    isMotionDetected = digitalRead(PIRPIN);
}

//Toggle LED on each button press (active-low, debounced, edge-triggered)
void updateButton() {
    int reading = digitalRead(BUTTONPIN);
    if (reading != lastRawButtonReading) {
        lastButtonChange = millis();
    }
    if (millis() - lastButtonChange > BUTTON_DEBOUNCE_MS && reading != stableButtonState) {
        stableButtonState = reading;
        if (stableButtonState == LOW) {
            isLedOn = !isLedOn;
            digitalWrite(LEDPIN, isLedOn ? HIGH : LOW);
        }
    }
    lastRawButtonReading = reading;
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
    Serial.println(digitalRead(BUTTONPIN));

    //Mirror the key readings on the 16x2 LCD (row addressing hides anything past column 15)
    String line0 = "T:" + String(temperature) + "C H:" + String(humidity) + "%";
    String line1 = "D:" + String((int)distance) + "cm " + (isBoxOpen ? "open" : "closed");
    while (line0.length() < 16) line0 += ' ';
    while (line1.length() < 16) line1 += ' ';
    lcd.setCursor(0, 0);
    lcd.print(line0);
    lcd.setCursor(0, 1);
    lcd.print(line1);
}

//Read temperature and auto-control fan via PWM (on >= FAN_TEMP_THRESHOLD, off otherwise)
void updateFan() {
    if (millis() - lastFanCheck < FAN_CHECK_INTERVAL) {
        return;
    }
    lastFanCheck = millis();

    DHT11.read(DHT11PIN);
    temperature = DHT11.temperature;
    humidity = DHT11.humidity;

    if (temperature >= FAN_TEMP_THRESHOLD) {
        analogWrite(FANPIN1, 100);
        analogWrite(FANPIN2, 0);
    } else {
        analogWrite(FANPIN1, 0);
        analogWrite(FANPIN2, 0);
    }
}

void getSensorsData() {
    //Acquire data
    int chk = DHT11.read(DHT11PIN);
    //Steam sensor
    rainwater = analogRead(RAINWATERPIN);
    //Photoresistor
    light = analogRead(LIGHTPIN);
    //Soil humidity sensor
    soilHumidity = analogRead(SOILHUMIDITYPIN) * 2.3;
    //Water level sensor
    waterLevel = analogRead(WATERLEVELPIN) * 2.5;
    //Temperature
    temperature = DHT11.temperature;
    //Humidity
    humidity = DHT11.humidity;
}

void loop() {
    updateFeedingBox();
    updateFan();
    updateMotion();
    updateButton();
    logSensorData();
}
