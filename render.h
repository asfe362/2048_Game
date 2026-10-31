#ifndef RENDER_H_
#define RENDER_H_

#include <windows.h>

#include <string>

#include "config.h"
#include "game.h"
#include "theme.h"

namespace render {

// 当前鼠标悬停的可点击控件，用于按钮高亮反馈
enum class HoverTarget {
    None,
    NewGame,
    Theme,
    OverlayPrimary,
    OverlaySecondary
};

// 界面布局：窗口尺寸变化后由 compute_layout 重新计算，
// 绘制模块与输入模块共用同一份布局，保证点击热区与画面一致。
struct Layout {
    RECT client = {0, 0, 0, 0};
    RECT titleRect = {0, 0, 0, 0};          // 左上角标题
    RECT scoreRect = {0, 0, 0, 0};          // 当前分数
    RECT timerRect = {0, 0, 0, 0};          // 本局用时
    RECT bestRect = {0, 0, 0, 0};           // 最高分（窗口较窄时隐藏）
    RECT newGameRect = {0, 0, 0, 0};        // “新游戏”按钮
    RECT themeRect = {0, 0, 0, 0};          // 主题切换按钮
    RECT gridRect = {0, 0, 0, 0};           // 网格背景
    RECT hintRect = {0, 0, 0, 0};           // 底部操作提示
    RECT overlayPanel = {0, 0, 0, 0};       // 胜利/失败提示面板
    RECT overlayPrimary = {0, 0, 0, 0};     // 面板主按钮
    RECT overlaySecondary = {0, 0, 0, 0};   // 面板次按钮（仅胜利时使用）
    bool showBest = true;
    double scale = 1.0;  // 整体缩放系数：窗口放大时所有组件同比放大
    int cellSize = 0;
    int gap = 0;
    int radius = 0;
};

Layout compute_layout(const RECT& client);

// 把秒数格式化为 “MM:SS”（超过一小时用 “H:MM:SS”），供标题栏与信息框共用
std::wstring format_elapsed(int totalSeconds);

// GDI 渲染器：内部维护离屏位图（双缓冲），避免绘制时闪烁
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void paint(HWND hwnd, const game::Board& board, const Layout& layout,
               const theme::Theme& theme, bool showWinOverlay, bool showLoseOverlay,
               HoverTarget hover);

private:
    void ensure_buffer(HDC dc, int width, int height);
    void release_buffer();

    HDC m_memDC = nullptr;
    HBITMAP m_memBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_width = 0;
    int m_height = 0;
};

}  // namespace render

#endif  // RENDER_H_
