// length for the internal communication buffer
#define COMM_BUF_LEN        200
// some constants for the IsRxFinished() method
#define RX_NOT_STARTED      0
#define RX_ALREADY_STARTED  1
byte comm_buf[COMM_BUF_LEN+1];  // communication buffer +1 for 0x00 termination

#define GSM_ON              8 // connect GSM Module turn ON to pin 77

#define PARAM_SET_0   0
#define PARAM_SET_1   1
	
enum at_resp_enum
{
	AT_RESP_ERR_NO_RESP = -1,   // nothing received
	AT_RESP_ERR_DIF_RESP = 0,   // response_string is different from the response
	AT_RESP_OK = 1,             // response_string was included in the response
	AT_RESP_LAST_ITEM
};

enum comm_line_status_enum
{
	// CLS like CommunicationLineStatus
	CLS_FREE,   // line is free - not used by the communication and can be used
	CLS_ATCMD,  // line is used by AT commands, includes also time for response
	CLS_DATA,   // for the future - line is used in the CSD or GPRS communication
	CLS_LAST_ITEM
};
/*
enum sms_type_enum
{
	SMS_UNREAD,
	SMS_READ,
	SMS_ALL,
	SMS_LAST_ITEM
};
*/
enum rx_state_enum
{
	RX_NOT_FINISHED = 0,      // not finished yet
	RX_FINISHED,              // finished, some character was received
	RX_FINISHED_STR_RECV,     // finished and expected string received
	RX_FINISHED_STR_NOT_RECV, // finished, but expected string not received
	RX_TMOUT_ERR,             // finished, no character received
	RX_LAST_ITEM			  // initial communication tmout occurred
};

enum getsms_ret_val_enum
{
	GETSMS_NO_SMS   = 0,
	GETSMS_UNREAD_SMS,
	GETSMS_READ_SMS,
	GETSMS_OTHER_SMS,

	GETSMS_NOT_AUTH_SMS,
	GETSMS_AUTH_SMS,

	GETSMS_LAST_ITEM
};

byte comm_line_status;
byte *p_comm_buf;               // pointer to the communication buffer
byte comm_buf_len;              // num. of characters in the buffer
byte rx_state;                  // internal state of rx state machine
uint16_t start_reception_tmout; // max tmout for starting reception
uint16_t interchar_tmout;       // previous time in msec.
unsigned long prev_time;        // previous time in msec.


char DeleteSMS(byte position)
{
	char ret_val = -1;

	//if (position == 0) return (-3);
	if (CLS_FREE != GetCommLineStatus()) return (ret_val);
	SetCommLineStatus(CLS_ATCMD);
	ret_val = 0; // not deleted yet
	
	//send "AT+CMGD=XY" - where XY = position
	if((int)position == 0){
		SIMSerial.println("AT+CMGDA=\"DEL ALL\"");
	}
	else {
		SIMSerial.print("AT+CMGD=");
		SIMSerial.println((int)position);
	}

	// 5000 msec. for initial comm tmout
	// 20 msec. for inter character timeout
	switch (WaitResp(5000, 50, "OK")) {
		case RX_TMOUT_ERR:
		// response was not received in specific time
		ret_val = -2;
		break;

		case RX_FINISHED_STR_RECV:
		// OK was received => SMS deleted
		ret_val = 1;
		break;

		case RX_FINISHED_STR_NOT_RECV:
		// other response: e.g. ERROR => SMS was not deleted
		ret_val = 0;
		break;
	}
	SetCommLineStatus(CLS_FREE);
	return (ret_val);
}

inline byte GetCommLineStatus(void) 
{
	return comm_line_status;
};

inline void SetCommLineStatus(byte new_status)
{
	comm_line_status = new_status;
};

char GetSMS(byte position, char *phone_number, char *SMS_text, byte max_SMS_len)
{
	char ret_val = -1;
	char *p_char;
	char *p_char1;
	byte len;

	//sendATcommand("AT+CMGF=1", "OK", 1000);    // sets the SMS mode to text
	//sendATcommand("AT+CPMS=\"SM\",\"SM\",\"SM\"", "OK", 1000);    // selects the memory

	if (position == 0) return (-3);
	if (CLS_FREE != GetCommLineStatus()) return (ret_val);
	SetCommLineStatus(CLS_ATCMD);
	phone_number[0] = 0;  // end of string for now
	ret_val = GETSMS_NO_SMS; // still no SMS
	
	//send "AT+CMGR=X" - where X = position
	SIMSerial.print("AT+CMGR=");
	SIMSerial.println((int)position);

	// 5000 msec. for initial comm tmout
	// 100 msec. for inter character tmout
	switch (WaitResp(5000, 100, "+CMGR")) {
		case RX_TMOUT_ERR:
		// response was not received in specific time
		ret_val = -2;
		break;

		case RX_FINISHED_STR_NOT_RECV:
		// OK was received => there is NO SMS stored in this position
		if(IsStringReceived("OK")) {
			// there is only response <CR><LF>OK<CR><LF>
			// => there is NO SMS
			ret_val = GETSMS_NO_SMS;
		}
		else if(IsStringReceived("ERROR")) {
			// error should not be here but for sure
			ret_val = GETSMS_NO_SMS;
		}
		break;

		case RX_FINISHED_STR_RECV:
		// find out what was received exactly
		//response for new SMS:
		//<CR><LF>+CMGR: "REC UNREAD","+XXXXXXXXXXXX",,"02/03/18,09:54:28+40"<CR><LF>
		//There is SMS text<CR><LF>OK<CR><LF>
		if(IsStringReceived("\"REC UNREAD\"")) {
			// get phone number of received SMS: parse phone number string
			// +XXXXXXXXXXXX
			// -------------------------------------------------------
			ret_val = GETSMS_UNREAD_SMS;

		}
		//response for already read SMS = old SMS:
		//<CR><LF>+CMGR: "REC READ","+XXXXXXXXXXXX",,"02/03/18,09:54:28+40"<CR><LF>
		//There is SMS text<CR><LF>
		else if(IsStringReceived("\"REC READ\"")) {
			// get phone number of received SMS
			// --------------------------------
			ret_val = GETSMS_READ_SMS;
		}
		else {
			// other type like stored for sending..
			ret_val = GETSMS_OTHER_SMS;
		}

		// extract phone number string
		// ---------------------------
		p_char = strchr((char *)(comm_buf),',');
		p_char1 = p_char+2; // we are on the first phone number character
		p_char = strchr((char *)(p_char1),'"');
		if (p_char != NULL) {
			*p_char = 0; // end of string
			strcpy(phone_number, (char *)(p_char1));
		}


		// get SMS text and copy this text to the SMS_text buffer
		// ------------------------------------------------------
		p_char = strchr(p_char+1, 0x0a);  // find <LF>
		if (p_char != NULL) {
			// next character after <LF> is the first SMS character
			p_char++; // now we are on the first SMS character

			// find <CR> as the end of SMS string
			p_char1 = strchr((char *)(p_char), 0x0d);
			if (p_char1 != NULL) {
				// finish the SMS text string
				// because string must be finished for right behaviour
				// of next strcpy() function
				*p_char1 = 0;
			}
			// in case there is not finish sequence <CR><LF> because the SMS is
			// too long (more then 130 characters) sms text is finished by the 0x00
			// directly in the gsm.WaitResp() routine

			// find out length of the SMS (excluding 0x00 termination character)
			len = strlen(p_char);

			if (len < max_SMS_len) {
				// buffer SMS_text has enough place for copying all SMS text
				// so copy whole SMS text
				// from the beginning of the text(=p_char position)
				// to the end of the string(= p_char1 position)
				strcpy(SMS_text, (char *)(p_char));
			}
			else {
				// buffer SMS_text doesn't have enough place for copying all SMS text
				// so cut SMS text to the (max_SMS_len-1)
				// (max_SMS_len-1) because we need 1 position for the 0x00 as finish
				// string character
				memcpy(SMS_text, (char *)(p_char), (max_SMS_len-1));
				SMS_text[max_SMS_len] = 0; // finish string
			}
		}
		break;
	}

	SetCommLineStatus(CLS_FREE);
	return (ret_val);
}

void RxInit(uint16_t start_comm_tmout, uint16_t max_interchar_tmout)
{
	rx_state = RX_NOT_STARTED;
	start_reception_tmout = start_comm_tmout;
	interchar_tmout = max_interchar_tmout;
	prev_time = millis();
	comm_buf[0] = 0x00; // end of string
	p_comm_buf = &comm_buf[0];
	comm_buf_len = 0;
	SIMSerial.flush(); // erase rx circular buffer
}
	
byte IsRxFinished(void)
{
  byte num_of_bytes;
  byte ret_val = RX_NOT_FINISHED;  // default not finished

  // Rx state machine
  // ----------------

  if (rx_state == RX_NOT_STARTED) {
    // Reception is not started yet - check tmout
    if (!SIMSerial.available()) {
      // still no character received => check timeout
	/*  
	#ifdef DEBUG_GSMRX
		
			DebugPrint("\r\nDEBUG: reception timeout", 0);			
			//Serial.print((unsigned long)(millis() - prev_time));	
			DebugPrint("\r\nDEBUG: start_reception_tmout\r\n", 0);			
			//Serial.print(start_reception_tmout);	
			
		
	#endif
	*/
      if ((unsigned long)(millis() - prev_time) >= start_reception_tmout) {
        // timeout elapsed => GSM module didn't start with response
        // so communication is takes as finished
		/*
			#ifdef DEBUG_GSMRX		
				DebugPrint("\r\nDEBUG: RECEPTION TIMEOUT", 0);	
			#endif
		*/
        comm_buf[comm_buf_len] = 0x00;
        ret_val = RX_TMOUT_ERR;
      }
    }
    else {
      // at least one character received => so init inter-character 
      // counting process again and go to the next state
      prev_time = millis(); // init tmout for inter-character space
      rx_state = RX_ALREADY_STARTED;
    }
  }

  if (rx_state == RX_ALREADY_STARTED) {
    // Reception already started
    // check new received bytes
    // only in case we have place in the buffer
    num_of_bytes = SIMSerial.available();
    // if there are some received bytes postpone the timeout
    if (num_of_bytes) prev_time = millis();
      
    // read all received bytes      
    while (num_of_bytes) {
      num_of_bytes--;
      if (comm_buf_len < COMM_BUF_LEN) {
        // we have still place in the GSM internal comm. buffer =>
        // move available bytes from circular buffer 
        // to the rx buffer
        *p_comm_buf = SIMSerial.read();

        p_comm_buf++;
        comm_buf_len++;
        comm_buf[comm_buf_len] = 0x00;  // and finish currently received characters
                                        // so after each character we have
                                        // valid string finished by the 0x00
      }
      else {
        // comm buffer is full, other incoming characters
        // will be discarded 
        // but despite of we have no place for other characters 
        // we still must to wait until  
        // inter-character tmout is reached
        
        // so just readout character from circular RS232 buffer 
        // to find out when communication id finished(no more characters
        // are received in inter-char timeout)
        SIMSerial.read();
      }
    }
    // finally check the inter-character timeout 
	/*
	#ifdef DEBUG_GSMRX
		
			DebugPrint("\r\nDEBUG: intercharacter", 0);			
<			//Serial.print((unsigned long)(millis() - prev_time));	
			DebugPrint("\r\nDEBUG: interchar_tmout\r\n", 0);			
			//Serial.print(interchar_tmout);		
	#endif
	*/
    if ((unsigned long)(millis() - prev_time) >= interchar_tmout) {
      // timeout between received character was reached
      // reception is finished
      // ---------------------------------------------=	  
		/*
	  	#ifdef DEBUG_GSMRX
		
			DebugPrint("\r\nDEBUG: OVER INTER TIMEOUT", 0);					
		#endif
		*/
      comm_buf[comm_buf_len] = 0x00;  // for sure finish string again
                                      // but it is not necessary
      ret_val = RX_FINISHED;
    }
  }	
  return (ret_val);
}

byte IsStringReceived(char const *compare_string)
{
  char *ch;
  byte ret_val = 0;

  if(comm_buf_len) {
	#ifdef DEBUG_ON
		//Serial.println("ATT: ");
		//Serial.print(compare_string);
		//Serial.print("RIC: ");
		//Serial.println((char *)comm_buf);
	#endif
    ch = strstr((char *)comm_buf, compare_string);
    if (ch != NULL) {
      ret_val = 1;
	  /*#ifdef DEBUG_PRINT
		DebugPrint("\r\nDEBUG: expected string was received\r\n", 0);
	  #endif
	  */
    }
	else
	{
	  /*#ifdef DEBUG_PRINT
		DebugPrint("\r\nDEBUG: expected string was NOT received\r\n", 0);
	  #endif
	  */
	}
  }

  return (ret_val);
}

char IsSMSPresent(byte required_status)
{
	char ret_val = -1;
	char *p_char;
	byte status;

	if (CLS_FREE != GetCommLineStatus()) return (ret_val);
	SetCommLineStatus(CLS_ATCMD);
	ret_val = 0; // still not present

	switch (required_status) {
		case SMS_UNREAD:
		SIMSerial.println("AT+CMGL=\"REC UNREAD\"");
		break;
		case SMS_READ:
		SIMSerial.println("AT+CMGL=\"REC READ\"");
		break;
		case SMS_ALL:
		SIMSerial.println("AT+CMGL=\"ALL\"");
		break;
	}

	// 5 sec. for initial comm tmout
	// and max. 1500 msec. for inter character timeout
	RxInit(5000, 1500);
	// wait response is finished
	do {
		if (IsStringReceived("OK")) {
			// perfect - we have some response, but what:

			// there is either NO SMS:
			// <CR><LF>OK<CR><LF>

			// or there is at least 1 SMS
			// +CMGL: <index>,<stat>,<oa/da>,,[,<tooa/toda>,<length>]
			// <CR><LF> <data> <CR><LF>OK<CR><LF>
			status = RX_FINISHED;
			break; // so finish receiving immediately and let's go to
			// to check response
		}
		status = IsRxFinished();
	} while (status == RX_NOT_FINISHED);

	switch (status) {
		case RX_TMOUT_ERR:
		// response was not received in specific time
		ret_val = -2;
		break;

		case RX_FINISHED:
		// something was received but what was received?
		// ---------------------------------------------
		if(IsStringReceived("+CMGL:")) {
			// there is some SMS with status => get its position
			// response is:
			// +CMGL: <index>,<stat>,<oa/da>,,[,<tooa/toda>,<length>]
			// <CR><LF> <data> <CR><LF>OK<CR><LF>
			p_char = strchr((char *)comm_buf,':');
			if (p_char != NULL) {
				ret_val = atoi(p_char+1);
			}
		}
		else {
			// other response like OK or ERROR
			ret_val = 0;
		}

		// here we have gsm.WaitResp() just for generation tmout 20msec. in case OK was detected
		// not due to receiving
		WaitResp(20, 20);
		break;
	}

	SetCommLineStatus(CLS_FREE);
	return (ret_val);
}

byte WaitResp(uint16_t start_comm_tmout, uint16_t max_interchar_tmout)
{
	byte status;

	RxInit(start_comm_tmout, max_interchar_tmout);
	// wait until response is not finished
	do {
		status = IsRxFinished();
	} while (status == RX_NOT_FINISHED);
	return (status);
}

byte WaitResp(uint16_t start_comm_tmout, uint16_t max_interchar_tmout,char const *expected_resp_string)
{
	byte status;
	byte ret_val;

	RxInit(start_comm_tmout, max_interchar_tmout);
	// wait until response is not finished
	do {
		status = IsRxFinished();
	} while (status == RX_NOT_FINISHED);

	if (status == RX_FINISHED) {
		// something was received but what was received?
		// ---------------------------------------------
		
		if(IsStringReceived(expected_resp_string)) {
			// expected string was received
			// ----------------------------
			ret_val = RX_FINISHED_STR_RECV;
		}
		else {
			ret_val = RX_FINISHED_STR_NOT_RECV;
		}
	}
	else {
		// nothing was received
		// --------------------
		ret_val = RX_TMOUT_ERR;
	}
	return (ret_val);
}
//SoftwareSerial. _cell;

//inline void setStatus(GSM_st_e status) { _status = status; }


int Gsm_begin(long baud_rate){
	//SoftwareSerial.Stream _cell;
	int response=-1;
	int cont=0;
	boolean norep=false;
	boolean turnedON=false;
	SetCommLineStatus(CLS_ATCMD);
	SIMSerial.begin(baud_rate);
	//setStatus(IDLE);

	
	for (cont=0; cont<3; cont++){
		if (AT_RESP_ERR_NO_RESP == SendATCmdWaitResp("AT", 500, 100, "OK", 5)&&!turnedON) {		//check power
			// there is no response => turn on the module
			
			// generate turn on pulse
			digitalWrite(GSM_ON, HIGH);
			delay(1200);
			digitalWrite(GSM_ON, LOW);
			delay(1200);
			norep=true;
		}
		else{
			norep=false;
		}
	}
	
	if (AT_RESP_OK == SendATCmdWaitResp("AT", 500, 100, "OK", 5)){
		turnedON=true;
	}
	if(cont==3&&norep){ //////////-- here--///////////
		//Serial.println("Trying to force the baud-rate to 9600\n");
		for (int i=0;i<8;i++){
			switch (i) {
				case 0:
				SIMSerial.begin(1200);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 1:
				SIMSerial.begin(2400);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 2:
				SIMSerial.begin(4800);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 3:
				SIMSerial.begin(9600);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 4:
				SIMSerial.begin(19200);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 5:
				SIMSerial.begin(38400);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 6:
				SIMSerial.begin(57600);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
				
				case 7:
				SIMSerial.begin(115200);
				SIMSerial.print(F("AT+IPR=9600\r"));
				break;
			}
		}
		//Serial.println("ERROR: SIM900 doesn't answer. Check power and serial pins in GSM.cpp");
		return 0;
	} // ----here---//


	if (AT_RESP_ERR_DIF_RESP == SendATCmdWaitResp("AT", 500, 100, "OK", 5)&&!turnedON){		//check OK  // ----here---//
		
		for (int i=0;i<8;i++){
			switch (i) {
				case 0:
				SIMSerial.begin(1200);
				break;
				
				case 1:
				SIMSerial.begin(2400);
				break;
				
				case 2:
				SIMSerial.begin(4800);
				break;
				
				case 3:
				SIMSerial.begin(9600);
				break;
				
				case 4:
				SIMSerial.begin(19200);
				break;
				
				case 5:
				SIMSerial.begin(38400);
				break;
				
				case 6:
				SIMSerial.begin(57600);
				break;
				
				case 7:
				SIMSerial.begin(115200);
				SIMSerial.print(F("AT+IPR=9600\r"));
				SIMSerial.begin(9600);
				delay(500);
				break;
				
				// if nothing else matches, do the default
				// default is optional
			}
			
			delay(100);			

			if (AT_RESP_OK == SendATCmdWaitResp("AT", 500, 100, "OK", 5)){
				
				SIMSerial.print(F("AT+IPR="));
				SIMSerial.print(baud_rate);
				SIMSerial.print("\r"); // send <CR>
				delay(500);
				SIMSerial.begin(baud_rate);
				delay(100);
				if (AT_RESP_OK == SendATCmdWaitResp("AT", 500, 100, "OK", 5)){
					#ifdef DEBUG_ON
					//Serial.println("DB:OK BR");
					#endif
				}
				turnedON=true;
				break;
			}
		}
		// communication line is not used yet = free
		SetCommLineStatus(CLS_FREE);
		// pointer is initialized to the first item of comm. buffer
		p_comm_buf = &comm_buf[0];
	} // ----here---//

	SetCommLineStatus(CLS_FREE);

	if(turnedON){		
		WaitResp(50, 50);
		
		InitParam(PARAM_SET_0);
		InitParam(PARAM_SET_1);//configure the module
		
		Echo(0);               //enable AT echo
		//setStatus(READY);
		return(1);

	}
	else{
		//just to try to fix some problems with 115200 baudrate
		SIMSerial.begin(9600);
		delay(1000);
		SIMSerial.print(F("AT+IPR="));
		SIMSerial.print(baud_rate);
		SIMSerial.print("\r"); // send <CR>
		return(0);
	}
}

char SendATCmdWaitResp(char const *AT_cmd_string,uint16_t start_comm_tmout, 
						uint16_t max_interchar_tmout,
						char const *response_string,byte no_of_attempts)
{
	byte status;
	char ret_val = AT_RESP_ERR_NO_RESP;
	byte i;

	for (i = 0; i < no_of_attempts; i++) {
		// delay 500 msec. before sending next repeated AT command
		// so if we have no_of_attempts=1 tmout will not occurred
		if (i > 0) delay(500);

		SIMSerial.println(AT_cmd_string);
		status = WaitResp(start_comm_tmout, max_interchar_tmout);
		if (status == RX_FINISHED) {
			// something was received but what was received?
			// ---------------------------------------------
			if(IsStringReceived(response_string)) {
				ret_val = AT_RESP_OK;
				break;  // response is OK => finish
			}
			else ret_val = AT_RESP_ERR_DIF_RESP;
		}
		else {
			// nothing was received
			// --------------------
			ret_val = AT_RESP_ERR_NO_RESP;
		}
		
	}

	return (ret_val);
}

void InitParam(byte group){
	switch (group) {
		case PARAM_SET_0:
		// check comm line
		//if (CLS_FREE != GetCommLineStatus()) return;

		SetCommLineStatus(CLS_ATCMD);
		// Reset to the factory settings
		SendATCmdWaitResp("AT&F", 1000, 50, "OK", 5);
		// switch off echo
		SendATCmdWaitResp("ATE0", 500, 50, "OK", 5);
		// setup fixed baud rate
		//SendATCmdWaitResp("AT+IPR=9600", 500, 50, "OK", 5);
		// setup mode
		//SendATCmdWaitResp("AT#SELINT=1", 500, 50, "OK", 5);
		// Switch ON User LED - just as signalization we are here
		//SendATCmdWaitResp("AT#GPIO=8,1,1", 500, 50, "OK", 5);
		// Sets GPIO9 as an input = user button
		//SendATCmdWaitResp("AT#GPIO=9,0,0", 500, 50, "OK", 5);
		// allow audio amplifier control
		//SendATCmdWaitResp("AT#GPIO=5,0,2", 500, 50, "OK", 5);
		// Switch OFF User LED- just as signalization we are finished
		//SendATCmdWaitResp("AT#GPIO=8,0,1", 500, 50, "OK", 5);
		SetCommLineStatus(CLS_FREE);
		break;

		case PARAM_SET_1:
		// check comm line
		//if (CLS_FREE != GetCommLineStatus()) return;
		SetCommLineStatus(CLS_ATCMD);
		// Request calling line identification
		SendATCmdWaitResp("AT+CLIP=1", 500, 50, "OK", 5);
		// Mobile Equipment Error Code
		//SendATCmdWaitResp((char *)F("AT+CMEE=0"), 500, 50, "OK", 5);
		// Echo canceller enabled
		//SendATCmdWaitResp("AT#SHFEC=1", 500, 50, "OK", 5);
		// Ringer tone select (0 to 32)
		//SendATCmdWaitResp("AT#SRS=26,0", 500, 50, "OK", 5);
		// Microphone gain (0 to 7) - response here sometimes takes
		// more than 500msec. so 1000msec. is more safety
		//SendATCmdWaitResp("AT#HFMICG=7", 1000, 50, "OK", 5);
		// set the SMS mode to text
//		SendATCmdWaitResp("AT+CMGF=1", 500, 50, "OK", 5);
		// Auto answer after first ring enabled
		// auto answer is not used
		//SendATCmdWaitResp("ATS0=1", 500, 50, "OK", 5);
		// select ringer path to handsfree
		//SendATCmdWaitResp("AT#SRP=1", 500, 50, "OK", 5);
		// select ringer sound level
		//SendATCmdWaitResp("AT+CRSL=2", 500, 50, "OK", 5);
		// we must release comm line because SetSpeakerVolume()
		// checks comm line if it is free
		SetCommLineStatus(CLS_FREE);
		// select speaker volume (0 to 14)
		//SetSpeakerVolume(9);
		// init SMS storage
		
		InitSMSMemory();

		// select phonebook memory storage
		SendATCmdWaitResp("AT+CPBS=\"SM\"", 1000, 50, "OK", 5);
		SendATCmdWaitResp("AT+CIPSHUT", 500, 50, "SHUT OK", 5);
		break;
	}
}

char InitSMSMemory(void)
{
	char ret_val = -1;

	if (CLS_FREE != GetCommLineStatus()) return (ret_val);
	SetCommLineStatus(CLS_ATCMD);
	ret_val = 0; // not initialized yet
	
	// Disable messages about new SMS from the GSM module
	SendATCmdWaitResp("AT+CNMI=2,0", 1000, 50, "OK", 2);

	// send AT command to init memory for SMS in the SIM card
	// response:
	// +CPMS: <usedr>,<totalr>,<usedw>,<totalw>,<useds>,<totals>
	if (AT_RESP_OK == SendATCmdWaitResp("AT+CPMS=\"SM\",\"SM\",\"SM\"", 1000, 1000, "+CPMS:", 10)) {
		ret_val = 1;
	}
	else ret_val = 0;

	SetCommLineStatus(CLS_FREE);
	return (ret_val);
}

void Echo(byte state)
{
	if (state == 0 or state == 1)
	{
		SetCommLineStatus(CLS_ATCMD);

		SIMSerial.print("ATE");
		SIMSerial.print((int)state);
		SIMSerial.print("\r");
		delay(500);
		SetCommLineStatus(CLS_FREE);
	}
}