#include "pipeline.h"
#include "color.h"
#include <iostream>
#include <cstring>

// Independent double-precision reference, quantized to RGB10 before the existing CPU packer.
static uint32_t Reference(uint32_t rgb) {
    double light[3];
    for(int c=0;c<3;c++) {
        double v=((rgb>>(c*10))&1023)/1023.0;
        light[c]=v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);
    }
    const double gamut[3][3]{{.627404,.329283,.043313},{.069097,.919540,.011362},{.016391,.088013,.895595}};
    uint32_t packed=3u<<30;
    for(int c=0;c<3;c++) {
        double nits=203*(gamut[c][0]*light[0]+gamut[c][1]*light[1]+gamut[c][2]*light[2]);
        double power=std::pow(nits/10000,2610.0/16384);
        double pq=std::pow((3424.0/4096+2413.0/128*power)/(1+2392.0/128*power),2523.0/32);
        packed|=static_cast<uint32_t>(std::lround(pq*1023))<<(c*10);
    }
    return packed;
}
int main(int argc,char** argv) {
    try {
        auto adapters=EnumerateAdapters();unsigned adapter=argc>1?std::stoul(argv[1]):0;
        auto a=std::find_if(adapters.begin(),adapters.end(),[&](const auto& x){return x.index==adapter && x.desc.VendorId==0x10de;});
        if(a==adapters.end())throw Failure(3,"NVIDIA GPU required");
        // Independent known PQ anchors: black=0, 203-nit white=0.58068888.
        if((Reference(0)&1023)!=0 || std::abs(int(Reference(0x3fffffff)&1023)-594)>1)throw Failure(5,"Reference white/black anchors failed");
        for(unsigned w:{1918u,1920u}) for(bool input10:{false,true}) {
            constexpr unsigned h=1080;
            Pipeline off(*a,w,h,false,input10),on(*a,w,h,false,input10),split(*a,w,h,false,input10);
            off.CreateResources();off.SetHdr(false);on.CreateResources();on.SetHdr(true);
            split.CreateResources();split.SetHdr(true);split.EnableComparison();
            unsigned frame=0;
            for(auto pattern:{"gray-ramp","color-bars"}) {
                auto input=GeneratePattern(w,h,pattern);
                if(input10) {std::vector<uint8_t> expanded(input.size()*2);for(size_t i=0;i<input.size();i++)expanded[i*2+1]=input[i];input=std::move(expanded);}
                std::vector<uint32_t> sdr,hdr;std::vector<uint16_t> actual;
                for(unsigned warm=0;warm<8;warm++,frame++) {
                    off.Upload(input);sdr=off.Process(frame);
                    on.Upload(input);hdr=on.Process(frame);
                    split.Upload(input);split.ProcessP010(frame,actual);
                }
                auto expectedRGB=hdr;double effect=0;
                for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
                    size_t i=static_cast<size_t>(y)*w+x;
                    effect+=std::abs(int(hdr[i]&1023)-int(sdr[i]&1023));
                    if(x<(w/4)*2) expectedRGB[i]=Reference(sdr[i]);
                }
                if(effect/(w*h)<5)throw Failure(5,"HDR effect disappeared while SDR reference was active");
                auto expected=ToP010(expectedRGB,w,h);unsigned left=0,right=0;
                for(unsigned y=0;y<h*3/2;y++)for(unsigned x=0;x<w;x++) {
                    size_t i=static_cast<size_t>(y)*w+x;
                    if(actual[i]&63)throw Failure(5,"Invalid P010 low bits");
                    unsigned delta=static_cast<unsigned>(std::abs(int(actual[i]>>6)-int(expected[i]>>6)));
                    auto& worst=x<(w/4)*2?left:right;worst=std::max(worst,delta);
                }
                std::cout<<w<<" "<<(input10?10:8)<<"bit "<<pattern<<" SDR error="<<left<<" HDR error="<<right<<'\n';
                if(left>2 || right>1)throw Failure(5,"Split differs from independent SDR/PQ or HDR reference");
                D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
                desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_P010;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
                ComPtr<ID3D11Texture2D> texture,staging;
                Check(split.Device()->CreateTexture2D(&desc,nullptr,&texture),"Comparison target");
                split.ProcessTexture(frame++,texture.Get());
                desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                Check(split.Device()->CreateTexture2D(&desc,nullptr,&staging),"Comparison staging");
                split.Context()->CopyResource(staging.Get(),texture.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};Check(split.Context()->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Comparison map");
                bool identical=true;
                for(unsigned row=0;row<h*3/2;row++)if(std::memcmp(static_cast<const uint8_t*>(mapped.pData)+row*mapped.RowPitch,actual.data()+static_cast<size_t>(row)*w,w*2))identical=false;
                split.Context()->Unmap(staging.Get(),0);
                if(!identical)throw Failure(5,"Comparison native texture and pipe buffer differ");
            }
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
