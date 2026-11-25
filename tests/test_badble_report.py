"""Exercise production keyboard sends: distinct press/release snapshots and failures."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
s=(root/'esp32_marauder/bt_badble.cpp').read_text()
a=s.index('bool report(const uint8_t');b=s.index('void release()',a)
report=s[a:b]
harness=r'''#include <atomic>
#include <array>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstring>
bool connected=true,subscribed=true,encrypted=true;
std::atomic<uint16_t> peerHandle{42};
std::atomic<uint32_t> queuedReports{0};std::atomic<int> reportError{0};
struct Characteristic {
 uint8_t value[8]={};bool fail=false;std::vector<std::array<uint8_t,8>> queue;
 void setValue(const uint8_t* bytes,size_t n){assert(n==8);memcpy(value,bytes,n);}
 bool notify(const uint8_t* bytes,size_t n,uint16_t peer){
  assert(n==8&&peer==42);if(fail)return false;
  std::array<uint8_t,8> copy{};memcpy(copy.data(),bytes,n);queue.push_back(copy);return true;
 }
 // The original zero-argument path coalesces all pending values.
 bool notify(){queue.clear();std::array<uint8_t,8> copy{};memcpy(copy.data(),value,8);queue.push_back(copy);return true;}
} characteristic;
Characteristic* input=&characteristic;
''' + report + r'''
int main(){
 uint8_t press[8]={2,0,11,0,0,0,0,0},release[8]={};
 assert(report(press));assert(report(release));
 assert(queuedReports==2&&characteristic.queue.size()==2);
 assert(characteristic.queue[0][0]==2&&characteristic.queue[0][2]==11);
 for(auto byte:characteristic.queue[1])assert(byte==0);
 characteristic.fail=true;assert(!report(press));assert(reportError==-1&&queuedReports==2);
 characteristic.fail=false;connected=false;assert(!report(press));assert(queuedReports==2);
 connected=true;subscribed=false;assert(!report(press));subscribed=true;encrypted=false;assert(!report(press));
}
'''
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'test.cpp';src.write_text(harness);exe=Path(tmp)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('Keyboard report snapshots, paired connection routing, gating and send failures passed')
