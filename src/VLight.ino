#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "CTBot.h"
#include "AsyncJson.h"


#define BOT_TOKEN ""  // Токен бота

#define DATA_PIN 4  // 2 ножка
#define AUDIO_PIN 0 // A0

#define BOT_DELAY 500

#define NUM_LEDS 68

#define NUM_STATE 3

#define K_NOICE 2
#define SMOOTH 0.525

char ssid[32];
char pass[64];


#define AP_SSID             "VLight"
#define AP_PASSWORD         "qwertyui"

#define AP_SERVER_IP        192,168,4,1
#define AP_SERVER_GATEWAY   192,168,4,1
#define AP_SERVER_MASK      255,255,255,0

AsyncWebServer server(80);
CTBot myBot;

IPAddress local_ip(AP_SERVER_IP);
IPAddress gateway(AP_SERVER_GATEWAY);
IPAddress netmask(AP_SERVER_MASK);

CRGB leds[NUM_LEDS];

byte hue = 0;
byte sat = 0;
byte bright = 255;
int sens = 150;

int state = 0;

int noise = 0;
int lastVol = 0;

int timing = 0; 

 
int getNoise() {
    FastLED.setBrightness(0);
    FastLED.show();

    int maxNoise = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(0xCE, 0x9F, 0xFC);
        if (maxNoise < analogRead(AUDIO_PIN)) {
            maxNoise = analogRead(AUDIO_PIN);
            delay(4);
        }
    }

    FastLED.setBrightness(bright);
    FastLED.show();

    return maxNoise + K_NOICE;
}

void fillLed(byte _h, byte _s, byte _v)
{
    for (int i = 0; i < NUM_LEDS; i++)
        leds[i] = CHSV(_h, _s, _v);
}

void setLedRainbow(int i, bool _revMode)
{
    float h = millis() / (sat / 4.0 + 12);
    int led = constrain(i, 0, NUM_LEDS - 1);

    if (!_revMode)
        leds[led] = CHSV(((int(led * ((hue * 3.0) / 255.0) + h)) * 4) % 256, 255, 255);
    else
        leds[led] = CHSV(((int((NUM_LEDS - 1 - led) * ((hue * 3.0) / 255.0) + h)) * 4) % 256, 255, 255);
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int loadWifiData() {
    File configFile = LittleFS.open("/config.json", "r");
    
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      configFile.close();

      DynamicJsonDocument jsonBuffer(1024);
      DeserializationError err = deserializeJson(jsonBuffer, String(buf.get()));

      if (!err) {    
          strcpy(ssid, jsonBuffer["ssid"]); 
          strcpy(pass, jsonBuffer["password"]);    

          Serial.printf("Found wifi ssid:%s pass:%s\n", ssid, pass);
          
          return 1;
      } else {
          Serial.printf("ERR Json Deserealization");
      }
    }

    return 0;
}



void saveWifiData(String data) {
    File configFile = LittleFS.open("/config.json", "w");
    configFile.write(data.c_str());
    Serial.println(data);
    configFile.close();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void wifiConnect()
{
    // reset networking
    WiFi.softAPdisconnect(true);
    WiFi.disconnect();
    delay(1000);

    // check for stored credentials
    if (LittleFS.exists("/config.json"))
    {
        if (loadWifiData())
        {
            Serial.printf("Trying to connect: %s pass:%s\n", ssid, pass);
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, pass);

            uint32_t startTime = millis();
            Serial.print("Connecting.");

            fillLed(0, 0, 0);
            FastLED.show();
            int led = 0;
            while (WiFi.status() != WL_CONNECTED)
            {
                delay(500);
                Serial.print(".");

                uint32_t maxTime = 30000;
                uint32_t curTime = (uint32_t)(millis() - startTime);

                if (curTime >= maxTime)
                    break;

                led = int((double)curTime / (double)maxTime * (NUM_LEDS - 1));

                leds[led] = CRGB::Green;
                FastLED.show();
            }
            Serial.print("\n");
        }
        else
        {
            Serial.print("No wifi in Flash\n");
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("AP Started\n");

        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(local_ip, gateway, netmask);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
    }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void setup() {
    // Init leds
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    fillLed(0, 0, 255);
    FastLED.show();

    Serial.begin(9600);

    // Initialize SPIFFS
    if (!LittleFS.begin()) {
        Serial.println("LittleFS in not available");
        return;
    }

    Serial.println("Hello");
    // Connect to Wi-Fi
    wifiConnect();

    // Start server
    server.begin();

    if (WiFi.getMode() == WiFiMode::WIFI_AP) {

        //Serial.println(LittleFS.open("/wifiForm.html", "r").readString());

        server.on("/", [](AsyncWebServerRequest *request) { 
            request->send(LittleFS, "/wifiForm.html", String(), false); 
        });


        server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) { 
            request->send(200, String(), "OK"); }, nullptr, 
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {

            File configFile = LittleFS.open("/config.json", "w");
            configFile.write(data, len);
            configFile.close();
            
            ESP.restart();
        });
    } else {
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { 
        request->send(LittleFS, "/index.html", String(), false); 
    });

    // Route to load style.css file
    server.on("/main.css", HTTP_GET, [](AsyncWebServerRequest *request) { 
        request->send(LittleFS, "/main.css", "text/css"); 
    });

    server.on("/chMode", HTTP_GET, [](AsyncWebServerRequest *request) {
        state = state + 1;

        if (state >= NUM_STATE)
            state = 0;

        switch(state) {
            case 0: request->send_P(200, "text/plain", "STATIC MODE"); break;
            case 1: request->send_P(200, "text/plain", "MUSIC MODE");  break;
            case 2: request->send_P(200, "text/plain", "RAINBOW MODE");  break;
        } 
    });

    server.on("/noise", HTTP_GET, [](AsyncWebServerRequest *request) { 
        noise = getNoise(); 
    });

    server.on("/chVal", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebParameter *p = request->getParam(0);

        if (p->name() == "bright") {
            bright = byte((p->value()).toInt());
            FastLED.setBrightness(bright);
        } else if (p->name() == "sens") {
            sens = (600 - (p->value()).toInt()) * 4;
        } else if (p->name() == "hue") {
            hue = 255 - byte((p->value()).toInt());
        } else if (p->name() == "sat") {
            sat = 255 - byte((p->value()).toInt()); 
        }
    });


    myBot.wifiConnect(ssid, pass);

	// set the telegram bot token
	myBot.setTelegramToken(BOT_TOKEN);

	// check if all things are ok
	if (myBot.testConnection())
		Serial.println("[BOT] testConnection OK");
	else
		Serial.println("[BOT] testConnection NOT OK");

    }
}

int colorIndex = 0;
void loop()
{    
    if (WiFi.getMode() == WiFiMode::WIFI_AP) {
        CRGBPalette16 currentPalette = CloudColors_p;

        
        for (int i = 0; i < NUM_LEDS; i++) {
            leds[i] = ColorFromPalette( currentPalette, colorIndex, 255, LINEARBLEND);
            colorIndex += 1;
        }

        FastLED.show();
        FastLED.delay(1000);
        return;
    }

    // BOT
    if (millis() - timing > BOT_DELAY) {  
        TBMessage msg;
        if (CTBotMessageText == myBot.getNewMessage(msg)) {
            if (msg.text.equalsIgnoreCase("/start")) {                  
                String smsg = "Добро пожаловать в бота VLight!\n"
                    "Создан для предмета \"Проектная деятельность\" в 2022 году.\n"
                    "Чтобы увидеть доступные команды введите - /help\n\n";

                smsg += "Сайт-пульт для управления: ";
                smsg += WiFi.localIP().toString();
                smsg += "\nТекущая сеть: ";
                smsg += WiFi.SSID();

                myBot.sendMessage(msg.sender.id, smsg);    
            } else if (msg.text.equalsIgnoreCase("/help")) {    

                String smsg = "== Команды ==\n"
                    "1. /help  - показывает помощь\n"
                    "2. /reset - сброс настроек\n"
                    "3. /switchMode - сменить режим\n"
                    "4. /setColor <значение 1-255> <контрастность 1-255> <якрость 1-255> - установить цвет\n"
                    "5. /setNoiseValue - установить порог шума\n\n";

                smsg += "Больше настроек можно найти на сайте-пульте: ";
                smsg += WiFi.localIP().toString();

                myBot.sendMessage(msg.sender.id, smsg);   
            } else if (msg.text.equalsIgnoreCase("/reset")) {                                                   
                LittleFS.remove("/config.json");
                myBot.sendMessage(msg.sender.id, "Устройство будет сброшено при следующем включении...");  
            } else if (msg.text.equalsIgnoreCase("/switchMode")) {                                                   
                state = state + 1;

                if (state >= NUM_STATE)
                    state = 0;

                String sState = ""; 
                switch(state) {
                    case 0: sState = "Статичный"; break;
                    case 1: sState = "Цвето музыка"; break;
                    case 2: sState = "Радуга"; break;
                }
                myBot.sendMessage(msg.sender.id, "Установлен режим \"" + sState + "\"");
            } else if (msg.text.equalsIgnoreCase("/setNoiseValue")) {                                                   
                noise = getNoise();   
                myBot.sendMessage(msg.sender.id, "Шум успешно откалиброван!"); 
            } else if (msg.text.equalsIgnoreCase("/switchMode")) {                                                   
                state = state + 1;

                if (state >= NUM_STATE)
                    state = 0;

                String sState = ""; 
                switch(state) {
                    case 0: sState = "Статичный"; break;
                    case 1: sState = "Цвето музыка"; break;
                    case 2: sState = "Радуга"; break;
                }
                myBot.sendMessage(msg.sender.id, "Установлен режим \"" + sState + "\"");
            } else if (msg.text.startsWith("/setColor")) {         
                state = 0;
                             
                int h, s, b;

                int i = 0;
                char *p = strtok ((char*)msg.text.c_str(), " ");
                while( (p = strtok (NULL, " ")) != NULL ) {
                    switch (i) {
                        case 0: h = atoi(p); break;
                        case 1: s = atoi(p); break;
                        case 2: b = atoi(p); break;
                    }

                    if (i == 0 && h == 0) {
                        myBot.sendMessage(msg.sender.id, "Введено некорректное значение цвета!"); 
                        return;
                    } else if (i == 1 && s == 0) {
                        myBot.sendMessage(msg.sender.id, "Введено некорректное значение насыщенности!"); 
                        return;
                    } else if (i == 2 && b == 0) {
                        myBot.sendMessage(msg.sender.id, "Введено некорректное значение яркости!"); 
                        return;
                    }

                    i++;
                    if (i >= 3) break;
                }

                if (i == 1) hue = h;
                if (i == 2) sat = s;
                if (i == 3) {bright = b; FastLED.setBrightness(b);}

                myBot.sendMessage(msg.sender.id, "Цвет установлен!"); 
            }
        }

        timing = millis(); 
    }   


    if (state == 0)
    {
        fillLed(hue, sat, 255);
    }
    else if (state == 1)
    {
        fillLed(197, 74, 10);

        int maxVol = 0;
        for (int i = 0; i < 100; i++)
        {
            if (maxVol < analogRead(AUDIO_PIN))
                maxVol = analogRead(AUDIO_PIN);
        }
        maxVol -= noise;
        maxVol = constrain(maxVol, 0, 600);
        maxVol = map(maxVol, 0, sens + 10, 0, NUM_LEDS / 2);
        maxVol = pow(maxVol, 1.4);
        lastVol = maxVol * SMOOTH + lastVol * (1 - SMOOTH);
        maxVol = constrain(lastVol, 0, NUM_LEDS / 2);
        // Draw
        for (int i = 0; i < maxVol; i++)
        {
            int rLeds = constrain(NUM_LEDS / 2 + i, 0, NUM_LEDS - 1);
            int lLeds = constrain(NUM_LEDS / 2 - i, 0, NUM_LEDS - 1);

            setLedRainbow(rLeds, true);
            setLedRainbow(lLeds, false);
        }
    }
    else if (state == 2)
    {
        for (int i = 0; i < NUM_LEDS; i++)
        {
            setLedRainbow(i, false);
        }
    }

    FastLED.show();
}
