"""Exercise the production DuckyScript compiler and nonblocking HID runner."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
harness=r'''#include "BadBleScript.h"
#include <cassert>
#include <array>
#include <vector>
#include <iostream>
using namespace specter;
int main(){
 BadBleScript script;
 assert(script.compile("REM hello\r\nDEFAULTDELAY 20\r\nSTRING Hi  \r\nSTRINGLN !\n"));
 assert(script.count==6 && script.characters==6);
 assert(script.steps[0].mod==2 && script.steps[0].key==11 && script.steps[0].afterMs==55);
 assert(script.steps[2].key==44 && script.steps[3].key==44 && script.steps[5].key==40);
 assert(script.compile("STRING ab\nREPEAT 2\nREPEAT 1\n"));
 assert(script.count==8 && script.characters==8);
 for(size_t i=0;i<script.count;++i)assert(script.steps[i].key==(i%2?5:4));
 assert(script.compile("CTRL-ALT t\nSHIFT TAB\nF12\nDEFAULT_DELAY 50\nDELAY 60000\n"));
 assert(script.count==4 && script.steps[0].mod==5 && script.steps[0].key==23);
 assert(script.steps[1].mod==2 && script.steps[1].key==43);
 assert(script.steps[2].key==69 && script.steps[3].afterMs==60000);
 for(auto bad:{"", "REM only", "REPEAT 2", "DELAY -1", "DELAY 60001", "DELAY 3x", "DELAY 1 2",
   "DEFAULTDELAY 65536", "STRING a\nBOGUS", "STRING a\nREPEAT 1024", "CTRL BOGUS", "CTRL a b",
   "STRING non-ASCII: \xc3\xa9", "DELAY 999999999999999999999"}){
   assert(!script.compile(bad));assert(script.count==0 && script.error[0]);
 }
 assert(!script.compile(std::string("STRING a\0STRING b",17)));
 assert(!script.compile(std::string(32769,'a')));
 assert(script.compile("STRING "+std::string(1024,'a')) && script.count==1024);
 assert(!script.compile("STRING "+std::string(1025,'a')) && script.count==0);
 assert(script.compile("STRING Hi\nDELAY 100\nSTRING !\n"));
 std::vector<std::array<uint8_t,8>> reports;
 auto send=[&](const uint8_t (&r)[8]){std::array<uint8_t,8> v;for(int i=0;i<8;++i)v[i]=r[i];reports.push_back(v);return true;};
 BadBleRunner runner;
 runner.tick(script,0,true,send);assert(reports.empty()); // connecting alone cannot type
 runner.start(0);runner.tick(script,1499,true,send);assert(reports.empty());
 runner.tick(script,1500,true,send);assert(reports.size()==1 && reports[0][0]==2 && reports[0][2]==11);
 runner.tick(script,1534,true,send);assert(reports.size()==1);
 runner.tick(script,1535,true,send);assert(reports.size()==2);
 for(auto b:reports.back())assert(b==0);
 for(uint32_t now=1536;runner.running && now<5000;++now)runner.tick(script,now,true,send);
 assert(!runner.running && runner.completed && runner.characters==3 && reports.size()==6);
 for(size_t i=1;i<reports.size();i+=2)for(auto b:reports[i])assert(b==0);
 runner.start(6000);runner.tick(script,7500,true,send);runner.stop(send,"Stopped");
 assert(!runner.running && !runner.completed);for(auto b:reports.back())assert(b==0);
 runner.start(8000);runner.tick(script,9500,true,send);runner.tick(script,9501,false,send);
 assert(!runner.running);size_t n=reports.size();runner.tick(script,9600,true,send);assert(reports.size()==n);
 runner.start(10000);runner.tick(script,11500,true,[](const uint8_t (&)[8]){return false;});assert(!runner.running);
 // Timers remain correct across millis rollover.
 runner.start(0xfffffff0);runner.tick(script,0xfffffff0,true,send);assert(runner.running);
 runner.tick(script,uint32_t(0xfffffff0+1500u),true,send);assert(reports.back()[2]==11);
 std::cout<<"DuckyScript grammar, limits, HID reports, timing, stop and disconnect checks passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'test.cpp';src.write_text(harness);exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
      '-I'+str(root/'esp32_marauder'),str(src),str(root/'esp32_marauder/BadBleScript.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
