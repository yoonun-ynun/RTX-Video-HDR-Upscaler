#pragma once
#include "pipeline.h"

// Child processes receive only explicitly listed pipe/log handles. No shell is involved.
class ChildProcess {
public:
    ChildProcess(const std::filesystem::path& executable, const std::vector<std::wstring>& args,
                 const std::filesystem::path& log, bool readOutput, bool writeInput);
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    size_t Read(void* bytes, size_t count);
    void Write(const void* bytes, size_t count);
    void CloseInput();
    unsigned Wait(DWORD timeoutMs = 60000);
    std::string Capture(size_t maxBytes = 64*1024*1024);
private:
    HANDLE process_ = nullptr, job_ = nullptr, read_ = nullptr, write_ = nullptr;
};
std::filesystem::path FindTool(const wchar_t* name, const std::filesystem::path& explicitDirectory);
std::wstring Wide(const std::string& s);
