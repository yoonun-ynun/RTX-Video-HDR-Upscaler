#pragma once
#include "pipeline.h"
#include <memory>
class NativeVideo {
public:
    NativeVideo(Pipeline& pipeline,const std::filesystem::path& input,const std::filesystem::path& output,
                unsigned width,unsigned height,bool input10,unsigned fpsNum,unsigned fpsDen,unsigned cq,
                int64_t bitrate,bool assume);
    ~NativeVideo();
    bool Decode(uint64_t index);
    void Seek(uint64_t frame,long double firstSeconds);
    void Encode(uint64_t index);
    void Finish();
    long double StartSeconds() const;
private:
    struct State;
    std::unique_ptr<State> s_;
};
