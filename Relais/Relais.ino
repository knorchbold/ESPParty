/******************************************************************************
 * Firmware to control four solid state relays and corresponding status LEDs
 * from an ESP8266 or ESP32 via E1.31 DMX over Ethernet
 ******************************************************************************/

#include <ESPAsyncE131.h>
#include <IotWebConf.h>

#define BOOL2STRING(x) (String(x ? "true" : "false"))
#define INT2BOOL(x) (x == 1 ? true : false)

#define UNIVERSE_COUNT 1 // determines buffer size for E1.31

// Initial AP config, can be changed in web interface
#define NAME "Relais"
#define PASSWORD "supersecret"

// Status LED pin for IotWebConf
#define STATUS_PIN 25

// Status LED pins for the ports
#define STATUS_P1 12
#define STATUS_P2 14
#define STATUS_P3 27
#define STATUS_P4 26

#define STRING_LEN 128
#define NUMBER_LEN 32

// Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "v1"

// Configuration parameters (set via web interface)
int g_dmx_universe = 1;
int g_dmx_channel_offset = 0;
bool g_usee131 = false;
bool g_port1 = false;
bool g_port2 = false;
bool g_port3 = false;
bool g_port4 = false;

// DMX values go here
uint8_t g_channel1 = 0;
uint8_t g_channel2 = 0;
uint8_t g_channel3 = 0;
uint8_t g_channel4 = 0;

unsigned long g_last_dmx_received = 0;
unsigned long g_last_stat_print = 0;
unsigned long g_cur_time = 0;

unsigned int g_received = 0;

// Solid state relais are attached to these pins
const int g_ch1_pin = 5;
const int g_ch2_pin = 17;
const int g_ch3_pin = 16;
const int g_ch4_pin = 4;

ESPAsyncE131 g_e131(UNIVERSE_COUNT);

// Callback method declarations.
void wifi_connected_callback();
void config_saved_callback();
boolean form_validator_callback();

DNSServer dns_server;
WebServer web_server(80);
HTTPUpdateServer update_server;

char g_value_usee131[NUMBER_LEN];
char g_value_dmx_universe[NUMBER_LEN];
char g_value_dmx_channel_offset[NUMBER_LEN];
char g_value_port1[NUMBER_LEN];
char g_value_port2[NUMBER_LEN];
char g_value_port3[NUMBER_LEN];
char g_value_port4[NUMBER_LEN];

IotWebConf iotWebConf(NAME, &dns_server, &web_server, PASSWORD, CONFIG_VERSION);
IotWebConfSeparator separator = IotWebConfSeparator();
IotWebConfSeparator separator1 = IotWebConfSeparator("E1.31 DMX settings");
IotWebConfParameter g_cfg_usee131 = IotWebConfParameter("Listen for E1.31 DMX data", "usee131", g_value_usee131, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");
IotWebConfParameter g_cfg_dmx_universe = IotWebConfParameter("E1.31 DMX universe", "dmx_universe", g_value_dmx_universe, NUMBER_LEN, "number", "1..512", NULL, "min='1' max='512' step='1'");
IotWebConfParameter g_cfg_dmx_channel_offset = IotWebConfParameter("E1.31 DMX channel offset", "dmx_channel", g_value_dmx_channel_offset, NUMBER_LEN, "number", "1..512", NULL, "min='1' max='512' step='1'");
// -- We can add a legend to the separator
IotWebConfSeparator separator2 = IotWebConfSeparator("Port power settings (ignored when E1.31 is enabled)");
IotWebConfParameter g_cfg_port1 = IotWebConfParameter("Enable power on port 1", "port1", g_value_port1, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");
IotWebConfParameter g_cfg_port2 = IotWebConfParameter("Enable power on port 1", "port2", g_value_port2, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");
IotWebConfParameter g_cfg_port3 = IotWebConfParameter("Enable power on port 1", "port3", g_value_port3, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");
IotWebConfParameter g_cfg_port4 = IotWebConfParameter("Enable power on port 1", "port4", g_value_port4, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");

void translate_config()
{
    g_usee131 = INT2BOOL(atoi(g_value_usee131));
    g_dmx_universe = atoi(g_value_dmx_universe);
    g_dmx_channel_offset = atoi(g_value_dmx_channel_offset);

    g_port1 = INT2BOOL(atoi(g_value_port1));
    g_port2 = INT2BOOL(atoi(g_value_port2));
    g_port3 = INT2BOOL(atoi(g_value_port3));
    g_port4 = INT2BOOL(atoi(g_value_port4));

    Serial.println("Configuration values: ");
    Serial.println("DMX universe: "       + String(g_dmx_universe));
    Serial.println("DMX channel offset: " + String(g_dmx_channel_offset));
    Serial.println("Use E1.31: " + BOOL2STRING(g_usee131));
    Serial.println("Enable Port 1: " + BOOL2STRING(g_port1));
    Serial.println("Enable Port 2: " + BOOL2STRING(g_port2));
    Serial.println("Enable Port 3: " + BOOL2STRING(g_port3));
    Serial.println("Enable Port 4: " + BOOL2STRING(g_port4));
}

void set_outputs()
{

    if (g_channel1 > 127)
    {
        digitalWrite(g_ch1_pin, HIGH);
        digitalWrite(STATUS_P1, HIGH);
    } else {
        digitalWrite(g_ch1_pin, LOW);
        digitalWrite(STATUS_P1, LOW);
    }

    if (g_channel2 > 127)
    {
        digitalWrite(g_ch2_pin, HIGH);
        digitalWrite(STATUS_P2, HIGH);
    } else {
        digitalWrite(g_ch2_pin, LOW);
        digitalWrite(STATUS_P2, LOW);
    }

    if (g_channel3 > 127)
    {
        digitalWrite(g_ch3_pin, HIGH);
        digitalWrite(STATUS_P3, HIGH);
    } else {
        digitalWrite(g_ch3_pin, LOW);
        digitalWrite(STATUS_P3, LOW);
    }

    if (g_channel4 > 127)
    {
        digitalWrite(g_ch4_pin, HIGH);
        digitalWrite(STATUS_P4, HIGH);
    } else {
        digitalWrite(g_ch4_pin, LOW);
        digitalWrite(STATUS_P4, LOW);
    }
}

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
    boolean tail = false;

    Serial.print("DMX: Univ: ");
    Serial.print(universe, DEC);
    Serial.print(", Seq: ");
    Serial.print(sequence, DEC);
    Serial.print(", Data (");
    Serial.print(length, DEC);
    Serial.print("): ");

    if (length > 16) {
        length = 16;
        tail = true;
    }
    // send out the buffer
    /* for (int i = 0; i < length; i++) */
    /* { */
        /* Serial.print(data[i], HEX); */
        /* Serial.print(" "); */
    /* } */
    /* if (tail) { */
        /* Serial.print("..."); */
    /* } */

    int offset = g_dmx_channel_offset;
    g_channel1 = data[offset + 0];
    g_channel2 = data[offset + 1];
    g_channel3 = data[offset + 2];
    g_channel4 = data[offset + 3];

    Serial.println();


}

void setupE131()
{
    if (g_usee131)
    {
        if (g_e131.begin(E131_MULTICAST, g_dmx_universe, UNIVERSE_COUNT))   // Listen via Multicast
        {
            Serial.println("DMX: Listening for E.131 DMX data in Universe " + String(g_dmx_universe));
        }
        else
        {
            Serial.println("DMX: [ERROR] g_e131.begin failed ");
        }
        g_last_dmx_received = millis();
        g_last_stat_print = g_last_dmx_received;
    } else {
        Serial.println("DMX: E1.31 not enabled ");
    }
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.println("setup");

#ifdef ESP32
    WiFi.setSleep(false);
    btStop();
#endif

    // Pins to the solid state relay
    pinMode(g_ch1_pin, OUTPUT);
    pinMode(g_ch2_pin, OUTPUT);
    pinMode(g_ch3_pin, OUTPUT);
    pinMode(g_ch4_pin, OUTPUT);
    digitalWrite(g_ch1_pin, LOW);
    digitalWrite(g_ch2_pin, LOW);
    digitalWrite(g_ch3_pin, LOW);
    digitalWrite(g_ch4_pin, LOW);

    // Pins to the status LEDs
    pinMode(STATUS_P1, OUTPUT);
    pinMode(STATUS_P2, OUTPUT);
    pinMode(STATUS_P3, OUTPUT);
    pinMode(STATUS_P4, OUTPUT);
    digitalWrite(STATUS_P1, LOW);
    digitalWrite(STATUS_P2, LOW);
    digitalWrite(STATUS_P3, LOW);
    digitalWrite(STATUS_P4, LOW);

    iotWebConf.setStatusPin(STATUS_PIN);
    /* iotWebConf.setConfigPin(CONFIG_PIN); */
    iotWebConf.setupUpdateServer(&update_server);
    iotWebConf.addParameter(&separator1);
    iotWebConf.addParameter(&g_cfg_usee131);
    iotWebConf.addParameter(&g_cfg_dmx_universe);
    iotWebConf.addParameter(&g_cfg_dmx_channel_offset);
    iotWebConf.addParameter(&separator2);
    iotWebConf.addParameter(&g_cfg_port1);
    iotWebConf.addParameter(&g_cfg_port2);
    iotWebConf.addParameter(&g_cfg_port3);
    iotWebConf.addParameter(&g_cfg_port4);

    iotWebConf.setConfigSavedCallback(&config_saved_callback);
    iotWebConf.setWifiConnectionCallback(&wifi_connected_callback);
    iotWebConf.setFormValidator(&form_validator_callback);
    iotWebConf.getApTimeoutParameter()->visible = true;

    // Initialize the configuration.
    iotWebConf.init();

    translate_config();

    // Set up required URL handlers on the web server.
    web_server.on("/", handleRoot);
    web_server.on("/config", []{ iotWebConf.handleConfig(); });
    web_server.onNotFound([](){ iotWebConf.handleNotFound(); });

    Serial.println(NAME " ready to party!");
}

unsigned int process_next_packet()
{
    unsigned int received = 0;

    // We process all packages until the queue is empty, because we only
    // care for the latest state
    e131_packet_t packet;
    while (!g_e131.isEmpty())
    {
        g_e131.pull(&packet);     // Pull packet from ring buffer
        received++;
    }

    if (received > 0)
    {
        /*
        Serial.printf("Universe %u / %u Channels | Packet#: %u / Errors: %u \n",
                htons(packet.universe),                 // The Universe for this packet
                htons(packet.property_value_count) - 1, // Start code is ignored, we're interested in dimmer data
                g_e131.stats.num_packets,               // Packet counter
                g_e131.stats.packet_errors              // Packet error counter
                );
        */

        int offset = g_dmx_channel_offset;
        g_channel1 = packet.property_values[offset + 0];
        g_channel2 = packet.property_values[offset + 1];
        g_channel3 = packet.property_values[offset + 2];
        g_channel4 = packet.property_values[offset + 3];

    }

    return received;
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
    if (iotWebConf.handleCaptivePortal())
    {
        return;
    }

    String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
    s += "<title>";
    s += NAME;
    s += "</title></head><body>Settings:";
    s += "<ul>";
    s += "<li>Use E1.31 DMX: ";
    s += BOOL2STRING(g_usee131);
    s += "<li>E1.31 DMX universe: ";
    s += g_dmx_universe;
    s += "<li>E1.31 DMX channel offset: ";
    s += g_dmx_channel_offset;
    s += "<li>Enable power on Port 1: ";
    s += BOOL2STRING(g_port1);
    s += "<li>Enable power on Port 2: ";
    s += BOOL2STRING(g_port2);
    s += "<li>Enable power on Port 3: ";
    s += BOOL2STRING(g_port3);
    s += "<li>Enable power on Port 4: ";
    s += BOOL2STRING(g_port4);
    s += "</ul>";
    s += "Go to <a href='config'>configure page</a> to change values.";
    s += "</body></html>\n";

    web_server.send(200, "text/html", s);
}

void config_saved_callback()
{
    Serial.println("Configuration was updated callback.");
    translate_config();
    setupE131();
}

void wifi_connected_callback()
{
      Serial.println("WiFi was connected callback.");
#ifdef ESP32
      // TODO: Is it necessary to disable wifi sleep after each reconnect?
      WiFi.setSleep(false);
      btStop();
#endif
      setupE131();
}

boolean form_validator_callback()
{
    Serial.println("Validating form. This does nothing atm.");
    boolean valid = true;

    /* int l = web_server.arg(stringParam.getId()).length(); */
    /* if (l < 3) */
    /* { */
        /* stringParam.errorMessage = "Please provide at least 3 characters for this test!"; */
        /* valid = false; */
    /* } */

    return valid;
}


void loop()
{
    iotWebConf.doLoop();

    /* if (millis() % 100000 == 0) */
            /* Serial.println("WiFi state " + String(WiFi.status())); */

    g_cur_time = millis();

    if (g_usee131)
    {
        if (g_cur_time - g_last_stat_print > 10000)
        {
            Serial.println("current E1.31 rate: " + String(g_received / 10.0f) + " packets/s");
            g_received = 0;
            g_last_stat_print = g_cur_time;
        }

        // if nothing is received in DMX mode for too long, switch off
        unsigned int received  = process_next_packet();
        if (received > 0)
        {
            g_last_dmx_received = g_cur_time;
        }
        else
        {
            // Restart if there was no packet for some time.
            if ((g_cur_time - g_last_dmx_received > 60000) 
                    && iotWebConf.getState() != IOTWEBCONF_STATE_AP_MODE)
            {
                Serial.println("Restarting because we did not receive a E1.31 DMX Packet in some time.");
                ESP.restart();
            }
        }
        g_received += received;
    }
    else
    {
        g_channel1 = g_port1 ? 255 : 0;
        g_channel2 = g_port2 ? 255 : 0;
        g_channel3 = g_port3 ? 255 : 0;
        g_channel4 = g_port4 ? 255 : 0;
    }
    set_outputs();
}
