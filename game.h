#ifndef GAME_H_
#define GAME_H_

#include <array>
#include <chrono>
#include <random>

#include "config.h"

namespace game {

// 四个移动方向
enum class Direction { Up, Down, Left, Right };

// 4x4 棋盘，0 表示空格
using Grid = std::array<std::array<int, config::kBoardSize>, config::kBoardSize>;

// 纯逻辑游戏内核：不依赖任何 Win32 API，便于单独测试与复用
class Board {
public:
    Board();

    // 开始新的一局：清空棋盘并随机放置两个方块（最高分会被保留）
    void reset();

    // 执行一次移动；棋盘发生变化返回 true
    bool move(Direction dir);

    // 在随机空格生成一个 2（90%）或 4（10%）
    bool spawnTile();

    // 是否还存在合法操作（有空格或存在可合并的相邻方块）
    bool canMove() const;

    // 无空格且无可合并方块
    bool isGameOver() const;

    // 本局是否曾经合成出 >= 2048 的方块
    bool hasWon() const;

    // 胜利提示是否仍处于“待确认”状态
    bool isWinPending() const;

    // 玩家选择“继续游戏”：清除待确认标记，但保留 hasWon 状态
    void acknowledgeWin();

    int score() const { return m_score; }
    int bestScore() const { return m_bestScore; }
    void updateBestScore();

    // 本局已进行的秒数（游戏结束后停表）
    int elapsedSeconds() const;

    // 停止计时；游戏结束时调用（重复调用安全）
    void stopTimer();

    int tile(int row, int col) const;

    static constexpr int rows() { return config::kBoardSize; }
    static constexpr int cols() { return config::kBoardSize; }

private:
    Grid m_grid;
    int m_score;
    int m_bestScore;
    bool m_won;
    bool m_winPending;
    // 计时器：使用单调时钟，避免系统时间调整导致跳变
    std::chrono::steady_clock::time_point m_startTime;
    int m_elapsedBase;   // 停止计时时已累计的秒数
    bool m_timerRunning;
    std::mt19937 m_rng;
};

}  // namespace game

#endif  // GAME_H_
