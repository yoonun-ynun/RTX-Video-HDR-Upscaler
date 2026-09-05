#include "pipeline.h"
#include <charconv>
#include <iostream>
#include <map>
#include <sstream>

static unsigned Number(const std::string& s) {
    unsigned value=0;
    auto r=std::from_chars(s.data(),s.data()+s.size(),value);
    if (r.ec!=std::errc{} || r.ptr!=s.data()+s.size()) throw Failure(2,"Invalid unsigned integer: "+s);
    return value;
}
int wmain(int argc, wchar_t** argv) {
    std::filesystem::path errorDir;
    try {
        if (argc<2 || std::wstring(argv[1])==L"--help") {
            std::cout << "RTXVideoHDRTest probe [--adapter N] [--size WxH] [--output-color pq|sdr] [--report FILE]\n"
                "RTXVideoHDRTest frame [--adapter N] [--size WxH] [--output-color pq|sdr] --hdr off|on|none\n"
                "  [--pattern gray-ramp|color-bars] [--frames N] --out NEW_DIRECTORY\n"
                "Diagnostic only: --experimental-conversion yes permits a vendor conversion probe.\n";
            return 0;
        }
        std::string command=Utf8(argv[1]);
        if (command!="probe" && command!="frame") throw Failure(2,"Unknown command");
        std::map<std::string,std::wstring> opts;
        for (int i=2;i<argc;i+=2) {
            std::string key=Utf8(argv[i]);
            if (i+1>=argc) throw Failure(2,"Missing option value");
            if (key!="--adapter" && key!="--size" && key!="--output-color" && key!="--report"
                && key!="--hdr" && key!="--pattern" && key!="--frames" && key!="--out"
                && key!="--experimental-conversion" && key!="--input-depth") throw Failure(2,"Unknown option: "+key);
            if (!opts.emplace(key,argv[i+1]).second) throw Failure(2,"Duplicate option: "+key);
        }
        auto get=[&](const std::string& k,const std::string& def) {return opts.contains(k)?Utf8(opts.at(k).c_str()):def;};
        std::string size=get("--size","1920x1080");
        auto sep=size.find('x');
        if (sep==std::string::npos) throw Failure(2,"Size must be WIDTHxHEIGHT");
        unsigned w=Number(size.substr(0,sep)),h=Number(size.substr(sep+1));
        if (w<16 || h<2 || w>8192 || h>8192 || w%2 || h%2) throw Failure(2,"Size must be even, 16..8192 x 2..8192");
        auto color=get("--output-color","pq");
        if (color!="pq" && color!="sdr") throw Failure(2,"Output color must be pq or sdr");
        auto hdr=get("--hdr","none");
        if (hdr!="off" && hdr!="on" && hdr!="none") throw Failure(2,"HDR must be off/on/none");
        auto experiment=get("--experimental-conversion","no");
        if (experiment!="yes" && experiment!="no") throw Failure(2,"Experimental conversion must be yes/no");
        unsigned frames=Number(get("--frames","1"));
        if (frames<1 || frames>1200) throw Failure(2,"Frames must be 1..1200");
        std::string pattern=get("--pattern","gray-ramp");
        if (pattern!="gray-ramp" && pattern!="color-bars") throw Failure(2,"Unknown pattern");
        auto adapters=EnumerateAdapters();
        const AdapterInfo* chosen=nullptr;
        for (const auto& a:adapters) {
            std::cout << "Adapter " << a.index << ": " << Utf8(a.desc.Description) << '\n';
            if (a.desc.VendorId==0x10de && !(a.desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) {
                if (opts.contains("--adapter")) { if (a.index==Number(get("--adapter","0"))) chosen=&a; }
                else if (!chosen) chosen=&a;
            }
        }
        if (!chosen) throw Failure(3,"No selected NVIDIA hardware adapter");
        if (command=="frame") {
            if (!opts.contains("--out")) throw Failure(2,"--out is required");
            std::filesystem::path out(opts.at("--out"));
            if (std::filesystem::exists(out)) throw Failure(2,"Output directory already exists");
            std::filesystem::create_directories(out);
            errorDir=out;
        }
        auto depth=get("--input-depth","8");
        if(depth!="8" && depth!="10") throw Failure(2,"Input depth must be 8 or 10");
        Pipeline pipeline(*chosen,w,h,color=="pq",depth=="10");
        if (pipeline.Supported() || experiment=="yes") {
            pipeline.CreateResources(experiment=="yes");
            if (hdr!="none") pipeline.SetHdr(hdr=="on");
        }
        std::cout << pipeline.Report();
        if (command=="probe") {
            if (opts.contains("--report")) WriteText(std::filesystem::path(opts.at("--report")),pipeline.Report());
            return pipeline.Supported()?0:3;
        }
        WriteText(errorDir/L"environment.json",pipeline.Report());
        if (!pipeline.Supported() && experiment!="yes") throw Failure(3,"Requested color conversion unsupported");
        auto bytes=GeneratePattern(w,h,pattern);
        if(depth=="10") {
            std::vector<uint8_t> p010(bytes.size()*2);
            for(size_t i=0;i<bytes.size();++i) {p010[i*2]=0;p010[i*2+1]=bytes[i];}
            bytes=std::move(p010);
        }
        WriteBytes(errorDir/(depth=="10"?L"input.p010":L"input.nv12"),bytes.data(),bytes.size());
        pipeline.Upload(bytes);
        std::ostringstream metrics;
        metrics << "frame,r_min,r_max,g_min,g_max,b_min,b_max,row_pitch\n";
        for (unsigned f=0;f<frames;++f) {
            auto pixels=pipeline.Process(f);
            unsigned lo[3]{1023,1023,1023},hi[3]{};
            for (uint32_t p:pixels) for(unsigned c=0;c<3;++c) {
                unsigned v=(p>>(c*10))&1023;
                lo[c]=std::min(lo[c],v); hi[c]=std::max(hi[c],v);
            }
            metrics << f;
            for(unsigned c=0;c<3;++c) metrics << ',' << lo[c] << ',' << hi[c];
            metrics << ',' << pipeline.lastRowPitch << '\n';
            // Keep first/last frames; statistics cover every frame without unbounded disk use.
            if (f==0 || f+1==frames) {
                auto stem=std::string("frame-")+std::to_string(f);
                WriteBytes(errorDir/(stem+".rgb10a2"),pixels.data(),pixels.size()*4);
                std::ostringstream sidecar;
                sidecar << "{\"schema_version\":1,\"width\":" << w << ",\"height\":" << h
                    << ",\"frame\":" << f << ",\"pts\":" << f << ",\"timebase\":[1,30],"
                    << "\"format\":\"R10G10B10A2_UNORM\",\"byte_order\":\"little-endian\",\"row_padding\":false,"
                    << "\"source_row_pitch\":" << pipeline.lastRowPitch
                    << ",\"requested_output_color\":" << JsonString(color)
                    << ",\"verified_color\":\"unknown\",\"hdr_extension\":" << JsonString(pipeline.HdrState()) << ",\"pattern\":"
                    << JsonString(pattern) << "}\n";
                WriteText(errorDir/(stem+".json"),sidecar.str());
            }
        }
        WriteText(errorDir/L"metrics.csv",metrics.str());
        std::cout << "Completed " << frames << " frames; HDR not verified.\n";
        return 0;
    } catch (const std::exception& e) {
        int code=4;
        if (auto f=dynamic_cast<const Failure*>(&e)) code=f->code;
        std::cerr << e.what() << '\n';
        if (!errorDir.empty()) try {
            WriteText(errorDir/L"error.json","{\"error\":"+JsonString(e.what())+",\"exit_code\":"+std::to_string(code)+"}\n");
        } catch (...) {}
        return code;
    }
}
