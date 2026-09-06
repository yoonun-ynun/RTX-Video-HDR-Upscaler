#include "child_process.h"
#include <fstream>
#include <iostream>
#include <iterator>
static std::string Read(const std::filesystem::path& p) {std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static void Require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv) {
    if(argc>1) {
        if(std::wstring(argv[1])==L"live") {CloseHandle(GetStdHandle(STD_INPUT_HANDLE));Sleep(15000);return 0;}
        if(std::wstring(argv[1])==L"log") {std::cerr << "synthetic encoder failure\n" << std::flush;ExitProcess(37);}
        ExitProcess(0xC0000005);
    }
    try {
        auto dir=std::filesystem::current_path()/L"artifacts"/(L"child-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(dir);
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        for(auto mode:{L"crash",L"live",L"log"}) {
            auto log=dir/(std::wstring(mode)+L".log");
            ChildProcess p(exe,{mode,L"argument with spaces and 한글"},log,false,true);
            bool failed=false;
            try {
                if(std::wstring(mode)==L"log") p.Wait();
                else {std::vector<char> bytes(8*1024*1024);p.Write(bytes.data(),bytes.size());}
            } catch(const Failure& e) {failed=std::string(e.what()).find(".failure.json")!=std::string::npos;}
            Require(failed,"Missing failure exception");
            auto json=Read(log.wstring()+L".failure.json");
            Require(json.find("\"stdin_bytes_written\":")!=std::string::npos,"Missing byte count");
            if(std::wstring(mode)==L"live") Require(json.find("\"exit_code\":null")!=std::string::npos && json.find("\"exited\":false")!=std::string::npos,"Live child reported exited");
            if(std::wstring(mode)==L"crash") Require(json.find("0xC0000005")!=std::string::npos,"Crash status lost");
            if(std::wstring(mode)==L"log") Require(Read(log.wstring()+L".tail.txt").find("synthetic encoder failure")!=std::string::npos,"Log tail missing");
            Require(Read(log.wstring()+L".command.json").find("arguments")!=std::string::npos,"Missing command record");
        }
        std::cout << "PASS: crash exit code, live closed pipe, nonzero wait, stderr and command snapshots\n";return 0;
    }catch(const std::exception& e) {std::cerr << e.what();return 1;}
}
