// =====================================================================
// Description
// =====================================================================
// Upload of sensor data (like reedswitches, PIR-Sensors and temperature sensors)
// Simple sensor data poll to be sent via WiFi to a Teamsperak channel where
// a Telegram bot checks for channel updates and triggers messages if there are 
// changes to the Sensor Data. (Like door opens => Alarm)
// Feat: reedswitch for door or window, PIR motion sensor, battery-voltage measure
// MOSFET module has ben added to the Project so we can switch a Load (Camera, Siren...)
// Runs on Wemos D1 Mini Module with Deepsleep to save energy so the Project
// is able to run on Solar Power.
// 
// Version 0.9.3 To Do: 
// Getting Sensor Information                          OK
// Switching the MOS-Module on/off                     OK
// Uploading Sensor Data to Thingspeak                 OK
// User defining minimal Time for the FET              OK
// User defining time for Deepsleep                    OK
// Measuring the voltage of Batteries up to 40 Volt    OK
// Reset FET time if there is any movement/Door open   OK
// 
// =====================================================================
// IO
// =====================================================================
// Wemos D1 Mini
// RST and D0 Are Connected so the ESP Wakes from Deepsleep!
//                DoorContact                ____________
// GND-|-------- / -------------------------|RST__     TX|-
// Battery Voltage                         -|A0   |    RX|- Reedswitch for Door / Window open recognition
//                                         -|D0¯¯¯     D1|-
// MOSFET Module for switching a Load      -|D5        D2|-
// PIR Motion Sensor                       -|D6        D3|-
// DHT22 Temperature and Humidity Sensor   -|D7        D4|-
//                                         -|D8       GND|- Common Ground 
// VCC for DHT22, PIR Sensor, FET          -|3V3       5V|- VCC for the Wemos Module (USB)
//                                           ¯¯¯¯¯USB¯¯¯¯
//
// Voltage Divider onchip needs to be changed!!! Is then able to Meassure up to 48 Volts Max!
//                   ______             _______
// Battery +[A0] ---|R1 10M|-----|-----|R2 220K|--- - Battery GND & GND on Wemos D1 Mini
//                   ¯¯¯¯¯¯      |      ¯¯¯¯¯¯¯
//                               |-----ADC on the ESP8266
// To calculate the Voltage "multiplicator", just set a Voltage of your choice to pin A0 and take a look at what the vread Value is. (Shows up on Serial Monitor)
// take that value and divide it through the set Voltage and you'll get the multiplicator Value.
// For Example: Voltage at Pin A0 = 10Volt, Value of the vread = 231.   So 10 divided by 231 equals 0,04329, which is the multiplicator value.
// To fine tune the correct Voltage Value just increase / decrease the multiplicator a little bit - as i did. => 0.0437
// Take a look in the Documentation for more information.
// Resistors R1 and R2 on the Wemos D1 Mini are soldered and located at:
// https://nerdiy.de/howto-espeasy-wemos-d1-mini-adc-an-eine-andere-maximalspannungen-anpassen/
//
// Rx     o RST Pin
// Cx
// R1     o A0  Pin
// R2     
// Rz     o D0  Pin
//
// ... And so on
// 
// IN: PIR Motion, Reed and DHT22 Sensor: 
//        _______   Motion Sensor      ________  Reedswitch           ___________      
//       | /¯¯\  |- VCC -  3V3        |        |- Out - RX           |           |- GND - GND
//       ||    | |- OUT -  D6         |________|- Out - GND          | DHT22     |- OUT - D7 
//       | \__/  |- GND -  GND                                       |           |- VCC - 3V3
//        ¯¯¯¯¯¯¯                                                     ¯¯¯¯¯¯¯¯¯¯¯
// OUT: Mosfet for switching something on and off for time X
//      _______
//     | |¯¯¯| |- SIG - D5  
//     | |___| |- VCC - 3V3
//     |  |||  |- GND - GND
//      ¯¯¯¯¯¯¯  
// 
// Thingspeak will receive Data as Follows:
// Door/Window        opened: 1         closed: 0
// Motionsensor       motion: 1        nothing: 0
// Fetstate        turned on: 1     turned off: 0
// DHT22         temperature: XX,X°C  humidity: XX,X%
// Battery Voltage: Calculated Voltage at A0 Pin up to 48 Volt after Modding the Voltgage Divider!
// =====================================================================
// Includes
// Included some of the Libraries because of possible version issues
// Credits to ThingSpeak(TM) and Adafruit Industries for their great libraries!
// =====================================================================
#include "ThingSpeak.h"
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <DHT.h>
// =====================================================================
// Variables and Connections
// =====================================================================

char ssid[] = SECRET_SSID;                                              // WiFi name (SSID)       => to find in secrets.h
char pass[] = SECRET_PASS;                                              // WiFI password          => to find in secrets.h
unsigned long myChannelNumber = SECRET_CH_ID;                           // Thingspeak Channel-ID  => to find in secrets.h
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;                       // Thingspeak API-Key     => to find in secrets.h
int keyIndex = 0;                                                       // your network key Index number (needed only for WEP)
WiFiClient  client;

bool fetstate = false;                                                  // Initialize the FET To be low / Inactive / Off
int fettime = 10;                                                       // Time for the FET to stay on = Time in Minutes / 2
int loopcount = 0;                                                      // Set a loop counter to 0
int sleeptime = 45;                                                     // Time for the ESP 8266 to sleep in seconds
float multiplicator = 0.0437;                                           // Multiplicator of the read Value from Voltage divider to calculate Voltage. * See reference in Description
float vread=0.0;                                                        // Read Voltage 
float vbatt=0.0;                                                        // Calculated Voltage
int movement = false;                                                   // Motion Sensor set to false = no movement 
int doorreed = true;                                                    // Door Reedswitch set to true = Door opened 

int BAT = A0;                                                           // Pin A0 for Battery Voltage measuring
int doorreed_pin = 3;                                                   // Pin RX (GPIO 3) for the ReedSensor
int FET = D5;                                                           // Pin D5 for the MOSFET to Switch something on and off
int movement_pin = D6;                                                  // Pin D6 for the Motion Sensor
DHT dht1(D7, DHT22);                                                    // Pin D7 for the DHT22 Sensor


// =====================================================================
// Setup
// =====================================================================

void setup() {
  Serial.begin(74880);                                                   // Initialize Serial connection @ 74880 baud
  delay(10);                                                             // wait
  dht1.begin();                                                          // Initialize the DHT22 Sensor
  ThingSpeak.begin(client);                                              // Initialize ThingSpeak
  pinMode(BAT, INPUT);                                                   // BAT As Input Pin
  pinMode(D7, INPUT);                                                    // D7  As Input Pin
  pinMode(doorreed_pin, INPUT);                                          // doorreed As Input Pin
  pinMode(movement_pin, INPUT);                                          // movement As Input Pin
  pinMode(FET, OUTPUT);                                                  // FET As Output Pin
  Serial.println("");
  Serial.println("Setup Complete");                                      // Show Setup complete on Serial Monitor
  Serial.println("");
}

// =====================================================================
// Main Program
// =====================================================================

void loop() {

//===================================================================// PIR MotionSensor and Reedswitch read
    movement = digitalRead(movement_pin);                            // PIR MotionSensor read
    doorreed = digitalRead(doorreed_pin);                            // Reedswitch read
//===================================================================// Meas Battery Voltage
    vread = analogRead(BAT);                                         // Read the Value of the ADC 
    vbatt = vread * multiplicator;                                   // Calculate the actual Voltage using the Multiplicator
//===================================================================// DHT22 Sensor Read
    float in_h = dht1.readHumidity();                                // Read the Humidity out of the Sensor
    float in_t = dht1.readTemperature();                             // Read the Temperature out of the Sensor
//===================================================================// Switch the FET On and Off for given time  
    if (movement == 1 & fetstate == true || doorreed == 1 & fetstate == true) {                                                           // Resetting the Loopcount if there is anzy movement or the door/window still open. 
      loopcount = 0;                                                 // Reset of the Loopcount 
      Serial.println("Loopcount reset because Movement detected!");  // Information of the Reset in Serial Monitor
    };                                                               // 
                                                                     //
    if (movement == 1 & loopcount < fettime || doorreed == 1 & loopcount < fettime || fetstate == true & loopcount < fettime) {            // Checking if the FET Should be switched on
      loopcount++;                                                   // Counting how many time passed since the fet has been turned on
      fetstate = true;                                               // setting the fet indicator Variable to true
      digitalWrite(FET, HIGH);                                       // Actually turning the FET on.
      Serial.println("Movement or Door triggered fet to turn On");   // Detailed information on the serial Monitor
      Serial.print("We`re at loop no. ");
      Serial.print(loopcount);
      Serial.print(" of ");
      Serial.print(fettime);
      Serial.print("; Fetstate is: ");
      Serial.println(fetstate);
      Serial.println("");
    }
    else {                                                           // If the FET should NOT be on...
      loopcount = 0;                                                 // resetting the loopcounter
      fetstate = false;                                              // setting the fet indicator Variable to false
      digitalWrite(FET, LOW);                                        // Actually turning the FET off.
      Serial.println("No Trigger from Movement/Door.");              // Detailed information on the serial Monitor
      Serial.print("We`re at loop no. ");
      Serial.print(loopcount);
      Serial.print(" of ");
      Serial.print(fettime);
      Serial.print("; Fetstate is: ");
      Serial.println(fetstate);
      Serial.println("");
    } 
//===================================================================// Write the Values to the Serial Monitor
   Serial.println("Data Collected:");
   Serial.print("Door/Window: ");
   Serial.println(doorreed);
   Serial.print("Motion: ");
   Serial.println(movement);
   Serial.print("Voltage Raw Data (vread) ");
   Serial.println(vread);
   Serial.print("Battery: ");
   Serial.println(vbatt);
   Serial.print("Temperature ");
   Serial.print(in_t);
   Serial.print("°C; Humidity in %: ");
   Serial.println(in_h);
   Serial.print("FET State: ");
   Serial.println(fetstate);
   Serial.println("Sending Data to Thingspeak");
   Serial.println("");
//===================================================================// Connect WiFi
   Serial.println("Connecting to: ");                                // 
   Serial.println(SECRET_SSID);                                      // 
   WiFi.begin(ssid, pass);                                           // Connecting ...
    while (WiFi.status() != WL_CONNECTED) 
     {
       delay(500);
       Serial.print(".");                                            // As long as theres no connection show a dot all 500ms
     }
   Serial.println("");
   Serial.println("WiFi connected.");                    
//===================================================================// Sending the Data to Thingspeak
                                                                     // 
   ThingSpeak.setField(1, doorreed);
   ThingSpeak.setField(2, movement);
   ThingSpeak.setField(3, vbatt);
   ThingSpeak.setField(4, in_t);
   ThingSpeak.setField(5, in_h);
   ThingSpeak.setField(6, fetstate);
// ThingSpeak.setField(7, x);
// ThingSpeak.setField(8, z);

  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);    // Sending the Data 
  if(x == 200){
    Serial.println("Channel update successfull.");
  }
  else{
    Serial.println("Were'nt able to send Update. Error: " + String(x));
  }
  
//===================================================================// ESP 8266 Goes to sleep or Pause 
   
   if (fetstate == false){                                           // Only sleep if there's no Movement or Door open.
   Serial.println("ESP8266 goes to deepsleep for a while... Zzz:)");
   ESP.deepSleep(sleeptime * 1000000);                               // Sleeptime defined in Variables section 
   delay(500);
   }
   else {                                                            // If there's movement or door open just pause 30 seconds and read sensor states again. 
    Serial.println("ESP8266 30 Seconds Pause until RE-Checking Sensors");
    delay(30000);                                                    // Waiting for 30 seconds until the next loop and Sensor Data Update.
   }
}
