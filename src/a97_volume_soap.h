#pragma once
// Worker-owned A97 absolute volume path. Confirmed on the tested A97 at
// HTTP :49152 /upnp/control/rendercontrol1; other commands remain HTTPS.
class A97VolumeSoap {
  String ip_;
  uint32_t epoch_=0,retryAt_=0;
  bool blocked_=false;
  static bool element(const String& body,const char* wanted) {
    const std::string xml(body.c_str());
    size_t pos=0;
    while((pos=xml.find('<',pos))!=std::string::npos) {
      ++pos;if(pos>=xml.size()) break;
      if(xml[pos]=='/' || xml[pos]=='!' || xml[pos]=='?') continue;
      const size_t end=xml.find_first_of(" \t\r\n/>",pos);
      if(end==std::string::npos) return false;
      std::string name=xml.substr(pos,end-pos);
      const size_t colon=name.find(':');
      if(colon!=std::string::npos) name=name.substr(colon+1);
      if(name==wanted) return true;
      pos=end;
    }
    return false;
  }
 public:
  std::atomic<uint32_t> attempts{0},successes{0};
  std::atomic<int> lastHttp{0};
  bool set(const String& ip,uint32_t epoch,int volume) {
    if(volume<0 || volume>100) return false;
    if(ip_!=ip || epoch_!=epoch) {ip_=ip;epoch_=epoch;blocked_=false;}
    if(blocked_ && int32_t(millis()-retryAt_)<0) return false;
    IPAddress address;
    if(WiFi.status()!=WL_CONNECTED || !address.fromString(ip)) return false;
    WiFiClient client;HTTPClient http;
    http.useHTTP10(true);http.setConnectTimeout(250);http.setTimeout(500);
    attempts.fetch_add(1);lastHttp.store(0);
    bool ok=false;
    if(http.begin(client,"http://"+ip+":49152/upnp/control/rendercontrol1")) {
      http.addHeader("Content-Type","text/xml; charset=utf-8");
      http.addHeader("SOAPAction","\"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume\"");
      const String body="<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
        "<u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
        "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>"+
        String(volume)+"</DesiredVolume></u:SetVolume></s:Body></s:Envelope>";
      const int status=http.POST(body);lastHttp.store(status);
      String reply;
      ok=status==200 && readBoundedResponse(http,reply,4096,1000) &&
         !element(reply,"Fault") && element(reply,"SetVolumeResponse");
      http.end();
    }
    if(ok) {successes.fetch_add(1);blocked_=false;}
    else {blocked_=true;retryAt_=millis()+30000;}
    return ok;
  }
};
