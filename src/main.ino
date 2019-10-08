#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Syslog.h>
#include "credentials.h"

const char* _default_ssid = DEFAULT_SSID;
const char* _default_password = DEFAULT_PASSWORD;
const char* _update_username = DEFAULT_UPDATE_USERNAME;
const char* _update_password = DEFAULT_UPDATE_PASSWORD;
const char* _mqtt_server = MQTT_SERVER;
const char* _ntp_server_name = NTP_SERVER_NAME;

const char* _hostname = DEVICE_HOSTNAME;

/*MQTT Topics name*/
const char* _mqtt_topic_command = "esp8266/energy_meter/command";
const char* _mqtt_topic_connected = "esp8266/energy_meter/connected_client";
const char* _mqtt_topic_values = "esp8266/energy_meter/values";

const int timeZone = 7;
unsigned long previousMillis;

int voltage;
float current, power;
int temperature, humidity, pressure, light;

WiFiClient espClient;
WiFiUDP udpClient;
PubSubClient mqtt(espClient);
Syslog Syslog(udpClient, SYSLOG_PROTO_IETF);

void setup()
{
    Serial.begin(115200);
    WiFi.begin(_default_ssid, _default_password);

    Serial.print("Connecting to WiFi");
    while(WiFi.status() != WL_CONNECTED)
    {
        delay (500);
        Serial.print (".");
    }
    Serial.println(" OK");
    Syslog.logf(LOG_INFO, "WiFi connected to %s", _default_ssid);

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress().c_str());
    Syslog.logf(LOG_INFO, "IP Address: %s MAC: %s",
        WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());

    Serial.print("Starting mDNS responder...");
    if (!MDNS.begin("esp8266"))
    {
        Serial.println(" FAIL");
        Syslog.log(LOG_ERR, "mDNS fail to start");
        /*while (true) { delay(1000); }*/
    }
    Serial.println(" OK");
    Syslog.log(LOG_INFO, "mDNS started");
    MDNS.addService("http", "tcp", 80);

    /*
     *Port defaults to 8266
     */
    ArduinoOTA.setPort(8266);

    /*
     *Hostname defaults OTA hostname and password
     */
    ArduinoOTA.setHostname("myesp8266");
    ArduinoOTA.setPassword("admin");

    /*
     *Callback function during OTA operation
     */
    ArduinoOTA.onStart([]()
    {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
        else type = "filesystem";

        /*
         *NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
         */
        Serial.println("Start updating " + type);
        Syslog.log(LOG_KERN, "Received OTA Update");
    });

    ArduinoOTA.onEnd([]()
    {
        Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();

    udpClient.begin(1337);

    /*timeClient.begin();*/
    setSyncProvider(getNtpTime);
    setSyncInterval(300);

    /*
     *Setup mqtt server and callback
     */
    mqtt.setServer(_mqtt_server, 1883);
    mqtt.setCallback(mqtt_callback);

    /*
     *Setup Syslog server address and port
     */
    Syslog.server(SYSLOG_SERVER, SYSLOG_PORT);
    Syslog.deviceHostname(DEVICE_HOSTNAME);
    Syslog.appName(APP_NAME);
    Syslog.defaultPriority(LOG_KERN);

    /*
     *ESP8266 Builtin LED
     */
    pinMode(D4, OUTPUT);
}

void loop()
{
    ArduinoOTA.handle();

    if (!mqtt.connected())
    {
        mqtt_reconnect();
    }

    mqtt.loop();

    if (millis() > previousMillis + 5000)
    {
        previousMillis = millis();
        digitalClockDisplay();
        mqtt_publish_topic();
        Serial.println("----------------------------");
    }
}

void digitalClockDisplay(void)
{
    /*digital clock display of the time*/
    Serial.print("Time: ");
    Serial.print(hour());
    printDigits(minute());
    printDigits(second());
    Serial.print(" ");
    Serial.print(day());
    Serial.print(".");
    Serial.print(month());
    Serial.print(".");
    Serial.print(year());
    Serial.println();
}

void printDigits(int digits)
{
  /*utility for digital clock display: prints preceding colon and leading 0*/
    Serial.print(":");
    if (digits < 10) Serial.print('0');
    Serial.print(digits);
}

/*
 *Publish values based on mqtt topic
 */
void mqtt_publish_topic(void)
{
    measurePower();
    measureEnvironment();

    String publish_values = "esp8266_energy_meter,location=outside,device=";
           publish_values += String(_hostname);
           publish_values += " ";
           publish_values += "voltage=";
           publish_values += String(voltage);
           publish_values += ",current=";
           publish_values += String(current);
           publish_values += ",power=";
           publish_values += String(power);
           publish_values += ",temperature=";
           publish_values += String(temperature);
           publish_values += ",humidity=";
           publish_values += String(humidity);
           publish_values += ",pressure=";
           publish_values += String(pressure);
           publish_values += ",light=";
           publish_values += String(light);
           publish_values += ",millis=";
           publish_values += String(millis());

    /*esp8266_energy_meter,location=outside,device=ESP8266Client-01 voltage=216,current=0.12,power=25.92,temperature=32,humidity=43,pressure=12,light=52*/

    mqtt.publish(_mqtt_topic_values, publish_values.c_str(), publish_values.length());
    Syslog.log(LOG_INFO, publish_values);
}

/**
   Just create a random measurement.
*/
void measurePower(void)
{
    voltage = random(210, 225);
    current = 0.12;
    power = voltage * current;

    Serial.printf("Voltage: %d, Current:%f, Power: %f\n", voltage, current, power);
    Syslog.logf(LOG_INFO,"Voltage: %d, Current:%f, Power: %f\n", voltage, current, power);
}

void measureEnvironment(void)
{
    temperature = random(30, 35);
    humidity = random(40, 45);
    pressure = random(11, 17);
    light = random(43, 60);

    Serial.printf("Temperature: %d, Humidity: %d, Pressure: %d, Light: %d\n",
                    temperature, humidity, pressure, light);
    Syslog.logf(LOG_INFO, "Temperature: %d, Humidity: %d, Pressure: %d, Light: %d\n",
                    temperature, humidity, pressure, light);
}

/*
 *MQTT callback on messages
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.printf("Message arrived [%s] %s\n", topic, message.c_str());
    Syslog.logf(LOG_INFO, "Message arrived [%s] %s\n", topic, message.c_str());
}

void mqtt_reconnect(void)
{
    /*Loop until we're reconnected*/
    while (!mqtt.connected()) {
        Serial.print("Attempting MQTT connection...");
        Syslog.log("Attempting MQTT connection...");
        if (mqtt.connect(_hostname))
        {
            Serial.println("connected");
            Syslog.log(LOG_INFO, "MQTT connected");
            /*Once connected, publish an announcement...*/
            mqtt.publish(_mqtt_topic_connected, _hostname);
            /*... and resubscribe*/
            mqtt.subscribe(_mqtt_topic_command);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqtt.state());
            Serial.println(" try again in 5 seconds");
            Syslog.logf(LOG_ERR, "MQTT connection failed rc=%d", mqtt.state());
            /*Wait 5 seconds before retrying*/
            delay(5000);
        }
    }
}

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
    IPAddress ntpServerIP; /*NTP server's ip address*/

    while (udpClient.parsePacket() > 0) ; /*discard any previously received packets*/
    Serial.println("Transmit NTP Request");
    /*get ip address of domain name*/
    WiFi.hostByName(_ntp_server_name, ntpServerIP);
    Serial.print(_ntp_server_name);
    Serial.print(": ");
    Serial.println(ntpServerIP);
    sendNTPpacket(ntpServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500)
    {
        int size = udpClient.parsePacket();
        if (size >= NTP_PACKET_SIZE)
        {
            Serial.println("Receive NTP Response");
            udpClient.read(packetBuffer, NTP_PACKET_SIZE);
            unsigned long secsSince1900;
            /*convert four bytes starting at location 40 to a long integer*/
            secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
        }
    }
    Serial.println("No NTP Response :-(");
    return 0; // return 0 if unable to get the time
}

/*send an NTP request to the time server at the given address*/
void sendNTPpacket(IPAddress &address)
{
    /*set all bytes in the buffer to 0*/
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    /*Initialize values needed to form NTP request*/
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udpClient.beginPacket(address, 123); //NTP requests are to port 123
    udpClient.write(packetBuffer, NTP_PACKET_SIZE);
    udpClient.endPacket();
}
