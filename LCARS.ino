#include "Arduino.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include "EEPROM.h"


#define DEBUG 1
// #define DEBUG_ANIM 1

/*
  LCARS Control Program

  Light Control Program for Enterprise 1701-A retrofit model.
  
  Outputs:
    Pin   Description
*/
const int impulsePin =     2;   //  impulse engines yellow (PWM)
const int dishBluePin = 3;      //  dish & dome B (PWM)
const int trustersPin = 4;      //  trusters lights
const int dishRedPin =  5;      //  dish & dome R (PWM)
const int dishGreenPin = 6;     //  dish & dome G (PWM)
const int navLightPin = 7;         //  navigation lights slower flash sequence 
const int cabinLightPin = 8;       //  cabin lighting 
const int warpEnginePin =  9;     //     warp engines blue narcelle's
const int photonLeftPin = 10;     //  photon torpedo - left (PWM)
const int photonRightPin = 11;    //  photon torpedo - right (PWM)
const int strobeLightPin  =   12;   //   strobe light sequence ( flash )
const int spotLightPin = 13;        //     Spot lights

const int warpButtonPin = 16;      // a2
const int photonButtonPin = 17;      // a3
const int modeButtonPin = 18;      // a4
const int audioRxPin = 15;
const int audioTxPin = 14;

  // restore saved settings from EEPROM
const int EEPROM_ADDRESS_VOLUME = 0;
const int EEPROM_ADDRESS_POWERSAVE_MINS = 1;
const int EEPROM_ADDRESS_RETRO_SOUNDS = 2 ;

/*
    On Sequence - sync to star trek theme

    00.0 - all off
    02.0 - navigation lights
    03.5 - strobe lights
    05.0 - cabin lights & spotlights
    07.0 - bring dish up from off to yellow @ 10.0 

*/




/*
    Proposed demo sequence

    bring on stobes - 3 seconds
    bring on navigation lights - 2 seconds
    cabin lights - 2 seconds
    spot lights
    dish comes to 1/2 yellow
    photon torpedos to low intensity - 10% ??
    left photon torpedo pulse
    right photon torpedo pulse
    photon torpedoes to off
    dish goes to full yellow, impulse engines to full yellow
    dish goes to 1/2 blue
    dish does full blue, impulse engines fade, warp engines blue

*/

//=================================================================================================
//
// Audio Playback - DFPlayer
//
//=================================================================================================
SoftwareSerial mySoftwareSerial(audioRxPin, audioTxPin); // RX, TX
DFRobotDFPlayerMini myDFPlayer;

//=================================================================================================
//  Timer - simple poll based timer
//
//  Usage:  1. Allocate Timer
//          2. startTimer
//          3. Call timerCheck - returns true on a timer fire
//=================================================================================================
typedef struct Timer {
  unsigned long timeStamp;
  unsigned long duration;
  bool continuous;
} Timer;

// initialise the timer and commence timing
void timerStart(Timer *timer, unsigned long duration,bool continuous )
{
  timer->timeStamp = millis();
  timer->duration = duration;
  timer->continuous = continuous;
}

void timerStop(Timer *timer)
{
  timer->duration = -1;
}

// check timer expired - returns true on fire
bool timerCheck(Timer *timer) {
  // if timer is running
  if( timer->duration > 0 ) { 
    // has timer has fired
    if( (millis() - timer->timeStamp) > timer->duration ) {
      if( timer->continuous ) {
        timer->timeStamp = millis();
      } else {
        timer->duration = -1;   // timer now disabled
      }
      return true;
    }
  }
  return false;
}


//=================================================================================================
//  Button - button with debouce 
//
//  Usage:  1. Allocate Button
//          2. buttonInit
//          3. Call buttonCheck - returns true on a state change
//=================================================================================================

typedef struct Button {
  int pinNo;
  bool pressed;
  bool previousState;
  unsigned long pressMilli;
} Button;

void buttonInit(struct Button *button,int pinNo) {
  button->pinNo = pinNo;
  pinMode(button->pinNo,INPUT);
}

// check if button pressed with debounce
bool buttonCheck(struct Button *button) {

  const long debounceDelay = 50;    // 50 ms for debounce

  bool reading = digitalRead(button->pinNo) == HIGH;
  if( button->previousState != reading ) {
    // Serial.println("Delta");
    // we have an edge, start the timer
    button->pressMilli = millis();
  }
  // check if we have held the state for long enough
  if ((millis() - button->pressMilli ) > debounceDelay) {
    // Serial.println("Timeup");
    if( button->pressed != reading ) {
      // Serial.println("Toggled");
      // state has been held long enough record the current
      button->pressed = reading;
      return true;    // indicates the state changed
    }
  }
  button->previousState = reading;
  return false; // not held long enough, no press
}

//=================================================================================================
//
// PinAnimation
//
//=================================================================================================
typedef struct PinAnimation {
  // user setable values
  int pin;                  // pin to animate
  bool autoRepeat;          // animation auto repeats
  bool isAnalog;            // select if values are analog (0..255) or digital 0..1
  bool active;              // animation is active
  bool smoothAnimation;     // if using smooth, animator interpolates between values
  long *times;               // array of animation delay durations in milliseconds. -1 indicates end of animation
  int  *values;              // array of values for pin digital or analog value

  // working values - used by the animator
  int currentStep;
  int currentValue;
  unsigned long stepTimestamp;  // timestamp of last step executed
} PinAnimation;


const int MAX_PIN_ANIMATIONS = 20;
int numAnimations = 0;
PinAnimation *pinAnimations[MAX_PIN_ANIMATIONS];

// commence animation of a pin
void startPinAnimation(struct PinAnimation *pinAnimation) {
  PinAnimation *animation = NULL;
  // check if animation already in active animations, if not add it
  for ( int i = 0; i < numAnimations ; i++ ) {
    if ( pinAnimations[i] == pinAnimation ) {
      animation = pinAnimation;
      break;
    }
  }
  if ( !animation ) {
    // add animation to next empty slote
    pinAnimations[ numAnimations ] = pinAnimation;
    numAnimations++;
    animation = pinAnimation;
  }
  // initalise values for the animation
  animation->currentStep = 0;
  if ( animation->isAnalog ) {
    animation->currentValue = analogRead( animation->pin );
  } else {
    animation->currentValue = digitalRead( animation->pin );
  }
  animation->stepTimestamp = millis();
  animation->active = true;
#if DEBUG_ANIM
  Serial.print("Starting animation for ");
  Serial.println( animation->pin );
#endif
}

void stopPinAnimation(struct PinAnimation *pinAnimation) {
  pinAnimation->active = false;
}


// update active animations
void animatePins() {

  unsigned long currentTimestamp = millis();

  // check for the active animations
  for ( int i = 0; i < numAnimations ; i++ ) {
    PinAnimation *anim = pinAnimations[i];
    // skip inactive anmiations
    if ( !anim->active ) continue;

    int duration = anim->times[anim->currentStep];
    // check if animation has completed, make inactive
    if ( duration < 0 ) {
      // if this animation is an auto repeat, restart it
      if( anim->autoRepeat ) {
        anim->currentStep = 0;
#if DEBUG_ANIM
        Serial.print("Pin Animation: ");
        Serial.print( anim->pin);
        Serial.println(" restarting");
#endif
      } else {
        anim->active = false;
#if DEBUG_ANIM
        Serial.print("Pin Animation: ");
        Serial.print( anim->pin);
        Serial.println(" finished");
#endif
        continue;
      }
    }
    int targetValue = anim->values[anim->currentStep];

    // handle smooth animations differently from discrete values
    if ( anim->smoothAnimation ) {
      // using the timestamp, interpolate to the required value and update
      long dT = currentTimestamp - anim->stepTimestamp;
      long dV = ((targetValue - anim->currentValue) * dT) / duration;
#if DEBUG_ANIM
      Serial.print( "Smooth anim( dT: ");
      Serial.print(dT);
      Serial.print(" value:");
      Serial.println( anim->currentValue + dV );
#endif
      analogWrite( anim->pin, anim->currentValue + dV );
    }

    // check if duration has been reached on animation, if so then set the new value
    if ( currentTimestamp - anim->stepTimestamp > duration ) {
      // change if required
      if ( anim->currentValue != targetValue ) {
        if ( anim->isAnalog ) {
          analogWrite( anim->pin, targetValue );
        } else {
          digitalWrite( anim->pin, targetValue );
        }
        anim->currentValue = targetValue;
      }
      // update to the next step in our animation
      anim->stepTimestamp = currentTimestamp;
      anim->currentStep++;
    }
  }
}


//=================================================================================================
//
//  PinAnimation Data
//
//=================================================================================================

// ---------------- Nav & Strobe Animations -------------------
long navPinDurations[] ={ 0,  900,  900,  -1 };   // timing
int navPinValues[] =    { 0,    1,    0,   0 };    // values

PinAnimation navPinAnim = {
  navLightPin,      // pin
  true,   // auto repeat
  false,  // digital on/off
  true,   // active
  false,  // no smooth animation
  navPinDurations,    // durations
  navPinValues        // values
};


long strobePinDurations[] ={ 100,  700,  -1 };   // timing
int strobePinValues[] =    { 0,    1,   0 };    // values

PinAnimation strobePinAnim = {
  strobeLightPin,      // pin
  true,   // auto repeat
  false,  // digital on/off
  true,   // active
  false,  // no smooth animation
  strobePinDurations,    // durations
  strobePinValues        // values
};


// ---------------- Warp Animations -------------------
long warpAnimDurations[] = { 1000, 1000, -1};
int warpAnimValues[] =     { 128, 255, 128 };

PinAnimation warpEngineAnim = {
  warpEnginePin,      // pin
  true,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  warpAnimDurations,    // durations
  warpAnimValues       // values
};



// ---------------- Dish Animations -------------------
long dishAnimDurations[] = {   0, 2000, -1 };
int dishYellowBlueValuesR[] =  {  255,  0, 0 };
int dishYellowBlueValuesG[] =  {  255,  0, 0 };
int dishYellowBlueValuesB[] =  {  0,  255, 0 };

int dishBlueYellowValuesR[] =  {    0,  255, 0 };
int dishBlueYellowValuesG[] =  {    0,  255, 0 };
int dishBlueYellowValuesB[] =  {  255,  0, 0 };

PinAnimation dishAnimRed = {
  dishRedPin,      // pin
  false,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  dishAnimDurations,    // durations
  dishYellowBlueValuesR        // values
};

PinAnimation dishAnimGreen = {
  dishGreenPin,      // pin
  false,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  dishAnimDurations,    // durations
  dishYellowBlueValuesG        // values
};

PinAnimation dishAnimBlue = {
  dishBluePin,      // pin
  false,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  dishAnimDurations,    // durations
  dishYellowBlueValuesB        // values
};

void animateDishBlueYellow() {
  // setup values and start/restart the dish animation
  dishAnimRed.values = dishBlueYellowValuesR;
  dishAnimGreen.values = dishBlueYellowValuesG;
  dishAnimBlue.values = dishBlueYellowValuesB;
  startPinAnimation( &dishAnimRed );
  startPinAnimation( &dishAnimGreen );
  startPinAnimation( &dishAnimBlue );
}

void animateDishYellowBlue() {
  // setup values and start/restart the dish animation
  dishAnimRed.values = dishYellowBlueValuesR;
  dishAnimGreen.values = dishYellowBlueValuesG;
  dishAnimBlue.values = dishYellowBlueValuesB;
  startPinAnimation( &dishAnimRed );
  startPinAnimation( &dishAnimGreen );
  startPinAnimation( &dishAnimBlue );
}

void turnOffDish() {
  analogWrite( dishBluePin, 0 );
  analogWrite( dishRedPin, 0 );
  analogWrite( dishGreenPin, 0 );
}


void setImpulseMode() {
   digitalWrite( impulsePin, 255);
   animateDishBlueYellow();
   warpEngineAnim.active = false;
   analogWrite( warpEngineAnim.pin, 0 );
}

void setWarpMode() {
   digitalWrite( impulsePin, 0);
   animateDishYellowBlue();
   startPinAnimation( &warpEngineAnim );
   myDFPlayer.play(3); //play SD:/MP3/0003.mp3
}

// ---------------- Photon Torpedo Animations -------------------
// Use only one animations and update it for each torpedo

long photonPinDurations[] ={ 0,  600,  50, 200, -1 };   // timing
int photonPinValues[] =    { 0,  32,  255, 0,  0 };    // values

PinAnimation photonPinAnim = {
  photonLeftPin,      // pin
  false,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  //  smooth animation
  photonPinDurations,    // durations
  photonPinValues        // values
};


//=================================================================================================
//  State Information
//=================================================================================================

bool impulseMode = true;
typedef enum { PHOTON_INACTIVE=0, PHOTON_LEFT, PHOTON_RIGHT } PhotonState;
PhotonState photonState = PHOTON_INACTIVE;   // no firing
enum { STATE_DEMO=0, STATE_INTRO, STATE_IDLE, STATE_WARP, STATE_POWERSAVE } state;

// Settings - pulled from EEPROM
byte volume;
byte powerSaveMinutes;
byte retroSounds;

// Timers

Timer stateTimer;
Timer powerSaveTimer;

// Buttons

Button modeButton;
Button photonButton;
Button warpButton;



void launchPhoton() {
  //check our state and advance through firing
  switch( photonState ) {
    case PHOTON_INACTIVE:
      // okay kick off our left photon torpedo animation and update state
      photonPinAnim.pin = photonLeftPin;
      startPinAnimation(&photonPinAnim);
      photonState = PHOTON_LEFT;
      myDFPlayer.play(2); //play specific mp3 in SD:/0001.mp3; File Name(0~65535)
      break;
    case PHOTON_LEFT:
      photonPinAnim.pin = photonRightPin;
      startPinAnimation(&photonPinAnim);
      photonState = PHOTON_INACTIVE;
      myDFPlayer.play(2); //play specific mp3 in SD:/0001.mp3; File Name(0~65535)
      break;
    case PHOTON_RIGHT:
    default:
      break;
  }  
}


void enterPowerSaveMode()
{
  turnOffDish();
  stopPinAnimation( &strobePinAnim );
  digitalWrite( cabinLightPin , LOW );
  digitalWrite( spotLightPin, LOW );  
}

void exitPowerSaveMode()
{
  setImpulseMode();
  startPinAnimation( &strobePinAnim );
  digitalWrite( cabinLightPin , HIGH );
  digitalWrite( spotLightPin, HIGH );
}




//=================================================================================================
//
//  Setup
//
//=================================================================================================

void setup() {

#if DEBUG
  Serial.begin(57600);
  Serial.println("Setting up...");
#endif

  // restore saved settings from EEPROM
  volume = EEPROM.read( EEPROM_ADDRESS_VOLUME );
  if( volume == 255 ) volume = 15;          // setup volume to default value 15 (mid volume)
  
  powerSaveMinutes = EEPROM.read( EEPROM_ADDRESS_POWERSAVE_MINS );
  if( powerSaveMinutes == 255 ) powerSaveMinutes = 2;   // default to 2 minutes for power save timer

  retroSounds = EEPROM.read( EEPROM_ADDRESS_RETRO_SOUNDS );
  if( retroSounds == 255 ) retroSounds = 1;         // default value (retrosounds to be used)

  // set initial pin values
  pinMode( navPinAnim.pin, OUTPUT );
  startPinAnimation( &navPinAnim );
  pinMode( strobePinAnim.pin, OUTPUT );
  startPinAnimation( &strobePinAnim );
  pinMode( cabinLightPin, OUTPUT );
  digitalWrite( cabinLightPin , HIGH );
  digitalWrite( spotLightPin, HIGH );

  analogWrite( warpEnginePin, 0 );
  setImpulseMode();

  buttonInit(&warpButton, warpButtonPin);
  buttonInit(&photonButton, photonButtonPin);
  buttonInit(&modeButton, modeButtonPin);

  // initialise audio
  mySoftwareSerial.begin(9600);
  if( !myDFPlayer.begin(mySoftwareSerial) ) {
#ifdef DEBUG
    Serial.println("DFPlayer failed to initialise");
#endif
    return;
  }
  myDFPlayer.setTimeOut(500);
  myDFPlayer.volume(volume);    // maximum volume (0-30)
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);    // nomal equaliser
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);    // using an SD
  // play the intro 0001intro.mp3
  myDFPlayer.play(1); //play specific mp3 in SD:/MP3/0004.mp3; File Name(0~65535)

  state = STATE_INTRO;
  timerStart( &stateTimer, 10000, false);
  timerStart( &powerSaveTimer, powerSaveMinutes*1000, false); 

  // startPinAnimation(&photonPinAnim);

//  analogWrite( dishBluePin, 0 );
//  analogWrite( dishRedPin, 25 );
//  analogWrite( dishGreenPin, 15 );

}

/*
    bring on stobes - 3 seconds
    bring on navigation lights - 2 seconds
    cabin lights - 2 seconds
    spot lights
    dish comes to 1/2 yellow
    photon torpedos to low intensity - 10% ??
    left photon torpedo pulse
    right photon torpedo pulse
    photon torpedoes to off
    dish goes to full yellow, impulse engines to full yellow
    dish goes to 1/2 blue
    dish does full blue, impulse engines fade, warp engines blue
*/

//=================================================================================================
//
//  Loop
//
//=================================================================================================
void loop() {

  // check button status
  bool modePressed = buttonCheck(&modeButton) && modeButton.pressed;
  bool photonPressed = buttonCheck(&photonButton) && photonButton.pressed;
  bool warpPressed = buttonCheck(&warpButton) && warpButton.pressed;



  switch( state ) {
  case STATE_INTRO:
    // wait for intro sequence to complete
    if( timerCheck( &stateTimer ) ) {
      // move to idle
      state = STATE_IDLE;
#ifdef DEBUG
    Serial.println("Intro->Idle");
#endif

    }
    break;
  case STATE_IDLE:
    if( timerCheck(&powerSaveTimer) ) {
#ifdef DEBUG
    Serial.println("Idle->Powersave");
#endif
      enterPowerSaveMode();
      state = STATE_POWERSAVE;
      break;
    }
    if( photonPressed ) {
      launchPhoton();
    }
    if( warpPressed ) {
      // enter warp mode and set timer to return to idle
      setWarpMode();
      timerStart( &stateTimer, 31000, false );
      state = STATE_WARP;
#ifdef DEBUG
    Serial.println("Idle->Warp");
#endif
    }    
    break;
  case STATE_WARP:
    if( timerCheck(&stateTimer) ) {
       setImpulseMode();
       state = STATE_IDLE;
#ifdef DEBUG
    Serial.println("Warp->Idle");
#endif
    }
    break;
  case STATE_POWERSAVE:
    // we are in powersave, if any buttons pressed, return to idle
    if( modePressed || photonPressed || warpPressed ) {
      exitPowerSaveMode();
      state = STATE_IDLE;
#ifdef DEBUG
    Serial.println("Powersave->Idle");
#endif
    }  
    break;  
  }  
  // any button pressed, restart our powersave timer
  if( modePressed || photonPressed || warpPressed ) {
    timerStart( &powerSaveTimer, powerSaveMinutes*1000, false); 
  }

  
  animatePins();
}
