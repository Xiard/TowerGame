// uses mpflaga fork for MagicQuest: https://github.com/mpflaga/Arduino-IRremote
// see https://docs.arduino.cc/learn/contributions/arduino-creating-library-guide for instructions on using the library
// recommendation is 36kHz IR receiver, not 38kHz: https://groups.google.com/g/high-impedance-air-gap/c/Q0fDHFBO9xI
#include <IRremote.h>
#include <Fsm.h>
#include <SoftPWM.h>
#include <LiquidCrystal.h>

#define SARAH 1208563073
#define EMILY 1207239425

#define PULSE_STEP 35
#define PULSE_TIME 10

#define IR_PIN A1

#define RS_PIN 12
#define ENABLE_PIN 11
#define D4_PIN 2
#define D5_PIN 4
#define D6_PIN 7
#define D7_PIN 8

byte heart[8] = {
  0b00000,
  0b01010,
  0b11111,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000
};

int sarah_pins[3] = {6, 5, 3};
int emily_pins[3] = {9, 10, 13};

// States
#define WAND_NON_OWNER 1
#define WAND_OWNER 2
#define COOLDOWN_EXPIRED 3
#define START_COOLDOWN 4

#define LEVEL_UP_COOLDOWN_DURATION 5000
#define LEVEL_DOWN_COOLDOWN_DURATION 3000

IRrecv irrecv(IR_PIN);
LiquidCrystal sbc_lcd(RS_PIN, ENABLE_PIN, D4_PIN, D5_PIN, D6_PIN, D7_PIN);

int speed[3] = {1000, 600, 300};
uint32_t time_last_score_recorded = 0;

long owner = -1;
int level = 0;
int emily_score = 0;
int sarah_score = 0;


uint32_t timer_started;
int seconds_since_start;
uint32_t cooldown_expires;
uint32_t last_pulse;
int volts = 0;
bool l_up = true;

State unclaimed = State(&do_nothing, &wait_for_wand, NULL);
State level_up = State(&level_up_entry, &level_up_entry_update, NULL);
State level_up_cooldown = State(&level_up_cooldown_entry, &level_up_cooling_down, NULL);
State level_down = State(&level_down_entry, &level_down_entry_update, NULL);
State level_down_cooldown = State(&level_down_cooldown_entry, &level_down_cooling_down, NULL);
State ready_for_wand = State(&wait_for_wand_entry, &wait_for_wand, NULL);

Fsm stateMachine(&unclaimed);

void do_nothing() {
  Serial.println("unclaimed");
}

void wait_for_wand_entry() {
  // I am trying to prevent spurious
  // wand captures; the code seems to cache a signal so that once the cooldown finishes we immediately
  // get another signal.
  irrecv.enableIRIn();
}

void wait_for_wand() {
  long wand = check_wand();
  if (wand == -1)
    return;

  if (owner == -1) {
    owner = wand;
    Serial.print("claimed by ");
    print_owner();
    Serial.println("");
    stateMachine.trigger(WAND_OWNER);
  }
  else if (owner == wand) {
    if (level < 3) {
      stateMachine.trigger(WAND_OWNER);
    }
  }
  else {
    stateMachine.trigger(WAND_NON_OWNER);
  }
}

void initialize_cooldown(int duration) {
    seconds_since_start = 0;
    last_pulse = 0;
    volts = 0;
    fade_up = true;
    timer_started = millis();
    cooldown_expires = timer_started + duration;
}

bool level_up_cooldown_required = false;
bool level_down_cooldown_required = false;

void print_owner() {
    Serial.print(owner == EMILY ? "Black Cat" : "Shadow");
}

void level_up_entry() {
  // we seem to get a couple of level up commands in a row for some reason
  if (level_up_cooldown_required)
    return;
  level += 1;
  if (level <= 3) {
    print_owner();
    Serial.print(" level ");
    Serial.println(level);
  }
  if (level < 3) {
    set_led_for_owner(level-1);
    level_up_cooldown_required = true;
  }
  else {
    level = 3;
    set_led_for_owner(level);
  }
}

void level_up_entry_update() {
  if (level_up_cooldown_required) {
    level_up_cooldown_required = false;
    stateMachine.trigger(START_COOLDOWN);
  } else
    stateMachine.trigger(WAND_OWNER);
}

void log_time() {
  int seconds = round((millis() - timer_started) / 1000);
  if (seconds > seconds_since_start) {
    seconds_since_start = seconds;
    Serial.print('.');
  }
}

void level_up_cooldown_entry() {
  initialize_cooldown(LEVEL_UP_COOLDOWN_DURATION);
}

void pulse_led() {
  int current = millis();
  if ((current - last_pulse) > PULSE_TIME) {
    last_pulse = current;
    if (fade_up) {
      volts += PULSE_STEP;
      if (volts >= 0xff) {
        volts = 0xff;
        fade_up = false;
      }
    } else {
      volts -= PULSE_STEP;
      if (volts <= 0) {
        volts = 0;
        fade_up = true;
      }
    }
    if (owner == EMILY) {
      //SoftPWMSet(emily_pins[level-1], volts);
      analogWrite(emily_pins[level-1], volts);
    }
    else {
      if (false) {
        Serial.print("set pin ");
        Serial.print(sarah_pins[level-1]);
        Serial.print(" to ");
        Serial.println(volts);
      }
      analogWrite(sarah_pins[level-1], volts);
      //SoftPWMSet(sarah_pins[level-1], volts);
    }
  }
}

void level_up_cooling_down() {
  // long wand = check_wand();
  // if (wand != -1) {
  //   if ((owner == EMILY && wand == SARAH) || (owner == SARAH && wand == EMILY)) {
  //     stateMachine.trigger(WAND_NON_OWNER);
  //     return;
  //   }
  // }

  log_time();
  if (millis() >= cooldown_expires) {
    Serial.println("ready!");
    cooldown_expires = 0;
    set_led_for_owner(level);
    stateMachine.trigger(COOLDOWN_EXPIRED);
  } else {
    pulse_led();
  }
}

void level_down_entry() {
  if (cooldown_expires != 0)
    Serial.println(""); // newline for serial monitor because we canceled the timer
  level -= 1;
  if (level == 0) {
    // switch owner
    owner = owner == EMILY ? SARAH : EMILY;
    Serial.print("owner changed to ");
    print_owner();
    Serial.println("");
    clear_leds_for_nonowner();
    stateMachine.trigger(WAND_OWNER);
  } else {
    clear_led_for_owner(level+1);
    Serial.print("level down to ");
    Serial.println(level);
    level_down_cooldown_required = true;
  }
}

void level_down_cooldown_entry() {
  initialize_cooldown(LEVEL_DOWN_COOLDOWN_DURATION);
}


void level_down_entry_update() {
  if (level_down_cooldown_required) {
    level_down_cooldown_required = false;
    stateMachine.trigger(START_COOLDOWN);
  } else
    stateMachine.trigger(WAND_OWNER);
}

void level_down_cooling_down() {
  log_time();
  if (millis() >= cooldown_expires) {
    Serial.println("ready!");
    cooldown_expires = 0;
    set_led_for_owner(level);
    stateMachine.trigger(COOLDOWN_EXPIRED);
  } else {
    pulse_led();
  }
}


decode_results results;

void setup_lcd() {
  // tell the LCD how many columns and rows it has
  sbc_lcd.begin(16, 2);

  sbc_lcd.createChar(0, heart);
  sbc_lcd.setCursor(0, 0);
  sbc_lcd.write(byte(0));

  sbc_lcd.setCursor(0, 1);
  sbc_lcd.write(byte(0));

  sbc_lcd.setCursor(1, 0);
  sbc_lcd.print("Shadow");
  
  sbc_lcd.setCursor(1, 1);
  sbc_lcd.print("Red Cat");
}

void setup() {

  // Let's use comms for debug
  Serial.begin(9600);
  
  while (!Serial) {
    ;
  }

  Serial.println("Comms enabled - beginning sensing");

  // turn on IR receiver
  irrecv.enableIRIn();

  setup_lcd();  

  //SoftPWMBegin();

  for (int x = 0; x < 3; x++) {
    pinMode(sarah_pins[x], OUTPUT);  
    //SoftPWMSet(sarah_pins[x], 0);
    pinMode(emily_pins[x], OUTPUT);  
    //SoftPWMSet(emily_pins[x], 0);
  }

  stateMachine.add_transition(&unclaimed, &level_up, WAND_OWNER, NULL);
  stateMachine.add_transition(&level_up, &level_up_cooldown, START_COOLDOWN, NULL);
  stateMachine.add_transition(&level_up, &ready_for_wand, WAND_OWNER, NULL);
  //stateMachine.add_transition(&level_up_cooldown, &level_down, WAND_NON_OWNER, NULL);
  stateMachine.add_transition(&ready_for_wand, &level_down, WAND_NON_OWNER, NULL);
  stateMachine.add_transition(&ready_for_wand, &level_up, WAND_OWNER, NULL);
  stateMachine.add_transition(&level_up_cooldown, &ready_for_wand, COOLDOWN_EXPIRED, NULL);
  stateMachine.add_transition(&level_down, &level_down_cooldown, START_COOLDOWN, NULL);
  stateMachine.add_transition(&level_down_cooldown, &ready_for_wand, COOLDOWN_EXPIRED, NULL);
  stateMachine.add_transition(&level_down, &level_up, WAND_OWNER, NULL);
}

void setColorValues(int red, int green, int blue) {
  // analogWrite(RED_PIN, red);
  // analogWrite(GREEN_PIN, green);
  // analogWrite(BLUE_PIN, blue);
}

void setColor(int red, int green, int blue) {
  setColorValues(red, green, blue);
  delay(1000);
  setColorValues(0,0,0);
}

void clear_leds_for_nonowner() {
  if (owner == EMILY) {
    for (int x; x < 3; x++) {
      digitalWrite(sarah_pins[x], LOW);
    }
  } else {
    for (int x; x < 3; x++) {
      digitalWrite(emily_pins[x], LOW);
    }
  }
}

void clear_led_for_owner(int level) {
  if (owner == EMILY) {
    digitalWrite(emily_pins[level-1], LOW);
  } else {
    digitalWrite(sarah_pins[level-1], LOW);
  }
}

void set_led_for_owner(int level) {
  if (level < 1)
    return;
  if (false) {
    Serial.print("set_led_for_owner ");
    print_owner();
    Serial.print(" pin index: ");
    Serial.print(level-1);
    Serial.print(", pin: ");
  }
  if (owner == EMILY) {
    digitalWrite(emily_pins[level-1], HIGH);
  } else {
    digitalWrite(sarah_pins[level-1], HIGH);
  }
}

int check_wand_count = 0;
long check_wand() {
  long wand = -1;

  // check_wand_count += 1;
  // if (check_wand_count % 500 == 0) {
  //   Serial.print("check_wand_count: ");
  //   Serial.println(check_wand_count);
  //   if (check_wand_count % 2000 == 0) {
  //     Serial.println("** RESET IR **");
  //     //irrecv.resume();
  //     irrecv.enableIRIn();
  //   }
  // }
  if (irrecv.decode(&results)) {

    if (results.value == SARAH) {
      wand = SARAH;
    } 
    else if (results.value == EMILY) {
      wand = EMILY;
    }
    Serial.print("magnitude ");
    Serial.println(results.magiquestMagnitude);
    irrecv.resume();
    delay(100);
  }
  return wand;
}

void print_time_to_lcd(int seconds, int row) {
  int column = 0;
  if (seconds < 10)
    column = 0;
  else if (seconds < 100)
    column = 1;
  else if (seconds < 1000)
    column = 2;
  else
    column = 3;
  sbc_lcd.setCursor(15 - column, row);
  sbc_lcd.print(seconds);
}

void check_score() {
  if (owner == -1)
    return;

  uint32_t current_time = millis();

  if (current_time - time_last_score_recorded > speed[level-1]) {
    time_last_score_recorded = current_time;
    int points = points + 1;
    int row = 0;
    int score = 0;

    if (owner == SARAH) {
      sarah_score = sarah_score + points;
      score = sarah_score;
    }
    else {
      row = 1;
      emily_score += points;
      score = emily_score;
    }  

    int column = 0;
    if (score < 10)
      column = 0;
    else if (score < 100)
      column = 1;
    else if (score < 1000)
      column = 2;
    else
      column = 3;
    sbc_lcd.setCursor(15 - column, row);
    sbc_lcd.print(score);
  }
}

void loop() {
  stateMachine.run_machine();
  delay(100);
  check_score();
}
