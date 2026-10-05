#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <math.h>
#include <PNGdec.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include "utf8_mono_font.h"
#include "sans_menu_font.h"
#include "preset_names.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {
constexpr int kEncoderA = 48;
constexpr int kEncoderB = 47;
constexpr int kButton = 9;
constexpr int kInitialVolume = 25;
constexpr int kVolumePerDetent = 2;
String deviceIp = "192.168.0.2";
constexpr uint32_t kPollIntervalMs = 15000;
constexpr uint32_t kFallbackPollIntervalMs = 4000;
constexpr uint32_t kVolumeSendDelayMs = 350;
constexpr uint32_t kVolumeRetryDelayMs = 2000;
constexpr uint32_t kLongPressMs = 850;
constexpr uint32_t kDimAfterMs = 30000;
constexpr uint8_t kNormalBrightness = 200;
constexpr uint8_t kDimBrightness = 3;  // 3/255 ~ 1% of the PWM range.
constexpr int kArtSize = 76;
constexpr uint32_t kMarqueeStepMs = 230;
constexpr size_t kMaxArtworkBytes = 350 * 1024;
constexpr uint32_t kArtworkPollMs = 20000;
constexpr uint32_t kGroupPollMs = 15000;
constexpr uint32_t kSlavePollMs = 8000;
constexpr char kFirmwareVersion[] = "0.7.3";

class KnobDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  KnobDisplay() {
    {
      auto c = bus_.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = true;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 5;
      c.pin_mosi = 1;
      c.pin_miso = -1;
      c.pin_dc = 3;
      bus_.config(c);
      panel_.setBus(&bus_);
    }
    {
      auto c = panel_.config();
      c.pin_cs = 2;
      c.pin_rst = 8;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 240;
      c.memory_width = 240;
      c.memory_height = 240;
      c.offset_x = 0;
      c.offset_y = 0;
      c.invert = true;
      c.rgb_order = false;
      panel_.config(c);
    }
    {
      auto c = light_.config();
      c.pin_bl = 6;
      c.invert = false;
      c.freq = 12000;
      c.pwm_channel = 0;
      light_.config(c);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

// Render normally to the LCD. For a screenshot, render the current state once
// into a temporary RGB565 sprite; the panel itself has no readback wiring.
class DisplayRouter {
  KnobDisplay lcd_;
  lgfx::LGFX_Sprite sprite_{&lcd_};
  bool capturing_ = false;
 public:
  void init() { lcd_.init(); sprite_.setColorDepth(16); }
  void setBrightness(uint8_t level) { lcd_.setBrightness(level); }
  bool beginCapture() {
    if (!sprite_.createSprite(240, 240)) return false;
    capturing_ = true;
    return true;
  }
  bool isCapturing() const { return capturing_; }
  void presentCapture() { if (capturing_) sprite_.pushSprite(0, 0); }
  void endCapture() { capturing_ = false; sprite_.deleteSprite(); }
  uint16_t capturePixel(int x, int y) { return sprite_.readPixel(x, y); }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return lcd_.color565(r, g, b);
  }
  template<typename C> void fillScreen(C c) { if (capturing_) sprite_.fillScreen(c); else lcd_.fillScreen(c); }
  template<typename C> void fillRect(int x, int y, int w, int h, C c) {
    if (capturing_) sprite_.fillRect(x,y,w,h,c); else lcd_.fillRect(x,y,w,h,c);
  }
  template<typename C> void fillRoundRect(int x, int y, int w, int h, int r, C c) {
    if (capturing_) sprite_.fillRoundRect(x,y,w,h,r,c);
    else lcd_.fillRoundRect(x,y,w,h,r,c);
  }
  template<typename C> void fillCircle(int x, int y, int r, C c) {
    if (capturing_) sprite_.fillCircle(x,y,r,c); else lcd_.fillCircle(x,y,r,c);
  }
  template<typename C> void drawLine(int x1, int y1, int x2, int y2, C c) {
    if (capturing_) sprite_.drawLine(x1,y1,x2,y2,c);
    else lcd_.drawLine(x1,y1,x2,y2,c);
  }
  template<typename C> void drawPixel(int x, int y, C c) {
    if (capturing_) sprite_.drawPixel(x,y,c); else lcd_.drawPixel(x,y,c);
  }
  void startWrite() { if (capturing_) sprite_.startWrite(); else lcd_.startWrite(); }
  void endWrite() { if (capturing_) sprite_.endWrite(); else lcd_.endWrite(); }
  template<typename T> void setTextDatum(T datum) {
    if (capturing_) sprite_.setTextDatum(datum); else lcd_.setTextDatum(datum);
  }
  void setTextSize(int size) {
    if (capturing_) sprite_.setTextSize(size); else lcd_.setTextSize(size);
  }
  template<typename F, typename B> void setTextColor(F foreground, B background) {
    if (capturing_) sprite_.setTextColor(foreground, background);
    else lcd_.setTextColor(foreground, background);
  }
  void setSwapBytes(bool swap) {
    if (capturing_) sprite_.setSwapBytes(swap); else lcd_.setSwapBytes(swap);
  }
  void pushImage(int x, int y, int w, int h, const uint16_t* pixels) {
    if (capturing_) sprite_.pushImage(x,y,w,h,pixels);
    else lcd_.pushImage(x,y,w,h,pixels);
  }
};
DisplayRouter display;
uint16_t accentColor() { return display.color565(255, 159, 10); }
WiFiServer screenshotServer(8080);
WebServer settingsServer(80);
Preferences settings;
bool isSlave = false;
String masterIp;
String groupValue;
bool groupKnown = false;
uint32_t lastGroupPollAt = 0;
bool rebootAfterResponse = false;
bool otaUploadFailed = false;
bool otaUploadFinished = false;
int volume = kInitialVolume;
bool playing = false;
uint8_t encoderPrevious = 0;
int encoderAccumulator = 0;
bool buttonRawPrevious = HIGH;
bool buttonStable = HIGH;
uint32_t buttonChangedAt = 0;
bool deviceOnline = false;
bool volumeDirty = false;
uint32_t volumeChangedAt = 0;
uint32_t volumeRetryAt = 0;
uint8_t volumeFailures = 0;
uint32_t lastPollAt = 0;
uint32_t lastA31ActivityAt = 0;
WiFiClient a31Tcp;
String a31TcpIp;
uint32_t lastTcpAttemptAt = 0;
uint32_t lastTcpSentAt = 0;
uint8_t tcpInput[2048];
size_t tcpInputLength = 0;
uint32_t pressedAt = 0;
bool longPressHandled = false;
uint32_t lastInteractionAt = 0;
bool displayDimmed = false;
enum class MenuScreen : uint8_t { Player, Home, Presets, Source, Multiroom,
                                  Join, Zone, Settings, EqPresets, ToneList, ToneEdit, LeaveConfirm,
                                  DeviceInfo, NetworkInfo, MemoryInfo, Scan };
MenuScreen menuScreen = MenuScreen::Player;
int selectedItem = 0;
String menuMessage;
int toneRaw[3] = {5, 0, 5};  // Bass, Mid, Treble; EQ bass/treble are offset by +5.
bool toneKnown[3] = {false, false, false};
bool virtualBassKnown = false;
bool virtualBassEnabled = false;
int editingTone = 0;
int editingRaw = 0;
String toneMessage;
bool mcuCommand(const String& payload, String* reply = nullptr);
struct ZoneInfo { String ip; String name; String uuid; };
ZoneInfo zones[8];
int zoneCount = 0;
WiFiUDP discoveryUdp;
MenuScreen scanReturnScreen = MenuScreen::Zone;
uint32_t scanStartedAt = 0;
uint8_t scanPhase = 0;
bool scanInBackground = false;
int scanHost = 1;
String infoLines[5];
int infoLineCount = 0;
bool muted = false;
int selectedPreset = 1;
String trackTitle;
String trackArtist;
String currentArtworkUrl;
uint32_t lastArtworkPollAt = 0;
bool artworkReady = false;
bool artworkAttempted = false;
uint32_t marqueeStartedAt = 0;
uint32_t marqueeLastAt = 0;
int marqueeOffset = 0;
uint16_t artPixels[kArtSize * kArtSize] = {};
int sourceWidth = 0;
int sourceHeight = 0;
void drawScreen();
void drawVolumeUpdate();
void drawMenu();
void drawMetadata();
PNG pngDecoder;
JPEGDEC jpegDecoder;

int pngLine(PNGDRAW* line) {
  if (!line || sourceWidth < 1 || sourceHeight < 1 || line->iWidth > 2048) return 0;
  static uint16_t pixels[2048];
  pngDecoder.getLineAsRGB565(line, pixels, PNG_RGB565_LITTLE_ENDIAN, 0);
  const int side = max(sourceWidth, sourceHeight);
  const int targetW = max(1, sourceWidth * kArtSize / side);
  const int targetH = max(1, sourceHeight * kArtSize / side);
  const int y = (kArtSize - targetH) / 2 + line->y * targetH / sourceHeight;
  if (y < 0 || y >= kArtSize) return 1;
  for (int x = 0; x < targetW; ++x) {
    const int sourceX = x * line->iWidth / targetW;
    artPixels[y * kArtSize + (kArtSize - targetW) / 2 + x] = pixels[sourceX];
  }
  return 1;
}

int jpegBlock(JPEGDRAW* block) {
  if (!block || sourceWidth < 1 || sourceHeight < 1) return 0;
  const int side = max(sourceWidth, sourceHeight);
  const int targetW = max(1, sourceWidth * kArtSize / side);
  const int targetH = max(1, sourceHeight * kArtSize / side);
  for (int by = 0; by < block->iHeight; ++by) {
    const int sy = block->y + by;
    const int dy = (kArtSize - targetH) / 2 + sy * targetH / sourceHeight;
    if (dy >= kArtSize) continue;
    for (int bx = 0; bx < block->iWidth; ++bx) {
      const int sx = block->x + bx;
      const int dx = (kArtSize - targetW) / 2 + sx * targetW / sourceWidth;
      if (dx >= 0 && dx < kArtSize)
        artPixels[dy * kArtSize + dx] = block->pPixels[by * block->iWidth + bx];
    }
  }
  return 1;
}

String decodeXml(String xml) {
  xml.replace("&lt;", "<");
  xml.replace("&gt;", ">");
  xml.replace("&quot;", "\"");
  xml.replace("&apos;", "'");
  xml.replace("&amp;", "&");
  return xml;
}

String xmlValue(const String& xml, const char* localName) {
  const int name = xml.indexOf(localName);
  if (name < 0) return "";
  const int open = xml.indexOf('>', name);
  if (open < 0) return "";
  const int close = xml.indexOf('<', open + 1);
  if (close < 0) return "";
  return xml.substring(open + 1, close);
}

void pollEncoder();
void sendVolumeWhenReady();

// Network artwork is fetched synchronously; keep the encoder and volume
// command alive between incoming chunks instead of waiting for the cover.
void serviceVolumeDuringArtwork() {
  pollEncoder();
  sendVolumeWhenReady();
  if (WiFi.status() == WL_CONNECTED) settingsServer.handleClient();
}

String fetchArtworkAddress() {
  WiFiClient client;
  client.setTimeout(2);
  const String body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"><InstanceID>0</InstanceID></u:GetPositionInfo></s:Body></s:Envelope>";
  IPAddress target;
  if (!target.fromString(deviceIp) || !client.connect(target, 49152, 350)) return "";
  client.print(String("POST /upnp/control/rendertransport1 HTTP/1.1\r\n") +
      "Host: " + deviceIp + ":49152\r\n" +
      "Content-Type: text/xml; charset=\"utf-8\"\r\n" +
      "SOAPAction: \"urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo\"\r\n" +
      "Connection: close\r\nContent-Length: " + String(body.length()) + "\r\n\r\n" + body);
  String response;
  response.reserve(12000);
  uint32_t lastByteAt = millis();
  while ((client.connected() || client.available()) && millis() - lastByteAt < 2000 && response.length() < 24000) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      lastByteAt = millis();
    }
    serviceVolumeDuringArtwork();
    delay(1);
  }
  client.stop();
  String metadata = decodeXml(xmlValue(response, "TrackMetaData"));
  String url = decodeXml(xmlValue(metadata, "albumArtURI"));
  if (url.startsWith("/")) url = "http://" + deviceIp + ":49152" + url;
  return url;
}

bool downloadAndDecodeArt(String url) {
  // This exact Qobuz CDN hostname was verified to serve the same cover over
  // HTTP 200. Do not downgrade other HTTPS hosts without checking them.
  if (url.startsWith("https://static.qobuz.com/")) {
    url.replace("https://", "http://");
    Serial.println("Artwork: Qobuz HTTP endpoint");
  }
  for (int redirects = 0; redirects < 4; ++redirects) {
    // This PlatformIO Arduino package has no secure-client library.
    // TuneIn A31 test artwork was verified to return HTTP 200 directly.
    if (!url.startsWith("http://")) return false;
    WiFiClient plain;
    HTTPClient http;
    http.setConnectTimeout(750);
    http.setTimeout(1800);
    const bool began = http.begin(plain, url);
    if (!began) return false;
    const char* headers[] = {"Location"};
    http.collectHeaders(headers, 1);
    const int status = http.GET();
    if (status >= 300 && status < 400) {
      String next = http.header("Location");
      http.end();
      if (next.startsWith("/")) {
        const int hostEnd = url.indexOf('/', url.indexOf("//") + 2);
        next = url.substring(0, hostEnd < 0 ? url.length() : hostEnd) + next;
      }
      if (next.isEmpty() || next == url) return false;
      url = next;
      continue;
    }
    if (status != 200 || http.getSize() > static_cast<int>(kMaxArtworkBytes)) {
      http.end();
      return false;
    }
    const size_t capacity = psramFound() ? kMaxArtworkBytes : 96 * 1024;
    uint8_t* bytes = static_cast<uint8_t*>(psramFound()
        ? heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        : malloc(capacity));
    if (!bytes) { http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    size_t length = 0;
    uint32_t lastDataAt = millis();
    while ((http.connected() || stream->available()) && millis() - lastDataAt < 1800) {
      const int available = stream->available();
      if (available > 0) {
        const size_t amount = min(static_cast<size_t>(available), capacity - length);
        if (!amount) break;
        length += stream->readBytes(bytes + length, amount);
        lastDataAt = millis();
      } else delay(1);
      serviceVolumeDuringArtwork();
    }
    http.end();
    bool ok = false;
    memset(artPixels, 0, sizeof(artPixels));
    if (length > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N') {
      if (pngDecoder.openRAM(bytes, length, pngLine) == PNG_SUCCESS) {
        sourceWidth = pngDecoder.getWidth();
        sourceHeight = pngDecoder.getHeight();
        ok = sourceWidth <= 2048 && sourceHeight <= 2048 &&
             sourceWidth > 0 && sourceHeight > 0 && !pngDecoder.isInterlaced() &&
             pngDecoder.decode(nullptr, 0) == PNG_SUCCESS;
        pngDecoder.close();
      }
    } else if (length > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
      if (jpegDecoder.openRAM(bytes, length, jpegBlock)) {
        sourceWidth = jpegDecoder.getWidth();
        sourceHeight = jpegDecoder.getHeight();
        jpegDecoder.setPixelType(RGB565_LITTLE_ENDIAN);
        ok = sourceWidth > 0 && sourceHeight > 0 &&
             sourceWidth <= 2048 && sourceHeight <= 2048 &&
             jpegDecoder.decode(0, 0, 0) == 1;
        jpegDecoder.close();
      }
    }
    free(bytes);
    Serial.printf("Artwork: %s (%u bytes)\n", ok ? "decoded" : "decode failed", static_cast<unsigned>(length));
    return ok;
  }
  return false;
}

void pollArtwork() {
  if (WiFi.status() != WL_CONNECTED || menuScreen != MenuScreen::Player) return;
  const String url = fetchArtworkAddress();
  lastA31ActivityAt = millis();
  if (url.isEmpty()) {
    if (artworkReady) { artworkReady = false; currentArtworkUrl = ""; artworkAttempted = false; drawScreen(); }
    return;
  }
  if (url == currentArtworkUrl && artworkAttempted) return;
  currentArtworkUrl = url;
  artworkAttempted = true;
  artworkReady = downloadAndDecodeArt(url);
  Serial.println(artworkReady ? "Artwork ready" : "Artwork unavailable");
  drawScreen();
}

String decodeMetadata(const char* value) {
  if (!value || !*value) return "";
  const String input(value);
  if (input.length() < 2 || (input.length() & 1)) return input;
  for (size_t i = 0; i < input.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(input[i]))) return input;
  }
  String output;
  output.reserve(input.length() / 2);
  for (size_t i = 0; i < input.length(); i += 2) {
    char pair[3] = {input[i], input[i + 1], 0};
    const char c = static_cast<char>(strtoul(pair, nullptr, 16));
    if (c) output += c;
  }
  return output;
}

uint32_t nextCodepoint(const String& value, size_t& offset) {
  const uint8_t first = static_cast<uint8_t>(value[offset++]);
  if (first < 0x80) return first;
  if (first >= 0xC2 && first <= 0xDF && offset < value.length()) {
    return ((first & 0x1F) << 6) |
           (static_cast<uint8_t>(value[offset++]) & 0x3F);
  }
  if (first >= 0xE0 && first <= 0xEF && offset + 1 < value.length()) {
    const uint32_t second = static_cast<uint8_t>(value[offset++]);
    const uint32_t third = static_cast<uint8_t>(value[offset++]);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  return '?';
}

// Center a single line, fitting up to 21 ten-pixel glyphs inside the round LCD.
void drawUtf8Line(const String& value, int y, uint16_t color) {
  constexpr int maxGlyphs = 21;
  uint32_t codepoints[maxGlyphs] = {};
  int count = 0;
  size_t offset = 0;
  while (offset < value.length() && count < maxGlyphs) {
    codepoints[count++] = nextCodepoint(value, offset);
  }
  const int startX = (240 - count * kGlyphWidth) / 2;
  display.startWrite();
  for (int index = 0; index < count; ++index) {
    uint32_t cp = codepoints[index];
    int glyph = -1;
    if (cp >= 32 && cp <= 126) glyph = static_cast<int>(cp - 32);
    if (cp >= 0x0400 && cp <= 0x049F) glyph = 95 + static_cast<int>(cp - 0x0400);
    if (glyph < 0) glyph = '?' - 32;
    for (int row = 0; row < kGlyphHeight; ++row) {
      const uint16_t pixels = pgm_read_word(&kGlyphRows[glyph][row]);
      for (int col = 0; col < kGlyphWidth; ++col) {
        if (pixels & (1U << (kGlyphWidth - 1 - col))) {
          display.drawPixel(startX + index * kGlyphWidth + col, y + row, color);
        }
      }
    }
  }
  display.endWrite();
}

void drawScreen();

bool requestTo(const String& ip, const String& command, String& response,
               uint32_t timeoutMs = 1200) {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress target;
  if (!target.fromString(ip)) return false;
  HTTPClient http;
  http.setConnectTimeout(350);
  http.setTimeout(timeoutMs);
  if (!http.begin(String("http://") + ip + "/httpapi.asp?command=" + command)) return false;
  const int status = http.GET();
  if (status == 200) response = http.getString();
  http.end();
  if (ip == deviceIp) lastA31ActivityAt = millis();
  return status == 200 && response.indexOf("unknown command") < 0 &&
         response.indexOf("unknown comman") < 0;
}

bool request(const String& command, String& response) {
  return requestTo(deviceIp, command, response);
}

bool refreshGroupStatus() {
  String body;
  lastGroupPollAt = millis();
  if (!request("getStatusEx", body)) {
    const bool wasSlave = groupKnown && isSlave;
    groupKnown = false;
    if (wasSlave && menuScreen == MenuScreen::Player) drawScreen();
    return false;
  }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) {
    const bool wasSlave = groupKnown && isSlave;
    groupKnown = false;
    if (wasSlave && menuScreen == MenuScreen::Player) drawScreen();
    return false;
  }
  const String nextMaster = doc["master_ip"].as<String>();
  const String nextGroup = doc["group"].as<String>();
  // A31 may retain a master_ip after leaving. group=0 reports that it is free.
  const bool ungrouped = nextGroup == "0";
  const bool nextSlave = !ungrouped && nextMaster.length() && nextMaster != "0" &&
                         nextMaster != "0.0.0.0" && nextMaster != deviceIp;
  const bool changed = !groupKnown || isSlave != nextSlave || masterIp != nextMaster ||
                       groupValue != nextGroup;
  groupKnown = true;
  isSlave = nextSlave;
  masterIp = nextMaster;
  groupValue = nextGroup;
  if (changed) Serial.printf("A31 group=%s master_ip=%s slave=%d\n",
                             groupValue.c_str(), masterIp.c_str(), isSlave ? 1 : 0);
  if (changed && menuScreen == MenuScreen::Player) drawScreen();
  return true;
}

void pollDevice() {
  String body;
  if (!request("getPlayerStatus", body)) {
    deviceOnline = false;
    return;
  }
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body)) {
    deviceOnline = false;
    return;
  }
  const char* state = doc["status"] | "";
  muted = doc["mute"].as<String>() == "1";
  const bool newPlaying = strcmp(state, "play") == 0;
  // A31 may return vol as "32" (JSON string), not as a JSON number.
  // ArduinoJson's integer fallback can silently leave the previous value.
  int remoteVolume = volume;
  if (!doc["vol"].isNull()) {
    JsonVariant volumeField = doc["vol"];
    if (volumeField.is<const char*>()) {
      const char* value = volumeField.as<const char*>();
      if (value && *value && strspn(value, "0123456789") == strlen(value)) {
        remoteVolume = atoi(value);
      }
    } else if (volumeField.is<int>()) {
      remoteVolume = volumeField.as<int>();
    }
  }
  Serial.printf("A31 status: remote vol=%d, shown=%d, pending=%d\n",
                remoteVolume, volume, volumeDirty ? 1 : 0);
  const String newTitle = decodeMetadata(doc["Title"] | "");
  const String newArtist = decodeMetadata(doc["Artist"] | "");
  bool changed = !deviceOnline || newPlaying != playing;
  deviceOnline = true;
  playing = newPlaying;
  if (newTitle != trackTitle || newArtist != trackArtist) {
    marqueeStartedAt = millis();
    marqueeLastAt = marqueeStartedAt;
    marqueeOffset = 0;
    if (newTitle != trackTitle && deviceOnline && !trackTitle.isEmpty()) {
      // A new track/station must not keep showing the previous cover.
      artworkReady = false;
      artworkAttempted = false;
      currentArtworkUrl = "";
      lastArtworkPollAt = millis() - kArtworkPollMs;
    }
    trackTitle = newTitle;
    trackArtist = newArtist;
    changed = true;
  }
  bool volumeChanged = false;
  if (!volumeDirty && remoteVolume >= 0 && remoteVolume <= 100 && remoteVolume != volume) {
    volume = remoteVolume;
    volumeChanged = true;
  }
  if (changed) drawScreen();
  else if (volumeChanged) drawVolumeUpdate();
}

void sendVolumeWhenReady() {
  if (!volumeDirty || millis() - volumeChangedAt < kVolumeSendDelayMs ||
      static_cast<int32_t>(millis() - volumeRetryAt) < 0) return;
  String body;
  char command[16];
  snprintf(command, sizeof(command), "MCU+VOL+%03d", volume);
  const bool ok = mcuCommand(command) || request("setPlayerCmd:vol:" + String(volume), body);
  if (ok) {
    volumeDirty = false;
    volumeRetryAt = 0;
    volumeFailures = 0;
    deviceOnline = true;
    Serial.printf("A31 volume: %d%%\n", volume);
  } else {
    volumeRetryAt = millis() + kVolumeRetryDelayMs;
    if (++volumeFailures >= 3) {
      volumeDirty = false;
      volumeFailures = 0;
      Serial.println("A31 volume retries stopped; next status poll will resync");
    }
    deviceOnline = false;
    Serial.println("A31 volume send failed; retry in 2 s");
  }
  if (!ok) drawScreen();
}


void drawVolumeArc() {
  // Open horseshoe from the lower left, across the top, to the lower right.
  constexpr float startDegrees = 160.0f;
  // The two ends meet the same height, clear of artwork and metadata.
  constexpr float sweepDegrees = 220.0f;
  constexpr int segments = 100;
  constexpr float centerX = 120.0f;
  constexpr float centerY = 120.0f;
  constexpr float radius = 108.0f;
  constexpr int thickness = 9;
  const int activeSegments = (volume * segments + 50) / 100;
  const float middleRadius = radius + (thickness - 1) * .5f;
  // Overlapping disks close every raster gap between angle samples. Drawing
  // the active path second covers every inactive pixel inside the yellow arc.
  const auto paint = [&](int lastSegment, uint16_t color) {
    for (int i = 0; i <= lastSegment; ++i) {
      const float a = (startDegrees + sweepDegrees * i / segments) * PI / 180.0f;
      display.fillCircle(roundf(centerX + middleRadius * cosf(a)),
                         roundf(centerY + middleRadius * sinf(a)),
                         thickness / 2 + 1, color);
    }
  };
  display.startWrite();
  const uint16_t inactive = display.color565(55, 55, 55);
  paint(segments, inactive);
  if (activeSegments) paint(activeSegments, accentColor());
  display.endWrite();
}

void drawVolumeLabel() {
  const int y = 210;
  display.fillRect(60, y, 120, kGlyphHeight, TFT_BLACK);
  drawUtf8Line("VOL " + String(volume) + "%", y, accentColor());
}

// Compose the whole 240x240 frame offscreen, then show it in one transfer.
void drawVolumeUpdate() {
  if (menuScreen == MenuScreen::Player) drawScreen();
}

void drawWindow(const String& value, int x, int y, int visible, int scale,
                int offset, uint16_t foreground, uint16_t background,
                bool enlarged = false) {
  uint32_t chars[128] = {};
  size_t pos = 0;
  int count = 0;
  while (pos < value.length() && count < 128)
    chars[count++] = nextCodepoint(value, pos);
  const int advance = enlarged ? 12 : kGlyphWidth * scale;
  const int height = enlarged ? 18 : kGlyphHeight * scale;
  display.fillRect(x, y, visible * advance, height, background);
  const int origin = count <= visible ? x + (visible - count) * advance / 2 : x;
  display.startWrite();
  for (int index = 0; index < visible; ++index) {
    const int source = index + (count <= visible ? 0 : offset);
    if (source >= count) continue;
    const uint32_t cp = chars[source];
    int glyph = cp >= 32 && cp <= 126 ? static_cast<int>(cp - 32) : -1;
    if (cp >= 0x0400 && cp <= 0x049F) glyph = 95 + static_cast<int>(cp - 0x0400);
    if (glyph < 0) glyph = '?' - 32;
    for (int row = 0; row < kGlyphHeight; ++row) {
      const uint16_t bits = pgm_read_word(&kGlyphRows[glyph][row]);
      for (int column = 0; column < kGlyphWidth; ++column)
        if (bits & (1U << (kGlyphWidth - 1 - column))) {
          const int left = column * advance / kGlyphWidth;
          const int right = (column + 1) * advance / kGlyphWidth;
          const int top = row * height / kGlyphHeight;
          const int bottom = (row + 1) * height / kGlyphHeight;
          display.fillRect(origin + index * advance + left, y + top,
                           right - left, bottom - top, foreground);
        }
    }
  }
  display.endWrite();
}

int codepointCount(const String& value) {
  size_t pos = 0;
  int count = 0;
  while (pos < value.length() && count < 128) { nextCodepoint(value, pos); ++count; }
  return count;
}

uint16_t blendColor(uint16_t foreground, uint16_t background, uint8_t alpha) {
  const int inv = 15 - alpha;
  const int r = (((foreground >> 11) & 31) * alpha + ((background >> 11) & 31) * inv) / 15;
  const int g = (((foreground >> 5) & 63) * alpha + ((background >> 5) & 63) * inv) / 15;
  const int b = ((foreground & 31) * alpha + (background & 31) * inv) / 15;
  return (r << 11) | (g << 5) | b;
}

int sansMenuWidth(const String& value, bool selected) {
  const SansGlyph* glyphs = selected ? kSansSelectedGlyphs : kSansNormalGlyphs;
  int width = 0;
  size_t pos = 0;
  while (pos < value.length()) {
    const uint32_t cp = nextCodepoint(value, pos);
    const int index = cp >= 32 && cp <= 126 ? cp - 32 :
                      cp >= 0x0400 && cp <= 0x049F ? cp - 0x0400 + 95 : '?' - 32;
    width += glyphs[index].advance;
  }
  return width;
}

// Proportional, antialiased Sans menu text with ASCII and Cyrillic glyphs.
void drawSansMenu(const String& text, int x, int y, int width, int height,
                  bool selected, int offset, uint16_t foreground, uint16_t background) {
  const SansGlyph* glyphs = selected ? kSansSelectedGlyphs : kSansNormalGlyphs;
  const uint8_t* pixels = selected ? kSansSelectedPixels : kSansNormalPixels;
  uint32_t chars[128] = {};
  int count = 0;
  size_t pos = 0;
  while (pos < text.length() && count < 128) chars[count++] = nextCodepoint(text, pos);
  const auto glyphIndex = [](uint32_t cp) -> int {
    if (cp >= 32 && cp <= 126) return cp - 32;
    if (cp >= 0x0400 && cp <= 0x049F) return cp - 0x0400 + 95;
    return '?' - 32;
  };
  int fullWidth = 0;
  for (int i = 0; i < count; ++i) fullWidth += glyphs[glyphIndex(chars[i])].advance;
  const int first = fullWidth <= width ? 0 : constrain(offset, 0, count);
  int pen = x + (fullWidth <= width ? (width - fullWidth) / 2 : 0);
  const int baseline = y + height - (selected ? 5 : 4);
  display.fillRect(x, y, width, height, background);
  display.startWrite();
  for (int i = first; i < count && pen < x + width; ++i) {
    const SansGlyph& glyph = glyphs[glyphIndex(chars[i])];
    const int gx = pen + glyph.left;
    const int gy = baseline + glyph.top;
    for (int row = 0; row < glyph.height; ++row) {
      const int py = gy + row;
      if (py < y || py >= y + height) continue;
      for (int col = 0; col < glyph.width; ++col) {
        const int px = gx + col;
        if (px < x || px >= x + width) continue;
        const int index = row * glyph.width + col;
        const uint8_t packed = pgm_read_byte(pixels + glyph.offset + index / 2);
        const uint8_t alpha = index & 1 ? packed & 15 : packed >> 4;
        if (alpha) display.drawPixel(px, py, blendColor(foreground, background, alpha));
      }
    }
    pen += glyph.advance;
  }
  display.endWrite();
}

void drawMetadata() {
  if (menuScreen != MenuScreen::Player) return;
  drawWindow(trackTitle.isEmpty() ? String("NO TRACK") : trackTitle,
             48, 157, 12, 1, marqueeOffset, TFT_WHITE, TFT_BLACK, true);
  drawWindow(trackArtist, 48, 181, 12, 1, marqueeOffset,
             display.color565(185, 185, 185), TFT_BLACK, true);
}

const char* const kHomeItems[] = {"Presets", "Source", "Multiroom", "Zone", "Settings"};
const char* const kSourceItems[] = {
    "Network", "Bluetooth", "AUX In", "USB Disk", "Optical In", "RCA 2 In", "USB DAC"};
const char* const kSourceModes[] = {
    "wifi", "bluetooth", "line-in", "udisk", "optical", "line-in2", "PCUSB"};
const char* const kEqItems[] = {"Flat", "Classical", "Pop", "Jazz", "Rock", "Vocal"};
const char* const kMultiroomItems[] = {
    "Add to group", "Leave group", "Group status", "Find zones"};

int joinZoneIndex(int item) {
  for (int i = 0; i < zoneCount; ++i)
    if (zones[i].ip != deviceIp && item-- == 0) return i;
  return -1;
}

int menuCount() {
  switch (menuScreen) {
    case MenuScreen::Home: return 5;
    case MenuScreen::Presets: return 10;
    case MenuScreen::Source: return 7;
    case MenuScreen::Multiroom: return 4;
    case MenuScreen::Join: return max(1, zoneCount - 1);
    case MenuScreen::Zone: return zoneCount + 1;  // Last item starts/resumes discovery.
    case MenuScreen::Settings: return 7;
    case MenuScreen::ToneList: return 5;
    case MenuScreen::EqPresets: return 6;
    case MenuScreen::LeaveConfirm: return 2;
    default: return 0;
  }
}

String menuLabel(int item) {
  if (item < 0 || item >= menuCount()) return "";
  switch (menuScreen) {
    case MenuScreen::Home: return kHomeItems[item];
    case MenuScreen::Presets: return String(item + 1) + " " + kPresetNames[item];
    case MenuScreen::Source: return kSourceItems[item];
    case MenuScreen::Multiroom: return kMultiroomItems[item];
    case MenuScreen::Join: {
      const int i = joinZoneIndex(item);
      return i < 0 ? "NO PEERS" : zones[i].name;
    }
    case MenuScreen::Zone:
      return item < zoneCount ?
          String(zones[item].ip == deviceIp ? "* " : "") + zones[item].name + " " +
              zones[item].ip.substring(zones[item].ip.lastIndexOf('.') + 1) :
          String("Find zones");
    case MenuScreen::Settings:
      switch (item) {
        case 0: return muted ? "Unmute" : "Mute";
        case 1: return "EQ presets";
        case 2: return "Tone control";
        case 3: return "Zone info";
        case 4: return "Zone IP";
        case 5: return "KNOB info";
        default: return "Find zones";
      }
    case MenuScreen::ToneList:
      if (item == 4) return "Refresh";
      if (item == 3) return String("Virtual Bass ") +
          (virtualBassKnown ? (virtualBassEnabled ? "ON" : "OFF") : "--");
      return String(item == 0 ? "Bass " : item == 1 ? "Mid " : "Treble ") +
          (toneKnown[item] ? String(toneRaw[item] - (item == 1 ? 0 : 5)) + " dB" : "--");
    case MenuScreen::EqPresets: return kEqItems[item];
    case MenuScreen::LeaveConfirm: return item ? "YES, LEAVE" : "CANCEL";
    default: return "";
  }
}

const char* menuTitle() {
  switch (menuScreen) {
    case MenuScreen::Home: return "airControl";
    case MenuScreen::Presets: return "PRESETS";
    case MenuScreen::Source: return "SOURCE";
    case MenuScreen::Multiroom: return "MULTIROOM";
    case MenuScreen::Join: return "ADD TO GROUP";
    case MenuScreen::Zone: return "ZONE";
    case MenuScreen::Settings: return "SETTINGS";
    case MenuScreen::EqPresets: return "EQ PRESETS";
    case MenuScreen::ToneList: return "TONE";
    case MenuScreen::LeaveConfirm: return "LEAVE GROUP?";
    default: return "";
  }
}

void drawMenu() {
  if (menuScreen == MenuScreen::Player) return;
  display.fillRect(16, 25, 208, 182, TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  drawSansMenu(menuTitle(), 30, 14, 180, 23, false, 0, TFT_WHITE, TFT_BLACK);
  const int yPositions[] = {43, 68, 99, 139, 168};
  for (int relative = -2; relative <= 2; ++relative) {
    const int index = selectedItem + relative;
    if (index < 0 || index >= menuCount()) continue;
    const bool active = relative == 0;
    const int y = yPositions[relative + 2];
    if (active) {
      const uint16_t pill = display.color565(61, 61, 61);
      display.fillRoundRect(25, 94, 190, 37, 18, pill);
      drawSansMenu(menuLabel(index), 32, 99, 176, 26, true, marqueeOffset,
                   TFT_WHITE, pill);
    } else {
      drawSansMenu(menuLabel(index), 35, y, 170, 22, false, 0,
                   display.color565(125, 125, 125), TFT_BLACK);
    }
  }
  display.fillRect(20, 210, 200, 18, TFT_BLACK);
  drawWindow(menuMessage.isEmpty() ? String("HOLD: BACK") : menuMessage,
             40, 210, 16, 1, 0, accentColor(), TFT_BLACK);
}

void drawInformation() {
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  drawWindow(menuScreen == MenuScreen::MemoryInfo ? "KNOB INFO" :
             menuScreen == MenuScreen::DeviceInfo ? "ZONE INFO" : "ZONE IP",
             35, 27, 17, 1, 0, TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < infoLineCount; ++i) {
    const uint16_t color = i == selectedItem ? TFT_WHITE : display.color565(150, 150, 150);
    drawWindow(infoLines[i], 30, 59 + i * 29, 18, 1,
               i == selectedItem ? marqueeOffset : 0, color, TFT_BLACK);
  }
  drawWindow("HOLD: BACK", 70, 204, 10, 1, 0, accentColor(), TFT_BLACK);
}

void drawScanScreen() {
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  drawWindow("FIND ZONES", 45, 48, 15, 1, 0, TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  const String progress = scanPhase == 1 ? String("SSDP") : String(scanHost - 1) + "/254";
  drawWindow(progress, 40, 96, 8, 2, 0, TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  drawWindow(String(zoneCount) + " A31 FOUND", 40, 149, 16, 1, 0, TFT_WHITE, TFT_BLACK);
  drawWindow("HOLD: STOP", 65, 198, 11, 1, 0, accentColor(), TFT_BLACK);
}

void drawScreen() {
  if (menuScreen == MenuScreen::Player && !display.isCapturing()) {
    if (display.beginCapture()) {
      drawScreen();
      display.presentCapture();
      display.endCapture();
      return;
    }
    // Low-memory fallback: retain controls if the frame buffer cannot fit.
    Serial.println("KNOB: frame buffer unavailable");
  }
  if (menuScreen == MenuScreen::Scan) { drawScanScreen(); return; }
  if (menuScreen == MenuScreen::DeviceInfo || menuScreen == MenuScreen::NetworkInfo ||
      menuScreen == MenuScreen::MemoryInfo) {
    drawInformation(); return;
  }
  if (menuScreen == MenuScreen::ToneEdit) {
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setTextSize(1);
    drawWindow(editingTone == 0 ? "BASS" : editingTone == 1 ? "MID" : "TREBLE",
               45, 46, 15, 1, 0, TFT_WHITE, TFT_BLACK);
    const int db = editingRaw - (editingTone == 1 ? 0 : 5);
    drawWindow(String(db > 0 ? "+" : "") + String(db) + " dB",
               30, 94, 9, 2, 0, TFT_WHITE, TFT_BLACK);
    drawWindow(toneMessage.isEmpty() ? "PRESS: SAVE" : toneMessage,
               35, 169, 15, 1, 0, accentColor(), TFT_BLACK);
    drawWindow("HOLD: CANCEL", 45, 199, 15, 1, 0, accentColor(), TFT_BLACK);
    return;
  }
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  if (menuScreen != MenuScreen::Player) {
    drawMenu();
    return;
  }
  drawVolumeArc();
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  drawWindow(deviceOnline ? (groupKnown && isSlave ? "SLAVE MODE" : (playing ? "PLAY" : "PAUSE")) : "OFFLINE",
             60, 32, 12, 1, 0,
             deviceOnline && groupKnown && isSlave ? display.color565(255, 60, 60) : TFT_WHITE, TFT_BLACK);
  if (artworkReady) {
    display.setSwapBytes(true);
    display.pushImage(82, 62, kArtSize, kArtSize, artPixels);
    display.setSwapBytes(false);
  } else {
    drawWindow("NO COVER", 60, 91, 12, 1, 0, TFT_WHITE, TFT_BLACK);
  }
  drawMetadata();
  drawVolumeLabel();
}

void handleScreenshot() {
  WiFiClient client = screenshotServer.available();
  if (!client) return;
  client.setTimeout(400);
  const String line = client.readStringUntil('\n');
  if (!line.startsWith("GET /screenshot.bmp ")) {
    client.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    client.stop();
    return;
  }
  // 115200 contiguous bytes are needed. PNG/JPEG downloads have already freed
  // their temporary buffers by the time the main loop serves this request.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 116000 ||
      !display.beginCapture()) {
    client.print("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n"
                 "Content-Length: 19\r\nConnection: close\r\n\r\nNot enough free RAM");
    client.stop();
    return;
  }
  drawScreen();
  constexpr uint32_t bytes = 54 + 240 * 240 * 3;
  uint8_t header[54] = {};
  header[0] = 'B'; header[1] = 'M';
  auto put16 = [&](int at, uint16_t n) {
    header[at] = uint8_t(n); header[at+1] = uint8_t(n >> 8);
  };
  auto put32 = [&](int at, uint32_t n) {
    for (int i = 0; i < 4; ++i) header[at+i] = uint8_t(n >> (8*i));
  };
  put32(2, bytes); put32(10, 54); put32(14, 40);
  put32(18, 240); put32(22, 240); put16(26, 1);
  put16(28, 24); put32(34, bytes - 54);
  client.print("HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\n"
               "Content-Disposition: attachment; filename=airControl-KNOB.bmp\r\n"
               "Content-Length: 172854\r\nConnection: close\r\n\r\n");
  client.write(header, sizeof(header));
  uint8_t row[240 * 3];
  for (int y = 239; y >= 0 && client.connected(); --y) {
    for (int x = 0; x < 240; ++x) {
      const uint16_t rgb = display.capturePixel(x, y);
      row[x*3]   = uint8_t((rgb & 31) * 255 / 31);
      row[x*3+1] = uint8_t(((rgb >> 5) & 63) * 255 / 63);
      row[x*3+2] = uint8_t(((rgb >> 11) & 31) * 255 / 31);
    }
    if (client.write(row, sizeof(row)) != sizeof(row)) break;
    delay(0);
  }
  display.endCapture();
  client.stop();
}

String htmlEscape(String value) {
  value.replace("&", "&amp;"); value.replace("<", "&lt;");
  value.replace(">", "&gt;"); value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

bool parseIpField(const String& text, IPAddress& address) {
  return address.fromString(text) && address != IPAddress(0,0,0,0) &&
         address != IPAddress(255,255,255,255);
}

void webPage(const String& notice = "") {
  const bool fixed = settings.getBool("fixed", false);
  const String ip = settings.getString("ip", "");
  const String gw = settings.getString("gw", "");
  const String mask = settings.getString("mask", "255.255.255.0");
  const String dns = settings.getString("dns", "");
  String page = F("<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>airControl KNOB</title><style>body{background:#000;color:#fff;font:16px system-ui;max-width:600px;margin:30px auto;padding:0 18px}"
    "section{border:1px solid #555;border-radius:12px;padding:20px;margin:18px 0}input{background:#161616;color:#fff;border:1px solid #777;"
    "border-radius:6px;padding:10px;width:100%;box-sizing:border-box;margin:4px 0 12px}input[type=checkbox]{width:auto}"
    "button,a{display:inline-block;background:#ff9f0a;color:#000;border:0;border-radius:7px;padding:10px 14px;font-weight:700;text-decoration:none}"
    "p,small{color:#aaa}label{display:block}</style><h1>airControl KNOB</h1>");
  page += "<p>Firmware " + String(kFirmwareVersion) + " · KNOB " + WiFi.localIP().toString() +
          " · A31 " + htmlEscape(deviceIp) + " · " +
          (groupKnown ? (isSlave ? "Slave" : "Independent / Master") : "Group unknown") + "</p>";
  page += "<p>Group: " + htmlEscape(groupValue) + " · master_ip: " + htmlEscape(masterIp) + "</p>";
  if (notice.length()) page += "<section>" + htmlEscape(notice) + "</section>";
  page += F(R"HTML(<section><h2>Зоны</h2><p id="zone-current"></p>
<button id="zone-search" type="button">Найти зоны</button><p id="zone-status"></p>
<select id="zone-list" style="width:100%;padding:12px;background:#161616;color:white;margin:12px 0"></select>
<button id="zone-select" type="button" disabled>Выбрать зону</button>
<p>Поиск совместимых A31. Выбранная зона сохраняется без перезагрузки KNOB.</p></section>
<script>
let zoneTimer;
const byId=id=>document.getElementById(id);
async function loadZones(){
 try{
  const r=await fetch('/zones',{cache:'no-store'});if(!r.ok)throw Error('Не удалось получить список');
  const d=await r.json(),list=byId('zone-list'),old=list.value;
  list.replaceChildren();
  for(const z of d.zones){const o=document.createElement('option');o.value=z.ip;o.textContent=z.name+' — '+z.ip;list.append(o);}
  if(d.zones.some(z=>z.ip===old))list.value=old;else list.value=d.current;
  byId('zone-current').textContent='Выбрана зона: '+d.current;
  byId('zone-status').textContent=d.scanning?'Идёт поиск… '+d.progress+'/254':'Найдено зон: '+d.zones.length;
  byId('zone-search').disabled=d.scanning;
  byId('zone-select').disabled=d.scanning||!list.value;
  clearTimeout(zoneTimer);if(d.scanning)zoneTimer=setTimeout(loadZones,1200);
 }catch(e){byId('zone-status').textContent=e.message;}
}
byId('zone-search').onclick=async()=>{
 try{byId('zone-search').disabled=true;const r=await fetch('/zones/scan',{method:'POST'});if(!r.ok)throw Error('Ошибка поиска');await loadZones();}
 catch(e){byId('zone-status').textContent=e.message;byId('zone-search').disabled=false;}
};
byId('zone-list').onchange=()=>{byId('zone-select').disabled=!byId('zone-list').value;};
byId('zone-select').onclick=async()=>{
 try{byId('zone-select').disabled=true;const r=await fetch('/zones/select',{method:'POST',body:new URLSearchParams({ip:byId('zone-list').value})});if(!r.ok)throw Error(await r.text());location.reload();}
 catch(e){byId('zone-status').textContent=e.message;byId('zone-select').disabled=false;}
};
loadZones();
</script>)HTML");
  page += F("<section><h2>Network</h2><form method='post' action='/save'>"
    "<label><input type='checkbox' name='fixed' value='1'");
  if (fixed) page += " checked";
  page += F("> Static IP (uncheck for DHCP)</label>");
  page += "<label>KNOB IP<input name='ip' value='" + htmlEscape(ip) + "'></label>";
  page += "<label>Gateway<input name='gw' value='" + htmlEscape(gw) + "'></label>";
  page += "<label>Subnet mask<input name='mask' value='" + htmlEscape(mask) + "'></label>";
  page += "<label>DNS<input name='dns' value='" + htmlEscape(dns) + "'></label>";
  page += "<label>A31 IP<input name='device' value='" + htmlEscape(deviceIp) + "'></label>";
  page += F("<button type='submit'>Save and restart</button></form></section>"
    "<section><h2>Обновление прошивки</h2><p>Обновление файлом firmware.bin:</p>"
    "<form method='post' action='/update' enctype='multipart/form-data'>"
    "<input name='firmware' type='file' accept='.bin' required><button type='submit'>Upload firmware</button></form></section>"
    "<section><h2>Factory reset</h2><p>Erases KNOB Wi-Fi credentials and network/A31 settings.</p>"
    "<form method='post' action='/reset' onsubmit=\"return confirm('Erase KNOB settings?')\">"
    "<button type='submit'>Reset KNOB</button></form></section>"
    "<p><a href='/screenshot.bmp' style='background:#fff'>Screenshot (port 8080)</a></p></html>");
  const String updateUi = String(R"HTML(<section><h2>Онлайн-обновление</h2>
<p>GitHub: keshman74/airKNOB · текущая версия: )HTML") + kFirmwareVersion + R"HTML(</p>
<button id="ota-check" type="button">Проверить обновление</button>
<button id="ota-install" type="button" disabled>Установить обновление</button>
<p id="ota-status"></p><script>
let otaManifest=null;
const otaStatus=document.getElementById('ota-status'),otaInstall=document.getElementById('ota-install');
async function otaFetch(url){const c=new AbortController(),t=setTimeout(()=>c.abort(),90000);try{const r=await fetch(url,{cache:'no-store',signal:c.signal});if(!r.ok)throw Error('HTTP '+r.status);return r;}finally{clearTimeout(t);}}
document.getElementById('ota-check').onclick=async()=>{
 otaManifest=null;otaInstall.disabled=true;otaStatus.textContent='Проверяем GitHub…';
 try{const m=await (await otaFetch('https://raw.githubusercontent.com/keshman74/airKNOB/main/firmware/latest.json?t='+Date.now())).json();
 if(m.board!=='onx2424g013'||!/^\d+\.\d+\.\d+$/.test(m.version)||!(/^[a-f0-9]{32}$/i.test(m.md5))||!Number.isInteger(m.size)||m.size<=0||m.size>8*1024*1024)throw Error('Неверный файл версии');
 const u=new URL(m.url);if(u.origin!=='https://raw.githubusercontent.com'||!u.pathname.startsWith('/keshman74/airKNOB/'))throw Error('Неверный адрес прошивки');
 const current=document.querySelector('meta[name="knob-version"]').content.split('.').map(Number),next=m.version.split('.').map(Number);
 let newer=false;for(let i=0;i<3;i++){if(next[i]!==current[i]){newer=next[i]>current[i];break;}}
 otaManifest=m;otaInstall.disabled=!newer;otaStatus.textContent=newer?'Доступна версия '+m.version:'Новых версий нет (GitHub: '+m.version+')';
 }catch(e){otaStatus.textContent='Обновление недоступно: '+e.message;}
};
otaInstall.onclick=async()=>{
 if(!otaManifest)return;otaInstall.disabled=true;
 try{otaStatus.textContent='Скачиваем версию '+otaManifest.version+'…';const bytes=new Uint8Array(await (await otaFetch(otaManifest.url+'?md5='+otaManifest.md5)).arrayBuffer());
 if(bytes.length!==otaManifest.size||bytes[0]!==0xe9)throw Error('Неверный размер или формат прошивки');
 const form=new FormData();form.append('md5',otaManifest.md5);form.append('firmware',new Blob([bytes]),'firmware.bin');
 otaStatus.textContent='Передаём прошивку в KNOB…';const r=await fetch('/update',{method:'POST',body:form});const result=await r.text();if(!r.ok)throw Error(result);
 otaStatus.textContent='Обновление установлено. KNOB перезагружается; обновите страницу через несколько секунд.';
 }catch(e){otaStatus.textContent='Ошибка обновления: '+e.message;otaInstall.disabled=false;}
};
</script></section>)HTML";
  page.replace("</html>", "<meta name='knob-version' content='" + String(kFirmwareVersion) + "'>" + updateUi + "</html>");
  // Screenshot is served independently on 8080; fix its URL on the page.
  page.replace("href='/screenshot.bmp'", "href='http://" + WiFi.localIP().toString() + ":8080/screenshot.bmp'");
  settingsServer.send(200, "text/html; charset=utf-8", page);
}

bool webScanRequested = false;
void discoverZones(bool keepMenuVisible = false);

void startSettingsWeb() {
  settingsServer.on("/", HTTP_GET, []() { webPage(); });
  settingsServer.on("/zones", HTTP_GET, []() {
    DynamicJsonDocument doc(3072);
    doc["scanning"] = bool(scanPhase || webScanRequested);
    doc["current"] = deviceIp;
    doc["progress"] = scanPhase == 1 ? 0 : min(scanHost - 1, 254);
    JsonArray list = doc.createNestedArray("zones");
    for (int i = 0; i < zoneCount; ++i) {
      JsonObject zone = list.createNestedObject();
      zone["name"] = zones[i].name;
      zone["ip"] = zones[i].ip;
    }
    String body; serializeJson(doc, body);
    settingsServer.send(200, "application/json; charset=utf-8", body);
  });
  settingsServer.on("/zones/scan", HTTP_POST, []() {
    if (!scanPhase) webScanRequested = true;
    settingsServer.send(202, "text/plain", "Searching");
  });
  settingsServer.on("/zones/select", HTTP_POST, []() {
    if (scanPhase || webScanRequested) {
      settingsServer.send(409, "text/plain", "Wait for the search to finish"); return;
    }
    const String ip = settingsServer.arg("ip");
    int found = -1;
    for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip) found = i;
    if (found < 0) { settingsServer.send(400, "text/plain", "Zone not found"); return; }
    deviceIp = zones[found].ip;
    settings.putString("device", deviceIp);
    a31Tcp.stop(); tcpInputLength = 0; lastTcpAttemptAt = 0;
    groupKnown = isSlave = deviceOnline = volumeDirty = false;
    masterIp = groupValue = trackTitle = trackArtist = currentArtworkUrl = "";
    artworkReady = artworkAttempted = false;
    lastPollAt = millis() - kFallbackPollIntervalMs;
    lastGroupPollAt = millis() - kGroupPollMs;
    lastArtworkPollAt = millis() - kArtworkPollMs + 2500;
    settingsServer.send(200, "text/plain; charset=utf-8", "Zone selected and saved");
    if (menuScreen == MenuScreen::Player) drawScreen();
  });
  settingsServer.on("/save", HTTP_POST, []() {
    IPAddress ip, gw, mask, dns, device;
    const bool fixed = settingsServer.hasArg("fixed");
    if (!parseIpField(settingsServer.arg("device"), device)) {
      webPage("Invalid A31 IP; nothing saved"); return;
    }
    if (fixed && (!parseIpField(settingsServer.arg("ip"), ip) ||
                  !parseIpField(settingsServer.arg("gw"), gw) ||
                  !parseIpField(settingsServer.arg("mask"), mask) ||
                  !parseIpField(settingsServer.arg("dns"), dns) ||
                  (ip[0] & mask[0]) != (gw[0] & mask[0]) ||
                  (ip[1] & mask[1]) != (gw[1] & mask[1]) ||
                  (ip[2] & mask[2]) != (gw[2] & mask[2]) ||
                  (ip[3] & mask[3]) != (gw[3] & mask[3]))) {
      webPage("Invalid static network settings; nothing saved"); return;
    }
    settings.putBool("fixed", fixed);
    if (fixed) {
      settings.putString("ip", ip.toString()); settings.putString("gw", gw.toString());
      settings.putString("mask", mask.toString()); settings.putString("dns", dns.toString());
    }
    settings.putString("device", device.toString());
    settingsServer.send(200, "text/plain; charset=utf-8", "Saved. KNOB is restarting; reconnect at the new IP.");
    rebootAfterResponse = true;
  });
  settingsServer.on("/reset", HTTP_POST, []() {
    WiFiManager manager;
    manager.resetSettings();
    settings.clear();
    settingsServer.send(200, "text/plain; charset=utf-8", "KNOB settings erased. Restarting into Wi-Fi setup.");
    rebootAfterResponse = true;
  });
  settingsServer.on("/update", HTTP_POST,
    []() {
      const bool ok = otaUploadFinished && !otaUploadFailed && !Update.hasError();
      settingsServer.send(ok ? 200 : 500, "text/plain; charset=utf-8",
        ok ? "Firmware installed. KNOB is restarting." : "Firmware update failed; existing firmware is retained.");
      if (ok) rebootAfterResponse = true;
    },
    []() {
      HTTPUpload& upload = settingsServer.upload();
      if (upload.status == UPLOAD_FILE_START) {
        otaUploadFinished = false;
        otaUploadFailed = !upload.filename.endsWith(".bin") || !Update.begin(UPDATE_SIZE_UNKNOWN);
        const String expectedMd5 = settingsServer.arg("md5");
        if (!otaUploadFailed && expectedMd5.length()) {
          bool valid = expectedMd5.length() == 32;
          for (size_t i = 0; i < expectedMd5.length(); ++i) valid = valid && isxdigit(static_cast<unsigned char>(expectedMd5[i]));
          if (!valid || !Update.setMD5(expectedMd5.c_str())) { Update.abort(); otaUploadFailed = true; }
        }
      } else if (upload.status == UPLOAD_FILE_WRITE && !otaUploadFailed) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) otaUploadFailed = true;
      } else if (upload.status == UPLOAD_FILE_END && !otaUploadFailed) {
        if (!Update.end(true)) otaUploadFailed = true;
        else otaUploadFinished = true;
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); otaUploadFailed = true;
      }
    });
  settingsServer.begin();
}

void setMenu(MenuScreen screen) {
  if (screen == MenuScreen::Zone && zoneCount == 0) {
    zones[0] = ZoneInfo{deviceIp, "A31", ""};
    zoneCount = 1;
  }
  menuScreen = screen;
  selectedItem = 0;
  menuMessage = "";
  marqueeStartedAt = marqueeLastAt = millis();
  marqueeOffset = 0;
  drawScreen();
}

String normalizedUuid(const String& original) {
  String clean;
  String value = original;
  if (value.startsWith("uuid:")) value.remove(0, 5);
  for (size_t i = 0; i < value.length(); ++i)
    if (isxdigit(static_cast<unsigned char>(value[i])))
      clean += static_cast<char>(toupper(static_cast<unsigned char>(value[i])));
  if (clean.length() != 32) return "";
  return clean.substring(0, 8) + "-" + clean.substring(8, 12) + "-" +
         clean.substring(12, 16) + "-" + clean.substring(16, 20) + "-" +
         clean.substring(20);
}

bool localIp(const String& ipText) {
  IPAddress ip;
  if (!ip.fromString(ipText)) return false;
  const IPAddress ours = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  for (int octet = 0; octet < 4; ++octet)
    if ((ip[octet] & mask[octet]) != (ours[octet] & mask[octet])) return false;
  return true;
}

void rememberZone(const String& ip) {
  if (zoneCount >= 8 || !localIp(ip)) return;
  for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip) return;
  String body;
  if (!requestTo(ip, "getStatusEx", body, 650)) return;
  DynamicJsonDocument info(4096);
  if (deserializeJson(info, body)) return;
  const String hardware = info["hardware"] | "";
  const String project = info["project"] | "";
  // A97/A98 need HTTPS; this build's secure-client is unavailable.
  if (hardware != "A31" && project.indexOf("UP2STREAM_PRO_V4") < 0) return;
  ZoneInfo& zone = zones[zoneCount++];
  zone.ip = ip;
  zone.name = String(info["DeviceName"] | "A31");
  if (zone.name.isEmpty()) zone.name = "A31";
  zone.uuid = normalizedUuid(String(info["uuid"] | ""));
  Serial.printf("Zone discovered: %s at %s\n", zone.name.c_str(), ip.c_str());
}

void discoverZones(bool keepMenuVisible) {
  if (scanPhase == 1) discoveryUdp.stop();
  if (WiFi.status() != WL_CONNECTED) {
    menuMessage = "WI-FI OFF";
    drawScreen();
    return;
  }
  scanReturnScreen = menuScreen == MenuScreen::Multiroom ? MenuScreen::Multiroom :
                     menuScreen == MenuScreen::Join ? MenuScreen::Join : MenuScreen::Zone;
  const String current = deviceIp;
  zoneCount = 0;
  rememberZone(current);
  if (zoneCount == 0) {
    zones[0] = ZoneInfo{current, "A31", ""};
    zoneCount = 1;
  }
  scanInBackground = keepMenuVisible;
  if (!keepMenuVisible) { menuScreen = MenuScreen::Scan; selectedItem = 0; }
  menuMessage = "SEARCHING";
  scanHost = 1;
  scanStartedAt = millis();
  scanPhase = 1;
  if (discoveryUdp.begin(0)) {
    discoveryUdp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    discoveryUdp.print("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                       "MAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n");
    discoveryUdp.endPacket();
  } else {
    scanPhase = 2;
  }
  drawScreen();
}

void pollZoneScan() {
  if (!scanPhase) return;
  if (menuScreen != MenuScreen::Scan &&
      !(scanInBackground && menuScreen == MenuScreen::Zone)) {
    discoveryUdp.stop();
    scanPhase = 0;
    return;
  }
  if (scanPhase == 1) {
    for (int count = 0; count < 5; ++count) {
      const int length = discoveryUdp.parsePacket();
      if (length <= 0) break;
      const String ip = discoveryUdp.remoteIP().toString();
      while (discoveryUdp.available()) discoveryUdp.read();
      rememberZone(ip);
    }
    if (millis() - scanStartedAt < 1400) return;
    discoveryUdp.stop();
    scanPhase = 2;
  }
  if (scanPhase == 2 && scanHost <= 254 && zoneCount < 8) {
    const IPAddress ours = WiFi.localIP();
    const String ipText = IPAddress(ours[0], ours[1], ours[2], scanHost++).toString();
    if (ipText != deviceIp && ipText != ours.toString() && localIp(ipText)) {
      WiFiClient probe;
      IPAddress ip;
      ip.fromString(ipText);
      if (probe.connect(ip, 80, 110)) {
        probe.stop();
        rememberZone(ipText);
      }
    }
    if (scanHost % 8 == 0) {
      if (scanInBackground) menuMessage = String(scanHost - 1) + "/254";
      if (scanInBackground) drawMenu();
      else drawScreen();
    }
    return;
  }
  scanPhase = 0;
  if (!scanInBackground) menuScreen = scanReturnScreen;
  selectedItem = 0;
  menuMessage = String(zoneCount) + " A31 FOUND";
  if (scanInBackground) drawMenu();
  else drawScreen();
}

bool openInformation(MenuScreen screen) {
  String body;
  if (!request("getStatusEx", body)) { menuMessage = "DEVICE OFF"; return false; }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) { menuMessage = "STATUS ERROR"; return false; }
  infoLineCount = 5;
  if (screen == MenuScreen::DeviceInfo) {
    infoLines[0] = "NAME " + String(doc["DeviceName"] | "A31");
    infoLines[1] = "MODEL " + String(doc["hardware"] | "A31");
    infoLines[2] = "FW " + String(doc["firmware"] | "?");
    infoLines[3] = "PROJECT " + String(doc["project"] | "?");
    infoLines[4] = "IP " + deviceIp;
  } else {
    infoLines[0] = "ZONE IP " + deviceIp;
    infoLines[1] = "SSID " + String(doc["ssid"] | "?");
    infoLines[2] = "KNOB " + WiFi.localIP().toString();
    infoLines[3] = "SIGNAL " + String(WiFi.RSSI()) + " dBm";
    infoLines[4] = "GATEWAY " + WiFi.gatewayIP().toString();
  }
  setMenu(screen);
  return true;
}

bool sendA31EqPreset(int index) {
  return mcuCommand("MCU+PAS+RAKOIT:EQS:" + String(index) + "&");
}

void handleTcpMessage(const String& message) {
  if (message.startsWith("AXX+VOL+") && message.length() >= 11) {
    const int n = message.substring(8, 11).toInt();
    if (n >= 0 && n <= 100 && !volumeDirty && volume != n) {
      volume = n;
      if (menuScreen == MenuScreen::Player) drawVolumeUpdate();
    }
    deviceOnline = true;
  } else if (message.startsWith("AXX+MUT+")) {
    muted = message.substring(8, 11) == "001";
  } else if (message.startsWith("AXX+PLY+INF")) {
    const int start = message.indexOf('{');
    const int end = message.lastIndexOf('}');
    if (start < 0 || end <= start) return;
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, message.substring(start, end + 1))) return;
    const String state = doc["status"] | "";
    const bool next = state == "play";
    if (next != playing) {
      playing = next;
      if (menuScreen == MenuScreen::Player) drawScreen();
    }
  } else if (message.startsWith("AXX+PLY+") &&
             (message.substring(8, 11) == "000" || message.substring(8, 11) == "001")) {
    const bool next = message.substring(8, 11) == "001";
    if (next != playing) {
      playing = next;
      if (menuScreen == MenuScreen::Player) drawScreen();
    }
    deviceOnline = true;
  } else if (message.startsWith("AXX+MEA+DAT")) {
    const int start = message.indexOf('{');
    const int end = message.lastIndexOf('}');
    if (start < 0 || end <= start) return;
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, message.substring(start, end + 1))) return;
    const String title = decodeMetadata(doc["title"] | "");
    const String artist = decodeMetadata(doc["artist"] | "");
    if (title != trackTitle || artist != trackArtist) {
      trackTitle = title;
      trackArtist = artist;
      artworkReady = artworkAttempted = false;
      currentArtworkUrl = "";
      lastArtworkPollAt = millis() - kArtworkPollMs;
      marqueeOffset = 0;
      marqueeStartedAt = marqueeLastAt = millis();
      if (menuScreen == MenuScreen::Player) drawScreen();
    }
  }
}

bool ensureA31Tcp() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (a31TcpIp != deviceIp) {
    a31Tcp.stop();
    tcpInputLength = 0;
    a31TcpIp = deviceIp;
    lastTcpAttemptAt = 0;
  }
  if (a31Tcp.connected()) return true;
  if (lastTcpAttemptAt && millis() - lastTcpAttemptAt < 5000) return false;
  lastTcpAttemptAt = millis();
  a31Tcp.stop();
  tcpInputLength = 0;
  a31Tcp.setTimeout(1);
  IPAddress target;
  const bool connected = target.fromString(deviceIp) && a31Tcp.connect(target, 8899, 350);
  if (connected) Serial.println("A31 TCP/8899 connected");
  return connected;
}

void pollA31Tcp(String* reply = nullptr) {
  if (!a31Tcp.connected()) return;
  while (a31Tcp.available() && tcpInputLength < sizeof(tcpInput))
    tcpInput[tcpInputLength++] = uint8_t(a31Tcp.read());
  while (tcpInputLength >= 20) {
    if (tcpInput[0] != 0x18 || tcpInput[1] != 0x96 ||
        tcpInput[2] != 0x18 || tcpInput[3] != 0x20) {
      memmove(tcpInput, tcpInput + 1, --tcpInputLength);
      continue;
    }
    const uint32_t size = uint32_t(tcpInput[4]) | (uint32_t(tcpInput[5]) << 8) |
        (uint32_t(tcpInput[6]) << 16) | (uint32_t(tcpInput[7]) << 24);
    if (size > sizeof(tcpInput) - 20) {
      memmove(tcpInput, tcpInput + 1, --tcpInputLength);
      continue;
    }
    if (tcpInputLength < size + 20) break;
    const uint32_t expected = uint32_t(tcpInput[8]) | (uint32_t(tcpInput[9]) << 8) |
        (uint32_t(tcpInput[10]) << 16) | (uint32_t(tcpInput[11]) << 24);
    uint32_t sum = 0;
    String payload;
    payload.reserve(size);
    for (uint32_t i = 0; i < size; ++i) {
      const uint8_t c = tcpInput[20+i];
      sum += c;
      payload += char(c);
    }
    const size_t consumed = size + 20;
    tcpInputLength -= consumed;
    memmove(tcpInput, tcpInput + consumed, tcpInputLength);
    if (sum != expected) continue;
    if (reply) { *reply += payload; *reply += '\n'; }
    handleTcpMessage(payload);
  }
  if (tcpInputLength == sizeof(tcpInput)) tcpInputLength = 0;
}

// MCU commands use the same 20-byte envelope as EQ presets. Read replies are
// framed too; ignore incomplete or malformed frames rather than guessing values.
bool mcuCommand(const String& payload, String* reply) {
  if (!ensureA31Tcp()) return false;
  pollA31Tcp(); // Discard old replies before issuing a new request.
  if (lastTcpSentAt && millis() - lastTcpSentAt < 220)
    delay(220 - (millis() - lastTcpSentAt));
  uint8_t frame[20] = {0x18, 0x96, 0x18, 0x20};
  uint32_t sum = 0;
  for (size_t i = 0; i < payload.length(); ++i) sum += uint8_t(payload[i]);
  const uint32_t size = payload.length();
  for (int i = 0; i < 4; ++i) {
    frame[4 + i] = uint8_t(size >> (8 * i));
    frame[8 + i] = uint8_t(sum >> (8 * i));
  }
  const bool ok = a31Tcp.write(frame, 20) == 20 &&
                  a31Tcp.write(reinterpret_cast<const uint8_t*>(payload.c_str()), size) == size;
  lastTcpSentAt = millis();
  if (!ok) { a31Tcp.stop(); return false; }
  if (!reply) return true;
  reply->clear();
  const uint32_t start = millis();
  uint32_t lastByteAt = start;
  while (millis() - start < 1200 && a31Tcp.connected()) {
    if (a31Tcp.available()) lastByteAt = millis();
    pollA31Tcp(reply);
    if (!reply->isEmpty() && millis() - lastByteAt > 240) break;
    delay(2);
  }
  return !reply->isEmpty();
}

bool parseTone(const String& reply, const char* key, int minimum, int maximum, int& value) {
  const int start = reply.indexOf(key);
  if (start < 0) return false;
  int pos = start + strlen(key);
  int sign = 1;
  if (pos < int(reply.length()) && (reply[pos] == '-' || reply[pos] == '+')) {
    if (reply[pos] == '-') sign = -1;
    ++pos;
  }
  if (pos >= int(reply.length()) || !isDigit(reply[pos])) return false;
  int number = 0;
  while (pos < int(reply.length()) && isDigit(reply[pos])) {
    number = number * 10 + reply[pos++] - '0';
    if (number > 100) return false;
  }
  number *= sign;
  if (number < minimum || number > maximum) return false;
  value = number;
  return true;
}

bool readTone() {
  String eq, mid;
  const bool eqOk = mcuCommand("MCU+PAS+EQGet&", &eq);
  const bool midOk = mcuCommand("MCU+PAS+RAKOIT:MID&", &mid);
  toneKnown[0] = eqOk && parseTone(eq, "EQ:bass:", 0, 10, toneRaw[0]);
  toneKnown[2] = eqOk && parseTone(eq, "EQ:treble:", 0, 10, toneRaw[2]);
  toneKnown[1] = midOk && parseTone(mid, "MID:", -5, 5, toneRaw[1]);
  return toneKnown[0] && toneKnown[1] && toneKnown[2];
}

bool readVirtualBass() {
  String reply;
  int value = 0;
  virtualBassKnown = mcuCommand("MCU+PAS+RAKOIT:VBS&", &reply) &&
                     parseTone(reply, "VBS:", 0, 1, value);
  if (virtualBassKnown) virtualBassEnabled = value == 1;
  return virtualBassKnown;
}

bool writeVirtualBass() {
  const bool desired = !virtualBassEnabled;
  if (!mcuCommand("MCU+PAS+RAKOIT:VBS:" + String(desired ? 1 : 0) + "&"))
    return false;
  delay(200);
  return readVirtualBass() && virtualBassEnabled == desired;
}

bool saveTone() {
  const String command = editingTone == 1 ?
      "MCU+PAS+RAKOIT:MID:" + String(editingRaw) + "&" :
      "MCU+PAS+EQSet:" + String(editingTone == 0 ? "bass:" : "treble:") +
          String(editingRaw) + "&";
  if (!mcuCommand(command)) return false;
  delay(180);
  String reply;
  const bool fetched = mcuCommand(editingTone == 1 ? "MCU+PAS+RAKOIT:MID&" :
                                 "MCU+PAS+EQGet&", &reply);
  int actual;
  const bool verified = fetched && parseTone(reply, editingTone == 0 ? "EQ:bass:" :
                       editingTone == 1 ? "MID:" : "EQ:treble:",
                       editingTone == 1 ? -5 : 0, editingTone == 1 ? 5 : 10, actual);
  toneKnown[editingTone] = verified;
  if (verified) toneRaw[editingTone] = actual;
  return verified && actual == editingRaw;
}

void chooseMenuItem() {
  String body;
  switch (menuScreen) {
    case MenuScreen::Home: {
      const MenuScreen pages[] = {MenuScreen::Presets, MenuScreen::Source,
          MenuScreen::Multiroom, MenuScreen::Zone, MenuScreen::Settings};
      setMenu(pages[selectedItem]);
      // Opening a menu must be immediate and must not start a blocking /24
      // network scan. Find zones is an explicit action inside both menus.
      return;
    }
    case MenuScreen::Presets:
      {
        // Refresh immediately before acting: a phone may have changed the group.
        if (!refreshGroupStatus()) { menuMessage = "CHECK ZONE"; break; }
        if (isSlave) { menuMessage = "SLAVE: NO PRESET"; break; }
        char command[16];
        snprintf(command, sizeof(command), "MCU+KEY+%03d", selectedItem + 1);
        if (mcuCommand(command) ||
            request("MCUKeyShortClick:" + String(selectedItem + 1), body)) {
        menuScreen = MenuScreen::Player;
        artworkReady = artworkAttempted = false;
        currentArtworkUrl = "";
        // Let A31 change tracks before fetching its artwork; volume stays live.
        lastArtworkPollAt = millis() - kArtworkPollMs + 2500;
        pollDevice();
        drawScreen();
        return;
        }
      }
      menuMessage = "PRESET FAILED";
      break;
    case MenuScreen::Source:
      menuMessage = request("setPlayerCmd:switchmode:" + String(kSourceModes[selectedItem]), body)
                        ? "SOURCE SELECTED" : "SOURCE FAILED";
      if (menuMessage == "SOURCE SELECTED") {
        artworkReady = artworkAttempted = false;
        currentArtworkUrl = "";
        lastArtworkPollAt = millis() - kArtworkPollMs;
      }
      break;
    case MenuScreen::Multiroom:
      if (selectedItem == 0) { setMenu(MenuScreen::Join); return; }
      if (selectedItem == 1) { setMenu(MenuScreen::LeaveConfirm); return; }
      if (selectedItem == 3) { discoverZones(); return; }
      if (request("getStatusEx", body)) {
        DynamicJsonDocument doc(4096);
        if (!deserializeJson(doc, body)) {
          const String master = doc["master_ip"].as<String>();
          const String group = doc["group"].as<String>();
          menuMessage = master.length() && master != "0" ? "SLAVE" :
                        group.length() && group != "0" ? "MASTER" : "FREE";
        } else menuMessage = "STATUS ERROR";
      } else menuMessage = "STATUS ERROR";
      break;
    case MenuScreen::Join: {
      const int target = joinZoneIndex(selectedItem);
      if (target < 0) { discoverZones(); break; }
      int master = -1;
      for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == deviceIp) master = i;
      String masterBody, peerBody;
      DynamicJsonDocument masterInfo(3072);
      DynamicJsonDocument peerInfo(3072);
      if (master < 0 || !request("getStatusEx", masterBody) ||
          deserializeJson(masterInfo, masterBody)) {
        menuMessage = "MASTER OFFLINE";
        break;
      }
      zones[master].uuid = normalizedUuid(String(masterInfo["uuid"] | ""));
      if (zones[master].uuid.isEmpty()) {
        menuMessage = "MASTER UUID MISSING";
        break;
      }
      const String masterIp = masterInfo["master_ip"].as<String>();
      if (masterIp.length() && masterIp != "0") { menuMessage = "SELECT MASTER"; break; }
      if (!requestTo(zones[target].ip, "getStatusEx", peerBody) ||
          deserializeJson(peerInfo, peerBody)) {
        menuMessage = "PEER OFFLINE";
        break;
      }
      const String peerMasterIp = peerInfo["master_ip"].as<String>();
      const String peerGroup = peerInfo["group"].as<String>();
      if ((peerMasterIp.length() && peerMasterIp != "0") ||
          (peerGroup.length() && peerGroup != "0")) {
        menuMessage = "PEER GROUPED";
        break;
      }
      const String command = "multiroom:JoinGroup:IP=" + deviceIp +
          ":uuid=" + zones[master].uuid;
      menuMessage = requestTo(zones[target].ip, command, body, 5000)
                        ? "GROUPED" : "JOIN FAILED";
      break;
    }
    case MenuScreen::Zone:
      if (selectedItem >= zoneCount) { discoverZones(); break; }
      deviceIp = zones[selectedItem].ip;
      groupKnown = false;
      isSlave = false;
      masterIp = "";
      groupValue = "";
      lastGroupPollAt = millis() - kGroupPollMs;
      volumeDirty = false;
      artworkReady = artworkAttempted = false;
      currentArtworkUrl = trackTitle = trackArtist = "";
      lastArtworkPollAt = millis() - kArtworkPollMs;
      setMenu(MenuScreen::Player);
      pollDevice();
      drawScreen();
      return;
    case MenuScreen::Settings:
      if (selectedItem == 0) {
        const String command = muted ? "MCU+MUT+000" : "MCU+MUT+001";
        menuMessage = (mcuCommand(command) ||
            request("setPlayerCmd:mute:" + String(muted ? 0 : 1), body))
                          ? "MUTE UPDATED" : "MUTE FAILED";
        if (menuMessage == "MUTE UPDATED") muted = !muted;
      } else if (selectedItem == 1) { setMenu(MenuScreen::EqPresets); return; }
      else if (selectedItem == 2) {
        const bool ok = readTone();
        const bool bassOk = readVirtualBass();
        setMenu(MenuScreen::ToneList);
        if (!ok || !bassOk) { menuMessage = "READ FAILED"; drawMenu(); }
        return;
      } else if (selectedItem == 3 || selectedItem == 4) {
        if (openInformation(selectedItem == 3 ? MenuScreen::DeviceInfo :
                            MenuScreen::NetworkInfo)) return;
      } else if (selectedItem == 5) {
        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t heapFree = ESP.getFreeHeap();
        infoLines[0] = "RAM TOTAL " + String(heapTotal / 1024) + " KB";
        infoLines[1] = "RAM USED " + String((heapTotal - heapFree) / 1024) + " KB";
        infoLines[2] = "RAM FREE " + String(heapFree / 1024) + " KB";
        infoLines[3] = "FIRMWARE " + String(ESP.getSketchSize() / 1024) + " KB";
        infoLines[4] = "IP: " + WiFi.localIP().toString();
        infoLineCount = 5;
        setMenu(MenuScreen::MemoryInfo);
        return;
      } else { discoverZones(); return; }
      break;
    case MenuScreen::ToneList:
      if (selectedItem == 4) {
        const bool toneOk = readTone();
        const bool bassOk = readVirtualBass();
        menuMessage = toneOk && bassOk ? "UPDATED" : "READ FAILED";
        break;
      }
      if (selectedItem == 3) {
        if (!virtualBassKnown) menuMessage = "REFRESH FIRST";
        else menuMessage = writeVirtualBass() ? "CONFIRMED" : "SAVE FAILED";
        break;
      }
      if (!toneKnown[selectedItem]) {
        menuMessage = "REFRESH FIRST";
        break;
      }
      editingTone = selectedItem;
      editingRaw = toneRaw[editingTone];
      toneMessage = "";
      setMenu(MenuScreen::ToneEdit);
      return;
    case MenuScreen::ToneEdit:
      if (saveTone()) {
        const int previous = editingTone;
        setMenu(MenuScreen::ToneList);
        selectedItem = previous;
        menuMessage = "SAVED";
        drawMenu();
      } else {
        toneMessage = "SAVE FAILED";
        drawScreen();
      }
      return;
    case MenuScreen::EqPresets:
      menuMessage = sendA31EqPreset(selectedItem) ? "EQ SENT" : "EQ FAILED";
      break;
    case MenuScreen::LeaveConfirm:
      if (selectedItem == 1) {
        menuMessage = requestTo(deviceIp, "multiroom:LeaveGroup", body, 5000)
                          ? "LEFT GROUP" : "LEAVE FAILED";
        if (menuMessage == "LEFT GROUP") {
          groupKnown = false;
          isSlave = false;
          // Let the A31 finish leaving before reading its new role.
          lastGroupPollAt = millis() - kGroupPollMs + 1500;
        }
      } else menuMessage = "CANCELLED";
      menuScreen = MenuScreen::Multiroom;
      selectedItem = 1;
      break;
    default: return;
  }
  drawMenu();
}

void recordInteraction() {
  lastInteractionAt = millis();
  if (displayDimmed) {
    display.setBrightness(kNormalBrightness);
    displayDimmed = false;
  }
}

void updateDisplaySleep() {
  if (!displayDimmed && millis() - lastInteractionAt >= kDimAfterMs) {
    display.setBrightness(kDimBrightness);
    displayDimmed = true;
  }
}

void pollEncoder() {
  // Four valid quadrature transitions are one physical step (resolution 4).
  // Gray-code lookup rejects invalid jumps caused by contact bounce.
  static constexpr int8_t transitions[16] = {
      0, -1, +1, 0,
      +1, 0, 0, -1,
      -1, 0, 0, +1,
      0, +1, -1, 0};
  const uint8_t now = (digitalRead(kEncoderA) << 1) | digitalRead(kEncoderB);
  if (now == encoderPrevious) return;
  recordInteraction();
  encoderAccumulator += transitions[(encoderPrevious << 2) | now];
  encoderPrevious = now;
  if (encoderAccumulator >= 4 || encoderAccumulator <= -4) {
    const int direction = encoderAccumulator > 0 ? 1 : -1;
    encoderAccumulator = 0;
    if (menuScreen == MenuScreen::Scan) return;
    if (menuScreen == MenuScreen::ToneEdit) {
      editingRaw = constrain(editingRaw + direction, editingTone == 1 ? -5 : 0,
                             editingTone == 1 ? 5 : 10);
      toneMessage = "";
      drawScreen();
      return;
    }
    if (menuScreen == MenuScreen::DeviceInfo || menuScreen == MenuScreen::NetworkInfo ||
        menuScreen == MenuScreen::MemoryInfo) {
      selectedItem = constrain(selectedItem + direction, 0, infoLineCount - 1);
      marqueeStartedAt = marqueeLastAt = millis();
      marqueeOffset = 0;
      drawInformation();
      return;
    }
    if (menuScreen != MenuScreen::Player) {
      selectedItem = constrain(selectedItem + direction, 0, menuCount() - 1);
      menuMessage = "";
      marqueeStartedAt = millis();
      marqueeLastAt = marqueeStartedAt;
      marqueeOffset = 0;
      drawMenu();
      return;
    }
    const int next = constrain(volume + direction * kVolumePerDetent, 0, 100);
    if (next != volume) {
      volume = next;
      volumeDirty = true;
      volumeFailures = 0;
      volumeRetryAt = 0;
      volumeChangedAt = millis();
      Serial.printf("Volume test: %d%%\n", volume);
      drawVolumeUpdate();
    }
  }
}

void pollButton() {
  const bool raw = digitalRead(kButton);
  if (raw != buttonRawPrevious) {
    buttonChangedAt = millis();
    recordInteraction();
  }
  buttonRawPrevious = raw;
  if (raw != buttonStable && millis() - buttonChangedAt >= 30) {
    buttonStable = raw;
    if (buttonStable == LOW) {
      pressedAt = millis();
      longPressHandled = false;
    } else if (!longPressHandled) {
      String body;
      if (menuScreen == MenuScreen::Scan) {
        discoveryUdp.stop();
        scanPhase = 0;
        setMenu(scanReturnScreen);
      } else if (menuScreen == MenuScreen::DeviceInfo || menuScreen == MenuScreen::NetworkInfo ||
                 menuScreen == MenuScreen::MemoryInfo) {
        setMenu(MenuScreen::Settings);
      } else if (menuScreen != MenuScreen::Player) {
        chooseMenuItem();
      } else {
        const String command = playing ? "setPlayerCmd:pause" : "setPlayerCmd:resume";
        const String tcpCommand = playing ? "MCU+PLY-PUS" : "MCU+PLY-PLA";
        if (mcuCommand(tcpCommand) || request(command, body)) {
          playing = !playing;
          deviceOnline = true;
          Serial.println(playing ? "A31 PLAY" : "A31 PAUSE");
        } else {
          deviceOnline = false;
          Serial.println("A31 play/pause failed");
        }
      }
      if (menuScreen == MenuScreen::Player) drawScreen();
    }
  }
  // A slow network discovery can delay polling after the physical release.
  // Only an actually held button may trigger the long-press back action.
  if (raw == LOW && buttonStable == LOW && !longPressHandled &&
      millis() - pressedAt >= kLongPressMs) {
    longPressHandled = true;
    if (menuScreen == MenuScreen::Scan) {
      discoveryUdp.stop();
      scanPhase = 0;
      setMenu(scanReturnScreen);
    } else if (menuScreen == MenuScreen::DeviceInfo || menuScreen == MenuScreen::NetworkInfo ||
               menuScreen == MenuScreen::MemoryInfo)
      setMenu(MenuScreen::Settings);
    else if (menuScreen == MenuScreen::Player) setMenu(MenuScreen::Home);
    else if (menuScreen == MenuScreen::Home) setMenu(MenuScreen::Player);
    else if (menuScreen == MenuScreen::Join || menuScreen == MenuScreen::LeaveConfirm)
      setMenu(MenuScreen::Multiroom);
    else if (menuScreen == MenuScreen::EqPresets) setMenu(MenuScreen::Settings);
    else if (menuScreen == MenuScreen::ToneEdit) {
      const int previous = editingTone;
      setMenu(MenuScreen::ToneList);
      selectedItem = previous;
      drawMenu();
    }
    else if (menuScreen == MenuScreen::ToneList) setMenu(MenuScreen::Settings);
    else setMenu(MenuScreen::Home);
    encoderAccumulator = 0;
    marqueeStartedAt = millis();
    marqueeLastAt = marqueeStartedAt;
    marqueeOffset = 0;
    Serial.println(menuScreen == MenuScreen::Player ? "Volume mode" : "Menu");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kEncoderA, INPUT_PULLUP);
  pinMode(kEncoderB, INPUT_PULLUP);
  pinMode(kButton, INPUT_PULLUP);
  encoderPrevious = (digitalRead(kEncoderA) << 1) | digitalRead(kEncoderB);
  buttonRawPrevious = buttonStable = digitalRead(kButton);
  display.init();
  display.setBrightness(kNormalBrightness);
  lastInteractionAt = millis();
  drawScreen();
  settings.begin("aircontrol", false);
  // Recovery if a static IP makes the web page unreachable: hold the encoder
  // button for five seconds while powering up the KNOB.
  if (digitalRead(kButton) == LOW) {
    drawWindow("HOLD 5S TO RESET", 35, 207, 17, 1, 0, TFT_WHITE, TFT_BLACK);
    const uint32_t resetStarted = millis();
    while (digitalRead(kButton) == LOW && millis() - resetStarted < 5000) delay(20);
    if (digitalRead(kButton) == LOW) {
      WiFiManager resetManager;
      resetManager.resetSettings();
      settings.clear();
      display.fillScreen(TFT_BLACK);
      drawWindow("RESET COMPLETE", 40, 110, 16, 1, 0, TFT_WHITE, TFT_BLACK);
      delay(500);
      ESP.restart();
    }
  }
  deviceIp = settings.getString("device", "192.168.0.2");
  WiFi.mode(WIFI_STA);
  if (settings.getBool("fixed", false)) {
    IPAddress ip, gw, mask, dns;
    if (parseIpField(settings.getString("ip", ""), ip) &&
        parseIpField(settings.getString("gw", ""), gw) &&
        parseIpField(settings.getString("mask", ""), mask) &&
        parseIpField(settings.getString("dns", ""), dns)) {
      if (!WiFi.config(ip, gw, mask, dns)) Serial.println("Static IP failed; Wi-Fi setup may be required");
    }
  }
  WiFiManager manager;
  manager.setConfigPortalTimeout(180);
  drawWindow("Wi-Fi setup", 60, 207, 12, 1, 0, TFT_WHITE, TFT_BLACK);
  const bool connected = manager.autoConnect("airControl-KNOB-Setup");
  if (connected) {
    Serial.print("Wi-Fi IP: ");
    Serial.println(WiFi.localIP());
    screenshotServer.begin();
    startSettingsWeb();
    // Keep the settings page and button responsive while the A31 is unreachable.
    lastPollAt = millis() - kFallbackPollIntervalMs;
    lastGroupPollAt = millis() - kGroupPollMs;
    lastArtworkPollAt = millis();
  } else {
    Serial.println("Wi-Fi setup timeout; reboot to try again");
  }
  drawScreen();
  Serial.println("airControl KNOB v0.7.3: GitHub updates");
  Serial.printf("PSRAM: %s, free=%u bytes\n", psramFound() ? "available" : "not detected",
                static_cast<unsigned>(ESP.getFreePsram()));
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) settingsServer.handleClient();
  if (rebootAfterResponse) { delay(250); ESP.restart(); }
  if (WiFi.status() == WL_CONNECTED) handleScreenshot();
  pollEncoder();
  pollButton();
  if (WiFi.status() == WL_CONNECTED) {
    if (ensureA31Tcp()) pollA31Tcp();
  }
  updateDisplaySleep();
  if (webScanRequested) {
    webScanRequested = false;
    discoverZones();
  }
  if (scanPhase) {
    pollZoneScan();
    if (menuScreen == MenuScreen::Scan) { delay(1); return; }
  }
  if (millis() - marqueeStartedAt >= 1400 &&
      millis() - marqueeLastAt >= kMarqueeStepMs) {
    const bool infoPage = menuScreen == MenuScreen::DeviceInfo ||
                          menuScreen == MenuScreen::NetworkInfo ||
                          menuScreen == MenuScreen::MemoryInfo;
    const int marqueeLength = infoPage ? codepointCount(infoLines[selectedItem]) :
         menuScreen == MenuScreen::Player ?
         max(codepointCount(trackTitle), codepointCount(trackArtist)) :
         codepointCount(menuLabel(selectedItem));
    const int visible = infoPage ? 18 : menuScreen == MenuScreen::Player ? 12 : 9;
    const bool menuOverflow = !infoPage && menuScreen != MenuScreen::Player &&
                              sansMenuWidth(menuLabel(selectedItem), true) > 176;
    if (marqueeLength > visible || menuOverflow) {
      marqueeOffset = marqueeOffset >= marqueeLength + 2 ? 0 : marqueeOffset + 1;
      marqueeLastAt = millis();
      if (marqueeOffset == 0) marqueeStartedAt = millis();
      if (infoPage)
        drawWindow(infoLines[selectedItem], 30, 59 + selectedItem * 29, 18, 1,
                   marqueeOffset, TFT_WHITE, TFT_BLACK);
      else if (menuScreen != MenuScreen::Player)
        drawSansMenu(menuLabel(selectedItem), 32, 99, 176, 26, true,
                     marqueeOffset, TFT_WHITE, display.color565(61, 61, 61));
      else drawMetadata();
    }
  }
  sendVolumeWhenReady();
  if (WiFi.status() == WL_CONNECTED && !volumeDirty &&
      millis() - lastGroupPollAt >= (groupKnown && isSlave ? kSlavePollMs : kGroupPollMs))
    refreshGroupStatus();
  if (WiFi.status() == WL_CONNECTED && !volumeDirty &&
      millis() - lastA31ActivityAt >= 800 &&
      millis() - lastArtworkPollAt >= kArtworkPollMs) {
    lastArtworkPollAt = millis();
    pollArtwork();
  }
  if (!volumeDirty && millis() - lastA31ActivityAt >= 800 &&
      millis() - lastPollAt >= (a31Tcp.connected() ? kPollIntervalMs : kFallbackPollIntervalMs)) {
    lastPollAt = millis();
    pollDevice();
  }
  delay(1);
}
