#include "input.h"

#include <cstdlib>

namespace input {

Action action_from_key(WPARAM key) {
    switch (key) {
        case VK_UP:
        case 'W':
            return Action::MoveUp;
        case VK_DOWN:
        case 'S':
            return Action::MoveDown;
        case VK_LEFT:
        case 'A':
            return Action::MoveLeft;
        case VK_RIGHT:
        case 'D':
            return Action::MoveRight;
        case 'R':
            return Action::Restart;
        case 'T':
            return Action::ToggleTheme;
        case VK_RETURN:
        case VK_SPACE:
            return Action::Primary;
        case VK_ESCAPE:
            return Action::Quit;
        default:
            return Action::None;
    }
}

bool direction_from_drag(const DragState& drag, int threshold, game::Direction& outDirection) {
    const int dx = drag.current.x - drag.start.x;
    const int dy = drag.current.y - drag.start.y;

    if (std::abs(dx) < threshold && std::abs(dy) < threshold) {
        return false;  // 位移太小，视为点击而非拖动
    }

    if (std::abs(dx) >= std::abs(dy)) {
        outDirection = (dx > 0) ? game::Direction::Right : game::Direction::Left;
    } else {
        outDirection = (dy > 0) ? game::Direction::Down : game::Direction::Up;
    }
    return true;
}

}  // namespace input
