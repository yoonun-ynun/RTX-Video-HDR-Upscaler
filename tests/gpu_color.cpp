#include "pipeline.h"
#include "color.h"
#include <iostream>
int main() {
    try {
        auto adapters=EnumerateAdapters();
        auto a=std::find_if(adapters.begin(),adapters.end(),[](const auto& x){return x.desc.VendorId==0x10de;});
        if(a==adapters.end()) throw Failure(3,"NVIDIA GPU required");
        for(unsigned w:{1918u,1920u}) for(bool input10:{false,true}) {
            Pipeline p(*a,w,1080,false,input10);p.CreateResources();p.SetHdr(false);
            unsigned frame=0;
            for(auto pattern:{"gray-ramp","color-bars"}) {
                auto input=GeneratePattern(w,1080,pattern);
                if(input10) {
                    std::vector<uint8_t> expanded(input.size()*2);
                    for(size_t i=0;i<input.size();++i) expanded[i*2+1]=input[i];
                    input=std::move(expanded);
                }
                p.Upload(input); auto rgb=p.Process(frame++);auto cpu=ToP010(rgb,w,1080);
                std::vector<uint16_t> gpu;p.ReadP010(gpu);
                if(cpu.size()!=gpu.size()) throw Failure(5,"P010 size mismatch");
                unsigned worst=0;
                for(size_t i=0;i<cpu.size();++i) {
                    if(gpu[i]&63) throw Failure(5,"P010 low bits are not zero");
                    worst=std::max(worst,static_cast<unsigned>(std::abs(int(gpu[i]>>6)-int(cpu[i]>>6))));
                }
                if(worst>1) throw Failure(5,"GPU/CPU P010 mismatch");
                p.Upload(input);p.SubmitP010(frame++);
                bool rejected=false;
                try {p.Upload(input);} catch(const Failure&) {rejected=true;}
                if(!rejected) throw Failure(5,"Pending GPU input reuse was allowed");
                std::vector<uint16_t> queued;p.CollectP010(queued);
                if(queued!=gpu) throw Failure(5,"Split submit/collect changed P010 pixels");
                rejected=false;
                try {p.CollectP010(queued);} catch(const Failure&) {rejected=true;}
                if(!rejected) throw Failure(5,"Collect without submit was allowed");
                D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=1080;desc.MipLevels=desc.ArraySize=1;
                desc.SampleDesc.Count=1;desc.Format=input10?DXGI_FORMAT_P010:DXGI_FORMAT_NV12;
                desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                ComPtr<ID3D11Texture2D> inputTexture,target,staging;
                Check(p.Device()->CreateTexture2D(&desc,nullptr,&inputTexture),"Test input texture");
                p.Context()->UpdateSubresource(inputTexture.Get(),0,nullptr,input.data(),w*(input10?2:1),0);
                p.UploadTexture(inputTexture.Get(),0);
                desc.Format=DXGI_FORMAT_P010;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
                Check(p.Device()->CreateTexture2D(&desc,nullptr,&target),"Test P010 texture");
                p.ProcessTexture(frame++,target.Get());
                desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                Check(p.Device()->CreateTexture2D(&desc,nullptr,&staging),"Test staging texture");
                p.Context()->CopyResource(staging.Get(),target.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};Check(p.Context()->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Test texture map");
                bool identical=true;
                for(unsigned row=0;row<1080*3/2;++row) {
                    auto actual=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+row*mapped.RowPitch);
                    for(unsigned x=0;x<w;++x) if(actual[x]!=gpu[static_cast<size_t>(row)*w+x]) identical=false;
                }
                p.Context()->Unmap(staging.Get(),0);
                if(!identical) throw Failure(5,"Direct GPU texture differs from reference P010");
                std::cout << w << "x1080 " << (input10?10:8) << "bit " << pattern << " max code error=" << worst << '\n';
            }
        }
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
