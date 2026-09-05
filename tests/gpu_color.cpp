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
                std::cout << w << "x1080 " << (input10?10:8) << "bit " << pattern << " max code error=" << worst << '\n';
            }
        }
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
