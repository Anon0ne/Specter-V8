"""Bounded, per-request submission records preserve separation and UTF-8."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <cassert>
#include <string>
#include "PortalSubmission.h"
int main(){
 using namespace specter;
 auto a=portalSubmission("employee@example.test",21,true,"test-only",9,true,123);
 assert(std::string(a.user)=="employee@example.test" && std::string(a.password)=="test-only");
 assert(a.hasUser && a.hasPassword && !a.truncated && a.receivedMs==123);
 auto b=portalSubmission("second",6,true,"",0,false,124);
 assert(!b.hasPassword && std::string(b.password).empty());
 assert(std::string(a.password)=="test-only"); // independent requests cannot share fields
 std::string longValue(600,'x');
 auto c=portalSubmission(longValue.data(),longValue.size(),true,"",0,false,125);
 assert(c.truncated && strlen(c.user)==kPortalFieldBytes);
 std::string utf8(511,'a');utf8+="\xc3\xa9";
 auto d=portalSubmission(utf8.data(),utf8.size(),true,"",0,false,126);
 assert(d.truncated && strlen(d.user)==511);
 auto e=portalSubmission("",0,true,"",0,true,127);
 assert(e.hasUser && e.hasPassword && !e.truncated);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(source);exe=Path(tmp)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'esp32_marauder'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('Portal submission record checks passed')
