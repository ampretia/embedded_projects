// Apacahe-2.0
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <Wire.h>
// #include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>

// bool DEBUG = false;
bool DEBUG = true;


void logln(const char* msg) {
  if (DEBUG)
    Serial.println(msg);
}

void log(const char* msg) {
  if (DEBUG)
    Serial.print(msg);
}

/** https://www.adafruit.com/product/2946 */
Adafruit_IS31FL3731_Wing matrix = Adafruit_IS31FL3731_Wing();

/** https://github.com/JChristensen/movingAvg */
#include <movingAvg.h>

/** 
  *Custom numeric display font 
*/
int digits[10][7][3] = {
  { { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 } },  //0
  { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } },  //1
  { { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 1, 1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 1, 1 } },  //2
  { { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 1, 1 } },  //3
  { { 1, 0, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } },  //4
  { { 1, 1, 1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 1, 1 } },  //5
  { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 } },  //6
  { { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } },  //7
  { { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 } },  //8
  { { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 1, 1 } }   //9
};



/** WiFi configuration */
const char* ssid = "BT-PJAH9R";
const char* password = "yuKC3LUd4avCrh";

const int MAX_BRIGHTNESS = 50;
const int ldrPin = A0;

WiFiClient espClient;
IPAddress ip;

// Define NTP Client to get time; want to get UTC format
// so offset is 0
WiFiUDP ntpUDP;
const long utcOffsetInSeconds = 0;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

uint8_t count = 0;
movingAvg lightLevelAvg(10);

uint8_t current_hour = 0;

// ----------------------------------------------------------------------------------------
/** WiFi Setup */
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  logln("WIFI");
  logln(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    log(".");
  }

  ip = WiFi.localIP();
  logln(ip.toString().c_str());
}

/** Primary setup fn */
void setup() {
  pinMode(BUILTIN_LED, OUTPUT);  // Initialize the BUILTIN_LED pin as an output
  pinMode(ldrPin, INPUT);        //initialize the LDR pin as an input

  // active low, turn off the LED
  digitalWrite(BUILTIN_LED, HIGH);

  if (DEBUG)
    Serial.begin(115200);

  // WiFI, TimeClient and Display setup
  setup_wifi();
  timeClient.begin();

  // Away the display matrix
  while (!matrix.begin()) {
    delay(500);
    log("x");
  }
  logln("IS31 Found!");

  // Start the rolling average
  lightLevelAvg.begin();

  // Setup the local timezone
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
}

/**
 * Draw digit i at position on display
 * @param i 0-9
 * @param position Position 1, 2, 3, 4 on led matrix
*/
void digit(uint8_t brightness, uint8_t i, uint8_t position) {
  uint8_t xoffset = 1;
  // position one is a special case; only ever 1 so can be at the
  // edge of the display

  if (position == 1) {
    for (uint8_t y = 0; y < 7; y++) {
      matrix.drawPixel(0, y, i * (brightness));
    }
    return;
  } else if (position == 2) {
    xoffset = 2;
  } else if (position == 3) {
    xoffset = 8;
  } else {
    xoffset = 12;
  }

  // loop over font arrays
  for (uint8_t x = 0; x < 3; x++) {
    for (uint8_t y = 0; y < 7; y++) {
      matrix.drawPixel(x + xoffset, y, digits[i][y][x] * brightness);
    }
  }
}

/** Request NTP time
 * Reference TZ Environment varaible at https://pubs.opengroup.org/onlinepubs/007908799/xbd/envvar.html
 * LocalTimet to convert to timezone https://pubs.opengroup.org/onlinepubs/007908799/xsh/localtime.html
 */
void updateTime(uint8_t brightness) {
  // NTP update
  timeClient.update();

  // Convert to local time zone
  time_t epoch = timeClient.getEpochTime();
  struct tm* t3 = localtime(&epoch);

  // Convert 24 t0 12 hours
  int h = (t3->tm_hour) % 12;
  if (h == 0) {
    h = 12;
  }

  // set the hours on the matrix, only include the
  // leading one if needed
  if (h > 9) {
    digit(brightness, 1, 1);
  } else {
    digit(brightness, 0, 1);
  }
  digit(brightness, h % 10, 2);

  // minutes
  digit(brightness, (t3->tm_min) % 10, 4);
  digit(brightness, (t3->tm_min) / 10, 3);

  current_hour = t3->tm_hour;
}

// -----------------------------------------------------------------------------------------
/** Primary event loop */
void loop() {

  uint8_t brightness = 1;

  // enforce NIGHT mode
  bool night = current_hour >= 22 || current_hour < 7;
  if (night) {
    brightness = 1;
  } else {
    // Adjust the brightness
    int ldrStatus = analogRead(ldrPin);

    // moving average of the raw value
    brightness = map(lightLevelAvg.reading(ldrStatus), 1, 255, 1, MAX_BRIGHTNESS);
  }

  // reduce the frequency of the ntp request; doesn't need to be that often
  // really
  if (count > 10) {
    updateTime(brightness);
  }

  // and the separator, second interval(ish) flashing
  matrix.drawPixel(6, 2, brightness);
  matrix.drawPixel(6, 4, brightness);
  delay(500);

  // and off again
  matrix.drawPixel(6, 2, 0);
  matrix.drawPixel(6, 4, 0);
  delay(500);
}
