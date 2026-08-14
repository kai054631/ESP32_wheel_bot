#include <Arduino.h>

// put function declarations here:
// int myFunction(int, int);

// void wheelspeedcalA();

// void MotorA();

int PWMA = 4; //motor A pwm pin
int AIN2 = 5; //motor A phase change 2
int AIN1 = 6; //motor A phase change 1

int STBY = 7; //standby pin for tb6612fng, high to run motor

int BIN1 = 15;  //motor B phase change 1
int BIN2 = 16;  //motor B phase change 2
int PWMB = 17;  //motor B pwm pin
int GND = 18;

int enAA = 8;  //motor A phase A
int enAB = 3;  //motor A phase B

int enBA = 46;  //motor B phase A
int enBB = 9;  //motor B phase B

int sda = 11; //gy85 pin 1
int scl = 10; //gy85 pin 


//motor speed 
// float wheel_radius=35;  //mm, 70mm diameter wheel
// int ppr = 11;
// int gear_ratio =  45;
// int interval=100;
// long encoderValue  = 0;
// long previousMillis = 0;
// long currentMillis  = 0;
// int rpm = 0;


void setup()
{
  // put your setup code here, to run once:
  //int result = myFunction(2, 3);
  Serial.begin(115200);

  pinMode(PWMA, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(AIN1, OUTPUT);

  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  pinMode(STBY, OUTPUT);
  pinMode(GND, OUTPUT);

  pinMode(enAA,INPUT);
  pinMode(enAB,INPUT);
  pinMode(enBA,INPUT);
  pinMode(enBB,INPUT);

  // attachInterrupt(digitalPinToInterrupt(enAA),MotorA,RISING);
  // encoderValue  = 0;
  // previousMillis  =  millis();
  // xTaskCreatePinnedToCore(wheelspeedcalA, "motorA_speed_cal", 10000, NULL, 3, NULL, 1);
  
}

void loop()
{
  // put your main code here, to run repeatedly:
  digitalWrite(STBY, HIGH);
  digitalWrite(GND, LOW);
  // motorSpeed(0,50);
  // motorSpeed(1,50);
  // printf("%d,%d,%d\n",digitalRead(enAA),digitalRead(enAB),pulseA);
}

// put function definitions here:
// int myFunction(int x, int y)
// {
//   return x + y;
// }
// void wheelspeedcalA()
// {
//   rpm=encoderValue/(ppr*gear_ratio)/0.1;
//   printf("%d\n",rpm);
//   vTaskDelay(100/portTICK_PERIOD_MS);
// }

// void MotorA(){
//   encoderValue++;
// }