#ifndef THEME_H_
#define THEME_H_

#include <windows.h>

namespace theme {

// 主题种类
enum class Mode {
    Light,
    Dark
};

// 方块样式：背景色 + 文字色
struct TileStyle {
    COLORREF background;
    COLORREF foreground;
};

// 一套完整的界面配色。
// 绘制层只认这个结构，不再引用任何全局颜色常量，因此换肤只需替换本结构。
struct Theme {
    const wchar_t* label;                 // 主题名（浅色 / 深色）

    COLORREF windowBgTop;                 // 窗口背景渐变（上）
    COLORREF windowBgBottom;              // 窗口背景渐变（下）

    COLORREF gridBgTop;                   // 网格底板渐变（上）
    COLORREF gridBgBottom;                // 网格底板渐变（下）
    COLORREF emptyCell;                   // 空格

    COLORREF textPrimary;                 // 主文字
    COLORREF textSecondary;               // 次文字
    COLORREF titleText;                   // 标题文字

    COLORREF panelBgTop;                  // 信息卡片渐变（上）
    COLORREF panelBgBottom;               // 信息卡片渐变（下）
    COLORREF panelBorder;                 // 卡片描边

    COLORREF accentTop;                   // 强调色渐变（上）
    COLORREF accentBottom;                // 强调色渐变（下）
    COLORREF accentText;                  // 强调色上的文字

    COLORREF overlayDim;                  // 遮罩覆盖层
    COLORREF overlayPanelTop;             // 遮罩面板渐变（上）
    COLORREF overlayPanelBottom;          // 遮罩面板渐变（下）
    COLORREF overlayLine;                 // 遮罩面板描边

    COLORREF shadow;                      // 投影

    TileStyle tiles[12];                  // 2、4、8 … 4096+
};

// 取指定主题的配色（返回静态实例，调用方不应修改）
const Theme& of(Mode mode);

// 主题名（浅色 / 深色）
const wchar_t* label_of(Mode mode);

// 在两套主题之间做线性插值，用于切换时的过渡动画。
// t 取值 [0, 1]，0 返回 from，1 返回 to。
Theme interpolate(const Theme& from, const Theme& to, double t);

// 把主题选择写入 exe 同目录的 2048.ini；失败返回 false
bool save_mode(Mode mode);

// 从 2048.ini 读取上次的选择；文件不存在或内容无效返回 false
bool load_mode(Mode& out);

// 读取 Windows 的深色模式设置（AppsUseLightTheme）；读取失败按浅色处理
Mode system_mode();

}  // namespace theme

#endif  // THEME_H_
