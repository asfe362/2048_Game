// 声明 Vista 之后 API 所需的版本宏（必须先于 windows.h）
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "window.h"

#include <windowsx.h>

#include <algorithm>
#include <new>
#include <string>

#include "config.h"
#include "game.h"
#include "input.h"
#include "render.h"
#include "theme.h"

namespace window {
namespace {

const wchar_t kClassName[] = L"Win32Gdi2048Window";
const wchar_t kAppTitle[] = L"2048 - Win32 / GDI 版本";

// 标准资源 ID（数值与 winuser.h 中的 IDI_APPLICATION / IDC_ARROW / IDC_HAND 一致）。
// 这里使用字面量，避免编译时未定义 UNICODE 宏导致 A/W 资源宏类型不匹配。
constexpr int kIconApplication = 32512;  // IDI_APPLICATION
constexpr int kCursorArrow = 32512;       // IDC_ARROW
constexpr int kCursorHand = 32649;        // IDC_HAND

// 计时器 ID：每秒刷新一次用时显示
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerIntervalMs = 1000;

// 主题过渡动画计时器：约 60fps 步进
constexpr UINT_PTR kAnimTimerId = 2;
constexpr UINT kAnimFrameMs = 16;
constexpr DWORD kTransitionDurationMs = 280;

// 右键菜单命令 ID
constexpr UINT kCmdLightTheme = 1001;
constexpr UINT kCmdDarkTheme = 1002;
constexpr UINT kCmdNewGame = 1003;

// 窗口的完整状态：游戏数据 + 渲染器 + 布局 + 输入状态
struct AppState {
    game::Board board;
    render::Renderer renderer;
    render::Layout layout;
    input::DragState drag;
    bool winOverlay = false;
    bool loseOverlay = false;
    UINT_PTR timerId = 0;      // 已安装的计时器句柄
    int lastTickSecond = -1;   // 上一次刷新时显示的秒数，用于判断是否需要重绘

    // 主题：currentTheme 是当前生效的主题（过渡期间为插值结果）
    theme::Mode mode = theme::Mode::Light;
    theme::Theme currentTheme = theme::of(theme::Mode::Light);
    theme::Theme themeFrom = theme::of(theme::Mode::Light);
    theme::Theme themeTo = theme::of(theme::Mode::Light);
    bool transitioning = false;
    DWORD animStartTick = 0;   // 过渡动画起始时刻（GetTickCount）
    UINT_PTR animTimerId = 0;  // 过渡动画计时器句柄
    render::HoverTarget hover = render::HoverTarget::None;
};

// ==== 临时诊断 ====
void log_layout(const char* tag, int w, int h) {
    static HANDLE file = INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        file = CreateFileW(L"C:\\Users\\95345\\AppData\\Local\\Temp\\2048_layout.log",
                           GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    char line[160];
    const int n = wsprintfA(line, "%s w=%d h=%d\r\n", tag, w, h);
    DWORD written = 0;
    WriteFile(file, line, n, &written, nullptr);
}

AppState* state_of(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

bool overlay_active(const AppState& state) {
    return state.winOverlay || state.loseOverlay;
}

void refresh_title(HWND hwnd, const game::Board& board) {
    const std::wstring title = std::wstring(kAppTitle) + L"  |  分数: " +
                               std::to_wstring(board.score()) + L"  |  用时: " +
                               render::format_elapsed(board.elapsedSeconds());
    SetWindowTextW(hwnd, title.c_str());
}

// 执行一次移动并刷新界面
void apply_move(HWND hwnd, AppState& state, game::Direction dir) {
    if (overlay_active(state)) {
        return;  // 遮罩显示期间不接受移动操作
    }
    if (!state.board.move(dir)) {
        return;  // 棋盘没有变化，不重新生成方块
    }

    state.board.spawnTile();
    state.board.updateBestScore();

    if (state.board.isWinPending()) {
        state.winOverlay = true;
        state.loseOverlay = false;
    } else if (state.board.isGameOver()) {
        state.winOverlay = false;
        state.loseOverlay = true;
    }

    // 无路可走时停止计时（无论胜利还是失败）
    if (state.board.isGameOver()) {
        state.board.stopTimer();
    }

    refresh_title(hwnd, state.board);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 开始新的一局
void restart_game(HWND hwnd, AppState& state) {
    state.board.reset();  // reset() 会同时把计时归零并重新开始
    state.winOverlay = false;
    state.loseOverlay = false;
    state.drag = input::DragState();
    state.lastTickSecond = -1;

    refresh_title(hwnd, state.board);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 遮罩上的主按钮：胜利时继续游戏，失败时再来一局
void handle_primary_action(HWND hwnd, AppState& state) {
    if (state.winOverlay) {
        state.board.acknowledgeWin();
        state.winOverlay = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        restart_game(hwnd, state);
    }
}

// 指针当前落在哪个可点击控件上
render::HoverTarget hover_at(const AppState& state, POINT point) {
    if (overlay_active(state)) {
        if (PtInRect(&state.layout.overlayPrimary, point)) {
            return render::HoverTarget::OverlayPrimary;
        }
        if (PtInRect(&state.layout.overlaySecondary, point)) {
            return render::HoverTarget::OverlaySecondary;
        }
        return render::HoverTarget::None;
    }
    if (PtInRect(&state.layout.newGameRect, point)) {
        return render::HoverTarget::NewGame;
    }
    if (PtInRect(&state.layout.themeRect, point)) {
        return render::HoverTarget::Theme;
    }
    return render::HoverTarget::None;
}

// 切换到指定主题：启动颜色插值过渡动画，并记住用户选择
void switch_theme(HWND hwnd, AppState& state, theme::Mode mode) {
    if (mode == state.mode && !state.transitioning) {
        return;  // 已经是目标主题，无需动画
    }

    state.themeFrom = state.currentTheme;  // 过渡中再次切换时以当前插值结果为起点，避免跳变
    state.themeTo = theme::of(mode);
    state.transitioning = true;
    state.animStartTick = GetTickCount();
    state.mode = mode;
    theme::save_mode(mode);

    if (state.animTimerId == 0) {
        state.animTimerId = SetTimer(hwnd, kAnimTimerId, kAnimFrameMs, nullptr);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 在浅色与深色之间来回切换
void toggle_theme(HWND hwnd, AppState& state) {
    switch_theme(hwnd, state,
                 state.mode == theme::Mode::Light ? theme::Mode::Dark : theme::Mode::Light);
}

// 推进主题过渡动画：按真实经过时间计算进度，避免计时器抖动导致掉帧
void update_theme_animation(HWND hwnd, AppState& state) {
    if (!state.transitioning) {
        return;
    }

    const DWORD now = GetTickCount();
    double t = static_cast<double>(now - state.animStartTick) /
               static_cast<double>(kTransitionDurationMs);
    if (t >= 1.0) {
        t = 1.0;
        state.transitioning = false;
        state.currentTheme = state.themeTo;
        if (state.animTimerId != 0) {
            KillTimer(hwnd, kAnimTimerId);
            state.animTimerId = 0;
        }
    } else {
        state.currentTheme = theme::interpolate(state.themeFrom, state.themeTo, t);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 旧版 MinGW 的头文件与导入库只提供 ANSI 版 TrackPopupMenu，
// 这里在运行时取 Unicode 版本，确保中文菜单项正确显示。
// （与 main.cpp 中 enable_dpi_awareness() 的做法一致）
typedef INT(WINAPI *TrackPopupMenuWProc)(HMENU, UINT, int, int, int, HWND, const RECT*);

UINT track_popup_menu(HMENU menu, HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return 0;
    }
    TrackPopupMenuWProc track =
        reinterpret_cast<TrackPopupMenuWProc>(GetProcAddress(user32, "TrackPopupMenuW"));
    if (track == nullptr) {
        return 0;
    }

    POINT cursor = {0, 0};
    GetCursorPos(&cursor);
    // 弹出菜单前必须把窗口置于前台，否则菜单关闭后窗口不会重新获得焦点
    SetForegroundWindow(hwnd);
    return static_cast<UINT>(
        track(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, nullptr));
}

// 右键菜单：切换主题 / 重新开始
void show_context_menu(HWND hwnd, AppState& state) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu,
                MF_STRING | ((state.mode == theme::Mode::Light) ? MF_CHECKED : MF_UNCHECKED),
                kCmdLightTheme, L"浅色主题");
    AppendMenuW(menu,
                MF_STRING | ((state.mode == theme::Mode::Dark) ? MF_CHECKED : MF_UNCHECKED),
                kCmdDarkTheme, L"深色主题");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdNewGame, L"新游戏");

    const UINT command = track_popup_menu(menu, hwnd);
    DestroyMenu(menu);

    if (command == kCmdLightTheme) {
        switch_theme(hwnd, state, theme::Mode::Light);
    } else if (command == kCmdDarkTheme) {
        switch_theme(hwnd, state, theme::Mode::Dark);
    } else if (command == kCmdNewGame) {
        restart_game(hwnd, state);
    }
}

void handle_key(HWND hwnd, AppState& state, WPARAM key) {
    switch (input::action_from_key(key)) {
        case input::Action::MoveUp:
            apply_move(hwnd, state, game::Direction::Up);
            break;
        case input::Action::MoveDown:
            apply_move(hwnd, state, game::Direction::Down);
            break;
        case input::Action::MoveLeft:
            apply_move(hwnd, state, game::Direction::Left);
            break;
        case input::Action::MoveRight:
            apply_move(hwnd, state, game::Direction::Right);
            break;
        case input::Action::Restart:
            restart_game(hwnd, state);
            break;
        case input::Action::Primary:
            if (overlay_active(state)) {
                handle_primary_action(hwnd, state);
            }
            break;
        case input::Action::Secondary:
            if (overlay_active(state)) {
                restart_game(hwnd, state);
            }
            break;
        case input::Action::ToggleTheme:
            toggle_theme(hwnd, state);
            break;
        case input::Action::Quit:
            DestroyWindow(hwnd);
            break;
        case input::Action::None:
            break;
    }
}

void handle_left_button_down(HWND hwnd, AppState& state, POINT point) {
    SetFocus(hwnd);

    if (overlay_active(state)) {
        if (PtInRect(&state.layout.overlayPrimary, point)) {
            handle_primary_action(hwnd, state);
        } else if (PtInRect(&state.layout.overlaySecondary, point)) {
            restart_game(hwnd, state);
        }
        return;
    }

    if (PtInRect(&state.layout.newGameRect, point)) {
        restart_game(hwnd, state);
        return;
    }

    if (PtInRect(&state.layout.themeRect, point)) {
        toggle_theme(hwnd, state);
        return;
    }

    if (PtInRect(&state.layout.gridRect, point)) {
        state.drag.active = true;
        state.drag.start = point;
        state.drag.current = point;
        SetCapture(hwnd);
    }
}

void handle_left_button_up(HWND hwnd, AppState& state, POINT point) {
    if (!state.drag.active) {
        return;
    }

    state.drag.current = point;
    state.drag.active = false;
    ReleaseCapture();

    game::Direction dir = game::Direction::Left;
    const int threshold = std::max(18, state.layout.cellSize / 2);
    if (input::direction_from_drag(state.drag, threshold, dir)) {
        apply_move(hwnd, state, dir);
    }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE: {
            AppState* state = new (std::nothrow) AppState();
            if (state == nullptr) {
                return FALSE;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        case WM_CREATE: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                RECT client;
                GetClientRect(hwnd, &client);
                state->layout = render::compute_layout(client);

                // 主题：优先沿用上次的选择，没有记录则跟随系统的深浅色设置
                theme::Mode mode = theme::Mode::Light;
                if (!theme::load_mode(mode)) {
                    mode = theme::system_mode();
                }
                state->mode = mode;
                state->currentTheme = theme::of(mode);
                state->themeFrom = state->currentTheme;
                state->themeTo = state->currentTheme;

                // 启动秒级刷新计时器，用于更新“用时”显示
                state->timerId = SetTimer(hwnd, kTimerId, kTimerIntervalMs, nullptr);
                refresh_title(hwnd, state->board);
            }
            return 0;
        }

        case WM_TIMER: {
            // 只有当显示的秒数发生变化时才重绘，避免无意义的刷新闪烁
            if (wParam == kTimerId) {
                AppState* state = state_of(hwnd);
                if (state != nullptr) {
                    const int seconds = state->board.elapsedSeconds();
                    if (seconds != state->lastTickSecond) {
                        state->lastTickSecond = seconds;
                        refresh_title(hwnd, state->board);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            } else if (wParam == kAnimTimerId) {
                AppState* state = state_of(hwnd);
                if (state != nullptr) {
                    update_theme_animation(hwnd, *state);
                }
            }
            return 0;
        }

        case WM_SIZE: {
            AppState* state = state_of(hwnd);
            if (state != nullptr && wParam != SIZE_MINIMIZED) {
                RECT client;
                GetClientRect(hwnd, &client);
                state->layout = render::compute_layout(client);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT minRect = {0, 0, config::kMinClientW, config::kMinClientH};
            AdjustWindowRectEx(&minRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
            info->ptMinTrackSize.x = minRect.right - minRect.left;
            info->ptMinTrackSize.y = minRect.bottom - minRect.top;
            return 0;
        }

        case WM_ERASEBKGND:
            // 背景完全由双缓冲绘制完成，屏蔽默认擦除以消除闪烁
            return 1;

        case WM_PAINT: {
            AppState* state = state_of(hwnd);
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (state != nullptr) {
                state->renderer.paint(hwnd, state->board, state->layout,
                                      state->currentTheme, state->winOverlay, state->loseOverlay,
                                      state->hover);
            }
            EndPaint(hwnd, &ps);
            (void)dc;
            return 0;
        }

        case WM_KEYDOWN: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                handle_key(hwnd, *state, wParam);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                handle_left_button_down(hwnd, *state, point);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (state->drag.active) {
                    state->drag.current = point;
                }
                // 只有悬停目标变化时才重绘，避免鼠标移动引发高频刷新
                const render::HoverTarget hover = hover_at(*state, point);
                if (hover != state->hover) {
                    state->hover = hover;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                // 跟踪鼠标离开事件，便于离开窗口后清除悬停高亮
                TRACKMOUSEEVENT track;
                ZeroMemory(&track, sizeof(track));
                track.cbSize = sizeof(TRACKMOUSEEVENT);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            AppState* state = state_of(hwnd);
            if (state != nullptr && state->hover != render::HoverTarget::None) {
                state->hover = render::HoverTarget::None;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                show_context_menu(hwnd, *state);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                handle_left_button_up(hwnd, *state, point);
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            AppState* state = state_of(hwnd);
            if (state != nullptr) {
                state->drag.active = false;  // 失去捕获时复位拖动状态
            }
            return 0;
        }

        case WM_SETCURSOR: {
            AppState* state = state_of(hwnd);
            if (state != nullptr && LOWORD(lParam) == HTCLIENT) {
                POINT point;
                GetCursorPos(&point);
                ScreenToClient(hwnd, &point);

                const render::HoverTarget hover = hover_at(*state, point);
                if (hover != state->hover) {
                    state->hover = hover;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                const bool overButton = (hover != render::HoverTarget::None);
                SetCursor(LoadCursorW(nullptr,
                                      MAKEINTRESOURCEW(overButton ? kCursorHand : kCursorArrow)));
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            AppState* state = state_of(hwnd);
            if (state != nullptr && state->timerId != 0) {
                KillTimer(hwnd, kTimerId);
                state->timerId = 0;
            }
            if (state != nullptr && state->animTimerId != 0) {
                KillTimer(hwnd, kAnimTimerId);
                state->animTimerId = 0;
            }
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int run(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(kIconApplication));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(kCursorArrow));
    wc.hbrBackground = nullptr;  // 由 WM_ERASEBKGND 接管
    wc.lpszClassName = kClassName;
    wc.hIconSm = LoadIconW(nullptr, MAKEINTRESOURCEW(kIconApplication));

    if (RegisterClassExW(&wc) == 0) {
        return 1;
    }

    RECT windowRect = {0, 0, config::kDefaultClientW, config::kDefaultClientH};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, kClassName, kAppTitle, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                windowRect.right - windowRect.left,
                                windowRect.bottom - windowRect.top,
                                nullptr, nullptr, hInstance, nullptr);
    if (hwnd == nullptr) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace window
