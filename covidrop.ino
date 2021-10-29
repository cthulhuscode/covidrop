#include <LiquidCrystal_I2C.h>
#include <Adafruit_MLX90614.h>
#include <HCSR04.h>
#include <cmath>
#include <driver/dac.h>
#include <WiFi.h>
#include "FirebaseESP32.h"
#include "time.h"
#include <analogWrite.h>
// Display Oled
#include <SPI.h>
#include <Wire.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);

//LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// Configure the time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5;
const int   daylightOffset_sec = -18000;
String dateTime;

// HC-SR04 - Distance sensor 1 - Used as a trigger for the temperature sensor
const int trigPin1 = 19;
const int echoPin1 = 18;
long duration1;
float distance1 = 0.0;

// HC-SR04 - Distance sensor 2 - Used as a trigger for the water pump
const int trigPin2 = 4;
const int echoPin2 = 5;
long duration2;
float distance2 = 0.0;

const int waterPump = 23; //D32 - Pin32 - GPIO32 - Connected to the relay

// MLX90614 - Temperature sensor connected through I2C
float ambientTemp = 0.0;
float objectTemp = 0.0; 
int convAmbTemp = -1;
int convObjTemp = -1;

// Use dual core capacity
TaskHandle_t Task1, Task2, Task3;
SemaphoreHandle_t baton; // Sync tasks 

// Wifi Credentials
#define WIFI_SSID "msi"
#define WIFI_PASSWORD "hola1234"

// Credentials Firebase Project
#define FIREBASE_HOST "https://tesis-covidrop-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "45fOHwsbVkpERyYMKrUjcOXG0NHxYqAKkuLiuYfD"

// Firebase Data Object
FirebaseData firebaseData;
FirebaseJson firebaseJson;

// Root node for specific path
String root = "/temperature";

// Buzzer
int buzzer = 33;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // Init display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Otra direccion es la 0x3D
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  Serial.println("Adafruit MLX90614 test");
  printOledDisplay(1, "Adafruit MLX90614 test", 0,0);

  while (!mlx.begin()) {
    Serial.println("Error connecting to MLX sensor. Check wiring.");
    display.clearDisplay();
    printOledDisplay(1, "Error connecting to MLX sensor. Check wiring.", 0,0);
  };

  Serial.print("Emissivity = "); Serial.println(mlx.readEmissivity());
  Serial.println("================================================");

  //lcd.init();                     
  //lcd.backlight();

  //Conf HCSR04 1
  pinMode(trigPin1, OUTPUT);
  pinMode(echoPin1, INPUT);

  //Conf HCSR04 2
  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);

  // Conf Water Pump
  pinMode(waterPump, OUTPUT);

  // Buzzer
  pinMode(buzzer, OUTPUT);

  /* Configuring WiFi connection */
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  printOledDisplay(1, "Connecting to WiFi", 0,8);

  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    printOledDisplay(1, "...", 0,16);
    delay(300);
  }
  Serial.println("");
  Serial.println("WiFi Connected!");
  printOledDisplay(1, "WiFi Connected!", 0,24);

  /* Firebase connection */
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  /* Create the tasks in each core */
  // Distance sensor 1
  xTaskCreatePinnedToCore(
    &codeForTask1,
    "distSenTask1",
    1000,  
    NULL,
    1,
    &Task1,
    0   
  );
  delay(500); // Needed to start-up task1

  // Distance sensor 2
  xTaskCreatePinnedToCore(
    codeForTask2,
    "distSenTask2",
    1000,
    NULL,
    1,
    &Task2,
    0
  );
  delay(500);

  display.clearDisplay();         // Borra la pantalla
}

void loop() {
  //printDistanceInDisplay(distance1, 0);
  //printDistanceInDisplay(distance2, 1);
  printDistanceInOledDisplay(distance1, 0);
  printDistanceInOledDisplay(distance2, 16);

  ambientTemp = mlx.readAmbientTempC();
  Serial.print("Ambient= "); Serial.print(ambientTemp); Serial.println("°C");   
  Serial.println(); 

  objectTemp = mlx.readObjectTempC();
  
  if(!isnan(ambientTemp)){
   convAmbTemp = floor(ambientTemp);
   //printDisplay(0,0, (String)convAmbTemp);
   //printDisplay(2,0,"C"); 
   printOledDisplay(4, (String)convAmbTemp, 0,0);
  }  
  

  // Distance sensor 1 - Temperature
  if(distance1 >= 14 && distance1 <= 20){       
   Serial.print("Object = "); Serial.print(objectTemp); Serial.println("°C");   
   Serial.println(); 

   if(!isnan(objectTemp)){
    convObjTemp = floor(objectTemp);
    //printDisplay(0,0, (String)convObjTemp);
    //printDisplay(2,0,"C");
    display.clearDisplay();
    String text = (String)convObjTemp + "C";
    printOledDisplay(4, text, 35,5);
    pushValuesToFirebase();
   }   
   analogWrite(buzzer, 150);
   delay(200);
   analogWrite(buzzer, 0);
   
   delay(1500);
  }    

  delay(500);
  //lcd.clear();
  display.clearDisplay();         // Borra la pantalla

}
/*
void printDisplay(int x, int y, String value){
    lcd.setCursor(x,y);
    lcd.print(value);       
}

void printDistanceInDisplay(float distance, int y){
  int d = distance + 0.5;  
  int cm = 12; //Ubicar la palabra cm
    
  if(d > 99) cm += 2;
  else if(d > 9) cm++;
  printDisplay(11,y, (String)d);
  printDisplay(cm,y,"cm"); 
}
*/
void distanceSensor1(){
   // Clears the trigPin1
  digitalWrite(trigPin1, LOW);
  delayMicroseconds(2);

  // Sets the trigPin1 on HIGH state for 10 micro seconds
  digitalWrite(trigPin1, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin1, LOW);
  
  // Reads the echoPin1, returns the sound wave travel time in microseconds
  duration1 = pulseIn(echoPin1, HIGH);
    
  // Calculating the distance1
  distance1= duration1*0.034/2;
}

void codeForTask1( void * parameter ){
  Serial.print("Task1 runs on Core: ");
  Serial.println(xPortGetCoreID());
  
  for (;;) {
    // Clears the trigPin1
    digitalWrite(trigPin1, LOW);
    delayMicroseconds(2);
  
    // Sets the trigPin1 on HIGH state for 10 micro seconds
    digitalWrite(trigPin1, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin1, LOW);
    
    // Reads the echoPin1, returns the sound wave travel time in microseconds
    duration1 = pulseIn(echoPin1, HIGH);
      
    // Calculating the distance1
    distance1= duration1*0.034/2;
    
    // Prints the distance1 on the Serial Monitor
    /*Serial.print("Distance 1: ");
    Serial.print(distance1);
    Serial.println(" cm");  */

    delay(100);
    
    vTaskDelay(10); // needed to not crash the esp
  }
}

void codeForTask2( void * parameter ){
  Serial.print("Task2 runs on Core: ");
  Serial.println(xPortGetCoreID());
  
  for (;;) {
    // Clears the trigPin2
    digitalWrite(trigPin2, LOW);
    delayMicroseconds(2);
  
    // Sets the trigPin2 on HIGH state for 10 micro seconds
    digitalWrite(trigPin2, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin2, LOW);
    
    // Reads the echoPin2, returns the sound wave travel time in microseconds
    duration2 = pulseIn(echoPin2, HIGH);
      
    // Calculating the distance2
    distance2 = duration2*0.034/2;
    
    // Prints the distance2 on the Serial Monitor
    /*Serial.print("Distance 2: ");
    Serial.print(distance2);
    Serial.println(" cm");  
    */

    // Distance sensor 2 - Dispense gel
    if(distance2 <= 20){
     /*
     digitalWrite(waterPump, HIGH);
     delay(1000);
     digitalWrite(waterPump, LOW);    
     */
    analogWrite(waterPump, 150);
    delay(1000);
    analogWrite(waterPump, 0);
    } 

    delay(100);    

    vTaskDelay(10); // needed to not crash the esp
  }
}

void pushValuesToFirebase(){
 dateTime = getDateTime();
 firebaseJson.add("value", objectTemp);  
 firebaseJson.add("createdAt", dateTime);
 
 if(Firebase.pushJSON(firebaseData, "/temperature/", firebaseJson))
   Serial.println(firebaseData.dataPath() + "/"+ firebaseData.pushName());
 else Serial.println(firebaseData.errorReason());
 firebaseJson.clear(); 
}

String getDateTime(){
  String dateAndTime = "";

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "";
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // Hour
  char tiempo[9];
  strftime(tiempo, 9, "%T", &timeinfo);
  Serial.print("tiempo: ");
  Serial.println(tiempo);

  // Date
  char fecha[11];
  strftime(fecha, 11, "%F", &timeinfo);
  Serial.print("fecha: ");
  Serial.println(fecha);

  dateAndTime.concat(fecha);
  dateAndTime.concat(" ");
  dateAndTime.concat(tiempo);
  
  return dateAndTime;
}

void printOledDisplay(int textSize, String text, int x, int y){
    display.setTextSize(textSize);         // Tamaño de la fuente del texto 1 - 2 - 3 - 4 - 5
    display.setCursor(x,y);       // (X,Y) . (Horizontal, Vertical)
    display.print(text);  // texto a mostrar / si es variable sin comillas
    display.display();      
}

void printDistanceInOledDisplay(float distance, int y){
  int d = distance + 0.5;  
  int cm = 80; //Ubicar la palabra cm
    
  if(d > 99) cm += 20;
  else if(d > 9) cm += 10;
  printOledDisplay(2,(String)d, 65,y);
  printOledDisplay(2,"cm",cm,y); 
}
