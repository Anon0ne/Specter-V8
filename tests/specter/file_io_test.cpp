#include "SpecterFileIO.h"
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <iostream>
#include <iterator>
std::string read(const std::string& path) {
  std::ifstream file(path);
  return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
int main() {
  char directory[]="/tmp/specter-io-XXXXXX";assert(mkdtemp(directory));
  const std::string root=directory, path=root+"/session.json", tmp=path+".tmp", index=root+"/outputs.jsonl";
  const char text[]="{\"name\":\"Lab\"}";
  assert(specter::fileio::publishSnapshot(path.c_str(),tmp.c_str(),text,sizeof(text)-1));
  assert(read(path)==text);assert(access(tmp.c_str(),F_OK)!=0);
  assert(!specter::fileio::publishSnapshot(path.c_str(),tmp.c_str(),"overwrite",9));
  assert(read(path)==text);
  assert(specter::fileio::appendSynced(index.c_str(),"one\n",4));
  assert(specter::fileio::appendSynced(index.c_str(),"two\n",4));
  assert(read(index)=="one\ntwo\n");
  assert(!specter::fileio::appendSynced(root.c_str(),"x",1));
  const std::string absent=root+"/missing/file", stage=absent+".tmp";
  assert(!specter::fileio::publishSnapshot(absent.c_str(),stage.c_str(),text,sizeof(text)-1));
  const std::string final2=root+"/complete.json", stage2=final2+".tmp";
  assert(specter::fileio::appendSynced(stage2.c_str(),"existing stage",14));
  assert(!specter::fileio::publishSnapshot(final2.c_str(),stage2.c_str(),text,sizeof(text)-1));
  assert(read(stage2)=="existing stage");assert(access(final2.c_str(),F_OK)!=0);
  unlink(path.c_str());unlink(index.c_str());unlink(stage2.c_str());rmdir(root.c_str());
  std::cout<<"Snapshot preservation, staging failure, and append checks passed\n";
}
