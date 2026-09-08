#pragma once
#include <windows.h>
#include <cmath>
#include <string>
#include <vector>
#pragma comment(lib,"user32.lib")

struct SdrWhite {
    unsigned nits=203;
    bool detected=false;
    std::wstring display;
};
inline bool ValidSdrWhite(unsigned nits) {return nits>=80 && nits<=1000;}
// Read only: never changes the user's HDR/SDR brightness or display configuration.
inline SdrWhite ReadSdrWhite(std::wstring display={}) {
    if(display.empty()) {
        MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor))display=monitor.szDevice;
    }
    SdrWhite result;result.display=display;
    for(unsigned attempt=0;attempt<3;attempt++) {
        UINT32 count=0,modeCount=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&count,&modeCount)!=ERROR_SUCCESS)return result;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        auto status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&count,paths.data(),&modeCount,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;
        if(status!=ERROR_SUCCESS)return result;
        paths.resize(count);
        for(const auto& path:paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || display.empty()
                || _wcsicmp(display.c_str(),source.viewGdiDeviceName)!=0)continue;
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(color),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&color.header)!=ERROR_SUCCESS || !color.advancedColorEnabled)continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&white.header)!=ERROR_SUCCESS)continue;
            // Microsoft defines SDRWhiteLevel as (nits / 80) * 1000.
            auto nits=static_cast<unsigned>(std::lround(white.SDRWhiteLevel*80.0/1000));
            if(ValidSdrWhite(nits)){result.nits=nits;result.detected=true;return result;}
        }
        return result;
    }
    return result;
}
