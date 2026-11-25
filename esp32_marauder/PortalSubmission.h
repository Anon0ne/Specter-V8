#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace specter {
constexpr size_t kPortalFieldBytes=512;
struct PortalSubmission {
 char user[kPortalFieldBytes+1];
 char password[kPortalFieldBytes+1];
 uint32_t receivedMs;
 bool hasUser,hasPassword,truncated;
};
inline size_t copyPortalField(char* dst,const char* src,size_t length){
 size_t n=length<kPortalFieldBytes?length:kPortalFieldBytes;
 // Do not cut a UTF-8 sequence when a field exceeds the storage bound.
 if(n<length)while(n && (static_cast<unsigned char>(src[n])&0xc0)==0x80)--n;
 memcpy(dst,src,n);dst[n]=0;return n;
}
inline PortalSubmission portalSubmission(const char* user,size_t userLen,bool hasUser,
 const char* password,size_t passwordLen,bool hasPassword,uint32_t ms){
 PortalSubmission out{};out.receivedMs=ms;out.hasUser=hasUser;out.hasPassword=hasPassword;
 out.truncated=copyPortalField(out.user,user,userLen)<userLen;
 out.truncated=(copyPortalField(out.password,password,passwordLen)<passwordLen)||out.truncated;
 return out;
}
}
