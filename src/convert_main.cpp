#include "pipeline.h"
#include "child_process.h"
#include "color.h"
#include "frame_timing.h"
#include "progress_rate.h"
#include <commdlg.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

struct VideoInfo {
    unsigned width=0,height=0;
    bool input10=false;
    unsigned fpsNum=30,fpsDen=1;
    std::string rate, timebase;
    long double fps=0,tick=0,start=0;
    uint64_t frames=0;
};
static long double Rational(const std::string& value) {
    auto slash=value.find('/');
    long double a=std::stold(value.substr(0,slash));
    long double b=slash==std::string::npos?1:std::stold(value.substr(slash+1));
    if(b<=0 || a<=0) throw Failure(2,"Invalid frame rate/timebase");
    return a/b;
}
static void ChildOK(unsigned code,const char* message) { if(code) throw Failure(6,std::string(message)+"; inspect run logs"); }
static std::wstring ParseBitrate(const std::wstring& value) {
    if(value.empty() || value.front()<L'0' || value.front()>L'9') throw Failure(2,"Invalid bitrate");
    size_t end=0; double n=std::stod(value,&end);
    if(end<value.size() && end+1==value.size() && (value[end]==L'M' || value[end]==L'k' || value[end]==L'K'))
        n*=value[end]==L'M'?1000000:1000;
    else if(end!=value.size()) throw Failure(2,"Use a bitrate such as 40M or 40000k");
    if(!std::isfinite(n) || n<100000 || n>1000000000) throw Failure(2,"Bitrate must be 100k..1000M");
    return std::to_wstring(static_cast<uint64_t>(n));
}
static VideoInfo Probe(const std::filesystem::path& tool,const std::filesystem::path& input,
                       const std::filesystem::path& run,bool assume,unsigned maxFrames) {
    ChildProcess probe(tool,{L"-v",L"error",L"-select_streams",L"v:0",L"-show_entries",
        L"stream=width,height,pix_fmt,codec_name,field_order,color_range,color_space,color_transfer,color_primaries,r_frame_rate,avg_frame_rate,time_base,start_time,duration,nb_frames,sample_aspect_ratio:stream_side_data=rotation",
        L"-of",L"default=noprint_wrappers=1",input.wstring()},run/L"probe.log",true,false);
    auto text=probe.Capture(); WriteText(run/L"input-info.txt",text);
    std::map<std::string,std::string> values;
    std::istringstream lines(text); std::string line;
    while(std::getline(lines,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        auto equal=line.find('='); if(equal!=std::string::npos) values[line.substr(0,equal)]=line.substr(equal+1);
    }
    if(!values.contains("width")) throw Failure(2,"Input has no usable video stream");
    VideoInfo v;
    v.width=std::stoul(values["width"]); v.height=std::stoul(values["height"]);
    if(v.width<16 || v.height<2 || v.width>8192 || v.height>8192 || v.width%2 || v.height%2)
        throw Failure(2,"Test version requires even dimensions within 8192x8192");
    if(values["codec_name"]!="h264" && values["codec_name"]!="hevc") throw Failure(2,"Test version supports H.264/HEVC input");
    v.input10=values["pix_fmt"]=="yuv420p10le" || values["pix_fmt"]=="p010le";
    if(values["pix_fmt"]!="yuv420p" && values["pix_fmt"]!="nv12" && !v.input10) throw Failure(2,"Test version requires 8/10-bit 4:2:0 SDR input");
    if(values["field_order"]!="progressive") throw Failure(2,"Input must be explicitly progressive");
    if(values.contains("rotation") && std::stod(values["rotation"])!=0) throw Failure(2,"Rotated input is not supported in this version");
    if(values["sample_aspect_ratio"]!="1:1" && values["sample_aspect_ratio"]!="N/A")
        throw Failure(2,"Test version requires square pixels");
    for(const auto& key:{"color_space","color_transfer","color_primaries"}) {
        auto value=values[key];
        if(value!="bt709" && !(assume && (value=="unknown" || value.empty())))
            throw Failure(2,std::string("Input must be BT.709 SDR: ")+key+"="+value+" (use --assume-bt709 only for untagged SDR)");
    }
    if(values["color_range"]!="tv" && !(assume && (values["color_range"]=="unknown" || values["color_range"].empty())))
        throw Failure(2,"Input must have limited (TV) range");
    v.rate=values["r_frame_rate"]; v.fps=Rational(v.rate);
    auto slash=v.rate.find('/');
    v.fpsNum=std::stoul(v.rate.substr(0,slash));v.fpsDen=std::stoul(v.rate.substr(slash+1));
    v.timebase=values["time_base"]; v.tick=Rational(v.timebase);
    if(v.fps>120 || v.fps<1) throw Failure(2,"Test version supports 1..120 fps");
    // Header counts are progress estimates only. EOF determines the actual count.
    try {v.frames=std::stoull(values["nb_frames"]);} catch(...) {}
    if(!v.frames) try {v.frames=static_cast<uint64_t>(std::stold(values["duration"])*v.fps+.5L);} catch(...) {}
    if(maxFrames) v.frames=v.frames?std::min<uint64_t>(v.frames,maxFrames):maxFrames;
    std::cout << "Input header ready; timestamps will be checked during conversion.\n";
    return v;
}
static double EffectCheck(const AdapterInfo& adapter,const VideoInfo& v,const std::filesystem::path& run,bool diagnostics) {
    auto w=v.width,h=v.height;
    Pipeline off(adapter,w,h,false,v.input10,v.fpsNum,v.fpsDen),on(adapter,w,h,false,v.input10,v.fpsNum,v.fpsDen);
    off.CreateResources(); on.CreateResources(); off.SetHdr(false); on.SetHdr(true);
    auto input=GeneratePattern(w,h,"gray-ramp");
    if(v.input10) {
        std::vector<uint8_t> p010(input.size()*2);
        for(size_t i=0;i<input.size();++i) {p010[2*i]=0;p010[2*i+1]=input[i];}
        input=std::move(p010);
    }
    off.Upload(input); on.Upload(input);
    std::vector<uint32_t> a,b;
    for(unsigned i=0;i<8;++i) { a=off.Process(i);b=on.Process(i); }
    uint64_t difference=0;
    for(size_t i=0;i<a.size();++i) for(unsigned shift:{0u,10u,20u})
        difference+=std::abs(int((a[i]>>shift)&1023)-int((b[i]>>shift)&1023));
    double mae=double(difference)/(a.size()*3);
    WriteText(run/L"hdr-on-environment.json",on.Report());
    WriteText(run/L"hdr-off-environment.json",off.Report());
    if(diagnostics) {
        WriteBytes(run/L"guard-off.rgb10a2",a.data(),a.size()*4);
        WriteBytes(run/L"guard-on.rgb10a2",b.data(),b.size()*4);
    }
    if(mae<5) throw Failure(5,"HDR effect not detected. Check Windows HDR and NVIDIA RTX Video HDR settings");
    return mae;
}
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc==2 && std::wstring(argv[1])==L"--list-gpus") {
        try {
            for(const auto& a:EnumerateAdapters())
                if(a.desc.VendorId==0x10de && !(a.desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))
                    std::cout << a.index << '\t' << Utf8(a.desc.Description) << '\n';
            return 0;
        } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 3;}
    }
    std::filesystem::path run;
    bool interactive=argc==1 || (argc==2 && std::wstring(argv[1]).rfind(L"--",0)!=0);
    try {
        std::filesystem::path input,output,toolDirectory;
        bool assume=false,softwareDecode=false,cpuColor=false,fullVerify=false,diagnostics=false;
        unsigned adapterIndex=0,maxFrames=0;
        std::wstring bitrate; unsigned cq=18; bool cqSet=false;
        if(argc==1) {
            wchar_t path[32768]{};
            OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.lpstrFile=path;
            dialog.nMaxFile=32768; dialog.lpstrFilter=L"SDR video\0*.mp4;*.mkv;*.mov;*.ts\0All files\0*.*\0";
            dialog.lpstrTitle=L"Select an SDR video to convert to HDR";
            dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameW(&dialog)) return 2;
            input=path;
        }
        for(int i=1;i<argc;++i) {
            std::wstring arg=argv[i];
            if(arg==L"--help") {
                std::cout << "RTX Video HDR Convert v0.3.4\nRTXVideoHDRConvert input.mp4 [--output output.hdr.mkv] [--adapter 0]\n"
                    "  [--ffmpeg-dir DIRECTORY] [--assume-bt709] [--max-frames N] [--bitrate 40M | --cq 18]\n"
                    "  [--software-decode] [--cpu-color] [--verify-full] [--diagnostics]\n"
                    "Double-click to choose a file, or drop one file onto this executable.\n"
                    "Test version: progressive 8/10-bit BT.709 limited H.264/HEVC, constant frame rate.\n"
                    "Requires working NVIDIA RTX Video HDR and ffmpeg/ffprobe. Resolution is preserved. Output .mkv (audio copy) or .mp4 (AAC 320k).\n";
                return 0;
            } else if(arg==L"--assume-bt709") assume=true;
            else if(arg==L"--software-decode") softwareDecode=true;
            else if(arg==L"--cpu-color") cpuColor=true;
            else if(arg==L"--verify-full") fullVerify=true;
            else if(arg==L"--diagnostics") diagnostics=true;
            else if(arg==L"--bitrate" || arg==L"--cq") {
                if(++i>=argc) throw Failure(2,"Missing rate control value");
                std::wstring value=argv[i]; size_t end=0;
                if(value.empty() || value.front()<L'0' || value.front()>L'9') throw Failure(2,"Invalid rate control value");
                if(arg==L"--cq") {
                    auto n=std::stoul(value,&end);
                    if(end!=value.size() || n>51) throw Failure(2,"CQ must be 0..51");
                    cq=static_cast<unsigned>(n);cqSet=true;
                } else {
                    bitrate=ParseBitrate(value);
                }
            }
            else if(arg==L"--output" || arg==L"--adapter" || arg==L"--ffmpeg-dir" || arg==L"--max-frames") {
                if(++i>=argc) throw Failure(2,"Missing option value");
                if(arg==L"--output") output=argv[i];
                else if(arg==L"--ffmpeg-dir") toolDirectory=argv[i];
                else if(arg==L"--max-frames") {
                    size_t end=0;auto n=std::stoul(argv[i],&end);
                    if(end!=std::wstring(argv[i]).size() || !n || n>1000000) throw Failure(2,"Invalid maximum frame count");
                    maxFrames=n;
                }
                else {
                    size_t end=0; auto number=std::stoul(argv[i],&end);
                    if(end!=std::wstring(argv[i]).size() || number>100) throw Failure(2,"Invalid adapter index");
                    adapterIndex=number;
                }
            } else if(arg.rfind(L"--",0)==0 || !input.empty()) throw Failure(2,"Unknown option or more than one input");
            else input=arg;
        }
        if(cqSet && !bitrate.empty()) throw Failure(2,"Choose --bitrate or --cq, not both");
        if(interactive) {
            std::cout << "Average video bitrate (e.g. 40M), or Enter for quality mode CQ 18: " << std::flush;
            std::string value;
            if(!std::getline(std::cin,value)) throw Failure(2,"Bitrate selection cancelled");
            if(!value.empty()) bitrate=ParseBitrate(Wide(value));
        }
        const auto launched=std::chrono::steady_clock::now();
        if(input.empty() || !std::filesystem::is_regular_file(FileSystemPath(input))) throw Failure(2,"Input file not found");
        input=std::filesystem::absolute(input);
        if(output.empty()) output=input.parent_path()/(input.stem().wstring()+L".hdr.mkv");
        output=std::filesystem::absolute(output);
        auto extension=output.extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
        bool mp4=extension==L".mp4";
        if(extension!=L".mkv" && !mp4) throw Failure(2,"Output must be .mkv or .mp4");
        if(std::filesystem::exists(FileSystemPath(output))) throw Failure(2,"Output exists; choose a new name");
        std::filesystem::create_directories(FileSystemPath(output.parent_path()));
        run=CreateRunDirectory(output);
        auto ffmpeg=FindTool(L"ffmpeg.exe",toolDirectory),ffprobe=FindTool(L"ffprobe.exe",toolDirectory);
        std::cout << "RTX Video HDR Convert v0.3.4\nInput: " << Utf8(input.c_str()) << '\n';
        auto v=Probe(ffprobe,input,run,assume,maxFrames);
        auto adapters=EnumerateAdapters(); const AdapterInfo* chosen=nullptr;
        for(const auto& a:adapters) if(a.index==adapterIndex && a.desc.VendorId==0x10de && !(a.desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) chosen=&a;
        if(!chosen) throw Failure(3,"Selected NVIDIA adapter unavailable");
        std::cout << "Checking actual HDR effect on " << Utf8(chosen->desc.Description) << "...\n";
        double effect=EffectCheck(*chosen,v,run,diagnostics);
        Pipeline pipeline(*chosen,v.width,v.height,false,v.input10,v.fpsNum,v.fpsDen);
        pipeline.CreateResources();pipeline.SetHdr(true);
        WriteText(run/L"environment.json",pipeline.Report());
        auto encoded=run/L"video.mkv",completed=run/(mp4?L"completed.mp4":L"completed.mkv");
        std::vector<std::wstring> encodeArgs{L"-hide_banner",L"-loglevel",L"warning",L"-nostdin",L"-n",
            L"-init_hw_device",L"d3d11va=enc:"+std::to_wstring(adapterIndex),L"-filter_hw_device",L"enc",
            L"-f",L"rawvideo",L"-pixel_format",L"p010le",L"-video_size",std::to_wstring(v.width)+L"x"+std::to_wstring(v.height),
            L"-framerate",Wide(v.rate),L"-color_range",L"tv",L"-colorspace",L"bt2020nc",L"-color_trc",L"smpte2084",L"-color_primaries",L"bt2020",
            L"-i",L"pipe:0",L"-an",L"-vf",L"hwupload",L"-c:v",L"hevc_nvenc",L"-profile:v",L"main10",L"-preset",L"p5",L"-tune",L"hq",
            L"-rc",L"vbr",L"-b:v",bitrate.empty()?L"0":bitrate,L"-bf",L"0",
            L"-color_range",L"tv",L"-colorspace",L"bt2020nc",L"-color_trc",L"smpte2084",L"-color_primaries",L"bt2020",
            L"-chroma_sample_location",L"center",L"-bsf:v",L"hevc_metadata=chroma_sample_loc_type=1",
            L"-fps_mode",L"passthrough",encoded.wstring()};
        if(bitrate.empty()) encodeArgs.insert(encodeArgs.end()-1,{L"-cq",std::to_wstring(cq)});
        std::cout << "Rate control: " << (bitrate.empty()?"quality CQ "+std::to_string(cq):"VBR average "+Utf8(bitrate.c_str())+" bit/s") << '\n';
        ChildProcess encoder(ffmpeg,encodeArgs,run/L"encode.log",false,true);
        std::vector<std::wstring> decodeArgs{L"-hide_banner",L"-loglevel",L"info",L"-nostats",L"-nostdin",L"-xerror"};
        if(!softwareDecode) decodeArgs.insert(decodeArgs.end(),{L"-hwaccel",L"d3d11va",L"-hwaccel_device",std::to_wstring(adapterIndex),L"-hwaccel_output_format",L"d3d11"});
        decodeArgs.insert(decodeArgs.end(),{L"-copyts",L"-noautorotate",L"-reinit_filter",L"0",L"-i",input.wstring(),L"-map",L"0:v:0",L"-an",L"-sn",L"-dn"});
        if(maxFrames) decodeArgs.insert(decodeArgs.end(),{L"-frames:v",std::to_wstring(maxFrames)});
        std::wstring filter=(softwareDecode?L"format=":L"hwdownload,format=")+std::wstring(v.input10?L"p010le":L"nv12")+L",showinfo=checksum=0";
        decodeArgs.insert(decodeArgs.end(),{L"-vf",filter,L"-fps_mode",L"passthrough",L"-f",L"rawvideo",L"pipe:1"});
        ChildProcess decoder(ffmpeg,decodeArgs,run/L"decode.log",true,false);
        FrameTiming timing(run/L"decode.log");
        std::vector<uint16_t> p010;
        double readSeconds=0,processSeconds=0,writeSeconds=0,firstFrameSeconds=0;
        std::vector<uint8_t> nv12(static_cast<size_t>(v.width)*v.height*3/2*(v.input10?2:1));
        uint64_t count=0;
        auto start=std::chrono::steady_clock::now();
        ProgressRate rate;
        ProgressRate::Rates rates{};
        double lastProgressSeconds=0,lastFrameSeconds=0;
        for(;;) {
            auto stage=std::chrono::steady_clock::now();
            size_t got=0;
            while(got<nv12.size()) { auto n=decoder.Read(nv12.data()+got,nv12.size()-got); if(!n) break;got+=n; }
            if(!got) break;
            if(got!=nv12.size()) throw Failure(6,"Decoder returned a partial frame");
            v.start=timing.CheckFrame(count,v.width,v.height,v.input10,v.fps,assume);
            readSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
            stage=std::chrono::steady_clock::now();
            pipeline.Upload(nv12);
            bool sample=diagnostics && (count==0 || count==v.frames/2 || count+1==v.frames);
            if(cpuColor || sample) {
                auto rgb=pipeline.Process(static_cast<unsigned>(count));
                if(cpuColor) p010=ToP010(rgb,v.width,v.height);
                else pipeline.ReadP010(p010);
                if(sample) {
                    auto reference=ToP010(rgb,v.width,v.height);
                    unsigned worst=0;
                    for(size_t k=0;k<p010.size();++k) worst=std::max(worst,static_cast<unsigned>(std::abs(int(p010[k]>>6)-int(reference[k]>>6))));
                    if(worst>1) throw Failure(5,"GPU P010 differs from CPU reference by more than one code");
                    auto stem=std::string("frame-")+std::to_string(count);
                    WriteBytes(run/(stem+".rgb10a2"),rgb.data(),rgb.size()*4);
                    WriteBytes(run/(stem+".p010"),p010.data(),p010.size()*2);
                }
            } else pipeline.ProcessP010(static_cast<unsigned>(count),p010);
            processSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
            stage=std::chrono::steady_clock::now();
            encoder.Write(p010.data(),p010.size()*2);
            writeSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
            if(!count) firstFrameSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-launched).count();
            ++count;
            double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            lastFrameSeconds=elapsed;
            rates=rate.Observe(elapsed,count);
            if(count==1 || elapsed-lastProgressSeconds>=.5 || count==v.frames) {
                lastProgressSeconds=elapsed;
                std::ostringstream progress;
                progress.setf(std::ios::fixed);progress.precision(1);
                progress << count << " frames";
                if(v.frames) progress << " / ~" << v.frames;
                progress << ", recent " << rates.recent << " fps, average " << rates.average << " fps";
                if(v.frames>count && rates.recent>0) progress << ", ~" << static_cast<uint64_t>(std::ceil((v.frames-count)/rates.recent)) << "s remaining";
                std::cout << "\r" << progress.str() << "          " << std::flush;
            }
        }
        ChildOK(decoder.Wait(),"Decode failed");
        if(!count) throw Failure(6,"No decodable frames");
        v.frames=count;
        const double conversionSeconds=lastFrameSeconds;
        std::ostringstream timingReport;
        timingReport << "{\"frames\":" << count << ",\"fps\":\"" << v.rate << "\",\"video_start_seconds\":" << static_cast<double>(v.start) << "}\n";
        WriteText(run/L"timing.json",timingReport.str());
        encoder.CloseInput(); ChildOK(encoder.Wait(),"Encode failed");
        std::cout << "\nPreserving audio and finalizing file...\n";
        std::wostringstream offset;offset.precision(15);offset << -v.start;
        std::vector<std::wstring> muxArgs{L"-hide_banner",L"-loglevel",L"warning",L"-nostdin",L"-n",L"-copyts",
            L"-i",encoded.wstring(),L"-itsoffset",offset.str(),L"-i",input.wstring(),L"-map",L"0:v:0",L"-map",L"1:a?",
            L"-c",L"copy",L"-map_metadata",L"-1",L"-map_chapters",L"-1",L"-avoid_negative_ts",L"disabled"};
        if(maxFrames) {std::wostringstream duration;duration.precision(15);duration << v.frames/v.fps;muxArgs.push_back(L"-t");muxArgs.push_back(duration.str());}
        if(mp4) muxArgs.insert(muxArgs.end(),{L"-c:a",L"aac",L"-b:a",L"320k",L"-tag:v",L"hvc1",L"-movflags",L"+faststart"});
        muxArgs.push_back(completed.wstring());
        ChildProcess mux(ffmpeg,muxArgs,run/L"mux.log",false,false);
        ChildOK(mux.Wait(INFINITE),"Audio mux failed");
        std::vector<std::wstring> verifyArgs{L"-v",L"error",L"-select_streams",L"v:0",L"-show_entries",
            L"stream=codec_name,profile,width,height,pix_fmt,color_range,color_space,color_transfer,color_primaries,chroma_location,nb_read_frames",
            L"-of",L"default=noprint_wrappers=1",completed.wstring()};
        if(fullVerify) verifyArgs.insert(verifyArgs.begin(),L"-count_frames");
        ChildProcess verify(ffprobe,verifyArgs,run/L"verify.log",true,false);
        auto verified=verify.Capture(); WriteText(run/L"output-info.txt",verified);
        for(auto item:{"width="+std::to_string(v.width)+"\n","height="+std::to_string(v.height)+"\n"})
            if(verified.find(item)==std::string::npos) throw Failure(5,"Output dimensions verification failed");
        for(auto item:{"codec_name=hevc","profile=Main 10","pix_fmt=yuv420p10le","color_range=tv","color_space=bt2020nc","color_transfer=smpte2084","color_primaries=bt2020","chroma_location=center"})
            if(verified.find(item)==std::string::npos) throw Failure(5,"Output codec/color verification failed");
        if(fullVerify && verified.find("nb_read_frames="+std::to_string(count)+"\n")==std::string::npos) throw Failure(5,"Output frame count verification failed");
        // MoveFileEx without REPLACE_EXISTING protects an output created while conversion was running.
        if(!MoveFileExW(FileSystemPath(completed).c_str(),FileSystemPath(output).c_str(),MOVEFILE_WRITE_THROUGH))
            throw Failure(6,FileError("Cannot finalize output",output,GetLastError()));
        std::ostringstream report;
        report << "{\"status\":\"completed\",\"test_version\":\"0.3.4\",\"frames\":" << count
            << ",\"hdr_effect_mae\":" << effect << ",\"input\":" << JsonString(Utf8(input.c_str()))
            << ",\"output\":" << JsonString(Utf8(output.c_str()))
            << ",\"width\":" << v.width << ",\"height\":" << v.height
            << ",\"input_bit_depth\":" << (v.input10?10:8)
            << ",\"limited_to_max_frames\":" << (maxFrames?"true":"false")
            << ",\"fps\":\"" << v.rate << "\",\"pixel_contract\":\"BT.2020/PQ RGB -> limited centered-chroma P010\","
            << "\"browser_equivalence_verified\":false,\"assumed_bt709\":" << (assume?"true":"false") << "}\n";
        auto reportText=report.str();
        reportText.erase(reportText.rfind('}'));
        std::ostringstream performance;
        performance << ",\"hardware_decode\":" << (softwareDecode?"false":"true")
            << ",\"gpu_color\":" << (cpuColor?"false":"true")
            << ",\"adapter_index\":" << adapterIndex
            << ",\"container\":\"" << (mp4?"mp4":"mkv") << "\",\"audio_mode\":\"" << (mp4?"aac_320k":"copy") << "\""
            << ",\"full_output_decode_verified\":" << (fullVerify?"true":"false")
            << ",\"target_bitrate\":" << (bitrate.empty()?"null":Utf8(bitrate.c_str()))
            << ",\"cq\":" << (bitrate.empty()?std::to_string(cq):"null")
            << ",\"first_frame_seconds\":" << firstFrameSeconds
            << ",\"conversion_seconds\":" << conversionSeconds
            << ",\"processing_fps\":" << count/conversionSeconds
            << ",\"recent_processing_fps\":" << rates.recent << ",\"recent_window_seconds\":5"
            << ",\"read_seconds\":" << readSeconds << ",\"process_seconds\":" << processSeconds
            << ",\"write_seconds\":" << writeSeconds << "}\n";
        WriteText(run/L"result.json",reportText+performance.str());
        std::cout << "Processing average: " << count/conversionSeconds << " fps; first frame: " << firstFrameSeconds << "s\n";
        std::cout << "Done: " << Utf8(output.c_str()) << "\nLogs: " << Utf8(run.c_str()) << '\n';
        if(interactive) {std::cout << "Press Enter to close.\n";std::cin.get();}
        return 0;
    } catch(const std::exception& e) {
        int code=4;if(auto f=dynamic_cast<const Failure*>(&e)) code=f->code;
        std::cerr << "\nConversion failed: " << e.what() << '\n';
        if(!run.empty()) {
            std::cerr << "Logs: " << Utf8(run.c_str()) << '\n';
            try {WriteText(run/L"error.json","{\"exit_code\":"+std::to_string(code)+",\"error\":"+JsonString(e.what())+"}\n");}catch(...){}
        }
        if(interactive) {std::cerr << "Press Enter to close.\n";std::cin.get();}
        return code;
    }
}
