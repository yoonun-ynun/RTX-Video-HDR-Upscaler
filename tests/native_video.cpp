#include "native_video.h"
#include <chrono>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=10)throw Failure(2,"input output width height input10 fpsNum fpsDen maxFrames adapter");
        auto adapters=EnumerateAdapters();unsigned adapter=std::stoul(argv[9]);
        auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto& x){return x.index==adapter;});
        if(a==adapters.end())throw Failure(3,"Adapter missing");
        unsigned w=std::stoul(argv[3]),h=std::stoul(argv[4]),n=std::stoul(argv[6]),d=std::stoul(argv[7]),limit=std::stoul(argv[8]);
        bool ten=std::stoul(argv[5])!=0;
        if(std::filesystem::exists(argv[2]))throw Failure(2,"Output exists");
        Pipeline p(*a,w,h,false,ten,n,d);p.CreateResources();p.SetHdr(true);
        NativeVideo video(p,argv[1],argv[2],w,h,ten,n,d,18,0,false);
        auto start=std::chrono::steady_clock::now();uint64_t count=0;
        while((!limit || count<limit) && video.Decode(count)) {video.Encode(count);++count;}
        video.Finish();double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout << "Native GPU frames=" << count << " seconds=" << elapsed << " fps=" << count/elapsed << '\n';return 0;
    }catch(const std::exception& e){std::cerr << e.what()<<'\n';return 1;}
}
