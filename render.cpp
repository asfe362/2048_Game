#include "render.h"

#include <algorithm>
#include <cwchar>
#include <string>

namespace render {
namespace {

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

// ------------------------------ 颜色工具 ------------------------------
// 把 overlay 颜色按 alpha 混入 base，用于生成柔和的高亮与投影
COLORREF mix_color(COLORREF base, COLORREF overlay, double alpha) {
    if (alpha <= 0.0) {
        return base;
    }
    if (alpha >= 1.0) {
        return overlay;
    }
    const int r = static_cast<int>(GetRValue(base) +
                                   (GetRValue(overlay) - GetRValue(base)) * alpha + 0.5);
    const int g = static_cast<int>(GetGValue(base) +
                                   (GetGValue(overlay) - GetGValue(base)) * alpha + 0.5);
    const int b = static_cast<int>(GetBValue(base) +
                                   (GetBValue(overlay) - GetBValue(base)) * alpha + 0.5);
    return RGB(r, g, b);
}

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

// 只画圆角边框（不填充），用于卡片描边
void draw_round_rect_border(HDC dc, const RECT& rect, int radius, COLORREF color) {
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) {
        return;
    }
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
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

// ------------------------ msimg32 动态加载封装 ------------------------
// GradientFill / AlphaBlend 位于 msimg32.dll。老系统上可能不存在，
// 这里在运行时动态获取，取不到时退化为纯色填充，功能不受影响。
// （与 main.cpp 中 enable_dpi_awareness() 的做法一致，无需改链接项）
typedef BOOL(WINAPI *GradientFillProc)(HDC, HDC, LPRECT, LPRECT, COLORREF, COLORREF, DWORD);
typedef BOOL(WINAPI *AlphaBlendProc)(HDC, RECT*, HDC, RECT*, BLENDFUNCTION*);

constexpr DWORD kGradientFillRectH = 0x00000001;
constexpr DWORD kGradientFillRectV = 0x00000002;

// 遮罩层不透明度（0-255）
constexpr BYTE kOverlayAlpha = 145;

class MsImg32 {
public:
    MsImg32() {
        m_module = LoadLibraryW(L"msimg32.dll");
        if (m_module == nullptr) {
            return;
        }
        m_gradientFill = reinterpret_cast<GradientFillProc>(GetProcAddress(m_module, "GradientFill"));
        m_alphaBlend = reinterpret_cast<AlphaBlendProc>(GetProcAddress(m_module, "AlphaBlend"));
        if (m_gradientFill == nullptr && m_alphaBlend == nullptr) {
            FreeLibrary(m_module);
            m_module = nullptr;
        }
    }

    ~MsImg32() {
        if (m_module != nullptr) {
            FreeLibrary(m_module);
            m_module = nullptr;
        }
    }

    MsImg32(const MsImg32&) = delete;
    MsImg32& operator=(const MsImg32&) = delete;

    // 渐变填充；返回 false 表示已退化为纯色填充
    bool fill_gradient(HDC dc, const RECT& rect, COLORREF start, COLORREF end,
                       bool vertical) const {
        if (m_gradientFill == nullptr) {
            fill_rect(dc, rect, start);
            return false;
        }
        RECT r = rect;
        const DWORD mode = vertical ? kGradientFillRectV : kGradientFillRectH;
        if (m_gradientFill(dc, dc, &r, nullptr, start, end, mode) == FALSE) {
            fill_rect(dc, rect, start);
            return false;
        }
        return true;
    }

    // 半透明填充；base 用于在不支持 AlphaBlend 时做等效混色
    void fill_translucent(HDC dc, const RECT& rect, COLORREF color, COLORREF base,
                           BYTE alpha) const {
        if (rect.right <= rect.left || rect.bottom <= rect.top || alpha == 0) {
            return;
        }
        if (m_alphaBlend == nullptr) {
            fill_rect(dc, rect, mix_color(base, color, alpha / 255.0));
            return;
        }

        HDC src = CreateCompatibleDC(dc);
        if (src == nullptr) {
            fill_rect(dc, rect, mix_color(base, color, alpha / 255.0));
            return;
        }
        HBITMAP bitmap = CreateBitmap(1, 1, 1, 32, nullptr);
        if (bitmap == nullptr) {
            DeleteDC(src);
            fill_rect(dc, rect, mix_color(base, color, alpha / 255.0));
            return;
        }

        HGDIOBJ oldBitmap = SelectObject(src, bitmap);
        {
            ScopedBrush brush(color);
            RECT pixel = {0, 0, 1, 1};
            FillRect(src, &pixel, brush.get());
        }
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_OVER};
        RECT dst = rect;
        RECT srcRect = {0, 0, 1, 1};
        m_alphaBlend(dc, &dst, src, &srcRect, &blend);

        // 先还原再销毁，避免删除仍被选入 DC 的对象
        SelectObject(src, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(src);
    }

private:
    HMODULE m_module = nullptr;
    GradientFillProc m_gradientFill = nullptr;
    AlphaBlendProc m_alphaBlend = nullptr;
};

MsImg32& msimg32() {
    static MsImg32 instance;
    return instance;
}

// --------------------- 圆角渐变 / 半透明 / 投影 ---------------------
// GDI 的 RoundRect 不支持渐变，用「圆角区域裁剪 + GradientFill」实现。
// 裁剪区的保存/恢复用 SaveDC / RestoreDC：它能正确处理“原先没有裁剪区”的情况。
void fill_round_rect_gradient(HDC dc, const RECT& rect, int radius, COLORREF top,
                              COLORREF bottom, bool vertical) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    if (radius <= 0) {
        msimg32().fill_gradient(dc, rect, top, bottom, vertical);
        return;
    }

    const int savedDc = SaveDC(dc);
    HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1,
                                     radius, radius);
    if (region == nullptr) {
        if (savedDc != 0) {
            RestoreDC(dc, savedDc);
        }
        msimg32().fill_gradient(dc, rect, top, bottom, vertical);
        return;
    }
    SelectClipRgn(dc, region);
    msimg32().fill_gradient(dc, rect, top, bottom, vertical);
    if (savedDc != 0) {
        RestoreDC(dc, savedDc);
    }
    DeleteObject(region);
}

void fill_round_rect_translucent(HDC dc, const RECT& rect, int radius, COLORREF color,
                                 COLORREF base, BYTE alpha) {
    if (rect.right <= rect.left || rect.bottom <= rect.top || alpha == 0) {
        return;
    }
    if (radius <= 0) {
        msimg32().fill_translucent(dc, rect, color, base, alpha);
        return;
    }

    const int savedDc = SaveDC(dc);
    HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1,
                                     radius, radius);
    if (region == nullptr) {
        if (savedDc != 0) {
            RestoreDC(dc, savedDc);
        }
        msimg32().fill_translucent(dc, rect, color, base, alpha);
        return;
    }
    SelectClipRgn(dc, region);
    msimg32().fill_translucent(dc, rect, color, base, alpha);
    if (savedDc != 0) {
        RestoreDC(dc, savedDc);
    }
    DeleteObject(region);
}

// 柔和投影：三层不同偏移的阴影叠加，营造卡片悬浮感
void draw_shadow(HDC dc, const RECT& rect, int radius, COLORREF base, COLORREF shadow) {
    const int offsets[3] = {6, 3, 1};
    const double alphas[3] = {0.06, 0.10, 0.16};
    for (int i = 0; i < 3; ++i) {
        RECT r = rect;
        OffsetRect(&r, 0, offsets[i]);
        const COLORREF color = mix_color(base, shadow, alphas[i]);
        fill_round_rect(dc, r, radius, color, color);
    }
}

// ------------------------- 方块配色（随主题） -------------------------
constexpr int kTileCount = 12;

// 索引 0 对应 2，1 对应 4 …… 10 对应 2048，更大则统一使用末位配色
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
    return index < kTileCount ? index : kTileCount - 1;
}

theme::TileStyle style_for(const theme::Theme& t, int value) {
    return t.tiles[style_index(value)];
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

    // 顶部栏：标题在左，从右向左依次为 新游戏 | 主题 | 分数 | 用时 |（可选）最高分
    const int titleWidth = scale_px(190.0, scale);
    const int titleTextWidth = scale_px(105.0, scale);  // “2048” 四个字符的估算宽度
    const int titleGap = scale_px(16.0, scale);

    const int boxW = scale_px(86.0, scale);
    const int boxH = scale_px(60.0, scale);
    const int timerW = scale_px(104.0, scale);  // 用时框略宽，以容纳 “MM:SS”
    const int buttonW = scale_px(96.0, scale);
    const int buttonH = scale_px(40.0, scale);
    const int themeW = scale_px(46.0, scale);    // 主题按钮为单字方形钮
    const int clusterGap = scale_px(12.0, scale);

    const int right = width - margin;
    const int leftSpace = right - (margin + titleTextWidth + titleGap);

    const int clusterCore = buttonW + clusterGap + themeW + clusterGap + boxW + clusterGap + timerW;
    const int clusterWithBest = clusterCore + clusterGap + boxW;
    layout.showBest = clusterWithBest <= leftSpace;

    const int clusterWidth = layout.showBest ? clusterWithBest : clusterCore;
    const int clusterLeft = right - clusterWidth;
    layout.titleRect = {margin, margin + scale_px(6.0, scale),
                        std::min(margin + titleWidth, clusterLeft - titleGap),
                        margin + scale_px(58.0, scale)};

    layout.newGameRect = {right - buttonW, margin + scale_px(12.0, scale), right,
                           margin + scale_px(12.0, scale) + buttonH};
    int boxRight = layout.newGameRect.left - clusterGap;
    layout.themeRect = {boxRight - themeW, margin + scale_px(12.0, scale), boxRight,
                         margin + scale_px(12.0, scale) + buttonH};
    boxRight = layout.themeRect.left - clusterGap;
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
                   double scale, int valueFontHeight, const theme::Theme& theme) {
    const int radius = scale_px(10.0, scale);
    draw_shadow(dc, rect, radius, theme.windowBgBottom, theme.shadow);
    fill_round_rect_gradient(dc, rect, radius, theme.panelBgTop, theme.panelBgBottom, true);
    draw_round_rect_border(dc, rect, radius, theme.panelBorder);

    const int pad = scale_px(4.0, scale);
    RECT labelRect = {rect.left + pad, rect.top + pad, rect.right - pad,
                      rect.top + scale_px(24.0, scale)};
    RECT valueRect = {rect.left + pad, rect.top + scale_px(22.0, scale), rect.right - pad,
                      rect.bottom - pad};

    {
        ScopedFont font(scale_px(14.0, scale), FW_NORMAL, L"Microsoft YaHei UI");
        draw_text(dc, labelRect, label, font.get(), theme.textSecondary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        ScopedFont font(valueFontHeight, FW_BOLD, L"Segoe UI");
        draw_text(dc, valueRect, value.c_str(), font.get(), theme.textPrimary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_score_box(HDC dc, const RECT& rect, const wchar_t* label, int value, double scale,
                    const theme::Theme& theme) {
    draw_info_box(dc, rect, label, std::to_wstring(value), scale, scale_px(26.0, scale),
                  theme);
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

void draw_top_bar(HDC dc, const Layout& layout, const game::Board& board,
                  const theme::Theme& theme, HoverTarget hover) {
    const double scale = layout.scale;

    {
        ScopedFont font(scale_px(46.0, scale), FW_BOLD, L"Segoe UI");
        draw_text(dc, layout.titleRect, L"2048", font.get(), theme.titleText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 标题下方的强调色装饰条
    {
        const int barH = std::max(2, scale_px(3.0, scale));
        const int barTop = layout.titleRect.bottom + scale_px(3.0, scale);
        RECT bar = {layout.titleRect.left, barTop,
                    std::min(layout.titleRect.left + scale_px(92.0, scale), layout.titleRect.right),
                    barTop + barH};
        if (bar.right > bar.left) {
            fill_round_rect_gradient(dc, bar, barH / 2, theme.accentTop, theme.accentBottom,
                                     false);
        }
    }

    draw_score_box(dc, layout.scoreRect, L"分数", board.score(), scale, theme);
    draw_info_box(dc, layout.timerRect, L"用时", format_elapsed_impl(board.elapsedSeconds()), scale,
                  scale_px(22.0, scale), theme);
    if (layout.showBest) {
        draw_score_box(dc, layout.bestRect, L"最高分", board.bestScore(), scale, theme);
    }

    // 新游戏按钮：强调色渐变 + 投影，hover 时提亮
    {
        const int radius = scale_px(10.0, scale);
        const bool hovered = (hover == HoverTarget::NewGame);
        const COLORREF top = hovered
                                 ? mix_color(theme.accentTop, RGB(255, 255, 255), 0.20)
                                 : theme.accentTop;
        const COLORREF bottom = hovered
                                    ? mix_color(theme.accentBottom, RGB(255, 255, 255), 0.20)
                                    : theme.accentBottom;
        draw_shadow(dc, layout.newGameRect, radius, theme.windowBgBottom, theme.shadow);
        fill_round_rect_gradient(dc, layout.newGameRect, radius, top, bottom, true);
        draw_round_rect_border(dc, layout.newGameRect, radius,
                               mix_color(theme.accentBottom, RGB(0, 0, 0), 0.18));
        ScopedFont font(scale_px(17.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, layout.newGameRect, L"新游戏", font.get(), theme.accentText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 主题切换按钮：显示当前主题名（单字），点击切换到另一套
    {
        const int radius = scale_px(10.0, scale);
        const bool hovered = (hover == HoverTarget::Theme);
        const COLORREF top = hovered
                                 ? mix_color(theme.panelBgTop, RGB(255, 255, 255), 0.16)
                                 : theme.panelBgTop;
        const COLORREF bottom = hovered
                                    ? mix_color(theme.panelBgBottom, RGB(255, 255, 255), 0.16)
                                    : theme.panelBgBottom;
        draw_shadow(dc, layout.themeRect, radius, theme.windowBgBottom, theme.shadow);
        fill_round_rect_gradient(dc, layout.themeRect, radius, top, bottom, true);
        draw_round_rect_border(dc, layout.themeRect, radius, theme.panelBorder);
        ScopedFont font(scale_px(18.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        const wchar_t* label = L"浅";
        if (theme.label != nullptr && wcscmp(theme.label, L"深色") == 0) {
            label = L"深";
        }
        draw_text(dc, layout.themeRect, label, font.get(), theme.textPrimary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_grid(HDC dc, const Layout& layout, const game::Board& board,
               const theme::Theme& theme) {
    draw_shadow(dc, layout.gridRect, layout.radius, theme.windowBgBottom, theme.shadow);
    fill_round_rect_gradient(dc, layout.gridRect, layout.radius, theme.gridBgTop,
                             theme.gridBgBottom, true);

    for (int row = 0; row < game::Board::rows(); ++row) {
        for (int col = 0; col < game::Board::cols(); ++col) {
            const RECT rect = cell_rect(layout, row, col);
            const int value = board.tile(row, col);

            if (value == 0) {
                fill_round_rect(dc, rect, layout.radius, theme.emptyCell, theme.emptyCell);
                continue;
            }

            const theme::TileStyle style = style_for(theme, value);
            // 1px 浅色描边提升质感
            const COLORREF border = mix_color(style.background, RGB(255, 255, 255), 0.25);
            fill_round_rect(dc, rect, layout.radius, style.background, border);

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

void draw_hint(HDC dc, const Layout& layout, const theme::Theme& theme) {
    ScopedFont font(scale_px(15.0, layout.scale), FW_NORMAL, L"Microsoft YaHei UI");
    draw_text(dc, layout.hintRect,
              L"方向键 / W A S D / 拖动鼠标 移动方块    ·    R 键 重新开始    ·    T 键 切换主题",
              font.get(), theme.textSecondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw_overlay_button(HDC dc, const RECT& rect, const wchar_t* text, bool primary,
                         double scale, const theme::Theme& theme, bool hovered) {
    const int radius = scale_px(10.0, scale);
    if (primary) {
        const COLORREF top = hovered
                                 ? mix_color(theme.accentTop, RGB(255, 255, 255), 0.20)
                                 : theme.accentTop;
        const COLORREF bottom = hovered
                                    ? mix_color(theme.accentBottom, RGB(255, 255, 255), 0.20)
                                    : theme.accentBottom;
        draw_shadow(dc, rect, radius, theme.overlayPanelBottom, theme.shadow);
        fill_round_rect_gradient(dc, rect, radius, top, bottom, true);
        ScopedFont font(scale_px(17.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, rect, text, font.get(), theme.accentText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        const COLORREF top = hovered
                                 ? mix_color(theme.panelBgTop, RGB(255, 255, 255), 0.18)
                                 : theme.panelBgTop;
        const COLORREF bottom = hovered
                                    ? mix_color(theme.panelBgBottom, RGB(255, 255, 255), 0.18)
                                    : theme.panelBgBottom;
        fill_round_rect_gradient(dc, rect, radius, top, bottom, true);
        draw_round_rect_border(dc, rect, radius, theme.panelBorder);
        ScopedFont font(scale_px(17.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, rect, text, font.get(), theme.textPrimary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_overlay(HDC dc, const Layout& layout, const game::Board& board, bool win,
                  const theme::Theme& theme, HoverTarget hover) {
    const double scale = layout.scale;

    // 半透明遮罩（AlphaBlend 不可用时退化为等效混色填充）
    fill_round_rect_translucent(dc, layout.gridRect, layout.radius, theme.overlayDim,
                                theme.gridBgBottom, kOverlayAlpha);

    // 提示面板：渐变 + 描边 + 投影
    const int panelRadius = scale_px(16.0, scale);
    draw_shadow(dc, layout.overlayPanel, panelRadius, theme.overlayDim, theme.shadow);
    fill_round_rect_gradient(dc, layout.overlayPanel, panelRadius, theme.overlayPanelTop,
                             theme.overlayPanelBottom, true);
    draw_round_rect_border(dc, layout.overlayPanel, panelRadius, theme.overlayLine);

    const int pad = scale_px(16.0, scale);
    RECT titleRect = {layout.overlayPanel.left + pad, layout.overlayPanel.top + pad,
                      layout.overlayPanel.right - pad, layout.overlayPanel.top + scale_px(62.0, scale)};
    RECT subRect = {layout.overlayPanel.left + pad, layout.overlayPanel.top + scale_px(62.0, scale),
                    layout.overlayPanel.right - pad, layout.overlayPanel.top + scale_px(92.0, scale)};

    {
        ScopedFont font(scale_px(28.0, scale), FW_BOLD, L"Microsoft YaHei UI");
        draw_text(dc, titleRect, win ? L"恭喜，胜利！" : L"游戏结束",
                  font.get(), theme.titleText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const std::wstring text =
            std::wstring(L"本局得分 ") + std::to_wstring(board.score());
        ScopedFont font(scale_px(16.0, scale), FW_NORMAL, L"Microsoft YaHei UI");
        draw_text(dc, subRect, text.c_str(), font.get(), theme.textSecondary,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (win) {
        draw_overlay_button(dc, layout.overlayPrimary, L"继续游戏", true, scale, theme,
                            hover == HoverTarget::OverlayPrimary);
        draw_overlay_button(dc, layout.overlaySecondary, L"重新开始", false, scale, theme,
                            hover == HoverTarget::OverlaySecondary);
    } else {
        draw_overlay_button(dc, layout.overlayPrimary, L"再来一局", true, scale, theme,
                            hover == HoverTarget::OverlayPrimary);
        draw_overlay_button(dc, layout.overlaySecondary, L"重新开始", false, scale, theme,
                            hover == HoverTarget::OverlaySecondary);
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
                     const theme::Theme& theme, bool showWinOverlay, bool showLoseOverlay,
                     HoverTarget hover) {
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

    // 窗口背景：竖向渐变铺满客户区
    msimg32().fill_gradient(m_memDC, client, theme.windowBgTop, theme.windowBgBottom, true);
    draw_top_bar(m_memDC, layout, board, theme, hover);
    draw_grid(m_memDC, layout, board, theme);
    draw_hint(m_memDC, layout, theme);

    if (showWinOverlay) {
        draw_overlay(m_memDC, layout, board, true, theme, hover);
    } else if (showLoseOverlay) {
        draw_overlay(m_memDC, layout, board, false, theme, hover);
    }

    BitBlt(windowDC, 0, 0, width, height, m_memDC, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, windowDC);
}

}  // namespace render
