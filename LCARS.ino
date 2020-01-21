
// #define DEBUG 1

/*
  LCARS Control Program

  Light Control Program for Enterprise 1701-A retrofit model.

  Outputs:
    D0  -  stobe light sequence ( flash )
    D1  -  navigation lights slower flash sequence
    D6  -  photon torpedo - left (PWM)
    D3  -  photon torpedo - right (PWM)
    D4  -  trusters lights
    D5  -  warp engines blue narcelle's
    D2  -  spot lights
    D7  -  cabin lighting
    D8  -  impulse engines yellow (PWM)
    D9  -  dish & dome R (PWM)
    D10  - dish & dome G (PWM)
    D11  - dish & dome B (PWM)

  Inputs
    A0 - demo mode sequence start/stop
    A1 - static steady / impulse / warp
    A2 - photon torpedos fire sequence
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


// pin assignments
const int strobePin = 0;
const int navigationPin = 1;
const int photonLeftPin = 6;

// control constants
const int strobeFlashDuration = 100;       // in milliseconds
const int strobeGapDuration = 700;
const int navigationFlashDuration = 500;
const int navigationGapDuration = 500;


// PinAnimation
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
void startPinAnimation(PinAnimation *pinAnimation) {
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
#if DEBUG
  Serial.print("Starting animation for ");
  Serial.println( animation->pin );
#endif
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
#if DEBUG
        Serial.print("Pin Animation: ");
        Serial.print( anim->pin);
        Serial.println(" restarting");
#endif
      } else {
        anim->active = false;
#if DEBUG
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
#if DEBUG
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


long navPinDurations[] ={ 0,  900,  900,  -1 };   // timing
int navPinValues[] =    { 0,    1,    0,   0 };    // values

PinAnimation navPinAnim = {
  LED_BUILTIN,      // pin
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
  9,      // pin
  true,   // auto repeat
  false,  // digital on/off
  true,   // active
  false,  // no smooth animation
  strobePinDurations,    // durations
  strobePinValues        // values
};






PinAnimation redPinAnimation;
long redPinDurations[] = {   0, 2000,  500,  500,  500,  500, 500, -1 };
int redPinValues[] =     {   0,  255,  128,  255,   64,  255,    0 };

PinAnimation greenPinAnimation;
long greenPinDurations[] = {   0, 2000,  2000,  2000,  2000,  2000,  2000, -1 };
int greenPinValues[] =     {   0,  255,     0,   255,     0,   255,     0  };

PinAnimation bluePinAnimation;
long bluePinDurations[] = {   0,  1000, 1000, -1 };
int bluePinValues[] =     {   0,   255, 0        };



void setup() {

#if DEBUG
  Serial.begin(9600);
  Serial.println("Setting up...");
#endif

//  pinMode( navPinAnim.pin, OUTPUT );
//  startPinAnimation( &navPinAnim );

  pinMode( strobePinAnim.pin, OUTPUT );
  startPinAnimation( &strobePinAnim );

  
//  pinMode(9, OUTPUT);
//  pinMode(10, OUTPUT);
//
//  redPinAnimation.pin = LED_BUILTIN;
//  redPinAnimation.autoRepeat = false;   // no repeat
//  redPinAnimation.isAnalog = true;
//  redPinAnimation.smoothAnimation = false;    // nothing smooth here yet
//  redPinAnimation.times = redPinDurations;
//  redPinAnimation.values = redPinValues;
//
//  // setup an animation for our Red Value
//  // startPinAnimation( &redPinAnimation );
//  greenPinAnimation.pin = 9;
//  greenPinAnimation.autoRepeat = true;   // repeating
//  greenPinAnimation.isAnalog = true;
//  greenPinAnimation.smoothAnimation = false;    // nothing smooth here yet
//  greenPinAnimation.times = greenPinDurations;
//  greenPinAnimation.values = greenPinValues;
//
//  // setup an animation for our Green Value
//  // startPinAnimation( &greenPinAnimation );
//
//  bluePinAnimation.pin = 10;
//  bluePinAnimation.autoRepeat = true;   // repeating
//  bluePinAnimation.isAnalog = true;
//  bluePinAnimation.smoothAnimation = true;    // nothing smooth here yet
//  bluePinAnimation.times = bluePinDurations;
//  bluePinAnimation.values = bluePinValues;
//
//  // setup an animation for our Green Value
//  startPinAnimation( &bluePinAnimation );
}

void loop() {

  animatePins();

}
