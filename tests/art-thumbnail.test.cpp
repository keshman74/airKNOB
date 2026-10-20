#include "artwork_thumbnail.h"
#include <cassert>
#include <iostream>
int main(){
 assert(artworkThumbnail("https://static.qobuz.com/images/covers/bf/q1/album_600.jpg?token=a#x")=="https://static.qobuz.com/images/covers/bf/q1/album_150.jpg?token=a#x");
 assert(artworkThumbnail("http://cdn-profiles.tunein.com/s227824/images/logog.png?t=123")=="http://cdn-profiles.tunein.com/s227824/images/logoq.png?t=123");
 for(const char*s:{"https://static.qobuz.com.evil/images/covers/a_600.jpg","https://static.qobuz.com@evil/images/covers/a_600.jpg","https://static.qobuz.com/other/a_600.jpg","https://static.qobuz.com/images/covers/a_150.jpg","http://cdn-profiles.tunein.com/sfoo/images/logog.png","http://cdn-profiles.tunein.com/s123/images/logoq.png","http://192.168.0.121:17890/radio-cover/id","https://publicfile.linkplay.com/radio-icons/a.png"})assert(artworkThumbnail(s)==s);
 std::cout<<"Thumbnail rules: exact CDN host/path, query preservation and unchanged other sources passed\n";
}
