#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <FastLED.h>
#include <unordered_map>
#include <string>
#include <list>

using namespace std;
const int margin = 1000;  // ms
const int wait = 2 * margin;  // ms

const int ledpin = 27;
const int numleds = 25;

int max_rssi = -50;
int min_rssi = -100;

struct rpi_data {
    unsigned long first_seen;
    unsigned long last_seen;
    int count = 0;
    int last_rssi;
};

unordered_map<string, rpi_data> seen;

void cleanup() {
    unsigned long now = millis();
    if (now <= wait) return;

    unordered_map<string, rpi_data>::iterator it = seen.begin();
    while (it != seen.end()) {
        auto rpi = it->first;
        auto data = it->second;
        if (data.last_seen > now - wait) {
            // not expired; ignore
            it++;
            continue;
        }
        for (auto maybe_same: seen) {
            // assume that first_seen ≈ last_seen means it's the same thing
            if (abs(maybe_same.second.first_seen - data.last_seen) < margin) {
                // assume it's the same thing, and copy stuff
                seen[maybe_same.first].count += data.count;
                seen[maybe_same.first].first_seen = data.first_seen;
                break;
            }
        }
        it = seen.erase(it);
    }
}

CRGB leds[numleds];

void display() {
    for (int i = 0; i < numleds; i++) leds[i] = CRGB(0,0,0);

    unsigned long now = millis();

    list<struct rpi_data> things;
    for (auto& thing: seen) {
        if (now > wait && now <= thing.second.first_seen + wait) continue; // too new, skip
        things.push_back(thing.second);
    }
    things.sort([](rpi_data a, rpi_data b) { return a.first_seen < b.first_seen; });

    int ledindex = 0;
    for (auto& thing: things) {
        int rssi = thing.last_rssi;
        int hue = 170.0 * (float) (rssi - min_rssi) / (float) (max_rssi - min_rssi);
        //Serial.printf("r = %d, min = %d, max = %d\n", rssi, min_rssi, max_rssi);
        leds[ledindex] = CHSV(hue, 255, 255);
        ledindex++;
        if (ledindex >= numleds) break;
    }
    FastLED.show();
}

String hex(string bin) {
    String r = "";
    for (auto c: bin) r += Sprintf("%02x", c);
    return r;
}

class dinges: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) {
        if (!dev.haveServiceUUID()) return;
        if (dev.getServiceUUID().toString() != "0000fd6f-0000-1000-8000-00805f9b34fb") return;

        string data = dev.getServiceData();
        if (data.length() != 20) return;

        string rpi = data.substr(0, 16);
        string aem = data.substr(16, 4);
        int rssi = dev.getRSSI();

        Serial.printf(
            "RPI: %s, AEM: %s, RSSI: %d, ",
            hex(rpi).c_str(),
            hex(aem).c_str(),
            rssi
        );

        if (rssi < min_rssi) min_rssi = rssi;
        if (rssi > max_rssi) max_rssi = rssi;

        auto& record = seen[rpi];
        if (!record.count) {
            record.first_seen = millis();
        }
        record.last_seen = millis();
        record.last_rssi = rssi;
        record.count++;

        Serial.printf("%d -> %d, #%d (n=%d)\n", record.first_seen, record.last_seen, record.count, seen.size());

        cleanup();
        display();
    }
};

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812B, ledpin, GRB>(leds, numleds);
    FastLED.setBrightness(20);

    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new dinges(), true);
    pBLEScan->setActiveScan(true);
    BLEScanResults foundDevices = pBLEScan->start(0);
}

void loop() { }
