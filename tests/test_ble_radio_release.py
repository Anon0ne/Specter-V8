"""Exercise production BLE release ordering and deinit failure handling."""
from pathlib import Path
import os,subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'esp32_marauder/bt_core.cpp').read_text()
a=s.index('void bt_radio_end()');b=s.index('\nvoid bt_set_radio_hooks',a)
source=s[a:b]
harness=r'''
#include <cassert>
#include <cstdint>
#include <vector>
std::vector<int> events;
bool s_hostReady=false,ok=true,server=false;
int scannerGeneration=0,hidGeneration=0;
void bt_forget_scan_host(){++scannerGeneration;events.push_back(7);}
void bt_badble_host_released(){++hidGeneration;events.push_back(6);}
void post(){events.push_back(5);}
void (*s_wifi_post)(void)=post;
void bt_stop_all(){events.push_back(1);}
void bt_tx_stop(){events.push_back(2);}
void bt_scan_stop(){events.push_back(3);}
struct ESPClass {uint32_t getFreeHeap(){return s_hostReady?20000:100000;}} ESP;
struct SerialClass {
 void println(const char*){}
 template<class... T>void printf(const char*,T...){}
} Serial;
struct NimBLEDevice {
 static void* getServer(){return server?reinterpret_cast<void*>(1):nullptr;}
 static bool deinit(bool clear){events.push_back(clear?8:4);if(clear){assert(ok);server=false;}return ok;}
};
'''
tests=r'''
int main(){
 bt_radio_end();assert(events.empty());
 s_hostReady=true;ok=false;bt_radio_end();
 assert(s_hostReady && events==std::vector<int>({1,2,3,4}));
 events.clear();ok=true;bt_radio_end();
 assert(!s_hostReady && events==std::vector<int>({1,2,3,4,5}));
 events.clear();bt_radio_end();assert(events.empty());
 // A retained HID/server forces native object invalidation, after successful stop.
 s_hostReady=true;server=true;ok=false;bt_radio_end();
 assert(s_hostReady && server && events==std::vector<int>({1,2,3,4}));
 assert(scannerGeneration==0 && hidGeneration==0);
 events.clear();ok=true;bt_radio_end();
 assert(!s_hostReady && !server && events==std::vector<int>({1,2,3,4,8,6,7,5}));
 assert(scannerGeneration==1 && hidGeneration==1);
 // Repeating a server lifecycle always invalidates both caches once per teardown.
 events.clear();s_hostReady=true;server=true;bt_radio_end();
 assert(scannerGeneration==2 && hidGeneration==2);

}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(harness+source+tests);exe=Path(tmp)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('BLE radio release checks passed')
