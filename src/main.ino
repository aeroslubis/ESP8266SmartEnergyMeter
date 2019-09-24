#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>
#include <InfluxDb.h>
#include "credentials.h"

const char* _default_ssid = DEFAULT_SSID;
const char* _default_password = DEFAULT_PASSWORD;
const char* _update_username = DEFAULT_UPDATE_USERNAME;
const char* _update_password = DEFAULT_UPDATE_PASSWORD;
const char* _influxdb_host = INFLUXDB_HOST;
const char* _influxdb_user = INFLUXDB_USER;
const char* _influxdb_pass = INFLUXDB_PASS;

const long utcOffsetInSeconds = 25200;
unsigned long previousMillis;

WiFiUDP ntp_udp;
NTPClient timeClient(ntp_udp, "asia.pool.ntp.org", utcOffsetInSeconds);
Influxdb influx(_influxdb_host);

void setup()
{
    Serial.begin(115200);
    WiFi.begin(_default_ssid, _default_password);

    Serial.print("Connecting to WiFi");
    while(WiFi.status() != WL_CONNECTED)
    {
        delay ( 500 );
        Serial.print ( "." );
    }
    Serial.println(" OK");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress().c_str());
    Serial.print("InfluxDB host: ");
    Serial.println(_influxdb_host);

    Serial.print("Starting mDNS responder...");
    if (!MDNS.begin("esp8266"))
    {
        Serial.println(" FAIL");
        while (true) { delay(1000); }
    }
    Serial.println(" OK");
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

    timeClient.begin();

    influx.setDbAuth("esp8266_powermeter", _influxdb_user, _influxdb_pass);
}

void loop()
{
    ArduinoOTA.handle();

    if (millis() > previousMillis + 5000)
    {
        previousMillis = millis();
        timeClient.update();
        displayTime();
        sendDataToDB();
    }
}

void displayTime()
{
    Serial.println("-------Current Time--------");
    Serial.println(timeClient.getFormattedTime());
    Serial.println(timeClient.getEpochTime());

    Serial.print(timeClient.getDay());
    Serial.print(", ");
    Serial.print(timeClient.getHours());
    Serial.print(":");
    Serial.print(timeClient.getMinutes());
    Serial.print(":");
    Serial.println(timeClient.getSeconds());

    Serial.println("---------------------------");
}

void sendDataToDB(void)
{
    InfluxData data_power = measurePower();
    influx.prepare(data_power);

    InfluxData data_environment = measureEnvironment();
    influx.prepare(data_environment);

    // only with this call all prepared measurements are sent
    influx.write();
}

/**
   Just create a random measurement.
*/
InfluxData measurePower(void)
{
    int voltage = random(210, 225);
    float current = 0.12;
    float power = voltage * current;

    Serial.printf("Voltage: %d, Current:%f, Power: %f", voltage, current, power);

    InfluxData row("data_power");
    row.addTag("location", "outside");
    row.addTag("sensor", "one");
    row.addTag("user", "esp8266");
    row.addValue("voltage", voltage);
    row.addValue("current", current);
    row.addValue("power", power);
    return row;
}

InfluxData measureEnvironment(void)
{
    float temperature = random(30, 35);
    float humidity = random(40, 45);
    float pressure = random(11, 17);
    float light = random(43, 60);

    InfluxData row("data_environment");
    row.addTag("location", "outside");
    row.addTag("sensor", "one");
    row.addTag("user", "esp8266");
    row.addValue("temperature", temperature);
    row.addValue("humidity", humidity);
    row.addValue("pressure", pressure);
    row.addValue("light", light);
    return row;
}
