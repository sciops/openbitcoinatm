/*
 ************************************************************************
 OpenBitcoinATM
 (ver. 1.5.4)
 
 MIT Licence (MIT)
 Copyright (c) 1997 - 2014 John Mayo-Smith for Federal Digital Coin Corporation
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 
 OpenBitcoinATM is the first open-source Bitcoin automated teller machine for
 experimentation and education. 
 
 This application, counts pulses from a Pyramid Technologies Apex 5000
 series bill acceptor and interfaces with the Adafruit 597 TTL serial Mini Thermal 
 Receipt Printer.
 
 
 References
 -----------
 John Mayo-Smith: https://github.com/mayosmith
 
 Here's the A2 Micro panel thermal printer --> http://www.adafruit.com/products/597
 
 Here's the bill accceptor --> APEX 5400 on eBay http://bit.ly/MpETED
 
 Peter Kropf: https://github.com/pkropf/greenbacks
 
 Thomas Mayo-Smith:http://www.linkedin.com/pub/thomas-mayo-smith/63/497/a57
 
 QR encoding adapted from sparkfun's QRprint library: https://github.com/sparkfun/Thermal_Printer
 
 and qrduino library: https://github.com/tz1/qrduino
 
 ************************************************************************
 */


#include <SoftwareSerial.h>
#include <Wire.h>
#include "RTClib.h"
#include <SPI.h>
#include <SD.h>
#include <qrbits.h>
#include <qrencode.h>
#include <qrprint.h>

File logfile; //logfile

char cHexBuf[3]; //for streaming from SD card

const char DOLLAR_PULSE = 4; //pulses per dollar
const short PULSE_TIMEOUT = 2000; //ms before pulse timeout
//const int MAX_BITCOINS = 500; //max btc per SD card
const char HEADER_LEN = 25; //maximum size of bitmap header

const char OUT_FILE_PIXEL_PRESCALER = 6; //scaling QR code pixels by six times when printing to file
const char unWidth = 29; //width of a v3Q QR code used

#define SET_RTCLOCK      1 // Set to true to set Bitcoin transaction log clock to program compile time.
#define TEST_MODE        0 // Set to true to not delete private keys (prints the same private key for each dollar).

#define DOUBLE_HEIGHT_MASK (1 << 4) //size of pixels
#define DOUBLE_WIDTH_MASK  (1 << 5) //size of pixels

RTC_DS1307 RTC; // define the Real Time Clock object
char LOG_FILE[] = "btclog.txt"; //name of Bitcoin transaction log file

const char chipSelect = 10; //SD module
const char printer_RX_Pin = 5;  // This is the green wire
const char printer_TX_Pin = 6;  // This is the yellow wire

char printDensity = 14; // 15; //text darkening
char printBreakTime = 4; //15; //text darkening

// -- Initialize the printer connection
SoftwareSerial *printer;
#define PRINTER_WRITE(b) printer->write(b)

short pulseCount = 0;
unsigned long pulseTime;
//volatile long pulsePerDollar = 6; unused, see DOLLAR_PULSE above.

void setup(){
  Serial.begin(9600); //baud rate for serial monitor
  attachInterrupt(0, onPulse, RISING); //interupt for Apex bill acceptor pulse detect
  //attachInterrupt(0, onPulse, CHANGE);
  pinMode(2, INPUT); //for Apex bill acceptor pulse detect 
  pinMode(10, OUTPUT); //Slave Select Pin #10 on Uno

  if (!SD.begin(chipSelect)) {    
    Serial.println("card failed or not present");
    return;// error("Card failed, or not present");     
  }

  printer = new SoftwareSerial(printer_RX_Pin, printer_TX_Pin);
  printer->begin(19200);

  //Modify the print speed and heat
  PRINTER_WRITE(27);
  PRINTER_WRITE(55);
  PRINTER_WRITE(7); //Default 64 dots = 8*('7'+1)
  PRINTER_WRITE(255); //Default 80 or 800us
  PRINTER_WRITE(255); //Default 2 or 20us

  //Modify the print density and timeout
  PRINTER_WRITE(18);
  PRINTER_WRITE(35);
  //int printSetting = (printDensity<<4) | printBreakTime;
  int printSetting = (printBreakTime<<5) | printDensity;
  PRINTER_WRITE(printSetting); //Combination of printDensity and printBreakTime

  /* For double height text. Disabled to save paper
   PRINTER_WRITE(27);
   PRINTER_WRITE(33);
   PRINTER_WRITE(DOUBLE_HEIGHT_MASK);
   PRINTER_WRITE(DOUBLE_WIDTH_MASK);
   */

#if SET_RTCLOCK
  // following line sets the RTC to the date & time for Bitcoin Transaction log
  Serial.println("Setting RTC");
  RTC.adjust(DateTime(__DATE__, __TIME__));
#endif

  Serial.println();
  Serial.println("Parameters set");
}

void loop(){
  //Serial.print("Pulse Counter: ");
  //Serial.print(pulseCount); 
  //Serial.println();
  //int valloop = digitalRead(2);
  //Serial.print(valloop);
  //Serial.println();

  if(pulseCount == 0)
    return;

  delay(25);//This seems to really help with pulse timeout reliability.

  //Serial.print("millis() at: ");
  //Serial.println(millis());

  if((millis() - pulseTime) < PULSE_TIMEOUT) //if time since start of last onPulse() < 6 secs
    return;

  Serial.println("Pulse Timeout!");
  //Serial.print("pulseTime: ");
  //Serial.println(pulseTime);   

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
  Serial.print("Pulse Counter: ");
  Serial.println(pulseCount); 
  //Serial.println();
  //Serial.print("pulseTime: ");
  //Serial.println(pulseTime);
  pulseTime = millis(); //set pulseTime to the ms since program start
}

/*****************************************************
 * getNextBitcoin
 * - Read next bitcoin QR Code from SD Card
 * 
 ******************************************************/

int getNextBitcoin(char denom){
  Serial.print("dollar baby! Denomination detected: ");
  Serial.println(denom);
  /*
   DateTime now;
   now=RTC.now();
   */
  //create private key
  //TODO: RNG for 256-bit key, based on system time and a static serial number assigned to this unit
  //TODO: reproduce the algo to generate that key on a desktop computer, so the user can load keys generated by the unit w/ BTC
  String privKey = "WinterIsComing";

  //char array
  String filename_s = privKey+".btc";
  char filename[filename_s.length()+1];   
  filename_s.toCharArray(filename, sizeof(filename));
  
  //encode private key in QR code file
  encodeQR(privKey,filename);

  //check if the bitcoin QR code exist on the SD card
  if(SD.exists(filename)){
    Serial.print("file exists: ");
    Serial.println(filename);

    //print logo at top of paper
    if(SD.exists("logo.oba")){
      printBitmap("logo.oba"); 
    }  

    //print QR code off the SD card
    printBitmap(filename); 
    printer->print("Key File Number: ");
    printer->println(filename);
    printer->println("Official Bitcoin Currency.");
    printer->println("Keep secure.");
    printer->println("OpenBitcoinATM.org");
    printer->println(" ");
    printer->println(" ");
    printer->println(" ");
    printer->println(" ");
  }  
  else{
    Serial.print("file does not exist: ");
    Serial.println(filename);        
  }
}  

/*****************************************************
 * printBitmap(char *filename)
 * - open QR code bitmap from SD card. Bitmap file consists of 
 * byte array output by OpenBitcoinQRConvert.pde
 * width of bitmap should be byte aligned -- evenly divisable by 8
 * 
 * 
 ******************************************************/
void printBitmap(char *filename){
  byte cThisChar; //for streaming from SD card
  byte cLastChar; //for streaming from SD card
  int nBytes = 0;
  short iBitmapWidth = 0 ;
  short iBitmapHeight = 0 ;
  File tempFile = SD.open(filename);

  for(short h = 0; h < HEADER_LEN; h++){
    cLastChar = cThisChar;
    if(tempFile.available()) cThisChar = tempFile.read(); 

    //read width of bitmap
    if(cLastChar == '0' && cThisChar == 'w'){
      if(tempFile.available()) cHexBuf[0] = tempFile.read(); 
      if(tempFile.available()) cHexBuf[1] = tempFile.read(); 
      cHexBuf[2] = '\0';

      iBitmapWidth = (byte)strtol(cHexBuf, NULL, 16); 
      Serial.println("bitmap width");//176
      Serial.println(iBitmapWidth);           
    }
    //read height of bitmap
    if(cLastChar == '0' && cThisChar == 'h'){
      if(tempFile.available()) cHexBuf[0] = tempFile.read(); 
      if(tempFile.available()) cHexBuf[1] = tempFile.read(); 
      cHexBuf[2] = '\0';

      iBitmapHeight = (byte)strtol(cHexBuf, NULL, 16);
      Serial.println("bitmap height");//176
      Serial.println(iBitmapHeight); 
    }
  }

  PRINTER_WRITE(0x0a); //line feed

  Serial.println("Print bitmap image");
  //set Bitmap mode
  PRINTER_WRITE(18); //DC2 -- Bitmap mode
  PRINTER_WRITE(42); //* -- Bitmap mode
  PRINTER_WRITE(iBitmapHeight); //r
  PRINTER_WRITE((iBitmapWidth+7)/8); //n (round up to next byte boundary
  //print 
  while(nBytes < (iBitmapHeight * ((iBitmapWidth+7)/8))){ 
    if(tempFile.available()){
      cLastChar = cThisChar;
      cThisChar = tempFile.read(); 

      if(cLastChar == '0' && cThisChar == 'x'){

        cHexBuf[0] = tempFile.read(); 
        cHexBuf[1] = tempFile.read(); 
        cHexBuf[2] = '\0';
        Serial.print(nBytes);
        Serial.print(" ");
        Serial.println(cHexBuf);

        PRINTER_WRITE((byte)strtol(cHexBuf, NULL, 16)); 
        nBytes++;
      }
    }  
  }
  PRINTER_WRITE(10); //Paper feed
  Serial.println("Print bitmap done");

  tempFile.close();
  Serial.println("file closed");

#if !TEST_MODE
  //delete the QR code file after it is printed
  SD.remove(filename);
#endif 

  // update transaction log file
  //if (! SD.exists(LOG_FILE)) {
  // only open a new file if it doesn't exist
  //}
  return;
}


/*****************************************************
 * updateLog()
 * Updates Bitcoin transaction log stored on SD Card
 * Logfile name = LOG_FILE
 * 
 ******************************************************/
void updateLog(){
  DateTime now;
  now=RTC.now();
  logfile = SD.open(LOG_FILE, FILE_WRITE); 
  logfile.print("Bitcoin Transaction ");
  logfile.print(now.unixtime()); // seconds since 1/1/1970
  logfile.print(",");
  logfile.print(now.year(), DEC);
  logfile.print("/");
  logfile.print(now.month(), DEC);
  logfile.print("/");
  logfile.print(now.day(), DEC);
  logfile.print(" ");
  logfile.print(now.hour(), DEC);
  logfile.print(":");
  logfile.print(now.minute(), DEC);
  logfile.print(":");
  logfile.println(now.second(), DEC);
  logfile.close();
}

void encodeQR(String message, char* filename) {
  // create QR code
  message.toCharArray((char *)strinbuf,47);
  //it appears to be set up to use version 3Q. TODO change qrencode default to 3M for 61 char support.
  //51 alphanumerics are needed for a SHA 256-bit private key
  qrencode();

  //write to SD card
  // Convert QrCode bits to bmp pixels
  File out = SD.open(filename);
  out.print("#define 0wB0#define 0hB0static {");//array header, defined 176x176 bitmap size for 30kb
  boolean firstByteFlag = true;//for the very first byte in the whole array
  char halfPixelFlag = 0;
  for (short y = 0; y < unWidth; y++) //row by row
  {
    for (char t = 0; t < OUT_FILE_PIXEL_PRESCALER; t++)//loop each row 6 times high
    {
      short sixScaleCounter = 1;
      for (short x = 0; x < unWidth; x++) //col by col
      {
        if (QRBIT(x,y)) // if this pixel is black
        {
          //x8//out.write("0x00", 4);//write byte to file
          //x6 scaling, puts 4 pixels into 3 bytes
          if (sixScaleCounter == 1) {//if this is the first pixel
            if (firstByteFlag == false) out.print(",");//commas between
            else firstByteFlag=false;//if very first byte, no comma before it

            out.print("0xf");//first half of first byte
            halfPixelFlag == 1;//pass white to next loop iteration
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 2) {//second pixel
            if (halfPixelFlag == 0) out.print("3,0xf");//second half of first byte and first half of second byte
            else if (halfPixelFlag == 1) out.print("f,0xf");//if last pixel was white
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 3) {//third pixel
            out.print("f,");//second half of second byte
            halfPixelFlag == 0;//pass black to next loop iteration
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 4) {//fourth pixel
            if (halfPixelFlag == 0) out.print("0x3f");//third byte
            else if (halfPixelFlag == 1) out.print("0xff");//if last pixel was white
            sixScaleCounter = 1;
          } //else logic error
        }
        else {//else if this pixel is white
          //x8//out.write("0xff", 4);
          //x6 scaling, puts 4 pixels into 3 bytes
          if (sixScaleCounter == 1) {//if this is the first pixel
            if (firstByteFlag == false) out.print(",");//commas between
            else firstByteFlag=false;//if very first byte, no comma before it

            out.print("0x0");//first half of first byte
            halfPixelFlag == 0;//pass black to next loop iteration
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 2) {//second pixel
            if (halfPixelFlag == 0) out.print("0,0x0");//second half of first byte and first half of second byte
            else if (halfPixelFlag == 1) out.print("c,0x0");//if last pixel was white
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 3) {//third pixel
            out.print("0,");//second half of second byte
            halfPixelFlag == 0;//pass black to next loop iteration
            sixScaleCounter++;
          }
          else if (sixScaleCounter == 4) {//fourth pixel
            if (halfPixelFlag == 0) out.print("0x00");//third byte
            else if (halfPixelFlag == 1) out.print("0xc0");//if last pixel was white
            sixScaleCounter = 1;
          } //else logic error
        }
      }//for x
      //finish off the last empty half-byte of the row
      out.print("0");
    }//for t
  }//for y

  //two rows of padding for x6, to 176 width
  for (char p = 0; p < 44;p++)
    out.print(",0x00");
  out.print("};");
  out.close();
}
