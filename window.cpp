#include "window.h"

#include <windowsx.h>

#include <algorithm>
#include <new>
#include <string>

#include "config.h"
#include "game.h"
#include "input.h"
#include "render.h"

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
};

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
                                      state->winOverlay, state->loseOverlay);
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
            if (state != nullptr && state->drag.active) {
                state->drag.current.x = GET_X_LPARAM(lParam);
                state->drag.current.y = GET_Y_LPARAM(lParam);
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

                bool overButton = PtInRect(&state->layout.newGameRect, point);
                if (overlay_active(*state)) {
                    overButton = PtInRect(&state->layout.overlayPrimary, point) ||
                                 PtInRect(&state->layout.overlaySecondary, point);
                }
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
