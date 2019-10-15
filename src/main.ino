#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include "credentials.h"

/*MQTT Topics name*/
#define MQTT_TOPIC_COMMAND "esp8266/energy_meter/command"
#define MQTT_TOPIC_CONNECTED "esp8266/energy_meter/connected"
#define MQTT_TOPIC_VALUES "esp8266/energy_meter/values"

#define NTP_TIME_ZONE 7
#define LED_PIN D4 // ESP-12 builtin led

int voltage;
float current, power;
int temperature, humidity, pressure, light;
char thishost[15];

WiFiUDP udpClient;
AsyncMqttClient mqttClient;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;
Ticker mqttPublishTopicTimer;
Ticker ledBlinkTimer;

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    /*Get device hostname*/
    sprintf(thishost, "EMeter-%04X", ESP.getChipId() & 0xFFFF);
    Serial.printf("Device Name: %s\n", thishost);

    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCredentials(MQTT_USERNAME, MQTT_PASSWORD);
#if ASYNC_TCP_SSL_ENABLED
    mqttClient.setSecure(MQTT_SECURE);
    if (MQTT_SECURE) {
        mqttClient.addServerFingerprint((const uint8_t[])MQTT_SERVER_FINGERPRINT);
    }
#endif
    mqttClient.setMaxTopicLength(200);

    connectToWifi();

    /*Wait for wifi connection for the first time device powered*/
    while (WiFi.status() != WL_CONNECTED) { delay(50); }

    /*Setup ntp time sync*/
    udpClient.begin(NTP_SERVER_PORT);
    setSyncProvider(getNtpTime);
    setSyncInterval(300); // Resync ntp every 5 minutes(300 secons)

    /*Hostname defaults OTA hostname and password*/
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    /*Callback function during OTA operation*/
    ArduinoOTA.onStart(OTAonStart);
    ArduinoOTA.onProgress(OTAonProgress);
    ArduinoOTA.onEnd(OTAonEnd);
    ArduinoOTA.onError(OTAonError);
    ArduinoOTA.begin();
}

void loop()
{
    ArduinoOTA.handle();
}

/*Publish values to mqtt broker*/
void mqttPublishTopic(void)
{
    if (timeStatus() == timeSet) displayClock();
    measurePower();
    measureEnvironment();

    String payload((char *)0);
    payload.reserve(160); /*Reserve 160(146 recuired) byte of memory*/

    payload += "esp8266_energy_meter,location=outside,device=";
    payload += (String)thishost;
    payload += " ";
    payload += "voltage=";
    payload += String(voltage);
    payload += ",current=";
    payload += String(current);
    payload += ",power=";
    payload += String(power);
    payload += ",temperature=";
    payload += String(temperature);
    payload += ",humidity=";
    payload += String(humidity);
    payload += ",pressure=";
    payload += String(pressure);
    payload += ",light=";
    payload += String(light);

    Serial.println("------------------------");

    /*Param @topic, @qos, @retain, @message, @message, length*/
    mqttClient.publish(MQTT_TOPIC_VALUES, 0, false, payload.c_str(), payload.length());
    ledBlinkTimer.attach_ms(50, blinkLed, 6);
}

/*Measure sensor data*/
void measurePower(void)
{
    voltage = random(210, 225);
    current = 0.12;
    power = voltage * current;

    Serial.printf("Voltage: %d, Current:%f, Power: %f\n", voltage, current, power);
}

void measureEnvironment(void)
{
    temperature = random(30, 35);
    humidity = random(40, 45);
    pressure = random(11, 17);
    light = random(43, 50);

    Serial.printf("Temperature: %d, Humidity: %d, Pressure: %d, Light: %d\n",
                    temperature, humidity, pressure, light);
}

/*Async functions*/
void connectToWifi(void)
{
    Serial.println("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    ledBlinkTimer.attach_ms(500, blinkLed, 0);
}

void connectToMqtt(void)
{
    Serial.print("Connecting to MQTT");
#if ASYNC_TCP_SSL_ENABLED
    Serial.println(" with SSL");
#else
    Serial.println();
#endif

    mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event)
{
    Serial.printf("  WiFi connected to %s\n", WIFI_SSID);
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  MAC Address: ");
    Serial.println(WiFi.macAddress().c_str());

    /*Disable wifi connect led timer*/
    ledBlinkTimer.detach();
    digitalWrite(LED_PIN, HIGH);

    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event)
{
    Serial.println("Disconnected from WiFi.");
    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, connectToWifi);
    ledBlinkTimer.attach_ms(500, blinkLed, 0);
}

void onMqttConnect(bool sessionPresent)
{
    Serial.println("Connected to MQTT.");
    mqttClient.subscribe(MQTT_TOPIC_COMMAND, 1);
    mqttClient.publish(MQTT_TOPIC_CONNECTED, 0, true, "connected");
    mqttPublishTopicTimer.attach(10, mqttPublishTopic);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Serial.println("Disconnected from MQTT.");

    if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT)
    {
        Serial.println("Bad server fingerprint.");
    }

    if (WiFi.isConnected())
    {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
    /*Stop sending data to server*/
    mqttPublishTopicTimer.detach();
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
    Serial.printf("Packet [%d] Qos [%d] Subscribe acknowledged.\n", packetId, qos);
}

void onMqttUnsubscribe(uint16_t packetId)
{
    Serial.printf("Packet [%d] Unsubscribe acknowledged.\n", packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
                   size_t len, size_t index, size_t total)
{
    char message[len+1];
    strncpy(message, payload, len);
    message[len] = '\0';

    Serial.printf("Message [%d] Topic: %s Payload: %s\n", index, topic, message);

    if ((String)message == "relay")
    {
        Serial.println("relay on");
    }
}

/*Async function executed when publish is acknowledged
 *when using qos 1 or 2
 */
void onMqttPublish(uint16_t packetId)
{
    Serial.printf("Packet [%d] Publish acknowkledged.\n", packetId);
}

void OTAonStart(void)
{
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
    else type = "filesystem";
    Serial.println("Start updating " + type);
}

void OTAonProgress(unsigned int progress, unsigned int total)
{
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
}

void OTAonError(ota_error_t error)
{
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
}

void OTAonEnd(void)
{
    Serial.println("\nEnd");
}

void displayClock(void)
{
    Serial.printf("Time: %d:%d:%d %d.%d.%d\n", hour(), minute(), second(),
                  day(), month(), year());
}

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime(void)
{
    IPAddress ntpServerIP; /*NTP server's ip address*/

    while (udpClient.parsePacket() > 0) ; /*discard any previously received packets*/
    Serial.println("Transmit NTP Request");
    /*get ip address of domain name*/
    WiFi.hostByName(NTP_SERVER_NAME, ntpServerIP);
    Serial.print(NTP_SERVER_NAME);
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
            return secsSince1900 - 2208988800UL + NTP_TIME_ZONE * SECS_PER_HOUR;
        }
    }
    Serial.println("No NTP Response :-(");
    return 0;
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

int blinkCount = 1;
void blinkLed(const int numBlink)
{
    if (digitalRead(LED_PIN) == HIGH) digitalWrite(LED_PIN, LOW);
    else digitalWrite(LED_PIN, HIGH);

    if (numBlink == 0) return;
    blinkCount++;
    if (blinkCount > numBlink)
    {
        blinkCount = 1;
        digitalWrite(LED_PIN, HIGH);
        ledBlinkTimer.detach();
    }
}
