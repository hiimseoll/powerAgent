//
// Created by hiimseoll on 24. 5. 1.
// SmartPlug Project.
//
#include "main.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <string>
#include <Wire.h>
#include <DFRobot_AHT20.h>
#include <EEPROM.h>
#include <ACS712.h> //acs712 hall effect current sensor
#include "DHT11.h"

DHT11 dht11(5);

#define RELAY 16
#define LED_R 15
#define LED_G 13
#define LED_B 12
#define BTN_EXTERNAL 2
//#define CURRENT_SENSOR A0

//DFRobot_AHT20 TEMP_HUMI_SENSOR; // aht20 temperature and humidity sensor

ACS712 CURRENT_SENSOR(ACS712_05B, A0);

const char* ap_ssid = "001_SMARTPLUG";//
const char* ap_password = "12345678";
const char* mqtt_server = "plug.hiimseoll.kr";
//const String clientId = "SmartPlugClient-2"; //mqtt clientId

String target_ssid;
String target_password;
String device_id;

String topic_from_client = "msgFromClient_";
String topic_from_server = "msgFromServer_";

ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE 50
char msg[MSG_BUFFER_SIZE];
long value = 0;

unsigned int timer_one;
//unsigned int timer_two;
unsigned int time_pressed;
unsigned int lastReconnectTry = 20000;
unsigned int sensor_publish_timer = 0;
unsigned int limit_detect_timer = 0;

int current_sensor_zero;
float current_sensor_offset;

bool g_relay_flag = false;
bool post_flag = false;
bool limit_mode = false; // for temp, humi limit
int temp_limit = 100, humi_limit = 100;


void handle_root();
void handle_post();
void setup_wifi(int);
void callback(char*, byte*, unsigned int);
void reconnect();
void send_msg(char*, byte*);
void detect_button();
void publish_master();
String get_temperature_humidity(int, int, int);
String get_current();
void reset_all(); // reset plug
void toggle_relay();
void set_relay(int, int);
void init_led(); // RGB LED TO ZERO

void setup(){
    Serial.begin(115200);
    EEPROM.begin(512);
    pinMode(RELAY, OUTPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    pinMode(BTN_EXTERNAL, INPUT_PULLUP);

    delay(1000);

    //TEMP_HUMI_SENSOR.begin(); // AHT20 sensor begin
    //Wire.begin();

    Serial.printf("eeprom: %d\n\r", EEPROM.read(0));
    setup_wifi(((int)EEPROM.read(0) == 1)  ? 1 : 0); // check ssid, password, device_id already exists

    client.setServer(mqtt_server, 50027); // smartplug.hiimseoll.kr:50026
    client.setCallback(callback);
    CURRENT_SENSOR.calibrate(); // calibrate acs712 current sensor
}

void loop(){
    if(!client.connected() && millis() - lastReconnectTry > 5000) reconnect();
    if(millis() - sensor_publish_timer > 13000){
        sensor_publish_timer = millis();

        publish_master();
    }
    if(limit_mode) {
        if(millis() - limit_detect_timer > 1500) {
            limit_detect_timer = millis();
            String a = get_temperature_humidity(1, temp_limit, humi_limit);
        }
    }

    client.loop();
    detect_button();


//    unsigned long now = millis(); // Publish every 10 seconds...
//    if(now - lastMsg > 10000){
//        //send_msg(byte*);
//    }
}

void handle_root(){
    String html = "<html><body>";
    html += "<form action=\"/connect\" method=\"POST\">";
    html += "SSID: <input type=\"text\" name=\"ssid\"><br>";
    html += "Password: <input type=\"password\" name=\"password\"><br>";
    html += "<input type=\"submit\" value=\"Submit\">";
    html += "</form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}
void handle_post(){
    if(server.hasArg("ssid") && server.hasArg("password") && server.hasArg("device_id")){
        target_ssid = server.arg("ssid");
        target_password = server.arg("password");
        device_id = server.arg("device_id");
        server.send(200, "application/json", "{\"status\":\"success\"}");

        Serial.println("\n\rssid: " + target_ssid);
        Serial.println("password: " + target_password);
        Serial.println("device id: " + device_id);

        post_flag = true;
    }
    else{
        server.send(400, "application/json", "{\"status\":\"fail\"}");
        post_flag = false;
    }
}

void setup_wifi(int flag){
    if(flag == 0){
        WiFi.softAP(ap_ssid, ap_password);

        server.on("/", HTTP_GET, handle_root);
        server.on("/connect", HTTP_POST, handle_post);
        server.begin();

        Serial.println("");
        Serial.print("Access Point \"");
        Serial.print(ap_ssid);
        Serial.println("\" started");
        Serial.print("IP address: ");
        Serial.println(WiFi.softAPIP());
        Serial.println("Waiting for ssid and password.");

        server.handleClient();
        while(!post_flag){
            Serial.print(".");
            detect_button();
            server.handleClient();
            delay(500);
        }

        int i, n;
        EEPROM.write(0, 1); // means SSID, PASSWORD, DEVICE_ID exists
        EEPROM.write(1, target_ssid.length()); // logging ssid and password length
        EEPROM.write(2, target_password.length());
        EEPROM.write(3, device_id.length());
        for(i = 3; i < target_ssid.length() + 3; i++){
            EEPROM.write(i, target_ssid[i - 3]);
        }
        for(n = 0; n < target_password.length() + 3; n++){
            EEPROM.write(n + i, target_password[n]);
        }
        for(int t = 0; t < device_id.length() + 3; t++){
            EEPROM.write(t + n + i, device_id[t]);
        }
        EEPROM.commit();
    }
    else{
        int i, n, saved_ssid_length = 0, saved_password_length = 0, saved_deviceid_length = 0;
        saved_ssid_length = (int)EEPROM.read(1);
        saved_password_length = (int)EEPROM.read(2);
        saved_deviceid_length = (int)EEPROM.read(3);
        for(i = 3; i < saved_ssid_length + 3; i++){
            target_ssid += (char)EEPROM.read(i);
        }
        for(n = 0; n < saved_password_length + 3; n++){
            target_password += (char)EEPROM.read(n + i);
        }
        for(int t = 0; t < saved_deviceid_length + 3; t++){
            device_id += (char)EEPROM.read(t + n + i);
        }
        EEPROM.commit();
    }

    delay(10);
    Serial.printf("\n\rConnecting to %s\n\r", target_ssid.c_str());

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(target_ssid, target_password);

    while(WiFi.status() != WL_CONNECTED){
        delay(500);
        Serial.print(".");
        detect_button();
    }

    randomSeed(micros());

    Serial.printf("\n\rWiFi connected\n\rIP addr: ");
    Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length){
    Serial.printf("Message arrived [%s]\n\r:", topic);
    for(int i = 0; i < length; i++) Serial.print((char)payload[i]);
    Serial.println();

    payload[length] = '\0';

    if(strcmp(topic, topic_from_server.c_str()) == 0){
        Serial.printf("%s\n\r", payload);

        if(payload[0] == '0' || payload[0] == '1'){             // need to modify after discussion
            set_relay((int)payload[0] - 48, 1);
        }
        else if(payload[0] == 'p' && payload[1] == 'c') {
            if(payload[2] == '1') {
                char temp_str[3], humi_str[3];
                limit_mode = true;
                memcpy(temp_str, payload + 4, 2);
                temp_str[2] = '\0'; // null-terminator

                memcpy(humi_str, payload + 7, 2);
                humi_str[2] = '\0'; // null-terminator

                temp_limit = atoi(temp_str);
                humi_limit = atoi(humi_str);
            }
            else {
                limit_mode = false;
            }
        }
    }
    else{
        Serial.println("no way this happened");
    }

}

void reconnect(){
    while(!client.connected()){

        topic_from_client += device_id; // change mqtt topics for specific device
        topic_from_server += device_id;

        Serial.print("Attempting MQTT connection...");
        // Create a random client ID -> DUNNO WHY

        //clientId + String(random(0xffff), HEX);

        //Attempting to connect;
        if(client.connect(device_id.c_str())){
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish(topic_from_client.c_str(), "1Hello world!"); // payload format 0: sensor value 1: specific message
            // ... and resubscribe
            client.subscribe(topic_from_server.c_str());
        }
        else{
            Serial.printf("failed, state(rc)= %d\r\n", client.state());
            Serial.println("Try again in 10 seconds...");
            lastReconnectTry = millis();
            break;
        }
    }
}

void send_msg(char* pub_topic, byte* payload){
    snprintf(msg, MSG_BUFFER_SIZE, "%s", (char*)payload);
    Serial.printf("Publish message: %s\n\r", msg);
    client.publish(pub_topic, msg);
    memset(msg, '\0', MSG_BUFFER_SIZE);
}

void detect_button(){
    while(true){
        if(!digitalRead(BTN_EXTERNAL) && !timer_one) {  // dunno why signal flipped
            timer_one = millis();
        }
        else if((!digitalRead(BTN_EXTERNAL) && timer_one)){
            time_pressed = millis() - timer_one;
        }
        else break;

        delay(5);
    }

    timer_one = 0;

    if(time_pressed){
        Serial.println(time_pressed);
        if(time_pressed > 5000){
            reset_all();
        }
        else if(time_pressed > 100){
            toggle_relay();
        }
    }

    time_pressed = 0;
}

void publish_master(){
    String data = "0";  // payload format 0: sensor data 1: specific message
    data += get_temperature_humidity(0, 0, 0);
    data += get_current();

    snprintf(msg, MSG_BUFFER_SIZE, "%s", data.c_str());
    Serial.printf("Publish message: %s\n\r", msg);
    client.publish(topic_from_client.c_str(), msg);
    memset(msg, '\0', MSG_BUFFER_SIZE);
}
String get_temperature_humidity(int mode, int t_limit, int h_limit){ // mode 1: limit detect mode | mode 0: publish mode
    //
    // if(mode == 1) {
    //     if((int)TEMP_HUMI_SENSOR.getTemperature_C() > t_limit && t_limit != 0) {
    //         set_relay(0, 0);
    //         snprintf(msg, MSG_BUFFER_SIZE, "%st", "1pc");
    //     }
    //     else if((int)TEMP_HUMI_SENSOR.getHumidity_RH() > h_limit && h_limit != 0) {
    //         set_relay(0, 0);
    //         snprintf(msg, MSG_BUFFER_SIZE, "%sh", "1pc");
    //     }
    //     else return "";
    //
    //     Serial.printf("Publish message: %s\n\r", msg);
    //     client.publish(topic_from_client.c_str(), msg);
    //     memset(msg, '\0', MSG_BUFFER_SIZE);
    //     return "";
    // }
    //
    // String data;
    //
    // if(TEMP_HUMI_SENSOR.startMeasurementReady(true)){
    //
    //     data += (int)TEMP_HUMI_SENSOR.getTemperature_C();
    //     data += (int)TEMP_HUMI_SENSOR.getHumidity_RH();
    // }
    // else {
    //     Serial.println("aht20 failure.");
    //     data += "0000"; // 0000 for sensor error
    // }
    // return data;


    float temp, humi;

    temp  = dht11.readTemperature();
    humi = dht11.readHumidity();

    if(temp == 253 || temp == 254 || humi == 253 || humi == 254) {
        return (String)0000;
    }

    if(mode == 1) {
        if((int)temp > t_limit && t_limit != 0) {
            set_relay(0, 0);
            snprintf(msg, MSG_BUFFER_SIZE, "%st", "1pc");
        }
        else if((int)humi > h_limit && h_limit != 0) {
            set_relay(0, 0);
            snprintf(msg, MSG_BUFFER_SIZE, "%sh", "1pc");
        }
        else return "";

        Serial.printf("Publish message: %s\n\r", msg);
        client.publish(topic_from_client.c_str(), msg);
        memset(msg, '\0', MSG_BUFFER_SIZE);
        return "";
    }

    String data;


    data += ((int)temp);
    data += ((int)humi);

    //Serial.println(readTempe)

    return data;
}

String get_current(){

    float amps = CURRENT_SENSOR.getCurrentAC();
    if(amps < 0.035){ // handle default value
        amps = 0;
    }
    else{
        amps -= 0.033; // minus amps offset
    }

    Serial.printf("amphere: %.1f(A), wattage: %.1f\n\r\n\r", amps, amps * 1137.93);

    return (String)0.00;
    //return (String)amps;
    //amps * 1137.93 = wattage
//    snprintf(msg, MSG_BUFFER_SIZE, "%.2f", amps);
//    Serial.printf("Publish message: %s\nif sensor \r", msg);
//    client.publish("data_current_sensor", msg);
//    memset(msg, '\0', MSG_BUFFER_SIZE);
}

void reset_all(){
    Serial.print("Hard resetting plug...");

    if(g_relay_flag){
        g_relay_flag = !g_relay_flag;
        digitalWrite(RELAY, g_relay_flag);
    }

    init_led();
    for(int i = 0; i < 10; i++) {
        analogWrite(LED_R, 255);
        delay(100);
        analogWrite(LED_R, 0);
        delay(100);
    }
    Serial.printf("done!!!\n\r");

    EEPROM.write(0, 0); // remove ssid, password flag from eeprom
    EEPROM.commit();
    post_flag = false;
    setup_wifi(0);
}

void toggle_relay(){
    init_led();
    g_relay_flag = !g_relay_flag;
    analogWrite(LED_G, g_relay_flag ? 255 : 0);
    digitalWrite(RELAY, g_relay_flag);
    snprintf(msg, MSG_BUFFER_SIZE, "1relay%d", g_relay_flag ? 1 : 0);
    Serial.printf("Publish message: %s\n\r", msg);
    client.publish(topic_from_client.c_str(), msg);

    memset(msg, '\0', MSG_BUFFER_SIZE);
    //Serial.printf("%s", msg);
}

void set_relay(int flag, int is_from_app){
    Serial.println(flag);
    init_led();
    g_relay_flag = (flag != 0); // ascii 48 == integer 0
    analogWrite(LED_G, g_relay_flag ? 255 : 0);
    digitalWrite(RELAY, g_relay_flag);

    if(is_from_app == 1) return;

    snprintf(msg, MSG_BUFFER_SIZE, "1relay%d", g_relay_flag ? 1 : 0);
    Serial.printf("Publish message: %s\n\r", msg);
    client.publish(topic_from_client.c_str(), msg);
    memset(msg, '\0', MSG_BUFFER_SIZE);
}

void init_led(){
    analogWrite(LED_R, 0);
    analogWrite(LED_G, 0);
    analogWrite(LED_B, 0);
}