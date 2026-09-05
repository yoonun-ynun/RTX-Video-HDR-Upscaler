#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Input is already BT.2020/PQ RGB from the vendor extension. No second transfer/gamut transform.
// Output P010: limited Y 64..940, Cb/Cr 64..960; centered 2x2 box chroma, 10 MSBs in uint16.
inline std::vector<uint16_t> ToP010(const std::vector<uint32_t>& rgb,unsigned w,unsigned h) {
    std::vector<uint16_t> p(static_cast<size_t>(w)*h*3/2);
    const size_t uvBase=static_cast<size_t>(w)*h;
    auto quant=[](double v,int low,int high) {
        return static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(v)),low,high)<<6);
    };
    for(unsigned y=0;y<h;y+=2) for(unsigned x=0;x<w;x+=2) {
        double cb=0,cr=0;
        for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
            size_t pos=static_cast<size_t>(y+dy)*w+x+dx;
            uint32_t word=rgb[pos];
            double r=(word&1023)/1023.0,g=((word>>10)&1023)/1023.0,b=((word>>20)&1023)/1023.0;
            double l=.2627*r+.6780*g+.0593*b;
            p[pos]=quant(64+876*l,64,940);
            cb+=(b-l)/(2*(1-.0593)); cr+=(r-l)/(2*(1-.2627));
        }
        size_t uv=uvBase+static_cast<size_t>(y/2)*w+x;
        p[uv]=quant(512+224*cb,64,960); p[uv+1]=quant(512+224*cr,64,960);
    }
    return p;
}
