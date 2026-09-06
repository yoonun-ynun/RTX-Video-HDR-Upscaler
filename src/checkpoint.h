#pragma once
#include "pipeline.h"
#include "child_process.h"
#include <bcrypt.h>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <sstream>
#pragma comment(lib,"bcrypt.lib")

// Files are committed only after the encoder trailer, close, and SHA256 succeed.
namespace checkpoint {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline std::string Hash(const std::filesystem::path& path,bool sample=false) {
    std::ifstream in(FileSystemPath(path),std::ios::binary);
    if(!in)throw Failure(6,"Cannot read checkpoint data for hashing");
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw Failure(6,"SHA256 unavailable");
    struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);}} cleanup{alg,hash};
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0)throw Failure(6,"Cannot create SHA256");
    std::vector<unsigned char> buffer(1024*1024);
    auto feed=[&](){in.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=in.gcount();if(n && BCryptHashData(hash,buffer.data(),static_cast<ULONG>(n),0)<0)throw Failure(6,"SHA256 failed");};
    if(sample) {
        const auto size=std::filesystem::file_size(FileSystemPath(path));
        for(auto offset:{uint64_t(0),size/2,size>buffer.size()?size-buffer.size():uint64_t(0)}){in.clear();in.seekg(offset);feed();}
    } else {while(in){feed();}if(!in.eof())throw Failure(6,"Checkpoint read failed");}
    unsigned char digest[32];if(BCryptFinishHash(hash,digest,32,0)<0)throw Failure(6,"SHA256 finish failed");
    std::ostringstream out;for(auto byte:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);return out.str();
}
inline std::string Digest(const std::string& text) {
    BCRYPT_ALG_HANDLE alg=nullptr;unsigned char digest[32]{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw Failure(6,"SHA256 unavailable");
    auto result=BCryptHash(alg,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),static_cast<ULONG>(text.size()),digest,32);
    BCryptCloseAlgorithmProvider(alg,0);if(result<0)throw Failure(6,"Checkpoint digest failed");
    std::ostringstream out;for(auto byte:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);return out.str();
}
inline void Sync(const std::filesystem::path& path) {
    Handle f;f.value=CreateFileW(FileSystemPath(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.value==INVALID_HANDLE_VALUE||!FlushFileBuffers(f.value))throw Failure(6,"Cannot flush saved video");
}
inline void Atomic(const std::filesystem::path& path,const std::string& text) {
    auto temp=path;temp+=L".tmp";
    Handle file;file.value=CreateFileW(FileSystemPath(temp).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)throw Failure(6,"Cannot save checkpoint");
    DWORD written=0;
    if(!WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size()||!FlushFileBuffers(file.value))throw Failure(6,"Cannot flush checkpoint");
    CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
    if(!MoveFileExW(FileSystemPath(temp).c_str(),FileSystemPath(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw Failure(6,"Cannot commit checkpoint");
}
struct Part {std::string path,hash;uint64_t frames=0;};
struct Job {
    std::filesystem::path directory,input,output;
    unsigned adapter=0,maximum=0,cq=18,chunkSeconds=10;
    bool assume=false,videoDone=false,fullVerify=false;
    std::filesystem::path toolDirectory;
    std::string bitrate,identity,gpu,engine,muxPath,muxHash;
    uint64_t size=0;int64_t modified=0;
    long double start=0;
    std::vector<Part> parts;
    uint64_t Count() const {uint64_t n=0;for(auto& p:parts)n+=p.frames;return n;}
    void Save() const {
        std::ostringstream s;s<<std::setprecision(20)<<"RTXHDR_CHECKPOINT_1\n";
        s<<std::quoted(Utf8(input.c_str()))<<' '<<std::quoted(Utf8(output.c_str()))<<'\n';
        s<<adapter<<' '<<maximum<<' '<<cq<<' '<<assume<<' '<<chunkSeconds<<' '<<std::quoted(bitrate)<<' '<<fullVerify<<' '<<std::quoted(Utf8(toolDirectory.c_str()))<<'\n';
        s<<size<<' '<<modified<<' '<<std::quoted(identity)<<' '<<std::quoted(gpu)<<' '<<std::quoted(engine)<<'\n';
        s<<start<<' '<<videoDone<<' '<<std::quoted(muxPath)<<' '<<std::quoted(muxHash)<<' '<<parts.size()<<'\n';
        for(auto& p:parts)s<<std::quoted(p.path)<<' '<<std::quoted(p.hash)<<' '<<p.frames<<'\n';
        Atomic(directory/L"checkpoint.txt",Digest(s.str())+"\n"+s.str());
    }
    std::filesystem::path Local(const std::string& value) const {
        auto p=std::filesystem::path(Wide(value));
        if(p.empty()||p.is_absolute()||p.has_root_name())throw Failure(2,"Invalid checkpoint path");
        for(auto& item:p)if(item==L".."||item==L".")throw Failure(2,"Invalid checkpoint path");
        return directory/p;
    }
    void Load(const std::filesystem::path& path) {
        parts.clear();directory=std::filesystem::absolute(path).parent_path();
        if(std::filesystem::file_size(FileSystemPath(path))>16*1024*1024)throw Failure(2,"Checkpoint too large");
        std::ifstream file(FileSystemPath(path),std::ios::binary);std::string expected;
        std::getline(file,expected);std::string payload((std::istreambuf_iterator<char>(file)),{});
        if(expected!=Digest(payload))throw Failure(2,"Checkpoint manifest is damaged");
        std::istringstream f(payload);std::string version,i,o,tools;size_t count=0;
        std::getline(f,version);if(version!="RTXHDR_CHECKPOINT_1")throw Failure(2,"Unsupported checkpoint version");
        f>>std::quoted(i)>>std::quoted(o)>>adapter>>maximum>>cq>>assume>>chunkSeconds>>std::quoted(bitrate)>>fullVerify>>std::quoted(tools);
        f>>size>>modified>>std::quoted(identity)>>std::quoted(gpu)>>std::quoted(engine);
        f>>start>>videoDone>>std::quoted(muxPath)>>std::quoted(muxHash)>>count;
        if(!f||count>100000||cq>51||adapter>100||chunkSeconds<1||chunkSeconds>600||!std::isfinite(start))throw Failure(2,"Damaged checkpoint");
        input=Wide(i);output=Wide(o);toolDirectory=Wide(tools);
        if(!bitrate.empty()) {
            if(bitrate.find_first_not_of("0123456789")!=std::string::npos)throw Failure(2,"Invalid saved bitrate");
            auto value=std::stoull(bitrate);if(value<100000||value>1000000000)throw Failure(2,"Invalid saved bitrate");
        }
        for(size_t n=0;n<count;++n){Part p;f>>std::quoted(p.path)>>std::quoted(p.hash)>>p.frames;if(!f||!p.frames||p.frames>72000)throw Failure(2,"Damaged checkpoint part");Local(p.path);parts.push_back(p);}
        if(!muxPath.empty())Local(muxPath);
        f>>std::ws;if(!f.eof())throw Failure(2,"Unexpected checkpoint data");
    }
    void Identify() {size=std::filesystem::file_size(FileSystemPath(input));modified=std::filesystem::last_write_time(FileSystemPath(input)).time_since_epoch().count();identity=Hash(input,true);}
    void Validate() const {
        if(size!=std::filesystem::file_size(FileSystemPath(input))||modified!=std::filesystem::last_write_time(FileSystemPath(input)).time_since_epoch().count()||identity!=Hash(input,true))throw Failure(2,"Original file changed; cannot resume this job");
        for(auto& p:parts)if(Hash(Local(p.path))!=p.hash)throw Failure(2,"Saved video segment is missing or damaged; refusing unsafe resume");
        if(!muxPath.empty()) {
            auto mux=Local(muxPath);
            // Rename may have completed immediately before process termination.
            if(!std::filesystem::exists(FileSystemPath(mux)))mux=output;
            if(Hash(mux)!=muxHash)throw Failure(2,"Saved mux output is damaged");
        }
    }
};
}
