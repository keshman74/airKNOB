#pragma once
#include <string>

// A31 AirPlay advertises a short-lived root-relative artwork object through
// UPnP. The UPnP control port is not the artwork origin: fetch this exact
// path family from the player's normal HTTP origin (port 80).
inline bool isA31AirPlayArtworkPath(const std::string& path) {
  static const std::string prefix = "/data/airplay_";
  static const std::string suffix = ".jpg";
  if (path.size() != prefix.size() + 32 + suffix.size() ||
      path.compare(0, prefix.size(), prefix) != 0 ||
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
  for (size_t i = prefix.size(); i < prefix.size() + 32; ++i) {
    const char c = path[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

// Resolves root-relative artwork while preserving absolute service URLs.
inline std::string resolveKnobArtworkUrl(const std::string& url, const std::string& ip,
                                         unsigned short port, bool tls, bool a31) {
  if (url.empty() || url[0] != '/') return url;
  if (a31 && isA31AirPlayArtworkPath(url)) return "http://" + ip + url;
  return std::string(tls ? "https://" : "http://") + ip + ":" +
         std::to_string(port) + url;
}
// Only exact CDN hosts and observed path patterns. Preserve query parameters.
inline std::string artworkThumbnail(const std::string& url) {
  const auto scheme=url.find("://");
  if(scheme==std::string::npos || (url.substr(0,scheme)!="http" && url.substr(0,scheme)!="https")) return url;
  const auto start=scheme+3,slash=url.find('/',start);
  if(slash==std::string::npos) return url;
  const auto host=url.substr(start,slash-start);
  const auto end=url.find_first_of("?#",slash);
  std::string path=url.substr(slash,end==std::string::npos?end:end-slash);
  const std::string suffix=end==std::string::npos?"":url.substr(end);
  if(host=="static.qobuz.com" && path.compare(0,15,"/images/covers/")==0) {
    const auto pos=path.rfind('_');
    if(pos!=std::string::npos) {
      const auto tail=path.substr(pos);
      if(tail=="_600.jpg" || tail=="_1200.jpg" || tail=="_max.jpg" || tail=="_300.jpg")
        path=path.substr(0,pos)+"_150.jpg";
    }
  } else if(host=="cdn-profiles.tunein.com" && path.size()>2 && path[0]=='/' && path[1]=='s') {
    size_t i=2;while(i<path.size() && path[i]>='0' && path[i]<='9')++i;
    if(i>2 && path.substr(i)=="/images/logog.png") path=path.substr(0,i)+"/images/logoq.png";
  }
  return url.substr(0,slash)+path+suffix;
}
