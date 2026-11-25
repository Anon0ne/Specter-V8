#include "configs.h"
#include "SpecterTouch.h"
#if defined(HAS_SCREEN) && defined(HAS_TOUCH) && defined(HAS_ILI9341)
#include "SpecterSessions.h"
#include "MenuFunctions.h"
#include "DeviceClock.h"
#include "TouchKeyboard.h"
#include "hale_team.h"
#include <WiFi.h>
#ifdef HAS_GPS
#include "GpsInterface.h"
extern GpsInterface gps_obj;
#endif
#ifdef HAS_SD
#include "SDInterface.h"
extern SDInterface sd_obj;
#endif
extern MenuFunctions menu_function_obj;
extern Display display_obj;
extern WiFiScan wifi_scan_obj;

namespace specter {
namespace {
enum class Page { NONE, HUB, SESSION, HISTORY, DETAIL, OUTPUTS, FILE_INFO, HEALTH };
Page page = Page::NONE;
Menu* returnMenu = nullptr;
String pendingName = "Session", notice, selectedSession;
SessionInfo history[5];
SessionInfo detail;
SessionOutput outputs[5];
SessionOutput selectedOutput;
size_t historyOffset = 0, historyCount = 0, outputOffset = 0, outputCount = 0;
bool moreHistory = false, moreOutputs = false;
bool waitRelease = true, fingerDown = false;
uint16_t lastX = 0, lastY = 0;
uint32_t lastDraw = 0;
int pressedAction = -1;
struct Button { int x, y, w, h, action; };
Button buttons[10];
size_t buttonCount = 0;
constexpr int BACK=1, SESSIONS=2, HISTORY=3, HEALTH=4, NAME=5, START=6,
              END=7, PREV=8, NEXT=9, FILES=10, ROW=100;
void line(int y, const String& text, uint16_t color = TFT_WHITE) {
  auto& tft = display_obj.tft;
  tft.setFreeFont(nullptr); tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK); tft.setTextWrap(false); tft.setCursor(8, y);
  tft.print(text.substring(0, (tft.width()-16)/6));
}
void wrapped(int y, const String& text, uint16_t color = TFT_LIGHTGREY) {
  const int chars = (display_obj.tft.width()-16)/6;
  line(y, text, color);
  if (int(text.length()) > chars) line(y+11, text.substring(chars), color);
}
void button(int y, const String& label, int action, uint16_t color=TFT_WHITE, bool right=false, bool half=false) {
  if (buttonCount >= sizeof(buttons)/sizeof(buttons[0])) return;
  auto& tft = display_obj.tft;
  const int width = half ? (tft.width()-24)/2 : tft.width()-16;
  const int x = right ? 16+(tft.width()-24)/2 : 8;
  buttons[buttonCount++] = {x,y,width,30,action};
  tft.drawRoundRect(x,y,width,30,4,color);
  tft.setFreeFont(nullptr); tft.setTextSize(1); tft.setTextColor(color,TFT_BLACK);
  tft.drawCentreString(label.substring(0,(width-8)/8),x+width/2,y+6,2);
}
int hit(uint16_t x, uint16_t y) {
  for (size_t i=0;i<buttonCount;++i) {
    const auto& b=buttons[i];
    if (x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h) return b.action;
  }
  return -1;
}
void releaseTouch() { waitRelease=true; fingerDown=false; pressedAction=-1; }
String duration(uint64_t ms) {
  char text[48];
  snprintf(text,sizeof(text),"%lluh %02um %02us",(unsigned long long)(ms/3600000),
    unsigned((ms/60000)%60),unsigned((ms/1000)%60));
  return String(text);
}
void loadHistory() { historyCount=sessionPage(historyOffset,history,5,moreHistory); }
void loadOutputs() { outputCount=outputPage(selectedSession,outputOffset,outputs,5,moreOutputs); }
void draw(bool full=true) {
  auto& tft=display_obj.tft;
  const int height=tft.height(), width=tft.width();
  if (full) {
    display_obj.clearScreen(); menu_function_obj.drawStatusBar(); buttonCount=0;
  } else tft.fillRect(0,44,width,page==Page::HUB?42:96,TFT_BLACK);
  const SessionInfo active=activeSession();
  const String title=page==Page::HUB ? "Specter" : page==Page::SESSION ? "Specter Sessions" :
    page==Page::HISTORY ? "Session History" : page==Page::DETAIL ? "Session Details" :
    page==Page::OUTPUTS ? "Session Files" : page==Page::FILE_INFO ? "File Details" : "Device Health";
  if (full) {
    tft.setFreeFont(nullptr);tft.setTextSize(1);tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.drawCentreString(title,width/2,22,2);
  }
  if (page==Page::HUB) {
    line(48,sessionActive() ? "Active: "+active.name : "No session active",TFT_CYAN);
    line(65,"Clock: "+marauder::clockUtc());
    if (full) {
      button(98,"Sessions",SESSIONS);button(136,"Session history",HISTORY);
      button(174,"Device health",HEALTH);button(height-44,"Back",BACK);
    }
    line(height-78,"Start a session, then use WiFi tools.",TFT_LIGHTGREY);
  } else if (page==Page::SESSION) {
    line(48,sessionActive() ? "State: ACTIVE" : "State: READY",TFT_CYAN);
    line(63,"Name: "+(sessionActive()?active.name:pendingName));
    line(78,sessionActive() ? "Start: "+active.startedUtc : "Clock: "+marauder::clockUtc());
    line(93,sessionActive() ? "Files: "+String(active.files)+"  "+duration(active.elapsedMs) : "Names are stored on SD.");
    wrapped(110,notice.length()?notice:active.error);
    if (full) {
      button(146,"Set session name",NAME);
      button(180,"Start session",START,TFT_CYAN);
      button(214,"End session",END,TFT_ORANGE);
      button(height-44,"Back to Specter",BACK);
    }
    line(height-72,"Back keeps the session active.",TFT_LIGHTGREY);
  } else if (page==Page::HISTORY || page==Page::OUTPUTS) {
    if (!full) return;
    const size_t count=page==Page::HISTORY?historyCount:outputCount;
    for (size_t row=0;row<count;++row) {
      String label;
      if (page==Page::HISTORY) label=history[row].name;
      else {
        label=outputs[row].path;
        label=label.substring(label.lastIndexOf('/')+1);
      }
      button(54+row*32,label,ROW+row);
    }
    if (!count) wrapped(62,page==Page::HISTORY?"No sessions / SD unavailable":"No indexed outputs / SD unavailable");
    button(height-80,"< Prev",PREV,TFT_WHITE,false,true);
    button(height-80,"Next >",NEXT,TFT_WHITE,true,true);
    button(height-44,"Back",BACK);
  } else if (page==Page::DETAIL) {
    line(48,detail.name,TFT_CYAN);line(65,"State: "+detail.status);
    line(82,"Start: "+detail.startedUtc);line(99,"Clock source: "+detail.clockSource);
    line(116,detail.files==UINT32_MAX?"Files: see output index":"Files: "+String(detail.files));
    line(133,detail.status=="interrupted"?"Duration: not finalized":"Duration: "+duration(detail.elapsedMs));
    wrapped(155,detail.error.length()?detail.error:detail.path);
    if (full) { button(214,"Browse session files",FILES);button(height-44,"Back to history",BACK); }
  } else if (page==Page::FILE_INFO) {
    wrapped(48,selectedOutput.path,TFT_CYAN);
    line(82,"Kind: "+selectedOutput.kind);line(99,"Recorded: "+selectedOutput.utc);
    #ifdef HAS_SD
    File file=sd_obj.supported?SD.open(selectedOutput.path,FILE_READ):File();
    if (file) {
      line(116,"Size: "+String(file.size())+" bytes");
      line(133,"Modified: "+marauder::formatClockUtc(file.getLastWrite()));
      file.close();
    } else line(116,"File no longer available",TFT_ORANGE);
    #endif
    wrapped(160,"Read-only view. Files stay on SD.");
    if (full) button(height-44,"Back to files",BACK);
  } else if (page==Page::HEALTH) {
    line(48,"Clock: "+marauder::clockUtc(),marauder::clockValid()?TFT_WHITE:TFT_ORANGE);
    line(66,"Time source: "+marauder::clockTimeSource());
    #ifdef HAS_SD
    line(84,sd_obj.supported?"SD: mounted ("+String(sd_obj.cardSizeMB)+" MB)":"SD: unavailable",TFT_CYAN);
    #else
    line(84,"SD: not configured");
    #endif
    line(102,"Heap free: "+String(ESP.getFreeHeap())+" bytes");
    #ifdef HAS_PSRAM
    line(120,"PSRAM free: "+String(ESP.getFreePsram())+" bytes");
    #endif
    #ifdef HAS_GPS
    line(138,gps_obj.getGpsModuleStatus()?String("GPS: ")+(gps_obj.getFixStatus()?"fix":"no fix")+" sats="+String(gps_obj.getNumSats()):"GPS: not detected");
    #endif
    line(156,WiFi.status()==WL_CONNECTED?"WiFi: "+WiFi.localIP().toString():"WiFi: not connected");
    #if HALE_TEAM_MODE
    line(174,String("Mode: ")+hale::teamModeName());
    #endif
    line(192,"Firmware: "+String(MARAUDER_VERSION));
    line(210,sessionActive()?"Session: "+active.name:"Session: none",TFT_CYAN);
    if (full) button(height-44,"Back",BACK);
  }
  lastDraw=millis();
}
void act(int action) {
  notice="";
  if (action==BACK) {
    if (page==Page::HUB) {
      page=Page::NONE;
      if(returnMenu) menu_function_obj.changeMenu(returnMenu);
      return;
    }
    if(page==Page::DETAIL) {page=Page::HISTORY;loadHistory();}
    else if(page==Page::OUTPUTS) page=Page::DETAIL;
    else if(page==Page::FILE_INFO) {page=Page::OUTPUTS;loadOutputs();}
    else page=Page::HUB;
  } else if(action==SESSIONS) page=Page::SESSION;
  else if(action==HISTORY) {page=Page::HISTORY;historyOffset=0;loadHistory();}
  else if(action==HEALTH) page=Page::HEALTH;
  else if(action==NAME && page==Page::SESSION) {
    if(sessionActive()) notice="End session before changing its name";
    else {
      char name[49]={};pendingName.toCharArray(name,sizeof(name));
      if(keyboardInput(name,sizeof(name),"Specter session name")) pendingName=name;
    }
  } else if(action==START && page==Page::SESSION) {
    String error;notice=startSession(pendingName,error)?"Session started; choose WiFi tools":error;
  } else if(action==END && page==Page::SESSION) {
    String error;notice=endSession(error)?"Session closed and saved":error;
  } else if(action==PREV) {
    if(page==Page::HISTORY) {historyOffset=historyOffset>=5?historyOffset-5:0;loadHistory();}
    else if(page==Page::OUTPUTS) {outputOffset=outputOffset>=5?outputOffset-5:0;loadOutputs();}
  } else if(action==NEXT) {
    if(page==Page::HISTORY && moreHistory) {historyOffset+=5;loadHistory();}
    else if(page==Page::OUTPUTS && moreOutputs) {outputOffset+=5;loadOutputs();}
  } else if(action==FILES && page==Page::DETAIL) {
    page=Page::OUTPUTS;outputOffset=0;loadOutputs();
  } else if(action>=ROW && action<ROW+5) {
    const size_t index=action-ROW;
    if(page==Page::HISTORY && index<historyCount) {
      selectedSession=history[index].path;
      if(readSession(selectedSession,detail)) page=Page::DETAIL;
    } else if(page==Page::OUTPUTS && index<outputCount) {
      selectedOutput=outputs[index];page=Page::FILE_INFO;
    }
  }
  draw();releaseTouch();
}
}
bool touchActive() {return page!=Page::NONE;}
void showHub() {
  if(wifi_scan_obj.currentScanMode!=WIFI_SCAN_OFF && wifi_scan_obj.currentScanMode!=WIFI_CONNECTED) {
    Serial.println(F("[Specter] Stop the current scan before opening Specter"));return;
  }
  returnMenu=menu_function_obj.current_menu;page=Page::HUB;notice="";
  display_obj.tft.setRotation(SCREEN_ORIENTATION);display_obj.setCalData(false);
  display_obj.exit_draw=false;wifi_scan_obj.orient_display=false;
  draw();releaseTouch();
}
bool touchLoop(uint32_t now) {
  if(page==Page::NONE) return false;
  if(uint32_t(now-lastDraw)>=1000 && (page==Page::SESSION || page==Page::HEALTH || page==Page::HUB))
    draw(page==Page::HEALTH);
  uint16_t x=0,y=0;const bool down=display_obj.updateTouch(&x,&y)!=0;
  if(waitRelease) {if(!down) waitRelease=false;return true;}
  if(down) {if(!fingerDown) pressedAction=hit(x,y);lastX=x;lastY=y;fingerDown=true;}
  else if(fingerDown) {
    const int action=pressedAction;fingerDown=false;pressedAction=-1;
    if(action>=0 && action==hit(lastX,lastY)) act(action);
  }
  return true;
}
}
#else
namespace specter {
void showHub() {}
bool touchActive() {return false;}
bool touchLoop(uint32_t) {return false;}
}
#endif
