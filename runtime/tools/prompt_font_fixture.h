#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace prompt_test {
inline void be16(std::vector<uint8_t>& b,int p,int v){b[p]=v>>8;b[p+1]=v;}
inline void be32(std::vector<uint8_t>& b,int p,int v){be16(b,p,v>>16);be16(b,p+2,v);}
// Original synthetic tiles: no font or game data in this fixture.
inline std::vector<uint8_t> fixture(){
    std::vector<uint8_t> b(32+16+32+1024);memcpy(b.data(),"FONTbfn1",8);be32(b,8,b.size());
    memcpy(b.data()+32,"MAP1",4);be32(b,36,16);be16(b,42,32);be16(b,44,126);
    const int p=48;memcpy(b.data()+p,"GLY1",4);be32(b,p+4,1056);be16(b,p+10,94);
    be16(b,p+12,2);be16(b,p+14,4);be32(b,p+16,1024);be16(b,p+20,1);be16(b,p+22,16);be16(b,p+24,8);be16(b,p+26,32);be16(b,p+28,32);
    std::fill(b.begin()+p+32,b.end(),180);return b;
}
}
