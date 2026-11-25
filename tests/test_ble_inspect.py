"""Host checks for the actual inspector: paging, identity checks, gates and touch."""
from pathlib import Path
import os
import runpy
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
base=runpy.run_path(str(root/'tests/test_ble_touch.py'))['stubs']
base='\n'.join(l for l in base.splitlines() if not l.startswith('namespace specter '))
base=base.replace('#include <algorithm>','#include <algorithm>\n#include <vector>\n#include <type_traits>\n#include <sstream>\n#define HEX 16\n#define DEVICE 17')
base=base.replace('String substring(size_t a,size_t b) const {return substr(a,b-a);}',r'''
 template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type =0>
 String(T v,int radix=10){std::ostringstream out;if(radix==16)out<<std::hex;out<<+v;assign(out.str());}
 String substring(size_t a,size_t b) const {return substr(a,b-a);}
 String substring(size_t a) const {return substr(a);}
''')
base='\n'.join(line for line in base.splitlines() if not line.startswith('bool bt_command('))
base=base.replace('void drawCentreString(const char*,int,int,int){}','void drawCentreString(const String&,int,int,int){}')
base+=r'''
namespace specter {int resumes=0;void resumeBleScreen(){++resumes;}}
struct bt_dev_t {uint8_t mac[6]={},addrType=0,flags=0;int8_t rssi=-55;char name[20]="Test";};
bt_dev_t devs[8];int deviceCount=0;
int bt_dev_count(){return deviceCount;}
bt_dev_t* bt_dev_get(uint8_t i){return i<deviceCount?devs+i:nullptr;}
String bt_mac_str(const uint8_t* mac){return "mac-"+String(mac[5]);}
struct BtGattCharInfo {uint16_t handle=1;String svcUuid,chrUuid,valueHex;uint8_t props=2;};
std::vector<BtGattCharInfo> table;
std::vector<BtGattCharInfo>* bt_predator_recon_table(){return &table;}
bool reconDone=false,reconSuccess=true,armed=false,red=false;int reconCalls=0;
bool bt_predator_recon_done(){return reconDone;}
String bt_predator_recon_error(){return "Connect failed (NimBLE 13)";}
bool bt_is_armed(){return armed;}
namespace hale {bool offensiveAllowed(){return red;}}
bool bt_command(const String& cmd){
 assert(cmd=="predator recon 0");++reconCalls;reconDone=reconSuccess;
 table.clear();if(reconSuccess)table.resize(2);return true;
}
'''
source=(root/'esp32_marauder/BleInspect.cpp').read_text()
source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
tests=r'''
void touch(bool down,int x=0,int y=0){
 display_obj.touch=down;display_obj.x=x;display_obj.y=y;
 assert(specter::bleInspectLoop(0));
}
int main(){
 using namespace specter;
 assert(displayUuid("0018")=="1800");
 assert(displayUuid("ea0b1032de01b08e1448668333122cfe")=="fe2c1233-8366-4814-8eb0-01de32100bea");
 assert(displayUuid("custom")=="custom");
 showBleDevices();assert(inspectPage==InspectPage::LIST && stops==1);
 // Opening finger and drag-out must not activate Back.
 touch(true,40,290);touch(false,40,290);assert(inspectPage==InspectPage::LIST);
 touch(true,40,290);touch(true,40,230);touch(false,40,230);
 assert(inspectPage==InspectPage::LIST);
 inspectAct(1);assert(deviceOffset==0);
 inspectAct(2);assert(deviceOffset==0); // empty page
 deviceCount=5;for(int i=0;i<deviceCount;++i){devs[i].mac[5]=i;devs[i].flags=0x80;}
 inspectAct(2);assert(deviceOffset==4);inspectAct(2);assert(deviceOffset==4);
 inspectAct(1);assert(deviceOffset==0);
 inspectAct(100);assert(selected==0 && inspectPage==InspectPage::TARGET_DETAIL);
 inspectAct(3);assert(reconCalls==0 && inspectNotice=="Recon requires Red Team mode");
 red=true;inspectAct(3);assert(reconCalls==0 && inspectNotice=="Arm BT+ from the menu first");
 armed=true;devs[0].flags=0;inspectAct(3);assert(reconCalls==0);
 devs[0].flags=0x80;devs[0].mac[5]=99;inspectAct(3);
 assert(reconCalls==0 && inspectNotice=="Device changed; choose again");
 devs[0].mac[5]=0;reconSuccess=false;inspectAct(3);
 assert(reconCalls==1 && inspectPage==InspectPage::TARGET_DETAIL && inspectNotice=="Connect failed (NimBLE 13)");
 reconSuccess=true;inspectAct(3);assert(reconCalls==2 && inspectPage==InspectPage::RESULT);
 inspectAct(1);assert(characteristic==0);inspectAct(2);assert(characteristic==1);
 inspectAct(2);assert(characteristic==1);table.clear();inspectDraw(); // empty result safe
 inspectAct(4);assert(inspectPage==InspectPage::LIST);
 inspectAct(4);assert(!bleInspectLoop(0) && resumes==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'inspect.cpp';cpp.write_text(base+source+tests)
    exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('BLE inspector checks passed')
