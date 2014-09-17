

/*************************************************************************
 * OpenBitcoinATM
 * (ver. 1.5.4)
 * 
 * MIT Licence (MIT)
 * Copyright (c) 1997 - 2014 John Mayo-Smith for Federal Digital Coin Corporation
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * OpenBitcoinATM is the first open-source Bitcoin automated teller machine for
 * experimentation and education. 
 * 
 * This application, counts pulses from a Pyramid Technologies Apex 5000
 * series bill acceptor and interfaces with the Adafruit 597 TTL serial Mini Thermal 
 * Receipt Printer.
 * 
 * 
 * References
 * -----------
 * John Mayo-Smith: https://github.com/mayosmith
 * 
 * Here's the A2 Micro panel thermal printer --> http://www.adafruit.com/products/597
 * 
 * Here's the bill accceptor --> APEX 5400 on eBay http://bit.ly/MpETED
 * 
 * Peter Kropf: https://github.com/pkropf/greenbacks
 *
 * Thomas Mayo-Smith:http://www.linkedin.com/pub/thomas-mayo-smith/63/497/a57
 *
 * Additional reference
 * https://github.com/adafruit/Adafruit-PCD8544-Nokia-5110-LCD-library
 * https://github.com/Cathedrow/Cryptosuite
 * http://rosettacode.org/wiki/Bitcoin/public_point_to_address
 * https://github.com/sparkfun/Thermal_Printer/tree/master/QRprint/QRprint
 ************************************************************************
 */

//This implementation requires a Mega and a PCD8544-Nokia5110 screen in place of the Uno and Thermal Printer
//It generates private keys and qr codes on the fly with no SD card required. It takes more RAM though, so you'll need the Mega.
//Stephen R. Williams, 2014

#include <SoftwareSerial.h>
#include <Wire.h>
#include "RTClib.h"
#include <SPI.h>
#include <qrbits.h>
#include <qrencode.h>
#include <frame.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <sha256.h>

const byte DOLLAR_PULSE PROGMEM = 4; //pulses per dollar
const short PULSE_TIMEOUT PROGMEM = 2000; //ms before pulse timeout
const short TIMEZONE_ADJUSTMENT PROGMEM = 300;
const unsigned char VERSION PROGMEM = 3;
const unsigned char unWidth PROGMEM = VERSION*4+17; //width of QR code used
const byte SCREEN_WIDTH PROGMEM =84;
const byte SCREEN_HEIGHT PROGMEM =48;
const byte SHA256_DIGEST_LENGTH PROGMEM = 32;
const short COIN_VER PROGMEM = 0x80;

short pulseCount = 0;
unsigned long pulseTime;

//char PROGMEM alphabet[] = "123456789" "ABCDEFGHJKLMNPQRSTUVWXYZ" "abcdefghijkmnopqrstuvwxyz";

// Software SPI (slower updates, more flexible pin options):
// pin 7 - Serial clock out (SCLK)
// pin 6 - Serial data out (DIN)
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)
Adafruit_PCD8544 display = Adafruit_PCD8544(7, 6, 5, 4, 3);

// Hardware SPI (faster, but must use certain hardware pins):
// SCK is LCD serial clock (SCLK) - this is pin 13 on Arduino Uno
// MOSI is LCD DIN - this is pin 11 on an Arduino Uno
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)
// Adafruit_PCD8544 display = Adafruit_PCD8544(5, 4, 3);
// Note with hardware SPI MISO and SS pins aren't used but will still be read
// and written to during SPI transfer.  Be careful sharing these pins!

void setup(){
  Serial.begin(9600); //baud rate for serial monitor
  attachInterrupt(0, onPulse, RISING); //interupt for Apex bill acceptor pulse detect
  pinMode(2, INPUT); //for Apex bill acceptor pulse detect 
  pinMode(10, OUTPUT); //Slave Select Pin #10 on Uno
  Wire.begin();

  //for DS1307 Real Time Clock Breakout Board Kit 
  pinMode(18, OUTPUT);      
  digitalWrite(18, LOW);
  pinMode(19, OUTPUT);          
  digitalWrite(19, HIGH);

  //start up the display
  display.begin();
  display.setContrast(60);

  Serial.print(F("Program loaded. RAM:"));
  Serial.println(freeram());

  // draw multiple rectangles as a screen test. takes no additional SRAM.
  testfillrect();
  display.display();
  delay(100);

  /*
  display.clearDisplay();
   display.setTextSize(1);
   display.setTextColor(BLACK);
   display.setCursor(0,0);
   display.println("0123456789ABCD0123456789ABCD0123456789ABCD0123456789ABCD0123456789ABCD0123456789ABCD");
   display.display();
   delay(10000);
   */

  //test QR code
  getNextBitcoin(1);

  //Serial.println();
  Serial.print(F("Parameters set. RAM:"));
  Serial.println(freeram());
}

void loop(){
  if(pulseCount == 0)
    return;

  Serial.print(F("Pulse Counter: "));
  Serial.println(pulseCount); 
  //Serial.println();
  //int valloop = digitalRead(2);
  //Serial.print(valloop);
  //Serial.println();
  //Serial.print("millis() at: ");
  //Serial.println(millis());

  delay(25);//This seems to really help with pulse timeout reliability.

  if((millis() - pulseTime) < PULSE_TIMEOUT) //if time since start of last onPulse() < 6 secs
    return;

  Serial.print(F("Pulse Timeout at "));
  Serial.println(pulseTime);   

  if(   (pulseCount == DOLLAR_PULSE)
    ||    (pulseCount == (DOLLAR_PULSE * 5))
    ||    (pulseCount == (DOLLAR_PULSE * 10))
    ||    (pulseCount == (DOLLAR_PULSE * 20))
    ||    (pulseCount == (DOLLAR_PULSE * 50))
    ||    (pulseCount == (DOLLAR_PULSE * 100)))
    getNextBitcoin(pulseCount / 4); //dollar baby!

  pulseCount = 0; // reset pulse count
  pulseTime = 0;
}

/*****************************************************
 * onPulse
 * - read 50ms pulses from Apex Bill Acceptor.
 * - 4 pulses indicates one dollar accepted
 * 
 ******************************************************/

void onPulse(){
  //Serial.println("onPulse() function entered");
  short val = digitalRead(2);

  //Serial.print("Digital Read value is: ");
  //Serial.println(val);
  if(val == HIGH)
    pulseCount++;
  //Serial.print("Pulse Counter: ");
  //Serial.println(pulseCount); 
  //Serial.println();
  //Serial.print("pulseTime: ");
  //Serial.println(pulseTime);
  pulseTime = millis(); //set pulseTime to the ms since program start
}

/*****************************************************
 * getNextBitcoin
 * - generates private key, encrypts, encodes, and displays it
 * 
 ******************************************************/

void getNextBitcoin(char denom){
  //display denomination confirmation
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0,0);   
  display.print(F(" $"));
  display.print((byte)denom);
  display.print(F(" detected. \n  Thank you, \n   valued \n  consumer!"));
  display.display();
  //TODO: display time of transaction on the screen.
  delay(5000);
  
  //create private key
  char toHash[32];
  toHashBuilder(toHash, denom);
  
  Serial.print(F("toHash bytes: "));
  for (byte i2 = 0; i2<=31; i2++) { 
    Serial.print((signed char) toHash[i2]);
    Serial.print(" ");
  }
  
  char * privKey=coin_encode((char *)toHash, 0);
  Serial.print(F("WIF: "));
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0,0);  
  for (byte pki=0; pki<=50; pki++) {
    Serial.print(privKey[pki]);
    display.print(privKey[pki]);  
  }
  Serial.println(F(""));
  display.display();
  Serial.print(F("Encoding key into QR code. RAM:"));
  Serial.println(freeram());
  delay(10000);
  encodeQR(privKey);//encodes and displays private key in QR code format.
  delay(45000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0,0);  
  display.print("READY"); 
  display.display();
}

/*****************************************************
 * toHashBuilder
 * builds char array to be SHA-256 hashed from determined values and time
 * in future implementation, gps stats will be added.
 * 
 ******************************************************/

void toHashBuilder(char * toHash, byte denom) {
  unsigned char SERIAL_NUMBER[] PROGMEM = {//this serial number identifies the unit. it should begin with F to prevent leading zeroes.
  //"FF0154D4B792D4D69C62219F10"//this is a 13-byte number, the value is hashed against the time for the private key.
  //102, 102,  48,  49,  53,  52, 100,  52,  98,  55,  57,  50, 100
  0xff, 0x01, 0x54, 0xd4, 0xb7, 0x92, 0xd4, 0xd6, 0x9c, 0x62, 0x21, 0x7a, 0x55
  };
  unsigned char OPERATOR_NUMBER[] PROGMEM = {//this identifies the owner of the unit
  102, 102, 102
  };
  char PROGMEM gpsHeading[] = {//static, future implementation
    102
  };
  char PROGMEM gpsLocX[] = {//static, future implementation
    102, 102, 102
  };
  char PROGMEM gpsLocY[] = {//static, future implementation
    102, 102, 102
  };
  char PROGMEM cryptoCurrencyType[] = {//use to switch between other cryto-currencies like Litecoin or Feathercoin. future implementation
    "0001"//designating 0x0001 as Bitcoin until ISO4217 assigns a number.
  };
  char PROGMEM fiatCurrencyType[] = {//switch governmenet currency inserted. future implementation.
    "0348" //840 = USD. see ISO4217 http://en.wikipedia.org/wiki/ISO_4217
  };

  for (byte i = 0; i<=12; i++)     
    toHash[i]=SERIAL_NUMBER[i];
  for (byte i = 13; i<=15; i++)    
    toHash[i]=OPERATOR_NUMBER[i-13];
  toHash[16]=gpsHeading[0];
  for (byte i = 17; i<=19; i++)    
    toHash[i]=gpsLocX[i-17];
  for (byte i = 20; i<=22; i++)    
    toHash[i]=gpsLocY[i-20];
  for (byte i = 23; i<=24; i++)    
    toHash[i]=cryptoCurrencyType[i-23];
  for (byte i = 25; i<=26; i++)    
    toHash[i]=fiatCurrencyType[i-25];
  toHash[27]=denom;
  long txnTime = getUTC();
  txnTime = txnTime / 60;//for minute rounding when i can't afford one code per second.
  txnTime += TIMEZONE_ADJUSTMENT; //adjustment for CST
  Serial.print(F("Transaction minute in utc/60: "));
  Serial.println(txnTime);
  //bitwise operations push the long value (4 bytes) into 4 locations of the char (single byte each) array
  toHash[28] = (int)((txnTime >> 24) & 0xFF) ;
  toHash[29] = (int)((txnTime >> 16) & 0xFF) ;
  toHash[30] = (int)((txnTime >> 8) & 0XFF);
  toHash[31] = (int)((txnTime & 0XFF));

  Serial.println("");
  Serial.print(F("Finished toHashBuilder. RAM:"));
  Serial.println(freeram());
}

/*****************************************************
 * encodeQR()
 * creates QR code from char * message and displays it
 * 
 * 
 ******************************************************/
void encodeQR(char* message) {
  strcpy((char *)strinbuf,message);
  qrencode();
  display.clearDisplay();   // clears the screen and buffer
  byte ly=0,lx=0;
  byte xMargin=(SCREEN_WIDTH-unWidth)/2;
  byte yMargin=(SCREEN_HEIGHT-unWidth)/2;
  for (ly = 0; ly < unWidth; ly++) //row by row
  {
    for (lx = 0; lx < unWidth; lx++) //col by col
    {
      if (QRBIT(lx,ly)==1) //is the pixel black?
      {
        display.drawPixel(lx+xMargin, ly+yMargin, BLACK); //draw one dot on the screen
      } //if (QRBIT(lx,ly)==1)
    }//for lx
    display.display();//TODO: move this down to increase performance
  }//for ly
  Serial.print(F("Key encoded. RAM:"));
  Serial.println(freeram());
  //delay(10000);
}//encodeQR()

/*****************************************************
 * coin_encode()
 * takes a 32 byte char array and converts it to a private key in WIF format
 * 
 * 
 ******************************************************/
char *coin_encode(char *x, char *out) {
  Serial.print(F("Entered coin_encode function. RAM:"));
  Serial.println(freeram());
  char rmd[37];
  /* following will check for leading zeroes. shouldn't need this, key produced programmaticly. removing to save SRAM
   	if (!is_hex(x) || !(is_hex(y))) {
   		coin_err = "bad public point string";
   		return 0;
   	}
  */
  rmd[0] = COIN_VER;//coin version. going to be step 1 from https://en.bitcoin.it/wiki/Base58Check_encoding
  
  
  //encoding in 256
  char * y = encodeSHA256(x,32);
  
  Serial.print(F("digest: "));
  for (byte i2 = 0; i2<=31; i2++) { 
    Serial.print((signed char) y[i2]);
    Serial.print(" ");
  }
  Serial.println();
  
  memcpy(rmd + 1, y, 32);    
  memcpy(rmd + 33, encodeSHA256(encodeSHA256(rmd, 33), SHA256_DIGEST_LENGTH), 4);//step 2
   /* Debugging loop to show SHA-256 encoded private key before base58 conversion.
   Serial.println("SHA-256 result pending base58 encoding: ");
   for (byte be = 0; be<=99;be++)
   Serial.print((byte)rmd[be]);
   Serial.println("");
   */
  return base58((byte*)rmd, out);
}

/*****************************************************
 * encodeSHA256()
 * subfunction which encrypts using SHA256
 * 
 * 
 ******************************************************/
char * encodeSHA256 (char * inArray, byte length) {
  Serial.print(F("Entered encodeSHA256 function. RAM:"));
  Serial.println(freeram());
  uint8_t hash[32];
  uint8_t data[length];
  for (byte i=0; i<=(length-1);i++)
    data[i]= inArray[i];
  struct SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx,data,sizeof(data));
  sha256_final(&ctx,hash); 
  return ((char *)hash);
}

//base58check encodes the message
char* base58(byte *s, char *out) { 
  // if s is changed to char *, the output WIF is nonesense characters. 
  //this algo relies on unsigned numbers. however, the output of the sha library is signed chars.
  Serial.print(F("Entered base58 function. RAM:"));
  Serial.println(freeram());  

  //can this be PROGMEM?
  static const char * alphabet = "123456789"
    "ABCDEFGHJKLMNPQRSTUVWXYZ"
    "abcdefghijkmnopqrstuvwxyz";

  static char buf[40];
  int c, i, n;
  if (!out) out = buf;

  out[n = 51] = 0;
  while (n--) {
    for (c = i = 0; i < 37; i++) {
      c = c * 256 + s[i];
      s[i] = c / 58;
      c %= 58;
    }
    out[n] = alphabet[c];
  }
  for (n = 0; out[n] == '1'; n++);
  memmove(out, out + n, 34 - n);
  return out;
}

long getUTC () {
  RTC_DS1307 RTC;
  RTC.begin();
  DateTime now=RTC.now();
  return now.unixtime(); 
}

//use to show how much SRAM is left. this helped me figure out that I needed a mega2560
int freeram () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

//test image from lady ada's pcb test example https://github.com/adafruit/Adafruit-PCD8544-Nokia-5110-LCD-library
//used here to indicate reboot
void testfillrect(void) {
  uint8_t color = 1;
  for (int16_t i=0; i<display.height()/2; i+=3) {
    // alternate colors
    display.fillRect(i, i, display.width()-i*2, display.height()-i*2, color%2);
    display.display();
    color++;
  }
}












