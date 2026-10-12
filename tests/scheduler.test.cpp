#include "../src/discovery_scheduler.h"
#include <cassert>
#include <iostream>
int main() {
 DiscoveryLedger d;
 d.offer(35,80,0);d.offer(35,443,0);assert(d.next(0)==35);assert(d.ports[35]==80);
 d.begin(35);d.finish(35,false,100);assert(d.next(799)==-1);assert(d.next(800)==35);
 d.begin(35);d.finish(35,false,900);assert(!d.hasWork());d.offer(35,80,901);assert(!d.hasWork());assert(d.attempts[35]==2);
 d=DiscoveryLedger{};d.offer(35,80,0);d.begin(35);d.finish(35,true,10);d.offer(35,80,20);assert(!d.hasWork());assert(d.verified[35]);
 d=DiscoveryLedger{};for(int h=1;h<255;++h)d.offer(h,443,0);
 for(int h=1;h<255;++h){assert(d.next(0)==h);d.begin(h);d.finish(h,true,0);}assert(!d.hasWork());
 d=DiscoveryLedger{};d.offer(35,80,0xfffffff0u);d.begin(35);d.finish(35,false,0xfffffff0u);assert(d.next(100)==-1);assert(d.next(684)==35);
 std::cout<<"Discovery ledger: duplicate suppression, bounded retry, verified hosts, full subnet, restart and timer wrap passed\n";
}
