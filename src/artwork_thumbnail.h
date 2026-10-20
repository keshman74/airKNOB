#pragma once
#include <string>
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
