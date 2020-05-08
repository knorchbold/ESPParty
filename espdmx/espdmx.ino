/******************************************************************************
 * Universal firmware to run WS2812 LED strips from an ESP8266 or ESP32 
 * via E1.31 DMX over Ethernet
 ******************************************************************************/

#include <ESPAsyncE131.h>
#include <IotWebConf.h>
#include <NeoPixelBus.h>

// Initial AP config, can be changed in web interface
#define NAME "ESPDMX"
#define PASSWORD "supersecret"

// Optional Status LED for iotWebConf. D1 Mini uses 2.
#define STATUS_PIN 2

// Determines buffer size for E1.31. Do not change unless you want to receive
// multiple universes and change the receive code below.
#define UNIVERSE_COUNT 1 // determines buffer size for E1.31

#define STRING_LEN 128
#define NUMBER_LEN 32
#define BOOL2STRING(x) (String(x ? "true" : "false"))
#define INT2BOOL(x) (x == 1 ? true : false)

// Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "v1"

// Configuration parameters (set via web interface)
// note that on an unconfigured board, these values will be set to the defaults
// defined on the corresponding value variables below.
bool g_usee131              = false;
int  g_dmx_universe         = 0;
int  g_dmx_channel_offset   = 0;
int  g_strip_pin            = 0;
int  g_num_leds             = 0;
int  g_default_r            = 0;
int  g_default_g            = 0;
int  g_default_b            = 0;

// Corresponding iotWebConf parameters.
char g_value_usee131[NUMBER_LEN]            = "0";
char g_value_dmx_universe[NUMBER_LEN]       = "1";
char g_value_dmx_channel_offset[NUMBER_LEN] = "1";
char g_value_strip_pin[NUMBER_LEN]          = "4";
char g_value_num_leds[NUMBER_LEN]           = "10";
char g_value_default_r[NUMBER_LEN]          = "255";
char g_value_default_g[NUMBER_LEN]          = "0";
char g_value_default_b[NUMBER_LEN]          = "0";

// internally used globals
unsigned long g_last_dmx_received   = 0;
unsigned long g_last_stat_print     = 0;
unsigned long g_cur_time            = 0;
unsigned int  g_packets_received    = 0;

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> *g_strip;
ESPAsyncE131        g_e131(UNIVERSE_COUNT);
DNSServer           dns_server;
WebServer           web_server(80);
HTTPUpdateServer    update_server;
IotWebConf          iotWebConf(NAME, &dns_server, &web_server, PASSWORD, CONFIG_VERSION);

IotWebConfSeparator separator = IotWebConfSeparator();
IotWebConfSeparator separator1 = IotWebConfSeparator("E1.31 DMX settings");

// Create the iotWebConf parameters. The default values are set here!
IotWebConfParameter g_cfg_usee131 = IotWebConfParameter("Listen for E1.31 DMX data", "usee131", g_value_usee131, NUMBER_LEN, "number", "0 = off, 1 = on", NULL, "min='0' max='1' step='1'");
IotWebConfParameter g_cfg_dmx_universe = IotWebConfParameter("E1.31 DMX universe", "dmx_universe", g_value_dmx_universe, NUMBER_LEN, "number", "1..512", NULL, "min='1' max='512' step='1'");
IotWebConfParameter g_cfg_dmx_channel_offset = IotWebConfParameter("E1.31 DMX channel offset", "dmx_channel", g_value_dmx_channel_offset, NUMBER_LEN, "number", "1..512", NULL, "min='1' max='511' step='1'");
IotWebConfSeparator separator2 = IotWebConfSeparator("LED strip settings");
IotWebConfParameter g_cfg_strip_pin = IotWebConfParameter("Pin connected to LED strip (This is ignored on ESP8266, which is fixed to GPIO3 (=RX))", "strip_pin", g_value_strip_pin, NUMBER_LEN, "number", "0", NULL, "min='0' max='64' step='1'");
IotWebConfParameter g_cfg_num_leds = IotWebConfParameter("Number of LEDs", "num_leds", g_value_num_leds, NUMBER_LEN, "number", "0", NULL, "min='0' max='512' step='1'");
IotWebConfParameter g_cfg_default_r = IotWebConfParameter("Default color r", "default_r", g_value_default_r, NUMBER_LEN, "number", "0", NULL, "min='0' max='255' step='1'");
IotWebConfParameter g_cfg_default_g = IotWebConfParameter("Default color g", "default_g", g_value_default_g, NUMBER_LEN, "number", "0", NULL, "min='0' max='255' step='1'");
IotWebConfParameter g_cfg_default_b = IotWebConfParameter("Default color b", "default_b", g_value_default_b, NUMBER_LEN, "number", "0", NULL, "min='0' max='255' step='1'");

// Callback method declarations.
void    wifi_connected_callback();
void    config_saved_callback();
boolean form_validator_callback();

void iotWeb_to_globals()
{
    g_usee131               = INT2BOOL(atoi(g_value_usee131));
    g_dmx_universe          = atoi(g_value_dmx_universe);
    g_dmx_channel_offset    = atoi(g_value_dmx_channel_offset);
    g_strip_pin             = atoi(g_value_strip_pin);
    g_num_leds              = atoi(g_value_num_leds);
    g_default_r             = atoi(g_value_default_r);
    g_default_g             = atoi(g_value_default_g);
    g_default_b             = atoi(g_value_default_b);

    Serial.println("Configuration values: ");
    Serial.println("Use E1.31: "                  + BOOL2STRING(g_usee131));
    Serial.println("DMX universe: "               + String(g_dmx_universe));
    Serial.println("DMX channel offset: "         + String(g_dmx_channel_offset));
    Serial.println("Pin connected to LED strip: " + String(g_strip_pin));
    Serial.println("Number of LEDs: "             + String(g_num_leds));
    Serial.println("Default color r: "            + String(g_default_r));
    Serial.println("Default color g: "            + String(g_default_g));
    Serial.println("Default color b: "            + String(g_default_b));
}

void set_outputs()
{
    g_strip->Show();
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
    Serial.println("Running on Core " + String(xPortGetCoreID()));
    // disable WiFi sleep mode. According to some bugreports it can 
    // introduce latency and we have plenty of power
    WiFi.setSleep(false);
    // disable bluetooth (is it disabled by default?)
    btStop();
#endif

    // iotWebConf manages WiFi configuration and also our custom
    // parameters with a nice web interface
    iotWebConf.setStatusPin(STATUS_PIN);
    // iotWebConf.setConfigPin(CONFIG_PIN);
    iotWebConf.setupUpdateServer(&update_server);
    iotWebConf.addParameter(&separator1);
    iotWebConf.addParameter(&g_cfg_usee131);
    iotWebConf.addParameter(&g_cfg_dmx_universe);
    iotWebConf.addParameter(&g_cfg_dmx_channel_offset);
    iotWebConf.addParameter(&separator2);
    iotWebConf.addParameter(&g_cfg_strip_pin);
    iotWebConf.addParameter(&g_cfg_num_leds);
    iotWebConf.addParameter(&g_cfg_default_r);
    iotWebConf.addParameter(&g_cfg_default_g);
    iotWebConf.addParameter(&g_cfg_default_b);

    iotWebConf.setConfigSavedCallback(&config_saved_callback);
    iotWebConf.setWifiConnectionCallback(&wifi_connected_callback);
    iotWebConf.setFormValidator(&form_validator_callback);
    // Show the paramter that sets the initial AP visibility 
    iotWebConf.getApTimeoutParameter()->visible = true;

    // Initializing the configuration. After that the saved parameter values
    // are available
    iotWebConf.init();

    // read the iotWeb config parameter's values (they are char*} to global 
    // variables that are correctly typed 
    iotWeb_to_globals();

    g_strip = new NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod>(g_num_leds, g_strip_pin);
    g_strip->Begin();

    // Set up required URL handlers on the web server.
    web_server.on("/", handleRoot);
    web_server.on("/config", []{ iotWebConf.handleConfig(); });
    web_server.onNotFound([](){ iotWebConf.handleNotFound(); });

    Serial.println(NAME " ready to party!");
}

unsigned int process_next_packet()
{
    unsigned int received = 0;
    int offset = g_dmx_channel_offset;

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

        for (int i = 0; i < g_num_leds; i++)
        {
            RgbColor color(
                    packet.property_values[offset + 3*i + 0],
                    packet.property_values[offset + 3*i + 1 ],
                    packet.property_values[offset + 3*i + 2 ]);

            g_strip->SetPixelColor(i, color);         //  Set pixel's color (in RAM)
        }

    }

    return received;
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
    // Let IotWebConf test and handle captive portal requests.
    if (iotWebConf.handleCaptivePortal())
    {
        // Captive portal request were already served.
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
    s += "</ul>";
    s += "Go to <a href='config'>configure page</a> to change values.";
    s += "</body></html>\n";

    web_server.send(200, "text/html", s);
}

void config_saved_callback()
{
    Serial.println("Configuration was updated callback.");
    iotWeb_to_globals();

    // Re-initialize LED strip
    delete g_strip;
    g_strip = new NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod>(g_num_leds, g_strip_pin);
    g_strip->Begin();

    setupE131();
}

void wifi_connected_callback()
{
      Serial.println("WiFi was connected callback.");
#ifdef ESP32
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

    g_cur_time = millis();

    if (g_usee131)
    {
        // Print stats from time to time.
        if (g_cur_time - g_last_stat_print > 10000)
        {
            Serial.println("current E1.31 rate: " 
                    + String(g_packets_received / 10.0f) + " packets/s");
            g_packets_received = 0;
            g_last_stat_print = g_cur_time;
        }

        unsigned int received  = process_next_packet();
        if (received > 0)
        {
            g_last_dmx_received = g_cur_time;
        }
        else
        {
            // Restart if there was no packet for some time.
            if ((g_cur_time - g_last_dmx_received > 600000)
                    && iotWebConf.getState() != IOTWEBCONF_STATE_AP_MODE)
            {
                Serial.println("Restarting because we did not receive a E1.31 DMX Packet in too long.");
                ESP.restart();
            }
        }
        g_packets_received += received;
    }
    else
    {
        g_strip->ClearTo(RgbColor(g_default_r, g_default_g, g_default_b));
    }
    set_outputs();
}
