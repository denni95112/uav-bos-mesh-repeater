#include "display.h"

#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "pins.h"

namespace display {

namespace {

constexpr int W = 128;
constexpr int H = 64;

Adafruit_SSD1306 oled(W, H, &Wire, PIN_OLED_RST);
bool panelOn = true;
const char *modeBadge = "AP";

void powerOn() {
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, VEXT_ON);
  delay(50);
}

void paint() {
  if (!panelOn) return;
  oled.display();
}

void line(int y, const char *text) {
  oled.setCursor(0, y);
  oled.print(text);
}

} // namespace

void begin() {
  powerOn();
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[display] OLED not found");
    return;
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.cp437(true);
  paint();
}

void setMode(RepeaterMode mode) { modeBadge = config::modeShort(mode); }

bool on() { return panelOn; }

void setOn(bool on) {
  panelOn = on;
  oled.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}

void toggle() { setOn(!panelOn); }

void showBoot(const char *version) {
  oled.clearDisplay();
  oled.setTextSize(2);
  line(8, "UAV-BOS");
  oled.setTextSize(1);
  line(32, "Mesh-Repeater");
  char buf[24];
  snprintf(buf, sizeof(buf), "FW %s", version);
  line(48, buf);
  paint();
}

void showStatus(const MeshStats &mesh, int apClients) {
  oled.clearDisplay();
  oled.setTextSize(1);
  char buf[32];
  snprintf(buf, sizeof(buf), "Repeater  %s", modeBadge);
  line(0, buf);

  if (!mesh.ok) {
    line(12, mesh.error[0] ? mesh.error : "Funk nicht bereit");
  } else {
    line(12, mesh.placeholderKey ? "Standard-Schluessel!" : "Funk ok");
    snprintf(buf, sizeof(buf), "%u Kn  Rel %lu", mesh.heardNodes, (unsigned long)mesh.relayCount);
    line(24, buf);
    snprintf(buf, sizeof(buf), "Air %.1f%%", mesh.airtimePercent);
    line(36, buf);
    snprintf(buf, sizeof(buf), "!%08lx  %s", (unsigned long)mesh.nodeNum, mesh.presetName);
    line(48, buf);
  }

  if (apClients < 0) line(56, "WLAN aus");
  else {
    snprintf(buf, sizeof(buf), "AP  %u Geraete", apClients);
    line(56, buf);
  }
  paint();
}

void showModeSelect(RepeaterMode selected, RepeaterMode current, uint32_t remainMs) {
  oled.clearDisplay();
  oled.setTextSize(1);
  line(0, "Betriebsart");
  line(16, config::modeName(selected));
  char buf[32];
  snprintf(buf, sizeof(buf), "jetzt: %s", config::modeShort(current));
  line(32, buf);
  snprintf(buf, sizeof(buf), "in %u s", (unsigned)((remainMs + 999) / 1000));
  line(48, buf);
  paint();
}

} // namespace display
