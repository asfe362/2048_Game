// 2048 —— Win32 / GDI 版本
// 程序入口：WinMain -> window::run() 注册窗口并进入消息循环

// 声明 SetProcessDPIAware 等 Vista 之后的 API 所需的版本宏（必须先于 windows.h）
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>

#include "window.h"

namespace {

// 高 DPI 感知：让窗口在高分辨率屏幕上不被系统拉伸，界面更清晰。
// 部分旧版 MinGW 的 user32 导入库没有该符号，因此运行时动态查找。
void enable_dpi_awareness() {
    typedef BOOL(WINAPI *SetProcessDPIAwareProc)(void);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    SetProcessDPIAwareProc func =
        reinterpret_cast<SetProcessDPIAwareProc>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (func != nullptr) {
        func();
    }
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow) {
    enable_dpi_awareness();
    return window::run(hInstance, nCmdShow);
}
