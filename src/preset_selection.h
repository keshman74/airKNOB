#pragma once
#include <string>
#include <algorithm>
#include <cctype>
enum class PresetSelection {Rejected,Accepted,Unconfirmed};
inline bool explicitApiRejection(std::string body) {
  std::transform(body.begin(),body.end(),body.begin(),[](unsigned char c){return char(std::tolower(c));});
  const auto first=body.find_first_not_of(" \r\n\t\"");
  const auto last=body.find_last_not_of(" \r\n\t\"");
  body=first==std::string::npos?"":body.substr(first,last-first+1);
  return body.find("unknown comman")!=std::string::npos || body=="error" || body=="failed" || body=="fail" || body=="format" || body=="exceed" || body=="unsupported" || body=="false";
}
inline PresetSelection classifyA33PresetSelection(int status,std::string body) {
  if(status==200) {
    if(explicitApiRejection(body)) return PresetSelection::Rejected;
    // A33 Not can mean selected; an empty HTTP 200 is not a rejection either.
    return PresetSelection::Accepted;
  }
  // HTTPClient connection-lost / read-timeout: the device may have acted.
  if(status==-5 || status==-11 || status>=500) return PresetSelection::Unconfirmed;
  return PresetSelection::Rejected;
}
