#include "pipeline.h"
#include <d3dcompiler.h>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>

static const char* P010PackSource=R"(
Texture2D<float4> rgb : register(t0);
RWByteAddressBuffer packed : register(u0);
uint q(float x, float lo, float hi) { return uint(clamp(floor(x+0.5),lo,hi))<<6; }
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w,h; rgb.GetDimensions(w,h);
    uint x=id.x*2,y=id.y*2;
    if(x>=w || y>=h) return;
    float cb=0,cr=0;
    [unroll] for(uint dy=0;dy<2;dy++) {
        uint pair=0;
        [unroll] for(uint dx=0;dx<2;dx++) {
            float3 c=rgb.Load(int3(x+dx,y+dy,0)).rgb;
            float l=dot(c,float3(0.2627,0.6780,0.0593));
            pair |= q(64+876*l,64,940)<<(dx*16);
            cb+=(c.b-l)/(2*(1-0.0593)); cr+=(c.r-l)/(2*(1-0.2627));
        }
        packed.Store(((y+dy)*w+x)*2,pair);
    }
    packed.Store((w*h+(y/2)*w+x)*2,q(512+224*cb,64,960)|(q(512+224*cr,64,960)<<16));
})";

static std::string Hex(HRESULT hr) {
    std::ostringstream s;
    s << "0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(hr);
    return s.str();
}
void Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw Failure(4, std::string(operation) + ": " + Hex(hr));
}
std::string Utf8(const wchar_t* value) {
    int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (!n) throw Failure(4, "UTF-8 conversion failed");
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), n, nullptr, nullptr);
    out.pop_back();
    return out;
}
std::string JsonString(const std::string& value) {
    std::ostringstream s;
    s << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') s << '\\' << c;
        else if (c < 32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else s << c;
    }
    s << '"';
    return s.str();
}
std::filesystem::path FileSystemPath(const std::filesystem::path& path) {
    // Extended-length paths do not depend on the machine's LongPathsEnabled setting.
    auto absolute = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    if (absolute.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (absolute.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + absolute.substr(2);
    return L"\\\\?\\" + absolute;
}
std::string FileError(const char* operation, const std::filesystem::path& path, DWORD error) {
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   error, 0, message, 512, nullptr);
    std::string detail = Utf8(message);
    while (!detail.empty() && (detail.back()=='\r' || detail.back()=='\n')) detail.pop_back();
    return std::string(operation) + ": " + Utf8(path.c_str()) + " (Win32 "
        + std::to_string(error) + ": " + detail + ")";
}
std::filesystem::path CreateRunDirectory(const std::filesystem::path& output) {
    // Keep this independent of the source/output basename: a 200-character video filename
    // used to push even input-info.txt beyond MAX_PATH before conversion could start.
    auto run = output.parent_path() / (L"rtxhdr-run-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directory(FileSystemPath(run)))
        throw Failure(4, "Run directory already exists: " + Utf8(run.c_str()));
    return run;
}
void WriteBytes(const std::filesystem::path& path, const void* data, size_t bytes) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(FileSystemPath(path.parent_path()));
    HANDLE file = CreateFileW(FileSystemPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        throw Failure(error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? 2 : 4,
                      FileError("Cannot create output file", path, error));
    }
    struct CloseFile { HANDLE file; ~CloseFile() { if(file != INVALID_HANDLE_VALUE) CloseHandle(file); } } guard{file};
    auto cursor = static_cast<const uint8_t*>(data);
    while (bytes) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, static_cast<DWORD>(std::min<size_t>(bytes, 1024*1024)), &written, nullptr))
            throw Failure(4, FileError("Cannot write output file", path, GetLastError()));
        if (!written) throw Failure(4, FileError("Zero-byte output write", path, ERROR_WRITE_FAULT));
        cursor += written; bytes -= written;
    }
    BOOL closed = CloseHandle(file);
    guard.file = INVALID_HANDLE_VALUE;
    if (!closed) throw Failure(4, FileError("Cannot close output file", path, GetLastError()));
}
void WriteText(const std::filesystem::path& path, const std::string& text) {
    WriteBytes(path, text.data(), text.size());
}
std::vector<AdapterInfo> EnumerateAdapters() {
    ComPtr<IDXGIFactory1> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    std::vector<AdapterInfo> result;
    for (unsigned i = 0;; ++i) {
        AdapterInfo a{};
        a.index = i;
        HRESULT hr = factory->EnumAdapters1(i, &a.adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        Check(hr, "EnumAdapters1");
        Check(a.adapter->GetDesc1(&a.desc), "GetDesc1");
        result.push_back(a);
    }
    return result;
}
std::vector<uint8_t> GeneratePattern(unsigned w, unsigned h, const std::string& pattern) {
    if (pattern != "gray-ramp" && pattern != "color-bars") throw Failure(2, "Unknown pattern");
    std::vector<uint8_t> data(static_cast<size_t>(w) * h * 3 / 2, 128);
    // BT.709 limited-range approximations: white, yellow, cyan, green, magenta, red, blue, black.
    constexpr uint8_t yv[]{235,219,188,173,78,63,32,16};
    constexpr uint8_t uv[]{128,16,154,42,214,102,240,128};
    constexpr uint8_t vv[]{128,138,16,26,230,240,118,128};
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
            data[static_cast<size_t>(y)*w+x] = pattern == "gray-ramp"
                ? static_cast<uint8_t>(16 + (219ull*x + (w-1)/2)/(w-1)) : yv[8ull*x/w];
    if (pattern == "color-bars") {
        const size_t base = static_cast<size_t>(w)*h;
        for (unsigned y = 0; y < h/2; ++y)
            for (unsigned x = 0; x < w; x += 2) {
                const size_t bar = 8ull*x/w;
                data[base+static_cast<size_t>(y)*w+x] = uv[bar];
                data[base+static_cast<size_t>(y)*w+x+1] = vv[bar];
            }
    }
    return data;
}
Pipeline::Pipeline(const AdapterInfo& a, unsigned w, unsigned h, bool pq, bool input10, unsigned fpsNum, unsigned fpsDen)
    : adapter_(a), width_(w), height_(h), pq_(pq), input10_(input10) {
    D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    Check(D3D11CreateDevice(a.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, 2, D3D11_SDK_VERSION,
        &device_, &level_, &context_), "D3D11CreateDevice");
    Check(device_.As(&video_), "ID3D11VideoDevice");
    Check(context_.As(&videoContext_), "ID3D11VideoContext1");
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate = {fpsNum, fpsDen}; desc.OutputFrameRate = {fpsNum, fpsDen};
    desc.InputWidth = desc.OutputWidth = w;
    desc.InputHeight = desc.OutputHeight = h;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    Check(video_->CreateVideoProcessorEnumerator(&desc, &enumerator_), "CreateVideoProcessorEnumerator");
    Check(enumerator_.As(&enumerator1_), "ID3D11VideoProcessorEnumerator1");
    auto inputFormat = input10_ ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
    inputHr_ = enumerator_->CheckVideoProcessorFormat(inputFormat, &inputFlags_);
    outputHr_ = enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_R10G10B10A2_UNORM, &outputFlags_);
    conversionHr_ = enumerator1_->CheckVideoProcessorFormatConversion(
        inputFormat, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        pq ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
        &conversion_);
}
bool Pipeline::Supported() const {
    return SUCCEEDED(inputHr_) && (inputFlags_ & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)
        && SUCCEEDED(outputHr_) && (outputFlags_ & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)
        && SUCCEEDED(conversionHr_) && conversion_;
}
std::string Pipeline::Report() const {
    std::ostringstream s;
    s << "{\n  \"schema_version\": 1,\n  \"adapter_index\": " << adapter_.index
      << ",\n  \"adapter_name\": " << JsonString(Utf8(adapter_.desc.Description))
      << ",\n  \"adapter_luid_low\": " << adapter_.desc.AdapterLuid.LowPart
      << ",\n  \"adapter_luid_high\": " << adapter_.desc.AdapterLuid.HighPart
      << ",\n  \"feature_level\": " << unsigned(level_)
      << ",\n  \"width\": " << width_ << ",\n  \"height\": " << height_
      << ",\n  \"input_format\": " << JsonString(input10_ ? "P010" : "NV12") << ",\n  \"output_format\": \"R10G10B10A2_UNORM\","
      << "\n  \"input_color\": \"YCBCR_STUDIO_G22_LEFT_P709\","
      << "\n  \"requested_output_color\": " << JsonString(pq_ ? "RGB_FULL_G2084_NONE_P2020" : "RGB_FULL_G22_NONE_P709")
      << ",\n  \"input_format_hresult\": " << JsonString(Hex(inputHr_))
      << ",\n  \"input_format_flags\": " << inputFlags_
      << ",\n  \"output_format_hresult\": " << JsonString(Hex(outputHr_))
      << ",\n  \"output_format_flags\": " << outputFlags_
      << ",\n  \"conversion_hresult\": " << JsonString(Hex(conversionHr_))
      << ",\n  \"conversion_supported\": " << (conversion_ ? "true" : "false")
      << ",\n  \"supported\": " << (Supported() ? "true" : "false")
      << ",\n  \"resources_created\": " << (inputView_ && outputView_ ? "true" : "false")
      << ",\n  \"experimental_conversion\": " << (experimental_ ? "true" : "false")
      << ",\n  \"hdr_extension\": " << JsonString(hdrState_)
      << ",\n  \"hdr_hresult\": " << JsonString(Hex(hdrHr_))
      << ",\n  \"hdr_payload_version\": 4,\n  \"hdr_payload_method\": 3,\n  \"hdr_verified\": false,\n  \"outputs\": [";
    bool first = true;
    for (unsigned i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        if (adapter_.adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND) break;
        if (!output) break;
        ComPtr<IDXGIOutput6> output6;
        DXGI_OUTPUT_DESC1 d{};
        if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&d))) {
            if (!first) s << ',';
            first = false;
            s << "{\"name\":" << JsonString(Utf8(d.DeviceName))
              << ",\"color_space\":" << unsigned(d.ColorSpace)
              << ",\"bits_per_color\":" << d.BitsPerColor
              << ",\"max_luminance\":" << d.MaxLuminance
              << ",\"attached\":" << (d.AttachedToDesktop ? "true" : "false") << '}';
        }
    }
    s << "]\n}\n";
    return s.str();
}
void Pipeline::CreateResources(bool allowUnreportedConversion) {
    if (!Supported() && !allowUnreportedConversion) throw Failure(3, "Requested format/color conversion unsupported; see probe report");
    if (FAILED(inputHr_) || !(inputFlags_ & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)
        || FAILED(outputHr_) || !(outputFlags_ & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        throw Failure(3, "Input/output format unsupported");
    experimental_ = !Supported();
    Check(video_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_), "CreateVideoProcessor");
    videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
    for (unsigned i = 0; i <= D3D11_VIDEO_PROCESSOR_FILTER_STEREO_ADJUSTMENT; ++i)
        videoContext_->VideoProcessorSetStreamFilter(processor_.Get(), 0, static_cast<D3D11_VIDEO_PROCESSOR_FILTER>(i), FALSE, 0);
    videoContext_->VideoProcessorSetStreamColorSpace1(processor_.Get(), 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    videoContext_->VideoProcessorSetOutputColorSpace1(processor_.Get(), pq_ ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    RECT rect{0,0,static_cast<LONG>(width_),static_cast<LONG>(height_)};
    videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &rect);
    videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &rect);
    videoContext_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE, &rect);
    videoContext_->VideoProcessorSetStreamOutputRate(processor_.Get(), 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
    D3D11_TEXTURE2D_DESC td{};
    td.Width=width_; td.Height=height_; td.MipLevels=1; td.ArraySize=1;
    td.Format=input10_ ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags=D3D11_BIND_DECODER;
    Check(device_->CreateTexture2D(&td, nullptr, &input_), "CreateTexture2D input");
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
    iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
    Check(video_->CreateVideoProcessorInputView(input_.Get(), enumerator_.Get(), &iv, &inputView_), "CreateVideoProcessorInputView");
    td.Format=DXGI_FORMAT_R10G10B10A2_UNORM; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Check(device_->CreateTexture2D(&td, nullptr, &output_), "CreateTexture2D output");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};
    ov.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    Check(video_->CreateVideoProcessorOutputView(output_.Get(), enumerator_.Get(), &ov, &outputView_), "CreateVideoProcessorOutputView");
    td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device_->CreateTexture2D(&td, nullptr, &staging_), "CreateTexture2D staging");
    D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
    Check(device_->CreateQuery(&q, &completion_), "CreateQuery");
}
void Pipeline::SetHdr(bool enable) {
    // Independently defined wire layout from the public vendor extension usage.
    // Source and compatibility constraints are recorded in docs/implementation-spec.md.
    constexpr GUID guid{0xfdd62bb4, 0x620b, 0x4fd7, {0x9a,0xb3,0x1e,0x59,0xd0,0xd5,0x44,0xb3}};
    struct Payload { uint32_t version, method, flags; } payload{4,3,enable ? 1u : 0u};
    static_assert(sizeof(Payload)==12);
    hdrHr_ = videoContext_->VideoProcessorSetStreamExtension(processor_.Get(), 0, &guid, sizeof(payload), &payload);
    hdrState_ = enable ? "on_requested" : "off_requested";
    Check(hdrHr_, "NVIDIA HDR extension");
}
void Pipeline::Upload(const std::vector<uint8_t>& bytes) {
    if(p010Pending_) throw Failure(4,"Collect pending P010 before reusing input");
    if (bytes.size() != static_cast<size_t>(width_)*height_*3/2*(input10_ ? 2 : 1)) throw Failure(2,"Input plane size mismatch");
    context_->UpdateSubresource(input_.Get(), 0, nullptr, bytes.data(), width_*(input10_ ? 2 : 1), 0);
}
void Pipeline::Blit(unsigned frame) {
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable=TRUE; stream.InputFrameOrField=frame; stream.pInputSurface=inputView_.Get();
    Check(videoContext_->VideoProcessorBlt(processor_.Get(), outputView_.Get(), frame, 1, &stream), "VideoProcessorBlt");
}
void Pipeline::WaitGpu(bool issue) {
    if(issue) {context_->End(completion_.Get()); context_->Flush();}
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for (;;) {
        BOOL done=FALSE;
        HRESULT hr=context_->GetData(completion_.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        Check(hr,"GetData");
        if (hr==S_OK && done) break;
        if (std::chrono::steady_clock::now()>deadline) throw Failure(4,"GPU completion timeout");
        Check(device_->GetDeviceRemovedReason(),"Device removed");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::vector<uint32_t> Pipeline::Process(unsigned frame) {
    if(p010Pending_) throw Failure(4,"Collect pending P010 before reusing output");
    Blit(frame);
    context_->CopyResource(staging_.Get(), output_.Get());
    WaitGpu();
    std::vector<uint32_t> pixels(static_cast<size_t>(width_)*height_);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context_->Map(staging_.Get(),0,D3D11_MAP_READ,0,&mapped),"Map staging");
    lastRowPitch=mapped.RowPitch;
    if (mapped.RowPitch<width_*4) {
        context_->Unmap(staging_.Get(),0);
        throw Failure(4,"Unexpected output row pitch");
    }
    for (unsigned y=0;y<height_;++y)
        std::memcpy(pixels.data()+static_cast<size_t>(y)*width_,
            static_cast<const uint8_t*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,width_*4);
    context_->Unmap(staging_.Get(),0);
    return pixels;
}

void Pipeline::ProcessP010(unsigned frame, std::vector<uint16_t>& pixels) {
    SubmitP010(frame);
    CollectP010(pixels);
}
void Pipeline::ReadP010(std::vector<uint16_t>& pixels) {
    if(p010Pending_) throw Failure(4,"P010 result already pending");
    PackP010();
    CollectP010(pixels);
}
void Pipeline::SubmitP010(unsigned frame) {
    if(p010Pending_) throw Failure(4,"P010 result already pending");
    Blit(frame);
    PackP010();
}
void Pipeline::PackP010() {
    if (!packShader_) {
        // One thread owns a 2x2 block, so every packed 32-bit store is aligned and exclusive.

        ComPtr<ID3DBlob> code,errors;
        HRESULT hr=D3DCompile(P010PackSource,std::strlen(P010PackSource),"p010",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)) throw Failure(4,errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"P010 shader compile failed");
        Check(device_->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&packShader_),"Create P010 shader");
        Check(device_->CreateShaderResourceView(output_.Get(),nullptr,&rgbView_),"Create RGB shader view");
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=width_*height_*3; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        Check(device_->CreateBuffer(&desc,nullptr,&packed_),"Create P010 buffer");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{}; view.Format=DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; view.Buffer.NumElements=desc.ByteWidth/4; view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        Check(device_->CreateUnorderedAccessView(packed_.Get(),&view,&packedView_),"Create P010 UAV");
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device_->CreateBuffer(&desc,nullptr,&packedStaging_),"Create P010 staging");
    }
    auto srv=rgbView_.Get(); auto uav=packedView_.Get();
    context_->CSSetShader(packShader_.Get(),nullptr,0);
    context_->CSSetShaderResources(0,1,&srv); context_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context_->Dispatch((width_/2+15)/16,(height_/2+15)/16,1);
    srv=nullptr;uav=nullptr;
    context_->CSSetShaderResources(0,1,&srv); context_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context_->CopyResource(packedStaging_.Get(),packed_.Get());
    context_->End(completion_.Get()); context_->Flush();
    p010Pending_=true;
}
void Pipeline::CollectP010(std::vector<uint16_t>& pixels) {
    if(!p010Pending_) throw Failure(4,"No pending P010 result");
    WaitGpu(false);
    pixels.resize(static_cast<size_t>(width_)*height_*3/2);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context_->Map(packedStaging_.Get(),0,D3D11_MAP_READ,0,&mapped),"Map P010 staging");
    std::memcpy(pixels.data(),mapped.pData,pixels.size()*2);
    context_->Unmap(packedStaging_.Get(),0);
    p010Pending_=false;
}

void Pipeline::UploadTexture(ID3D11Texture2D* texture, unsigned slice) {
    if(p010Pending_) throw Failure(4,"Collect pending P010 before reusing input");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    ComPtr<ID3D11Device> owner;texture->GetDevice(&owner);
    if(owner.Get()!=device_.Get() || slice>=desc.ArraySize || desc.MipLevels!=1 || desc.Width<width_ || desc.Height<height_
       || desc.Format!=(input10_?DXGI_FORMAT_P010:DXGI_FORMAT_NV12)) throw Failure(4,"Invalid hardware input texture");
    D3D11_BOX region{0,0,0,width_,height_,1};
    context_->CopySubresourceRegion(input_.Get(),0,0,0,0,texture,slice,&region);
}
void Pipeline::ProcessTexture(unsigned frame, ID3D11Texture2D* destination) {
    if(p010Pending_) throw Failure(4,"Collect pending P010 before reusing output");
    D3D11_TEXTURE2D_DESC desc{};destination->GetDesc(&desc);
    ComPtr<ID3D11Device> owner;destination->GetDevice(&owner);
    if(owner.Get()!=device_.Get() || desc.Width!=width_ || desc.Height!=height_ || desc.Format!=DXGI_FORMAT_P010
       || desc.ArraySize!=1 || desc.MipLevels!=1) throw Failure(4,"Invalid hardware output texture");
    if(!texturePackShader_) {
        std::string source=P010PackSource;
        auto replace=[&](const std::string& a,const std::string& b) {auto at=source.find(a);if(at==std::string::npos) throw Failure(4,"P010 shader source mismatch");source.replace(at,a.size(),b);};
        replace("RWByteAddressBuffer packed : register(u0);","RWTexture2D<unorm float> planeY : register(u0); RWTexture2D<unorm float2> planeUV : register(u1);");
        replace("packed.Store(((y+dy)*w+x)*2,pair);","planeY[uint2(x,y+dy)]=float(pair & 65535)/65535.0; planeY[uint2(x+1,y+dy)]=float(pair >> 16)/65535.0;");
        replace("packed.Store((w*h+(y/2)*w+x)*2,q(512+224*cb,64,960)|(q(512+224*cr,64,960)<<16));","planeUV[uint2(x/2,y/2)]=float2(q(512+224*cb,64,960),q(512+224*cr,64,960))/65535.0;");
        ComPtr<ID3DBlob> code,errors;
        HRESULT hr=D3DCompile(source.data(),source.size(),"p010-texture",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)) throw Failure(4,errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Texture shader compile failed");
        Check(device_->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&texturePackShader_),"Create texture pack shader");
    }
    if(!rgbView_) Check(device_->CreateShaderResourceView(output_.Get(),nullptr,&rgbView_),"Create RGB shader view");
    D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11UnorderedAccessView> y,uv;
    view.Format=DXGI_FORMAT_R16_UNORM;Check(device_->CreateUnorderedAccessView(destination,&view,&y),"Create P010 Y UAV");
    view.Format=DXGI_FORMAT_R16G16_UNORM;Check(device_->CreateUnorderedAccessView(destination,&view,&uv),"Create P010 UV UAV");
    Blit(frame);
    auto srv=rgbView_.Get();ID3D11UnorderedAccessView* uav[]{y.Get(),uv.Get()};
    context_->CSSetShader(texturePackShader_.Get(),nullptr,0);
    context_->CSSetShaderResources(0,1,&srv);context_->CSSetUnorderedAccessViews(0,2,uav,nullptr);
    context_->Dispatch((width_/2+15)/16,(height_/2+15)/16,1);
    srv=nullptr;uav[0]=uav[1]=nullptr;
    context_->CSSetShaderResources(0,1,&srv);context_->CSSetUnorderedAccessViews(0,2,uav,nullptr);
    context_->Flush();
}
