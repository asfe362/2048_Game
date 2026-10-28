#include "render.h"

#include <algorithm>
#include <string>

namespace render {
namespace {

// ----------------------------- 配色方案 -----------------------------
constexpr COLORREF kWindowBg      = RGB(250, 248, 239);
constexpr COLORREF kGridBg        = RGB(187, 173, 160);
constexpr COLORREF kEmptyCell     = RGB(206, 193, 180);
constexpr COLORREF kTextPrimary   = RGB(119, 110, 101);
constexpr COLORREF kTextSecondary = RGB(150, 142, 133);
constexpr COLORREF kPanelBg       = RGB(238, 228, 218);
constexpr COLORREF kAccent        = RGB(237, 194, 46);
constexpr COLORREF kOverlayDim    = RGB(226, 219, 207);
constexpr COLORREF kOverlayPanel  = RGB(252, 250, 244);
constexpr COLORREF kOverlayLine   = RGB(219, 213, 202);

struct TileStyle {
    COLORREF background;
    COLORREF foreground;
};

// 经典 2048 配色：索引 0 对应 2，1 对应 4 …… 10 对应 2048，更大则统一使用末位配色
constexpr TileStyle kPalette[] = {
    { RGB(238, 228, 218), RGB(119, 110, 101) },  // 2
    { RGB(237, 224, 200), RGB(119, 110, 101) },  // 4
    { RGB(242, 177, 121), RGB(249, 246, 242) },  // 8
    { RGB(245, 149,  99), RGB(249, 246, 242) },  // 16
    { RGB(246, 124,  95), RGB(249, 246, 242) },  // 32
    { RGB(246,  94,  59), RGB(249, 246, 242) },  // 64
    { RGB(237, 207, 114), RGB(249, 246, 242) },  // 128
    { RGB(237, 204,  97), RGB(249, 246, 242) },  // 256
    { RGB(237, 200,  80), RGB(249, 246, 242) },  // 512
    { RGB(237, 197,  63), RGB(249, 246, 242) },  // 1024
    { RGB(237, 194,  46), RGB(249, 246, 242) },  // 2048
    { RGB( 60,  58,  50), RGB(249, 246, 242) },  // 4096 及以上
};
constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

int style_index(int value) {
    if (value <= 2) {
        return 0;
    }
    int exponent = 0;
    int v = value;
    while (v > 1) {
        v >>= 1;
        ++exponent;
    }
    const int index = exponent - 1;  // 4 -> 1, 8 -> 2 ...
    return index < kPaletteSize ? index : kPaletteSize - 1;
}

TileStyle style_for(int value) {
    return kPalette[style_index(value)];
}

// ------------------------- GDI 资源的 RAII 封装 -------------------------
// 目的：任何绘制路径（正常返回或提前 return）都不会泄漏 GDI 句柄
class ScopedBrush {
public:
    explicit ScopedBrush(COLORREF color) : m_brush(CreateSolidBrush(color)) {}
    ~ScopedBrush() {
        if (m_brush != nullptr) {
            DeleteObject(m_brush);
        }
    }
    HBRUSH get() const { return m_brush; }

    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

private:
    HBRUSH m_brush;
};

class ScopedFont {
public:
    ScopedFont(int height, int weight, const wchar_t* family) {
        m_font = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    }
    ~ScopedFont() {
        if (m_font != nullptr) {
            DeleteObject(m_font);
        }
    }
    HFONT get() const { return m_font; }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

private:
    HFONT m_font;
};

// --------------------------- 基础绘制辅助函数 ---------------------------
void fill_rect(HDC dc, const RECT& rect, COLORREF color) {
    ScopedBrush brush(color);
    FillRect(dc, &rect, brush.get());
}

void fill_round_rect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border) {
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) {
        return;
    }
    ScopedBrush brush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush.get());
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    // 先还原再销毁，避免删除仍被选入 DC 的对象
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void draw_text(HDC dc, const RECT& rect, const wchar_t* text, HFONT font,
               COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT tmp = rect;
    DrawTextW(dc, text, -1, &tmp, format);
    SelectObject(dc, oldFont);
}

RECT cell_rect(const Layout& layout, int row, int col) {
    const int step = layout.cellSize + layout.gap;
    const int left = layout.gridRect.left + layout.gap + col * step;
    const int top = layout.gridRect.top + layout.gap + row * step;
    RECT rect = {left, top, left + layout.cellSize, top + layout.cellSize};
    return rect;
}

// ------------------------------ 布局计算 ------------------------------
// 所有尺寸都以“基准网格边长 kBaseGridSize”为参照乘以缩放系数，
// 因此窗口放大时标题、分数框、按钮、提示、遮罩面板与网格整体同比放大。
int scale_px(double base, double scale) {
    return static_cast<int>(base * scale + 0.5);
}

double clamp_scale(double value) {
    if (value < config::kMinScale) {
        return config::kMinScale;
    }
    if (value > config::kMaxScale) {
        return config::kMaxScale;
    }
    return value;
}

Layout compute_layout_impl(const RECT& client) {
    Layout layout;
    layout.client = client;

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    // 缩放系数：取客户区宽高相对默认尺寸比例的较小值，保证宽高方向同比放大
    const double rawScale = std::min(static_cast<double>(width) / config::kDefaultClientW,
                                     static_cast<double>(height) / config::kDefaultClientH);
    const double scale = clamp_scale(rawScale);
    layout.scale = scale;

    const int margin = scale_px(18.0, scale);
    const int topHeight = scale_px(78.0, scale);
    const int bottomHeight = scale_px(34.0, scale);

    const int availableW = width - margin * 2;
    const int availableH = height - margin * 2 - topHeight - bottomHeight;

    // 网格目标尺寸随缩放系数放大，同时不能超出可用空间
    const int targetGrid = scale_px(static_cast<double>(config::kBaseGridSize), scale);
    int gridSize = std::min(std::min(availableW, availableH), targetGrid);
    gridSize = std::max(config::kMinGridSize, std::min(gridSize, config::kMaxGridSize));

    int gap = scale_px(static_cast<double>(gridSize) / 30.0, 1.0);
    gap = std::max(6, std::min(gap, 60));

    int cell = (gridSize - gap * (config::kBoardSize + 1)) / config::kBoardSize;
    cell = std::max(config::kMinCellSize, std::min(cell, config::kMaxCellSize));
    gridSize = cell * config::kBoardSize + gap * (config::kBoardSize + 1);

    layout.cellSize = cell;
    layout.gap = gap;
    layout.radius = std::max(4, cell / 14);

    // 网格：水平居中，垂直方向在顶部栏与提示栏之间的剩余空间内居中
    const int gridX = (width - gridSize) / 2;
    const int gridY = margin + topHeight + std::max(0, (availableH - gridSize) / 2);
    layout.gridRect = {gridX, gridY, gridX + gridSize, gridY + gridSize};

    // 顶部栏：标题在左，用时/分数/最高分/新游戏按钮从右向左排列
    const int titleWidth = scale_px(190.0, scale);
    const int titleTextWidth = scale_px(105.0, scale);  // “2048” 四个字符的估算宽度
    const int titleGap = scale_px(16.0, scale);

    const int boxW = scale_px(86.0, scale);
    const int boxH = scale_px(60.0, scale);
    const int timerW = scale_px(104.0, scale);  // 用时框略宽，以容纳 “MM:SS”
    const int buttonW = scale_px(96.0, scale);
    const int buttonH = scale_px(40.0, scale);
    const int clusterGap = scale_px(12.0, scale);

    const int right = width - margin;
    const int leftSpace = right - (margin + titleTextWidth + titleGap);

    // 从右向左依次为：新游戏按钮、分数、用时、（可选）最高分
    const int clusterCore = buttonW + clusterGap + boxW + clusterGap + timerW;
    const int clusterWithBest = clusterCore + clusterGap + boxW;
    layout.showBest = clusterWithBest <= leftSpace;

    const int clusterWidth = layout.showBest ? clusterWithBest : clusterCore;
    int clusterLeft = right - clusterWidth;
    // 标题栏：标题在左，用时/分数/最高分/新游戏按钮从右向左排列
    layout.titleRect = {margin, margin + scale_px(6.0, scale),
                        std::min(margin + titleWidth, clusterLeft - titleGap),
                        margin + scale_px(58.0, scale)};

    layout.newGameRect = {right - buttonW, margin + scale_px(12.0, scale), right,
                           margin + scale_px(12.0, scale) + buttonH};
    int boxRight = layout.newGameRect.left - clusterGap;
    layout.scoreRect = {boxRight - boxW, margin + scale_px(8.0, scale), boxRight,
                         margin + scale_px(8.0, scale) + boxH};
    boxRight = layout.scoreRect.left - clusterGap;
    layout.timerRect = {boxRight - timerW, margin + scale_px(8.0, scale), boxRight,
                         margin + scale_px(8.0, scale) + boxH};
    if (layout.showBest) {
        boxRight = layout.timerRect.left - clusterGap;
        layout.bestRect = {boxRight - boxW, margin + scale_px(8.0, scale), boxRight,
                           margin + scale_px(8.0, scale) + boxH};
    }

    // 底部提示
    layout.hintRect = {margin, height - margin - bottomHeight, width - margin, height - margin};

    // 遮罩面板及其按钮（尺寸随缩放系数放大）
    const int panelW = std::max(scale_px(240.0, scale), static_cast<int>(gridSize * 0.80));
    const int panelH = scale_px(168.0, scale);
    const int panelX = gridX + (gridSize - panelW) / 2;
    const int panelY = gridY + (gridSize - panelH) / 2;
    layout.overlayPanel = {panelX, panelY, panelX + panelW, panelY + panelH};

    const int innerPad = scale_px(24.0, scale);
    const int buttonGap = scale_px(14.0, scale);
    const int overlayButtonH = scale_px(40.0, scale);
    const int overlayButtonW = (panelW - innerPad * 2 - buttonGap) / 2;
    const int buttonY = panelY + scale_px(104.0, scale);

    layout.overlayPrimary = {panelX + innerPad, buttonY,
                              panelX + innerPad + overlayButtonW, buttonY + overlayButtonH};
    layout.overlaySecondary = {panelX + panelW - innerPad - overlayButtonW, buttonY,
                                panelX + panelW - innerPad, buttonY + overlayButtonH};
    return layout;
}

// ------------------------------ 具体绘制 ------------------------------
// 通用信息框：上方一行标签，下方一行数值
void draw_info_box(HDC dc, const RECT& rect, const wchar_t* label, const std::wstring& value,
                   double scale, int valueFontHeight) {
    fill_round_rect(dc, rect, scale_px(10.0, scale), kPanelBg, kPanelBg);

    const int pad = scale_px(4.0, scale);
    RECT labelRect = {rect.left + pad, rect.top + pad, rect.right - pad,
                      rect.top + scale_px(24.0, scale)};
    RECT valueRect = {rect.left + pad, rect.top + scale_px(22.0, scale), rect.right - pad,
                      rect.bottom - pad};

    {
        ScopedFont font(scale_px(14.0, scale), FW_NORMAL, L"Microsoft YaHei UI");
        draw_text(dc, labelRect, label, font.get(), kTextSecondary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        ScopedFont font(valueFontHeight, FW_BOLD, L"Segoe UI");
        draw_text(dc, valueRect, value.c_str(), font.get(), kTextPrimary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_score_box(HDC dc, const RECT& rect, const wchar_t* label, int value, double scale) {
    draw_info_box(dc, rect, label, std::to_wstring(value), scale, scale_px(26.0, scale));
}

// 把秒数格式化为 “MM:SS”（超过一小时用 “H:MM:SS”）
std::wstring format_elapsed_impl(int totalSeconds) {
    if (totalSeconds < 0) {
        totalSeconds = 0;
    }
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    std::wstring text;
    if (hours > 0) {
        text = std::to_wstring(hours) + L":";
    }
    if (minutes < 10) {
        text += L"0";
    }
    text += std::to_wstring(minutes);
    text += L":";
    if (seconds < 10) {
        text += L"0";
    }
    text += std::to_wstring(seconds);
    return text;
}

void draw_top_bar(HDC dc, const Layout& layout, const game::Board& board) {
    const double scale = layout.scale;

    {
        ScopedFont font(scale_px(46.0, scale), FW_BOLD, L"Segoe UI");
        draw_text(dc, layout.titleRect, L"2048", font.get(), kTextPrimary,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    draw_score_box(dc, layout.scoreRect, L"分数", board.score(), scale);
    draw_info_box(dc, layout.timerRect, L"用时", format_elapsed_impl(board.elapsedSeconds()), scale,
                  scale_px(22.0, scale));
    if (layout.showBest) {
        draw_score_box(dc, layout.bestRect, L"最高分", board.bestScore(), scale);
    }

    fill_round_rect(dc, layout.newGameRect, scale_px(10.0, scale), kPanelBg, kPanelBg);
    {
        ScopedFont font(scale_px(17.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, layout.newGameRect, L"新游戏", font.get(), kTextPrimary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_grid(HDC dc, const Layout& layout, const game::Board& board) {
    fill_round_rect(dc, layout.gridRect, layout.radius, kGridBg, kGridBg);

    for (int row = 0; row < game::Board::rows(); ++row) {
        for (int col = 0; col < game::Board::cols(); ++col) {
            const RECT rect = cell_rect(layout, row, col);
            const int value = board.tile(row, col);

            if (value == 0) {
                fill_round_rect(dc, rect, layout.radius, kEmptyCell, kEmptyCell);
                continue;
            }

            const TileStyle style = style_for(value);
            fill_round_rect(dc, rect, layout.radius, style.background, style.background);

            // 字号随位数自适应，保证 1024 / 2048 也不会溢出格子
            int fontHeight = static_cast<int>(layout.cellSize * 0.46);
            if (value >= 1000) {
                fontHeight = static_cast<int>(layout.cellSize * 0.30);
            } else if (value >= 100) {
                fontHeight = static_cast<int>(layout.cellSize * 0.36);
            }

            ScopedFont font(fontHeight, FW_BOLD, L"Segoe UI");
            const std::wstring text = std::to_wstring(value);
            draw_text(dc, rect, text.c_str(), font.get(), style.foreground,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

void draw_hint(HDC dc, const Layout& layout) {
    ScopedFont font(scale_px(15.0, layout.scale), FW_NORMAL, L"Microsoft YaHei UI");
    draw_text(dc, layout.hintRect,
              L"方向键 / W A S D / 拖动鼠标 移动方块    ·    R 键 重新开始",
              font.get(), kTextSecondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw_overlay_button(HDC dc, const RECT& rect, const wchar_t* text, bool primary,
                         double scale) {
    fill_round_rect(dc, rect, scale_px(10.0, scale), primary ? kAccent : kPanelBg,
                    primary ? kAccent : kPanelBg);
    ScopedFont font(scale_px(17.0, scale), FW_BOLD, L"Microsoft YaHei UI");
    draw_text(dc, rect, text, font.get(), kTextPrimary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw_overlay(HDC dc, const Layout& layout, const game::Board& board, bool win) {
    const double scale = layout.scale;

    // 用不透明底色模拟遮罩（不依赖 msimg32 等额外库）
    fill_round_rect(dc, layout.gridRect, layout.radius, kOverlayDim, kOverlayDim);
    fill_round_rect(dc, layout.overlayPanel, scale_px(16.0, scale), kOverlayPanel, kOverlayLine);

    const int pad = scale_px(16.0, scale);
    RECT titleRect = {layout.overlayPanel.left + pad, layout.overlayPanel.top + pad,
                      layout.overlayPanel.right - pad, layout.overlayPanel.top + scale_px(62.0, scale)};
    RECT subRect = {layout.overlayPanel.left + pad, layout.overlayPanel.top + scale_px(62.0, scale),
                    layout.overlayPanel.right - pad, layout.overlayPanel.top + scale_px(92.0, scale)};

    {
        ScopedFont font(scale_px(28.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, titleRect, win ? L"恭喜，胜利！" : L"游戏结束",
                  font.get(), kTextPrimary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const std::wstring text =
            std::wstring(L"本局得分 ") + std::to_wstring(board.score());
        ScopedFont font(scale_px(16.0, scale), FW_NORMAL, L"Microsoft YaHei UI");
        draw_text(dc, subRect, text.c_str(), font.get(), kTextSecondary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (win) {
        draw_overlay_button(dc, layout.overlayPrimary, L"继续游戏", true, scale);
        draw_overlay_button(dc, layout.overlaySecondary, L"重新开始", false, scale);
    } else {
        draw_overlay_button(dc, layout.overlayPrimary, L"再来一局", true, scale);
        draw_overlay_button(dc, layout.overlaySecondary, L"重新开始", false, scale);
    }
}

}  // namespace

Layout compute_layout(const RECT& client) {
    return compute_layout_impl(client);
}

std::wstring format_elapsed(int totalSeconds) {
    return format_elapsed_impl(totalSeconds);
}

// ------------------------------ Renderer ------------------------------
Renderer::Renderer() = default;

Renderer::~Renderer() {
    release_buffer();
}

void Renderer::ensure_buffer(HDC dc, int width, int height) {
    if (m_memDC != nullptr && width == m_width && height == m_height) {
        return;
    }
    release_buffer();

    m_memDC = CreateCompatibleDC(dc);
    if (m_memDC == nullptr) {
        return;
    }
    m_memBitmap = CreateCompatibleBitmap(dc, width, height);
    if (m_memBitmap == nullptr) {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
        return;
    }
    // 选入位图后必须保存原对象，释放时先还原再删除
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_memBitmap));
    m_width = width;
    m_height = height;
}

void Renderer::release_buffer() {
    if (m_memDC != nullptr) {
        if (m_oldBitmap != nullptr) {
            SelectObject(m_memDC, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_memBitmap != nullptr) {
            DeleteObject(m_memBitmap);
            m_memBitmap = nullptr;
        }
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

void Renderer::paint(HWND hwnd, const game::Board& board, const Layout& layout,
                     bool showWinOverlay, bool showLoseOverlay) {
    HDC windowDC = GetDC(hwnd);
    if (windowDC == nullptr) {
        return;
    }

    RECT client;
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ensure_buffer(windowDC, width, height);
    if (m_memDC == nullptr) {
        ReleaseDC(hwnd, windowDC);
        return;
    }

    fill_rect(m_memDC, client, kWindowBg);
    draw_top_bar(m_memDC, layout, board);
    draw_grid(m_memDC, layout, board);
    draw_hint(m_memDC, layout);

    if (showWinOverlay) {
        draw_overlay(m_memDC, layout, board, true);
    } else if (showLoseOverlay) {
        draw_overlay(m_memDC, layout, board, false);
    }

    BitBlt(windowDC, 0, 0, width, height, m_memDC, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, windowDC);
}

}  // namespace render
