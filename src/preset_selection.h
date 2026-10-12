#pragma once
#include <string>
#include <algorithm>
#include <cctype>
enum class PresetSelection {Rejected,Accepted,Unconfirmed};
inline PresetSelection classifyA33PresetSelection(int status,std::string body) {
  std::transform(body.begin(),body.end(),body.begin(),[](unsigned char c){return char(std::tolower(c));});
  const auto first=body.find_first_not_of(" \r\n\t\"");
  const auto last=body.find_last_not_of(" \r\n\t\"");
  body=first==std::string::npos?"":body.substr(first,last-first+1);
  if(status==200) {
    if(body.find("unknown comman")!=std::string::npos || body=="error" || body=="failed" || body=="unsupported" || body=="false") return PresetSelection::Rejected;
    // A33 Not can mean selected; an empty HTTP 200 is not a rejection either.
    return PresetSelection::Accepted;
  }
  // HTTPClient connection-lost / read-timeout: the device may have acted.
  if(status==-5 || status==-11 || status>=500) return PresetSelection::Unconfirmed;
  return PresetSelection::Rejected;
}
