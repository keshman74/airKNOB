#include "artwork_thumbnail.h"
#include <cassert>
#include <iostream>
int main(){
 assert(artworkThumbnail("https://static.qobuz.com/images/covers/bf/q1/album_600.jpg?token=a#x")=="https://static.qobuz.com/images/covers/bf/q1/album_150.jpg?token=a#x");
 assert(artworkThumbnail("http://cdn-profiles.tunein.com/s227824/images/logog.png?t=123")=="http://cdn-profiles.tunein.com/s227824/images/logoq.png?t=123");
 for(const char*s:{"https://static.qobuz.com.evil/images/covers/a_600.jpg","https://static.qobuz.com@evil/images/covers/a_600.jpg","https://static.qobuz.com/other/a_600.jpg","https://static.qobuz.com/images/covers/a_150.jpg","http://cdn-profiles.tunein.com/sfoo/images/logog.png","http://cdn-profiles.tunein.com/s123/images/logoq.png","http://192.168.0.121:17890/radio-cover/id","https://publicfile.linkplay.com/radio-icons/a.png"})assert(artworkThumbnail(s)==s);
 const std::string airplayPath="/data/airplay_f67a80954965fd2f79a144f41d4bbe38.jpg";
 assert(isA31AirPlayArtworkPath(airplayPath));
 assert(isA31AirPlayArtworkPath("/data/airplay_3DC11F6BACAF2037BE977BBCF2CCC5A1.jpg"));
 assert(!isA31AirPlayArtworkPath("/data/airplay_bad.jpg"));
 assert(!isA31AirPlayArtworkPath("/data/airplay_f67a80954965fd2f79a144f41d4bbe38.png"));
 assert(resolveKnobArtworkUrl(airplayPath,"192.168.0.35",80,false,true)=="http://192.168.0.35"+airplayPath);
 assert(resolveKnobArtworkUrl(airplayPath,"192.168.0.21",443,true,false)=="https://192.168.0.21:443"+airplayPath);
 assert(resolveKnobArtworkUrl("https://static.qobuz.com/images/covers/a_600.jpg","192.168.0.35",80,false,true)=="https://static.qobuz.com/images/covers/a_600.jpg");
 assert(resolveKnobArtworkUrl("http://cdn-profiles.tunein.com/s123/images/logog.png?t=1","192.168.0.35",80,false,true)=="http://cdn-profiles.tunein.com/s123/images/logog.png?t=1");
 std::cout<<"Thumbnail rules: exact CDN host/path, query preservation and unchanged other sources passed\n";
}
