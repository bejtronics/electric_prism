//Install these libraries using the Arduino library manager
////////////////////////////////////////////////////////////////////

#include <Adafruit_NeoPixel.h>
#include <ADCTouch.h>

//Variables and definitions related to capacitive touch
////////////////////////////////////////////////////////

int L_touch_offset, R_touch_offset;     //reference values to remove cap touch offset
#define L_touch_PIN A5
#define R_touch_PIN A0

#define button_press_threshold 250 //milliseconds (below this threshold it's a "tap")
#define button_press_debounce_time 300 //This is the minimum number of milliseconds before you can tap again

//Variables and definitions related to LEDs
//////////////////////////////////////////////////////

#define min_intensity 20  // 0-255, user cannot adjust below this value
#define max_intensity 255 // 0-255, user cannot adjust above this value

#define LEDPIN    9
#define NUMleds   8

// Button and slider related variables
//////////////////////////////////////////////////////

unsigned long button_press_timer; //Used to determine how long a button press is
unsigned long last_button_press_timer; //Used to debounce the touch button signal
bool button_active = 0; //Flag to indicate if a button press is ongoing (1 = yes)
float slider_position = 0; //This variable holds the raw slider position
int last_slider; //Holds the previous filtered slider value, used to detect slider velocity
float slider_velocity; //Change in slider position from one reading to the next
float filt_slider; //This stores a filtered version of the slider value - it's a low-pass recursive filter which stabilizes this value

// Light mode variables
//////////////////////////////////////////////////////

int light_color_mode = 4; //Initialize in mode 4 (off)
int slide_mode = 0;

/* reference information about light_color_mode:
 * these are the included modes -
 * 0: Linear rainbow
 * 1: Solid color
 * 2: Symmetric rainbow
 * 3: White light
 * 4: Off
 */

// Color and brightness variables
//////////////////////////////////////////////////////

float color_change_velocity = 180; //Color shift per loop cycle (a full trip around the color wheel is 65536)
float rainbow_increment = 8192; //Color shift between LEDs (baseline is 65536/8 or 1/8 of the way around the wheel)
uint16_t hue_0 = 0; //This is the base color (left side or center in the rainbow modes)
float light_intensity = 30; //Initialize the light at logw brightness

// Ambient sensor variables
//////////////////////////////////////////////////////

#define Light_sense_PIN A3 //This pin is the ambient light sensor
float ambient_light_filtered;  //Value to store and filter ambient sensor reading
float ambient_last_change = 0;  //Sensor reading when mode was last changed, used to determine if light should turn off automatically

//The NeoPixel object
//////////////////////////////////////////////////////

Adafruit_NeoPixel leds(NUMleds, LEDPIN, NEO_GRB + NEO_KHZ800);

void setup() {
  leds.begin();
  leds.setBrightness(255); 

  L_touch_offset = ADCTouch.read(L_touch_PIN, 500);    //Measure the initial pad capacitance to "zero" the signal
  R_touch_offset = ADCTouch.read(R_touch_PIN, 500);    //Measure the initial pad capacitance to "zero" the signal

  Serial.begin(57600); //Probably wise to comment out if you're not using - might reduce power consumption but haven't tested it

  pinMode(Light_sense_PIN, INPUT);
}

void loop() {
  //Begin by reading the touch sensors
  int L_touch_raw = ADCTouch.read(L_touch_PIN,20);   //20 samples
  int R_touch_raw = ADCTouch.read(R_touch_PIN,20);   //20 samples

  L_touch_raw -= L_touch_offset;       //remove offset from reading
  R_touch_raw -= R_touch_offset;       //remove offset from reading

  if(L_touch_raw < 0) {L_touch_offset -= 1;} //Adjust offset if reading becomes negative, but only by the smallest amount (1)
  if(R_touch_raw < 0) {R_touch_offset -= 1;} //Adjust offset if reading becomes negative, but only by the smallest amount (1)

  // My placeholder for sending data out via serial - use for tuning ambient light sensor and touch functions
  //Serial.println(ambient_light_filtered);// Serial.print(", "); Serial.print(millis()); Serial.print(", "); Serial.print(analogRead(Light_sense_PIN)); Serial.print(", "); Serial.print(L_touch_raw); Serial.print(", "); Serial.println(R_touch_raw);

  //This block executes if a finger touch is detected
  if (L_touch_raw > 20 || R_touch_raw > 20) {
    //Things to do if we just detected a touch for the first time (record time and reset velocity tracking variables)
    if(button_active == 0) {
      button_press_timer = millis();
      button_active = 1;
      //find current position and reset the "last slider" values to avoid false velocity
      //slider position calculation method is explained in more detail below this block
      if (L_touch_raw != 0) {slider_position = 240.0 * log((float)R_touch_raw / (float)L_touch_raw);} else {slider_position = -500;}
      if (slider_position < -500) {slider_position = -500;}
      else if (slider_position > 500) {slider_position = 500;}
      slider_position += 500; // offset to 0-1000 range
      filt_slider = slider_position;
      last_slider = slider_position;           
    }

    //Determine the slider position
    //Position is initially calculated as a value in the range of -500 (left side) to 500 (right side)
    //The log function was added to make the response more linear - without this there is a dead zone in the middle
    if (L_touch_raw != 0) {slider_position = 240.0 * log((float)R_touch_raw / (float)L_touch_raw);} else {slider_position = -500;}
    if (slider_position < -500) {slider_position = -500;}
    else if (slider_position > 500) {slider_position = 500;}
    //The last line makes the slider position fully positive from 0 (full left) to 1000 (full right)
    slider_position += 500; // offset to 0-1000 range
    
    //only re-calc filtered slider position after a little bit of settling time in case it's a press or tap
    if (millis() - button_press_timer > 150) {
      filt_slider = 0.05*slider_position + 0.95*filt_slider; // low pass recursive filter - this smoothes the signal
      //calculate velocity of swipe
      slider_velocity = filt_slider-last_slider;
      last_slider = filt_slider;
    }
    
    //Adjust intensity during swipe in white light mode
    if (light_color_mode == 3) {
      light_intensity += slider_velocity * 0.2; // The multiplier adjusts swipe sensitivity
      if (light_intensity < min_intensity) {light_intensity = min_intensity;}
      if (light_intensity > max_intensity) {light_intensity = max_intensity;}
    }
    
    //Adjust spectrum width during swipe in rainbow banner modes
    else if (light_color_mode == 0 || light_color_mode == 2) {
      rainbow_increment -= slider_velocity*6; // The multiplier adjusts swipe sensitivity
      if (rainbow_increment > 8192) {rainbow_increment = 8192;} // 8192 gives the full spectrum - 8192*8 = 65536
      else if (rainbow_increment < 100) {rainbow_increment = 100;} 
    }
    
    //Adjust color change velocity in solid color mode
    else if (light_color_mode == 1) {
      color_change_velocity += 0.5*slider_velocity; // The multiplier adjusts swipe sensitivity
      if (color_change_velocity > 2000) {color_change_velocity = 2000;}
      else if (color_change_velocity < 50) {color_change_velocity = 50;}
    }
  }
  
  //This block only executes if there is no touch:
  else {
    if (button_active == 1) { //Do this if we just exited a button press
      if ((millis() - button_press_timer) < button_press_threshold && (millis() - last_button_press_timer) > button_press_debounce_time) { //Do this if the press was real short
        if (slider_position < 400) {
          ambient_last_change = ambient_light_filtered;
          light_color_mode += 1;
          if (light_color_mode > 4) {light_color_mode = 0;}
        }
        if (slider_position > 600) {
          slide_mode += 1;
          if (slide_mode > 1) {slide_mode = 0;}
        }
      }
      button_active = 0;
      slider_velocity = 0;
      last_button_press_timer = millis();
    }
  }

  //This code executes regardless of touch or not
  
  ambient_light_filtered = 0.1 * analogRead(Light_sense_PIN) + 0.9*ambient_light_filtered; // Simultaneously reading and low-pass filtering the ambient light sensor

  //This code is not perfect for all situations - it needs work!
  //if(light_color_mode != 4 && (ambient_light_filtered - ambient_last_change) > 75) {light_color_mode = 4;} //Turn off if ambient light has increased
  
  if(slide_mode == 1) {hue_0 += color_change_velocity;}  //Shift the base hue by the color change velocity value if this mode is enabled
  leds.clear(); 

  //Set up the LED colors for modes 0, 1, 3, and 4
  for(long i=0; i<NUMleds; i++) { 
    if (light_color_mode == 0) {leds.setPixelColor(i, leds.ColorHSV(hue_0 + i*rainbow_increment, 255, light_intensity));}
    if (light_color_mode == 1) {leds.setPixelColor(i, leds.ColorHSV(hue_0, 255, light_intensity));}
    if (light_color_mode == 3) {leds.setPixelColor(i, leds.ColorHSV(6007,18,light_intensity));}
    if (light_color_mode == 4) {leds.setPixelColor(i, leds.ColorHSV(0,0,0));}
  }

  //Set up the LED colors for mode 2
    if (light_color_mode == 2) {
      leds.setPixelColor(3, leds.ColorHSV(hue_0, 255, light_intensity));
      leds.setPixelColor(4, leds.ColorHSV(hue_0, 255, light_intensity));
      leds.setPixelColor(2, leds.ColorHSV(hue_0 - 2*rainbow_increment, 255, light_intensity));
      leds.setPixelColor(5, leds.ColorHSV(hue_0 - 2*rainbow_increment, 255, light_intensity));
      leds.setPixelColor(1, leds.ColorHSV(hue_0 - 4*rainbow_increment, 255, light_intensity));
      leds.setPixelColor(6, leds.ColorHSV(hue_0 - 4*rainbow_increment, 255, light_intensity));
      leds.setPixelColor(0, leds.ColorHSV(hue_0 - 6*rainbow_increment, 255, light_intensity));
      leds.setPixelColor(7, leds.ColorHSV(hue_0 - 6*rainbow_increment, 255, light_intensity));
    }
  
  //Send it to the LEDs
  leds.show();  
}
