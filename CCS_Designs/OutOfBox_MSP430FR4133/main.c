#include "main.h"
#include "hal_LCD.h"
#include "StopWatchMode.h"
#include "TempSensorMode.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
#include <msp430.h>
#include "driverlib.h"
#include "Board.h"
#include "stdbool.h"
#include "stdlib.h"
#include <stdio.h>
#include <string.h>


#define TRIG_CYCLE 10 // 87.2us delay

// LEDS
#define LED1 GPIO_PORT_P8, GPIO_PIN2
#define LED2 GPIO_PORT_P2, GPIO_PIN5
#define LED3 GPIO_PORT_P8, GPIO_PIN0
#define LED4 GPIO_PORT_P2, GPIO_PIN7
// USD
#define USDT GPIO_PORT_P8, GPIO_PIN1
#define USD1 GPIO_PORT_P1, GPIO_PIN1
#define USD2 GPIO_PORT_P1, GPIO_PIN0
#define USD3 GPIO_PORT_P1, GPIO_PIN6
#define USD4 GPIO_PORT_P1, GPIO_PIN7
// KEYPAD
#define COL2 GPIO_PORT_P1, GPIO_PIN5
#define COL1 GPIO_PORT_P1, GPIO_PIN4
#define COL3 GPIO_PORT_P1, GPIO_PIN3
#define ROW1 GPIO_PORT_P5, GPIO_PIN1
#define ROW2 GPIO_PORT_P5, GPIO_PIN0
#define ROW3 GPIO_PORT_P5, GPIO_PIN2
#define ROW4 GPIO_PORT_P5, GPIO_PIN3
//PWM
#define PWM GPIO_PORT_P8, GPIO_PIN3

#define DISTANCE_THRESH 3000
#define ROT_BUF_LEN 4

//FOR ALARM
#define completePeriod 200

char hexaKeys[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'S','0','H'}
};
char pressedKey;
char * keypad_buf;
enum states {ARM_INPUT, SCANNING, ALARM} STATE;

int alarm_state[4] = {0,0,0,0};
int alarm_mask[4]  = {0,0,0,0};
long distances[4] = {0,0,0,0};

uint16_t start;
uint16_t stop;
int expecting_USD;

char str_buf[100];
char rot_buf[ROT_BUF_LEN] = {'0','0','0','0'};
char password[ROT_BUF_LEN] = {'2','4','6','8'};
unsigned int rot_buf_ind = 0;




void Key();
void activate_alarm();
void init_GPIO();
void init_KEY();
void arm_sensors();
void init_TIMER();
void init_ECHO();
long measure_usd(uint8_t port, uint16_t pin);
int abs(int a);
void set_leds();
///////////////////////////////////////////////////////////////////////////////////////////////////

// Backup Memory variables to track states through LPM3.5
volatile unsigned char * S1buttonDebounce = &BAKMEM2_L;       // S1 button debounce flag
volatile unsigned char * S2buttonDebounce = &BAKMEM2_H;       // S2 button debounce flag
volatile unsigned char * stopWatchRunning = &BAKMEM3_L;       // Stopwatch running flag
volatile unsigned char * tempSensorRunning = &BAKMEM3_H;      // Temp Sensor running flag
volatile unsigned char * mode = &BAKMEM4_L;                   // mode flag
volatile unsigned int holdCount = 0;

// TimerA0 UpMode Configuration Parameter
Timer_A_initUpModeParam initUpParam_A0 =
{
        TIMER_A_CLOCKSOURCE_SMCLK,              // SMCLK Clock Source
        TIMER_A_CLOCKSOURCE_DIVIDER_1,          // SMCLK/1 = 2MHz
        30000,                                  // 15ms debounce period
        TIMER_A_TAIE_INTERRUPT_DISABLE,         // Disable Timer interrupt
        TIMER_A_CCIE_CCR0_INTERRUPT_ENABLE ,    // Enable CCR0 interrupt
        TIMER_A_DO_CLEAR,                       // Clear value
        true                                    // Start Timer
};
///////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * main.c
 */
int main(void) {
    // Stop Watchdog timer
    WDT_A_hold(__MSP430_BASEADDRESS_WDT_A__);     // Stop WDT
    pressedKey = ' ';
    STATE = ARM_INPUT;
    int i;

    Init_LCD();
    init_GPIO();
    init_ECHO();
    init_KEY();
    init_TIMER();

    PMM_unlockLPM5();
    _EINT();        // Start interrupt
    PMM_unlockLPM5();

    displayScrollText("HI");

//    //breaks if: 2 alarm_mask set AND SET LEDS
//    if (alarm_mask[0]) GPIO_setOutputHighOnPin(LED1);
//    else GPIO_setOutputLowOnPin(LED1);
//
//    if (alarm_mask[1]) GPIO_setOutputHighOnPin(LED2);
//    else GPIO_setOutputLowOnPin(LED2);
//
//    if (alarm_mask[2]) GPIO_setOutputHighOnPin(LED3);
//    else GPIO_setOutputLowOnPin(LED3);
//
//    if (alarm_mask[3]) GPIO_setOutputHighOnPin(LED4);
//    else GPIO_setOutputLowOnPin(LED4);


    arm_sensors();
//    set_leds();
    while(1){
        if (STATE == ARM_INPUT){
            displayScrollText("1 TO 4 TO TOGGLE ZONES");
            if (pressedKey == '0') {
                arm_sensors();
                continue;
            }
            displayScrollText("0 TO TOGGLE SCAN");
            if (pressedKey == '0') arm_sensors();
        }
        if (STATE == SCANNING){
//            showChar(alarm_mask[0]+48, pos1);
//            showChar(alarm_mask[1]+48, pos2);
//            showChar(alarm_mask[2]+48, pos3);
//            showChar(alarm_mask[3]+48, pos4);
            showChar('S', pos1);
            showChar('C', pos2);
            showChar('A', pos3);
            showChar('N', pos4);

            _delay_cycles(1000000);

            //this  also did not work
//            set_leds();

            if (alarm_mask[0]) measure_usd(USD1);
            if (alarm_mask[1]) measure_usd(USD2);
            if (alarm_mask[2]) measure_usd(USD3);
            if (alarm_mask[3]) measure_usd(USD4);

            for (i = 0; i < 4; i++){
                if (alarm_mask[i] && alarm_state[i]) activate_alarm();
            }
        }
    }

    // LOOP MESSAGE "PRESS 1-4 TO ARM/DISARM"
    // DURING LOOP, ACCEPT INTERRUPTS FROM KEYBOARD
    // IF KEYPRESSED = '1' => TOGGLE ARM/DISARM OF LED1
    // DURING LOOP, POLL ALL USDS THAT ARE ARMED
    // IF ANY ALARM BIT IS SET, GO INTO ACTIVATE_ALARM
}


void arm_sensors(){
    long res;
    if (alarm_mask[0]){
        res = measure_usd(USD1);
        distances[0] = res;
    }
    if (alarm_mask[1]){
        res = measure_usd(USD2);
        distances[1] = res;
    }
    if (alarm_mask[2]){
        res = measure_usd(USD3);
        distances[2] = res;
    }
    if (alarm_mask[3]){
        res = measure_usd(USD4);
        distances[3] = res;
    }

    int i;
    for (i = 0; i < 4; i++){
        alarm_state[i] = 0;
    }
}

long measure_usd(uint8_t port, uint16_t pin){
//    int i;
    int index;
    if (pin == GPIO_PIN1) index = 0;
    else if (pin == GPIO_PIN0) index = 1;
    else if (pin == GPIO_PIN6) index = 2;
    else if (pin == GPIO_PIN7) index = 3;

    expecting_USD = 1;

    // FOR POTENTIAL POWER CONSUMPTION ISSUES
    GPIO_setOutputLowOnPin(LED1);
    GPIO_setOutputLowOnPin(LED2);
    GPIO_setOutputLowOnPin(LED3);
    GPIO_setOutputLowOnPin(LED4);

    GPIO_enableInterrupt(port, pin); // enable interrupt for this pin
    start = Timer_A_getCounterValue(TIMER_A0_BASE);

    // SEND TRIG SIGNAL
    GPIO_setOutputHighOnPin(USDT);
    _delay_cycles(10);  // for (i = 0; i < TRIG_CYCLE; i++){}
    GPIO_setOutputLowOnPin(USDT);


    _delay_cycles(50000);
    GPIO_disableInterrupt(port, pin);
    expecting_USD = 0;

    long ans = abs(stop-start);
    sprintf(str_buf, "%d %d", index, ans);
    displayScrollText(str_buf);

    if (abs(ans-distances[index]) > DISTANCE_THRESH){
        alarm_state[index] = 1;
    }

    set_leds();
    return ans;
}

//    PMM_unlockLPM5();
//    _EINT();        // Start interrupt
//    PMM_unlockLPM5();
//    _delay_cycles(10);


#pragma vector = PORT1_VECTOR       // Using PORT1_VECTOR interrupt because P1.4 and P1.5 are in port 1
__interrupt void PORT1_ISR(void)
{
    pressedKey = ' ';
    Key();
//    if (pressedKey == ' '){
//    }

    if (STATE == ALARM && pressedKey != ' '){

        rot_buf[rot_buf_ind] = pressedKey;
        rot_buf_ind = (rot_buf_ind + 1) % ROT_BUF_LEN;

        int i;
        int cnt = 0;
        for(i = 0; i < 4; i++){
            cnt += (rot_buf[i] == password[i]);
        }
        if (cnt == 4){
            STATE = ARM_INPUT;
        }

    }

    if (STATE == ARM_INPUT && pressedKey != ' '){
        if (pressedKey == '1')      alarm_mask[0] ^= 1;
        else if (pressedKey == '2') alarm_mask[1] ^= 1;
        else if (pressedKey == '3') alarm_mask[2] ^= 1;
        else if (pressedKey == '4') alarm_mask[3] ^= 1;
        else if (pressedKey == '0') STATE = SCANNING;


        //this guy has cancer
        if (pressedKey >= '0' && pressedKey <= '4') set_leds();
    } else if (STATE == SCANNING && pressedKey == '0'){
        STATE = ARM_INPUT;
    }


    if (expecting_USD && pressedKey == ' '){
        stop =Timer_A_getCounterValue(TIMER_A0_BASE);
    }

    GPIO_clearInterrupt(COL1);
    GPIO_clearInterrupt(COL2);
    GPIO_clearInterrupt(COL3);
    GPIO_clearInterrupt(USD1);
    GPIO_clearInterrupt(USD2);
    GPIO_clearInterrupt(USD3);
    GPIO_clearInterrupt(USD4);

    GPIO_disableInterrupt(USD1);
    GPIO_disableInterrupt(USD2);
    GPIO_disableInterrupt(USD3);
    GPIO_disableInterrupt(USD4);
}


void activate_alarm()
{
    int i;
    for (i = 0; i < 4; i++){
        rot_buf[i] = '0';
    }
    rot_buf_ind = 0;

    STATE = ALARM;
    int count = 0;

    GPIO_setOutputLowOnPin(LED1);
    GPIO_setOutputLowOnPin(LED2);
    GPIO_setOutputLowOnPin(LED3);
    GPIO_setOutputLowOnPin(LED4);

    //P8.3 as output with module function
    GPIO_setAsPeripheralModuleFunctionOutputPin(PWM, GPIO_PRIMARY_MODULE_FUNCTION);
    //PMM_unlockLPM5();

    //Start timer
    Timer_A_initUpModeParam param = {0};
    param.clockSource = TIMER_A_CLOCKSOURCE_SMCLK;
    param.clockSourceDivider = TIMER_A_CLOCKSOURCE_DIVIDER_1;
    param.timerPeriod = completePeriod;
    param.timerInterruptEnable_TAIE = TIMER_A_TAIE_INTERRUPT_DISABLE;
    param.captureCompareInterruptEnable_CCR0_CCIE =
        TIMER_A_CCIE_CCR0_INTERRUPT_DISABLE;
    param.timerClear = TIMER_A_DO_CLEAR;
    param.startTimer = true;
    Timer_A_initUpMode(TIMER_A1_BASE, &param);

    //Initialize compare mode to generate PWM
    Timer_A_initCompareModeParam initComp2Param = {0};
    initComp2Param.compareRegister = TIMER_A_CAPTURECOMPARE_REGISTER_2;
    initComp2Param.compareInterruptEnable = TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE;
    initComp2Param.compareOutputMode = TIMER_A_OUTPUTMODE_RESET_SET;
    initComp2Param.compareValue = completePeriod / 2;

    Timer_A_initCompareMode(TIMER_A1_BASE, &initComp2Param);
    _delay_cycles(20000);

    while(STATE == ALARM) {
        // read keypad (button for demo) to see if we should break
        if (GPIO_getInputPinValue(GPIO_PORT_P2, GPIO_PIN6) == GPIO_INPUT_PIN_LOW)
            break;

        // KEEP POLLING USDS AND UPDATING ALARM BITS

        // ONLY TOGGLE LEDS THAT ARE ARMED AND ALARMED
        if (count < 100){
            if (alarm_mask[0] && alarm_state[0]) GPIO_setOutputHighOnPin(LED1);
            if (alarm_mask[1] && alarm_state[1]) GPIO_setOutputHighOnPin(LED2);
            if (alarm_mask[2] && alarm_state[2]) GPIO_setOutputHighOnPin(LED3);
            if (alarm_mask[3] && alarm_state[3]) GPIO_setOutputHighOnPin(LED4);
        }
        if (count >= 100){
            if (alarm_mask[0] && alarm_state[0]) GPIO_setOutputLowOnPin(LED1);
            if (alarm_mask[1] && alarm_state[1]) GPIO_setOutputLowOnPin(LED2);
            if (alarm_mask[2] && alarm_state[2]) GPIO_setOutputLowOnPin(LED3);
            if (alarm_mask[3] && alarm_state[3]) GPIO_setOutputLowOnPin(LED4);
        }

        if (count == 200){
            count = 0;
        }

        count++;

        showChar(rot_buf[0], pos1);
        showChar(rot_buf[1], pos2);
        showChar(rot_buf[2], pos3);
        showChar(rot_buf[3], pos4);

    }

    // deactivate PWM
    param.startTimer = false;
    param.timerClear = TIMER_A_DO_CLEAR;
    GPIO_setAsOutputPin(PWM);
    GPIO_setOutputLowOnPin(PWM);
    PMM_unlockLPM5();

    // RESET TO NATURAL STATE
    set_leds();
    for (i = 0; i < 4; i++){
        alarm_state[i] = 0;
    }
    displayScrollText("ACCEPTED");
    STATE = ARM_INPUT;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int abs(int a){
    if (a > 0 ) return a;
    return -a;
}

void set_leds(){
    if (alarm_mask[0]) GPIO_setOutputHighOnPin(LED1);
    else GPIO_setOutputLowOnPin(LED1);

    if (alarm_mask[1]) GPIO_setOutputHighOnPin(LED2);
    else GPIO_setOutputLowOnPin(LED2);

    if (alarm_mask[2]) GPIO_setOutputHighOnPin(LED3);
    else GPIO_setOutputLowOnPin(LED3);

    if (alarm_mask[3]) GPIO_setOutputHighOnPin(LED4);
    else GPIO_setOutputLowOnPin(LED4);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Key()
{
        GPIO_setOutputLowOnPin(ROW1);
        GPIO_setOutputHighOnPin(ROW2);
        GPIO_setOutputHighOnPin(ROW3);
        GPIO_setOutputHighOnPin(ROW4);

        if (GPIO_getInputPinValue(COL1) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[0][0];        // Shows "1"
//            displayScrollText("1");
        }
        if (GPIO_getInputPinValue(COL2) == GPIO_INPUT_PIN_LOW){     // Column 2
            pressedKey = hexaKeys[0][1];       // Shows "2"
//            displayScrollText("2");

        }
        if (GPIO_getInputPinValue(COL3) == GPIO_INPUT_PIN_LOW){     // Column 3
            pressedKey = hexaKeys[0][2];       // Shows "3"
//            displayScrollText("3");
        }

        GPIO_setOutputHighOnPin(ROW1);
        GPIO_setOutputLowOnPin(ROW2);
        GPIO_setOutputHighOnPin(ROW3);
        GPIO_setOutputHighOnPin(ROW4);

        if (GPIO_getInputPinValue(COL1) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[1][0];        // Shows "1"
//            displayScrollText("4");
        }
        if (GPIO_getInputPinValue(COL2) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[1][1];        // Shows "1"
//            displayScrollText("5");
        }
        if (GPIO_getInputPinValue(COL3) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[1][2];        // Shows "1"
//            displayScrollText("6");
        }

        GPIO_setOutputHighOnPin(ROW1);
        GPIO_setOutputHighOnPin(ROW2);
        GPIO_setOutputLowOnPin(ROW3);
        GPIO_setOutputHighOnPin(ROW4);

        if (GPIO_getInputPinValue(COL1) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[2][0];        // Shows "1"
//            displayScrollText("7");
        }
        if (GPIO_getInputPinValue(COL2) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[2][1];        // Shows "1"
//            displayScrollText("8");
        }
        if (GPIO_getInputPinValue(COL3) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[2][2];        // Shows "1"
//            displayScrollText("9");
        }

        GPIO_setOutputHighOnPin(ROW1);
        GPIO_setOutputHighOnPin(ROW2);
        GPIO_setOutputHighOnPin(ROW3);
        GPIO_setOutputLowOnPin(ROW4);

        if (GPIO_getInputPinValue(COL1) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[3][0];        // Shows "1"
//            displayScrollText("A");
        }
        if (GPIO_getInputPinValue(COL2) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[3][1];        // Shows "1"
//            displayScrollText("0");
        }
        if (GPIO_getInputPinValue(COL3) == GPIO_INPUT_PIN_LOW){     // Column 1 to GND
            pressedKey = hexaKeys[3][2];        // Shows "1"
//            displayScrollText("H");
        }

//        init_KEY();
        GPIO_setOutputLowOnPin(ROW1);
        GPIO_setOutputLowOnPin(ROW2);
        GPIO_setOutputLowOnPin(ROW3);
        GPIO_setOutputLowOnPin(ROW4);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void init_GPIO()
{
   // LED INIT
   GPIO_setAsOutputPin(LED1);
   GPIO_setAsOutputPin(LED2);
   GPIO_setAsOutputPin(LED3);
   GPIO_setAsOutputPin(LED4);
   GPIO_setOutputLowOnPin(LED1);
   GPIO_setOutputLowOnPin(LED2);
   GPIO_setOutputLowOnPin(LED3);
   GPIO_setOutputLowOnPin(LED4);
//   PMM_unlockLPM5();

   // BUTTON 2 ON BOARD
   GPIO_setAsInputPinWithPullUpResistor(GPIO_PORT_P2, GPIO_PIN6);
   GPIO_setAsInputPin(GPIO_PORT_P5, GPIO_PIN2);
   // LED 2 ON BOARD
   GPIO_setAsOutputPin(GPIO_PORT_P4, GPIO_PIN0);
   GPIO_setOutputLowOnPin(GPIO_PORT_P4, GPIO_PIN0);
//   PMM_unlockLPM5();
}

void init_ECHO(){
    expecting_USD = 0;
    // USD TRIG INIT
    GPIO_setAsOutputPin(USDT);
    GPIO_setOutputLowOnPin(USDT);
 //   PMM_unlockLPM5();

    // USD ECHO INTERRUPT INIT HIGH->LOW, initially disabled
    GPIO_setAsPeripheralModuleFunctionInputPin(USD1, GPIO_PRIMARY_MODULE_FUNCTION);
    GPIO_selectInterruptEdge(USD1, GPIO_HIGH_TO_LOW_TRANSITION);
    GPIO_setAsInputPinWithPullUpResistor(USD1);
    GPIO_clearInterrupt(USD1);
    GPIO_disableInterrupt(USD1);

    GPIO_setAsPeripheralModuleFunctionInputPin(USD2, GPIO_PRIMARY_MODULE_FUNCTION);
    GPIO_selectInterruptEdge(USD2, GPIO_HIGH_TO_LOW_TRANSITION);
    GPIO_setAsInputPinWithPullUpResistor(USD2);
    GPIO_clearInterrupt(USD2);
    GPIO_disableInterrupt(USD2);

    GPIO_setAsPeripheralModuleFunctionInputPin(USD3, GPIO_PRIMARY_MODULE_FUNCTION);
    GPIO_selectInterruptEdge(USD3, GPIO_HIGH_TO_LOW_TRANSITION);
    GPIO_setAsInputPinWithPullUpResistor(USD3);
    GPIO_clearInterrupt(USD3);
    GPIO_disableInterrupt(USD3);

    GPIO_setAsPeripheralModuleFunctionInputPin(USD4, GPIO_PRIMARY_MODULE_FUNCTION);
    GPIO_selectInterruptEdge(USD4, GPIO_HIGH_TO_LOW_TRANSITION);
    GPIO_setAsInputPinWithPullUpResistor(USD4);
    GPIO_clearInterrupt(USD4);
    GPIO_disableInterrupt(USD4);
}
void init_KEY(){
   // ROW OUTPUT LOW
   GPIO_setAsOutputPin(ROW1);
   GPIO_setAsOutputPin(ROW2);
   GPIO_setAsOutputPin(ROW3);
   GPIO_setAsOutputPin(ROW4);
   GPIO_setOutputLowOnPin(ROW1);
   GPIO_setOutputLowOnPin(ROW2);
   GPIO_setOutputLowOnPin(ROW3);
   GPIO_setOutputLowOnPin(ROW4);
//    PMM_unlockLPM5();

//    // COLUMNS ARE ISR TRIGGERS
   GPIO_setAsPeripheralModuleFunctionInputPin(COL1, GPIO_PRIMARY_MODULE_FUNCTION);     // Column 1: Input direction
   GPIO_selectInterruptEdge(COL1, GPIO_HIGH_TO_LOW_TRANSITION);
   GPIO_setAsInputPinWithPullUpResistor(COL1);
   GPIO_clearInterrupt(COL1);
   GPIO_enableInterrupt(COL1);

   GPIO_setAsPeripheralModuleFunctionInputPin(COL2, GPIO_PRIMARY_MODULE_FUNCTION);     // Column 2: Input direction
   GPIO_selectInterruptEdge(COL2, GPIO_HIGH_TO_LOW_TRANSITION);
   GPIO_setAsInputPinWithPullUpResistor(COL2);
   GPIO_clearInterrupt(COL2);
   GPIO_enableInterrupt(COL2);

   GPIO_setAsPeripheralModuleFunctionInputPin(COL3, GPIO_PRIMARY_MODULE_FUNCTION);     // Column 3: Input direction
   GPIO_selectInterruptEdge(COL3, GPIO_HIGH_TO_LOW_TRANSITION);
   GPIO_setAsInputPinWithPullUpResistor(COL3);
   GPIO_clearInterrupt(COL3);
   GPIO_enableInterrupt(COL3);

}

void init_TIMER(){
    //Start timer
    Timer_A_initContinuousModeParam param = {0};
    param.clockSource = TIMER_A_CLOCKSOURCE_SMCLK;
    param.clockSourceDivider = TIMER_A_CLOCKSOURCE_DIVIDER_1;
    param.timerInterruptEnable_TAIE = TIMER_A_TAIE_INTERRUPT_DISABLE;
    param.timerClear = TIMER_A_DO_CLEAR;
    param.startTimer = true;
    Timer_A_initContinuousMode(TIMER_A0_BASE, &param);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Clock System Initialization
 */
void Init_Clock()
{
    // Intializes the XT1 crystal oscillator
    CS_turnOnXT1(CS_XT1_DRIVE_1);
}

/*
 * Real Time Clock counter Initialization
 */
void Init_RTC()
{
    // Set RTC modulo to 327-1 to trigger interrupt every ~10 ms
    RTC_setModulo(RTC_BASE, 326);
    RTC_enableInterrupt(RTC_BASE, RTC_OVERFLOW_INTERRUPT);
}
