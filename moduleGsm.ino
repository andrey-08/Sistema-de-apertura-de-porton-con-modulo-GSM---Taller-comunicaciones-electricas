#include "Adafruit_FONA.h"
#include "SPIFFS.h"
#include "Preferences.h"

// VARIABLES PARA TRABAJO CON MODULO GSM //
#define SIM800L_RX     27
#define SIM800L_TX     26
#define SIM800L_PWRKEY 4
#define SIM800L_RST    5
#define SIM800L_POWER  23
#define ADMIN  "+50685769481"  // Usuario ADMIN creado como usuario con todos los permisos

// VARIABLES UTILIZADAS PARA EL FUNCIONAMIENTO DEL MOTOR //
#define STEP      33  // El pin dedicado al step
#define DIR       32  // Con este pin se decide para que direccion debe girar el motor.
#define STEP_REV  150 // Estos son la cantidad de pasos por revolucion.

HardwareSerial *sim800lSerial = &Serial1;
Adafruit_FONA sim800l = Adafruit_FONA(SIM800L_PWRKEY);
Preferences preferences;

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

#define LED_BLUE  13

String smsString = "";
String senderString = "";
void setup()
{ 
      
      pinMode(LED_BLUE, OUTPUT);
      pinMode(SIM800L_POWER, OUTPUT);
    
      digitalWrite(LED_BLUE, HIGH);
      digitalWrite(SIM800L_POWER, HIGH);
    
      Serial.begin(115200);
      Serial.println(F("ESP32 with GSM SIM800L"));
      Serial.println(F("Initializing....(May take more than 10 seconds)"));
    
      delay(10000);
    
      // Make it slow so its easy to read!
      sim800lSerial->begin(4800, SERIAL_8N1, SIM800L_TX, SIM800L_RX);
      if (!sim800l.begin(*sim800lSerial)) {
        Serial.println(F("Couldn't find GSM SIM800L"));
        while (1);
      }
      Serial.println(F("GSM SIM800L is OK"));
    
      char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!
      uint8_t imeiLen = sim800l.getIMEI(imei);
      if (imeiLen > 0) {
        Serial.print("SIM card IMEI: "); Serial.println(imei);
      }
    
      // Set up the FONA to send a +CMTI notification
      // when an SMS is received
      sim800lSerial->print("AT+CNMI=2,1\r\n");
    
      Serial.println("GSM SIM800L Ready");
      sim800l.unlockSIM("3895");

      // Initialize library SPIFFS //
      if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
      }

      // MOTOR //
      pinMode(STEP, OUTPUT);
      pinMode(DIR, OUTPUT);
      // Se necesita que si la ESP32 se reinicia o se va la luz
      // verifique si el porton esta abierto o cerrado y siempre que esto suceda lo
      // cierre si el mismo esta abierto. Se realiza en el setup() ya que esto se corre solo una vez cuando se inicia.

      unsigned short stateGate = get_variables();
      if(stateGate == 1){ // si el porton esta abierto entonces cierrelo por seguridad
          close_openGate(2);
          save_variables(0);
      }

//      for(int i = 1; i<41; i++){
//            sim800l.deleteSMS(i);
//      }
}

long prevMillis = 0;
int interval = 1000;
char sim800lNotificationBuffer[64];          //for notifications from the FONA
char smsBuffer[250];
boolean ledState = false;

void loop()
{

      if (millis() - prevMillis > interval) {
        ledState = !ledState;
        digitalWrite(LED_BLUE, ledState);
    
        prevMillis = millis();
      }
    
      char* bufPtr = sim800lNotificationBuffer;    //handy buffer pointer
    
      if (sim800l.available()) {
          int slot = 0; // this will be the slot number of the SMS
          int charCount = 0;
  
          // Read the notification into fonaInBuffer
          do {
            *bufPtr = sim800l.read();
            Serial.write(*bufPtr);
            delay(1);
          } while ((*bufPtr++ != '\n') && (sim800l.available()) && (++charCount < (sizeof(sim800lNotificationBuffer) - 1)));
      
          //Add a terminal NULL to the notification string
          *bufPtr = 0;
      
          //Scan the notification string for an SMS received notification.
          //  If it's an SMS message, we'll get the slot number in 'slot'
          if (1 == sscanf(sim800lNotificationBuffer, "+CMTI: \"SM\",%d", &slot)) {
            Serial.print("slot: "); Serial.println(slot);
      
            char callerIDbuffer[32];  //we'll store the SMS sender number in here
      
            // Retrieve SMS sender address/phone number.
            if (!sim800l.getSMSSender(slot, callerIDbuffer, 31)) {
              Serial.println("Didn't find SMS message in slot!");
            }
            Serial.print(F("FROM: ")); Serial.println(callerIDbuffer);
      
            // converted sender char to string
            senderString = String(callerIDbuffer);

            if(seeUser(senderString)){ // we check User
                // Retrieve SMS value.
                uint16_t smslen;
                
                // Pass in buffer and max len!
                if (sim800l.readSMS(slot, smsBuffer, 250, &smslen)) {
                    smsString = String(smsBuffer);
                    Serial.println(smsString);
                }
  
                // received the msg, we delete it from the buffer
                if (sim800l.deleteSMS(slot)) {
                    Serial.println(F("OK!"));
                }
                else {  
                    Serial.print(F("Couldn't delete SMS in slot ")); Serial.println(slot);
                    sim800l.print(F("AT+CMGD=?\r\n"));
                }
   
                // ACTION OF THE MESSAGE
                
                if (smsString == "Open") {        // IF YOU WANT TO OPEN
                  
                    unsigned short stateGate = get_variables();
                    if(stateGate == 0){
                        // Enviamos msj para notificar que el porton se esta abriendo
                        Serial.println("Opening the gate.");
                        delay(100);
                        // Send SMS for status
                        if (!sim800l.sendSMS(callerIDbuffer, "Opening the gate.")) {
                          Serial.println(F("Failed"));
                        } else {
                          Serial.println(F("Sent!"));
                        }
                        
                        close_openGate(1);
                
                        save_variables(1);    // Ponemos la variable en 1 para indicar que el porton esta abierto
                        Serial.println("Gate is Open!");
                    }
                    else if (stateGate == 1){
                        sim800l.sendSMS(callerIDbuffer, "Gate is already open.");
                    }
                }
                else if (smsString == "Close") {  // IF YOU WANT TO CLOSE
                    
                    unsigned short stateGate = get_variables();
                    if(stateGate == 1){
                        Serial.println("Closing the gate.");
                        delay(100);
                        // Send SMS for status
                        if (!sim800l.sendSMS(callerIDbuffer, "Closing the gate.")) {
                          Serial.println(F("Failed"));
                        } else {
                          Serial.println(F("Sent!"));
                        }
                        close_openGate(2);
                        save_variables(0);    // Ponemos la variable en 0 para indicar que el porton esta cerrado
                        Serial.println("Gate is close!");
                    }
                    else if(stateGate == 0){
                        sim800l.sendSMS(callerIDbuffer, "Gate is already close.");
                    }    
                }

                else if(smsString == "Users"){
                      show_Users(callerIDbuffer);
                }
                
                // Ahora pasamos donde hay mas de 2 comandos dentro del msj
                // se necesita primero identificar los 2 strings y ver que es lo que se quiere hacer
                // eliminar o agregar a un usuario.
                
                else{
                    char add[5] = "Add ";
                    char remov[8] = "Remove ";
                    char plus = '+';
                    String num = "";
                    if(senderString == ADMIN){
                        if(strstr(smsBuffer, add)){ // IF YOU WANT TO ADD
                           num = String(strchr(smsBuffer, plus));
                           Serial.println(num);
                           
                           if(seeUser(num)){
                               sim800l.sendSMS(callerIDbuffer, "User already exists.");
                           }
                           else{
                               addUser(num);
                               sim800l.sendSMS(callerIDbuffer, "User added.");
                           }         
                        } 
                        else if(strstr(smsBuffer, remov)){  // IF YOU WANT TO REMOVE
                           num = String(strchr(smsBuffer, plus));
                           Serial.println(num);
    
                           if(!seeUser(num)){
                                sim800l.sendSMS(callerIDbuffer, "User doesn't exist.");
                           }
                           else{
                                removeUser(num);
                                sim800l.sendSMS(callerIDbuffer, "User deleted."); 
                           }
                        }
                    }
                    else{
                        sim800l.sendSMS(callerIDbuffer, "You don't have permission, only ADMIN");
                    }
                    readFile();
               }
          }
          
          else{
             Serial.print(F("You aren't an user.")); 
                  
             // received the msg, we delete it from the buffer
            if (sim800l.deleteSMS(slot)) {
                Serial.println(F("OK!"));
            }
            else {  
                Serial.print(F("Couldn't delete SMS in slot ")); Serial.println(slot);
                sim800l.print(F("AT+CMGD=?\r\n"));
            }
            sim800l.sendSMS(callerIDbuffer, "You aren't an user.");
          }      
        }
      }
}

bool seeUser(String sender){ // FUNCION ENCARGADA DE BUSCAR USUARIOS
    String aux = "";
    char c;
    File file = SPIFFS.open("/user.txt");
    if(!file){
        Serial.println("Error");  
        return false;
    }  
    while(file.available()){
         aux = "";
         c = file.read();
         while(c != '\n' && c != '\r'){
            aux = aux + c;
            c = file.read();
            if(!file.available()){
                break;
            }
         }
         if(aux == sender){
            Serial.println(F("Usuario autorizado"));
            file.close();
            return true; 
         }
    }
    file.close();
    Serial.println(F("Usuario no autorizado"));
    return false;
}

void addUser(String user){ //FUNCION ENCARGADA DE AGREGAR USUARIO
    File file = SPIFFS.open("/user.txt", FILE_APPEND);
    if(!file){
          Serial.println("The file couldn't be opened");  
          return;
    }
    if(file.println(user)){
        Serial.println("User added");
        Serial.println(user); 
    }
    else{
        Serial.println("User not added"); 
    } 
    file.close(); 
}

void readFile(){ //FUNCION ENCARGADA DE MOSTRAR USUARIOS EN MONITOR SERIAL
    // Read .txt
    String aux = "";
    char c;
    File file = SPIFFS.open("/user.txt");
    if(!file){
        Serial.println("Error");  
        return;
    }  

//    Serial.println(file.available());
    while(file.available()){
         aux = "";
         c = file.read();
         while(c != '\n'){
            aux = aux + c;
            c = file.read();
            if(!file.available()){
                break;
            }
         }
         Serial.println(aux);       
    }        
   file.close();  
}

void show_Users(char* call){
    File file = SPIFFS.open("/user.txt");
    String aux = "";
    char c;
    unsigned int cont = 0;
    while(file.available()){
        c = file.read();
        aux = aux + c;
        cont++;
    }          
   file.close();
   char usuarios[cont];
   aux.toCharArray(usuarios, cont); 
   sim800l.sendSMS(call, usuarios);  
}

void removeUser(String elim){
    File file = SPIFFS.open("/user.txt");
    if(!file){
          Serial.println("The file couldn't be opened");  
          return;
    }
    String info = ""; //Aqui se guardan los datos que se quieren salvar
    char c;
    while(file.available()){
        String aux = "";
        c = file.read();
        while(c != '\n' && c != '\r'){
            aux = aux + c;
            c = file.read();
            if(!file.available()){
                break;
            }
            if(c == '\r'){
                if(aux != elim){
                  aux = aux + '\r' + '\n';
                  info = info + aux;
                }
            }
        }  
    }
    file.close(); 
    SPIFFS.remove("/user.txt");

    file  = SPIFFS.open("/user.txt", FILE_WRITE);
    file.println(info);
    file.close();
}

void close_openGate(int action){
    
     if(action == 2){
         // Set motor direction Clockwise
          digitalWrite(DIR, HIGH); 
     
          for(int i = 0; i<STEP_REV; i++){
              digitalWrite(STEP,HIGH);
              delayMicroseconds(5000);
              digitalWrite(STEP, LOW);
              delayMicroseconds(5000);
          }
          delay(1000); // wait a second
     }

     else if(action == 1){
          // Set motor direction counterClockwise
          digitalWrite(DIR, LOW); 
         
          for(int i = 0; i<STEP_REV; i++){
              digitalWrite(STEP,HIGH);
              delayMicroseconds(5000);
              digitalWrite(STEP, LOW);
              delayMicroseconds(5000);
          } 
          delay(1000);
     } 
 }

void save_variables(unsigned short restart){
    // si es cero esta cerrado y si es 1 esta abierto
    preferences.begin("dataGate", false);       
    preferences.putUShort("state", restart); 
    preferences.end();  
}

unsigned short get_variables(){
    preferences.begin("dataGate", false);       
    unsigned short value = preferences.getUShort("state", 0);  
    preferences.end(); 
    return value;
}
