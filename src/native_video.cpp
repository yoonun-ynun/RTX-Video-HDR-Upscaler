#include "native_video.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <chrono>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
}
namespace {
std::mutex nativeLogMutex;
FILE* nativeLog=nullptr;
void NativeLog(void* context,int level,const char* format,va_list args) {
    if(level>AV_LOG_INFO)return;
    std::lock_guard<std::mutex> guard(nativeLogMutex);
    if(!nativeLog)return;
    char line[4096]{};int prefix=1;av_log_format_line2(context,level,format,args,line,sizeof(line),&prefix);
    std::fputs(line,nativeLog);std::fflush(nativeLog);
}

void Av(int result,const char* op) {
    if(result<0) {char error[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(result,error,sizeof(error));throw Failure(6,std::string(op)+": "+error+" ("+std::to_string(result)+")");}
}
AVPixelFormat HardwareFormat(AVCodecContext*,const AVPixelFormat* formats) {
    for(auto p=formats;*p!=AV_PIX_FMT_NONE;++p) if(*p==AV_PIX_FMT_D3D11) return *p;
    return AV_PIX_FMT_NONE;
}
}
struct NativeVideo::State {
    Pipeline& pipeline;
    FILE* log=nullptr;
    std::chrono::steady_clock::time_point lastReport=std::chrono::steady_clock::now();
    AVFormatContext *input=nullptr,*output=nullptr;
    AVCodecContext *decoder=nullptr,*encoder=nullptr;
    AVBufferRef *device=nullptr,*frames=nullptr;
    AVFrame *decoded=nullptr,*encoded=nullptr;
    AVPacket *packet=nullptr,*coded=nullptr;
    int stream=-1;unsigned width,height;bool input10,assume,draining=false,finished=false;
    AVRational rate;long double first=0;
    State(Pipeline& p,unsigned w,unsigned h,bool ten,AVRational r,bool a):pipeline(p),width(w),height(h),input10(ten),assume(a),rate(r) {}
    ~State() {
        av_frame_free(&decoded);av_frame_free(&encoded);av_packet_free(&packet);av_packet_free(&coded);
        avcodec_free_context(&encoder);avcodec_free_context(&decoder);
        av_buffer_unref(&frames);av_buffer_unref(&device);avformat_close_input(&input);
        if(output) {if(output->pb) avio_closep(&output->pb);avformat_free_context(output);}
        if(log) {std::lock_guard<std::mutex> guard(nativeLogMutex);av_log_set_callback(av_log_default_callback);nativeLog=nullptr;std::fclose(log);}
    }
    void Packets() {
        for(;;) {
            int result=avcodec_receive_packet(encoder,coded);
            if(result==AVERROR(EAGAIN) || result==AVERROR_EOF) return;
            Av(result,"Receive NVENC packet");
            av_packet_rescale_ts(coded,encoder->time_base,output->streams[0]->time_base);coded->stream_index=0;
            Av(av_interleaved_write_frame(output,coded),"Write native video packet");av_packet_unref(coded);
        }
    }
};
NativeVideo::NativeVideo(Pipeline& pipeline,const std::filesystem::path& input,const std::filesystem::path& output,
    unsigned w,unsigned h,bool input10,unsigned fpsNum,unsigned fpsDen,unsigned cq,int64_t bitrate,bool assume)
    :s_(std::make_unique<State>(pipeline,w,h,input10,AVRational{static_cast<int>(fpsNum),static_cast<int>(fpsDen)},assume)) {
    auto& s=*s_;
    if(_wfopen_s(&s.log,FileSystemPath(output.parent_path()/L"native.log").c_str(),L"wb") || !s.log) throw Failure(6,"Cannot create native log");
    {std::lock_guard<std::mutex> guard(nativeLogMutex);nativeLog=s.log;av_log_set_callback(NativeLog);}
    WriteText(output.parent_path()/L"native-config.json","{\"ffmpeg_version\":"+JsonString(av_version_info())+",\"input\":"+JsonString(Utf8(input.c_str()))+
        ",\"width\":"+std::to_string(w)+",\"height\":"+std::to_string(h)+",\"cq\":"+std::to_string(cq)+",\"bitrate\":"+std::to_string(bitrate)+
        ",\"preset\":\"p5\",\"tune\":\"hq\",\"surfaces\":8,\"delay\":4}\n");
    Av(avformat_open_input(&s.input,Utf8(FileSystemPath(input).c_str()).c_str(),nullptr,nullptr),"Open native input");
    Av(avformat_find_stream_info(s.input,nullptr),"Read native stream headers");
    // Match the v:0 stream selected by the existing header probe and pipe path.
    for(unsigned i=0;i<s.input->nb_streams;++i) if(s.input->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {s.stream=static_cast<int>(i);break;}
    if(s.stream<0)throw Failure(2,"No native video stream");
    auto codec=avcodec_find_decoder(s.input->streams[s.stream]->codecpar->codec_id);
    if(!codec) throw Failure(2,"Native decoder unavailable");
    s.decoder=avcodec_alloc_context3(codec);if(!s.decoder) throw Failure(6,"Allocate decoder");
    Av(avcodec_parameters_to_context(s.decoder,s.input->streams[s.stream]->codecpar),"Configure native decoder");
    s.device=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);if(!s.device) throw Failure(6,"Allocate D3D11 device context");
    auto hw=reinterpret_cast<AVD3D11VADeviceContext*>(reinterpret_cast<AVHWDeviceContext*>(s.device->data)->hwctx);
    hw->device=pipeline.Device();hw->device->AddRef();Av(av_hwdevice_ctx_init(s.device),"Initialize shared D3D11 device");
    s.decoder->hw_device_ctx=av_buffer_ref(s.device);s.decoder->get_format=HardwareFormat;
    s.decoder->thread_count=1;s.decoder->pkt_timebase=s.input->streams[s.stream]->time_base;
    s.decoder->err_recognition=AV_EF_EXPLODE;Av(avcodec_open2(s.decoder,codec,nullptr),"Open D3D11 decoder");
    s.frames=av_hwframe_ctx_alloc(s.device);if(!s.frames) throw Failure(6,"Allocate encoder frames");
    auto frameContext=reinterpret_cast<AVHWFramesContext*>(s.frames->data);
    frameContext->format=AV_PIX_FMT_D3D11;frameContext->sw_format=AV_PIX_FMT_P010;
    frameContext->width=w;frameContext->height=h;frameContext->initial_pool_size=0;
    auto frameHw=reinterpret_cast<AVD3D11VAFramesContext*>(frameContext->hwctx);
    frameHw->BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    Av(av_hwframe_ctx_init(s.frames),"Initialize P010 texture pool");
    codec=avcodec_find_encoder_by_name("hevc_nvenc");if(!codec) throw Failure(2,"Native NVENC unavailable");
    s.encoder=avcodec_alloc_context3(codec);if(!s.encoder) throw Failure(6,"Allocate encoder");
    auto e=s.encoder;e->width=w;e->height=h;e->pix_fmt=AV_PIX_FMT_D3D11;
    e->hw_frames_ctx=av_buffer_ref(s.frames);e->time_base=av_inv_q(s.rate);e->framerate=s.rate;
    e->sample_aspect_ratio={1,1};e->max_b_frames=0;e->bit_rate=bitrate;e->profile=AV_PROFILE_HEVC_MAIN_10;
    e->color_range=AVCOL_RANGE_MPEG;e->colorspace=AVCOL_SPC_BT2020_NCL;e->color_primaries=AVCOL_PRI_BT2020;
    e->color_trc=AVCOL_TRC_SMPTE2084;e->chroma_sample_location=AVCHROMA_LOC_CENTER;
    Av(avformat_alloc_output_context2(&s.output,nullptr,"matroska",Utf8(FileSystemPath(output).c_str()).c_str()),"Create native muxer");
    if(s.output->oformat->flags&AVFMT_GLOBALHEADER)e->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    Av(av_opt_set(e->priv_data,"preset","p5",0),"Set preset");Av(av_opt_set(e->priv_data,"tune","hq",0),"Set tune");
    Av(av_opt_set(e->priv_data,"rc","vbr",0),"Set rate control");
    if(!bitrate)Av(av_opt_set_double(e->priv_data,"cq",cq,0),"Set CQ");
    Av(av_opt_set_int(e->priv_data,"surfaces",8,0),"Set surfaces");Av(av_opt_set_int(e->priv_data,"delay",4,0),"Set async depth");
    Av(avcodec_open2(e,codec,nullptr),"Open texture NVENC");
    auto stream=avformat_new_stream(s.output,nullptr);if(!stream)throw Failure(6,"Allocate output stream");
    stream->time_base=e->time_base;stream->avg_frame_rate=s.rate;stream->r_frame_rate=s.rate;Av(avcodec_parameters_from_context(stream->codecpar,e),"Set output parameters");
    Av(avio_open(&s.output->pb,Utf8(FileSystemPath(output).c_str()).c_str(),AVIO_FLAG_WRITE),"Open native output");
    Av(avformat_write_header(s.output,nullptr),"Write native header");
    s.decoded=av_frame_alloc();s.encoded=av_frame_alloc();s.packet=av_packet_alloc();s.coded=av_packet_alloc();
    if(!s.decoded || !s.encoded || !s.packet || !s.coded)throw Failure(6,"Allocate native frame/packet");
}
NativeVideo::~NativeVideo()=default;
bool NativeVideo::Decode(uint64_t index) {
    auto& s=*s_;av_frame_unref(s.decoded);
    for(;;) {
        int result=avcodec_receive_frame(s.decoder,s.decoded);
        if(result==AVERROR_EOF)return false;
        if(result>=0)break;
        if(result!=AVERROR(EAGAIN))Av(result,"Decode hardware frame");
        if(s.draining)throw Failure(6,"Native decoder stalled during flush");
        for(;;) {
            result=av_read_frame(s.input,s.packet);
            if(result==AVERROR_EOF) {Av(avcodec_send_packet(s.decoder,nullptr),"Flush decoder");s.draining=true;break;}
            Av(result,"Read native packet");
            if(s.packet->stream_index!=s.stream) {av_packet_unref(s.packet);continue;}
            result=avcodec_send_packet(s.decoder,s.packet);av_packet_unref(s.packet);Av(result,"Send decoder packet");break;
        }
    }
    auto f=s.decoded;
    if(f->format!=AV_PIX_FMT_D3D11 || f->width!=static_cast<int>(s.width) || f->height!=static_cast<int>(s.height)
       || (f->flags&AV_FRAME_FLAG_INTERLACED) || (f->flags&AV_FRAME_FLAG_CORRUPT))throw Failure(2,"Native input frame format changed or corrupted");
    auto color=[&](int actual,int expected,int unknown){return actual==expected || (s.assume && actual==unknown);};
    if(!color(f->color_range,AVCOL_RANGE_MPEG,AVCOL_RANGE_UNSPECIFIED) || !color(f->colorspace,AVCOL_SPC_BT709,AVCOL_SPC_UNSPECIFIED)
       || !color(f->color_primaries,AVCOL_PRI_BT709,AVCOL_PRI_UNSPECIFIED) || !color(f->color_trc,AVCOL_TRC_BT709,AVCOL_TRC_UNSPECIFIED))throw Failure(2,"Native input frame color changed");
    auto base=s.input->streams[s.stream]->time_base;
    if(f->best_effort_timestamp==AV_NOPTS_VALUE)throw Failure(2,"Missing native frame timestamp");
    long double tick=static_cast<long double>(base.num)/base.den,pts=f->best_effort_timestamp*tick;
    if(!index)s.first=pts;
    if(std::abs(pts-s.first-index*static_cast<long double>(s.rate.den)/s.rate.num)>std::max(tick*1.1L,.0001L))throw Failure(2,"Variable/discontinuous timestamps at frame "+std::to_string(index));
    s.pipeline.UploadTexture(reinterpret_cast<ID3D11Texture2D*>(f->data[0]),static_cast<unsigned>(reinterpret_cast<uintptr_t>(f->data[1])));
    return true;
}
void NativeVideo::Encode(uint64_t index) {
    auto& s=*s_;av_frame_unref(s.encoded);Av(av_hwframe_get_buffer(s.frames,s.encoded,0),"Get NVENC texture");
    s.encoded->pts=index;s.encoded->duration=1;s.encoded->color_range=AVCOL_RANGE_MPEG;
    s.encoded->colorspace=AVCOL_SPC_BT2020_NCL;s.encoded->color_primaries=AVCOL_PRI_BT2020;
    s.encoded->color_trc=AVCOL_TRC_SMPTE2084;s.encoded->chroma_location=AVCHROMA_LOC_CENTER;
    s.pipeline.ProcessTexture(static_cast<unsigned>(index),reinterpret_cast<ID3D11Texture2D*>(s.encoded->data[0]));
    Av(avcodec_send_frame(s.encoder,s.encoded),"Submit NVENC texture");s.Packets();
    auto now=std::chrono::steady_clock::now();
    if(index==0 || now-s.lastReport>=std::chrono::seconds(1)) {
        std::lock_guard<std::mutex> guard(nativeLogMutex);
        std::fprintf(s.log,"Native frames submitted: %llu\n",static_cast<unsigned long long>(index+1));std::fflush(s.log);s.lastReport=now;
    }
}
void NativeVideo::Finish() {
    auto& s=*s_;if(s.finished)throw Failure(6,"Native encoder already finalized");
    Av(avcodec_send_frame(s.encoder,nullptr),"Flush NVENC");s.Packets();Av(av_write_trailer(s.output),"Finalize native video");
    Av(avio_closep(&s.output->pb),"Close native video");s.finished=true;
}
long double NativeVideo::StartSeconds() const {return s_->first;}
