"""Check the production insertion/eviction routine, including negative RSSI."""
from pathlib import Path
import os
import subprocess
import tempfile
s=(Path(__file__).resolve().parents[1]/'esp32_marauder/bt_core.cpp').read_text()
a=s.index('static int dev_add('); b=s.index('\nvoid bt_scan_add_sink',a)
source=s[a:b]
harness=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define BT_MAX_DEVS 4
struct bt_dev_t {uint8_t mac[6],addrType;int8_t rssi;uint32_t firstSeen,lastSeen;};
bt_dev_t s_devs[BT_MAX_DEVS];int s_devCount=0;uint32_t now=10;
uint32_t millis(){return now;}
'''
tests=r'''
int main(){
 uint8_t mac[6]={};
 for(int i=0;i<4;++i){mac[5]=i;now+=10;assert(dev_add(mac,1)==i);}
 assert(s_devCount==4 && s_devs[0].rssi==INT8_MIN);
 const int rssi=-67;if(rssi>s_devs[0].rssi)s_devs[0].rssi=rssi;
 assert(s_devs[0].rssi==-67);
 mac[5]=9;++now;assert(dev_add(mac,0)==0);assert(s_devCount==4);
 assert(s_devs[0].mac[5]==9 && s_devs[0].addrType==0);
 assert(s_devs[1].mac[5]==1 && s_devs[3].mac[5]==3);
 mac[5]=10;++now;assert(dev_add(mac,1)==1);assert(s_devCount==4);
 assert(s_devs[0].mac[5]==9 && s_devs[3].mac[5]==3);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(harness+source+tests);exe=Path(tmp)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('BLE device table checks passed')
