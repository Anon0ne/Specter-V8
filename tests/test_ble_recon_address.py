"""Verify the production recon address expression against installed NimBLE constructors."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
lib=Path(os.environ.get('NIMBLE_LIBRARY_DIR',str(Path.home()/'Arduino/libraries/NimBLE-Arduino')))
address=lib/'src/NimBLEAddress.cpp'
if not address.exists():
    print('SKIP: NimBLE-Arduino SDK not installed (set NIMBLE_LIBRARY_DIR)')
    raise SystemExit(0)
sdk=address.read_text()
copy=next(line for line in sdk.splitlines() if line.startswith('NimBLEAddress::NimBLEAddress(ble_addr_t'))
a=sdk.index('NimBLEAddress::NimBLEAddress(const uint8_t address[')
b=sdk.index('} // NimBLEAddress',a)+1
array=sdk[a:b]
compat=(root/'esp32_marauder/bt_compat.h').read_text()
helper=compat[compat.index('static inline NimBLEAddress bt_address_from_native'):]
recon=(root/'esp32_marauder/bt_predator.cpp').read_text()
expression=re.search(r'^    NimBLEAddress addr[^\n]*;',recon,re.M).group(0)
connect_expression=re.search(r'^    const bool connected = cl->connect[^\n]*;',recon,re.M).group(0)
harness=r'''#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>
#define BLE_DEV_ADDR_LEN 6
struct ble_addr_t {uint8_t type;uint8_t val[6];};
class NimBLEAddress:private ble_addr_t {
public:
 NimBLEAddress(ble_addr_t);
 NimBLEAddress(const uint8_t[6],uint8_t);
 const uint8_t* getVal()const{return val;}
 uint8_t getType()const{return type;}
};
'''+copy+'\n'+array+'\n'+helper+r'''
struct Device {uint8_t mac[6];uint8_t addrType;};
NimBLEAddress reconAddress(const Device* d){
'''+expression+r'''
 return addr;
}
// Model the SDK's MTU-stage EALREADY while the underlying link is usable.
struct NimBLEClient {
 bool link=false;
 bool connect(const NimBLEAddress&,bool=true,bool=false,bool exchangeMTU=true){link=true;return !exchangeMTU;}
};
bool reconConnect(NimBLEClient* cl,const NimBLEAddress& addr){
'''+connect_expression+r'''
 return connected;
}
int main(){
 for(uint8_t type=0;type<4;++type){
  Device d={{0xf6,0xe5,0xd4,0xc3,0xb2,0xa1},type};
  auto got=reconAddress(&d);
  assert(!memcmp(got.getVal(),d.mac,6) && got.getType()==type);
  NimBLEClient client;assert(reconConnect(&client,got) && client.link);
  // Negative control: the old pointer constructor actually reverses native bytes.
  NimBLEAddress wrong(d.mac,type);
  assert(memcmp(wrong.getVal(),d.mac,6)!=0);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'test.cpp';src.write_text(harness);exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('Production GATT address preserves native bytes and address type with installed NimBLE SDK')
