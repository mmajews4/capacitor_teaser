#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h> // 12-pixel-high font
#include <Adafruit_SSD1306.h>
#include <string.h>

// Constatnt defines
#define CH1_MEASURE_PIN A0
#define CH2_MEASURE_PIN A1
#define CH3_MEASURE_PIN A2
#define CH1_ACTIVATE_PIN 5
#define CH2_ACTIVATE_PIN 6
#define CH3_ACTIVATE_PIN 7
#define MEASURE_PERMIT_PIN 8
#define BUTTON_PIN 2
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SMALL_CHAR_WIDTH 6
#define SMALL_CHAR_HEIGHT 8
#define BIG_CHAR_WIDTH 11
#define BIG_CHAR_HEIGHT 12
#define LONG_PRESS_DELAY 300
#define MIN_ACCEPTED_CHARGE 5 //%
#define RELAY_SWITCHING_TIME 8 //ms
#define NO_CHARGE_MESSAGE_DISPLAY_TIME 1300

// Battery graphic
const uint8_t battery_bitmap[7][8] PROGMEM = {{
    0b01110000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000
},{
    0b01110000, 
    0b10001000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000
},{    
    0b01110000, 
    0b10001000, 
    0b10001000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000
},{    
    0b01110000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b11111000, 
    0b11111000, 
    0b11111000, 
    0b11111000
},{
    0b01110000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b11111000, 
    0b11111000, 
    0b11111000
},{
    0b01110000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b11111000, 
    0b11111000
},{
    0b01110000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b10001000, 
    0b11111000
}};

// Global structs and variables definitons
struct Channel {
    bool selected;
    long int value;
    long int charge;
};
Channel channel[3] = {{0, 1150, 0}, 
                      {0, 1010, 0},
                      {1, 680, 0}};

bool channel_cycle[7][3] = {
    {0,0,1},
    {0,1,0},
    {1,0,0},
    {0,1,1},
    {1,0,1},
    {1,1,0},
    {1,1,1}
};
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool activated = false;                 // Channel activation flag
uint8_t curr_channel_cycle = 0;         // Keeps track of current channel cycle
long int currTime, lastTime;            // Button variables to determine long or short press
volatile bool buttonPressed = false;    // Button interrupt flag


// Button interrupt flag function
void buttonISR() { 
    buttonPressed = true;
    lastTime = millis();
}


// Function calculates x posiotn of a string printed on screen 
int calcStrPosX(char* str, uint8_t size, uint8_t channel){
    //          counting column pixel                    counting offset to left
    return ((SCREEN_WIDTH*(channel*2-1)/6)-((size?BIG_CHAR_WIDTH:SMALL_CHAR_WIDTH)*(strlen(str))/2)-1);
}


// Function updates information on the display
void update_display(){
    // Set up text
    display.clearDisplay();
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(WHITE); // Draw white text

    // Display channel numbers
    // Big chars are drawn from bottom up and samall from up to bottom
    display.setFont(&FreeMonoBold9pt7b);    // big char
    for(int i = 1; i <= 3; i++){
        char str[] = "CHx";
        str[2] = '0'+i;
        display.setCursor(calcStrPosX(str, 1, i), 4+BIG_CHAR_HEIGHT);  
        display.print(str);
    }

    // Display battery procentage
    display.setFont();  // small char
    for(int i = 1; i <= 3; i++){
        char str[10];
        sprintf(str, " %d%%", (int)channel[i-1].charge);

        display.drawBitmap(calcStrPosX(str, 0, i)-1, 4+SMALL_CHAR_HEIGHT*2,\
            battery_bitmap[map(channel[i-1].charge, 0, 100, 6, 0)], 8, 8, SSD1306_WHITE);

        display.setCursor(calcStrPosX(str, 0, i)+1, 5+SMALL_CHAR_HEIGHT*2);
        display.print(str);
    }

    // Display channel capacitance values
    for(int i = 1; i <= 3; i++){
        char str[10];
        sprintf(str, "%du", (int)channel[i-1].value);

        display.setCursor(calcStrPosX(str, 0, i), 4*SMALL_CHAR_HEIGHT);
        display.print(str);   
    }

    // Display total expected charge
    display.setFont(&FreeMonoBold9pt7b);
    char str[10];
    int total_expected_charge = (channel[0].selected?(channel[0].value*channel[0].charge/100):0)+
                                (channel[1].selected?(channel[1].value*channel[1].charge/100):0)+
                                (channel[2].selected?(channel[2].value*channel[2].charge/100):0);
    sprintf(str, "%du", (int)total_expected_charge);
    display.setCursor(calcStrPosX(str, 1, 2), SCREEN_HEIGHT-4);
    display.print(str);

    // Display selected channels
    for(int i = 1; i <= 3; i++){
        if(channel[i-1].selected){
            display.fillRect(ceil(SCREEN_WIDTH*(i-1)/3.0), 0, ceil(SCREEN_WIDTH/3.0), SCREEN_HEIGHT-BIG_CHAR_HEIGHT-8, SSD1306_INVERSE);

            // Channel wrapped in rect while actives
            if(activated){
                display.drawRect(ceil(SCREEN_WIDTH*(i-1)/3.0)+1, 1, SCREEN_WIDTH/3-2, SCREEN_HEIGHT-BIG_CHAR_HEIGHT-10, BLACK);
                display.drawRect(ceil(SCREEN_WIDTH*(i-1)/3.0)+2, 2, SCREEN_WIDTH/3-4, SCREEN_HEIGHT-BIG_CHAR_HEIGHT-12, BLACK);
            }
        }
    }

    display.display();    
}


// Function displays NO CHARGE error message while all channels are not charged
void no_charge_message(){
    char str[] = "NO CHARGE";
    
    display.clearDisplay();
    display.setCursor(calcStrPosX(str, 1, 2), (SCREEN_HEIGHT+BIG_CHAR_HEIGHT)/2);
    display.print(str);
    display.display(); 

    delay(NO_CHARGE_MESSAGE_DISPLAY_TIME);
    update_display();
}


// Functions goes to next cycle and checks if it valid - selected chanel is charged
bool find_valid_cycle(){
    // Cycle to next cycle
    if(curr_channel_cycle < 6){
        curr_channel_cycle++;
    } else {
        curr_channel_cycle = 0;
    }

    // Check if cycle is valid
    for(int i = 0; i < 3; i++){
        if(channel_cycle[curr_channel_cycle][i] == 1 && channel[i].charge < MIN_ACCEPTED_CHARGE){
            return false;
        }
    }
    return true;
}


// Function opens MEASURE_PERMIT relay for as short time as possible to not discharge capacitors and takes measure
void take_measure(){
    delay(2*RELAY_SWITCHING_TIME);          // Just to be sure no voltage or current spike form closing realys goes onto microcontroller 
    digitalWrite(MEASURE_PERMIT_PIN, HIGH);
    delay(RELAY_SWITCHING_TIME);

    channel[0].charge = map(analogRead(CH1_MEASURE_PIN), 0, 1023, 0, 100);
    channel[1].charge = map(analogRead(CH2_MEASURE_PIN), 0, 1023, 0, 100);
    channel[2].charge = map(analogRead(CH3_MEASURE_PIN), 0, 1023, 0, 100);

    digitalWrite(MEASURE_PERMIT_PIN, LOW);
}


void setup(){
    Serial.begin(115200);

    // Display init
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
        for(;;){}; 
    }

    // Initiate all of the pins
    pinMode(CH1_MEASURE_PIN, INPUT);
    pinMode(CH2_MEASURE_PIN, INPUT);
    pinMode(CH3_MEASURE_PIN, INPUT);
    pinMode(CH1_ACTIVATE_PIN, OUTPUT);
    pinMode(CH2_ACTIVATE_PIN, OUTPUT);
    pinMode(CH3_ACTIVATE_PIN, OUTPUT);
    pinMode(MEASURE_PERMIT_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    digitalWrite(CH1_ACTIVATE_PIN, LOW);
    digitalWrite(CH2_ACTIVATE_PIN, LOW);
    digitalWrite(CH3_ACTIVATE_PIN, LOW);
    digitalWrite(MEASURE_PERMIT_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, RISING);

    update_display();
    
    lastTime = currTime = millis();
}

void loop() {  
    // No need to do anthing unless button is pressed
    if(buttonPressed){      // If user didn't unpress button untill LONG_PRESS_DEALY passed then it is long press
        currTime = millis();
        if(currTime - lastTime > LONG_PRESS_DELAY && digitalRead(BUTTON_PIN) == HIGH){ // Long press
            if(channel[0].selected) digitalWrite(CH1_ACTIVATE_PIN, HIGH);
            if(channel[1].selected) digitalWrite(CH2_ACTIVATE_PIN, HIGH);
            if(channel[2].selected) digitalWrite(CH3_ACTIVATE_PIN, HIGH);
            activated = true;

            update_display();

            while(digitalRead(BUTTON_PIN) == HIGH){} // Wait untill button is unpressed

            activated = false;
            digitalWrite(CH1_ACTIVATE_PIN, LOW);
            digitalWrite(CH2_ACTIVATE_PIN, LOW);
            digitalWrite(CH3_ACTIVATE_PIN, LOW);

            take_measure();
            update_display();
            // If we have used current channel or just charged and updated measurments
            // do a short press automatically to switch to another channel
            if(!((channel[0].selected && channel[0].charge < MIN_ACCEPTED_CHARGE)||
                (channel[1].selected && channel[1].charge < MIN_ACCEPTED_CHARGE) ||
                (channel[2].selected && channel[2].charge < MIN_ACCEPTED_CHARGE))&&
                !(!channel[0].selected && !channel[1].selected && !channel[2].selected)){
                buttonPressed = false;  // If we don't unpress button, short press will do automatically
            }

        // Short press 
        } else if(digitalRead(BUTTON_PIN) == LOW){  // If button was unpressed before LONG_PRESS_DEALY passed then it is short press

            // If no channel is charged, display "NO CHARGE" message and don't dispaly selected channel
            if(channel[0].charge < MIN_ACCEPTED_CHARGE &&
                channel[1].charge < MIN_ACCEPTED_CHARGE &&
                channel[2].charge < MIN_ACCEPTED_CHARGE){

                channel[0].selected = 0;
                channel[1].selected = 0;
                channel[2].selected = 0;
                curr_channel_cycle = 0;
                no_charge_message();  

            } else {
                // Cycle through valid options, if found one then selectS channels according to it
                while(find_valid_cycle() == false){}
                
                channel[0].selected = channel_cycle[curr_channel_cycle][0];
                channel[1].selected = channel_cycle[curr_channel_cycle][1];
                channel[2].selected = channel_cycle[curr_channel_cycle][2];

                update_display();
            }
            buttonPressed = false;
        }
    }
}