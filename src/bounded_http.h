#pragma once
// HTTPClient handles Content-Length and chunk framing; this sink bounds memory
// and total body time. Do not parse an incomplete response as a valid snapshot.
class BoundedHttpSink : public Stream {
  String& body_;
  size_t limit_;
  uint32_t started_, budget_;
 public:
  bool failed=false;
  BoundedHttpSink(String& body,size_t limit,uint32_t budget)
      :body_(body),limit_(limit),started_(millis()),budget_(budget){}
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
  size_t write(uint8_t b) override {return write(&b,1);}
  size_t write(const uint8_t* bytes,size_t count) override {
    if(failed || millis()-started_>=budget_ || count>limit_-body_.length() ||
       !body_.concat(reinterpret_cast<const char*>(bytes),count)) {
      failed=true;return 0;
    }
    return count;
  }
};
inline bool readBoundedResponse(HTTPClient& http,String& body,size_t limit=65536,uint32_t budget=5000) {
  body="";
  const int expected=http.getSize();
  if(expected>int(limit) || !body.reserve(expected>0?size_t(expected):1024)) return false;
  BoundedHttpSink sink(body,limit,budget);
  const int received=http.writeToStream(&sink);
  if(received<0 || sink.failed || (expected>=0 && body.length()!=size_t(expected))) {
    body="";return false;
  }
  return true;
}
