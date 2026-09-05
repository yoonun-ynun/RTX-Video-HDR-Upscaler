#include "pipeline.h"
#include <array>
#include <iostream>

static void Require(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
static std::string Read(const std::filesystem::path& path) {
    HANDLE f=CreateFileW(FileSystemPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Require(f!=INVALID_HANDLE_VALUE,"Cannot read test output");
    std::array<char,256> data{};DWORD n=0;
    BOOL ok=ReadFile(f,data.data(),static_cast<DWORD>(data.size()),&n,nullptr);
    CloseHandle(f);Require(ok,"Read failed");return {data.data(),n};
}
int main() {
    try {
        auto root=std::filesystem::current_path()/L"artifacts"/(L"file-path-tests-"+std::to_wstring(GetCurrentProcessId()));
        const std::string payload="UTF-8 path test: exact contents\n";
        // Reproduce the reported threshold (261), then test well beyond MAX_PATH.
        for(size_t length:{size_t(261),size_t(340)}) {
            auto parent=root/L"한글 [spaces]"/std::wstring(90,L'a');
            size_t remaining=length-parent.wstring().size()-1-std::wstring(L"/input-info.txt").size();
            auto file=parent/std::wstring(remaining,L'b')/L"input-info.txt";
            Require(file.wstring().size()==length,"Incorrect regression path length");
            WriteText(file,payload);
            Require(Read(file)==payload,"Long-path contents differ");
            bool refused=false;
            try {WriteText(file,"replacement");} catch(const Failure& e) {
                refused=e.code==2 && std::string(e.what()).find("Win32")!=std::string::npos;
            }
            Require(refused,"Overwrite was not rejected with a useful error");
            Require(Read(file)==payload,"Original was overwritten");
        }
        auto run=CreateRunDirectory(root/(std::wstring(210,L'v')+L".hdr.mkv"));
        Require(run.filename().wstring().size()<50,"Run folder still depends on long video name");
        WriteText(run/L"input-info.txt",payload);
        Require(Read(run/L"input-info.txt")==payload,"Run report write failed");
        auto directory=root/L"directory-as-file";
        std::filesystem::create_directories(FileSystemPath(directory));
        bool useful=false;
        try {WriteText(directory,"data");} catch(const Failure& e) {
            auto message=std::string(e.what());
            useful=message.find("directory-as-file")!=std::string::npos && message.find("Win32")!=std::string::npos;
        }
        Require(useful,"File errors must identify both path and OS error");
        std::cout << "PASS: 261/340-character Unicode paths, short run folder, overwrite protection, useful errors\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
