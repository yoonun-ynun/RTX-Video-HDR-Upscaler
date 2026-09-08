#pragma once
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct Failure : std::runtime_error {
    int code;
    Failure(int c, const std::string& message) : std::runtime_error(message), code(c) {}
};
void Check(HRESULT hr, const char* operation);
std::string JsonString(const std::string& value);
std::string Utf8(const wchar_t* value);
void WriteText(const std::filesystem::path& path, const std::string& text);
void WriteBytes(const std::filesystem::path& path, const void* data, size_t bytes);
std::filesystem::path FileSystemPath(const std::filesystem::path& path);
std::filesystem::path CreateRunDirectory(const std::filesystem::path& output);
std::string FileError(const char* operation, const std::filesystem::path& path, DWORD error);

struct AdapterInfo {
    unsigned index;
    DXGI_ADAPTER_DESC1 desc;
    ComPtr<IDXGIAdapter1> adapter;
};
std::vector<AdapterInfo> EnumerateAdapters();
std::vector<uint8_t> GeneratePattern(unsigned width, unsigned height, const std::string& pattern);

class Pipeline {
public:
    Pipeline(const AdapterInfo& adapter, unsigned width, unsigned height, bool pq,
             bool input10 = false, unsigned fpsNum = 30, unsigned fpsDen = 1);
    std::string Report() const;
    bool Supported() const;
    void CreateResources(bool allowUnreportedConversion = false);
    void SetHdr(bool enable);
    void EnableComparison(unsigned sdrWhiteNits=203); // Resolved white stays fixed throughout a job.
    std::string HdrState() const { return hdrState_; }
    void Upload(const std::vector<uint8_t>& bytes);
    std::vector<uint32_t> Process(unsigned frame);
    void ProcessP010(unsigned frame, std::vector<uint16_t>& pixels);
    void ReadP010(std::vector<uint16_t>& pixels);
    void SubmitP010(unsigned frame);
    void CollectP010(std::vector<uint16_t>& pixels);
    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }
    void UploadTexture(ID3D11Texture2D* texture, unsigned slice);
    void ProcessTexture(unsigned frame, ID3D11Texture2D* destination);
    unsigned lastRowPitch = 0;
private:
    void Blit(unsigned frame);
    void WaitGpu(bool issue = true);
    void PackP010();
    void ConfigureProcessor(ID3D11VideoProcessor* processor, bool pq);
    ComPtr<ID3D11VideoProcessor> sdrProcessor_;
    ComPtr<ID3D11Texture2D> sdrOutput_;
    ComPtr<ID3D11VideoProcessorOutputView> sdrOutputView_;
    ComPtr<ID3D11ShaderResourceView> sdrView_;
    ComPtr<ID3D11Buffer> comparisonConstants_;
    bool p010Pending_ = false;
    ComPtr<ID3D11ComputeShader> packShader_;
    ComPtr<ID3D11ComputeShader> texturePackShader_;
    ComPtr<ID3D11ShaderResourceView> rgbView_;
    ComPtr<ID3D11Buffer> packed_, packedStaging_;
    ComPtr<ID3D11UnorderedAccessView> packedView_;
    AdapterInfo adapter_;
    unsigned width_, height_;
    bool pq_;
    bool input10_;
    HRESULT inputHr_ = E_FAIL, outputHr_ = E_FAIL, conversionHr_ = E_FAIL;
    UINT inputFlags_ = 0, outputFlags_ = 0;
    BOOL conversion_ = FALSE;
    bool experimental_ = false;
    std::string hdrState_ = "not_requested";
    HRESULT hdrHr_ = E_PENDING;
    D3D_FEATURE_LEVEL level_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_;
    ComPtr<ID3D11VideoContext1> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessorEnumerator1> enumerator1_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> input_, output_, staging_;
    ComPtr<ID3D11VideoProcessorInputView> inputView_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;
    ComPtr<ID3D11Query> completion_;
};
