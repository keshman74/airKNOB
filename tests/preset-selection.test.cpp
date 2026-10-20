#include "preset_selection.h"
#include <cassert>
#include <iostream>
int main(){
 for(const char*body:{"OK","Not",""," \r\n","\"Not\"","not"})assert(classifyA33PresetSelection(200,body)==PresetSelection::Accepted);
 for(const char*body:{"unknown command","unknown comman&#x64;","Error","Failed","FAIL","Format","Exceed","Unsupported","false"})assert(classifyA33PresetSelection(200,body)==PresetSelection::Rejected);
 for(int status:{-5,-11,500,503})assert(classifyA33PresetSelection(status,"")==PresetSelection::Unconfirmed);
 for(int status:{0,-1,-2,400,401,404})assert(classifyA33PresetSelection(status,"")==PresetSelection::Rejected);
 std::cout<<"A33 selection: Not/empty200 accepted, lost response uncertain, explicit rejection preserved passed\n";
}
