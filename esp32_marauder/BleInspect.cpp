#include "configs.h"
#include "BleInspect.h"
#include "BleTouch.h"
#if defined(HAS_BT) && defined(HAS_SCREEN) && defined(HAS_TOUCH) && defined(HAS_ILI9341)
#include "MenuFunctions.h"
#include "bt_predator.h"
#include "hale_team.h"
extern MenuFunctions menu_function_obj;
extern Display display_obj;
namespace specter {
namespace {
enum class InspectPage { CLOSED, LIST, TARGET_DETAIL, RESULT };
InspectPage inspectPage=InspectPage::CLOSED;
unsigned deviceOffset=0, characteristic=0;
int selected=-1, inspectPressed=-1;
bool inspectWaitRelease=true, inspectFinger=false;
uint16_t inspectX=0,inspectY=0;
String inspectNotice, selectedMac;
uint8_t selectedAddrType=0;
struct InspectButton {int x,y,w,h,action;};
InspectButton inspectButtons[8];unsigned inspectButtonCount=0;
void inspectLine(int y,const String& s,uint16_t color=TFT_WHITE){
 auto& t=display_obj.tft;t.setFreeFont(nullptr);t.setTextFont(1);t.setTextSize(1);
 t.setTextWrap(false);t.setTextColor(color,TFT_BLACK);t.setCursor(8,y);
 t.print(s.substring(0,(t.width()-16)/6));
}
// Recon's legacy table stores UUID bytes in NimBLE's little-endian order.
String displayUuid(const String& stored){
 if(stored.length()!=4 && stored.length()!=32)return stored;
 String result;
 for(int i=int(stored.length())-2;i>=0;i-=2)result+=stored.substring(i,i+2);
 if(result.length()==32){
  return result.substring(0,8)+"-"+result.substring(8,12)+"-"+
   result.substring(12,16)+"-"+result.substring(16,20)+"-"+result.substring(20);
 }
 return result;
}
void inspectWrapped(int y,const String& s){
 const unsigned chars=(display_obj.tft.width()-16)/6;
 inspectLine(y,s);if(s.length()>chars)inspectLine(y+12,s.substring(chars));
}
void inspectButton(int x,int y,int w,const String& label,int action){
 if(inspectButtonCount>=8)return;
 inspectButtons[inspectButtonCount++]={x,y,w,30,action};auto& t=display_obj.tft;
 t.drawRoundRect(x,y,w,30,4,TFT_WHITE);t.setTextColor(TFT_WHITE,TFT_BLACK);
 t.drawCentreString(label.substring(0,(w-8)/8),x+w/2,y+7,2);
}
int inspectHit(uint16_t x,uint16_t y){
 for(unsigned i=0;i<inspectButtonCount;++i){const auto& b=inspectButtons[i];
 if(x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h)return b.action;}
 return -1;
}
bool selectedUnchanged(){
 auto* d=selected>=0?bt_dev_get(selected):nullptr;
 return d && selectedMac==bt_mac_str(d->mac) && selectedAddrType==d->addrType;
}
void inspectDraw(){
 auto& t=display_obj.tft;display_obj.clearScreen();menu_function_obj.drawStatusBar();
 t.setFreeFont(nullptr);t.setTextSize(1);inspectButtonCount=0;
 inspectLine(28,inspectPage==InspectPage::LIST?"BLE devices":
  inspectPage==InspectPage::TARGET_DETAIL?"Selected BLE device":"GATT inspection",TFT_CYAN);
 if(inspectPage==InspectPage::LIST){
  const int total=bt_dev_count();
  if(deviceOffset>=unsigned(total))deviceOffset=0;
  inspectLine(49,"Discovered: "+String(total)+" (scan stopped)");
  for(unsigned i=0;i<4 && deviceOffset+i<unsigned(total);++i){
   const int idx=deviceOffset+i;auto* d=bt_dev_get(idx);if(!d)continue;
   inspectButton(8,72+i*40,224,String(idx)+": "+
    (d->name[0]?String(d->name):bt_mac_str(d->mac)),100+idx);
  }
  if(!total)inspectLine(90,"No devices. Run Predator Scan first.",TFT_YELLOW);
  inspectButton(8,240,108,"Previous",1);inspectButton(124,240,108,"Next",2);
 }else if(inspectPage==InspectPage::TARGET_DETAIL){
  auto* d=selectedUnchanged()?bt_dev_get(selected):nullptr;
  if(d){
   inspectLine(52,d->name[0]?String(d->name):String("Unnamed device"));
   inspectLine(70,selectedMac);inspectLine(88,"Signal: "+String(d->rssi)+" dBm");
   inspectLine(106,(d->flags&0x80)?"Connectable advertisement":"Not advertised as connectable");
   inspectLine(130,"Reads services and characteristics.");
   inspectLine(148,"No characteristic writes.");
   inspectButton(8,190,224,"Inspect GATT",3);
  }else inspectLine(65,"Device changed; choose again",TFT_YELLOW);
  inspectWrapped(222,inspectNotice);
 }else{
  auto* table=bt_predator_recon_table();
  inspectLine(49,"Characteristic "+String(table->empty()?0:characteristic+1)+" / "+String(table->size()));
  if(characteristic<table->size()){
   const auto& c=(*table)[characteristic];
   inspectLine(70,"Service UUID",TFT_LIGHTGREY);inspectWrapped(86,displayUuid(c.svcUuid));
   inspectLine(118,"Characteristic UUID",TFT_LIGHTGREY);inspectWrapped(134,displayUuid(c.chrUuid));
   inspectLine(166,"Handle: "+String(c.handle)+" Props: 0x"+String(c.props,HEX));
   inspectLine(184,"Read value (hex preview)",TFT_LIGHTGREY);
   inspectWrapped(200,c.valueHex.length()?c.valueHex:String("Empty / unreadable / read failed"));
  }else inspectLine(85,"No characteristics returned",TFT_YELLOW);
  inspectButton(8,240,108,"Previous",1);inspectButton(124,240,108,"Next",2);
 }
 inspectButton(8,278,224,inspectPage==InspectPage::LIST?"Back to scan":"Back to devices",4);
 inspectWaitRelease=true;inspectFinger=false;inspectPressed=-1;
}
void inspectAct(int action){
 if(action==4){
  if(inspectPage==InspectPage::LIST){inspectPage=InspectPage::CLOSED;resumeBleScreen();return;}
  inspectPage=InspectPage::LIST;inspectNotice="";
 }else if(inspectPage==InspectPage::LIST){
  if(action==1)deviceOffset=deviceOffset>=4?deviceOffset-4:0;
  else if(action==2 && deviceOffset+4<unsigned(bt_dev_count()))deviceOffset+=4;
  else if(action>=100 && action<100+bt_dev_count()){
   selected=action-100;auto* d=bt_dev_get(selected);
   if(d){selectedMac=bt_mac_str(d->mac);selectedAddrType=d->addrType;
    inspectPage=InspectPage::TARGET_DETAIL;inspectNotice="";}
  }
 }else if(inspectPage==InspectPage::TARGET_DETAIL && action==3){
  auto* d=selectedUnchanged()?bt_dev_get(selected):nullptr;
  if(!d)inspectNotice="Device changed; choose again";
  else if(!(d->flags&0x80))inspectNotice="Select a connectable device";
  else if(!hale::offensiveAllowed())inspectNotice="Recon requires Red Team mode";
  else if(!bt_is_armed())inspectNotice="Arm BT+ from the menu first";
  else {
   inspectNotice="Connecting... please wait";inspectDraw();
   bt_command("predator recon "+String(selected));
   if(bt_predator_recon_done()){inspectPage=InspectPage::RESULT;characteristic=0;}
   else { inspectNotice=bt_predator_recon_error();
    if(!inspectNotice.length())inspectNotice="Connection / discovery failed"; }
  }
 }else if(inspectPage==InspectPage::RESULT){
  const auto n=bt_predator_recon_table()->size();
  if(action==1 && characteristic)--characteristic;
  else if(action==2 && characteristic+1<n)++characteristic;
 }
 inspectDraw();
}
}
void showBleDevices(){
 // Stop scanning before selection so normal callbacks cannot replace target slots.
 bt_stop_all();inspectPage=InspectPage::LIST;deviceOffset=0;selected=-1;inspectNotice="";inspectDraw();
}
bool bleInspectLoop(uint32_t){
 if(inspectPage==InspectPage::CLOSED)return false;
 uint16_t x=0,y=0;bool down=display_obj.updateTouch(&x,&y)!=0;
 if(inspectWaitRelease){if(!down)inspectWaitRelease=false;return true;}
 if(down){if(!inspectFinger)inspectPressed=inspectHit(x,y);inspectFinger=true;inspectX=x;inspectY=y;}
 else if(inspectFinger){inspectFinger=false;int action=inspectPressed;inspectPressed=-1;
  if(action>=0 && action==inspectHit(inspectX,inspectY))inspectAct(action);
 }
 return true;
}
}
#else
namespace specter {void showBleDevices(){} bool bleInspectLoop(uint32_t){return false;}}
#endif
