#include "intermediate_cleanup.h"
#include <winioctl.h>
#include <iostream>

static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool Exists(const std::filesystem::path& p){return std::filesystem::exists(FileSystemPath(p));}
static std::filesystem::path TestRun(const std::filesystem::path& parent) {
    static unsigned sequence=0;auto p=parent/(L"rtxhdr-run-1-"+std::to_wstring(++sequence));
    Require(std::filesystem::create_directories(FileSystemPath(p)),"Create unique test run");return p;
}
static void Junction(const std::filesystem::path& link,const std::filesystem::path& target) {
    struct Buffer {DWORD tag;USHORT bytes,reserved,subOffset,subBytes,printOffset,printBytes;wchar_t paths[2048];} buffer{};
    auto print=std::filesystem::absolute(target).wstring(),sub=L"\\??\\"+print;
    buffer.tag=IO_REPARSE_TAG_MOUNT_POINT;buffer.subBytes=static_cast<USHORT>(sub.size()*2);
    buffer.printOffset=buffer.subBytes+2;buffer.printBytes=static_cast<USHORT>(print.size()*2);
    buffer.bytes=static_cast<USHORT>(8+buffer.printOffset+buffer.printBytes+2);
    std::copy(sub.begin(),sub.end(),buffer.paths);std::copy(print.begin(),print.end(),buffer.paths+sub.size()+1);
    intermediate_cleanup::Handle h;h.value=CreateFileW(FileSystemPath(link).c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    DWORD returned=0;
    Require(h.value!=INVALID_HANDLE_VALUE&&DeviceIoControl(h.value,FSCTL_SET_REPARSE_POINT,&buffer,buffer.bytes+8,nullptr,0,&returned,nullptr),"Create test junction");
}
int main() {
    try {
        auto base=std::filesystem::current_path()/L"artifacts"/(L"cleanup-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(FileSystemPath(base));
        auto input=base/L"input.mp4",output=base/L"output.mkv";WriteText(input,"original");WriteText(output,"verified output");
        auto root=TestRun(base),attempt=TestRun(root),segment=TestRun(attempt);
        for(auto p:{root/L"video.mkv",attempt/L"completed.mp4",segment/L"video.mkv",attempt/L"guard-on.rgb10a2",attempt/L"frame-123.p010"})WriteText(p,"intermediate");
        WriteText(attempt/L"native.log","log");WriteText(root/L"checkpoint.txt","metadata");WriteText(root/L"my-video.mkv","user file");
        auto outside=TestRun(base);WriteText(outside/L"video.mkv","outside data");
        auto junction=TestRun(root);Junction(junction,outside);
        auto linked=TestRun(root)/L"video.mkv";
        Require(CreateHardLinkW(FileSystemPath(linked).c_str(),FileSystemPath(output).c_str(),nullptr),"Create protected output hardlink");
        auto locked=attempt/L"completed.mkv";WriteText(locked,"locked artifact");
        intermediate_cleanup::Handle lock;lock.value=CreateFileW(FileSystemPath(locked).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Require(lock.value!=INVALID_HANDLE_VALUE,"Lock intermediate");
        auto result=intermediate_cleanup::Clean(root,input,output);
        Require(result.removedFiles==5 && !result.warnings.empty(),"Cleanup counts and partial status");
        Require(Exists(input)&&Exists(output)&&Exists(linked)&&Exists(locked),"Protect input, output, hardlink, locked file");
        Require(Exists(outside/L"video.mkv")&&Exists(root/L"my-video.mkv")&&Exists(root/L"checkpoint.txt")&&Exists(attempt/L"native.log"),"Keep outside files, unknown files, metadata and logs");
        CloseHandle(lock.value);lock.value=INVALID_HANDLE_VALUE;
        result=intermediate_cleanup::Clean(root,input,output);
        Require(result.removedFiles==1&&!Exists(locked),"Retry removes only remaining intermediate");
        Require(intermediate_cleanup::Clean(base,input,output).removedFiles==0,"Reject non-run root");
        WriteText(attempt/L"video.mkv","preserve if output absent");
        Require(intermediate_cleanup::Clean(root,input,base/L"missing.mkv").removedFiles==0&&Exists(attempt/L"video.mkv"),"No output means no cleanup");
        // Even a source with a generated intermediate name must remain intact.
        result=intermediate_cleanup::Clean(root,attempt/L"video.mkv",output);
        Require(Exists(attempt/L"video.mkv"),"Protect source within run directory");
        std::cout<<"PASS: generated files only, partial retry, output/input preservation, junction and hardlink safety\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
