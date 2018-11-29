//2018.01.15
//2018.01.23 - AtTiny84a PORT
//Cakeng

//PIN 13,12,11,10,9,8,6,2 which is AtTiny84a PIN PA0:5 PA7 PB0
//goes to SN76489 data pin 0:7 respectively.
//PIN 7, which is AtTiny84a PIN PA6 is used to provide 2Mhz Clock to SN76489
//PIN 3, which is AtTiny84a PIN PB1 is used to signal Write Enable (Active Low)

#define F_CPU 8000000
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include "C:\Users\Cakeng\Documents\Atmel Studio\7.0\SN76489MIDI\SN76489MIDI\sheet.h"



//////////////////////////// Base Setup //////////////////////////// 
void clockSetup()
{ 
  //System Clock On Timer 0
  TCCR0A = 0;
  TCCR0A |= (1<<WGM01);//CTC Mode, Top OCR0A
  TCCR0B = 0;
  TCCR0B |= (1<<CS01)|(1<<CS00);//1/64 PreScaler

  OCR0A = 155; // 400hz Clock

  TIMSK0 = 0;
  TIMSK0 |= (1<<OCIE0A);
  
  //SN76489 2Mhz Clock On timer 1
  TCCR1A = 0;
  TCCR1A |= (1<<COM1A0);//CTC Mode, Top OCR1A, Toggle OC1A at Compare Match
  TCCR1B = 0;
  TCCR1B |= (1<<CS10)|(1<<WGM12);//No Prescalers. CTC Mode

	OCR1AH = 0;
  OCR1AL = 1; //2Mhz Clock

  TIMSK1 = 0;
  //TIMSK2 |= (1<<OCIE2A);//Output Compare Match A Intterupt Enabled
  
  //sei();//Interrupt Enabled
}

volatile uint8_t mills = 0;// 1/1000 Secs



//////////////////////////// SN76489 Comms //////////////////////////// 
void dataOut(uint8_t data)
{
  PORTB |= 0b00000010;
  PORTA &= 0b01000000;
  PORTB &= 0b11111110;
  PORTB |= ((data&0b00000001));
  PORTA |= ((data&0b00000010)<<6);
  PORTA |= ((data&0b00000100)<<3);
  PORTA |= ((data&0b00001000)<<1);
  PORTA |= ((data&0b00010000)>>1);
  PORTA |= ((data&0b00100000)>>3);
  PORTA |= ((data&0b01000000)>>5);
  PORTA |= ((data&0b10000000)>>7);
  _delay_us(10);
  PORTB &= ~(0b00000010);
  _delay_us(30);
  PORTB |= 0b00000010;
  _delay_us(10);
}

//2Mhz Clock. SN76489 Internal Divider 32. 62500 Base Freq. 
//C2 Freq 261.6/2^2 = 65.406Hz, 62500/956 = 65.3765Hz. Midi Note C4 = 60, C2 = 36.
//Freq Range : C2 ~ C7
uint16_t o2NoteBox[] = 
{978, 924, 872, 823, 777, 733, 692, 653, 616, 582, 549, 518}; 

uint8_t dataBuffer = 0;
uint16_t toneBuffer = 0;
uint8_t octaveP1 = 0;
void sendSound(uint8_t ch, uint8_t tn, uint8_t vol)
{
  dataBuffer = (1<<7)|(ch<<5)|(1<<4)|(0b00001111&(15-vol));//Sets the Volume
  dataOut(dataBuffer);
  toneBuffer = ((o2NoteBox[tn%12])>>(tn/12 - 3));//Outputs the Tone
  dataBuffer = (1<<7)|(ch<<5)|(toneBuffer&0b00001111);
  dataOut(dataBuffer);
  dataBuffer = ((toneBuffer&0b0000001111110000)>>4);
  dataOut(dataBuffer);
}

void sendVolume(uint8_t ch, uint8_t vol)
{
  dataBuffer = (1<<7)|(ch<<5)|(1<<4)|(0b00001111&(15-vol));//Sets the Volume
  dataOut(dataBuffer);
}

//Mode0: Periodic, Mode1: White.
void sendNoise(uint8_t mode, uint8_t shiftRate, uint8_t vol)
{
  dataBuffer = (1<<7)|(3<<5)|(mode<<2)|(3-shiftRate);//Outputs the mode and ShiftRate
  dataOut(dataBuffer);
  dataBuffer = (1<<7)|(3<<5)|(1<<4)|(0b00001111&(15-vol));//Sets the Volume
  dataOut(dataBuffer);
}

void soundOff()
{
  dataOut(0b10011111);
  dataOut(0b10111111);
  dataOut(0b11011111);
  dataOut(0b11111111);
}

void soundFade()
{
  for(uint8_t i = 2; i < 6; i++)
  {
    _delay_ms(400);
    dataOut((0b10010000|i*3));
    dataOut((0b10110000|i*3));
    dataOut((0b11010000|i*3));
    dataOut((0b11110000|i*3));
  }
}


//////////////////////////// Sheet thread Execution //////////////////////////// 
const uint16_t *sheetPtr[] = {sheet0,sheet1,sheet2,sheet3};
uint8_t channelNum = 4;

uint16_t sheetLength[] = {0,0,0,0};
uint16_t channelPosition[] = {0,0,0,0};
uint8_t channelVelocity[] = {0,0,0,0};
uint8_t channelNote[] = {0,0,0,0};

uint16_t currentTicks = 0;
uint8_t channelTicks[] = {0,0,0,0};
uint16_t channelTotalTicks[] = {0,0,0,0};
uint8_t channelTimeConst = 60;

uint8_t channelStopFlags = 0;

uint16_t channelTotalTicks2[] = {0,0,0,0};//DEBUG

 
void readLength()
{
  sheetLength[0] = sizeof(sheet0)/2;
  sheetLength[1] = sizeof(sheet1)/2;
  sheetLength[2] = sizeof(sheet2)/2;
  sheetLength[3] = sizeof(sheet3)/2;
}

void dataRead(uint8_t channelNum , uint16_t data)
{
  channelTicks[channelNum] = ((data>>10)&(0b00111111));

  channelNote[channelNum] = ((data>>3)&(0b01111111));
  
  if(data&(0b00000111))
  {
    channelVelocity[channelNum] = (data&(0b00000111))+8;
    if(channelVelocity[channelNum] > 15)
    {
      channelVelocity[channelNum] = 15;
    }
  }
  else
  {
    channelVelocity[channelNum] = 0;
  }
}

uint8_t noise = 0;
void exc()
{
  cli();
  for (uint8_t i = 0; i < 4; i++)
  {
    if (!(channelStopFlags&(1<<i)))
    {
      if (currentTicks >= channelTicks[i]+channelTotalTicks[i])
      {
        channelPosition[i]++; 
        channelTotalTicks[i] = currentTicks;
        
        channelVelocity[0] = 0;
        uint16_t buf = pgm_read_word(sheetPtr[i]+channelPosition[i]);
        
        dataRead(i, buf);
          
        if (channelPosition[i] >= sheetLength[i])
        {
          channelPosition[i] = 0;
          channelStopFlags |= (1<<i);
          channelVelocity[i] = 0;
        }
        if(i < 3)
        {
          sendSound(i, channelNote[i], channelVelocity[i]);
        }
        else
        {
          noise++;
          if(noise >250)
          {
            noise = 0;
          }
		  if (channelVelocity[i] > 3)
		  {
			  sendNoise(0, 1, channelVelocity[i]-3);
		  }
		  else
		  {
			  sendNoise(0, 1, 0);
		  }
        }
      }

    } 
  }
  if (channelStopFlags == 0b00001111)
  {
    soundFade();
    soundOff();
    _delay_ms(3000);
    mills = 0;
    currentTicks = 0;
    channelStopFlags = 0;
    for (uint8_t i = 0; i < channelNum; i++)
    {   
      channelPosition[i] = 0;
      channelTicks[i] = 0;
      channelTotalTicks[i] = 0;
    }
  }
  sei();
}

uint8_t ledCount = 0;

//////////////////////////// Run  //////////////////////////// 
ISR(TIM0_COMPA_vect)
{
	
  mills++;
  if(channelVelocity[0] > 0)
      {
        PORTB |= (1<<5);
      }
      else
      {
        PORTB &= ~(1<<5);
      }
  if(mills>channelTimeConst)
  {
    mills -= channelTimeConst;
	ledCount++;
	if (ledCount == 15)
	{
		ledCount = 0;
		PORTB ^= (1<<PORTB2);
	}
    exc();
    for (uint8_t i = 0; i < 4; i++)
    {
      if ((currentTicks > channelTotalTicks[i]+channelTicks[i]*2/7)&&(channelTotalTicks[i]+channelTicks[i]-currentTicks < channelTimeConst))
      {
        if(channelVelocity[i]>5)
        {
          sendVolume(i, channelVelocity[i]-3);
        }  
      }
	  /*
	  else if ((currentTicks > channelTotalTicks[i]+channelTicks[i]*4/7)&&(channelTotalTicks[i]+channelTicks[i]-currentTicks < 2*channelTimeConst))
	  {
		  if(channelVelocity[i]>5)
		  {
			  sendVolume(i, channelVelocity[i]-3);
		  }
	  }*/
    }
      currentTicks++;      
  }
}

int main(void)
{
  DDRA |= 0b11111111; //Data direction register Setup, PIN PA0:7
  DDRB |= 0b00000111; //DDR Setup, PIN PB0:3
  
  readLength();
  soundOff();

  clockSetup();
  //Internal 1Khz Clock Setup, Using Timer0
  //SN76489 2Mhz Clock Setup at PA6, PIN 7, Using Timer1
  soundOff();
  sei();
  while(1)
  {
  }
  
}
