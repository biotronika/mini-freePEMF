/*
 * One Arduino firmware file for freePEMF and freePEMF duo
 * Supports: NANO 4.2  4.3  5.0 hardware & XM-15B bluetooth serial ext.
 *
 * 2019-05-28 added Real Time Clock (DS1307) support. RTC works with freePEMF & freePEMF duo
 *
 * Chris Czoba (copyleft) krzysiek@biotronika.pl
 * See: biotronics.eu or biotronika.pl
 *
 * License: https://github.com/biotronika/freePEMF
 *
 * New software version running bioZAP 2018-10-21 and more
 */

#include <Arduino.h>  		// For eclipse IDE only

#define FREEPEMF_DUO  		// Comment that line for standard (not duo) freePEMF device
//#define RTC				// Uncomment if you have DS1307 installed in freePEMF duo


//#define SERIAL_DEBUG     	// Uncomment this line for debug purpose
//#define NO_CHECK_BATTERY 	// Uncomment this line for debug purpose

#define SOFT_VER "2019-06-10"

#ifdef FREEPEMF_DUO
 #define HRDW_VER "NANO 5.0" 	// freePEMF duo
 #include <Wire.h>
 #include <LiquidCrystal_I2C.h>
#else
 #define HRDW_VER "NANO 4.3"	// Old device with IRF540 or L298N driver
#endif

#ifdef RTC
 #include <Wire.h>				// For non-duo devices
#endif


#include <EEPROM.h>


#define powerPin 4	// Power
#define buzzPin 10	// Buzzer
#define btnPin 3	// Power On-Off / Pause / Change program button
#define hrmPin 2	// Biofeedback HR meter on 3th plug pin.

#define coilPin 5	// Coil driver IRF540 (NANO 4.2) or ENB ch2 driver pin for NANO 4.3
#define relayPin 9	// Direction relay - NANO 4.2 only (4.3 does not have direction relay)

#define int1Pin A0	// NANO 4.3 and 5.0 supports L298N driver INT1 (ch 1) driver pin
#define int2Pin A1	// INT2 (ch 1)
#define int3Pin A2	// INT3 (ch 2)
#define int4Pin A3	// INT4 (ch 2)

#ifdef FREEPEMF_DUO
 #define redPin   LED_BUILTIN  	// Not used,
 #define greenPin LED_BUILTIN	// on board led
 #define coilAuxPin 12	// ENA ch1 driver pin for NANO 5.0

 #define SCL A5  		// I2C LCD interface
 #define SDA A4

#else
 #define redPin 12		// Red LED
 #define greenPin 11	// Green LED

#endif


//Bluetooth
#define btPowerPin	6	//OUT


//Battery staff
#define batPin A7                 // Analog-in battery level
#define BATTERY_VOLTAGE_RATIO 0.153   // include 10k/4,7k resistor voltage divider. 5V*(10k+4,7k)/4,7k = 0,0153 (x10)
#define MIN_BATTERY_LEVEL 100          // 90 means 9.0 V  (x10), less then that turn off
#define USB_POWER_SUPPLY_LEVEL 65     // Maximum USB voltage level means 6.5V


//bioZAP_def.h/////////////////////////////////////////////////////
#define WELCOME_SCR "bioZAP welcome! See http://biotronics.eu"
#define PROGRAM_SIZE 1000   // Maximum program size
#define PROGRAM_BUFFER 64  // SRAM buffer size, used for script loading
#define MAX_CMD_PARAMS 4    // Count of command parameters

#ifdef FREEPEMF_DUO
 #define LCD_SCREEN_LINES 2 // LCD user line number, -1 = no LCD
 #define LCD_MESSAGE_LINE 1	// Default lcd line for bioZAP messages
 #define LCD_PBAR_LINE 0	// Default lcd for progress bar and heart rate

#else
 #define LCD_SCREEN_LINES -1 // LCD user lines number, -1 = no LCD
#endif

#define MIN_FREQ_OUT 1      //  0.01 Hz
#define MAX_FREQ_OUT 6100   // 61.00 Hz for software function
#define MAX_FREQ_TIMER 1600000	// 16kHz for timer
#define SCAN_STEPS 20       // For scan function purpose - default steps
#define MAX_LABELS 9        // Number of jump labels

//END bioZAP_def.h/////////////////////////////////////////////////////////

#ifdef FREEPEMF_DUO

LiquidCrystal_I2C lcd(0x3F,16,2);  // set the LCD address to 0x3F for a 16 chars and 2 line display
//LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

#endif

struct OutMode {
	char mode = 'B';  //B=Both, M=Main, A=Auxiliary
	byte mask = B11;  //B11,    B01,    B10
};

//bioZAP.h/////////////////////////////////////////////////////////////
String inputString = "";                // A string to hold incoming serial data
String line;
String param[MAX_CMD_PARAMS];           // param[0] = cmd name
boolean stringComplete = false;         // whether the string is complete
boolean memComplete = false;
unsigned long lastFreq = MIN_FREQ_OUT;  // Uses scan function
byte pwm = 50;							// Duty cycle of pulse width modulation: 1-99 %
int minBatteryLevel = 0; 
boolean Xoff = false;
boolean btOn = false;
byte display = 1;
OutMode outMode;
long pbarTotalTimeSec=0, pbarLeftTimeSec=0;  //For total progress bar calculation

//use global variables locally, for saving RAM memory
byte b;
int i;
long l;
unsigned long ul, timeMark=0;
char c;
//END bioZAP.h/////////////////////////////////////////////////////////////


//bioZAP jump & labels
int labelPointer[MAX_LABELS+1];  		// Next line beginning address of label
unsigned int labelLoops[MAX_LABELS+1];  // Number of left jump loops
int adr=0;								// Script interpreter pointer


const unsigned long checkDeltaBatteryIncreasingVoltageTime = 600000UL;  // During charging battery minimum increasing voltage after x milliseconds.
                                                                        //  If after the x period the same voltage, green led starts lights. 
const unsigned long pauseTimeOut = 600000UL;                            // 600000 Time of waiting in pause state as turn power off. (60000 = 1 min.)
#define btnBtMode 5000UL
#define btnTimeOut 5000UL 												// Choose therapy program time out. Counted form released button.
#define btnHrCalibrationMode 10000UL                                	// Choose therapy program time out. Counted form released button.

//boolean outputDir = false;

//Outputs state
byte coilsState = B00; // Aux,Main  0=OFF, 1=ON
byte relayState = B00; // Aux,Main  0=NORMAL, 1=CHANGED

byte pin3State = LOW;

unsigned long pauseTime =0; 
 
volatile boolean pause = false; // true = pause on
unsigned long pressTime = 0;    // Time of pressing the button
unsigned long startInterval;    // For unused timeout off.
byte programNo = 1;             // 0 = PC connection, 1= first program etc.
byte hr = 0;                    // User pulse from hrmPin

char memBuffer[PROGRAM_BUFFER];

//function prototypes
 int readEepromLine(int fromAddress, String &lineString);
void getParams(String &inputString);
 int executeCmd(String cmdLine, boolean directMode = false);
void eepromUpload(int adr = 0);
boolean readSerial2Buffer(int &endBuffer);
unsigned long inline checkPause();
void rechargeBattery();
void checkBattLevel();
void btnEvent();
int readFlashLine(int fromAddress, String &lineString);
boolean checkBtnAsEscFromForeverLoop();

#ifdef FREEPEMF_DUO
void progressBar (long totalTimeSec, long leftTimeSec);
void message (String messageText, byte row = LCD_MESSAGE_LINE);
void printMode();
#endif

#ifdef RTC //RTC support
static uint8_t bcd2bin (uint8_t val) { return val - 6 * (val >> 4); }
static uint8_t bin2bcd (uint8_t val) { return val + 6 * (val / 10); }
void rtcSetTime(uint8_t hh, uint8_t mm, uint8_t ss);
void rtcGetTime(uint8_t &hh, uint8_t &mm, uint8_t &ss);
uint8_t hh,mm,ss;
#endif

//bioZAP.h///////////////////////////////////////////////////////////////
//functions
void scan(unsigned long freq, unsigned long period, int steps=SCAN_STEPS);
 int jump(int labelNumber, int &adr);
void off();
void beep( unsigned int period);
void freq(unsigned long _freq, long period, byte pwm = 50);
 int bat();
void wait( unsigned long period);
void exe(int &adr, int prog=0);
 int mem(String param);
void ls();
void rm();
void chp(byte relayState);
void out(byte coilsState);
void pin3(byte value);
#ifdef RTC
void waitFor(byte hh, byte mm);
#endif
//END bioZAP.h/////////////////////////////////////////////////////////////

 
void setup() {  
              
	// Initialize the digital pin as an in/output
	pinMode(coilPin,  OUTPUT);  // Mail coil ch1
	pinMode(powerPin, OUTPUT);  // Power relay
	pinMode(greenPin, OUTPUT);  // LED on board
	pinMode(redPin,   OUTPUT);  // LED on board
	pinMode(relayPin, OUTPUT);  // Direction signal relay
	pinMode(buzzPin,  OUTPUT);  // Buzzer relay (5V or 12V which is no so loud)
	pinMode(btnPin,    INPUT);  // Main button
	pinMode(hrmPin,    INPUT_PULLUP); //Devices connection //_PULLUP

	pinMode(btPowerPin, OUTPUT);


	pinMode(int3Pin,  OUTPUT);   // Direction L298N (ch2)
	pinMode(int4Pin,  OUTPUT);   // Direction L298N (ch2)

#ifdef FREEPEMF_DUO
	pinMode(int1Pin,  OUTPUT);   // Direction L298N (ch1)
	pinMode(int2Pin,  OUTPUT);   // Direction L298N (ch1)
	pinMode(coilAuxPin,  OUTPUT);   // Auxiliary coil ch1

#endif

	//bioZAP
	// Initialize serial communication to PC communication
	Serial.begin(9600);



	// Reserve the memory for inputString:
	inputString.reserve(65); //NANO serial buffer has 63 bytes

#ifdef FREEPEMF_DUO
	//Initialize LCD display
	lcd.init();
	lcd.backlight();

	message ("freePEMF duo", LCD_PBAR_LINE);
    message (SOFT_VER, 1);

#endif
  
	if (bat() < USB_POWER_SUPPLY_LEVEL) {
		//Detected USB PC connection

		programNo = 0; //PC

#ifdef FREEPEMF_DUO
		message ("freePEMF duo", LCD_PBAR_LINE); //Add PC mark
#endif
    
	} else if ( (digitalRead(btnPin)==HIGH) || (digitalRead(hrmPin)==LOW) ) {
		//Power button pressed or pin 3 is connected to ground
    
		//Turn power on
		digitalWrite(powerPin, HIGH);
    
	} else {
		//Power supplier id plugged

	    //Work as a power charger
		rechargeBattery();

		//Turn on if while battery charging pressed button
		digitalWrite(powerPin, HIGH);

#ifdef FREEPEMF_DUO
		//Initialize LCD again
		lcd.init();
		lcd.backlight();

		message ("freePEMF duo", LCD_PBAR_LINE);
		message (SOFT_VER, 1);

#endif

	}


	//Turn on green LED
	digitalWrite(greenPin, HIGH);



	beep(200);
  
	//Wait until turn-on button release
	startInterval = millis();
	while (digitalRead(btnPin)==HIGH){
		ul = millis() - startInterval; //Pressed button period

		if (ul > btnHrCalibrationMode) {
			digitalWrite(redPin, HIGH);
			programNo = 4; //HRM calibration
			btOn=false;
#ifdef FREEPEMF_DUO
			message ("HRM calibration", LCD_PBAR_LINE);
#endif
			beep(200);
			while(digitalRead(btnPin));
		}


		if ((ul > btnBtMode) && programNo && programNo!=4){
			digitalWrite(btPowerPin,HIGH);

			digitalWrite(redPin, HIGH);
			programNo = 0; //PC mode via BT
			btOn=true;
#ifdef FREEPEMF_DUO
			message ("freePEMF duo", LCD_PBAR_LINE); //Add BT mark
#endif
			beep(200);
		}

	}

	delay(10);

	//Initialization of NANO 4.3 and 5.0
	chp(relayState=B00);

  
	//Define minimum battery level uses in working for performance purpose.
	minBatteryLevel = (MIN_BATTERY_LEVEL / BATTERY_VOLTAGE_RATIO) ;

                                
 if (programNo) { 
    // not PC option (programNo>0)
    //Program select  
    unsigned long startInterval = millis();
    while(((millis()-startInterval) < btnTimeOut) && programNo!=4){
      if (digitalRead(btnPin)) {  
          //Reset start moment after btn pressed
          startInterval = millis();
                
          programNo++;
          if (programNo>3) programNo=1;
          for (int p=programNo; p>0; p--){
            //Signals count
            beep(80); delay(150);
          }

#ifdef FREEPEMF_DUO

          switch (programNo) {

          case 1: message ("Demo or user prg" ); break;

          case 2: message ("Earth rhythm" ); break;

          case 3: message ("Antistrs & meditn" ); break;

          }

#endif

          //Wait until button is pressed
          while(digitalRead(btnPin));
         
          //Turn off if pressed more then 1 sec.
          if ((millis()-startInterval) > 1000 ) { 
            beep(500); 
            off();
          } 
      }
    }
    
    // Configure button interrupt
    attachInterrupt(digitalPinToInterrupt(btnPin), btnEvent, CHANGE);
    
  } else {
    //PC option
    
    //Power on
    digitalWrite(powerPin, HIGH);


     
    Serial.println(WELCOME_SCR);
    Serial.print("Device freePEMF ");
    Serial.print(HRDW_VER);
    Serial.print(" ");
    Serial.println(SOFT_VER);
    
    Serial.print('>');  
    
    // Configure button interrupt for using off option
    attachInterrupt(digitalPinToInterrupt(btnPin), btnEvent, CHANGE); 
    
  }

  startInterval=millis();
}


void loop() {

  switch (programNo) {
    	case 1:
    	//Check if user program is load in EEPROM memory

    		if ((byte)EEPROM.read(0)!=255 && (byte)EEPROM.read(0)!=0 && (byte)EEPROM.read(0)!='@') {
    			//User program execute
    			executeCmd("exe\n",true);
    			off();

    		} else {

			  	//Standard program execute
			  	executeCmd("exe 1\n",true);

    		} break;
      
		case 2:

				//Earth regeneration
		  		executeCmd("exe 2\n",true);

		  	break;

		case 3:

				// Antistress & meditation
				executeCmd("exe 3\n",true);

			break;

		case 4:
		//ON-OFF coil by pressing the button
		//TODO: HRM calibration

			digitalWrite(redPin, LOW);
			while(1) {
				checkBattLevel(); //If too low then off

				if (digitalRead(btnPin)) {

					startInterval = millis();
					beep(100);

					while(digitalRead(btnPin));

						if ((millis()-startInterval) > btnTimeOut ) {
							beep(500);
							off();
						}


						out(coilsState^=B11);
						digitalWrite(redPin, coilsState & 1);   // turn LED on/off

				}
			} break;

		default:
    	// PC controlled program

			if (stringComplete) {

				//Restart timeout interval to turn off.
				startInterval=millis();

				//message (inputString);
				executeCmd(inputString, true);
				Serial.print('>'); //Cursor for new command

				// clear the command string
				inputString = "";
				stringComplete = false;

			} break;
    
  	  }

  	  //Turn of if pause time is too long (longer then timeout)
  	  if (millis()-startInterval > pauseTimeOut) off();

} 


//bioZAP_cmd.h///////////////////////////////////////

String formatLine(int adr, String line){
  String printLine;
  printLine.reserve(22);
  printLine = "000"+String(adr,DEC);
  printLine = printLine.substring(printLine.length()-3,  printLine.length());
  printLine+=": "+line; //end marker for appending program
  return printLine;
}

int executeCmd(String cmdLine, boolean directMode){
// Main interpreter function

	getParams(cmdLine);

#ifdef FREEPEMF_DUO

	cmdLine.replace('\n', ' ');
	if ( (param[0] != "wait") && (param[0] != "mem") ) message (cmdLine);

#endif


    if ( param[0]=="mem" ) { 
// Upload terapy to EEPROM
    	if ( !mem(param[1]) ){
    		Serial.println("OK");
    	}

        
    } else if ( param[0]=="ls" ) {
//List therapy
    	ls();

    
    } else if (param[0].charAt(0)=='#') {
// Comment
      ;


    } else if (param[0]==""){
// Emptyline
      
      ;

    } else if (param[0]=="settime"){
// Set time
    	if ( param[1]!="" && param[2]!="" ){

#ifdef RTC
    		hh = param[1].toInt();
    		mm = param[2].toInt();

    		rtcSetTime(hh, mm, ss);
    		Serial.println("OK");
#else
    		Serial.println("Error: No RTC support!");
#endif

     	} else {
    		Serial.println("Error: Syntax: settime [hh] [mm] [ss]");
    	}


     } else if (param[0]=="gettime"){
// Write current time
#ifdef RTC
    		rtcGetTime(hh, mm, ss);
    		Serial.print(hh);
    		Serial.print(" ");
    		Serial.print(mm);
    		Serial.print(" ");
    		Serial.println(ss);
#else
    		Serial.println("Error: No RTC support!");
#endif

     } else if (param[0]=="waitfor"){
//Wait for a specific time hh mm ss
     	if ( param[1]!="" && param[2]!="" ){

 #ifdef RTC

     		Serial.println("waiting...");
     		waitFor(param[1].toInt(),param[2].toInt());
     		Serial.println("OK");
 #else
     		Serial.println("Error: No RTC support!");
 #endif

      	} else {
     		Serial.println("Error: Syntax settime [hh] [mm] [ss]");
     	}



    } else if (param[0].charAt(0)==':') {
// Label - setup for new label jump counter
    	b = param[0].substring(1).toInt();
    	if (b>0 && b<MAX_LABELS){
    		if(param[1].length()>=1) {
    			if (param[1].toInt()) {
    				labelLoops[b] = param[1].toInt()-1;
    			}

#ifdef SERIAL_DEBUG
        Serial.print("label->labelno: ");
        Serial.print(b);
        Serial.print(" ");
        Serial.println(labelLoops[b]);
#endif
    		} else {
    			labelLoops[b] = -1; //Infinity loop
    		}
    	}


    } else if (param[0]=="jump"){
// Jump [label number]

    	if (  jump(param[1].toInt(), adr)  )  {

			#ifdef SERIAL_DEBUG
				Serial.print("jump->label: ");
				Serial.println(param[1].toInt());
				Serial.print("jump->address: ");
				Serial.println(adr);
			#endif

    	   	return adr;
    	}


    } else if (param[0]=="rm"){
      // Remove, clear therapy - high speed

    	EEPROM.put(0, '@');
    	Serial.println("OK");


    } else if (param[0]=="print"){
// Print command
      
      if (cmdLine.length()>6) {
        Serial.println(cmdLine.substring(6,cmdLine.length()-1));

#ifdef FREEPEMF_DUO
		i = display; // Remember display status
		if (!i) display = 1;
		message(cmdLine.substring(6,cmdLine.length()-1), LCD_MESSAGE_LINE);
		display=i;
#endif

      } else {
        Serial.println();
      }
      

    } else if (param[0]=="bat"){
// Print baterry voltage
    	Serial.println( int(analogRead(batPin)*BATTERY_VOLTAGE_RATIO));
        //Serial.println( int(analogRead(batPin)*BATTERY_VOLTAGE_RATIO));
        Serial.println(bat());


    } else if (param[0]=="cbat"){
// Calibrate battery voltage - deprecated

        Serial.println("OK");
   

    } else if (param[0]=="hr"){
// Print heart rate
      
        Serial.println(hr);

    } else if (param[0]=="pbar"){
// Set progress bar indicator

    	if (param[1].toInt()>0) {
    		pbarTotalTimeSec=param[1].toInt();
    		timeMark = millis();

    		if (param[2].toInt()>0) {
    			pbarLeftTimeSec = param[2].toInt() * pbarTotalTimeSec/100;
    		} else {
    			pbarLeftTimeSec = pbarTotalTimeSec; //100%
    		}
    		Serial.println("OK");
    	} else 	if (pbarTotalTimeSec) {

			i = display; // Remember display status
			display = 1;
			pbarLeftTimeSec= pbarTotalTimeSec- (millis()- timeMark)/1000;
#ifdef FREEPEMF_DUO
			progressBar(pbarTotalTimeSec, pbarLeftTimeSec);
#endif
			display=i;
			Serial.println("OK");

    		} else {
    			Serial.println("Error: initialization pbar");
    		}



    } else if (param[0]=="beep"){
// Beep [time_ms]
        
        beep(param[1].toInt());
        Serial.println("OK");


    } else if (param[0]=="blight"){
 // Turn lcd back light off/on

#ifdef FREEPEMF_DUO

    	if (param[1].toInt()){
    		lcd.backlight();
    	} else {
    		lcd.noBacklight();
    	}
#endif
    	Serial.println("OK");


    } else if (param[0]=="off"){
// Turn off device

    	off();


    } else if (param[0]=="chp"){
// Change output signal polarity
    
    	if (param[1]=="~") {
    		relayState ^= B11;

    	} else {
    		relayState = byte(param[1].toInt()) & B11;

    	}
		chp(relayState); // Bxx:  Aux, Main;
    	Serial.println("OK");


    } else if (param[0]=="wait"){
// Wait millis or micros (negative value)

    	wait(param[1].toInt());
    	Serial.println("OK");


    } else if (param[0]=="rec"){
// Generate rectangle signal - rec [freq] [time_sec]
      
    	if (param[1]!="" ) {
    		//Deprecated way of using this command
    		freq(param[1].toInt(), param[2].toInt());
    	}
    	Serial.println("OK");


    } else if (param[0]=="sin"){
// Generate sinusoidal signal - not supported

    	Serial.println("OK");


    } else if (param[0]=="pwm"){
// Change pwm duty cycle

    	pwm = constrain( param[1].toInt(), 1, 99) ;
    	Serial.println("OK");

    } else if (param[0]=="disp"){
// Turn display off in freePEMF duo for better performance
#ifdef FREEPEMF_DUO
    	c=param[1].charAt(0);

    	switch (c){
    	case '0':

    		message("Display is off",LCD_PBAR_LINE);
    		//message("");
    		display=0;
    		Serial.println("OK");

    	break;
    	case '1':
    		display=1;
    		message("Display is on",LCD_PBAR_LINE);
    		Serial.println("OK");

    	break;
    	default:
    		Serial.println("Error: wrong parameter.");
    	}
#else
    	Serial.println("OK");
#endif

    } else if (param[0]=="out"){
// On-off, change mode of coils

		i = param[1].toInt();
		if (i==11) i=3;
		if (i==10) i=2;

		c=param[1].charAt(0);
		if (c=='a') c='A';
		if (c=='b') c='B';
		if (c=='m') c='M';

    	if (c=='~'){
			coilsState = ~coilsState & B11;
			out(coilsState);
			Serial.println("OK");

    	} else if(c=='M' || c=='B' || c=='A') {
    		out (c);
    		Serial.println("OK");

    	} else if (i<=B11){
			coilsState=i;
			out(coilsState);
			Serial.println("OK");

    	} else {
			Serial.print("Error: wrong parameter: ");
			Serial.println(param[1]);

    	}


    } else if (param[0]=="pin3"){
// On-off pin3

    	switch (param[1].charAt(0)) {
    		case '1':
    			pin3State = HIGH;
    	    break;

    		case '0':
    			pin3State = LOW;
    	    break;

    		case '~':
    			pin3State ^= B1;
	    	break;

    		default:
				Serial.print("Error: wrong parameter: ");
	    		Serial.println(param[1]);
	    	break;

    	}

    	pin3(pin3State);
    	Serial.println("OK");

    } else if (param[0]=="freq"){
// Generate square signal - freq [freq] [time_sec]

      freq(param[1].toInt(), param[2].toInt(), pwm);
      Serial.println("OK");


    } else if (param[0]=="scan"){
// Scan from lastFreq  - scan [freq to] [time_ms] <steps>
      
    	if (param[3]==""){
    		scan(param[1].toInt(), param[2].toInt());
    	} else {
    		scan(param[1].toInt(), param[2].toInt(), param[3].toInt());
    	}
      Serial.println("OK");


    } else if (param[0]=="restart"){
// User program restart

    	adr=0;
    	Serial.println("OK");
    	exe(adr,0);


    } else if (param[0]=="exe"){
// Execute EEPROM program only in direct mode
      //if ( directMode) {
    	b = param[1].toInt();

      if (b<4){

    	exe(adr, b);

      } else {

        Serial.print("Error: can't execute program: ");
        Serial.println(b);

      }
      //param[0]="";


    }  else {
//Unknown command
      Serial.println("Unknown command: "+param[0]);         
    }
    
return 0;
}



//bioZAP.h////////////////////////////////////////

#ifdef RTC
void waitFor(byte hh, byte mm){
byte _hh,_mm,_ss;
unsigned long backlightStart = millis();
	do  {
		rtcGetTime(_hh, _mm, _ss);


     	checkBattLevel();
     	startInterval=millis();
      
     	while(millis()< 1000 + startInterval) {

#ifdef FREEPEMF_DUO
     		//Light lcd backlight after button was pressed for 2 seconds
     		if (digitalRead(btnPin)) {
     			lcd.backlight();
     			backlightStart= millis();
     		}
     		if (millis() > backlightStart + 5000) lcd.noBacklight();
     	}//while(millis()< 1000 + startInterval) {
     	String s = String(_ss);
     	if (_ss<10) s = "0"+s;
     	s = String(_mm)+":"+s;
     	if(_mm<10) s = "0"+s;
     	s = String(_hh)+":"+s;
     	//String s = String(_hh)+":"+String(_mm)+":"+ ("0"+);
     	message (s, LCD_PBAR_LINE);
#else
		}//while(millis()< 1000 + startInterval) {
#endif


    } while ( _hh!=hh || _mm!=mm );
}
#endif

void pin3(byte value){
// Use pin3 as output
//	value=0 - off, value>0 =- on

	pinMode(hrmPin, OUTPUT);

	digitalWrite(hrmPin, value);
	digitalWrite(redPin, value);

}

void rm(){
// Remove, clear script therapy from memory
	EEPROM.put(0, '@');

//TODO: Full version
//	for(int i=0; i<PROGRAM_SIZE; i++){
//		EEPROM.put(i, 255);
		//if (!(i % 128)) Serial.print(".");
//	}

}

int mem(String param){
// Upload therapy to EEPROM
    if (param=="\0") {
      eepromUpload();


    } else if (param=="@") {
      //Find script end
      int endAdr=0;
      for (int i=0; i<PROGRAM_SIZE; i++){
        if ((byte)EEPROM.read(i)==255 || (char)EEPROM.read(i)=='@'){
          endAdr=i;

          break;
        }
      }
      Serial.println(formatLine(endAdr,"appending from..."));
      eepromUpload(endAdr);


    } else if (param.toInt()>0 && param.toInt()<PROGRAM_SIZE) {
      eepromUpload(param.toInt());

    } else {
      Serial.print("Error: incorrect parameter ");
      Serial.println(param);
      return -1;
    }
    return 0;
}


int readLabelPointers(int prog){
	/* Initialize Labels pointers and jump loops
	 * prog:
	 * 0 - user program, jumps have counters,
	 * 1-9 Internal programs,
	 */
	int i;
	int adr=0;

	for(i=1; i<MAX_LABELS+1; i++ )
		labelLoops[i] = 0;

	i=0;

	do {
		if (prog>0) {

			//Internal program addresses
			adr = readFlashLine(i,line);
			getParams(line);
		} else {
			//EEPROM program labels
			adr = readEepromLine(i,line);
			getParams(line);
		}

		if (line.length()>1)
		if (line[0]==':'){
			byte lblNo = line[1]-48;
			if(lblNo>0 && lblNo<10){
				labelPointer[lblNo] = i+line.length();  // Next line of label
				//labelPointer[lblNo] = adr;  // Next line of label
				if (param[1].length()){

					if (param[1].toInt()>0) {
						labelLoops[lblNo] = param[1].toInt()-1;
					}

				} else {
					labelLoops[lblNo] = -1;
				}

				if (lblNo==prog && prog>0) return labelPointer[lblNo];

			}
		}

		i+=line.length();
		//i=adr;

	} while(adr);

#ifdef SERIAL_DEBUG
	for (i=1; i<MAX_LABELS+1;i++){
		Serial.print("readLabelPointers->label: ");
		Serial.print(i);
		Serial.print(" loops: ");
		Serial.print(labelLoops[i]);
		Serial.print(" ptr: ");
		Serial.println(labelPointer[i]);
	}
#endif


	return 0;
}
void exe(int &adr, int prog){
//Execute program

	String line;
	int endLine;
	pbarTotalTimeSec = 0; //reset progress bar counter
	pbarLeftTimeSec = 0; //reset progress bar counter


	//First time of internal and user program init.
	if (!adr && (prog>0) ){

		//Internal flash programs table
		adr = readLabelPointers(prog);

	} else if (!adr) {

		//User program label table initialization
		readLabelPointers(0);
	}


	do {

		// Read program line
		if (prog>0){

			//EEPROM memory
			endLine = readFlashLine(adr,line);

		} else {

			//Flash memory
			endLine = readEepromLine(adr,line);

		}


		adr = adr + endLine;


  		//while (endLine = readEepromLine(adr,line))
		if (endLine){

  			//Serial.print("$");
  			Serial.print(line);
  			//message(line);

  			executeCmd(line);

		}

  	} while (endLine);
       
  		Serial.println("Script done.");
  		Serial.println("OK");
  		adr=0;
}


int jump(int labelNumber, int &adr){

	if (labelNumber>0 && labelNumber<MAX_LABELS){
#ifdef SERIAL_DEBUG
			Serial.print("jump->ptr: ");
			Serial.println(labelPointer[labelNumber]);
#endif

		if (labelPointer[labelNumber]) {

#ifdef SERIAL_DEBUG
			Serial.print("jump->loops: ");
			Serial.println(labelLoops[labelNumber]);
#endif

			if (labelLoops[labelNumber] > 0) {

				adr = labelPointer[labelNumber];	//Jump to new position
				labelLoops[labelNumber]--;			//Decrees jump counter

				return adr;

			} else if(labelLoops[labelNumber]==-1) { //Unlimited loop  (-1 means maximum value of unsigned variable)

				adr = labelPointer[labelNumber];
				return adr;

			}
		}
	}
	return 0;
}


void scan(unsigned long _freq, unsigned long period, int steps){
// Scan from lastFreq to freq used SCAN_STEPS by period

	//SCAN_STEPS default;
	long scanSteps=steps;

  
	long stepPeriod = period /scanSteps;
	if (stepPeriod < 1) {
		scanSteps = period;
		stepPeriod=1;
	}
	long startFreq = lastFreq;
	long stepFreq = long( constrain(_freq, MIN_FREQ_OUT, MAX_FREQ_TIMER) - lastFreq ) / scanSteps;


	#ifdef SERIAL_DEBUG
		Serial.println("scan");
		Serial.print("freq: ");
		Serial.println(_freq);
		Serial.print("lastFreq: ");
		Serial.println(lastFreq);
		Serial.print("freq-lastFreq: ");
		Serial.println(_freq-lastFreq);
		Serial.print("startFreq: ");
		Serial.println(startFreq);
		Serial.print("stepPeriod: ");
		Serial.println(stepPeriod);
		Serial.print("scanSteps: ");
		Serial.println(scanSteps);
		Serial.print("stepFreq: ");
		Serial.println(stepFreq);
	#endif

	for (int i=1; i<=scanSteps; i++) {
		freq(startFreq+(i*stepFreq), stepPeriod);

		#ifdef SERIAL_DEBUG
			Serial.print("freq: ");
			Serial.println(startFreq+(i*stepFreq));
		#endif
	}
}


void ls(){
//List script therapy
	int adr=0;
	int endLine;
	String line;

	if (param[1]=="-n") {
		Serial.println("Adr  Command");

		while ((endLine = readEepromLine(adr,line)) && (adr<PROGRAM_SIZE) ){
		  Serial.print(formatLine(adr,line));
		  adr = adr + endLine;
		}

		//End marker (@) informs an user where to start appending of program
		if (adr<PROGRAM_SIZE) {
			Serial.println(formatLine(adr,"@"));
		}

	} else {

		for(int i=0; i<PROGRAM_SIZE; i++){
			char eeChar=(char)EEPROM.read(i);

			if ((eeChar=='@') || (eeChar==char(255))) {
				break;
			}

			Serial.print(eeChar);
		}
	}

}


void xfreq(unsigned long _freq, /*long period,*/ byte pwm){
/* Experimental version of freq function, generating more then 60Hz signal on pin PD5 (OC0B)
 * xfreq supports 61Hz - 16kHz with accuracy less then 1.6% + quartz error
 * See:
 * https://biotronika.pl/sites/default/files/2018-10/freePEMF_supported_freq_61Hz-16kHz.pdf
 */


	uint16_t prescaler = 1;

	lastFreq =constrain( _freq, MAX_FREQ_OUT, MAX_FREQ_TIMER) ;

	//OC0B / PD5 / PIN5 / CoilPin as output
    //DDRD |= (1 << DDD5); //Already is set

	//Set mode 7 (Fast PWM - TOP OCRA, non-inverting mode)
    TCCR0A = (0 << COM0A1) | (0 << COM0A0) | (1 << COM0B1) | (0 << COM0B0) | (1 << WGM01) | (1 << WGM00);


    // Choose the best prescaler for the frequency
    if (lastFreq<24500) {

    	prescaler = 1024;
    	TCCR0B = (1 << WGM02)  | (1 << CS02)   | (0 << CS01)   | (1 << CS00);

    } else if(lastFreq<98000){

    	prescaler = 256;
    	TCCR0B = (1 << WGM02)  | (1 << CS02)   | (0 << CS01)   | (0 << CS00);

    } else if (lastFreq<784300) {

    	prescaler = 64;
    	TCCR0B = (1 << WGM02)  | (0 << CS02)   | (1 << CS01)   | (1 << CS00);

    } else {

    	prescaler = 8;
    	TCCR0B = (1 << WGM02)  | (0 << CS02)   | (1 << CS01)   | (0 << CS00);

    }


    //Set the nearest applicable frequency
    OCR0A = F_CPU /( prescaler * (lastFreq / 100));

    //Set PWM duty cycle
    OCR0B = pwm * OCR0A / 100;

	#ifdef SERIAL_DEBUG
    /*
			Serial.println("xfreq->registers: ");
			Serial.print("TCCR0A: ");
			Serial.println(TCCR0A, BIN);
			Serial.print("TCCR0B: ");
			Serial.println(TCCR0B, BIN);
			Serial.print("OCR0A : ");
			Serial.println(OCR0A , BIN);
			Serial.print("OCR0B : ");
			Serial.println(OCR0B , BIN);
			*/
	#endif

}


void freq(unsigned long _freq, long period, byte pwm) {
  //Square signal generate, freq=783 for 7.83Hz, period in seconds or milliseconds (negative)

	//Remember last frequency for scan function
	//lastFreq =constrain( _freq, MIN_FREQ_OUT, MAX_FREQ_OUT) ;
	lastFreq =_freq;
	boolean flashLED = 1;

	//Prevent pause turn on during waitFor
	pause=false;

	if (lastFreq==0) lastFreq=1;

	if (lastFreq>MAX_FREQ_OUT) {

		//Use experimental time-counter generator
		out('M');
#ifdef FREEPEMF_DUO
		printMode();
#endif

		xfreq(lastFreq, /*period,*/ pwm);

		//Wait time
		if (period >= 0) {

			//time in seconds;

			for( l = 0 ; l < period*10; l++ ) {
				_delay_ms(100);
				if (l % 10 == 0) {
					Serial.print('.');
#ifdef FREEPEMF_DUO
				if (pbarTotalTimeSec) {
					progressBar( pbarTotalTimeSec, pbarLeftTimeSec--);
				} else {
					progressBar (period,period-(l)/10-1);
				}
#endif
				}



				// Battery level check
				checkBattLevel(); //If too low then off

				//Green led flashing every one second
				digitalWrite(greenPin, flashLED);
				flashLED = !flashLED;



				if (checkPause()) {

					//Set timer registers again after back from pause
					xfreq(lastFreq,  pwm);
				}

			}

		} else {

			//Turn green LED off for a while (milliseconds)
			digitalWrite(greenPin, LOW);

			//negative time in milliseconds
			for( l=0 ; l < -period; l++ ) _delay_ms(1);

			checkBattLevel(); //If too low then off

			checkPause();


		}

	    //Reset registers
	    TCCR0A = 3;
	    TCCR0B = 3;
	    OCR0A=0;
	    OCR0B=0;
	    digitalWrite(greenPin, HIGH);

	} else {
  
		//Use software generator
		unsigned long upInterval = pwm*1000UL/lastFreq;
		unsigned long downInterval = (100UL-pwm)*1000UL/lastFreq;


		unsigned long uptime;

		if (period>0) {
			//Seconds
			uptime = millis() + (period*1000);
		} else {
			//Milliseconds
			uptime = millis() + (-period);
		}
	

		unsigned long serialStartMillis = millis();
		unsigned long startIntervalMillis = millis();

		out(coilsState = outMode.mask); // turn coil on
		digitalWrite(greenPin, HIGH);   // turn LED on

		while(millis()< uptime) {
			//time loop


			if (((millis() - startIntervalMillis) >= upInterval) && (coilsState>0)) {

				//Save start time interval
				startIntervalMillis = millis();

				out(coilsState = B00); // turn coil off
				digitalWrite(greenPin, LOW);   // turn LED off

			}

			if (((millis() - startIntervalMillis) >= downInterval) && (coilsState==0)) {

				//Save start time interval
				startIntervalMillis = millis();

				out(coilsState = outMode.mask); // turn coil on
				digitalWrite(greenPin, HIGH);   // turn LED on

			}


			checkBattLevel(); //If too low then off

			//TODO serial break command

			//Check if pause button pressed and correct uptime
			uptime += checkPause();

			//Count each second and print dot
			if (millis()-serialStartMillis >= 1000) { //one second
				Serial.print('.');
				serialStartMillis = millis();

#ifdef FREEPEMF_DUO
				if (pbarTotalTimeSec) {
					progressBar( pbarTotalTimeSec, pbarLeftTimeSec--);
				} else {
					progressBar (period, (uptime-millis())/1000);
				}
#endif
			}

		}
		out(coilsState=B00); // turn coil off
		digitalWrite(greenPin, HIGH);   // turn LED on
	}

}


unsigned long inline checkPause(){
	if (pause) {
	//Pause - button pressed

		unsigned long pausePressedMillis;

		pausePressedMillis = millis();
		beep(200);

		// turn coil off
		out(B00);

		digitalWrite(greenPin, HIGH);   // turn LED on

		while (pause){
		//wait pauseTimeOut or button pressed

			if (millis()> pausePressedMillis + pauseTimeOut) {
				beep(500);
				off();
			}

		}
		beep(200);

		//Continue
		out(coilsState);

		digitalWrite(greenPin, coilsState);   // turn LED on/

		//Return delta to work time correction
		return millis()-pausePressedMillis;

	} else	return 0;
}


void off() {
  // Power off function
  
	out(coilsState=B00); 		// Turn coil off by making the voltage LOW
	chp(relayState=B00); 		// Relay off

	digitalWrite(greenPin, LOW);    // Green LED off for PC mode
	digitalWrite(powerPin, LOW);  	// Turn power off //If not USB power

	while(digitalRead(btnPin)==HIGH); // Wait because still power on

#ifdef FREEPEMF_DUO
	lcd.init();
	lcd.noBacklight();
#endif


	//If USB PC connection is plugged to arduino pcb cannot turn power off
	while(1); //forever loop
  
}


void chp(byte relayState){
	//Change output polarity
	// bits Bxx: HSb=Aux, LSb=Main;

	//turn both outputs off
	out(0);

	//NANO 4.2 direction relay
	digitalWrite(relayPin, relayState & B01);

	//NANO 4.3 L298N driver
	digitalWrite(int3Pin,  relayState & B01  );
	digitalWrite(int4Pin,  !(relayState & B01)  );
	//digitalWrite(int4Pin, (relayState ^ B01) & B01 );

#ifdef SERIAL_DEBUG
	Serial.print("relayState ");
	Serial.print(relayState);
	Serial.print("  Main: ");
	Serial.print(relayState & B01);
	Serial.print(" ");
	Serial.print((relayState ^ B01) & B01);
	Serial.print(" Aux: ");
	Serial.print((relayState & B10) >> 1);
	Serial.print(" ");
	Serial.println((relayState ^ B10) >> 1);
#endif

	//NANO 5.0 L298 driver - Auxiliary channel
	digitalWrite(int1Pin, (relayState & B10) >> 1 );
	digitalWrite(int2Pin, !((relayState & B10) >> 1) );
	//digitalWrite(int2Pin, (relayState ^ B10) >> 1 );

}


void out(byte coilsState){
	//Change output state on-off
	// bits Bxx:  HSb=Aux, LSb=Main;

#ifdef SERIAL_DEBUG_
	Serial.print("coilsState: ");
	Serial.println(coilsState );
#endif

	switch (coilsState) {

	case 'B': outMode.mask = B11; outMode.mode='B'; break;

	case 'M': outMode.mask = B01; outMode.mode='M'; break;

	case 'A': outMode.mask = B10; outMode.mode='A'; break;


	default: //coilsState: 0, 1, 10, 11, ~, 3, 2

		// Change both outputs
		digitalWrite(coilPin, coilsState & B00000001);
#ifdef FREEPEMF_DUO
		digitalWrite(coilAuxPin, (coilsState & B00000010) >> 1 );
#endif
	}
}


int bat() {
// Get battery voltage function

  return ( analogRead(batPin) * BATTERY_VOLTAGE_RATIO );

}


void wait( unsigned long period) {
// wait [period_ms]

//TODO : change to _delay_ms(1000)

  unsigned long serialStartPeriod = millis();
  unsigned long startInterval = millis();    
  
  while(millis()-startInterval <= period){
    //time loop
      
    //TODO serial break command
          
    //count each second
    if (millis()-serialStartPeriod >= 1000) {
      Serial.print('.');
      serialStartPeriod = millis();
    }
  }

}


void beep( unsigned int period) {
  // beep [period_ms]

  unsigned long serialStartPeriod = millis();
  unsigned long startInterval = millis();

  digitalWrite(buzzPin, HIGH);
  while(millis()-startInterval <= period){
    //time loop

    //TODO serial break command

    //count each second
    if (millis()-serialStartPeriod >= 1000) { //one second
      Serial.print('.');
      serialStartPeriod = millis();
    }
  }
    
  digitalWrite(buzzPin, LOW);
}


//END bioZAP.h///////////////////////////////////////////////////

void btnEvent() {
   //Change button state interruption
   //unsigned long pressTime =0;

  if (digitalRead(btnPin)==HIGH){
    pressTime = millis(); //Specific use of millis(). No incrementing in interruption function.
  } else {
    if (pressTime && (millis() - pressTime > 50)) pause=!pause;
    if (pressTime && (millis() - pressTime > 1000)) {
      for(unsigned int i=0; i<50000; i++) digitalWrite(buzzPin, HIGH); //Cannot use delay() therefore beep() function in interruption
      digitalWrite(buzzPin, LOW);
      off();
    }
    pressTime = 0;
  }
}




void getParams(String &inputString){
  for (int i=0; i<MAX_CMD_PARAMS; i++) param[i]="";

  int from = 0;
  int to = 0;
  for (int i=0; i<MAX_CMD_PARAMS; i++){
    to = inputString.indexOf(' ',from); //Find SPACE

    if (to == -1) {
      to = inputString.indexOf('\n',from); //Find NL #10
      if (to>0) param[i] = inputString.substring(from,to);
      param[i].trim();
      break;
    }

    if (to>0) param[i] = inputString.substring(from,to);
    param[i].trim();
    from = to + 1;
  }
}



//freePEMF_battery.h///////////////////////////////////////////////////////////

void checkBattLevel() {
  //Check battery level

#ifndef NO_CHECK_BATTERY

  if ( analogRead(batPin) < minBatteryLevel) { 
    //Emergency turn off 
    Serial.println();  
    Serial.println(":LowBattery");
    //Serial.println(bat());
    
#ifdef FREEPEMF_DUO
    message("Battery is too low");
#endif

    // red LED on
    digitalWrite(redPin, HIGH);
    digitalWrite(greenPin, LOW);   
      
    // Turn all off
    out(0); 			// Turn coil off by making the voltage LOW
    chp(relayState=0); 	// Relay off

    for (int x=0; x<10; x++){
      digitalWrite(buzzPin, HIGH);   // Turn buzzer on 
      delay(100); 
      digitalWrite(buzzPin, LOW);    // Turn buzzer off
      delay(200); 
    }
        
    beep(500);
    off();
  }
#endif

}


void rechargeBattery() {
  //Recharges is plugged
boolean keepLoop = true;
  
  digitalWrite(powerPin, LOW); // turn power relay off
  digitalWrite(redPin, HIGH);
  beep(200);
  digitalWrite(greenPin, LOW);

#ifdef FREEPEMF_DUO
	lcd.setCursor(0, LCD_MESSAGE_LINE);
	lcd.print( "battery charging");
	delay(5000);
	lcd.noBacklight();

    Serial.println();
    Serial.println(":BatteryCharging");

#endif
      
  unsigned long startInterval = millis();
  int startBatLevel = analogRead(batPin);

  do {

    if ( millis() - startInterval > checkDeltaBatteryIncreasingVoltageTime) {          
      if (analogRead(batPin)-startBatLevel <= 0) { //no increasing voltage
        //Battery recharged

        digitalWrite(greenPin, HIGH);
        beep(200);

#ifdef FREEPEMF_DUO
    	lcd.setCursor(0, LCD_MESSAGE_LINE);
    	lcd.print( "battery ready   ");
    	lcd.backlight();

#endif

        // ... and charge further.
        while (checkBtnAsEscFromForeverLoop());
      }
 
      //Start new charging period with new values
      startInterval = millis();
      startBatLevel = analogRead(batPin);

    }

  }  while (checkBtnAsEscFromForeverLoop()); //forever loop

}

boolean checkBtnAsEscFromForeverLoop(){
    if (digitalRead(btnPin)) {

#ifdef FREEPEMF_DUO
    	lcd.backlight();
#endif

    	startInterval = millis();
    	delay(2000);

#ifdef FREEPEMF_DUO
    	lcd.noBacklight();
#endif

    	//Check if after 2 seconds button is still pressed
    	if(digitalRead(btnPin)){
    		beep(200);
    		return false;

    	} else return true;
    } else
    return true;

}


//freePEMF_serial///////////////////////////////////////////////////////////////////////////
void serialEvent() {

  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    if (inChar!='\r'){
      inputString += inChar;
    }

    Serial.print(inChar); //echo

    // if the incoming character is a newline, set a flag
    if (inChar == '\n') {
      stringComplete = true;
    }

    if (inChar == '@') {
      memComplete = true;
    }
  }

}


//2018-11-18 elektros230: New and better version function readSerial2Buffer from multiZAP
// Local echo is on
boolean readSerial2Buffer(int &endBuffer) {
    int i ; //= 0; //buffer indicator
    char c;

    //while(true) {
    for( i=0; i<PROGRAM_BUFFER; i++){

          while(!Serial.available());

          c = Serial.read();

          memBuffer[i] = c;

          //Show echo when loading to the memory
          Serial.print(c);

          endBuffer = i;

          if (c == '@' ) {

        	  //voice signal after committing each part of script
        	  beep(30);

        	  return false;

          }

    }

    return true;
}

//freePEMF_eeprom.h//////////////////////////////////////////////////////////////////////////

int readEepromLine(int fromAddress, String &lineString){
  //Read one line from EEPROM memory
  int i = 0;
  lineString="";
  do {
    char eeChar=(char)EEPROM.read(fromAddress+i);
    if ((eeChar==char(255)) ||(eeChar==char('@'))) {
      if (i>0) {
        eeChar='\n';
      } else {
        i=0;
        break;
      }
    }
    lineString+=eeChar;
    i++;
    if (eeChar=='\n') break;
  } while (1);

  return i;
}

void eepromUpload(int adr) {
  unsigned int i = 0;
  boolean flagCompleted;
  boolean Xoff = false;
  int endBuffer;
  //eepromLoad = true;

  do {
    //Serial.print(char(XON));
    Xoff = readSerial2Buffer(endBuffer);
    int b =0; // buffer pointer
    flagCompleted = false;
    while (!flagCompleted){

      flagCompleted = !(i+adr<PROGRAM_SIZE) || (memBuffer[b]=='@') || !(b < endBuffer);
      if (memBuffer[b]==';') memBuffer[b]='\n';   //Semicolon as end line LF (#10) for windows script
      if (memBuffer[b]=='\r') memBuffer[b] = ' '; //#13 -> space, No continue because of changing script length
      EEPROM.write(i+adr, memBuffer[b]);
      //Serial.print(memBuffer[b]);
      i++; b++;
    }
    //End of shorter program then PROGRAM_SIZE size

  } while (Xoff);
  if (i+adr<PROGRAM_SIZE) EEPROM.write(i+adr, '@');
  //eepromLoad=false;
}



//freePEMF_prog.h////////////////////////////////////////////////////////////////////////

#include <avr/pgmspace.h>
const char internalProgram[] PROGMEM   = {

		":1\n"
		"#Standard program 13 m\n"
		"wait 2000\n"
		"pbar 780\n"
		"freq 1179 120\n"
		"chp 11\n"
		"freq 783 120\n"
		"chp 00\n"
		"freq 2000 60\n"
		"chp 11\n"
		"freq 1500 60\n"
		"chp 00\n"
		"freq 1000 90\n"
		"chp 11\n"
		"freq 700 90\n"
		"chp 00\n"
		"freq 200 120\n"
		"beep 500\n"
		"off\n"

		":2\n"
		"#Earth regeneration - 8 m\n"
		"wait 2000\n"
		"pbar 480\n"
		"freq 1179 120\n"
		"chp 11\n"
		"freq 1179 120\n"
		"chp 00\n"
		"freq 783 120\n"
		"chp 11\n"
		"freq 783 120\n"
		"beep 500\n"
		"off \n"

		":3\n"
		"#Antisterss & meditation 16 m\n"
		"wait 2000\n"
		"pbar 960\n"
		"freq 1200 20\n"
		"freq 1179 150\n"
		"chp 11\n"
		"freq 1166 20\n"
		"freq 1133 20\n"
		"freq 1100 20\n"
		"freq 1066 20\n"
		"freq 1033 20\n"
		"freq 1000 20\n"
		"freq 966 20\n"
		"freq 933 20\n"
		"freq 900 20\n"
		"freq 866 20\n"
		"freq 833 20\n"
		"freq 800 20\n"
		"chp 00\n"
		"freq 800 20\n"
		"freq 783 120\n"
		"chp 11\n"
		"freq 766 20\n"
		"freq 733 20\n"
		"freq 700 20\n"
		"freq 666 20\n"
		"freq 633 20\n"
		"freq 600 20\n"
		"freq 566 20\n"
		"freq 533 20\n"
		"freq 500 20\n"
		"freq 466 20\n"
		"freq 433 20\n"
		"freq 400 20\n"
		"chp 00\n"
		"freq 366 20\n"
		"freq 333 20\n"
		"freq 300 20\n"
		"freq 266 20\n"
		"freq 233 20\n"
		"freq 200 20\n"
		"freq 166 20\n"
		"freq 130 20\n"
		"freq 100 20\n"
		"off \n"

		"@"

};


int readFlashLine(int fromAddress, String &lineString){
	  //Read one line from EEPROM memory
	  int i = 0;
	  lineString="";

#ifdef SERIAL_DEBUG
	  	//Serial.print("readFlashLine->fromAddress: ");
		//Serial.println(fromAddress);
#endif

	  do {

	    char eeChar = char( pgm_read_byte(&internalProgram[fromAddress+i])  )  ;

#ifdef SERIAL_DEBUG
	  	//Serial.print("readFlashLine->eeChar: ");
		//Serial.println(eeChar);
#endif
	    if ( eeChar==char('@') ) {
	      if (i>0) {
	        eeChar='\n';
	      } else {
	        i=0;
	        break;
	      }
	    }
	    lineString+=eeChar;
	    i++;
	    if (eeChar=='\n') break;
	  } while (1);
#ifdef SERIAL_DEBUG
	  	//Serial.print("readFlashLine->i: ");
		//Serial.println(i);
#endif
	  return i;
}


//freePEMF_lcd.h///////////////////////////////////////////////////////////////////

#ifdef FREEPEMF_DUO

void printMode(){
	lcd.setCursor(15, LCD_MESSAGE_LINE);
	lcd.print( outMode.mode );
}


void message (String messageText, byte row ) {
// Show message in row line
	if (display) {

		lcd.setCursor(0, row);
		lcd.print( "                " );
		lcd.setCursor(0, row);
		lcd.print( messageText );

		if ((row==LCD_PBAR_LINE) && (programNo==0) ){
			lcd.setCursor(14, row);
			if (btOn){
				lcd.print( "BT" );
			} else {
				lcd.print( "PC" );
			}

		}

		if (row==LCD_MESSAGE_LINE){
			printMode();
		}

	}
}


void progressBar (long totalTimeSec, long leftTimeSec) {
//Showing progress with left time in formats: 999m (greater then 10min), 120s (less then 10min)


#ifdef SERIAL_DEBUG
	//Serial.print("progressBar1: ");
	//Serial.print(totalTimeSec);
	//Serial.print(" ");
	//Serial.println(leftTimeSec);
#endif


	//Show ones a second
	//if ( millis() > _lastProgressBarShowed + 1000 || mode ) {
	//_lastProgressBarShowed = millis();
	if (display) {


		// Show progress bar in LCD_PBAR_LINE line - first is 0
		lcd.setCursor( 0, LCD_PBAR_LINE );
		if (leftTimeSec<36000) {
			if (leftTimeSec>600){

				lcd.print(  leftTimeSec/60 );
				lcd.print("m   ");
			} else if (leftTimeSec<60) {

				lcd.print( leftTimeSec );
				lcd.print("s   ");
			} else {
				//Minutes section
				lcd.print( int( leftTimeSec/60 ) );
				lcd.print(':');

				//Seconds section
				if (leftTimeSec % 60 <10) lcd.print('0');
				lcd.print(leftTimeSec % 60);
			}
		}


		if (totalTimeSec) {

			byte percent = 5 + 100 * leftTimeSec / totalTimeSec;

#ifdef SERIAL_DEBUG
			//Serial.print("progressBar2 percent: ");
			//Serial.println(percent);
#endif
			lcd.setCursor(4,LCD_PBAR_LINE);
			lcd.write('[');
			//lcd.setCursor(5,LCD_PBAR_LINE);
			//for (int i=0; i<(percent/13);i++) lcd.write(255); //lcd.write('#');
			for (int i=0; i<8;i++) {
				if (i<(percent/13)) {
					lcd.write(255); //lcd.write('#');}
				} else {
					lcd.print(" ");
				}
			}

			//lcd.print("        ");



			//lcd.setCursor(13,LCD_PBAR_LINE);
			lcd.write(']');
		}

	}

}


#endif

#ifdef RTC //////////////////////////////////////////////////////////////
		   // Real Time Clock support and settime, waitfor, gettime commands

 void rtcSetTime(uint8_t hh, uint8_t mm, uint8_t ss) {
	//Set time in DS 3231
  Wire.beginTransmission(0x68);
  Wire.write((byte)0); // start at location 0
  Wire.write(bin2bcd(ss));
  Wire.write(bin2bcd(mm));
  Wire.write(bin2bcd(hh));
  Wire.endTransmission();
}

 void rtcGetTime(uint8_t &hh, uint8_t &mm, uint8_t &ss){
	//Get time from DS3231
	ss = 255;
	mm = 255;
	hh = 255;

 	Wire.beginTransmission(0x68); // 0x68 is DS3231 device address
	Wire.write((byte)0); // start at register 0
	Wire.endTransmission();
	Wire.requestFrom(0x68, 3); // request three bytes (seconds, minutes, hours)

 	while( Wire.available() )  {

 		ss = bcd2bin(Wire.read() & 0x7F);
		mm = bcd2bin(Wire.read());
		hh = bcd2bin(Wire.read());
	}
}

 #endif
