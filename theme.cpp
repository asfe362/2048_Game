#include "theme.h"

#include <algorithm>
#include <cwchar>
#include <string>

namespace theme {
namespace {

// ---------- 单通道插值 ----------
COLORREF lerp_color(COLORREF a, COLORREF b, double t) {
    if (t <= 0.0) {
        return a;
    }
    if (t >= 1.0) {
        return b;
    }

    const int r = static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5);
    const int g = static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5);
    const int bl = static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5);
    return RGB(r, g, bl);
}

// ---------- 主题实例 ----------
// 浅色：经典 2048 米色系，保持原有的识别度
const Theme kLight = {
    L"浅色",
    RGB(0xFA, 0xF8, 0xF0),  // windowBgTop
    RGB(0xF1, 0xED, 0xE3),  // windowBgBottom
    RGB(0xBB, 0xAD, 0xA0),  // gridBgTop
    RGB(0xB3, 0xA4, 0x97),  // gridBgBottom
    RGB(0xCE, 0xC3, 0xB4),  // emptyCell
    RGB(0x77, 0x6E, 0x65),  // textPrimary
    RGB(0x8A, 0x81, 0x78),  // textSecondary
    RGB(0x6B, 0x62, 0x59),  // titleText
    RGB(0xEE, 0xE4, 0xDA),  // panelBgTop
    RGB(0xE7, 0xDB, 0xCF),  // panelBgBottom
    RGB(0xDC, 0xCF, 0xC0),  // panelBorder
    RGB(0xED, 0xC2, 0x2E),  // accentTop
    RGB(0xE0, 0xB2, 0x14),  // accentBottom
    RGB(0x5A, 0x44, 0x0A),  // accentText
    RGB(0x3A, 0x34, 0x2C),  // overlayDim（配合半透明遮罩使用，需为暗色）
    RGB(0xFC, 0xFA, 0xF4),  // overlayPanelTop
    RGB(0xF3, 0xEF, 0xE6),  // overlayPanelBottom
    RGB(0xDB, 0xD3, 0xC2),  // overlayLine
    RGB(0xB9, 0xAC, 0x99),  // shadow
    {
        // tiles：2、4、8、16、32、64、128、256、512、1024、2048、4096+
        {RGB(0xEE, 0xE4, 0xDA), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xED, 0xE0, 0xC8), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xF2, 0xB1, 0x79), RGB(0xF9, 0xF6, 0xF2)},
        {RGB(0xF5, 0x95, 0x63), RGB(0xF9, 0xF6, 0xF2)},
        {RGB(0xF6, 0x7C, 0x5F), RGB(0xF9, 0xF6, 0xF2)},
        {RGB(0xF6, 0x5E, 0x3B), RGB(0xF9, 0xF6, 0xF2)},
        {RGB(0xED, 0xCF, 0x72), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xED, 0xCC, 0x61), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xED, 0xC8, 0x50), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xED, 0xC5, 0x3F), RGB(0x77, 0x6E, 0x65)},
        {RGB(0xED, 0xC2, 0x2E), RGB(0x77, 0x6E, 0x65)},
        {RGB(0x3C, 0x3A, 0x32), RGB(0xF9, 0xF6, 0xF2)},
    },
};

// 深色：低亮度表面 + 亮文字；2/4 用深底浅字，8 以上用降饱和的橙红金阶
const Theme kDark = {
    L"深色",
    RGB(0x1C, 0x1E, 0x24),  // windowBgTop
    RGB(0x14, 0x16, 0x1B),  // windowBgBottom
    RGB(0x2A, 0x2D, 0x35),  // gridBgTop
    RGB(0x24, 0x27, 0x2E),  // gridBgBottom
    RGB(0x36, 0x3A, 0x44),  // emptyCell
    RGB(0xE8, 0xE6, 0xE3),  // textPrimary
    RGB(0x9A, 0x9A, 0xA2),  // textSecondary
    RGB(0xF2, 0xF0, 0xEC),  // titleText
    RGB(0x2C, 0x2F, 0x37),  // panelBgTop
    RGB(0x26, 0x29, 0x31),  // panelBgBottom
    RGB(0x3B, 0x40, 0x4B),  // panelBorder
    RGB(0xF0, 0xB4, 0x29),  // accentTop
    RGB(0xDD, 0xA0, 0x17),  // accentBottom
    RGB(0x24, 0x1C, 0x05),  // accentText
    RGB(0x05, 0x06, 0x08),  // overlayDim
    RGB(0x2E, 0x31, 0x3A),  // overlayPanelTop
    RGB(0x26, 0x2A, 0x32),  // overlayPanelBottom
    RGB(0x45, 0x4A, 0x57),  // overlayLine
    RGB(0x0E, 0x10, 0x13),  // shadow
    {
        {RGB(0x3A, 0x3D, 0x46), RGB(0xE8, 0xE6, 0xE3)},
        {RGB(0x45, 0x48, 0x52), RGB(0xE8, 0xE6, 0xE3)},
        {RGB(0xB2, 0x74, 0x3C), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xC0, 0x62, 0x33), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xCC, 0x50, 0x2C), RGB(0xFA, 0xF7, 0xF2)},
        {RGB(0xD4, 0x3E, 0x28), RGB(0xFA, 0xF7, 0xF2)},
        {RGB(0xA8, 0x8B, 0x33), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xB8, 0x98, 0x2C), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xC8, 0xA5, 0x26), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xD8, 0xB2, 0x20), RGB(0x1B, 0x14, 0x0C)},
        {RGB(0xE8, 0xC2, 0x2E), RGB(0x24, 0x1C, 0x05)},
        {RGB(0xA8, 0x75, 0x2A), RGB(0xFA, 0xF7, 0xF2)},
    },
};

// ---------- ini 文件路径 ----------
// 取 exe 所在目录下的 2048.ini；取不到路径时返回 false
bool config_path(wchar_t* buffer, DWORD size) {
    wchar_t exePath[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    // 截掉文件名部分
    wchar_t* lastSlash = nullptr;
    for (wchar_t* p = exePath; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            lastSlash = p;
        }
    }
    if (lastSlash == nullptr) {
        return false;
    }

    std::wstring dir(exePath, static_cast<size_t>(lastSlash - exePath) + 1);
    const std::wstring path = dir + L"2048.ini";
    if (path.size() + 1 > size) {
        return false;
    }
    std::copy(path.begin(), path.end(), buffer);
    buffer[path.size()] = L'\0';
    return true;
}

}  // namespace

const Theme& of(Mode mode) {
    return (mode == Mode::Dark) ? kDark : kLight;
}

const wchar_t* label_of(Mode mode) {
    return (mode == Mode::Dark) ? kDark.label : kLight.label;
}

Theme interpolate(const Theme& from, const Theme& to, double t) {
    Theme result = from;
    // 过渡过程中仍显示旧主题名，动画结束后再翻到目标主题名，避免文字突然跳变
    result.label = (t >= 1.0) ? to.label : from.label;

    result.windowBgTop = lerp_color(from.windowBgTop, to.windowBgTop, t);
    result.windowBgBottom = lerp_color(from.windowBgBottom, to.windowBgBottom, t);
    result.gridBgTop = lerp_color(from.gridBgTop, to.gridBgTop, t);
    result.gridBgBottom = lerp_color(from.gridBgBottom, to.gridBgBottom, t);
    result.emptyCell = lerp_color(from.emptyCell, to.emptyCell, t);
    result.textPrimary = lerp_color(from.textPrimary, to.textPrimary, t);
    result.textSecondary = lerp_color(from.textSecondary, to.textSecondary, t);
    result.titleText = lerp_color(from.titleText, to.titleText, t);
    result.panelBgTop = lerp_color(from.panelBgTop, to.panelBgTop, t);
    result.panelBgBottom = lerp_color(from.panelBgBottom, to.panelBgBottom, t);
    result.panelBorder = lerp_color(from.panelBorder, to.panelBorder, t);
    result.accentTop = lerp_color(from.accentTop, to.accentTop, t);
    result.accentBottom = lerp_color(from.accentBottom, to.accentBottom, t);
    result.accentText = lerp_color(from.accentText, to.accentText, t);
    result.overlayDim = lerp_color(from.overlayDim, to.overlayDim, t);
    result.overlayPanelTop = lerp_color(from.overlayPanelTop, to.overlayPanelTop, t);
    result.overlayPanelBottom = lerp_color(from.overlayPanelBottom, to.overlayPanelBottom, t);
    result.overlayLine = lerp_color(from.overlayLine, to.overlayLine, t);
    result.shadow = lerp_color(from.shadow, to.shadow, t);

    for (int i = 0; i < 12; ++i) {
        result.tiles[i].background =
            lerp_color(from.tiles[i].background, to.tiles[i].background, t);
        result.tiles[i].foreground =
            lerp_color(from.tiles[i].foreground, to.tiles[i].foreground, t);
    }
    return result;
}

bool save_mode(Mode mode) {
    wchar_t path[MAX_PATH] = {0};
    if (!config_path(path, MAX_PATH)) {
        return false;
    }
    const wchar_t* value = (mode == Mode::Dark) ? L"Dark" : L"Light";
    return WritePrivateProfileStringW(L"Theme", L"Mode", value, path) != 0;
}

bool load_mode(Mode& out) {
    wchar_t path[MAX_PATH] = {0};
    if (!config_path(path, MAX_PATH)) {
        return false;
    }

    wchar_t value[32] = {0};
    const DWORD len = GetPrivateProfileStringW(L"Theme", L"Mode", L"", value,
                                              static_cast<DWORD>(sizeof(value) / sizeof(value[0])),
                                              path);
    if (len == 0) {
        return false;
    }
    if (wcscmp(value, L"Dark") == 0) {
        out = Mode::Dark;
        return true;
    }
    if (wcscmp(value, L"Light") == 0) {
        out = Mode::Light;
        return true;
    }
    return false;
}

Mode system_mode() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return Mode::Light;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = REG_DWORD;
    const LONG status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_DWORD) {
        return Mode::Light;
    }
    return (value == 0) ? Mode::Dark : Mode::Light;
}

}  // namespace theme
