#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

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

const char *ssid = "TP-Link_C36C";
const char *pwd = "36856008";

//Initialize LCD1602, 0x27 is I2C address
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiServer server(80); //Initialize wifi server
dht11 DHT11; //Initialize temperature and humidity sensor
Servo myservo; // create servo object to control a servo
// 16 servo objects can be created on the ESP32

//Define variable as detected values
String request;
String dataBuffer;
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

void setup() {
    Serial.begin(9600);
    //Connect to wifi
    WiFi.begin(ssid, pwd);
    //Determine whether connected
    Serial.println("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    delay(1000);
    //Serial monitor prints wifi name and IP address
    Serial.println("Connected to WiFi");
    Serial.print("WiFi NAME:");
    Serial.println(ssid);
    Serial.print("IP:");
    Serial.println(WiFi.localIP());

    //Initialize LCD
    lcd.init();
    // Turn the (optional) backlight off/on
    lcd.backlight();
    //lcd.noBacklight();
    lcd.clear();
    //Set the position of cursor
    lcd.setCursor(0, 0);
    //LCD prints
    lcd.print("IP:");
    //Set the position of cursor
    lcd.setCursor(0, 1);
    //LCD prints
    lcd.print(WiFi.localIP());

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
    delay(1000);

    // attaches the servo on pin 26 to the servo object
    myservo.attach(SERVOPIN);

    //Start server
    server.begin();
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

//Convert data into percentage
String dataHandle(int data) {
    // Convert analog values into percentage
    int percentage = (data / 4095.0) * 100;
    // If the converted percentage is greater than 100, output 100.
    percentage = percentage > 100 ? 100 : percentage;
    // Six characters store hexadecimal strings, one character is as terminators
    char hexString[3];
    // Convert hexadecimal values to 6-digit hexadecimal strings, add leading zeros: 0 is 00, 1 is 01...
    sprintf(hexString, "%02X", percentage);

    return hexString;
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

    if (isMotionDetected != 0) {
        Serial.print(isMotionDetected);
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
    Serial.print(" C | Distance: ");
    Serial.print(distance);
    Serial.print(" cm | Box: ");
    Serial.print(isBoxOpen ? "open" : "closed");
    Serial.print(" | Motion: ");
    Serial.println(isMotionDetected ? "yes" : "no");
}

//Read temperature and auto-control fan via PWM (on >= FAN_TEMP_THRESHOLD, off otherwise)
void updateFan() {
    if (millis() - lastFanCheck < FAN_CHECK_INTERVAL) {
        return;
    }
    lastFanCheck = millis();

    DHT11.read(DHT11PIN);
    temperature = DHT11.temperature;

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
    logSensorData();

    //Check whether a client is connected to the web server
    //When the client is connected to server, "server.available()" returns a WiFiClient object for communication at client-side.
    WiFiClient client = server.available();
    if (client) {
        Serial.println("New client connected");
        while (client.connected()) {
            //Determine whether the server sends data
            if (client.available()) {
                request = client.readStringUntil('s');
                Serial.print("Received message: ");
                Serial.println(request);
            }
            //Acquire all senser data
            getSensorsData();
            //put all data into "dataBuffer"
            dataBuffer = "";
            dataBuffer += String(temperature,HEX);
            dataBuffer += String(humidity,HEX);
            dataBuffer += dataHandle(soilHumidity);
            dataBuffer += dataHandle(light);
            dataBuffer += dataHandle(waterLevel);
            dataBuffer += dataHandle(rainwater);
            //Send data to server, transmit to APP
            client.print(dataBuffer);
            delay(500);

            //LED
            if (request == "a") {
                digitalWrite(LEDPIN,HIGH);
            } else if (request == "A") {
                digitalWrite(LEDPIN,LOW);
            }
            //Irrigation
            else if (request == "b") {
                digitalWrite(RELAYPIN,HIGH);
                delay(400); //Irrigation delay
                digitalWrite(RELAYPIN,LOW);
                delay(650);
            }
            //Fan is auto-controlled by temperature, see updateFan()
            //Feeding box
            else if (request == "d") {
                //Servo rotates to 180В°, open feeding box
                myservo.write(80);
                delay(500);
            } else if (request == "D") {
                //Servo rotates to 80В°, close feeding box
                myservo.write(180);
            }
            //Music
            else if (request == "e") {
                Music();
            }
            request = "";
        }
        Serial.println("Client disconnected");
    }
}
