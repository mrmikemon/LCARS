#include "Arduino.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include "EEPROM.h"


#define DEBUG 1
#define ANIM_DEBUG 1

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
typedef byte PinValues[3];


typedef struct PinAnimation {
  // user setable values
  int pinCount;             // number of pins in animation
  int pins[3];                // pins to animate
  bool autoRepeat;          // animation auto repeats
  bool isAnalog;            // select if values are analog (0..255) or digital 0..1
  bool active;              // animation is active
  bool smoothAnimation;     // if using smooth, animator interpolates between values
  long *times;               // array of animation delay durations in milliseconds. -1 indicates end of animation
  PinValues *values;         // array of values for pin digital or analog value

  // working values - used by the animator
  int currentStep;
  PinValues currentValues;     // value for interpolation
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
  // for each pin
  for( int pin=0 ; pin < animation->pinCount ; pin++ ) {
    if ( animation->isAnalog ) {
      animation->currentValues[pin] = animation->values[0][pin];
    } else {
      animation->currentValues[pin] = animation->values[0][pin];
    }
    animation->stepTimestamp = millis();
    animation->active = true;
  }
#ifdef ANIM_DEBUG
  Serial.print("Animation for pins: ");
  for( int pin=0; pin < animation->pinCount ; pin++ ) {
    Serial.print( animation->pins[pin] );
  }
  Serial.println();
  Serial.println("Steps");
  for( int index = 0; 1 ; index++ ) {
    long duration = animation->times[index];
    if( duration < 0 ) break;
    Serial.print(duration);
    Serial.print(": ");
    for(int i=0;i<animation->pinCount;i++) {
      Serial.print( animation->values[index][i] );
      Serial.print(", ");
    }
    Serial.println();
  }
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

    // update the current step
    int duration = anim->times[anim->currentStep];
    if ( currentTimestamp > (anim->stepTimestamp + duration) ) {
      // update to the next step in our animation
      anim->stepTimestamp = currentTimestamp;
      for( int pin=0; pin < anim->pinCount ; pin++ ) {
        anim->currentValues[pin] = anim->values[anim->currentStep][pin];
      }
      anim->currentStep++;
      // get the next duration
      duration = anim->times[anim->currentStep];
      // check if animation has completed, make inactive
      if ( duration < 0 ) {
        // if this animation is an auto repeat, restart it
        if( anim->autoRepeat ) {
          anim->currentStep = 0;
        } else {
          anim->active = false;
          continue;
        }
      }
    }
    
    for(int pin=0; pin< anim->pinCount ; pin++ ) {
      int targetValue = anim->values[anim->currentStep][pin];

      // handle smooth animations differently from discrete values
      if ( anim->smoothAnimation ) {
        // using the timestamp, interpolate to the required value and update
        long dT = currentTimestamp - anim->stepTimestamp;
        int currentValue = anim->currentValues[pin];
        long dV = ((targetValue - currentValue ) * dT) / duration;
#ifdef ANIM_DEBUG
        Serial.print("Pin: ");
        Serial.print(anim->pins[pin]);
        Serial.print("[ ");
        Serial.print( targetValue ); 
        Serial.print(":");
        Serial.print( currentValue + dV ); 
        Serial.print("] ");
        Serial.print(" dt: ");
        Serial.print(dT);
        Serial.print(" dv: ");
        Serial.println(dV);

#endif
        analogWrite( anim->pins[pin], currentValue + dV );
      }  else {
//      Serial.print( targetValue ); 
        digitalWrite( anim->pins[pin], targetValue );
      }
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
PinValues navPinValues[] = { {0}, {1}, {0}, {0} };    // values

PinAnimation navPinAnim = {
  1,                 // pinCount
  { navLightPin },      // pin
  true,   // auto repeat
  false,  // digital (not analog) 
  true,   // active
  false,  // no smooth animation
  navPinDurations,    // durations
  navPinValues        // values
};


long strobePinDurations[] = { 100,  700,  -1 };   // timing
PinValues strobePinValues[] = { {1},  {0},   {0} };    // values

PinAnimation strobePinAnim = {
  1,                 // pinCount
  {strobeLightPin},      // pin
  true,   // auto repeat
  false,  // digital (not analog) 
  true,   // active
  false,  // no smooth animation
  strobePinDurations,    // durations
  strobePinValues        // values
};


// ---------------- Warp Animations -------------------
long warpAnimDurations[] = { 1000, 1000, -1};
PinValues warpAnimValues[] =  { {128}, {255}, {0} };

PinAnimation warpEngineAnim = {
  1,                 // pinCount
  { warpEnginePin },      // pin
  true,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  warpAnimDurations,    // durations
  warpAnimValues       // values
};



// ---------------- Dish Animations -------------------
long dishAnimDurations[] = { 0 , 2000, -1 };a
PinValues dishBlueYellowValues[] = { {0,0,255},  {255,255,0}, {0,0,0} };
PinValues dishYellowBlueValues[] = { {255,255,0},{0, 0 ,255}, {0,0,0} };

PinAnimation dishAnim = {
  3,              // pinCount
  {dishRedPin, dishGreenPin, dishBluePin },      // pins
  false,   // auto repeat
  false,  // digital on/off
  true,   // active
  true,  // no smooth animation
  dishAnimDurations,    // durations
  dishYellowBlueValues        // values
};

void animateDishYellow() {
  // setup values and start/restart the dish animation
  dishAnim.values = dishBlueYellowValues;
  startPinAnimation( &dishAnim );
}

void animateDishBlue() {
  // setup values and start/restart the dish animation
  dishAnim.values = dishYellowBlueValues;
  startPinAnimation( &dishAnim );
}


void turnOffDish() {
  analogWrite( dishBluePin, 0 );
  analogWrite( dishRedPin, 0 );
  analogWrite( dishGreenPin, 0 );
}

void dishRed() {
  analogWrite( dishBluePin, 0 );
  analogWrite( dishRedPin, 255 );
  analogWrite( dishGreenPin, 0 );
}

void dishBlue() {
  analogWrite( dishBluePin, 255 );
  analogWrite( dishRedPin, 0 );
  analogWrite( dishGreenPin, 0 );
}

void dishGreen() {
  analogWrite( dishBluePin, 0 );
  analogWrite( dishRedPin, 0 );
  analogWrite( dishGreenPin, 255 );
}

void setImpulseMode() {
   digitalWrite( impulsePin, 255);
   animateDishYellow();
   warpEngineAnim.active = false;
   analogWrite( warpEnginePin, 0 );
}

void setWarpMode() {
   digitalWrite( impulsePin, 0);
   animateDishBlue();
   startPinAnimation( &warpEngineAnim );
   myDFPlayer.play(3); //play SD:/MP3/0003.mp3
}

// ---------------- Photon Torpedo Animations -------------------
// Use only one animations and update it for each torpedo

long photonPinDurations[] ={ 0,  600,  50, 200, -1 };   // timing
PinValues photonPinValues[] =    { {0},  {32},  {255}, {0},  {0} };    // values

PinAnimation photonPinAnim = {
  1,                // pinCount
  {photonLeftPin},      // pin
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
enum { STATE_DEMO=0, STATE_INTRO, STATE_IDLE, STATE_WARP, STATE_POWERSAVE, STATE_PREFERENCES } state;

enum { PREF_VOLUME=0, PREF_POWERSAVE, PREF_RETRO_SOUND } currentPreference;
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
      photonPinAnim.pins[0] = photonLeftPin;
      startPinAnimation(&photonPinAnim);
      photonState = PHOTON_LEFT;
      myDFPlayer.play(2); //play specific mp3 in SD:/0001.mp3; File Name(0~65535)
      break;
    case PHOTON_LEFT:
      photonPinAnim.pins[0] = photonRightPin;
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

void enterPreferencesMode() {
  state = STATE_PREFERENCES;
  currentPreference = PREF_VOLUME;
  showPreferenceMode();
}

bool selectNextPreference() {
  switch( currentPreference ) {
    case PREF_VOLUME:
      currentPreference = PREF_POWERSAVE;
      return false;
    case PREF_POWERSAVE:
      currentPreference = PREF_RETRO_SOUND;
      return false;
    case PREF_RETRO_SOUND:
      return true;
  }
  showPreferenceMode();  
}

void showPreferenceMode() {  
  switch( currentPreference ) {
    case PREF_VOLUME:
       dishRed();
      break;
    case PREF_POWERSAVE:
       dishGreen();
      break;
    case PREF_RETRO_SOUND:
      dishBlue();
      break;
  }
}

void selectNextPreferenceValue() {

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
  pinMode( navLightPin, OUTPUT );
  startPinAnimation( &navPinAnim );
  pinMode( strobeLightPin, OUTPUT );
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
  timerStart( &powerSaveTimer, powerSaveMinutes*60000, false); 

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
    if( photonPressed && modeButton.pressed ) {
      // enter a settings mode
#ifdef DEBUG
    Serial.println("Idle->Preferences");
#endif
      enterPreferencesMode();
      break;
    }
    if( photonPressed ) {
      launchPhoton();
      break;
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
  case STATE_PREFERENCES:
    if( timerCheck( &stateTimer ) ) {
#ifdef DEBUG
    Serial.println("Preferences->Idle");
#endif
      // timer fired, return to our idle state
      state = STATE_IDLE;
      setImpulseMode();
      break;
    }
    if( modePressed ) {
      // select next preference, if no more prefs - exist preferences mode
      if( selectNextPreference() ) {
#ifdef DEBUG
        Serial.println("Preferences->Idle");
#endif
        state = STATE_IDLE;
        setImpulseMode();
        break;
      }
    }
    if( photonPressed ) {
        selectNextPreferenceValue();
    }
    break;
  }  
  // any button pressed, restart our powersave timer
  if( modePressed || photonPressed || warpPressed ) {
    timerStart( &powerSaveTimer, powerSaveMinutes*60000, false); 
  }

  
  animatePins();
}
