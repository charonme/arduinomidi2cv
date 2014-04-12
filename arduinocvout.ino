#define SPI_OUT_PIN 11   //MOSI 11 = PB3
#define SPI_OUT_VAL  8   //pin  11 = PB3 = PORTB value 8
#define SPI_CLK_PIN 13   //sck  13 = PB5
#define SPI_CLK_VAL 32   //pin  13 = PB5 = PORTB value 32
#define SPI_SS_PIN  10   //ss   10 = PB2
#define SPI_SS_VAL   4   //ss   10 = PB2 = PORTB value 4
#define GATE_PIN     2   //pin   2 = PD2
#define GATE_VAL     4   //pin   2 = PORTD value 4
 
void setup() {
	// ------------ SPI communication code based on http://www.sinneb.net/?p=85
	pinMode(SPI_OUT_PIN, OUTPUT);
	pinMode(SPI_CLK_PIN, OUTPUT);
	pinMode(SPI_SS_PIN,  OUTPUT);
	pinMode(GATE_PIN,    OUTPUT);
	PORTD &= ~GATE_VAL; //gate off

	delay(100); Serial.begin(31250); delay(100);//midi
	sendIntValueSPI(0);
}
void spiPinWrite(uint8_t pinValue, uint8_t data) {
	if (data==0) 
		PORTB &= ~pinValue;
	else
		PORTB |= pinValue;
} 
void sendIntValueSPI(int value) {
	// -------------------------------------------------------------

	// initiate data transfer with 4921
	//digitalWrite(SPI_SS_PIN,LOW);
	spiPinWrite(SPI_SS_VAL,0);

	// send 4 bit header
	sendSPIHeader();

	// send data
	for(int i=11;i>=0;i--){
		//digitalWrite(SPI_OUT_PIN,((value&(1<<i)))>>i);
		spiPinWrite(SPI_OUT_VAL,((value&(1<<i)))>>i);
		sendSPIClock();
	}

	// finish data transfer
	//digitalWrite(SPI_SS_PIN,HIGH);
	spiPinWrite(SPI_SS_VAL,1);
}
     
void sendSPIHeader() {
	// bit 15
	// 0 write to DAC *
	// 1 ignore command
	//digitalWrite(SPI_OUT_PIN,LOW);
	spiPinWrite(SPI_OUT_VAL,0);
	sendSPIClock();
	// bit 14 Vref input buffer control
	// 0 unbuffered *
	// 1 buffered
	//digitalWrite(SPI_OUT_PIN,LOW);
	spiPinWrite(SPI_OUT_VAL,0);
	sendSPIClock();
	// bit 13 Output Gain selection
	// 0 2x
	// 1 1x *
	//digitalWrite(SPI_OUT_PIN,HIGH);
	spiPinWrite(SPI_OUT_VAL,1);
	sendSPIClock();
	// bit 12 Output shutdown control bit
	// 0 Shutdown the device
	// 1 Active mode operation *
	//digitalWrite(SPI_OUT_PIN,HIGH);
	spiPinWrite(SPI_OUT_VAL,1);
	sendSPIClock();
}
     
void sendSPIClock() {
	//digitalWrite(SPI_CLK_PIN,HIGH);
	spiPinWrite(SPI_CLK_VAL,1);
	//digitalWrite(SPI_CLK_PIN,LOW);
	spiPinWrite(SPI_CLK_VAL,0);
}
int get12bitFromMidi(int midiValue) {
	return (int)round(((float)midiValue - 34.27538387) * 69.400442636);
}
void loop() {
	midiProcessInput();
}

#define MIDI_NOTES_STACK_SIZE 16
uint8_t midiNotesStack[MIDI_NOTES_STACK_SIZE];
uint8_t midiNotesStackSize = 0;

boolean removeFromStack(uint8_t value) {
	boolean wasThere = false;
	uint8_t i = 0;
	for (i = 0; i < midiNotesStackSize; i++) {
		if (value == midiNotesStack[i]) {
			wasThere = true;
			break;
			
		}
	}
	while (wasThere && i < midiNotesStackSize - 1) {
		midiNotesStack[i] = midiNotesStack[i + 1];
		i++;	
	}		
	if (wasThere) midiNotesStackSize--;
	return wasThere;
}
boolean addToStack(uint8_t value) {
	removeFromStack(value);
	while (midiNotesStackSize >= MIDI_NOTES_STACK_SIZE) {
		for (uint8_t i = 0; i < midiNotesStackSize - 1; i++) {
			midiNotesStack[i] = midiNotesStack[i + 1];
		}
		midiNotesStackSize--;
	}
	if (midiNotesStackSize < MIDI_NOTES_STACK_SIZE) {
		midiNotesStack[midiNotesStackSize] = value;
		midiNotesStackSize++;
		return true;
	}
	return false;
}

void midiNoteOn(uint8_t midiNote) {
	int new12bitValue = get12bitFromMidi(midiNote);
	if (new12bitValue >= 0 && new12bitValue < 4096) {
		if (addToStack(midiNote)) {
			sendIntValueSPI(new12bitValue);
		}
	}
	PORTD |= GATE_VAL; //gate on
}
void midiAllNotesOff() {
	midiNotesStackSize = 0;
	PORTD &= ~GATE_VAL; //gate off
}
void midiNoteOff(uint8_t midiNote) {
	if (removeFromStack(midiNote)) {
		int new12bitValue = get12bitFromMidi(midiNotesStack[midiNotesStackSize-1]);
		if (new12bitValue >= 0 && new12bitValue < 4096) {
			sendIntValueSPI(new12bitValue);
		}
	}
	if (midiNotesStackSize <= 0) {
		PORTD &= ~GATE_VAL; //gate off
	}
}
//------------------------------------------------------------------------------
#define MIDI_WAITING_FOR_SYSEX -1
#define MIDI_WAITING_FOR_ON_NOTE 2
#define MIDI_WAITING_FOR_ON_VELOCITY 3
#define MIDI_WAITING_FOR_OFF_NOTE 4
#define MIDI_WAITING_FOR_OFF_VELOCITY 5
#define MIDI_WAITING_FOR_CC 6
#define MIDI_WAITING_FOR_CC_DATA 7

int8_t midiWaitingForIncomingBytesCount = 0; 
int8_t midiWaitingForIncomingData = 0;
int8_t midiLastReceivedOnNote = -1;
int8_t midiLastReceivedOffNote = -1;
int8_t midiLastReceivedCC = -1;
uint8_t runningStatus = 0;

void midiProcessInput() {
	uint8_t processedMidiBytes = 0;
	int8_t nextProcessRunningByte = -1;
	while (
		(Serial.available() && processedMidiBytes < 1) || 
		nextProcessRunningByte>=0
	) {
		uint8_t midiDataByte;
		if (nextProcessRunningByte == -1) {
			midiDataByte = Serial.read();
			
			if (midiWaitingForIncomingBytesCount == 0 && midiDataByte<128) {
				nextProcessRunningByte = midiDataByte;
				midiDataByte = runningStatus;
			}
			
		} else {
			midiDataByte = nextProcessRunningByte;
			nextProcessRunningByte = -1;
		}

		if (
			midiWaitingForIncomingData == MIDI_WAITING_FOR_SYSEX && 
			(midiDataByte & 128) != 0
		) {
			midiWaitingForIncomingData = 0;
			midiWaitingForIncomingBytesCount = 0;
		} else if (
			midiWaitingForIncomingData == MIDI_WAITING_FOR_SYSEX && 
			(midiDataByte & 128) == 0
		) {
			
			//NOP, sysex data
			
		} else if (midiWaitingForIncomingBytesCount == 0) {
			//----------------------------------- process status byte
			if (midiDataByte == 240) { //sysex
				midiWaitingForIncomingData = MIDI_WAITING_FOR_SYSEX;
				midiWaitingForIncomingBytesCount = MIDI_WAITING_FOR_SYSEX;
			} else if ((midiDataByte & 240) == 128) { //note off
				midiWaitingForIncomingData = MIDI_WAITING_FOR_OFF_NOTE;
				midiWaitingForIncomingBytesCount = 2;
			} else if ((midiDataByte & 240) == 144) { //note on
				midiWaitingForIncomingData = MIDI_WAITING_FOR_ON_NOTE;
				midiWaitingForIncomingBytesCount = 2;
			} else if ((midiDataByte & 240) == 160) { //PKP aftertouch
				midiWaitingForIncomingBytesCount = 2;
			} else if ((midiDataByte & 240) == 176) { //CC
				midiWaitingForIncomingData = MIDI_WAITING_FOR_CC;
				midiWaitingForIncomingBytesCount = 2;
			} else if ((midiDataByte & 240) == 192) { //prog chng
				midiWaitingForIncomingBytesCount = 1;
			} else if ((midiDataByte & 240) == 208) { //chan aftertouch
				midiWaitingForIncomingBytesCount = 1;
			} else if ((midiDataByte & 240) == 224) { //Pitch Wheel Change
				midiWaitingForIncomingBytesCount = 2;
			} else if (midiDataByte == 241) { //time code
				midiWaitingForIncomingBytesCount = 1;
			} else if (midiDataByte == 242) { //song position
				midiWaitingForIncomingBytesCount = 2;
			} else if (midiDataByte == 243) { //song select
				midiWaitingForIncomingBytesCount = 1;
			} else if (midiDataByte == 248) { //beat clock
				midiWaitingForIncomingBytesCount = 0;
			} else if (midiDataByte == 250) { //start
				midiWaitingForIncomingBytesCount = 0;
			} else if (midiDataByte == 251) { //continue
				midiWaitingForIncomingBytesCount = 0;
			} else if (midiDataByte == 252) { //stop
				midiWaitingForIncomingBytesCount = 0;
			} else if (midiDataByte == 247) { //sysex end
				midiWaitingForIncomingBytesCount = 0;
			} else { //unknown command?
				midiWaitingForIncomingBytesCount = 0;
			}
			
			if (
				midiDataByte >= 128 && //is status byte
				midiDataByte < 248 //is not realtime statu byte
			) {
				runningStatus = midiDataByte;
			}
		
		} else if (midiWaitingForIncomingBytesCount > 0) {
    
			if (midiWaitingForIncomingData == MIDI_WAITING_FOR_ON_NOTE) {
    
				midiLastReceivedOnNote = midiDataByte;
				midiWaitingForIncomingData = MIDI_WAITING_FOR_ON_VELOCITY;

			} else if (midiWaitingForIncomingData == MIDI_WAITING_FOR_ON_VELOCITY) {

				if (
					midiDataByte>0  && 
					midiLastReceivedOnNote>=35 && midiLastReceivedOnNote<=93
				) {
					midiNoteOn(midiLastReceivedOnNote);
				} else if (midiDataByte==0) { //zero velocity = note off
					midiNoteOff(midiLastReceivedOnNote);
				}
				midiWaitingForIncomingData = 0;

			} else if (midiWaitingForIncomingData == MIDI_WAITING_FOR_OFF_NOTE) {
    
				midiLastReceivedOffNote = midiDataByte; 
				midiWaitingForIncomingData = MIDI_WAITING_FOR_OFF_VELOCITY;

			} else if (midiWaitingForIncomingData == MIDI_WAITING_FOR_OFF_VELOCITY) {
    
				midiNoteOff(midiLastReceivedOffNote);
				midiWaitingForIncomingData = 0;

			} else if (midiWaitingForIncomingData == MIDI_WAITING_FOR_CC) {
    
				midiLastReceivedCC = midiDataByte; 
				midiWaitingForIncomingData = MIDI_WAITING_FOR_CC_DATA;

			} else if (midiWaitingForIncomingData == MIDI_WAITING_FOR_CC_DATA) {
    
				if (midiLastReceivedCC == 120 && midiDataByte == 0) {
					midiAllNotesOff();
				} else if (midiLastReceivedCC == 123 && midiDataByte == 0) {
					midiAllNotesOff();
				}

				midiWaitingForIncomingData = 0;
			}
			midiWaitingForIncomingBytesCount--;
			if (midiWaitingForIncomingBytesCount <= 0) {
				midiWaitingForIncomingBytesCount = 0;
				midiWaitingForIncomingData = 0;
    			}
		} else { //this should not happen
			midiWaitingForIncomingBytesCount = 0;
			midiWaitingForIncomingData = 0;
		}
		
		processedMidiBytes++;
	}
}
     

