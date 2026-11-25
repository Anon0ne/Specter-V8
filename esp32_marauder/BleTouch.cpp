#include "configs.h"
#include "BleTouch.h"
#include "BleInspect.h"
#if defined(HAS_BT) && defined(HAS_SCREEN) && defined(HAS_TOUCH) && defined(HAS_ILI9341)
#include "MenuFunctions.h"
#include "bt_core.h"
#include "bt_badble.h"
extern MenuFunctions menu_function_obj;
extern Display display_obj;
extern WiFiScan wifi_scan_obj;
namespace specter {
namespace {
bool active=false, job=false, observed=false, waitRelease=true, downBefore=false;
int pressed=-1;
uint16_t lastX=0,lastY=0;
uint32_t lastDraw=0;
Menu* returnMenu=nullptr;
String title, message;
BtUiState snapshot{};
bool scriptPicker=false, scriptMore=false;
size_t scriptOffset=0, scriptCount=0;
String scriptNames[5];
void loadScripts() {
  scriptCount=bt_badble_scripts(scriptNames,5,scriptOffset,scriptMore);
}
void text(int y,const String& value,uint16_t color=TFT_WHITE) {
  auto& t=display_obj.tft;
  t.setFreeFont(nullptr);t.setTextFont(1);t.setTextSize(1);
  t.setTextColor(color,TFT_BLACK);t.setTextWrap(false);t.setCursor(8,y);
  t.print(value.substring(0,(t.width()-16)/6));
}
void draw(bool full=false) {
  auto& t=display_obj.tft;
  if(full) {
    display_obj.clearScreen();menu_function_obj.drawStatusBar();
    text(28,title,TFT_CYAN);
  }
  t.fillRect(0,44,t.width(),t.height()-92,TFT_BLACK);
  if(scriptPicker) {
    text(49,"SD: /btloot/payloads",TFT_CYAN);
    for(size_t i=0;i<scriptCount;++i) {
      const int y=67+i*30;
      t.drawRoundRect(8,y,t.width()-16,27,4,TFT_CYAN);
      text(y+9,scriptNames[i]);
    }
    const int w=(t.width()-24)/2;
    t.drawRoundRect(8,228,w,27,4,scriptOffset?TFT_WHITE:TFT_DARKGREY);
    t.drawRoundRect(16+w,228,w,27,4,scriptMore?TFT_WHITE:TFT_DARKGREY);
    text(237,"< Previous              Next >");
  } else {
  BtUiState current{};
  if(job && bt_ui_get(current)) {snapshot=current;observed=true;}
  else if(job) {job=false;message=observed?"Finished / idle":"Did not start; check Serial";}
  text(49,job?String(snapshot.phase?snapshot.phase:"Running"):message,job?TFT_GREEN:TFT_YELLOW);
  if(observed) {
    for(unsigned i=0;i<snapshot.lineCount && i<BT_UI_MAX_LINES;++i)
      text(73+i*18,snapshot.lines[i]);
    text(t.height()-76,job?"Live results":"Last observed results",TFT_LIGHTGREY);
  }
  if(observed && snapshot.module==BT_MOD_PREDATOR){
    t.setFreeFont(nullptr);t.setTextSize(1);t.setTextColor(TFT_CYAN,TFT_BLACK);
    t.drawRoundRect(8,t.height()-112,t.width()-16,30,4,TFT_CYAN);
    t.drawCentreString("Devices / Inspect",t.width()/2,t.height()-105,2);
  }
  if(job && observed && snapshot.module==BT_MOD_BADBLE) {
    const String name=bt_badble_selected();
    text(151,name.substring(0,36),TFT_CYAN);
    if(name.length()>36)text(167,name.substring(36,72),TFT_CYAN);
    text(185,"Select a script, then tap Run",TFT_LIGHTGREY);
    const int w=(t.width()-24)/2,y=t.height()-112;
    t.setTextColor(TFT_CYAN,TFT_BLACK);
    t.drawRoundRect(8,y,w,30,4,TFT_CYAN);
    t.drawRoundRect(16+w,y,w,30,4,bt_badble_ready()&&!bt_badble_typing()?TFT_GREEN:TFT_DARKGREY);
    t.drawCentreString("Scripts",8+w/2,y+7,2);
    t.drawCentreString("Run",16+w+w/2,y+7,2);
  }
  }
  const int y=t.height()-44,w=(t.width()-24)/2;
  t.setFreeFont(nullptr);t.setTextSize(1);
  t.drawRoundRect(8,y,w,32,4,job?TFT_ORANGE:TFT_DARKGREY);
  t.drawRoundRect(16+w,y,w,32,4,TFT_WHITE);
  t.setTextColor(TFT_WHITE,TFT_BLACK);
  t.drawCentreString("Stop",8+w/2,y+7,2);
  t.drawCentreString("Back",16+w+w/2,y+7,2);
  lastDraw=millis();
}
int hit(uint16_t x,uint16_t y) {
  auto& t=display_obj.tft;
  if(scriptPicker) {
    for(size_t i=0;i<scriptCount;++i)
      if(x>=8 && x<t.width()-8 && y>=67+i*30 && y<94+i*30)return 100+i;
    if(y>=228 && y<255) {
      if(x>=8 && x<t.width()/2-4 && scriptOffset)return 5;
      if(x>=t.width()/2+4 && x<t.width()-8 && scriptMore)return 6;
    }
  }
  if(!scriptPicker && observed && snapshot.module==BT_MOD_PREDATOR && x>=8 && x<t.width()-8 &&
     y>=t.height()-112 && y<t.height()-82)return 3;
  if(!scriptPicker && job && observed && snapshot.module==BT_MOD_BADBLE &&
     y>=t.height()-112 && y<t.height()-82) {
    if(x>=8 && x<t.width()/2-4 && !bt_badble_typing())return 7;
    if(x>=t.width()/2+4 && x<t.width()-8 && bt_badble_ready() && !bt_badble_typing())return 4;
  }
  if(y<t.height()-44 || y>=t.height()-12)return -1;
  const int w=(t.width()-24)/2;
  if(x>=8 && x<8+w)return 1;
  if(x>=16+w && x<16+2*w)return 2;
  return -1;
}
void open(const char* label) {
  if(!active)returnMenu=menu_function_obj.current_menu;
  scriptPicker=false;scriptOffset=0;
  active=true;title=label;observed=false;job=false;snapshot=BtUiState{};
  waitRelease=true;downBefore=false;pressed=-1;
  display_obj.tft.setRotation(SCREEN_ORIENTATION);display_obj.setCalData(false);
  display_obj.exit_draw=false;wifi_scan_obj.orient_display=false;
}
}
void showBleMessage(const char* label,const char* value) {
  open(label);message=value;draw(true);
}
void showBle(const char* command,const char* label) {
  // One module owns the result screen; do not display a previous job's state.
  bt_stop_all();open(label);
  const bool handled=bt_command(String(command));
  job=handled;message=handled?"Starting":"Command unavailable";draw(true);
}
void resumeBleScreen(){
  if(active){waitRelease=true;downBefore=false;pressed=-1;draw(true);}
}
bool bleTouchLoop(uint32_t now) {
  if(!active)return false;
  if(uint32_t(now-lastDraw)>=250)draw();
  uint16_t x=0,y=0;const bool down=display_obj.updateTouch(&x,&y)!=0;
  if(waitRelease){if(!down)waitRelease=false;return true;}
  if(down){if(!downBefore)pressed=hit(x,y);lastX=x;lastY=y;downBefore=true;}
  else if(downBefore){
    downBefore=false;const int action=pressed;pressed=-1;
    if(action>0 && action==hit(lastX,lastY)) {
      if(action>=100 && size_t(action-100)<scriptCount && scriptPicker){
        bt_badble_select(scriptNames[action-100]);scriptPicker=false;draw(true);
      } else if(action==5 || action==6){
        if(action==5)scriptOffset=scriptOffset>=5?scriptOffset-5:0;
        else scriptOffset+=5;
        loadScripts();draw(true);
      } else if(action==7){
        scriptOffset=0;loadScripts();scriptPicker=true;draw(true);
      } else if(action==4){
        bt_badble_run_selected();draw();
      } else if(action==3){
        bt_stop_all();job=false;message="Stopped";showBleDevices();
      } else if(action==1 && job){
        BtUiState current{};if(bt_ui_get(current)){snapshot=current;observed=true;}
        bt_stop_all();job=false;scriptPicker=false;message="Stopped";draw();
      } else if(action==2 && scriptPicker){
        scriptPicker=false;draw(true);
      } else if(action==2){
        bt_stop_all();bt_radio_end();active=false;
        display_obj.exit_draw=false;display_obj.printing=false;display_obj.loading=false;
        wifi_scan_obj.orient_display=false;
        display_obj.tft.setTextWrap(true);
        display_obj.init();
        display_obj.tft.setRotation(SCREEN_ORIENTATION);display_obj.setCalData(false);
        if(returnMenu)menu_function_obj.changeMenu(returnMenu,true);
      }
    }
  }
  return true;
}
}
#else
namespace specter {
void showBle(const char*,const char*){}
void showBleMessage(const char*,const char*){}
void resumeBleScreen(){}
bool bleTouchLoop(uint32_t){return false;}
}
#endif
