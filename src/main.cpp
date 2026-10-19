#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <math.h>
#include <PNGdec.h>
#include "artwork_thumbnail.h"
#include "preset_resume.h"
#include "preset_selection.h"
#include "a98_presets.h"
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "discovery_scheduler.h"
#include "media_state.h"
#include "utf8_mono_font.h"
#include "sans_menu_font.h"

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
constexpr uint32_t kVolumeSendDelayMs = 80;
uint32_t lastVolumeSentAt = 0;
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
constexpr char kFirmwareVersion[] = "0.8.16";

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

// Render into a small reusable RGB565 band. The panel has no readback wiring;
// screenshots regenerate the same scene one band at a time.
class DisplayRouter {
  KnobDisplay lcd_;
  lgfx::LGFX_Sprite sprite_{&lcd_};
  bool capturing_ = false;
  int captureTop_=0;
 public:
  static constexpr int kCaptureRows=16;
  void init() { lcd_.init(); sprite_.setColorDepth(16); }
  void setBrightness(uint8_t level) { lcd_.setBrightness(level); }
  bool beginCapture(int top=0) {
    if (!sprite_.createSprite(240, kCaptureRows)) return false;
    captureTop_=top;capturing_ = true;
    return true;
  }
  void setCaptureTop(int top) { captureTop_=top; }
  int clipTop() const {return capturing_ ? captureTop_ : 0;}
  int clipBottom() const {return capturing_ ? captureTop_+kCaptureRows : 240;}
  bool isCapturing() const { return capturing_; }
  void presentCapture() { if (capturing_) sprite_.pushSprite(0, captureTop_); }
  void endCapture() { capturing_ = false; sprite_.deleteSprite(); }
  uint16_t capturePixel(int x, int y) { return sprite_.readPixel(x, y-captureTop_); }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return lcd_.color565(r, g, b);
  }
  template<typename C> void fillScreen(C c) { if (capturing_) sprite_.fillScreen(c); else lcd_.fillScreen(c); }
  template<typename C> void fillRect(int x, int y, int w, int h, C c) {
    if (capturing_) sprite_.fillRect(x,y-captureTop_,w,h,c); else lcd_.fillRect(x,y,w,h,c);
  }
  template<typename C> void fillRoundRect(int x, int y, int w, int h, int r, C c) {
    if (capturing_) sprite_.fillRoundRect(x,y-captureTop_,w,h,r,c);
    else lcd_.fillRoundRect(x,y,w,h,r,c);
  }
  template<typename C> void fillCircle(int x, int y, int r, C c) {
    if (capturing_) sprite_.fillCircle(x,y-captureTop_,r,c); else lcd_.fillCircle(x,y,r,c);
  }
  template<typename C> void drawLine(int x1, int y1, int x2, int y2, C c) {
    if (capturing_) sprite_.drawLine(x1,y1-captureTop_,x2,y2-captureTop_,c);
    else lcd_.drawLine(x1,y1,x2,y2,c);
  }
  template<typename C> void drawPixel(int x, int y, C c) {
    if (capturing_) sprite_.drawPixel(x,y-captureTop_,c); else lcd_.drawPixel(x,y,c);
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
    if (capturing_) sprite_.pushImage(x,y-captureTop_,w,h,pixels);
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
bool otaActive = false;
size_t otaExpectedBytes = 0, otaWrittenBytes = 0;
uint32_t otaDrawAt = 0;
int volume = kInitialVolume;
bool playing = false;
PresetResume presetResume;
String presetResumeZone;
String a98Zone;
uint32_t a98ArtworkEpoch=0,a98IncomingSeq=0;
void resetA98State();
void pollA98Events();
volatile uint8_t encoderPrevious = 0;
volatile int encoderAccumulator = 0;
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int8_t encoderSteps[128];
volatile uint8_t encoderRead = 0, encoderWrite = 0;
void IRAM_ATTR captureEncoder() {
  static DRAM_ATTR const int8_t transitions[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  const uint8_t now = (digitalRead(kEncoderA) << 1) | digitalRead(kEncoderB);
  portENTER_CRITICAL_ISR(&encoderMux);
  encoderAccumulator += transitions[(encoderPrevious << 2) | now];
  encoderPrevious = now;
  if (encoderAccumulator >= 4 || encoderAccumulator <= -4) {
    const uint8_t next = (encoderWrite + 1) & 127;
    if (next != encoderRead) { encoderSteps[encoderWrite] = encoderAccumulator > 0 ? 1 : -1; encoderWrite = next; }
    encoderAccumulator = 0;
  }
  portEXIT_CRITICAL_ISR(&encoderMux);
}
uint32_t feedbackUntil = 0;
int feedbackKind = 0; // 1 play, 2 pause, 3 preset selected, 4 checking selection
void showFeedback(int kind) { feedbackKind = kind; feedbackUntil = millis() + 2000; }

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
                                  DeviceInfo, NetworkInfo, MemoryInfo, Scan, EqBands, EqBandEdit };
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
enum class Chip : uint8_t { Unknown, A31, A97, A98, A33, Linkplay };
struct Endpoint { Chip chip = Chip::Unknown; uint16_t port = 80; bool tls = false; };
Endpoint activeEndpoint;
struct ZoneInfo { String ip; String name; String uuid; Endpoint endpoint; };
constexpr int kMaxZones = 20;
ZoneInfo zones[kMaxZones];
WiFiUDP cloudDiscoveryUdp;
uint8_t scanPortIndex = 0;
String reportedArtworkUrl;
String currentMediaUri;
String discoveredSourceNames[12];
String discoveredSourceModes[12];
int discoveredSourceCount = 0;
bool sourceCapabilitiesKnown = false;
const char* const kBandNames[] = {"31 Hz", "63 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};
const char* const kBandParams[] = {"band31hz", "band63hz", "band125hz", "band250hz", "band500hz", "band1khz", "band2khz", "band4khz", "band8khz", "band16khz"};
int graphicBands[10] = {};
bool graphicKnown = false;
bool graphicEnabled = false;
int editingBand = 0;
int editingBandValue = 0;

const char* chipName(Chip chip) {
  switch (chip) { case Chip::A31: return "A31"; case Chip::A97: return "A97";
    case Chip::A98: return "A98"; case Chip::A33: return "A33"; case Chip::Linkplay: return "LINKPLAY"; default: return "UNKNOWN"; }
}
bool isCloud() { return activeEndpoint.chip == Chip::A33; }
void applyEndpoint(const Endpoint& endpoint) {
  if(a98Zone.length() && (a98Zone!=deviceIp || endpoint.chip!=Chip::A98)) resetA98State();
  presetResume.cancel();
  activeEndpoint = endpoint;
  graphicKnown = false;
  a31Tcp.stop(); tcpInputLength = 0; lastTcpAttemptAt = 0; a31TcpIp = "";
  discoveredSourceCount = 0; sourceCapabilitiesKnown = false; reportedArtworkUrl = ""; currentMediaUri="";
  for (int i = 0; i < 3; ++i) toneKnown[i] = false;
  virtualBassKnown = false;
}
Chip classifyChip(JsonVariantConst info) {
  String platform = String(info["hardware"] | "") + " " + String(info["project"] | "");
  platform.toUpperCase();
  if (platform.indexOf("A31") >= 0 || platform.indexOf("UP2STREAM_PRO_V4") >= 0) return Chip::A31;
  if (platform.indexOf("A97") >= 0 || platform.indexOf("ALLWINNER") >= 0 || platform.indexOf("R328") >= 0) return Chip::A97;
  if (platform.indexOf("A98") >= 0 || platform.indexOf("AMLOGIC") >= 0 || platform.indexOf("A113") >= 0) return Chip::A98;
  if (!info["DevVolumeL"].isNull() || !info["ProjectName"].isNull() || !info["UserDevName"].isNull()) return Chip::A33;
  return Chip::Unknown;
}
String encodeCommand(const String& value) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':' || c == '/') out += char(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}
bool readEndpoint(const String& ip, const Endpoint& endpoint, const String& command,
                  String& response, uint32_t timeoutMs = 1200, int* responseStatus = nullptr) {
  if(responseStatus) *responseStatus=0;
  IPAddress address;
  if (WiFi.status() != WL_CONNECTED || !address.fromString(ip)) return false;
  WiFiClient plain;
  WiFiClientSecure secure;
  // Local players use self-signed TLS certificates, as in desktop airControl.
  if (endpoint.tls) { secure.setInsecure(); secure.setHandshakeTimeout(2); }
  HTTPClient http;
  http.setConnectTimeout(350); http.setTimeout(timeoutMs);
  const String path = endpoint.chip == Chip::A33 ? "/?Instruct=" : "/httpapi.asp?command=";
  const String url = String(endpoint.tls ? "https://" : "http://") + ip + ":" + String(endpoint.port) + path + encodeCommand(command);
  if (!(endpoint.tls ? http.begin(secure, url) : http.begin(plain, url))) return false;
  const int status = http.GET();
  if(responseStatus) *responseStatus=status;
  response = status == 200 ? http.getString() : "";
  http.end();
  if (endpoint.chip == Chip::A33) {
    String clean; clean.reserve(response.length());
    for (size_t i = 0; i < response.length(); ++i) {
      const uint8_t c = response[i]; if (c >= 32 || c == '\n' || c == '\r' || c == '\t') clean += char(c);
    }
    if (clean.startsWith("\xEF\xBB\xBF")) clean.remove(0, 3);
    response = clean;
  }
  return status == 200 && response.length() && response.indexOf("unknown comman") < 0;
}
bool looksLikeLinkplayStatus(JsonVariantConst info) {
  return !info["uuid"].isNull() && (!info["DeviceName"].isNull() || !info["project"].isNull() || !info["hardware"].isNull());
}
bool discoveryEpochValid(uint32_t generation);
bool detectEndpoint(const String& ip, Endpoint& endpoint, String& statusBody,
                    uint16_t preferredPort = 0, uint32_t statusTimeoutMs = 1600, uint32_t generation = 0) {
  uint16_t ports[] = {80, 8000, 443, 8443};
  IPAddress address; if (!address.fromString(ip)) return false;
  if (preferredPort) for (int i = 0; i < 4; ++i) if (ports[i] == preferredPort) {
    const uint16_t first = ports[0]; ports[0] = ports[i]; ports[i] = first; break;
  }
  for (int i = 0; i < 4; ++i) {
    Endpoint candidate; candidate.port = ports[i];
    candidate.tls = ports[i] == 443 || ports[i] == 8443;
    const bool cloudPort = ports[i] == 8000 || ports[i] == 8443;
    candidate.chip = cloudPort ? Chip::A33 : Chip::Unknown;
    // GET itself opens the connection. No extra connect/close gate that can
    // reject a responding A31 or leave its HTTP server cleaning up sockets.
    for (int attempt = 0; attempt < (ports[i] == 80 ? 2 : 1); ++attempt) {
      if (generation && !discoveryEpochValid(generation)) return false;
      String body;
      if (!readEndpoint(ip, candidate, "getStatusEx", body, statusTimeoutMs)) {
        if (ports[i] == 80 && attempt == 0) delay(40);
        continue;
      }
      DynamicJsonDocument doc(12288);
      if (deserializeJson(doc, body)) { Serial.printf("Zone %s:%u invalid status JSON\n",ip.c_str(),ports[i]); continue; }
      Chip detected = classifyChip(doc.as<JsonVariantConst>());
      if (detected == Chip::Unknown && !cloudPort && looksLikeLinkplayStatus(doc.as<JsonVariantConst>())) detected = Chip::Linkplay;
      if (detected == Chip::Unknown || (detected == Chip::A33) != cloudPort) {
        Serial.printf("Zone %s:%u unsupported status profile\n",ip.c_str(),ports[i]); break;
      }
      candidate.chip = detected;
      endpoint = candidate; statusBody = body; return true;
    }
  }
  return false;
}

int zoneCount = 0;
WiFiUDP discoveryUdp;
WiFiUDP ssdpNotifyUdp;
MenuScreen scanReturnScreen = MenuScreen::Zone;
uint32_t scanStartedAt = 0;
uint8_t scanPhase = 0;
bool scanInBackground = false;
int scanHost = 1;
String infoLines[7];
int infoLineCount = 0;
bool muted = false;
struct DevicePreset { int slot=0; String name; };
DevicePreset devicePresets[20];
int devicePresetCount=0;
String presetZone;
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
uint16_t nextArtPixels[kArtSize * kArtSize] = {};
String lastArtAttemptUrl;
uint32_t lastArtAttemptAt = 0;
int sourceWidth = 0;
int sourceHeight = 0;
bool localVolumePending() {
  return volumeDirty || (menuScreen == MenuScreen::Player && encoderRead != encoderWrite) ||
         (volumeChangedAt && millis() - volumeChangedAt < 500);
}
void drawScreen();
void drawVolumeUpdate();
void drawMenu();
void drawMetadata();
PNG pngDecoder;
JPEGDEC jpegDecoder;

void invalidateArtwork() {
  artworkReady=artworkAttempted=false;
  currentArtworkUrl=reportedArtworkUrl=lastArtAttemptUrl="";
  lastArtAttemptAt=0;
}
bool updateTrackMetadata(const String& title,const String& artist,const String& uri) {
  const bool identityChanged=mediaIdentityChanged(currentMediaUri.c_str(),uri.c_str(),trackTitle.c_str(),title.c_str());
  if(identityChanged) invalidateArtwork();
  if(uri.length()) currentMediaUri=uri;
  const bool changed=identityChanged || title!=trackTitle || artist!=trackArtist;
  trackTitle=title;trackArtist=artist;
  if(changed) {marqueeOffset=0;marqueeStartedAt=marqueeLastAt=millis();lastArtworkPollAt=millis()-kArtworkPollMs;}
  return changed;
}
// Destination-driven sampling fills every output pixel, including when a
// small source must be enlarged. Decoder callbacks may arrive in MCU blocks.
int pngLine(PNGDRAW* line) {
  if (!line || sourceWidth < 1 || sourceHeight < 1 || line->iWidth > 2048) return 0;
  static uint16_t pixels[2048];
  pngDecoder.getLineAsRGB565(line, pixels, PNG_RGB565_LITTLE_ENDIAN, 0);
  const int side = max(sourceWidth, sourceHeight);
  const int targetW = max(1, sourceWidth * kArtSize / side);
  const int targetH = max(1, sourceHeight * kArtSize / side);
  for (int dy=0; dy<targetH; ++dy) {
    if (dy * sourceHeight / targetH != line->y) continue;
    for (int dx=0; dx<targetW; ++dx) {
      const int sx=dx * sourceWidth / targetW;
      if(sx<line->iWidth)
        nextArtPixels[((kArtSize-targetH)/2+dy)*kArtSize+(kArtSize-targetW)/2+dx]=pixels[sx];
    }
  }
  return 1;
}

int jpegBlock(JPEGDRAW* block) {
  if (!block || sourceWidth < 1 || sourceHeight < 1) return 0;
  const int side = max(sourceWidth, sourceHeight);
  const int targetW = max(1, sourceWidth * kArtSize / side);
  const int targetH = max(1, sourceHeight * kArtSize / side);
  const int validW=min(block->iWidth,sourceWidth-block->x);
  for (int dy=0; dy<targetH; ++dy) {
    const int sy=dy * sourceHeight / targetH;
    if(sy<block->y || sy>=block->y+block->iHeight) continue;
    for (int dx=0; dx<targetW; ++dx) {
      const int sx=dx * sourceWidth / targetW;
      if(sx<block->x || sx>=block->x+validW) continue;
      nextArtPixels[((kArtSize-targetH)/2+dy)*kArtSize+(kArtSize-targetW)/2+dx]=
          block->pPixels[(sy-block->y)*block->iWidth+sx-block->x];
    }
  }
  return 1;
}

String decodeXml(String xml) {
  for (int round=0; round<3; ++round) {
  xml.replace("&lt;", "<");
  xml.replace("&gt;", ">");
  xml.replace("&quot;", "\"");
  xml.replace("&apos;", "'");
  xml.replace("&amp;", "&");
  }
  return xml;
}

String xmlValue(const String& xml, const char* localName) {
  int cursor=0;
  while ((cursor=xml.indexOf('<',cursor))>=0) {
    const int start=++cursor;
    if(xml[start]=='/' || xml[start]=='!' || xml[start]=='?') continue;
    int end=start;
    while(end<int(xml.length()) && xml[end]!='>' && xml[end]!=' ' && xml[end]!='/' && xml[end]!='\t' && xml[end]!='\r' && xml[end]!='\n') ++end;
    const String name=xml.substring(start,end);
    const int colon=name.lastIndexOf(':');
    if(name.substring(colon+1)!=localName) continue;
    const int open=xml.indexOf('>',end),close=xml.indexOf("</"+name+">",open+1);
    if(open<0 || close<0) return "";
    String value=xml.substring(open+1,close);value.trim();
    if(value.startsWith("<![CDATA[") && value.endsWith("]]>")) value=value.substring(9,value.length()-3);
    return value;
  }
  return "";
}

bool request(const String& command, String& response);
String cleanPresetName(String name) {
  name=decodeXml(name);name.trim();
  const int suffix=name.lastIndexOf('_');
  if(suffix>=0 && name.substring(suffix).startsWith("_#~")) {
    const String stamp=name.substring(suffix+3);
    if(stamp.length()==19 && stamp[4]=='-' && stamp[7]=='-' && stamp[10]==' ' && stamp[13]==':' && stamp[16]==':') name=name.substring(0,suffix);
  }
  name.trim();return name;
}
bool parseKeyPresets(const String& response) {
  devicePresetCount=0;
  const String context=decodeXml(xmlValue(response,"QueueContext"));
  if(context.indexOf("<KeyList")<0 || context.indexOf("</KeyList>")<0) return false;
  int maximum=xmlValue(context,"MaxNumber").toInt();
  if(maximum<1 || maximum>20) return false;
  for(int slot=1;slot<=maximum;++slot) {
    char key[8];snprintf(key,sizeof(key),"Key%d",slot);
    const String entry=decodeXml(xmlValue(context,key));
    const String name=cleanPresetName(xmlValue(entry,"Name"));
    if(name.length()) {devicePresets[devicePresetCount].slot=slot;devicePresets[devicePresetCount++].name=name;}
  }
  return true;
}
bool isWiiMPresetChip() {return activeEndpoint.chip==Chip::A97 || activeEndpoint.chip==Chip::A98;}
bool sendWiiMPreset(int slot) {
  if(!isWiiMPresetChip() || slot<1 || slot>12) return false;
  String body;return request("MCUKeyShortClick:"+String(slot),body);
}
bool parseWiiMPresets(JsonVariantConst doc) {
  devicePresetCount=0;
  if(!doc["preset_list"].is<JsonArrayConst>()) return false;
  for(JsonObjectConst item:doc["preset_list"].as<JsonArrayConst>()) {
    if(!item["number"].is<int>() || !item["name"].is<const char*>()) continue;
    const int slot=item["number"].as<int>();
    const String name=cleanPresetName(item["name"].as<String>());
    if(slot<1 || slot>12 || name.isEmpty()) continue;
    bool duplicate=false;
    for(int i=0;i<devicePresetCount;++i) if(devicePresets[i].slot==slot) duplicate=true;
    if(duplicate || devicePresetCount>=12) continue;
    devicePresets[devicePresetCount].slot=slot;devicePresets[devicePresetCount++].name=name;
  }
  for(int i=0;i<devicePresetCount;++i) for(int j=i+1;j<devicePresetCount;++j)
    if(devicePresets[j].slot<devicePresets[i].slot) std::swap(devicePresets[i],devicePresets[j]);
  return true;
}
// A98 callback receiver is separate from WebServer: Arduino WebServer does not
// parse the UPnP NOTIFY verb. One bounded client, no waiting in the UI loop.
a98::Model a98Model;
WiFiServer a98EventServer(8081);
WiFiClient a98EventClient;
struct A98Subscription {String sid;uint32_t due=0,seq=0;bool hasSeq=false;};
A98Subscription a98Subscriptions[2];
struct A98Unsubscribe {String ip,sid;int service=0;};
A98Unsubscribe a98OldSubscriptions[2];
char a98EventBuffer[18433];size_t a98EventLength=0,a98HeaderLength=0;int a98BodyLength=0,a98EventService=-1;
uint32_t a98ClientStarted=0,a98MetadataRetry=0,a98EventEpoch=0;
bool a98MetadataLoaded=false,a98EventServerStarted=false;
const char* a98EventPaths[]={"/upnp/event/PlayQueue1","/upnp/event/rendertransport1"};
void resetA98State() {
  for(int i=0;i<2;++i) {
    if(a98Subscriptions[i].sid.length()) {a98OldSubscriptions[i].ip=a98Zone;a98OldSubscriptions[i].sid=a98Subscriptions[i].sid;a98OldSubscriptions[i].service=i;}
    a98Subscriptions[i].sid="";a98Subscriptions[i].due=0;a98Subscriptions[i].hasSeq=false;
  }
  a98EventClient.stop();a98Model.clear();a98Zone="";a98MetadataLoaded=false;a98MetadataRetry=0;++a98EventEpoch;
}
void a98ActiveChanged(int before,bool wakePoll=true) {
  if(before==a98Model.active) return;
  ++a98ArtworkEpoch;
  invalidateArtwork();if(wakePoll) lastPollAt=millis()-kFallbackPollIntervalMs;
  if(menuScreen==MenuScreen::Presets) drawMenu();
  else if(menuScreen==MenuScreen::Player) drawScreen();
}
void pollA98Events() {
  if(!a98EventServerStarted) return;
  if(!a98EventClient) {
    a98EventClient=a98EventServer.available();if(!a98EventClient) return;
    a98EventLength=a98HeaderLength=0;a98BodyLength=0;a98EventService=-1;a98ClientStarted=millis();
    if(activeEndpoint.chip!=Chip::A98 || a98EventClient.remoteIP().toString()!=deviceIp) {a98EventClient.stop();return;}
  }
  int budget=2048;
  while(a98EventClient.available() && budget-->0 && a98EventLength<sizeof(a98EventBuffer)-1) {
    a98EventBuffer[a98EventLength++]=char(a98EventClient.read());a98EventBuffer[a98EventLength]=0;
    if(!a98HeaderLength && a98EventLength>=4 && !memcmp(a98EventBuffer+a98EventLength-4,"\r\n\r\n",4)) {
      a98HeaderLength=a98EventLength;
      if(a98HeaderLength>2048) {a98EventClient.stop();return;}
      a98::Notify header;
      if(!a98::parseNotify(std::string(a98EventBuffer,a98HeaderLength),header)) {a98EventClient.print("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");a98EventClient.stop();return;}
      const String path=String("/a98/")+String(a98EventEpoch)+"/";
      for(int i=0;i<2;++i) if(header.path==std::string((path+String(i)).c_str()) && header.sid==std::string(a98Subscriptions[i].sid.c_str()) && a98Subscriptions[i].sid.length()) a98EventService=i;
      if(a98EventService<0) {a98EventClient.print("HTTP/1.1 412 Precondition Failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");a98EventClient.stop();return;}
      auto& sub=a98Subscriptions[a98EventService];
      if(sub.hasSeq && !a98::newSequence(header.seq,sub.seq)) {a98EventClient.print("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");a98EventClient.stop();return;}
      // Commit SEQ only with a complete body, not a truncated notification.
      a98IncomingSeq=header.seq;a98BodyLength=header.length;
    }
    if(a98HeaderLength && a98EventLength>=a98HeaderLength+size_t(a98BodyLength)) {
      a98EventClient.print("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      a98EventClient.stop();auto& sub=a98Subscriptions[a98EventService];sub.seq=a98IncomingSeq;sub.hasSeq=true;
      const int before=a98Model.active;
      a98Model.event(a98EventService==0,std::string(a98EventBuffer+a98HeaderLength,a98BodyLength));
      a98ActiveChanged(before);return;
    }
  }
  if((!a98HeaderLength && a98EventLength>=2048) || a98EventLength>=sizeof(a98EventBuffer)-1 || millis()-a98ClientStarted>2000) a98EventClient.stop();
}
bool a98ReadKeyMapping(String& response) {
  WiFiClient client;HTTPClient http;http.useHTTP10(true);http.setConnectTimeout(350);http.setTimeout(1200);
  if(!http.begin(client,"http://"+deviceIp+":59152/upnp/control/PlayQueue1")) return false;
  http.addHeader("Content-Type","text/xml; charset=utf-8");http.addHeader("SOAPAction","\"urn:schemas-wiimu-com:service:PlayQueue:1#GetKeyMapping\"");
  const int status=http.POST("<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:GetKeyMapping xmlns:u=\"urn:schemas-wiimu-com:service:PlayQueue:1\"/></s:Body></s:Envelope>");
  if(status!=200 || http.getSize()>65536){http.end();return false;}
  auto* stream=http.getStreamPtr();char chunk[513];uint32_t last=millis();response="";
  while((http.connected() || stream->available()) && millis()-last<1200) {
    int available=stream->available();if(available>0){int n=stream->readBytes(chunk,min(available,512));if(response.length()+n>65536){http.end();return false;}chunk[n]=0;response+=chunk;last=millis();}else delay(1);
  }
  http.end();return true;
}
bool readA98Presets() {
  String mapping;if(!a98ReadKeyMapping(mapping)) return false;
  a98::Model next;if(!next.mapping(std::string(mapping.c_str()))) return false;
  bool needsRoutines=false;for(const auto& key:next.keys) if(!key.routine.empty()) needsRoutines=true;
  bool complete=true;
  if(needsRoutines) {
    String body;
    if(request("getAllRoutines",body)) {
      StaticJsonDocument<512> filter;auto entry=filter["routines"][0];entry["id"]=true;entry["index"]=true;entry["name"]=true;entry["steps"][0]["type"]=true;entry["steps"][0]["payload"]["input"]=true;
      DynamicJsonDocument doc(8192);
      if(deserializeJson(doc,body,DeserializationOption::Filter(filter)) || !doc["routines"].is<JsonArray>()) complete=false;
      else for(JsonObject routine:doc["routines"].as<JsonArray>()) {
        const std::string id=routine["id"] | "";
        for(auto& key:next.keys) if(!key.routine.empty() && key.routine==id && (routine["index"].isNull() || routine["index"].as<int>()==key.slot)) {
          const String name=routine["name"].as<String>();if(name.length()) key.label=name.c_str();
          for(JsonObject step:routine["steps"].as<JsonArray>()) if(a98::lower(step["type"] | "")=="audioinput") {key.input=a98::inputName(step["payload"]["input"] | "");break;}
        }
      }
    } else complete=false;
  }
  const int before=a98Model.active;
  next.active=next.matchMedia(a98Model.lastQueue);next.lastQueue=a98Model.lastQueue;
  if(a98Model.inputActive) {next.active=0;next.inputActive=true;for(const auto& old:a98Model.keys) if(old.slot==a98Model.active) next.active=next.matchInput(old.input);}
  a98Model=next;devicePresetCount=0;
  for(const auto& key:a98Model.keys) if(key.slot) {devicePresets[devicePresetCount].slot=key.slot;devicePresets[devicePresetCount++].name=key.label.c_str();}
  presetZone=deviceIp;a98Zone=deviceIp;a98MetadataLoaded=true;a98MetadataRetry=millis()+60000;lastA31ActivityAt=millis();
  a98ActiveChanged(before);if(!complete) Serial.println("A98 routines unavailable; unresolved slots labelled Routine");
  return true;
}
void maintainA98Subscriptions() {
  if(WiFi.status()!=WL_CONNECTED || volumeDirty || scanPhase || otaActive || localVolumePending()) return;
  if(activeEndpoint.chip==Chip::A98 && !a98EventServerStarted){a98EventServer.begin();a98EventServerStarted=true;}
  if(a98Zone.length() && (activeEndpoint.chip!=Chip::A98 || a98Zone!=deviceIp)) resetA98State();
  for(auto& old:a98OldSubscriptions) if(old.sid.length()) {
    WiFiClient client;HTTPClient http;http.setConnectTimeout(250);http.setTimeout(500);
    if(http.begin(client,"http://"+old.ip+":59152"+a98EventPaths[old.service])) {http.addHeader("SID",old.sid);http.sendRequest("UNSUBSCRIBE");http.end();}
    old.sid="";return;
  }
  if(activeEndpoint.chip!=Chip::A98) return;
  if(a98Zone.isEmpty()) a98Zone=deviceIp;
  if(!a98MetadataLoaded) {
    if(int32_t(millis()-a98MetadataRetry)<0) return;
    if(!readA98Presets()) a98MetadataRetry=millis()+30000;
    return;
  }
  for(int i=0;i<2;++i) {auto& sub=a98Subscriptions[i];if(int32_t(millis()-sub.due)<0)continue;
    WiFiClient client;HTTPClient http;http.setConnectTimeout(350);http.setTimeout(800);
    if(!http.begin(client,"http://"+deviceIp+":59152"+a98EventPaths[i])) {sub.due=millis()+30000;return;}
    const char* headers[]={"SID","TIMEOUT"};http.collectHeaders(headers,2);http.addHeader("TIMEOUT","Second-600");
    if(sub.sid.length()) http.addHeader("SID",sub.sid);
    else {http.addHeader("NT","upnp:event");http.addHeader("CALLBACK","<http://"+WiFi.localIP().toString()+":8081/a98/"+String(a98EventEpoch)+"/"+String(i)+">");}
    const int status=http.sendRequest("SUBSCRIBE");
    if(status==200 && http.header("SID").length()) {
      const String sid=http.header("SID");if(sid!=sub.sid) sub.hasSeq=false;sub.sid=sid;
      String timeout=http.header("TIMEOUT");int seconds=timeout.substring(timeout.indexOf('-')+1).toInt();if(seconds<1)seconds=600;seconds=min(seconds,3600);
      sub.due=millis()+uint32_t(seconds)*500;
    } else {sub.sid="";sub.hasSeq=false;sub.due=millis()+30000;}
    http.end();lastA31ActivityAt=millis();return;
  }
}

bool readDevicePresets() {
  devicePresetCount=0;presetZone=deviceIp;
  if(activeEndpoint.chip==Chip::A98) return readA98Presets();
  if(isWiiMPresetChip()) {
    String body;if(!request("getPresetInfo",body)) return false;
    StaticJsonDocument<256> filter;
    filter["preset_list"][0]["number"]=true;filter["preset_list"][0]["name"]=true;
    DynamicJsonDocument doc(4096);
    if(deserializeJson(doc,body,DeserializationOption::Filter(filter))) return false;
    return parseWiiMPresets(doc.as<JsonVariantConst>());
  }
  if(isCloud()) {
    String body;if(!request("getPresetListInfo",body)) return false;
    StaticJsonDocument<256> filter;
    filter["Presets"][0]["PresetNumber"]=true;filter["Presets"][0]["PresetName"]=true;
    DynamicJsonDocument doc(4096);
    if(deserializeJson(doc,body,DeserializationOption::Filter(filter))) return false;
    if(!doc["Presets"].is<JsonArray>()) return false;
    for(JsonObject item:doc["Presets"].as<JsonArray>()) {
      const int slot=item["PresetNumber"].as<int>();
      const String name=cleanPresetName(item["PresetName"].as<String>());
      if(slot>=1 && slot<=12 && name.length() && devicePresetCount<20) {
        devicePresets[devicePresetCount].slot=slot;devicePresets[devicePresetCount++].name=name;
      }
    }
    return true;
  }
  WiFiClient client;HTTPClient http;http.useHTTP10(true);
  http.setConnectTimeout(350);http.setTimeout(1200);
  if(!http.begin(client,"http://"+deviceIp+":49152/upnp/control/PlayQueue1")) return false;
  http.addHeader("Content-Type","text/xml; charset=utf-8");
  http.addHeader("SOAPAction","\"urn:schemas-wiimu-com:service:PlayQueue:1#GetKeyMapping\"");
  const String body="<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:GetKeyMapping xmlns:u=\"urn:schemas-wiimu-com:service:PlayQueue:1\"/></s:Body></s:Envelope>";
  const int status=http.POST(body);
  if(status!=200 || http.getSize()>65536) {http.end();return false;}
  // Bound even a response without Content-Length. Never log token-bearing metadata.
  String response;response.reserve(4096);char chunk[513];auto* stream=http.getStreamPtr();
  uint32_t lastData=millis();
  while((http.connected() || stream->available()) && millis()-lastData<1200) {
    const int available=stream->available();
    if(available>0) {
      const int count=stream->readBytes(chunk,min(available,512));
      if(response.length()+count>65536) {http.end();return false;}
      chunk[count]=0;response+=chunk;lastData=millis();
    } else delay(1);
  }
  http.end();return parseKeyPresets(response);
}

void pollEncoder();
void sendVolumeWhenReady();

// Network artwork is fetched synchronously; keep the encoder and volume
// command alive between incoming chunks instead of waiting for the cover.
void serviceVolumeDuringArtwork() {
  pollEncoder();
  sendVolumeWhenReady();
  if (WiFi.status() == WL_CONNECTED) {settingsServer.handleClient();pollA98Events();}
}

bool request(const String& command, String& response);
String cleanMedia(String value) {
  value.trim(); String lower=value; lower.toLowerCase();
  return lower=="unknown" || lower=="null" || lower=="http_music" ? String("") : value;
}
String cleanArtworkUrl(String url) {
  url=decodeXml(cleanMedia(url));
  if(url.startsWith("/")) url=String(activeEndpoint.tls?"https://":"http://")+deviceIp+":"+String(activeEndpoint.port)+url;
  return url.startsWith("http://") || url.startsWith("https://") ? url : String("");
}
int parsePlayback(String value) { value.trim(); value.toLowerCase(); return playbackState(value.c_str()); }
String fetchSoapArtworkAddress();
String fetchArtworkAddress() {
  if (isCloud() && cleanArtworkUrl(reportedArtworkUrl).length()) return cleanArtworkUrl(reportedArtworkUrl);
  if (activeEndpoint.chip != Chip::A31) {
    String body;
    if (request("getMetaInfo", body)) {
      DynamicJsonDocument info(6144);
      if (!deserializeJson(info, body)) {
        JsonVariant data = info["metaData"].isNull() ? info.as<JsonVariant>() : info["metaData"].as<JsonVariant>();
        const String title = String(data["title"] | ""), artist = String(data["artist"] | "");
        // Reading artwork must not overwrite transport metadata with a different title source.
        String art = String(data["albumArtURI"] | "");
        if (art.isEmpty()) art = String(data["albumArtURI "] | "");
        if(isCloud() && (title.isEmpty() || !cleanMedia(title).equalsIgnoreCase(trackTitle))) art="";
        art=cleanArtworkUrl(art);
        if (art.length()) return art;
      }
    }
  }
  if (isCloud()) return ""; // gmrender state is independent of A33 main playback
  return fetchSoapArtworkAddress();
}
String fetchSoapArtworkAddress() {
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
  String raw=xmlValue(response,"TrackMetaData");
  if(raw.isEmpty()) raw=xmlValue(response,"CurrentTrackMetaData");
  if(raw.isEmpty()) raw=xmlValue(response,"AVTransportURIMetaData");
  String metadata = decodeXml(raw);
  String url = decodeXml(xmlValue(metadata, "albumArtURI"));
  if (url.startsWith("/")) url = "http://" + deviceIp + ":49152" + url;
  return url;
}

bool downloadAndDecodeArtOriginal(String url) {
  // This exact Qobuz CDN hostname was verified to serve the same cover over
  // HTTP 200. Do not downgrade other HTTPS hosts without checking them.

  for (int redirects = 0; redirects < 4; ++redirects) {
    // HTTPS covers use the same local TLS client as chip-aware control.
    // TuneIn A31 test artwork was verified to return HTTP 200 directly.
    if (!url.startsWith("http://") && !url.startsWith("https://")) return false;
    WiFiClient plain; WiFiClientSecure secure;
    secure.setInsecure(); secure.setHandshakeTimeout(2);
    HTTPClient http;
    http.useHTTP10(true);
    http.setConnectTimeout(750);
    http.setTimeout(1800);
    const bool began = url.startsWith("https://") ? http.begin(secure, url) : http.begin(plain, url);
    if (!began) return false;
    const char* headers[] = {"Location"};
    http.collectHeaders(headers, 1);
    http.addHeader("User-Agent","airControl-KNOB/0.8.16");
    http.addHeader("Accept","image/*,*/*;q=0.8");
    http.addHeader("Accept-Encoding","identity");
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
    const int declaredSize=http.getSize();
    const size_t capacity = declaredSize>0 ? size_t(declaredSize) : psramFound() ? kMaxArtworkBytes : 96*1024;
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
    sourceWidth=sourceHeight=0;
    memset(nextArtPixels, 0, sizeof(nextArtPixels));
    const bool complete=declaredSize<=0 || length==size_t(declaredSize);
    if (complete && length > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N') {
      if (pngDecoder.openRAM(bytes, length, pngLine) == PNG_SUCCESS) {
        sourceWidth = pngDecoder.getWidth();
        sourceHeight = pngDecoder.getHeight();
        ok = sourceWidth <= 2048 && sourceHeight <= 2048 &&
             sourceWidth > 0 && sourceHeight > 0 && !pngDecoder.isInterlaced() &&
             pngDecoder.decode(nullptr, 0) == PNG_SUCCESS;
        pngDecoder.close();
      }
    } else if (complete && length > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
      if (jpegDecoder.openRAM(bytes, length, jpegBlock)) {
        sourceWidth = jpegDecoder.getWidth();
        sourceHeight = jpegDecoder.getHeight();
        const bool validDimensions=sourceWidth>0 && sourceHeight>0 && sourceWidth<=2048 && sourceHeight<=2048;
        // JPEGDEC emits progressive DC thumbnails at ceil(original / 8).
        // Callback coordinates refer to that output, not the JPEG header size.
        if (jpegDecoder.getJPEGType() == JPEG_MODE_PROGRESSIVE) {
          sourceWidth=(sourceWidth+7)/8;
          sourceHeight=(sourceHeight+7)/8;
        }
        jpegDecoder.setPixelType(RGB565_LITTLE_ENDIAN);
        ok = validDimensions &&
             jpegDecoder.decode(0, 0, 0) == 1;
        jpegDecoder.close();
      }
    }
    free(bytes);
    if (ok) memcpy(artPixels,nextArtPixels,sizeof(artPixels));
    Serial.printf("Artwork: %s (%u bytes, source %dx%d, target %d)\n", ok ? "decoded" : "decode failed", static_cast<unsigned>(length), sourceWidth, sourceHeight, kArtSize);
    return ok;
  }
  return false;
}

bool downloadAndDecodeArt(String url) {
  const String small(artworkThumbnail(std::string(url.c_str())).c_str());
  if(small!=url) {
    if(downloadAndDecodeArtOriginal(small)) {Serial.println("Artwork: service thumbnail used");return true;}
    Serial.println("Artwork: thumbnail unavailable; trying original");
  }
  return downloadAndDecodeArtOriginal(url);
}

void pollArtwork() {
  if (WiFi.status() != WL_CONNECTED || menuScreen != MenuScreen::Player) return;
  const String requestedDevice=deviceIp,requestedTitle=trackTitle;
  const uint32_t requestedArtEpoch=a98ArtworkEpoch;
  String url=cleanArtworkUrl(fetchArtworkAddress());
  if(deviceIp!=requestedDevice || trackTitle!=requestedTitle || requestedArtEpoch!=a98ArtworkEpoch) return;
  lastA31ActivityAt=millis();
  if(url.isEmpty()) { const bool redraw=artworkReady; invalidateArtwork(); if(redraw) drawScreen(); return; }
  if(url==currentArtworkUrl && artworkReady) return;
  if(url==lastArtAttemptUrl && millis()-lastArtAttemptAt<5000) return;
  lastArtAttemptUrl=url; lastArtAttemptAt=millis();
  bool ok=downloadAndDecodeArt(url);
  if(!ok && !isCloud()) {
    const String fallback=cleanArtworkUrl(fetchSoapArtworkAddress());
    if(fallback.length() && fallback!=url) {
      if(fallback==currentArtworkUrl && artworkReady) return;
      if(downloadAndDecodeArt(fallback)) {url=fallback;ok=true;}
    }
  }
  if(deviceIp!=requestedDevice || trackTitle!=requestedTitle || requestedArtEpoch!=a98ArtworkEpoch) return;
  if(ok) { currentArtworkUrl=url; artworkAttempted=artworkReady=true; drawScreen(); }
  else { const bool redraw=artworkReady; artworkReady=artworkAttempted=false; currentArtworkUrl=""; if(redraw) drawScreen(); }
  Serial.println(ok?"Artwork committed":"Artwork unavailable; cover area left blank");
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
  Endpoint endpoint;
  bool known = false;
  if (ip == deviceIp && activeEndpoint.chip != Chip::Unknown) { endpoint = activeEndpoint; known = true; }
  if (!known) for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip && zones[i].endpoint.chip != Chip::Unknown) { endpoint = zones[i].endpoint; known = true; break; }
  if (!known) {
    String statusBody;
    if (!detectEndpoint(ip, endpoint, statusBody)) return false;
    if (ip == deviceIp) applyEndpoint(endpoint);
    if (command == "getStatusEx") { response = statusBody; if (ip == deviceIp) lastA31ActivityAt = millis(); return true; }
  }
  const bool ok = readEndpoint(ip, endpoint, command, response, timeoutMs);
  if (ip == deviceIp) lastA31ActivityAt = millis();
  return ok;
}

String sourceMode(const String& name) {
  if (isCloud()) return name;
  if (name == "Network") return "wifi";
  if (name == "Bluetooth") return "bluetooth";
  if (name == "AUX In" || name == "LineIn" || name == "RCA In") return "line-in";
  if (name == "USB Disk") return "udisk";
  if (name == "Optical In" || name == "OpticalIn") return "optical";
  if (name == "RCA 2 In") return "line-in2";
  if (name == "USB DAC" || name == "USBDAC") return "PCUSB";
  if (name == "HDMI ARC") return "HDMI";
  return "";
}
void readSourceCapabilities(JsonVariantConst info) {
  JsonArrayConst list = info["DevFunction"].as<JsonArrayConst>();
  if (list.isNull()) return;
  discoveredSourceCount = 0; sourceCapabilitiesKnown = true;
  for (JsonVariantConst item : list) {
    const String name = item.as<String>(); const String mode = sourceMode(name);
    if (mode.length() && discoveredSourceCount < 12) {
      discoveredSourceNames[discoveredSourceCount] = name;
      discoveredSourceModes[discoveredSourceCount++] = mode;
    }
  }
}
bool request(const String& command, String& response) {
  const bool ok = requestTo(deviceIp, command, response);
  if (ok && command == "getStatusEx") {
    DynamicJsonDocument info(12288);
    if (!deserializeJson(info, response)) readSourceCapabilities(info.as<JsonVariantConst>());
  }
  return ok;
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
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body)) {
    const bool wasSlave = groupKnown && isSlave;
    groupKnown = false;
    if (wasSlave && menuScreen == MenuScreen::Player) drawScreen();
    return false;
  }
  const String nextMaster = isCloud() ? String(doc["Host"] | "0") : doc["master_ip"].as<String>();
  const String nextGroup = isCloud() ? String(doc["MultiroomStatus"] | "free") : doc["group"].as<String>();
  // A31 may retain a master_ip after leaving. group=0 reports that it is free.
  const bool ungrouped = nextGroup == "0" || nextGroup == "free" || nextGroup == "none";
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
  if (activeEndpoint.chip == Chip::Unknown && !request("getStatusEx", body)) { deviceOnline = false; return; }
  if (!request(isCloud() ? "getStatusEx" : "getPlayerStatus", body)) {
    deviceOnline = false;
    return;
  }
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body)) {
    deviceOnline = false;
    return;
  }
  if (isCloud()) {
    JsonVariant primary=doc["AllMate"].as<JsonVariant>();
    JsonVariant alt=primary.is<JsonObject>() && primary.size()>0 ? primary : doc["AltMate"].as<JsonVariant>();
    String state=cleanMedia(doc["PlayState"].as<String>());
    if(parsePlayback(state)<0) state=cleanMedia(alt["PlayState"].as<String>());

    DynamicJsonDocument player(4096);
    if(parsePlayback(state)<0) {
      String extra;
      if(request("getPlayerStatus",extra) && !deserializeJson(player,extra)) {
        state=cleanMedia(player["PlayStatus"].as<String>());
        if(parsePlayback(state)<0) state=cleanMedia(player["status"].as<String>());
      }
    }
    const int parsedState=parsePlayback(state);
    const bool newPlaying=parsedState<0 ? playing : parsedState==1;
    const int remoteVolume = doc["DevVolumeL"].as<int>();
    String muteState = String(doc["VolumeState"] | ""); muteState.toLowerCase(); muted = muteState == "mute";
    String title=cleanMedia(String(alt["TrackTitle"]|"")),artist=cleanMedia(String(alt["TrackArtist"]|""));
    const String playerTitle=cleanMedia(decodeMetadata(player["Title"]|""));
    if(title.isEmpty()) title=playerTitle;
    if(artist.isEmpty() && playerTitle.length() && playerTitle.equalsIgnoreCase(title)) artist=cleanMedia(decodeMetadata(player["Artist"]|""));
    const String uri=cleanMedia(String(alt["TrackUrl"]|""));
    bool changed=!deviceOnline || playing!=newPlaying;
    changed |= updateTrackMetadata(title,artist,uri);
    const String art=cleanArtworkUrl(String(alt["TrackImage"]|""));
    if(art!=reportedArtworkUrl) {reportedArtworkUrl=art;lastArtworkPollAt=millis()-kArtworkPollMs;}
    if(art.isEmpty() && artworkReady) {invalidateArtwork();changed=true;}
    playing=newPlaying;deviceOnline=true;
    if (!localVolumePending() && !doc["DevVolumeL"].isNull() && remoteVolume >= 0 && remoteVolume <= 100) { changed |= volume != remoteVolume; volume = remoteVolume; }
    if (changed && menuScreen == MenuScreen::Player) drawScreen();
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
  const String parsedTitle=cleanMedia(decodeMetadata(doc["Title"]|""));
  const String newTitle=parsedTitle;
  const String parsedArtist=cleanMedia(decodeMetadata(doc["Artist"]|""));
  const String newArtist=parsedArtist;
  bool changed = !deviceOnline || newPlaying != playing;
  deviceOnline = true;
  playing = newPlaying;
  const String uri=cleanMedia(decodeMetadata(doc["uri"]|doc["url"]|""));
  changed |= updateTrackMetadata(newTitle,newArtist,uri);
  if(activeEndpoint.chip==Chip::A98 && a98Subscriptions[1].sid.isEmpty()) {
    const int before=a98Model.active;a98Model.mode(doc["mode"].as<int>());a98ActiveChanged(before,false);
  }
  bool volumeChanged = false;
  if (!localVolumePending() && remoteVolume >= 0 && remoteVolume <= 100 && remoteVolume != volume) {
    volume = remoteVolume;
    volumeChanged = true;
  }
  if (changed) drawScreen();
  else if (volumeChanged) drawVolumeUpdate();
}

void sendVolumeWhenReady() {
  if (!volumeDirty || millis() - lastVolumeSentAt < kVolumeSendDelayMs ||
      static_cast<int32_t>(millis() - volumeRetryAt) < 0) return;
  // Do not block the UI waiting for the A31 TCP command spacing.
  if (activeEndpoint.chip == Chip::A31 && lastTcpSentAt && millis() - lastTcpSentAt < 220) return;
  String body;
  char command[16];
  snprintf(command, sizeof(command), "MCU+VOL+%03d", volume);
  lastVolumeSentAt = millis();
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


int readA33PresetPlayback() {
  String body;
  if(!readEndpoint(deviceIp,activeEndpoint,"getPlayerStatus",body,600)) return -1;
  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc,body)) return -1;
  String state=cleanMedia(doc["PlayStatus"].as<String>());
  if(parsePlayback(state)<0) state=cleanMedia(doc["status"].as<String>());
  return parsePlayback(state);
}
void servicePresetResume() {
  if(!presetResume.pending) return;
  if(!isCloud() || deviceIp!=presetResumeZone || isSlave || presetResume.expired(millis())) {presetResume.cancel();return;}
  if(!presetResume.ready(millis()) || volumeDirty || WiFi.status()!=WL_CONNECTED) return;
  presetResume.cancel(); // At most one read and one resume, never repeat choicePreset.
  const int state=readA33PresetPlayback();
  lastA31ActivityAt=millis();
  if(state==1) {lastPollAt=millis()-kFallbackPollIntervalMs;return;}
  // Unknown status uses the hardware-tested choice -> 2s -> resume fallback.
  // This is not claimed necessary for every preset. No automatic retry on Not.
  String body;
  const bool ok=readEndpoint(deviceIp,activeEndpoint,"setPlayerCmd:resume",body,800);
  Serial.println(ok ? "A33 preset resume sent" : "A33 preset resume failed");
  lastA31ActivityAt=millis();
  lastPollAt=millis()-kFallbackPollIntervalMs;
}

void drawVolumeArc() {
  // Paint each arc pixel exactly once in its final colour: never grey then yellow.
  const uint16_t inactive = display.color565(55, 55, 55);
  constexpr float r = 112.0f, half = 5.0f;
  const float end = 220.0f * volume / 100.0f;
  display.startWrite();
  for (int y = max(2,display.clipTop()); y < min(239,display.clipBottom()); ++y) {
    int runStart = -1; uint16_t runColor = 0;
    for (int x = 2; x <= 239; ++x) {
      const float dx = x - 120.0f, dy = y - 120.0f;
      const float distance2 = dx*dx + dy*dy;
      const bool ring = distance2 >= (r-half)*(r-half) && distance2 <= (r+half)*(r+half);
      float angle = ring ? atan2f(dy, dx) * 180.0f / PI - 160.0f : 360.0f;
      if (angle < 0) angle += 360.0f;
      const bool inside = x < 239 && ring && angle <= 220.0f;
      const uint16_t color = volume > 0 && angle <= end ? accentColor() : inactive;
      if (runStart >= 0 && (!inside || color != runColor)) {
        display.fillRect(runStart, y, x-runStart, 1, runColor); runStart = -1;
      }
      if (inside && runStart < 0) { runStart = x; runColor = color; }
    }
  }
  display.endWrite();
}

void drawVolumeLabel() {
  const String label = "VOL " + String(volume) + "%";
  const int width = 120, left = 60, y = 210;
  const int origin = (width - label.length()*kGlyphWidth)/2;
  uint16_t rowPixels[120];
  for (int row=0; row<kGlyphHeight; ++row) {
    for (int x=0; x<width; ++x) {
      const int glyphX = x-origin;
      bool ink = false;
      if (glyphX >= 0 && glyphX < static_cast<int>(label.length())*kGlyphWidth) {
        const int glyph = label[glyphX/kGlyphWidth]-32;
        const uint16_t bits = pgm_read_word(&kGlyphRows[glyph][row]);
        ink = bits & (1U << (kGlyphWidth-1-glyphX%kGlyphWidth));
      }
      rowPixels[x] = ink ? accentColor() : TFT_BLACK;
    }
    display.setSwapBytes(true); display.pushImage(left,y+row,width,1,rowPixels); display.setSwapBytes(false);
  }
}

// Compose the whole 240x240 frame offscreen, then show it in one transfer.
void drawVolumeUpdate() {
  if (menuScreen == MenuScreen::Player) { drawVolumeArc(); drawVolumeLabel(); }
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
    case MenuScreen::Presets: return devicePresetCount + 1;
    case MenuScreen::Source: return sourceCapabilitiesKnown ? discoveredSourceCount : activeEndpoint.chip == Chip::A31 ? 7 : isCloud() ? 5 : 3;
    case MenuScreen::EqBands: return 12;
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
    case MenuScreen::Presets: return item==devicePresetCount ? String("Refresh") : String(devicePresets[item].slot)+(activeEndpoint.chip==Chip::A98 && a98Model.active==devicePresets[item].slot ? " * " : " ")+devicePresets[item].name;
    case MenuScreen::Source: return sourceCapabilitiesKnown ? discoveredSourceNames[item] : isCloud() && item == 4 ? "HDMI ARC" : kSourceItems[item];
    case MenuScreen::Multiroom: return kMultiroomItems[item];
    case MenuScreen::Join: {
      const int i = joinZoneIndex(item);
      return i < 0 ? "NO PEERS" : zones[i].name;
    }
    case MenuScreen::Zone:
      return item < zoneCount ?
          String(zones[item].ip == deviceIp ? "* " : "") + zones[item].name + " " + chipName(zones[item].endpoint.chip) + " " +
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
    case MenuScreen::EqBands:
      if (item == 11) return "Refresh";
      if (item == 10) return graphicEnabled ? "EQ OFF" : "EQ ON";
      return String(kBandNames[item]) + " " + (graphicKnown ? String(isCloud() ? graphicBands[item] : graphicBands[item] - 50) : "--");
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
    case MenuScreen::EqBands: return "GRAPHIC EQ";
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

int firstInfoLine() { return constrain(selectedItem - 2, 0, max(0, infoLineCount - 5)); }
void drawInformation() {
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  drawWindow(menuScreen == MenuScreen::MemoryInfo ? "KNOB INFO" :
             menuScreen == MenuScreen::DeviceInfo ? "ZONE INFO" : "ZONE IP",
             35, 27, 17, 1, 0, TFT_WHITE, TFT_BLACK);
  const int first = firstInfoLine();
  for (int i = first; i < min(infoLineCount, first + 5); ++i) {
    const uint16_t color = i == selectedItem ? TFT_WHITE : display.color565(150, 150, 150);
    drawWindow(infoLines[i], 30, 59 + (i-first) * 29, 18, 1,
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
  const String progress = scanPhase == 1 ? String("SSDP") : String(scanPhase == 3 ? "R " : "") + String(scanHost - 1) + "/254";
  drawWindow(progress, 40, 96, 8, 2, 0, TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  drawWindow(String(zoneCount) + " ZONES FOUND", 40, 149, 16, 1, 0, TFT_WHITE, TFT_BLACK);
  drawWindow("HOLD: STOP", 65, 198, 11, 1, 0, accentColor(), TFT_BLACK);
}

void drawScreen() {
  if (menuScreen == MenuScreen::Player && !display.isCapturing()) {
    if (display.beginCapture()) {
      for(int top=0;top<240;top+=DisplayRouter::kCaptureRows) {
        display.setCaptureTop(top);drawScreen();display.presentCapture();
      }
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
  if (menuScreen == MenuScreen::EqBandEdit) {
    display.fillScreen(TFT_BLACK);
    drawWindow(kBandNames[editingBand], 45, 46, 15, 1, 0, TFT_WHITE, TFT_BLACK);
    drawWindow(String(isCloud() ? editingBandValue : editingBandValue - 50), 30, 94, 9, 2, 0, TFT_WHITE, TFT_BLACK);
    drawWindow(toneMessage.length() ? toneMessage : "PRESS: SAVE", 35, 169, 15, 1, 0, accentColor(), TFT_BLACK);
    drawWindow("HOLD: CANCEL", 45, 199, 15, 1, 0, accentColor(), TFT_BLACK);
    return;
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
  const bool feedbackActive=feedbackKind && static_cast<int32_t>(feedbackUntil-millis())>0;
  const bool slaveLabel=deviceOnline && groupKnown && isSlave;
  if(!feedbackActive || slaveLabel) drawWindow(deviceOnline ? (slaveLabel ? "SLAVE MODE" : (playing?"PLAY":"PAUSE")) : "OFFLINE",
    60,32,12,1,0,slaveLabel?display.color565(255,60,60):TFT_WHITE,TFT_BLACK);
  if(feedbackActive) {
    if(feedbackKind==3 || feedbackKind==4) drawWindow(feedbackKind==4 ? "CHECKING" : "SELECTED",60,32,12,1,0,accentColor(),TFT_BLACK);
    else {
      const int x=slaveLabel?54:111,y=31;
      if(feedbackKind==2) {display.fillRect(x,y,5,18,TFT_WHITE);display.fillRect(x+11,y,5,18,TFT_WHITE);}
      else for(int column=0;column<18;++column) {const int h=18-column;display.drawLine(x+column,y+9-h/2,x+column,y+9+h/2,TFT_WHITE);}
    }
  }
  if (artworkReady) {
    display.setSwapBytes(true);
    display.pushImage(82, 62, kArtSize, kArtSize, artPixels);
    display.setSwapBytes(false);
  } // No cover: intentionally leave this area black.
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
  // Only 240*16*2 = 7680 bytes, instead of a 115200-byte full framebuffer.
  if (!display.beginCapture(240-DisplayRouter::kCaptureRows)) {
    client.print("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n"
                 "Content-Length: 19\r\nConnection: close\r\n\r\nNot enough free RAM");
    client.stop();
    return;
  }
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
  bool writing=true;
  for(int top=240-DisplayRouter::kCaptureRows;top>=0 && writing && client.connected();top-=DisplayRouter::kCaptureRows) {
    display.setCaptureTop(top);drawScreen();
    for(int y=top+DisplayRouter::kCaptureRows-1;y>=top && client.connected();--y) {
      for (int x = 0; x < 240; ++x) {
        const uint16_t rgb = display.capturePixel(x, y);
        row[x*3]   = uint8_t((rgb & 31) * 255 / 31);
        row[x*3+1] = uint8_t(((rgb >> 5) & 63) * 255 / 63);
        row[x*3+2] = uint8_t(((rgb >> 11) & 31) * 255 / 31);
      }
      if (client.write(row, sizeof(row)) != sizeof(row)) {writing=false;break;}
      delay(0);
    }
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
          " · " + String(chipName(activeEndpoint.chip)) + " " + htmlEscape(deviceIp) + " · " +
          (groupKnown ? (isSlave ? "Slave" : "Independent / Master") : "Group unknown") + "</p>";
  page += "<p>Group: " + htmlEscape(groupValue) + " · master_ip: " + htmlEscape(masterIp) + "</p>";
  if (notice.length()) page += "<section>" + htmlEscape(notice) + "</section>";
  page += F(R"HTML(<section><h2>Зоны</h2><p id="zone-current"></p>
<button id="zone-search" type="button">Найти зоны</button><button id="zone-stop" type="button" hidden>Остановить поиск</button><p id="zone-status"></p>
<select id="zone-list" style="width:100%;padding:12px;background:#161616;color:white;margin:12px 0"></select>
<button id="zone-select" type="button" disabled>Выбрать зону</button>
<p>Поиск A31, A97, A98 и A33. Выбранная зона сохраняется без перезагрузки KNOB.</p></section>
<script>
let zoneTimer;
const byId=id=>document.getElementById(id);
async function loadZones(){
 try{
  const r=await fetch('/zones',{cache:'no-store'});if(!r.ok)throw Error('Не удалось получить список');
  const d=await r.json(),list=byId('zone-list'),old=list.value;
  list.replaceChildren();
  for(const z of d.zones){const o=document.createElement('option');o.value=z.ip;o.textContent=z.name+' · '+z.chip+' · '+z.ip;list.append(o);}
  if(d.zones.some(z=>z.ip===old))list.value=old;else list.value=d.current;
  byId('zone-current').textContent='Выбрана зона: '+d.current;
  byId('zone-status').textContent=d.scanning?(d.phase===3?'Повторная проверка A31… ':'Идёт поиск… ')+d.progress+'/254':'Найдено зон: '+d.zones.length;
  byId('zone-search').disabled=d.scanning;
  byId('zone-select').disabled=!list.value;
  byId('zone-stop').hidden=!d.scanning;
  clearTimeout(zoneTimer);if(d.scanning)zoneTimer=setTimeout(loadZones,500);
 }catch(e){byId('zone-status').textContent=e.message;}
}
byId('zone-stop').onclick=async()=>{try{await fetch('/zones/stop',{method:'POST'});await loadZones();}catch(e){byId('zone-status').textContent=e.message;}};
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
  page += "<label>Player IP<input name='device' value='" + htmlEscape(deviceIp) + "'></label>";
  page += F("<button type='submit'>Save and restart</button></form></section>"
    "<section><h2>Обновление прошивки</h2><p>Обновление файлом firmware.bin:</p>"
    "<form id='firmware-upload' method='post' action='/update' enctype='multipart/form-data'>"
    "<input name='firmware' type='file' accept='.bin' required><button type='submit'>Upload firmware</button></form></section>"
    "<section><h2>Factory reset</h2><p>Erases KNOB Wi-Fi credentials and network/A31 settings.</p>"
    "<form method='post' action='/reset' onsubmit=\"return confirm('Erase KNOB settings?')\">"
    "<button type='submit'>Reset KNOB</button></form></section>"
    "<p><a href='/screenshot.bmp' style='background:#fff'>Screenshot (port 8080)</a></p></html>");
  const String updateUi = String(R"HTML(<section><h2>Онлайн-обновление</h2>
<p>GitHub: keshman74/airKNOB · текущая версия: )HTML") + kFirmwareVersion + R"HTML(</p>
<button id="ota-check" type="button">Проверить обновление</button>
<button id="ota-install" type="button" disabled>Установить обновление</button>
<progress id="ota-progress" max="100" value="0" hidden style="width:100%;accent-color:white"></progress><p id="ota-status"></p><script>
let otaManifest=null,uploadBusy=false;
const uploadProgress=document.getElementById('ota-progress');
function uploadFirmware(form,size){
 return new Promise((resolve,reject)=>{
 const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout=180000;
 xhr.setRequestHeader('X-Firmware-Size',String(size));
 uploadProgress.hidden=false;uploadProgress.value=0;
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const percent=Math.min(99,Math.floor(e.loaded/e.total*100));uploadProgress.value=percent;otaStatus.textContent='Передача прошивки: '+percent+'%';}};
 xhr.upload.onload=()=>{otaStatus.textContent='Проверка и завершение записи на KNOB…';};
 xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300){uploadProgress.value=100;resolve(xhr.responseText);}else reject(Error(xhr.responseText||'HTTP '+xhr.status));};
 xhr.onerror=()=>reject(Error('Соединение потеряно. Проверьте статус на панели перед повтором.'));
 xhr.ontimeout=()=>reject(Error('Нет ответа от KNOB. Проверьте статус на панели.'));
 xhr.onabort=()=>reject(Error('Передача прервана'));xhr.send(form);
 });
}
function updateSucceeded(){otaStatus.textContent='Прошивка установлена. KNOB перезагружается…';setTimeout(()=>location.replace('/'),7000);}
document.getElementById('firmware-upload').onsubmit=async e=>{
 e.preventDefault();if(uploadBusy)return;
 const file=e.target.querySelector('input[type=file]').files[0];if(!file)return;
 uploadBusy=true;const button=e.target.querySelector('button');button.disabled=true;otaInstall.disabled=true;
 try{await uploadFirmware(new FormData(e.target),file.size);updateSucceeded();}
 catch(err){otaStatus.textContent='Ошибка обновления: '+err.message;button.disabled=false;uploadBusy=false;}
};
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
 if(!otaManifest||uploadBusy)return;uploadBusy=true;otaInstall.disabled=true;
 try{otaStatus.textContent='Скачиваем версию '+otaManifest.version+'…';const bytes=new Uint8Array(await (await otaFetch(otaManifest.url+'?md5='+otaManifest.md5)).arrayBuffer());
 if(bytes.length!==otaManifest.size||bytes[0]!==0xe9)throw Error('Неверный размер или формат прошивки');
 const form=new FormData();form.append('md5',otaManifest.md5);form.append('firmware',new Blob([bytes]),'airControl-KNOB-v'+otaManifest.version+'.bin');
 await uploadFirmware(form,bytes.length);updateSucceeded();
 }catch(e){otaStatus.textContent='Ошибка обновления: '+e.message;otaInstall.disabled=false;uploadBusy=false;}
};
</script></section>)HTML";
  page.replace("</html>", "<meta name='knob-version' content='" + String(kFirmwareVersion) + "'>" + updateUi + "</html>");
  // Screenshot is served independently on 8080; fix its URL on the page.
  page.replace("href='/screenshot.bmp'", "href='http://" + WiFi.localIP().toString() + ":8080/screenshot.bmp'");
  settingsServer.send(200, "text/html; charset=utf-8", page);
}

bool webScanRequested = false;
void discoverZones(bool keepMenuVisible = false);
void cancelZoneScan(bool returnMenu = false);

void drawOtaProgress(const String& state, bool force = false) {
  if (!force && millis()-otaDrawAt < 120) return;
  otaDrawAt = millis();
  display.setBrightness(kNormalBrightness); displayDimmed = false; lastInteractionAt = millis();
  const int percent = otaUploadFinished ? 100 : otaExpectedBytes ? min(size_t(99), otaWrittenBytes*100/otaExpectedBytes) : 0;
  if (force) { display.fillScreen(TFT_BLACK); drawWindow("FIRMWARE UPDATE",30,46,18,1,0,TFT_WHITE,TFT_BLACK); }
  drawWindow(state,30,80,18,1,0,TFT_WHITE,TFT_BLACK);
  display.fillRoundRect(30,118,180,16,5,display.color565(55,55,55));
  if (percent) display.fillRoundRect(30,118,180*percent/100,16,5,TFT_WHITE);
  drawWindow(otaExpectedBytes || otaUploadFinished ? String(percent)+"%" : String(otaWrittenBytes/1024)+" KB",45,151,15,1,0,TFT_WHITE,TFT_BLACK);
}
void startSettingsWeb() {
  const char* otaHeaders[] = {"X-Firmware-Size"};
  settingsServer.collectHeaders(otaHeaders, 1);
  settingsServer.on("/", HTTP_GET, []() { webPage(); });
  settingsServer.on("/health",HTTP_GET,[](){
    StaticJsonDocument<512> doc;
    doc["firmware"]=kFirmwareVersion;doc["uptime_s"]=millis()/1000;
    doc["free_heap"]=ESP.getFreeHeap();doc["min_free_heap"]=ESP.getMinFreeHeap();
    doc["largest_free_block"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    doc["psram_total"]=ESP.getPsramSize();doc["psram_free"]=ESP.getFreePsram();
    doc["capture_buffer_bytes"]=240*DisplayRouter::kCaptureRows*2;
    String body;serializeJson(doc,body);settingsServer.send(200,"application/json",body);
  });
  settingsServer.on("/zones", HTTP_GET, []() {
    DynamicJsonDocument doc(8192);
    doc["scanning"] = bool(scanPhase || webScanRequested);
    doc["phase"] = scanPhase;
    doc["current"] = deviceIp;
    doc["progress"] = scanPhase == 1 ? 0 : min(scanHost - 1, 254);
    JsonArray list = doc.createNestedArray("zones");
    for (int i = 0; i < zoneCount; ++i) {
      if (zones[i].endpoint.chip == Chip::Unknown) continue;
      JsonObject zone = list.createNestedObject();
      zone["name"] = zones[i].name;
      zone["ip"] = zones[i].ip;
      zone["chip"] = chipName(zones[i].endpoint.chip);
      zone["protocol"] = zones[i].endpoint.tls ? "https" : "http";
      zone["port"] = zones[i].endpoint.port;
    }
    String body; serializeJson(doc, body);
    settingsServer.send(200, "application/json; charset=utf-8", body);
  });
  settingsServer.on("/zones/scan", HTTP_POST, []() {
    if (!scanPhase) webScanRequested = true;
    settingsServer.send(202, "text/plain", "Searching");
  });
  settingsServer.on("/zones/stop", HTTP_POST, []() { cancelZoneScan(true); settingsServer.send(200,"text/plain","Stopped"); });
  settingsServer.on("/zones/select", HTTP_POST, []() {
    const String ip = settingsServer.arg("ip");
    int found = -1;
    for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip) found = i;
    if (found < 0 || zones[found].endpoint.chip == Chip::Unknown) { settingsServer.send(400, "text/plain", "Zone not found"); return; }
    cancelZoneScan();
    deviceIp = zones[found].ip;
    applyEndpoint(zones[found].endpoint);
    settings.putString("device", deviceIp);
    settings.putUChar("chip", uint8_t(activeEndpoint.chip));
    settings.putUShort("port", activeEndpoint.port);
    settings.putBool("tls", activeEndpoint.tls);
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
      webPage("Invalid player IP; nothing saved"); return;
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
    settings.putUChar("chip", 0);
    const String target = "http://" + (fixed ? ip.toString() : WiFi.localIP().toString()) + "/";
    settingsServer.send(200, "text/html; charset=utf-8",
      "<!doctype html><meta charset='utf-8'><style>body{background:#000;color:#fff;font:18px Arial;padding:32px}a{color:#fff}</style><p>Настройки сохранены. KNOB перезагружается…</p><p><a href='" + target + "'>Открыть главную страницу</a></p><script>history.replaceState(null,'','/');setTimeout(()=>location.replace('" + target + "'),7000);</script>");
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
      otaActive = false;
      portENTER_CRITICAL(&encoderMux); encoderRead=encoderWrite; encoderAccumulator=0; portEXIT_CRITICAL(&encoderMux);
      drawOtaProgress(ok ? "RESTARTING" : "UPDATE FAILED", true);
      if (ok) rebootAfterResponse = true;
    },
    []() {
      HTTPUpload& upload = settingsServer.upload();
      if (upload.status == UPLOAD_FILE_START) {
        cancelZoneScan();
        otaActive = true;
        const long requestedSize = settingsServer.header("X-Firmware-Size").toInt();
        otaExpectedBytes = requestedSize > 0 && requestedSize <= 8*1024*1024 ? size_t(requestedSize) : 0;
        otaWrittenBytes = 0;
        portENTER_CRITICAL(&encoderMux); encoderRead=encoderWrite; encoderAccumulator=0; portEXIT_CRITICAL(&encoderMux);
        drawOtaProgress("RECEIVING", true);
        otaUploadFinished = false;
        otaUploadFailed = !upload.filename.endsWith(".bin") || !Update.begin(UPDATE_SIZE_UNKNOWN);
        const String expectedMd5 = settingsServer.arg("md5");
        if (!otaUploadFailed && expectedMd5.length()) {
          bool valid = expectedMd5.length() == 32;
          for (size_t i = 0; i < expectedMd5.length(); ++i) valid = valid && isxdigit(static_cast<unsigned char>(expectedMd5[i]));
          if (!valid || !Update.setMD5(expectedMd5.c_str())) { Update.abort(); otaUploadFailed = true; }
        }
      } else if (upload.status == UPLOAD_FILE_WRITE && !otaUploadFailed) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.abort(); otaUploadFailed = true; drawOtaProgress("WRITE FAILED", true); }
        else { otaWrittenBytes += upload.currentSize; drawOtaProgress("WRITING FLASH"); }
      } else if (upload.status == UPLOAD_FILE_END && !otaUploadFailed) {
        drawOtaProgress("VERIFYING", true);
        if (!Update.end(true)) otaUploadFailed = true;
        else otaUploadFinished = true;
        drawOtaProgress(otaUploadFinished ? "COMPLETE" : "UPDATE FAILED", true);
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); otaUploadFailed = true; otaActive = false; drawOtaProgress("UPLOAD ABORTED", true);
      }
    });
  settingsServer.begin();
}

void setMenu(MenuScreen screen) {
  if (screen == MenuScreen::Zone && zoneCount == 0) {
    zones[0] = ZoneInfo{deviceIp, chipName(activeEndpoint.chip), "", activeEndpoint};
    zoneCount = 1;
  }
  menuScreen = screen;
  selectedItem = 0;
  menuMessage = "";
  marqueeStartedAt = marqueeLastAt = millis();
  marqueeOffset = 0;
  drawScreen();
  if(screen==MenuScreen::Presets) {
    menuMessage="READING";drawMenu();
    const bool ok=readDevicePresets();
    selectedItem=0;menuMessage=ok ? (devicePresetCount ? "" : "NO PRESETS") : "READ FAILED";
    drawMenu();
  }
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

void rememberZone(const String& ip, uint16_t preferredPort = 0) {
  if (zoneCount >= kMaxZones || !localIp(ip)) return;
  for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip && zones[i].endpoint.chip != Chip::Unknown) return;
  Endpoint endpoint; String body;
  if (!detectEndpoint(ip, endpoint, body, preferredPort)) { Serial.printf("Zone %s not identified\n",ip.c_str()); return; }
  DynamicJsonDocument info(12288); if (deserializeJson(info, body)) return;
  int index = -1;
  for (int i = 0; i < zoneCount; ++i) if (zones[i].ip == ip) index = i;
  if (index < 0) index = zoneCount++;
  ZoneInfo& zone = zones[index];
  zone.ip = ip; zone.endpoint = endpoint;
  zone.name = endpoint.chip == Chip::A33 ? String(info["UserDevName"] | info["DevName"] | "A33") : String(info["DeviceName"] | chipName(endpoint.chip));
  zone.uuid = normalizedUuid(String(info["uuid"] | ""));
  if (ip == deviceIp) { if (activeEndpoint.chip != endpoint.chip || activeEndpoint.port != endpoint.port) applyEndpoint(endpoint); readSourceCapabilities(info.as<JsonVariantConst>()); }
  Serial.printf("Zone: %s %s %s:%u\n", zone.name.c_str(), chipName(endpoint.chip), ip.c_str(), endpoint.port);
}

String ssdpLocationIp(const String& packet) {
  String lower = packet; lower.toLowerCase();
  int at = lower.indexOf("\nlocation:");
  if (at < 0) return "";
  at += 10;
  int end = packet.indexOf('\n',at);
  String url = packet.substring(at,end < 0 ? packet.length() : end); url.trim();
  if (!url.startsWith("http://") && !url.startsWith("https://")) return "";
  const int start = url.indexOf("://") + 3;
  int hostEnd = url.length();
  for (int i=start;i<int(url.length());++i) if (url[i]==':' || url[i]=='/') { hostEnd=i; break; }
  String host = url.substring(start,hostEnd); IPAddress ip;
  return ip.fromString(host) ? host : String("");
}
struct DiscoveryJob { uint32_t generation; uint8_t host; uint16_t port; };
struct DiscoveryResult {
  uint32_t generation; uint8_t host; bool ok; Endpoint endpoint;
  char name[128]; char uuid[64];
};
QueueHandle_t discoveryJobs = nullptr, discoveryResults = nullptr;
TaskHandle_t discoveryTaskHandle = nullptr;
std::atomic<uint32_t> discoveryGeneration{0}, discoveryDone{0}, discoveryOwnIp{0};
std::atomic<bool> discoveryRunning{false};
std::atomic<int> discoveryProgress{0};
bool discoveryEpochValid(uint32_t generation) {
  return discoveryRunning.load() && discoveryGeneration.load()==generation;
}
String discoveryIp(uint8_t host, IPAddress own) { return IPAddress(own[0],own[1],own[2],host).toString(); }
void discoveryWorker(void*) {
  struct Probe { int fd=-1; uint8_t host=0; uint16_t port=0; uint32_t started=0; } probes[4];
  uint32_t generation=0, started=0; int cursor=0;
  IPAddress own; DiscoveryLedger ledger;
  const uint16_t ports[4]={80,443,8000,8443};
  const auto closeProbes=[&]() { for(auto& p:probes) if(p.fd>=0) { ::close(p.fd); p.fd=-1; } };
  for (;;) {
    if(!discoveryRunning.load()) { closeProbes(); ulTaskNotifyTake(pdTRUE,portMAX_DELAY); continue; }
    const uint32_t current=discoveryGeneration.load();
    if(current!=generation) {
      closeProbes(); generation=current; started=millis(); cursor=0;
      own=IPAddress(discoveryOwnIp.load()); ledger=DiscoveryLedger{};
    }
    DiscoveryJob job;
    for(int n=0;n<8 && xQueueReceive(discoveryJobs,&job,0)==pdTRUE;++n)
      if(job.generation==generation && job.host!=own[3]) ledger.offer(job.host,job.port,millis());
    // Poll nonblocking connections first. No HTTP client runs while these
    // probe sockets are open, keeping the ESP32 socket count bounded.
    int active=0;
    for(auto& p:probes) if(p.fd>=0) {
      fd_set writeSet,errorSet; FD_ZERO(&writeSet); FD_ZERO(&errorSet);
      FD_SET(p.fd,&writeSet); FD_SET(p.fd,&errorSet); timeval timeout={0,0};
      const int ready=select(p.fd+1,nullptr,&writeSet,&errorSet,&timeout);
      if(ready>0) {
        int error=0; socklen_t size=sizeof(error);
        if(getsockopt(p.fd,SOL_SOCKET,SO_ERROR,&error,&size)==0 && error==0) ledger.offer(p.host,p.port,millis());
        ::close(p.fd); p.fd=-1;
      } else if(ready<0 || millis()-p.started>=180) { ::close(p.fd); p.fd=-1; }
      if(p.fd>=0) ++active;
    }
    if(!discoveryEpochValid(generation)) { closeProbes(); continue; }
    if(active==0) {
      const int host=ledger.next(millis());
      if(host>0) {
        ledger.begin(host);
        DiscoveryResult result{}; result.generation=generation; result.host=host;
        String body;
        result.ok=detectEndpoint(discoveryIp(host,own),result.endpoint,body,ledger.ports[host],600,generation);
        if(result.ok) {
          DynamicJsonDocument info(12288);
          result.ok=!deserializeJson(info,body);
          if(result.ok) {
            const String name=result.endpoint.chip==Chip::A33 ? String(info["UserDevName"]|info["DevName"]|"A33") : String(info["DeviceName"]|chipName(result.endpoint.chip));
            name.toCharArray(result.name,sizeof(result.name));
            normalizedUuid(String(info["uuid"]|"")).toCharArray(result.uuid,sizeof(result.uuid));
          }
        }
        ledger.finish(host,result.ok,millis());
        if(discoveryEpochValid(generation)) {
          // Result queue is bounded; main loop is always free to drain it.
          while(xQueueSend(discoveryResults,&result,pdMS_TO_TICKS(10))!=pdTRUE && discoveryEpochValid(generation)) {}
        }
        vTaskDelay(1); continue;
      }
    }
    // Up to four simultaneous TCP connects, independently of SSDP reception.
    int launched=0;
    if(!ledger.hasWork()) for(auto& p:probes) if(p.fd<0) {
      while(cursor<254*4 && launched<16) {
        const uint8_t host=cursor/4+1; const uint16_t port=ports[cursor%4];
        if(host==own[3] || ledger.verified[host] || ledger.attempts[host]>=2) { ++cursor; continue; }
        const int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(fd<0) break; // retry after outstanding sockets are released
        if(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0) { ::close(fd); break; }
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(port);
        const String ip=discoveryIp(host,own); address.sin_addr.s_addr=inet_addr(ip.c_str());
        ++cursor; ++launched;
        const int connected=::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
        if(connected==0) { ledger.offer(host,port,millis()); ::close(fd); break; }
        if(errno==EINPROGRESS || errno==EWOULDBLOCK || errno==EALREADY) { p.fd=fd; p.host=host; p.port=port; p.started=millis(); break; }
        ::close(fd);
      }
    }
    discoveryProgress.store(min(254,cursor/4));
    active=0; for(const auto& p:probes) if(p.fd>=0) ++active;
    if(cursor>=254*4 && !active && !ledger.hasWork() && uxQueueMessagesWaiting(discoveryJobs)==0 && millis()-started>=6500) {
      discoveryDone.store(generation);
      while(discoveryEpochValid(generation)) vTaskDelay(pdMS_TO_TICKS(5));
    }
    // A finite overall deadline covers hostile/unresponsive endpoints and
    // socket exhaustion. Preserve all results already verified.
    if(millis()-started>=60000) { closeProbes(); discoveryDone.store(generation); while(discoveryEpochValid(generation)) vTaskDelay(pdMS_TO_TICKS(5)); }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
bool initDiscoveryWorker() {
  if(discoveryTaskHandle) return true;
  discoveryJobs=xQueueCreate(32,sizeof(DiscoveryJob));
  discoveryResults=xQueueCreate(24,sizeof(DiscoveryResult));
  if(!discoveryJobs || !discoveryResults || xTaskCreatePinnedToCore(discoveryWorker,"zone-search",12288,nullptr,1,&discoveryTaskHandle,0)!=pdPASS) {
    if(discoveryJobs) vQueueDelete(discoveryJobs); if(discoveryResults) vQueueDelete(discoveryResults);
    discoveryJobs=discoveryResults=nullptr; discoveryTaskHandle=nullptr; return false;
  }
  return true;
}
String ssdpSeen[64]; int ssdpSeenCount=0;
void queueDiscoveryCandidate(const String& ip,uint16_t port=0) {
  IPAddress address; const IPAddress own=WiFi.localIP();
  if(!address.fromString(ip) || !localIp(ip) || address[0]!=own[0] || address[1]!=own[1] || address[2]!=own[2] || address==own || !discoveryRunning.load()) return;
  DiscoveryJob job{discoveryGeneration.load(),address[3],port};
  xQueueSend(discoveryJobs,&job,0);
}
void rememberSsdpCandidate(const String& ip) {
  if(!localIp(ip)) return;
  for(int i=0;i<ssdpSeenCount;++i) if(ssdpSeen[i]==ip) return;
  if(ssdpSeenCount>=64) return;
  IPAddress address; if(!address.fromString(ip)) return;
  DiscoveryJob job{discoveryGeneration.load(),address[3],0};
  if(xQueueSend(discoveryJobs,&job,0)==pdTRUE) ssdpSeen[ssdpSeenCount++]=ip;
}
void collectSsdpCandidates(WiFiUDP& udp,int budget) {
  while(budget-- >0 && udp.parsePacket()>0) {
    const String sender=udp.remoteIP().toString(); String packet; packet.reserve(1536);
    while(udp.available()) { const char c=char(udp.read()); if(packet.length()<1536) packet+=c; }
    rememberSsdpCandidate(sender);
    const String location=ssdpLocationIp(packet); if(location.length() && location!=sender) rememberSsdpCandidate(location);
  }
}
void sendDiscoveryBurst() {
  const char* targets[]={"ssdp:all","upnp:rootdevice","urn:schemas-upnp-org:device:MediaRenderer:1"};
  for(const auto target:targets) {
    discoveryUdp.beginPacket(IPAddress(239,255,255,250),1900);
    discoveryUdp.print(String("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ")+target+"\r\n\r\n");
    discoveryUdp.endPacket();
  }
}
uint8_t discoveryBursts=0; uint32_t discoveryUiAt=0;
void cancelZoneScan(bool returnMenu) {
  const bool wasRunning=scanPhase!=0;
  if(wasRunning && zoneCount) {
    String cache; for(int i=0;i<zoneCount;++i) { if(i) cache+=';'; cache+=zones[i].ip; }
    settings.putString("zone-cache",cache);
  }
  discoveryRunning.store(false); discoveryGeneration.fetch_add(1);
  discoveryUdp.stop(); cloudDiscoveryUdp.stop(); ssdpNotifyUdp.stop(); scanPhase=0; webScanRequested=false;
  if(returnMenu && wasRunning && menuScreen==MenuScreen::Scan) setMenu(scanReturnScreen);
}
void discoverZones(bool keepMenuVisible) {
  cancelZoneScan();
  if(WiFi.status()!=WL_CONNECTED || !initDiscoveryWorker()) { menuMessage="SEARCH UNAVAILABLE"; drawScreen(); return; }
  scanReturnScreen=menuScreen==MenuScreen::Join ? MenuScreen::Join : MenuScreen::Zone;
  xQueueReset(discoveryJobs); xQueueReset(discoveryResults);
  discoveryOwnIp.store(uint32_t(WiFi.localIP())); discoveryProgress.store(0); discoveryDone.store(0);
  for(int i=0;i<ssdpSeenCount;++i) ssdpSeen[i]=""; ssdpSeenCount=0;
  discoveryGeneration.fetch_add(1); discoveryRunning.store(true);
  queueDiscoveryCandidate(deviceIp,activeEndpoint.port);
  for(int i=0;i<zoneCount;++i) queueDiscoveryCandidate(zones[i].ip,zones[i].endpoint.port);
  String cached=settings.getString("zone-cache","");
  while(cached.length()) { const int end=cached.indexOf(';'); const String ip=cached.substring(0,end<0?cached.length():end); queueDiscoveryCandidate(ip); if(end<0) break; cached.remove(0,end+1); }
  zoneCount=0; scanHost=1; scanPhase=2; scanInBackground=keepMenuVisible;
  if(!keepMenuVisible) { menuScreen=scanReturnScreen; selectedItem=0; }
  cloudDiscoveryUdp.begin(53308); ssdpNotifyUdp.beginMulticast(IPAddress(239,255,255,250),1900);
  discoveryUdp.begin(0); scanStartedAt=millis(); discoveryBursts=1; discoveryUiAt=0;
  xTaskNotifyGive(discoveryTaskHandle);
  sendDiscoveryBurst(); menuMessage="SEARCHING"; drawScreen();
}
void pollZoneScan() {
  if(!scanPhase) return;
  // Always drain discovery traffic before anything that can draw a menu.
  collectSsdpCandidates(discoveryUdp,12); collectSsdpCandidates(ssdpNotifyUdp,8);
  for(int i=0;i<4 && cloudDiscoveryUdp.parsePacket()>0;++i) {
    const String ip=cloudDiscoveryUdp.remoteIP().toString(); while(cloudDiscoveryUdp.available()) cloudDiscoveryUdp.read(); rememberSsdpCandidate(ip);
  }
  if(discoveryBursts<3 && millis()-scanStartedAt>=uint32_t(discoveryBursts)*800) { sendDiscoveryBurst(); ++discoveryBursts; }
  DiscoveryResult result; bool changed=false;
  while(xQueueReceive(discoveryResults,&result,0)==pdTRUE) {
    if(result.generation!=discoveryGeneration.load() || !result.ok) continue;
    const String ip=discoveryIp(result.host,WiFi.localIP()); int index=-1;
    for(int i=0;i<zoneCount;++i) if(zones[i].ip==ip) index=i;
    if(index<0) { if(zoneCount>=kMaxZones) continue; index=zoneCount++; }
    zones[index]=ZoneInfo{ip,String(result.name),String(result.uuid),result.endpoint}; changed=true;
    if(ip==deviceIp && (activeEndpoint.chip!=result.endpoint.chip || activeEndpoint.port!=result.endpoint.port || activeEndpoint.tls!=result.endpoint.tls)) applyEndpoint(result.endpoint);
    Serial.printf("Zone verified: %s %s %s:%u\n",result.name,chipName(result.endpoint.chip),ip.c_str(),result.endpoint.port);
  }
  scanHost=discoveryProgress.load()+1;
  if(changed || millis()-discoveryUiAt>=500) {
    discoveryUiAt=millis(); menuMessage=String(zoneCount)+" FOUND | SEARCH";
    if(menuScreen==MenuScreen::Zone || menuScreen==MenuScreen::Join) { selectedItem=constrain(selectedItem,0,max(0,menuCount()-1)); drawMenu(); }
  }
  if(discoveryDone.load()==discoveryGeneration.load() || zoneCount>=kMaxZones || WiFi.status()!=WL_CONNECTED) {
    cancelZoneScan(); menuMessage=String(zoneCount)+" ZONES FOUND";
    if(menuScreen==MenuScreen::Zone || menuScreen==MenuScreen::Join) drawMenu();
  }
}

bool openInformation(MenuScreen screen) {
  String body;
  if (!request("getStatusEx", body)) { menuMessage = "DEVICE OFF"; return false; }
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body)) { menuMessage = "STATUS ERROR"; return false; }
  infoLineCount = 5;
  if (screen == MenuScreen::DeviceInfo) {
    infoLines[0] = "NAME " + (isCloud() ? String(doc["UserDevName"] | doc["DevName"] | "A33") : String(doc["DeviceName"] | "?"));
    infoLines[1] = "CHIP " + String(chipName(activeEndpoint.chip));
    infoLines[2] = "FW " + (isCloud() ? String(doc["VERSION"] | "?") : String(doc["firmware"] | "?"));
    infoLines[3] = "PROJECT " + (isCloud() ? String(doc["ProjectName"] | "?") : String(doc["project"] | "?"));
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
  if (activeEndpoint.chip == Chip::A98 || activeEndpoint.chip == Chip::A97) {
    const char* names[] = {"Flat", "Classical", "Pop", "Jazz", "Rock", "Vocal Booster"};
    String body; return request("EQLoad:" + String(names[index]), body);
  }
  if (isCloud()) return false;
  return mcuCommand("MCU+PAS+RAKOIT:EQS:" + String(index) + "&");
}

void handleTcpMessage(const String& message) {
  if (message.startsWith("AXX+VOL+") && message.length() >= 11) {
    const int n = message.substring(8, 11).toInt();
    if (n >= 0 && n <= 100 && !localVolumePending() && volume != n) {
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
    const String incomingTitle=cleanMedia(decodeMetadata(doc["title"]|"")),incomingArtist=cleanMedia(decodeMetadata(doc["artist"]|""));
    const String title=doc.containsKey("title")?incomingTitle:trackTitle;
    const String artist=doc.containsKey("artist")?incomingArtist:(title!=trackTitle?String(""):trackArtist);
    const String uri=doc.containsKey("uri")?cleanMedia(decodeMetadata(doc["uri"]|"")):currentMediaUri;
    if(updateTrackMetadata(title,artist,uri) && menuScreen==MenuScreen::Player) drawScreen();
  }
}

bool ensureA31Tcp() {
  if (activeEndpoint.chip != Chip::A31 && activeEndpoint.chip != Chip::A33) return false;
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
  const bool connected = target.fromString(deviceIp) && a31Tcp.connect(target, isCloud() ? 1234 : 8899, 350);
  if (connected) Serial.printf("%s TCP connected\n", chipName(activeEndpoint.chip));
  return connected;
}

void pollA31Tcp(String* reply = nullptr) {
  if (!a31Tcp.connected()) return;
  if (isCloud()) {
    // JSON events may be concatenated or fragmented; frame by braces, respecting strings.
    static String previousIp;
    static String event; static int depth = 0; static bool quoted = false, escaped = false;
    if (previousIp != deviceIp) { previousIp = deviceIp; event = ""; depth = 0; quoted = escaped = false; }
    int budget = 2048;
    while (a31Tcp.available() && budget-- > 0) {
      const char c = char(a31Tcp.read());
      if (!depth) { if (c != '{') continue; event = ""; quoted = escaped = false; }
      event += c;
      if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
      else { if (c == '"') quoted = true; else if (c == '{') ++depth; else if (c == '}') --depth; }
      if (event.length() > 8192) { event = ""; depth = 0; quoted = escaped = false; continue; }
      if (!depth) {
        DynamicJsonDocument doc(8192);
        if (!deserializeJson(doc, event)) {
          const String command = doc["cmd"] | "";
          if (command == "volume" && !localVolumePending() && !doc["level"].isNull()) {
            const int n = doc["level"].as<int>(); if (n >= 0 && n <= 100) { volume = n; if (menuScreen == MenuScreen::Player) drawVolumeUpdate(); }
          } else if (command == "mute") muted = String(doc["state"] | "") == "on";
          else if (command == "play" || command == "pause") { playing = command == "play"; if (menuScreen == MenuScreen::Player) drawScreen(); }
          else if (command == "play_info") lastPollAt = millis() - kPollIntervalMs;
          deviceOnline = true;
        }
        event = "";
      }
    }
    return;
  }
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
  if (isCloud()) {
    if (reply) return false;
    String json;
    if (payload.startsWith("MCU+VOL+")) json = "{\"cmd\":\"volume\",\"level\":" + String(payload.substring(8).toInt()) + "}";
    else if (payload == "MCU+PLY-PUS") json = "{\"cmd\":\"pause\"}";
    else if (payload == "MCU+PLY-PLA") json = "{\"cmd\":\"play\"}";
    else if (payload.startsWith("MCU+MUT+")) json = String("{\"cmd\":\"mute\",\"state\":\"") + (payload.endsWith("001") ? "on" : "off") + "\"}";
    else return false;
    if (!ensureA31Tcp()) return false;
    json += '\n';
    return a31Tcp.write(reinterpret_cast<const uint8_t*>(json.c_str()), json.length()) == json.length();
  }
  if (activeEndpoint.chip != Chip::A31) return false;
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
  if (isCloud()) {
    String body; toneKnown[0] = toneKnown[1] = toneKnown[2] = false;
    if (!request("getEqInfo:1", body)) return false;
    DynamicJsonDocument doc(3072); if (deserializeJson(doc, body)) return false;
    JsonVariant data = doc["data"].isNull() ? doc.as<JsonVariant>() : doc["data"].as<JsonVariant>();
    toneKnown[0] = !data["Bass"].isNull(); toneKnown[2] = !data["Treble"].isNull();
    toneRaw[0] = data["Bass"].as<int>() + 5; toneRaw[2] = data["Treble"].as<int>() + 5;
    return toneKnown[0] && toneKnown[2];
  }
  if (activeEndpoint.chip != Chip::A31) return false;
  String eq, mid;
  const bool eqOk = mcuCommand("MCU+PAS+EQGet&", &eq);
  const bool midOk = mcuCommand("MCU+PAS+RAKOIT:MID&", &mid);
  toneKnown[0] = eqOk && parseTone(eq, "EQ:bass:", 0, 10, toneRaw[0]);
  toneKnown[2] = eqOk && parseTone(eq, "EQ:treble:", 0, 10, toneRaw[2]);
  toneKnown[1] = midOk && parseTone(mid, "MID:", -5, 5, toneRaw[1]);
  return toneKnown[0] && toneKnown[1] && toneKnown[2];
}

bool readVirtualBass() {
  if (activeEndpoint.chip != Chip::A31) { virtualBassKnown = false; return false; }
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
  if (isCloud()) {
    if (editingTone == 1) return false;
    const int bass = editingTone == 0 ? editingRaw - 5 : toneRaw[0] - 5;
    const int treble = editingTone == 2 ? editingRaw - 5 : toneRaw[2] - 5;
    String body;
    const String payload = "{\"Bass\":\"" + String(bass) + "\",\"Treble\":\"" + String(treble) + "\"}";
    return request("setEqHighAndLowFrequencies:" + payload, body) && readTone() && toneRaw[editingTone] == editingRaw;
  }
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

bool readGraphicEq() {
  String body; graphicKnown = false;
  if (!request(isCloud() ? "getEqInfo:2" : "EQGetBand", body)) return false;
  DynamicJsonDocument doc(4096); if (deserializeJson(doc, body)) return false;
  JsonArray list = isCloud() ? doc["EqBand"].as<JsonArray>() : doc["EQBand"].as<JsonArray>();
  if (list.isNull() || list.size() != 10) return false;
  bool seen[10] = {};
  for (int i = 0; i < 10; ++i) {
    const int index = isCloud() ? i : list[i]["index"].as<int>();
    if (index < 0 || index >= 10 || seen[index] || (!isCloud() && list[i]["value"].isNull())) return false;
    const int value = isCloud() ? list[i].as<int>() : list[i]["value"].as<int>();
    if (value < (isCloud() ? -12 : 0) || value > (isCloud() ? 12 : 99)) return false;
    seen[index] = true; graphicBands[index] = value;
  }
  graphicEnabled = isCloud() ? true : String(doc["EQStat"] | "") == "on";
  graphicKnown = true; return true;
}
bool saveGraphicBand() {
  StaticJsonDocument<1024> doc;
  if (isCloud()) {
    doc["Custom"] = "1"; doc["Name"] = "airControl";
    JsonArray list = doc.createNestedArray("EqBand");
    for (int i = 0; i < 10; ++i) list.add(String(i == editingBand ? editingBandValue : graphicBands[i]));
  } else {
    JsonObject band = doc.createNestedArray("EQBand").createNestedObject();
    band["index"] = editingBand; band["param_name"] = kBandParams[editingBand]; band["value"] = editingBandValue;
  }
  String payload, body; serializeJson(doc, payload);
  if (!request((isCloud() ? "setPresetEq:" : "EQSetBand:") + payload, body)) return false;
  return readGraphicEq() && graphicBands[editingBand] == editingBandValue;
}
void chooseMenuItem() {
  String body;
  switch (menuScreen) {
    case MenuScreen::Home: {
      const MenuScreen pages[] = {MenuScreen::Presets, MenuScreen::Source,
          MenuScreen::Multiroom, MenuScreen::Zone, MenuScreen::Settings};
      if (pages[selectedItem] == MenuScreen::Source) { String status; request("getStatusEx", status); }
      setMenu(pages[selectedItem]);
      // Opening a menu must be immediate and must not start a blocking /24
      // network scan. Find zones is an explicit action inside both menus.
      return;
    }
    case MenuScreen::Presets:
      {
        if(selectedItem==devicePresetCount) {
          menuMessage="READING";drawMenu();
          const bool ok=readDevicePresets();selectedItem=0;
          menuMessage=ok ? (devicePresetCount ? "" : "NO PRESETS") : "READ FAILED";
          drawMenu();return;
        }
        if(presetZone!=deviceIp || selectedItem<0 || selectedItem>=devicePresetCount) {menuMessage="REFRESH PRESETS";break;}
        presetResume.cancel();
        const int slot=devicePresets[selectedItem].slot;
        if(slot<1 || slot>(isCloud() || isWiiMPresetChip() ? 12 : 10)) {menuMessage="SLOT NOT VERIFIED";break;}
        // Refresh immediately before acting: a phone may have changed the group.
        if (!refreshGroupStatus()) { menuMessage = "CHECK ZONE"; break; }
        if (isSlave) { menuMessage = "SLAVE: NO PRESET"; break; }
        char command[16];
        snprintf(command, sizeof(command), "MCU+KEY+%03d", slot);
        bool sent=false;bool unconfirmed=false;
        if(isCloud()) {
          int status=0;
          readEndpoint(deviceIp,activeEndpoint,"choicePreset:"+String(slot),body,1200,&status);
          const auto result=classifyA33PresetSelection(status,std::string(body.c_str()));
          sent=result!=PresetSelection::Rejected;
          unconfirmed=result==PresetSelection::Unconfirmed;
          Serial.printf("A33 preset %d HTTP=%d bytes=%u result=%s\n",slot,status,unsigned(body.length()),
              unconfirmed?"unconfirmed":sent?"accepted":"rejected");
        } else if(isWiiMPresetChip()) {
          sent=sendWiiMPreset(slot);
        } else sent=mcuCommand(command) || request("MCUKeyShortClick:"+String(slot),body);
        if (sent) {
        if(isCloud() && !unconfirmed) {presetResumeZone=deviceIp;presetResume.schedule(millis());}
        menuScreen = MenuScreen::Player;
        artworkReady = artworkAttempted = false;
        currentArtworkUrl = "";
        // Let A31 change tracks before fetching its artwork; volume stays live.
        lastArtworkPollAt = millis() - kArtworkPollMs + 2500;
        showFeedback(unconfirmed ? 4 : 3);
        lastPollAt = millis() - kFallbackPollIntervalMs;
        drawScreen();
        return;
        }
      }
      menuMessage = "PRESET FAILED";
      break;
    case MenuScreen::Source:
      presetResume.cancel();
      menuMessage = request("setPlayerCmd:switchmode:" + (sourceCapabilitiesKnown ? discoveredSourceModes[selectedItem] : isCloud() ? (selectedItem == 4 ? String("HDMI ARC") : String(kSourceItems[selectedItem])) : String(kSourceModes[selectedItem])), body)
                        ? "SOURCE SELECTED" : "SOURCE FAILED";
      if (menuMessage == "SOURCE SELECTED") {
        artworkReady = artworkAttempted = false;
        currentArtworkUrl = "";
        lastArtworkPollAt = millis() - kArtworkPollMs;
      }
      break;
    case MenuScreen::Multiroom:
      if (selectedItem == 2 && isCloud()) { menuMessage = refreshGroupStatus() ? (isSlave ? "SLAVE" : groupValue) : "STATUS ERROR"; break; }
      if (selectedItem == 0) { setMenu(MenuScreen::Join); return; }
      if (selectedItem == 1) { setMenu(MenuScreen::LeaveConfirm); return; }
      if (selectedItem == 3) { discoverZones(); return; }
      if (request("getStatusEx", body)) {
        DynamicJsonDocument doc(12288);
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
      if (target >= 0) cancelZoneScan();
      if (target < 0) { discoverZones(); break; }
      if (zones[target].endpoint.chip == Chip::Unknown) { menuMessage = "FIND ZONES FIRST"; break; }
      if ((zones[target].endpoint.chip == Chip::A33) != isCloud()) { menuMessage = "DIFFERENT FAMILY"; break; }
      if (isCloud()) {
        if (!refreshGroupStatus() || isSlave) { menuMessage = "SELECT MASTER"; break; }
        menuMessage = request("multiroom:setHost", body) && requestTo(zones[target].ip, "multiroom:setSlave:" + deviceIp, body, 2500) ? "GROUPED" : "JOIN FAILED";
        lastGroupPollAt = millis() - kGroupPollMs; break;
      }
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
      cancelZoneScan();
      deviceIp = zones[selectedItem].ip;
      applyEndpoint(zones[selectedItem].endpoint);
      settings.putString("device", deviceIp);
      settings.putUChar("chip", uint8_t(activeEndpoint.chip));
      settings.putUShort("port", activeEndpoint.port); settings.putBool("tls", activeEndpoint.tls);
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
      } else if (selectedItem == 1) {
        if (isCloud()) { readGraphicEq(); setMenu(MenuScreen::EqBands); return; }
        setMenu(MenuScreen::EqPresets); return;
      }
      else if (selectedItem == 2) {
        if (activeEndpoint.chip == Chip::A97 || activeEndpoint.chip == Chip::A98) {
          const bool ok = readGraphicEq(); setMenu(MenuScreen::EqBands);
          if (!ok) { menuMessage = "EQ API UNAVAILABLE"; drawMenu(); }
          return;
        }
        const bool ok = readTone();
        const bool bassOk = readVirtualBass();
        setMenu(MenuScreen::ToneList);
        if (!ok || (activeEndpoint.chip == Chip::A31 && !bassOk)) { menuMessage = activeEndpoint.chip == Chip::A31 || isCloud() ? "READ FAILED" : "NO TONE API"; drawMenu(); }
        return;
      } else if (selectedItem == 3 || selectedItem == 4) {
        if (openInformation(selectedItem == 3 ? MenuScreen::DeviceInfo :
                            MenuScreen::NetworkInfo)) return;
      } else if (selectedItem == 5) {
        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t heapFree = ESP.getFreeHeap();
        infoLines[0] = "VERSION " + String(kFirmwareVersion);
        infoLines[1] = "IP:";
        infoLines[2] = WiFi.localIP().toString();
        infoLines[3] = "RAM TOTAL " + String(heapTotal / 1024) + " KB";
        infoLines[4] = "RAM USED " + String((heapTotal - heapFree) / 1024) + " KB";
        infoLines[5] = "RAM FREE " + String(heapFree / 1024) + " KB";
        infoLines[6] = "FIRMWARE " + String(ESP.getSketchSize() / 1024) + " KB";
        infoLineCount = 7;
        setMenu(MenuScreen::MemoryInfo);
        return;
      } else { discoverZones(); return; }
      break;
    case MenuScreen::EqBands:
      if (selectedItem == 11) { menuMessage = readGraphicEq() ? "UPDATED" : "EQ API UNAVAILABLE"; break; }
      if (!graphicKnown) { menuMessage = "REFRESH FIRST"; break; }
      if (selectedItem == 10) {
        const bool wanted = !graphicEnabled;
        menuMessage = request(isCloud() ? String("eqEnable:") + (wanted ? "0010" : "0000") : wanted ? "EQOn" : "EQOff", body) ? "EQ SENT" : "EQ FAILED";
        if (menuMessage == "EQ SENT") graphicEnabled = wanted;
        break;
      }
      editingBand = selectedItem; editingBandValue = graphicBands[editingBand]; toneMessage = "";
      setMenu(MenuScreen::EqBandEdit); return;
    case MenuScreen::EqBandEdit:
      if (saveGraphicBand()) { setMenu(MenuScreen::EqBands); selectedItem = editingBand; menuMessage = "SAVED"; drawMenu(); }
      else { toneMessage = "SAVE FAILED"; drawScreen(); }
      return;
    case MenuScreen::ToneList:
      if (selectedItem == 4) {
        const bool toneOk = readTone();
        const bool bassOk = readVirtualBass();
        menuMessage = toneOk && bassOk ? "UPDATED" : "READ FAILED";
        break;
      }
      if (selectedItem == 3) {
        if (activeEndpoint.chip != Chip::A31) menuMessage = "NOT SUPPORTED";
        else if (!virtualBassKnown) menuMessage = "REFRESH FIRST";
        else menuMessage = writeVirtualBass() ? "CONFIRMED" : "SAVE FAILED";
        break;
      }
      if (!toneKnown[selectedItem]) {
        menuMessage = isCloud() && selectedItem == 1 ? "NO MID API" : "REFRESH FIRST";
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
      menuMessage = isCloud() ? "USE GRAPHIC EQ" : sendA31EqPreset(selectedItem) ? "EQ SENT" : "EQ FAILED";
      break;
    case MenuScreen::LeaveConfirm:
      if (selectedItem == 1) {
        menuMessage = requestTo(deviceIp, isCloud() ? (isSlave ? "multiroom:disconnectSlave" : "multiroom:breakUp") : "multiroom:LeaveGroup", body, 5000)
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
  portENTER_CRITICAL(&encoderMux);
  if (encoderRead == encoderWrite) { portEXIT_CRITICAL(&encoderMux); return; }
  int direction = encoderSteps[encoderRead];
  encoderRead = (encoderRead + 1) & 127;
  int volumeTarget = constrain(volume + direction*kVolumePerDetent, 0, 100);
  if (menuScreen == MenuScreen::Player) {
    // Drain in one snapshot, preserving reversals and clipping at the limits.
    while (encoderRead != encoderWrite) {
      volumeTarget = constrain(volumeTarget + encoderSteps[encoderRead]*kVolumePerDetent, 0, 100);
      encoderRead = (encoderRead + 1) & 127;
    }
  }
  portEXIT_CRITICAL(&encoderMux);
  recordInteraction();
  {
    if (menuScreen == MenuScreen::Scan) return;
    if (menuScreen == MenuScreen::EqBandEdit) {
      editingBandValue = constrain(editingBandValue + direction, isCloud() ? -12 : 0, isCloud() ? 12 : 99);
      toneMessage = ""; drawScreen(); return;
    }
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
    const int next = volumeTarget;
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
        cancelZoneScan();
        setMenu(scanReturnScreen);
      } else if (menuScreen == MenuScreen::DeviceInfo || menuScreen == MenuScreen::NetworkInfo ||
                 menuScreen == MenuScreen::MemoryInfo) {
        setMenu(MenuScreen::Settings);
      } else if (menuScreen != MenuScreen::Player) {
        chooseMenuItem();
      } else {
        presetResume.cancel(); // Manual transport always overrides preset auto-start.
        const bool requestedPlaying = !playing;
        const String command = requestedPlaying ? "setPlayerCmd:resume" : "setPlayerCmd:pause";
        const String tcpCommand = requestedPlaying ? "MCU+PLY-PLA" : "MCU+PLY-PUS";
        if (mcuCommand(tcpCommand) || request(command, body)) {
          playing = requestedPlaying;
          showFeedback(playing ? 1 : 2);
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
    if (scanPhase) cancelZoneScan();
    if (menuScreen == MenuScreen::Scan) {
      cancelZoneScan();
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
    else if (menuScreen == MenuScreen::EqBandEdit) { setMenu(MenuScreen::EqBands); selectedItem = editingBand; drawMenu(); }
    else if (menuScreen == MenuScreen::EqBands) setMenu(MenuScreen::Settings);
    else if (menuScreen == MenuScreen::ToneList) setMenu(MenuScreen::Settings);
    else setMenu(MenuScreen::Home);
    portENTER_CRITICAL(&encoderMux); encoderAccumulator = 0; encoderRead = encoderWrite; portEXIT_CRITICAL(&encoderMux);
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
  attachInterrupt(digitalPinToInterrupt(kEncoderA), captureEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(kEncoderB), captureEncoder, CHANGE);
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
  activeEndpoint.chip = Chip(settings.getUChar("chip", 0));
  activeEndpoint.port = settings.getUShort("port", 80);
  activeEndpoint.tls = settings.getBool("tls", false);
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
  manager.setTitle("airControl KNOB");
  manager.setCustomHeadElement("<style>body{background:#000!important;color:#fff!important;font-family:Arial,sans-serif}div,form{color:#fff}input,select{background:#161616!important;color:#fff!important;border:1px solid #777!important;border-radius:6px}button,input[type=submit]{background:#fff!important;color:#000!important;border:1px solid #fff!important;border-radius:6px}a{color:#fff!important}.q{color:#bbb!important}</style>");
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
  portENTER_CRITICAL(&encoderMux); encoderRead = encoderWrite; encoderAccumulator = 0; portEXIT_CRITICAL(&encoderMux);
  drawScreen();
  Serial.println("airControl KNOB v0.8.16: multi-chip zones");
  Serial.printf("PSRAM: %s, free=%u bytes\n", psramFound() ? "available" : "not detected",
                static_cast<unsigned>(ESP.getFreePsram()));
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {pollA98Events();settingsServer.handleClient();}
  if (rebootAfterResponse) { delay(500); ESP.restart(); }
  if (otaActive) { delay(1); return; }
  if (WiFi.status() == WL_CONNECTED && !scanPhase) handleScreenshot();
  pollEncoder();
  pollButton();
  if (WiFi.status() == WL_CONNECTED) {
    if (!scanPhase && ensureA31Tcp()) pollA31Tcp();
  }
  updateDisplaySleep();
  if (feedbackKind && static_cast<int32_t>(millis() - feedbackUntil) >= 0) { feedbackKind = 0; if (menuScreen == MenuScreen::Player) drawScreen(); }
  if (webScanRequested) {
    webScanRequested = false;
    discoverZones(true);
  }
  if (scanPhase) {
    pollZoneScan();
    sendVolumeWhenReady();
    delay(1); return;
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
        drawWindow(infoLines[selectedItem], 30, 59 + (selectedItem-firstInfoLine()) * 29, 18, 1,
                   marqueeOffset, TFT_WHITE, TFT_BLACK);
      else if (menuScreen != MenuScreen::Player)
        drawSansMenu(menuLabel(selectedItem), 32, 99, 176, 26, true,
                     marqueeOffset, TFT_WHITE, display.color565(61, 61, 61));
      else drawMetadata();
    }
  }
  sendVolumeWhenReady();
  servicePresetResume();
  if(millis()-lastA31ActivityAt>=800) maintainA98Subscriptions();
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
