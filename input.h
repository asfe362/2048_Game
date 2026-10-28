#ifndef INPUT_H_
#define INPUT_H_

#include <windows.h>

#include "game.h"

namespace input {

// 归一化后的输入动作，窗口过程只需处理这一层
enum class Action {
    None,
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Restart,   // 重新开始
    Primary,   // 遮罩上的主按钮（继续游戏 / 再来一局）
    Secondary, // 遮罩上的次按钮（重新开始）
    Quit       // 关闭窗口
};

// 鼠标拖动状态
struct DragState {
    bool active = false;
    POINT start = {0, 0};
    POINT current = {0, 0};
};

// 键盘：方向键与 WASD 都能控制移动
Action action_from_key(WPARAM key);

// 鼠标拖动：位移超过阈值后按主轴方向判定移动方向
bool direction_from_drag(const DragState& drag, int threshold, game::Direction& outDirection);

}  // namespace input

#endif  // INPUT_H_
