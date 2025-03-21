#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h> // Import a 12-pixel-high font
#include <Adafruit_SSD1306.h>
#include <string.h>

#include "ssd1306_test.h"

#define CH1_MEASURE_PIN A0
#define CH2_MEASURE_PIN A1
#define CH3_MEASURE_PIN A2
#define CH1_ACTIVATE_PIN 5
#define CH2_ACTIVATE_PIN 6
#define CH3_ACTIVATE_PIN 7
#define MEASURE_PERMIT_PIN 8
#define BUTTON_PIN 2
#define SMALL_CHAR_WIDTH 6
#define SMALL_CHAR_HEIGHT 8
#define BIG_CHAR_WIDTH 11
#define BIG_CHAR_HEIGHT 12
#define LONG_PRESS_DELAY 800
#define MIN_ACCEPTED_CHARGE 5 //%

// Button interrupt flag
volatile bool buttonPressed = false;

// Button interrupt flag function
void buttonISR() { 
    buttonPressed = true;
    lastTime = millis();
}

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

bool channel_cycle[7][3] = {
    {0,0,1},
    {0,1,0},
    {1,0,0},
    {0,1,1},
    {1,0,1},
    {1,1,0},
    {1,1,1}
};
uint8_t curr_channel_cycle = 0;

class Channel {

public:
    bool selected;
    long int value;
    long int charge;

    Channel(bool s, int v, int ch){
        selected = s;
        value = v;
        charge = ch;
    }
};

Channel channel[3] = {{0, 1150, 7}, 
                      {1, 1010, 42},
                      {0, 680, 70}};

bool activated = false;

long int currTime, lastTime;

int calcStrPosX(char* str, uint8_t size, uint8_t channel){
    //          counting column pixel                    counting offset to left
    return ((SCREEN_WIDTH*(channel*2-1)/6)-((size?BIG_CHAR_WIDTH:SMALL_CHAR_WIDTH)*(strlen(str))/2)-1);
}


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
        sprintf(str, "%du", channel[i-1].value);

        display.setCursor(calcStrPosX(str, 0, i), 4*SMALL_CHAR_HEIGHT);
        display.print(str);   
    }

    // Display total expected charge
    display.setFont(&FreeMonoBold9pt7b);
    char str[10];
    int total_expected_charge = (channel[0].value*channel[0].charge/100)+
                                (channel[1].value*channel[1].charge/100)+
                                (channel[2].value*channel[2].charge/100);
    sprintf(str, "%du", total_expected_charge);
    display.setCursor(calcStrPosX(str, 1, 2), SCREEN_HEIGHT-4);
    display.print(str);

    // Display selected channels
    for(int i = 1; i <= 3; i++){
        if(channel[i-1].selected){
            display.fillRect(SCREEN_WIDTH*(i-1)/3, 0, SCREEN_WIDTH/3, SCREEN_HEIGHT-BIG_CHAR_HEIGHT-8, SSD1306_INVERSE);

            // Channel wrapped in rect while active
            if(activated){
                display.drawRect((SCREEN_WIDTH*(i-1)/3)+1, 1, SCREEN_WIDTH/3-2, SCREEN_HEIGHT-BIG_CHAR_HEIGHT-10, SSD1306_INVERSE);
                display.drawRect((SCREEN_WIDTH*(i-1)/3)+2, 2, SCREEN_WIDTH/3-4, SCREEN_HEIGHT-BIG_CHAR_HEIGHT-12, SSD1306_INVERSE);
            }
        }
    }

    display.display();    
}


void no_charge_message(){



    delay(1000);
    update_display();
}


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


void take_measure(){

}




void setup(){
    Serial.begin(115200);

    // Display init
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
        for(;;){}; 
    }

    pinMode(CH1_MEASURE_PIN, OUTPUT);
    pinMode(CH2_MEASURE_PIN, OUTPUT);
    pinMode(CH3_MEASURE_PIN, OUTPUT);
    pinMode(CH1_ACTIVATE_PIN, INPUT);
    pinMode(CH2_ACTIVATE_PIN, INPUT);
    pinMode(CH3_ACTIVATE_PIN, INPUT);
    pinMode(MEASURE_PERMIT_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    digitalWrite(CH1_MEASURE_PIN, LOW);
    digitalWrite(CH2_MEASURE_PIN, LOW);
    digitalWrite(CH3_MEASURE_PIN, LOW);
    digitalWrite(MEASURE_PERMIT_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, RISING);

    update_display();
    
    lastTime = currTime = millis();
}

void loop() {  
    // Long press
    if(buttonPressed){      // If user didn't unpress button untill LONG_PRESS_DEALY passed then it is long press
        currTime = millis();
        if(currTime - lastTime > LONG_PRESS_DELAY && digitalRead(BUTTON_PIN) == HIGH){ // Long press
            if(channel[0].selected) digitalWrite(CH1_ACTIVATE_PIN, HIGH);
            if(channel[1].selected) digitalWrite(CH2_ACTIVATE_PIN, HIGH);
            if(channel[2].selected) digitalWrite(CH3_ACTIVATE_PIN, HIGH);
            activated = true;
            update_display();

            while(digitalRead(BUTTON_PIN) == HIGH){} // Wait untill button is unpressed

            take_measure();
            update_display();
            buttonPressed = false;
        } else if(digitalRead(BUTTON_PIN) == LOW){  // If button was unpressed before LONG_PRESS_DEALY passed then it is short press
            // If no channel are charged display "NO CHARGE" message
            if(channel[0].value < MIN_ACCEPTED_CHARGE &&
               channel[1].value < MIN_ACCEPTED_CHARGE &&
               channel[2].value < MIN_ACCEPTED_CHARGE){
                no_charge_message();
            } else {
                // Cycle through valid options, if found one then select channels according to it
                while(find_valid_cycle() == false){
                    channel[0].selected = channel_cycle[curr_channel_cycle][0];
                    channel[1].selected = channel_cycle[curr_channel_cycle][1];
                    channel[2].selected = channel_cycle[curr_channel_cycle][2];
                }
            }
        }
        
    }


    // Short press 
}