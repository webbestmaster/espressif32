#include <Arduino.h>
#include <analogWrite.h>

int brightStep = 1;
int brightness = 0;

void setup() {
}

void loop() {
  brightness += brightStep;
  if ( brightness == 0 || brightness == 255 ) {
    brightStep = -brightStep;
  }

  analogWrite(32, brightness);

  delay(10);
}