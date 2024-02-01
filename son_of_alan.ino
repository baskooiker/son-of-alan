/*
	"Chain-Saw" (A Tool that is also a sequence)
 
 Records an incomming value, then plays it back, quantised to steps.
 
 TR/G input;
 supply this with your clock (for example from another sequencer)
 Input 1 & Knob 1;
 The signal to be recorded. The most simple use-case is simply twisting the knob.
 In this case 12 0'clock results in an output of 0V with the extremes of the knob
 range going from -5 to +5V
 With a signal connected to input 1 the knob will scale that input. Please note
 the Tool can use inputs in the 0-5V range yet outputs in the -5 to +5 range.
 Input 2 & knob 2;
 Sets the loop-length. Both for recording and playback. 8 options are provided, being;
 4, 6, 8, 12, 16, 24, 32 and 64 steps (or clock-pulses).
 Button A;
 Record. Just hold it to record. While you record the Tool wil output continuous values,
 even though it will play back quantisided to the clock later. This way of monitoring will 
 hopefully make hitting the right values easier. You may just record for a few clock 
 pulses, if you wish, to "overdub" on an existing recording, fixing mistakes or introducing
 variations. No "undo" is supplied, which is on purpose, as undo is bad for the soul.
 Release the button to resume playback.
 Led A;
 indicatest the state of TR/G in. So; it'll blink when there is a pulse there.
 
 */

#include "SPI.h" // spi library
#include <avr/interrupt.h> //library for using interupts


//our struct
struct Button
{
  bool state;					//current state of the button as we see it
  unsigned char history;		//history of the last 8 measured states
};

//takes a Button struct and the currently measured value of the relevant pin
//returns true if the state changed at which point "my_button.state" can be used by your program to get the new state
bool updateButton( struct Button *but, bool in)
{
  but->history = but->history << 1;
  but->history = but->history | (unsigned char)in;
  if (but->state)
  {
    if (!but->history)
    {
      but->state = false;
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    if (in)													//we assume buttons do not accidentally turn on
    {
      but->state = true;
      return true;
    }
    else
    {
      return false;
      ;
    }
  }
}

static byte mod_a_pin = A1;
static byte mod_b_pin = A2;
static byte trigger_pin = 4;
static byte but_A_pin = 2;
static byte but_B_pin = 9;
static byte led_a = 3;
static byte led_b = 5;

Button trigger;
Button but_A;
Button but_B;

word out_val = 0;
word mod_a_val;
word mod_b_val;
word read_val;
bool pattern_lock = true;  // Pattern lock enabled
bool state_b = false;
unsigned long trig_count = 0;
unsigned long t = 0;
unsigned long now = 0;
unsigned long loop_counter = 0;
byte num_steps;
word sequence[128];
byte step_table[] = {
  4, 8, 8, 16, 16, 32, 32, 64};

// Binary representation of boolean euclidean rhtyhms of length 16
//int16_t rhythms[17] = { 
//  0,
//  1,     257,   1057,  4369, 
//  4681,  10537, 21673, 21845, 
//  22189, 44461, 56173, 56797, 
//  63421, 65021, 32767, 65535};

const int MAX_V = 4096;

word getRandomValue()
{
  return random(MAX_V);
}

//bool isEuclid(int index, int rhythm)
//{
//  return rhythms[rhythm] >> index & 1;
//}

//void generateEuclideanPattern()
//{
//  for (int bar = 0; bar < 4; bar++)
//  { 
//    int rhythm_index = random(3, 7);
//    int v = 0;
//    for (int i = 0; i < 16; i++)
//    {
//      if (isEuclid(i, rhythm_index))
//      {
//        v = getRandomValue();
//      }
//      sequence[bar * 16 + i] = v;
//    }
//  } 
//}

void generateRamps()
{
  int div = pow(2, random(4)) * 8;
  for (int i = 0; i < 64; i++)
  {
    sequence[i] = ((i % div) * MAX_V) / div;
  }
}

void generateRandom()
{
  for (int i = 0; i < 128; i++)
  {
    sequence[i] = getRandomValue();
  }
}

void resetSequence()
{
  generateRandom();
//  switch(random(3))
//  {
//  case 0:
//    generateRandom();
//    break;
//  case 1:
//    generateRamps();
//    break; 
//  case 2:
//    generateEuclideanPattern();
//    break; 
//  }
}

void setup()
{
  cli();							// disable global interrupts
  /*
	TCCR2A = 0;						// set entire TCCR1A register to 0
   	TCCR2B = 0;						// same for TCCR1B
   	TCCR2B =  0b00000011;			//scaling to a 256th of the CPU freq = 31250 Hz
   	// enable Timer1 overflow interrupt:
   	TIMSK2 |= (1 << TOIE1);			//interupt enable
   	TCNT2 = 0;						//timer pre-loading
   	sei();							//re-enable global interupts
   */

  //set pin(s) to input and output
  pinMode(10, OUTPUT);
  SPI.begin();					// wake up the SPI bus. This is the fast way of controling the DAC
  SPI.setBitOrder(MSBFIRST);
  pinMode(trigger_pin, INPUT_PULLUP);
  pinMode(but_A_pin, INPUT_PULLUP);
  pinMode(but_B_pin, INPUT_PULLUP);

  resetSequence();
}

/*
 *send calculated value to the dac
 */
void out(word value)
{
  byte data;						//spi deals in bytes (8 bits) chunks
  //digitalWrite(10, LOW);		//enable writing
  PORTB &= 0b11111011;			//same except faster
  data = highByte(value);			//this includes the 4 most signifficant bits
  data = 0b00001111 & data;		//the rest of the byte is settings for the dac: mask those
  data = 0b00110000 | data;		//set those setting bits (see DAC data sheet or trust me)
  SPI.transfer(data);				//actually send this out
  data = lowByte(value);			//the 8 least signifficant bits of our value
  SPI.transfer(data);				//send them
  //digitalWrite(10, HIGH);		//close connection, allowing the DAC to update
  PORTB |= 0b00000100;			//same except faster
}


void loop()
{
  mod_b_val = analogRead(mod_b_pin);
  num_steps = step_table[ mod_b_val >> 7 ];
  loop_counter += 1;

  if (updateButton( &but_A, !(digitalRead(but_A_pin))))
  {
    if (but_A.state)
    {
      pattern_lock = !pattern_lock; 
    }
  }
  if (updateButton( &but_B, !(digitalRead(but_B_pin))))
  {
    if (but_B.state)
    {
      resetSequence();
      state_b = !state_b; 
    }
    state_b = but_B.state;
  }

  if (updateButton( &trigger, !(digitalRead(trigger_pin))))
  {
    if (trigger.state)
    {
      if (loop_counter > 20000)
      {
        trig_count = 0;
      }
      loop_counter = 0;
      
      int step_offset = analogRead(mod_a_pin) >> 5;
      int index = (trig_count + step_offset) % num_steps;
      
      if (random(1024) < 16)
      {
        // Play random value without updating pattern.
        out((word)(getRandomValue()));
      }
      else
      {
        out((word)(sequence[index]));
      }

      // See if we want to randomize a step in the sequence
      if (!pattern_lock)
      {
        if (random(1024) < 16)
        {
          sequence[index] = getRandomValue();
        }
      }

      trig_count++;

      // Auto-randomization
//      if (!pattern_lock)
//      {
//        if ((trig_count % (16 * 15)) == 15)
//        {
//           // After 15 bars, after the 15th step, see if we want to randomize the pattern
////          if (random(4096) < 256)
//          {
//            resetSequence();
//          } 
//        }
//      }
    }
  }

  // Update leds
  digitalWrite(led_a, pattern_lock ^ trigger.state);
  digitalWrite(led_b, state_b);
}
