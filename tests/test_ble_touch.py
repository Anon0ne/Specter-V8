"""Exercise the real BLE modal with fake TFT, touch and module status."""
from pathlib import Path
import os
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'esp32_marauder/BleTouch.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
stubs = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <algorithm>
#define HAS_BT
#define HAS_SCREEN
#define HAS_TOUCH
#define HAS_ILI9341
#define SCREEN_ORIENTATION 0
#define BT_MOD_BADBLE 99
#define BT_UI_MAX_LINES 8
#define BT_MOD_PREDATOR 5
namespace specter {int deviceOpens=0;void showBleDevices(){++deviceOpens;}}
#define TFT_WHITE 1
#define TFT_BLACK 0
#define TFT_CYAN 2
#define TFT_GREEN 3
#define TFT_YELLOW 4
#define TFT_LIGHTGREY 5
#define TFT_ORANGE 6
#define TFT_DARKGREY 7
struct String:std::string {
 using std::string::string;
 String(const std::string& s):std::string(s){}
 String substring(size_t a,size_t b) const {return substr(a,b-a);}
 size_t length()const{return size();}
};
uint32_t clockNow=0;
uint32_t millis(){return clockNow;}
struct Menu{};
struct TFT {
 int rotation=0;
 int width(){return 240;} int height(){return 320;}
 void setFreeFont(void*){} void setTextFont(int){} void setTextSize(int){}
 void setTextColor(int,int){} void setTextWrap(bool){} void setCursor(int,int){}
 void print(const String&){} void fillRect(int,int,int,int,int){}
 void drawRoundRect(int,int,int,int,int,int){}
 void drawCentreString(const char*,int,int,int){}
 void setRotation(int r){rotation=r;}
};
struct Display {
 TFT tft;
 bool exit_draw=true,printing=true,loading=true,touch=false;
 uint16_t x=0,y=0;
 int updateTouch(uint16_t* a,uint16_t* b){*a=x;*b=y;return touch;}
 void clearScreen(){} void setCalData(bool){}
 void init(){tft.rotation=99;}
} display_obj;
struct MenuFunctions {
 Menu* current_menu=nullptr; int restores=0;
 void drawStatusBar(){}
 void changeMenu(Menu* m,bool){current_menu=m;++restores;}
} menu_function_obj;
struct WiFiScan {bool orient_display=true;} wifi_scan_obj;
struct BtUiState {
 const char* title=nullptr;bool running=false;const char* phase=nullptr;
 int module=0;String lines[8];uint8_t lineCount=0;
};
bool running=false,handle=true;int statusModule=0;
bool keyboardReady=true,keyboardTyping=false;
int scriptSelections=0;
bool bt_badble_ready(){return keyboardReady;}
bool bt_badble_typing(){return keyboardTyping;}
size_t bt_badble_scripts(String* names,size_t cap,size_t offset,bool& more){
 size_t n=0;more=false;for(size_t i=offset;i<8;++i){if(n==cap){more=true;break;}names[n++]=i==0?"demo-hello":"test.txt";}return n;
}
bool bt_badble_select(const String&){++scriptSelections;return true;}
String bt_badble_selected(){return "demo-hello";}

int helloRequests=0;
bool bt_badble_run_selected(){++helloRequests;return true;}
int stops=0,releases=0;
void bt_radio_end(){++releases;}
bool bt_stop_all(){++stops;running=false;return true;}
bool bt_command(const String& c){if(c=="bb hello"){++helloRequests;return true;}running=handle;return handle;}
bool bt_ui_get(BtUiState& out){
 if(!running)return false;
 out.module=statusModule;out.phase="scanning";out.lineCount=1;out.lines[0]="frames: 7";return true;
}
'''
tests = r'''
void tick(bool down,int x=0,int y=0){
 display_obj.touch=down;display_obj.x=x;display_obj.y=y;
 clockNow+=260;assert(specter::bleTouchLoop(clockNow));
}
int main(){
 Menu menu;menu_function_obj.current_menu=&menu;
 // Opening press cannot activate Stop or Back.
 specter::showBle("tracker scan 30 log","Tracker");
 assert(specter::job && specter::observed);
 int before=stops;tick(true,180,290);tick(false,180,290);
 assert(stops==before && menu_function_obj.restores==0);
 // Press-drag away-release must not activate Back.
 tick(true,180,290);tick(true,180,200);tick(false,180,200);
 assert(menu_function_obj.restores==0);
 // Stop keeps results and the modal; Back stops and restores calibration/orientation.
 tick(true,40,290);tick(false,40,290);
 assert(!specter::job && specter::active && specter::observed);
 assert(specter::message=="Stopped" && specter::snapshot.lines[0]=="frames: 7");
 tick(true,180,290);tick(false,180,290);
 assert(!specter::active && menu_function_obj.restores==1 && releases==1);
 assert(display_obj.tft.rotation==SCREEN_ORIENTATION);
 assert(!display_obj.exit_draw && !display_obj.printing && !display_obj.loading);
 assert(!specter::bleTouchLoop(clockNow));
 // Natural completion retains an explicitly labelled last-observed snapshot.
 specter::showBle("tracker scan 30 log","Tracker");running=false;
 tick(false);assert(!specter::job && specter::observed);
 assert(specter::message=="Finished / idle");
 // Blocked/error screens have a working Back, without stale scan results.
 specter::showBleMessage("Unavailable","Red Team mode only");
 assert(!specter::job && !specter::observed);tick(false);
 tick(true,180,290);tick(false,180,290);assert(!specter::active);
 handle=false;specter::showBle("invalid","Unavailable");
 assert(!specter::job && specter::message=="Command unavailable");
 handle=true;statusModule=BT_MOD_PREDATOR;specter::showBle("predator scan 20","Predator");
 tick(false);tick(true,40,220);tick(false,40,220);
 assert(specter::deviceOpens==1 && !specter::job);
 specter::resumeBleScreen();assert(specter::waitRelease);
 // Pair/ready status never types. Only a released press in Send hello does.
 statusModule=BT_MOD_BADBLE;specter::showBle("bb serve","Keyboard");
 tick(false);assert(helloRequests==0);
 tick(true,180,220);tick(false,180,220);assert(helloRequests==1);
 // Selection and page navigation do not run; Back from picker preserves keyboard.
 tick(true,40,220);tick(false,40,220);assert(specter::scriptPicker && specter::scriptMore);
 tick(true,180,240);tick(false,180,240);assert(specter::scriptOffset==5);
 tick(true,40,80);tick(false,40,80);assert(scriptSelections==1 && !specter::scriptPicker && helloRequests==1);
 tick(true,40,220);tick(false,40,220);assert(specter::scriptPicker);
 tick(true,180,290);tick(false,180,290);assert(!specter::scriptPicker && specter::job);
 keyboardReady=false;tick(true,180,220);tick(false,180,220);assert(helloRequests==1);
 keyboardReady=true;keyboardTyping=true;
 tick(true,40,220);tick(false,40,220);assert(!specter::scriptPicker);
 keyboardTyping=false;
 tick(true,40,290);tick(false,40,290);assert(!specter::job);
 tick(true,40,220);tick(false,40,220);assert(helloRequests==1);


}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'ble_touch_test.cpp'
    cpp.write_text(stubs + source + tests)
    exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
print('BLE touch lifecycle checks passed')
