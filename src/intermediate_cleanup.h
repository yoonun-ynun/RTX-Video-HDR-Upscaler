#pragma once
#include "pipeline.h"
#include <algorithm>
#include <sstream>

namespace intermediate_cleanup {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline bool RunName(const std::wstring& name) {
    if(name.rfind(L"rtxhdr-run-",0)!=0)return false;
    auto suffix=name.substr(11);auto dash=suffix.find(L'-');
    return dash!=std::wstring::npos && dash>0 && dash+1<suffix.size() &&
        suffix.substr(0,dash).find_first_not_of(L"0123456789")==std::wstring::npos &&
        suffix.substr(dash+1).find_first_not_of(L"0123456789")==std::wstring::npos;
}
inline bool ArtifactName(const std::wstring& name) {
    if(name==L"video.mkv"||name==L"completed.mkv"||name==L"completed.mp4"||
       name==L"guard-off.rgb10a2"||name==L"guard-on.rgb10a2")return true;
    auto p=std::filesystem::path(name);auto stem=p.stem().wstring(),ext=p.extension().wstring();
    return (ext==L".rgb10a2"||ext==L".p010") && stem.rfind(L"frame-",0)==0 && stem.size()>6 &&
        stem.substr(6).find_first_not_of(L"0123456789")==std::wstring::npos;
}
inline std::wstring Resolved(HANDLE handle) {
    DWORD length=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED);
    if(!length)throw Failure(6,"Cannot resolve cleanup path");
    std::wstring name(length,L'\0');
    auto written=GetFinalPathNameByHandleW(handle,name.data(),length,FILE_NAME_NORMALIZED);
    if(!written||written>=length)throw Failure(6,"Cannot resolve cleanup path");
    name.resize(written);return name;
}
inline bool Within(const std::wstring& child,const std::wstring& root) {
    auto prefix=root+L"\\";
    return child.size()>prefix.size() && _wcsnicmp(child.c_str(),prefix.c_str(),prefix.size())==0;
}
inline bool SameFile(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
struct Report {
    uint64_t removedFiles=0,removedBytes=0;
    std::vector<std::string> warnings;
    std::string Json() const {
        std::ostringstream s;s<<"{\"status\":\""<<(warnings.empty()?"completed":"partial")<<"\",\"removed_files\":"<<removedFiles
            <<",\"removed_bytes\":"<<removedBytes<<",\"warnings\":[";
        for(size_t i=0;i<warnings.size();++i){if(i)s<<',';s<<JsonString(warnings[i]);}return s.str()+"]}\n";
    }
};

// Never recursively delete directories. Only generated names beneath pinned run
// directories are eligible; reparse points, unknown files and protected IDs stay.
inline Report Clean(const std::filesystem::path& root,const std::filesystem::path& input,const std::filesystem::path& output) {
    Report report;
    try {
        if(!RunName(root.filename().wstring()))throw Failure(6,"Refusing cleanup outside a generated run directory");
        Handle final;final.value=CreateFileW(FileSystemPath(output).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION outputInfo{};
        if(final.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(final.value,&outputInfo)||
           (outputInfo.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
           (!outputInfo.nFileSizeHigh&&!outputInfo.nFileSizeLow))throw Failure(6,"Final output cannot be protected; intermediates retained");
        Handle original;original.value=CreateFileW(FileSystemPath(input).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        BY_HANDLE_FILE_INFORMATION inputInfo{};
        if(original.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(original.value,&inputInfo))throw Failure(6,"Original input cannot be protected; intermediates retained");
        std::wstring boundary;
        auto walk=[&](auto&& self,const std::filesystem::path& directory,unsigned depth)->void {
            if(depth>8){report.warnings.push_back("Unexpected cleanup directory depth");return;}
            Handle parent;parent.value=CreateFileW(FileSystemPath(directory).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            BY_HANDLE_FILE_INFORMATION info{};
            if(parent.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(parent.value,&info)||
               !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
                report.warnings.push_back("Skipped inaccessible or linked directory: "+Utf8(directory.c_str()));return;
            }
            auto resolved=Resolved(parent.value);
            if(!depth)boundary=resolved;
            else if(!Within(resolved,boundary)){report.warnings.push_back("Skipped directory outside cleanup boundary");return;}
            std::error_code error;
            std::filesystem::directory_iterator it(FileSystemPath(directory),error),end;
            if(error){report.warnings.push_back("Cannot enumerate intermediates: "+error.message());return;}
            for(;it!=end;it.increment(error)) {
                if(error)break;
                const auto path=it->path();auto name=path.filename().wstring();
                DWORD attributes=GetFileAttributesW(path.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES)continue;
                if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) {
                    if(RunName(name)||ArtifactName(name))report.warnings.push_back("Skipped linked intermediate: "+Utf8(path.c_str()));
                    continue;
                }
                if(attributes&FILE_ATTRIBUTE_DIRECTORY) {if(RunName(name))self(self,path,depth+1);continue;}
                if(!ArtifactName(name))continue;
                Handle file;file.value=CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                BY_HANDLE_FILE_INFORMATION candidate{};
                if(file.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&candidate)) {
                    report.warnings.push_back(FileError("Cannot remove intermediate",path,GetLastError()));continue;
                }
                if((candidate.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
                   SameFile(candidate,inputInfo)||SameFile(candidate,outputInfo)||!Within(Resolved(file.value),boundary))continue;
                FILE_DISPOSITION_INFO disposition{TRUE};
                if(!SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition))) {
                    report.warnings.push_back(FileError("Cannot remove intermediate",path,GetLastError()));continue;
                }
                ++report.removedFiles;report.removedBytes+=(uint64_t(candidate.nFileSizeHigh)<<32)|candidate.nFileSizeLow;
            }
            if(error)report.warnings.push_back("Cannot enumerate intermediates: "+error.message());
        };
        walk(walk,root,0);
    } catch(const std::exception& e){report.warnings.push_back(e.what());}
    return report;
}
}
