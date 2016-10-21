//#include "SmsGsmLib.h"
/*
* sp_ATcom_19feb.ino
*
* Created: 2/19/2016 7:50:38 AM
* Author: Anuj
*/

#include "SoftwareSerial.h"
#include "EEPROM.h"

SoftwareSerial SIMSerial(2,3); // RX, TX

#define DEVICE 8						// Relay to this
#define RSTTRIGGER 12					// connected to reset switch(tak switch) // pin 18 atmega(pb-4)// pulled down
//#define RSTPULSE 4						// connected to actual reset pin PC6 (1 no) = pin 6 atmega
//#define MOTION 9
#define ADDR 5							// EEPROM address for digital pin status
#define ADDR4RESUME 9					// EEPROM address for Power resume status
#define ADDRMOTION 11					// EEPROM address for MOTION status (Enable/Disable)
#define SENDPIN 5
#define RECEIVEPIN 6
//#define DEBUG							// To enable/disable serial print debug

#define GSMMODEMNO 15					// memory location for gsm modem number
#define GSM1STATUS 14					// memory location for gsm status flag for first use

enum sms_type_enum
{
	SMS_UNREAD,
	SMS_READ,
	SMS_ALL,
	SMS_LAST_ITEM
};

int8_t answer;
int8_t answer1;

int ThreeAlert=0;
int Motion_detect_pin = 9;
int pirState = HIGH;					// we start, assuming no MOTION detected

int pirConnected = 15;					// To check if PIR connected or not

//byte active = 0;
//byte reg_info = 0;
byte ad_val = 0;
byte latch_try_count = 0;
byte loop_count = 0;
//byte round_robin_count = 0;

boolean started=false;					// GSM Modem status check Flag
boolean chkON=false;					// Detect CapSwitch ON
boolean chkOFF=false;					// Detect CapSwitch OFF
boolean round_robin=false;				// for manual switch
//boolean pirState = true;				// we start, assuming no MOTION detected


long time = 0;
int CapSwitchstate = LOW;
boolean yes;
boolean previous = false;
const int debounce PROGMEM = 200;  // 200

byte SM_sent = 0;
char n[20];
char currentINFO[30];
char localsmsbuffer[6];
char aux_string[30];
//char GSM_No[]="9910130022";   // "9910130022" Server GSM pool no
char GSM_No[]="9212356764";   // Long code Server GSM pool no 9212356764
char SMS_buff[40];
char check_gsm[11];

const int currentPin = 0;
const unsigned long sampleTime = 100000UL;
const unsigned long numSamples = 250UL;
const unsigned long sampleInterval = sampleTime/numSamples;
//const int adc_zero = 613;
int adc_zero;
//unsigned long tat;
unsigned long req_time = 0;
bool switch_previous_state = false;

//String uid_round_robin;

void setup()
{
	//digitalWrite(RSTPULSE, HIGH);		// very imp
	delay(200);
	adc_zero = determineVQ(currentPin);
	
	//Serial.begin(115200);
	//SIMSerial.begin(4800);

	//Serial.println("Con N/W");
	if (Gsm_begin(4800)){
		while( (sendATcommand("AT+CREG?", "+CREG: 0,1", 500) || sendATcommand("AT+CREG?", "+CREG: 0,5", 500)) == 0 ) {
			delay(100);
			//Serial.print(".");
		}
		
		answer1 = sendATcommand("AT", "OK", 500);
		delay(10);
		answer = sendATcommand("AT+CMGF=1", "OK", 500);    // sets the SMS mode to text
		if (answer1 == 1 || answer == 1) {
			started=true;
		}
		else{
			started=false;
		}
	}
		
	if(IsSMSPresent(SMS_ALL) !=0){
		DeleteSMS(0);
	}
	delay(100);
	
	pinMode(DEVICE, OUTPUT);
	pinMode(RSTTRIGGER, INPUT);
	//pinMode(RSTPULSE, OUTPUT);
	pinMode(Motion_detect_pin, INPUT); 
	digitalWrite(Motion_detect_pin, LOW);
	pinMode(SENDPIN, OUTPUT);
	pinMode(RECEIVEPIN, INPUT_PULLUP);

	pinMode(pirConnected, INPUT); 
	digitalWrite(pirConnected, LOW);
	pinMode(16, OUTPUT);
	pinMode(17, OUTPUT); //14 to 19
	
	digitalWrite(16,LOW);
	digitalWrite(17,LOW);

	//EEPROM.write(GSM1STATUS, 0);
	/*Initialize the EEPROM for the first time use */
		
	if (EEPROM.read(GSM1STATUS) != 1){				
		writeGSMno(GSMMODEMNO, GSM_No);				// Write GSM No for the first time
		EEPROM.write(GSM1STATUS, 1);				// GSM status flag = 1
		EEPROM.write(ADDRMOTION, 0);				// Motion sensor flag = 0
		EEPROM.write(ADDR4RESUME, 1);				//power resume ENABLE
		EEPROM.write(ADDR, 0);
	}
	
	readGSMno(GSMMODEMNO);		//Read GSM Modem No from EEPROM to Local variable	

	//delay(100);

	Deviceinfo();
	delay(500);
	Relayrebootfunc();
	
	req_time = millis();
	delay(2000);
}

void (* resetFunc) (void) = 0;

//Function to write GSM Modem no to EEPROM
void writeGSMno(int address, char *gsmno)
{
	byte i;
	for (i = 0; i < 10; i++) {
		EEPROM.write(address + i, gsmno[i]);
		delay(10);
	}
	EEPROM.write(GSM1STATUS, 1);
}

//Function to read GSM Modem no from EEPROM
void readGSMno(long address)
{
	byte i;
	for (i = 0; i < 10; i++) {
		GSM_No[i] = EEPROM.read(address + i);
		delay(10);
	}
}


void delsms(){
	int i;
	for (i=0; i<5; i++){						//do it max 10 times
		int posALL=IsSMSPresent(SMS_ALL);
		if (posALL!=0){
			if (DeleteSMS(posALL)==1){
			}
			else{
				//
			}
		}
	}
}

void Relayrebootfunc(){							// This is a redirect to previous state
	if(started){
		if ((EEPROM.read(ADDR4RESUME) == 1)&&(EEPROM.read(ADDR) == 1)){
			CapSwitchstate=HIGH;
			ad_val = 1;
			send_sms("ACK ON_","INIT_", "0.00");
			digitalWrite(DEVICE,CapSwitchstate);
			EEPROM.write(ADDR, ad_val);
			led_green();
		}
		else {
			CapSwitchstate=LOW;
			ad_val = 0;
			send_sms("ACK OFF_", "INIT_", "0.00");
			digitalWrite(DEVICE,CapSwitchstate);
			EEPROM.write(ADDR, ad_val);
			led_off();
		}
	}
}

void Exfunc(){
	int pos=0;                                                // SMS position counter
	if(started){
		pos=IsSMSPresent(SMS_UNREAD);
		if(pos){
			loop_count = 0 ; // If pos=1 it means any mess received and if so then no network issue. Hence loop_count = 0;
			int SMSread;
			int returntext;
			for (SMSread=0; SMSread < 3; SMSread++){          // Reading an incoming message without MSISDN check
				returntext=GetSMS(pos,n,SMS_buff,40);
				//read_sms();
				if (returntext == 2){
					//Serial.println("check pass");
					//req_time = millis();
					break;
				}
				
			}
			//Serial.println(SMS_buff);
			
			String checkauth = (String)SMS_buff;
			String keyword = checkauth.substring(0,3);        // extract keyword
			keyword.toCharArray(localsmsbuffer,4);
			
			if(!strcmp(localsmsbuffer,"CHN")){
				String gsm_new = checkauth.substring(4,14);
				gsm_new.toCharArray(check_gsm,11);
				
				writeGSMno(GSMMODEMNO, check_gsm);
				delay(100);
				readGSMno(GSMMODEMNO);
				SM_sent = send_sms("CHN"," ",GSM_No);
				delsms();
				//DeleteSMS(1);
				EEPROM.write(ADDR4RESUME, 1);
				delay(200);
				resetFunc();  //call reset
			}
			else {
				
				String uid = checkauth.substring(4,11);           // extract session id

			if(!strcmp(localsmsbuffer,"ON ")){    // ||checkauth.indexOf("ON") >= 0
				if (EEPROM.read(ADDR) == 1) {
					switch_previous_state = true;
				}
				else if (EEPROM.read(ADDR) == 0) {
					switch_previous_state = false;
				}
				CapSwitchstate=HIGH;
				ad_val = 1;
				led_green();
				delay(2);
				digitalWrite(DEVICE,CapSwitchstate);
				delay(2);				
				EEPROM.write(ADDR, ad_val);				
				
				SM_sent = send_sms("ACK ON_", uid, "_0.00");
				if (SM_sent == 1){
					round_robin = false;
					//round_robin_count = 0;
				}
				else {
					//led_red();
					round_robin = true;
					//uid_round_robin = uid;
				}
				//delsms();
			}
			else if(!strcmp(localsmsbuffer,"OFF")){     // ||checkauth.indexOf("OFF") >= 0
				//sensordata();
				if (EEPROM.read(ADDR) == 1) {
					switch_previous_state = true;
				}
				else if(EEPROM.read(ADDR) == 0) {
					switch_previous_state = false;
				}
				CapSwitchstate=LOW;				
				ad_val = 0;
				led_off();
				delay(2);
				digitalWrite(DEVICE,CapSwitchstate);
				delay(2);
				EEPROM.write(ADDR, ad_val);
				
				sensordata();
				uid = uid + "_";
				SM_sent = send_sms("ACK OFF_", uid, currentINFO);
				if (SM_sent == 1){
					round_robin = false;
					//round_robin_count = 0;
				}
				else {
					//led_red();
					round_robin = true;
					//uid_round_robin = uid;
				}
				//delsms();
			}
			else if(!strcmp(localsmsbuffer,"111")){
				SM_sent = send_sms("PDSTATE 111_", uid, "");
				if (SM_sent == 1){
					delay(100);
					PowerResume("111");
				}
				else {
				}
				//delsms();
			}
			else if(!strcmp(localsmsbuffer,"000")){
				SM_sent = send_sms("PDSTATE 000_", uid, "");
				if (SM_sent == 1){
					delay(100);
					PowerResume("000");
				}
				else {
				}
				//delsms();
			}
			else if(!strcmp(localsmsbuffer,"RST")){
				HWflush("RST ",uid);
			}
			
			else if(!strcmp(localsmsbuffer,"MEN")){
				if(!digitalRead(pirConnected)){     // 1 = DISCONNECTED / 0 = CONNECTED
					SM_sent = send_sms("MEN ", uid, "");
					if (SM_sent == 1){
						//Motionstate=1;
						EEPROM.write(ADDRMOTION, 1);
						delay(10);
						req_time = millis();
					}
					else {
					}
					//delsms();
				}
				//else if(check_pir_connection){
				else{
					SM_sent = send_sms("MDC ", uid, "");
					//delsms();
				}
			}
			else if(!strcmp(localsmsbuffer,"MDA")){
				SM_sent = send_sms("MDA ", uid, "");
				if (SM_sent == 1){
					//Motionstate=0;
					EEPROM.write(ADDRMOTION, 0);
					delay(10);
				}
				else {
				}
				//delsms();
			}
			else {
				//delsms();  // new change
				}
			}
			delsms();
			//DeleteSMS(1);
			memset(SMS_buff,0,sizeof(SMS_buff));				// clear sms buffer
			memset(localsmsbuffer,0,sizeof(localsmsbuffer));
			while( SIMSerial.available() > 0) SIMSerial.read(); // clear ring buffer
			SM_sent = 0;										// set sms flag false
			//loop_count = 0;  // to reset n/w check mechanism counter
		}
	}
}

void TacSwitch()
{
	if(started){
		//String uid = "SWT";
		//int return_val=0;
		digitalWrite(SENDPIN, LOW); // send pin shorts with receive pin on press
		int readvalue = digitalRead(RECEIVEPIN);
		if (readvalue == 0){yes = true;}
		else {yes = false;}
		if(yes == true && previous  == false && millis() - time>debounce){
			
			if(CapSwitchstate == LOW){
				CapSwitchstate = HIGH;
				chkON=true;
				chkOFF=false;
			}
			else{
				CapSwitchstate = LOW;
				chkOFF=true;
				chkON=false;
			}
			time = millis();
			
			if(chkON){
				if (CapSwitchstate == HIGH){
					if (EEPROM.read(ADDR) == 1) {
						switch_previous_state = true;
					}
					else if(EEPROM.read(ADDR) == 0) {
						switch_previous_state = false;
					}
					led_green();
					ad_val = 1;
					EEPROM.write(ADDR, ad_val);
					digitalWrite(DEVICE,CapSwitchstate);
					
					//return_val = sms.SendSMS(ack,"ACK ON_SWT_0.00");
					SM_sent = send_sms("ACK ON_", "SWT_", "0.00");
					if (SM_sent == 1){
						led_green();
						round_robin = false;
						loop_count = 0;	// to reset n/w check mechanism counter
						//round_robin_count = 0;
					}
					else {
						if (EEPROM.read(ADDR) == 1){
							//led_red();
							round_robin = true;
							//uid_round_robin = "SWT_";
						}
					}
				}
				chkON=false;
			}
			
			if(chkOFF){
				if (CapSwitchstate == LOW){
					if (EEPROM.read(ADDR) == 1) {
						switch_previous_state = true;
					}
					else if(EEPROM.read(ADDR) == 0) {
						switch_previous_state = false;
					}
					led_off();
					//char* cur_val;
					ad_val = 0;
					EEPROM.write(ADDR, ad_val);
					digitalWrite(DEVICE,CapSwitchstate);
					sensordata(); 
					////for (int SenseC=0;SenseC<3;SenseC++){
					////	currentAmps = sensordata();
					////}
					SM_sent = send_sms("ACK OFF_", "SWT_", currentINFO);
					if (SM_sent == 1){
						led_off();
						round_robin = false;
						loop_count = 0;	// to reset n/w check mechanism counter
						//round_robin_count = 0;
					}
					else {
						if (EEPROM.read(ADDR) == 0){
							//led_red();
							//led_off();
							round_robin = true;
							//uid_round_robin = "SWT_";
							}
						}
					}
				chkOFF=false;
			}
		}
		previous = yes;
		delay(10);
		SM_sent = 0;
	}
}

void sensordata(){
	unsigned long currentAcc = 0;
	unsigned int count = 0;
	unsigned long prevMicros = micros() - sampleInterval ;
	while (count < numSamples){
		if (micros() - prevMicros >= sampleInterval){
			int adc_raw = analogRead(currentPin) - adc_zero;
			currentAcc += (unsigned long)(adc_raw * adc_raw);
			++count;
			prevMicros += sampleInterval;
		}
	}
	
	float rms = sqrt((float)currentAcc/(float)numSamples) * (50 / 1023.0);  // 75.7576
	rms = rms - 0.07;
	//rms = rms - 0.31;
	if (rms < 0.00){
		rms = 0.00;
	}
	char tmp[10];
	dtostrf(rms,1,2,tmp);
	//dtostrf(rms,1,2,currentINFO);
	//Serial.println(tmp);
	sprintf(currentINFO,"%s", tmp);
	//currentAmps.toCharArray(replyINFO,30);
	//return tmp;
}

int determineVQ(int SensorPIN) {
	//estimating avg. quiscent voltage
	long VQ = 0;
	//read 5000 samples to stabilize value
	for (int i=0; i<5000; i++) {
		VQ += analogRead(SensorPIN);
		delay(1);//depends on sampling (on filter capacitor), can be 1/80000 (80kHz) max.
	}
	VQ /= 5000;
	map(VQ, 0, 1023, 0, 5000);
	return int(VQ);
}

/*
void balanceinfo(String ussd){
char sendUSSD[30];
ussd.toCharArray(sendUSSD,30);
gsm.SimpleWriteln(sendUSSD);   //bal - (F("AT+CUSD=1,\"*121#\"")) // sms - (F("AT+CUSD=1,\"*131*3#\""))
delay(5000);
char resp[105];
gsm.read(resp,105);
sms.SendSMS(ack,resp);
//return 1;
}
*/

void Deviceinfo(){
	String imeistring = "DIMEI ";
	SIMSerial.println("AT+GSN");	//IMEI Info
	delay (500);
	char inchar;
	
	unsigned long starttime = millis();
	while ((millis() - starttime < 1200)&&(SIMSerial.available() > 0)){
//		if ( SIMSerial.available() > 0) {
			inchar = SIMSerial.read();
			////String temp PROGMEM = (String)serialnumber;
			if (inchar == '\r' || inchar == '=' || inchar == '\n' ||
			inchar == '/r' || inchar == 'O' || inchar == 'K' ||
			inchar == 'A' || inchar == 'T' || inchar == '+' ||
			inchar == 'G' || inchar == 'S' || inchar == 'N' || inchar == ' '){
				//do nothing
			}
			else {
				imeistring = imeistring + inchar;
			}
//		}
	}
		
	delay(10);
	if ( imeistring.length() != 21) {
		EEPROM.write(ADDR4RESUME, 1);
		delay(200);
		resetFunc();  //call reset
	}
	
	SM_sent = send_sms("",imeistring,"");
}

void HWflush(char *ACKresp_text, String id){
	digitalWrite(DEVICE, LOW);
	led_off();
	SM_sent = send_sms(ACKresp_text,id,"");
	if (SM_sent == 1){
		led_off();
		ad_val = 0;                                  // plug off
		EEPROM.write(ADDR, ad_val);
		//Motionstate=0;                           // MOTION sens off
		EEPROM.write(ADDRMOTION, 0);
		//PResume=1;
		EEPROM.write(ADDR4RESUME, 1);
		delay(20);
		resetFunc();  //call reset
		//digitalWrite(RSTPULSE, LOW);             // actual controller reset
	}
	else{
		led_red();
	}
}

void PowerResume(char* logic){
	if (!strcmp(logic,"111")){
		//PResume=1;
		EEPROM.write(ADDR4RESUME, 1);
	}
	else if (!strcmp(logic,"000")){
		//PResume=0;
		EEPROM.write(ADDR4RESUME, 0);
	}
}

void motionsence(){
	digitalWrite(Motion_detect_pin,LOW);
	if (digitalRead(Motion_detect_pin) == HIGH) {            // check if the input is HIGH (intruder detected) from PIR
		delay(10);
		if (digitalRead(Motion_detect_pin) == HIGH) {  
			if (pirState == LOW) {
				//Serial.println("M-on");
				for (ThreeAlert=0;ThreeAlert<2;ThreeAlert++){
				//sms.SendSMS(ack,"INTRUDER");
					SM_sent = send_sms("INTRUDER","","");
					delay(2000);
				}
			digitalWrite(Motion_detect_pin,LOW);
			EEPROM.write(ADDRMOTION, 0);
			ThreeAlert=0;
			pirState = HIGH;
			delay(10);
			}
		}
	}
	else {
		delay(100);
		if (pirState == HIGH){
			digitalWrite(Motion_detect_pin,LOW);
			//Serial.println("M-off");
			pirState = LOW;
			//MotionVal= 0;
		}
	}
}

void network_status()
{
	while( (sendATcommand("AT+CREG?", "+CREG: 0,1", 500) || sendATcommand("AT+CREG?", "+CREG: 0,5", 500)) == 0 )
	{
		//Serial.println("Reg failed");
		delay (10);
		SIMSerial.println("AT+CFUN=1,1");
		latch_try_count ++;


		if (latch_try_count == 5)
		{
			//Serial.println("Limit exceeded");
			//network_check = false;
			
			EEPROM.write(ADDR4RESUME, 1);
			delay(200);
			resetFunc();  //call reset
			
			latch_try_count = 0;
			break;
		}
		delay (120000); // wait for 2mins for the n/w to reattach
	}
	latch_try_count = 0;
	loop_count = 0;
}


void led_green()
{
	digitalWrite(16,LOW);  // Green // //pin 25 vcc
	digitalWrite(17,LOW);  // pin 26 gnd
	delay(2);
	digitalWrite(16,HIGH);// Green // //pin 25 vcc
	//delay(2);
	//digitalWrite(A3,LOW);  // pin 26 gnd
}

void led_red()
{
	digitalWrite(16,LOW);  //pin 25 gnd
	digitalWrite(17,LOW); // Red // pin 26 vcc
	delay(2);
	//digitalWrite(A2,LOW);  //pin 25 gnd
	//delay(2);
	digitalWrite(17,HIGH); // Red // pin 26 vcc
}

void led_off()
{
	//delay(5);
	digitalWrite(16,LOW);
	//delay(2);
	digitalWrite(17,LOW); // Off //
}

int send_sms(char* ACKresp_text, String id, char* Load_Current)
{
	sendATcommand("AT+CMGF=1", "OK", 1000);    // sets the SMS mode to text
	byte ret_val = 0;
	sprintf(aux_string,"AT+CMGS=\"%s\"", GSM_No);
	answer = sendATcommand(aux_string, ">", 2000);    // send the SMS number
	if (answer == 1)
	{
		char SMSsendBuf[50];
		String finalack = ACKresp_text + id + Load_Current;
		finalack.toCharArray(SMSsendBuf,50);
		SIMSerial.println(SMSsendBuf);
		SIMSerial.write(0x1A);
		answer = sendATcommand("", "OK", 20000);
		if (answer == 1)
		{
			//Serial.println("Sent.");
			ret_val = 1;
		}
		else
		{
			//Serial.println("Not Sent.");
			ret_val = 0;
		}
	}
	else
	{
		//Serial.print("error ");
		//Serial.println(answer, DEC);
		ret_val = 0;
	}
	return ret_val;
}

int8_t sendATcommand(char* ATcommand, char* expected_answer, unsigned int timeout){

	uint8_t x=0,  answer=0;
	char response[100];
	unsigned long previous;

	memset(response, '\0', 100);    // Initialize the string with null
	
	delay(100);
	
	while( SIMSerial.available() > 0) SIMSerial.read();    // Clean the input buffer
	
	SIMSerial.println(ATcommand);    // Send the AT command

	x = 0;
	previous = millis();

	// this loop waits for the answer
	do{
		// if there are data in the UART input buffer, reads it and checks for the asnwer
		if(SIMSerial.available() != 0){
			response[x] = SIMSerial.read();
			x++;
			// check if the desired answer is in the response of the module
			if (strstr(response, expected_answer) != NULL)
			{
				answer = 1;
			}
		}
		// Waits for the asnwer with time out
	}while((answer == 0) && ((millis() - previous) < timeout));

	return answer;
}

void loop()
{
	//Serial.println("loop");
	TacSwitch();
	//currentAmps = sensordata();
	Exfunc();
	
	if (digitalRead(RSTTRIGGER)){
		HWflush("RST ","");
	}
	
	if (EEPROM.read(ADDRMOTION)){
		if(!digitalRead(pirConnected)){           // 0 = PIR Connected	// 1 = PIR Not Connected
			if((millis()-req_time) > 4000) {
				req_time = 0;
				motionsence();
			}
		}
	}
	
	if (round_robin == true){
	
		if (switch_previous_state == true){
			
			CapSwitchstate = HIGH;
			ad_val = 1;
			digitalWrite(DEVICE,CapSwitchstate);
			delay(2);
			led_green();
			delay(2);
			EEPROM.write(ADDR, ad_val);	
			round_robin = false;
		}
		else if (switch_previous_state == false){
			CapSwitchstate = LOW;
			ad_val = 0;
			digitalWrite(DEVICE,CapSwitchstate);
			delay(2);
			EEPROM.write(ADDR, ad_val);
			led_off();
			round_robin = false;
		}
	}
	//sensordata();

	if (loop_count >= 215) // 213*looptime(283) = Scan every min
	{
		network_status();
	}
	++loop_count;
}
