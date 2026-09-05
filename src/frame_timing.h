#pragma once
#include "pipeline.h"
#include <algorithm>
#include <cmath>

// showinfo runs on the frames being converted, before rawvideo writes them to stdout.
// Read its already-written metadata sequentially; never scan or decode the input twice.
class FrameTiming {
    HANDLE log_=INVALID_HANDLE_VALUE;
    std::string pending_;
    long double tick_=0, first_=0;
    static std::string Field(const std::string& line,const std::string& key) {
        auto p=line.find(key);
        if(p==std::string::npos) throw Failure(2,"Decoder frame metadata is incomplete: "+key);
        p+=key.size(); p=line.find_first_not_of(" \t",p);
        return line.substr(p,line.find_first_of(" \t,\r",p)-p);
    }
    std::string Line() {
        for(;;) {
            auto e=pending_.find('\n');
            if(e!=std::string::npos) {auto line=pending_.substr(0,e);pending_.erase(0,e+1);return line;}
            char buf[8192];DWORD n=0;
            if(!ReadFile(log_,buf,sizeof(buf),&n,nullptr) || !n)
                throw Failure(6,"Missing timestamp metadata for decoded frame; inspect decode.log");
            pending_.append(buf,n);
            if(pending_.size()>1024*1024) throw Failure(6,"Decoder log line is too large");
        }
    }
public:
    explicit FrameTiming(const std::filesystem::path& path) {
        log_=CreateFileW(FileSystemPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log_==INVALID_HANDLE_VALUE) throw Failure(6,FileError("Cannot read timing log",path,GetLastError()));
    }
    ~FrameTiming() {if(log_!=INVALID_HANDLE_VALUE) CloseHandle(log_);}
    FrameTiming(const FrameTiming&)=delete;
    FrameTiming& operator=(const FrameTiming&)=delete;
    long double CheckFrame(uint64_t frame,unsigned w,unsigned h,bool input10,long double fps,bool assume) {
        bool found=false;
        for(;;) {
            auto line=Line();
            if(line.find("Parsed_showinfo_")==std::string::npos) continue;
            if(line.find("config in time_base:")!=std::string::npos) {
                auto value=Field(line,"config in time_base:"); auto slash=value.find('/');
                auto tick=std::stold(value.substr(0,slash))/std::stold(value.substr(slash+1));
                if(!std::isfinite(tick) || tick<=0 || (tick_ && tick!=tick_)) throw Failure(2,"Input timebase changed");
                tick_=tick;
            }
            if(line.find(" n:")!=std::string::npos) {
                if(found || std::stoull(Field(line," n:"))!=frame || !tick_)
                    throw Failure(2,"Input frame sequence changed");
                auto size=Field(line," s:");
                if(size!=std::to_string(w)+"x"+std::to_string(h) || Field(line," i:")!="P"
                    || Field(line," fmt:")!=(input10?"p010le":"nv12"))
                    throw Failure(2,"Input dimensions, pixel format or interlace changed");
                long double pts=std::stoll(Field(line," pts:"))*tick_;
                if(!frame) first_=pts;
                if(std::abs((pts-first_)-frame/fps)>std::max(tick_*1.1L,.0001L))
                    throw Failure(2,"Variable/discontinuous timestamps at frame "+std::to_string(frame));
                found=true;
            }
            if(found && line.find("color_range:")!=std::string::npos) {
                for(auto key:{"color_range:","color_space:","color_primaries:","color_trc:"}) {
                    auto value=Field(line,key);
                    std::string expected=std::string(key)=="color_range:"?"tv":"bt709";
                    if(value!=expected && !(assume && value=="unknown")) throw Failure(2,"Input frame color changed");
                }
                return first_;
            }
        }
    }
};
