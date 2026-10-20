#pragma once
// Included after Arduino/HTTPClient and encodeCommand(). Worker-owned only.
// HTTPClient 2.0.17 honours server Connection: close and decodes chunked bodies.
// No automatic replay: only the UI may retry its current absolute volume target.
class WiiMWorkerHttp {
  WiFiClientSecure secure_;
  HTTPClient http_;
  String ip_;
  uint16_t port_=0;
  uint32_t epoch_=0;
 public:
  std::atomic<uint32_t> requests{0},reused{0};
  void close() {
    http_.setReuse(false);http_.end();secure_.stop();
    ip_="";port_=0;epoch_=0;
  }
  bool read(const String& ip,uint16_t port,uint32_t epoch,const String& command,
            String& response,uint32_t timeout) {
    response="";
    IPAddress address;
    if(WiFi.status()!=WL_CONNECTED || !address.fromString(ip)) {close();return false;}
    if(ip_!=ip || port_!=port || epoch_!=epoch) close();
    ip_=ip;port_=port;epoch_=epoch;
    secure_.setInsecure();secure_.setHandshakeTimeout(2);
    const bool existing=secure_.connected();
    http_.useHTTP10(false);http_.setReuse(true);
    http_.setConnectTimeout(350);http_.setTimeout(timeout);
    const String url="https://"+ip+":"+String(port)+"/httpapi.asp?command="+encodeCommand(command);
    if(!http_.begin(secure_,url)) {close();return false;}
    requests.fetch_add(1);
    const int status=http_.GET();
    const bool complete=status==200 && readBoundedResponse(http_,response,65536,1500);
    const bool ok=complete && response.length() &&
      !(response.length()<128 && explicitApiRejection(std::string(response.c_str())));
    if(!ok) close();
    else {if(existing) reused.fetch_add(1);http_.end();}
    return ok;
  }
};
