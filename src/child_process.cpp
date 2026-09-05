#include "child_process.h"
#include <algorithm>
#include <array>

namespace {
struct Handle {
    HANDLE h=nullptr;
    ~Handle() { if (h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    HANDLE release() { HANDLE x=h; h=nullptr; return x; }
};
void WinCheck(bool good, const char* operation) {
    if (!good) throw Failure(6,std::string(operation)+": Win32 "+std::to_string(GetLastError()));
}
std::wstring Quote(const std::wstring& arg) {
    std::wstring s=L"\"";
    size_t slashes=0;
    for (wchar_t c:arg) {
        if (c==L'\\') { ++slashes; continue; }
        if (c==L'"') { s.append(slashes*2+1,L'\\'); s+=c; }
        else { s.append(slashes,L'\\'); s+=c; }
        slashes=0;
    }
    s.append(slashes*2,L'\\'); s+=L'"';
    return s;
}
}
std::wstring Wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if (!n && !s.empty()) throw Failure(2,"Invalid UTF-8 text");
    std::wstring w(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);
    return w;
}
std::filesystem::path FindTool(const wchar_t* name,const std::filesystem::path& directory) {
    if (!directory.empty()) {
        auto p=directory/name;
        if (!std::filesystem::is_regular_file(p)) throw Failure(2,"FFmpeg tool not found in --ffmpeg-dir");
        return std::filesystem::absolute(p);
    }
    std::array<wchar_t,32768> path{};
    GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    auto sibling=std::filesystem::path(path.data()).parent_path()/name;
    if (std::filesystem::is_regular_file(sibling)) return sibling;
    DWORD n=SearchPathW(nullptr,name,nullptr,static_cast<DWORD>(path.size()),path.data(),nullptr);
    if (!n || n>=path.size()) throw Failure(2,"FFmpeg/ffprobe not found beside executable or in PATH");
    return path.data();
}
ChildProcess::ChildProcess(const std::filesystem::path& exe,const std::vector<std::wstring>& args,
                          const std::filesystem::path& log,bool readOutput,bool writeInput) {
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle childInput,childOutput,childError,parentRead,parentWrite,job,process;
    childError.h=CreateFileW(FileSystemPath(log).c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(childError.h==INVALID_HANDLE_VALUE) throw Failure(6,FileError("Cannot create process log",log,GetLastError()));
    if (readOutput) {
        WinCheck(CreatePipe(&parentRead.h,&childOutput.h,&sa,4*1024*1024),"Create output pipe");
        WinCheck(SetHandleInformation(parentRead.h,HANDLE_FLAG_INHERIT,0),"Pipe inheritance");
    } else childOutput.h=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    if (writeInput) {
        WinCheck(CreatePipe(&childInput.h,&parentWrite.h,&sa,4*1024*1024),"Create input pipe");
        WinCheck(SetHandleInformation(parentWrite.h,HANDLE_FLAG_INHERIT,0),"Pipe inheritance");
    } else childInput.h=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    WinCheck(childInput.h!=INVALID_HANDLE_VALUE && childOutput.h!=INVALID_HANDLE_VALUE,"Open NUL");
    STARTUPINFOEXW si{}; si.StartupInfo.cb=sizeof(si);
    si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput=childInput.h; si.StartupInfo.hStdOutput=childOutput.h; si.StartupInfo.hStdError=childError.h;
    SIZE_T bytes=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<uint8_t> storage(bytes);
    si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    WinCheck(InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&bytes),"Initialize process attributes");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes() {DeleteProcThreadAttributeList(p);} } attributes{si.lpAttributeList};
    HANDLE inherited[]{childInput.h,childOutput.h,childError.h};
    WinCheck(UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr),"Child handles");
    job.h=CreateJobObjectW(nullptr,nullptr); WinCheck(job.h!=nullptr,"Create process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    WinCheck(SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Set job limits");
    std::wstring command=Quote(exe.wstring());
    for(const auto& arg:args) command+=L" "+Quote(arg);
    PROCESS_INFORMATION pi{};
    WinCheck(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi),"Create child process");
    process.h=pi.hProcess; Handle thread; thread.h=pi.hThread;
    if (!AssignProcessToJobObject(job.h,pi.hProcess)) {
        TerminateProcess(pi.hProcess,6);
        throw Failure(6,"Cannot assign child process cleanup job");
    }
    WinCheck(ResumeThread(pi.hThread)!=static_cast<DWORD>(-1),"Resume child process");
    process_=process.release(); job_=job.release(); read_=parentRead.release(); write_=parentWrite.release();
}
ChildProcess::~ChildProcess() {
    if(write_) CloseHandle(write_);
    if(read_) CloseHandle(read_);
    if(job_) CloseHandle(job_);
    if(process_) CloseHandle(process_);
}
size_t ChildProcess::Read(void* data,size_t count) {
    DWORD n=0;
    if (!ReadFile(read_,data,static_cast<DWORD>(std::min<size_t>(count,1024*1024)),&n,nullptr)) {
        if (GetLastError()==ERROR_BROKEN_PIPE) return 0;
        WinCheck(false,"Read child output");
    }
    return n;
}
void ChildProcess::Write(const void* data,size_t count) {
    auto ptr=static_cast<const uint8_t*>(data);
    while(count) {
        DWORD n=0;
        WinCheck(WriteFile(write_,ptr,static_cast<DWORD>(std::min<size_t>(count,1024*1024)),&n,nullptr),"Write encoder input (see encode.log)");
        if(!n) throw Failure(6,"Encoder input stalled");
        ptr+=n; count-=n;
    }
}
void ChildProcess::CloseInput() { if(write_) {CloseHandle(write_);write_=nullptr;} }
unsigned ChildProcess::Wait(DWORD timeoutMs) {
    if(WaitForSingleObject(process_,timeoutMs)!=WAIT_OBJECT_0) throw Failure(6,"Child process completion timeout");
    DWORD code=0; WinCheck(GetExitCodeProcess(process_,&code),"Child exit status"); return code;
}
std::string ChildProcess::Capture(size_t maxBytes) {
    std::string result; std::array<char,32768> buf{};
    for(;;) {
        auto n=Read(buf.data(),buf.size()); if(!n) break;
        if(result.size()+n>maxBytes) throw Failure(2,"Probe output exceeds supported size");
        result.append(buf.data(),n);
    }
    if(Wait()!=0) throw Failure(6,"Probe failed; see diagnostic log");
    result.erase(std::remove(result.begin(),result.end(),'\r'),result.end());
    return result;
}
