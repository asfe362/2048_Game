#include "game.h"

#include <utility>
#include <vector>

namespace game {
namespace {

constexpr int kBoardSize = config::kBoardSize;

// 生成 4 的概率（百分比）
constexpr int kFourChancePercent = 10;

// 用 random_device 播种，避免每次运行都得到相同的序列
std::mt19937 make_rng() {
    std::random_device rd;
    return std::mt19937(rd());
}

}  // namespace

Board::Board()
    : m_score(0),
      m_bestScore(0),
      m_won(false),
      m_winPending(false),
      m_startTime(std::chrono::steady_clock::now()),
      m_elapsedBase(0),
      m_timerRunning(true),
      m_rng(make_rng()) {
    reset();  // reset() 会把棋盘清零并放置初始方块
}

void Board::reset() {
    for (auto& row : m_grid) {
        row.fill(0);
    }
    m_score = 0;
    m_won = false;
    m_winPending = false;
    // 新的一局：重新开始计时
    m_startTime = std::chrono::steady_clock::now();
    m_elapsedBase = 0;
    m_timerRunning = true;
    spawnTile();
    spawnTile();
}

bool Board::spawnTile() {
    std::vector<std::pair<int, int>> emptyCells;
    emptyCells.reserve(config::kBoardSize * config::kBoardSize);
    for (int r = 0; r < kBoardSize; ++r) {
        for (int c = 0; c < kBoardSize; ++c) {
            if (m_grid[r][c] == 0) {
                emptyCells.push_back(std::make_pair(r, c));
            }
        }
    }
    if (emptyCells.empty()) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> pick(0, emptyCells.size() - 1);
    std::uniform_int_distribution<int> chance(1, 100);

    const std::pair<int, int> cell = emptyCells[pick(m_rng)];
    m_grid[cell.first][cell.second] = (chance(m_rng) <= kFourChancePercent) ? 4 : 2;
    return true;
}

bool Board::move(Direction dir) {
    Grid next = m_grid;  // 先在副本上操作，确认有变化后再提交
    int gained = 0;

    for (int line = 0; line < kBoardSize; ++line) {
        // 统一抽象：cells[0] 永远是“移动方向上最靠前”的格子
        int cells[kBoardSize];
        int rowOf[kBoardSize];
        int colOf[kBoardSize];

        for (int i = 0; i < kBoardSize; ++i) {
            switch (dir) {
                case Direction::Left:  rowOf[i] = line;               colOf[i] = i;               break;
                case Direction::Right: rowOf[i] = line;               colOf[i] = kBoardSize - 1 - i; break;
                case Direction::Up:    rowOf[i] = i;                  colOf[i] = line;              break;
                case Direction::Down:  rowOf[i] = kBoardSize - 1 - i;  colOf[i] = line;              break;
            }
            cells[i] = m_grid[rowOf[i]][colOf[i]];
        }

        // 1) 压缩：把非零方块朝移动方向聚拢
        int compact[kBoardSize];
        int count = 0;
        for (int i = 0; i < kBoardSize; ++i) {
            if (cells[i] != 0) {
                compact[count++] = cells[i];
            }
        }

        // 2) 合并：相邻且数值相同则合并，合并后的方块本轮不再参与合并
        //    例如 [2,2,2] -> [4,2]（而不是错误地得到 [4,0]）
        int merged[kBoardSize];
        int mergedCount = 0;
        int i = 0;
        while (i < count) {
            if (i + 1 < count && compact[i] == compact[i + 1]) {
                const int value = compact[i] * 2;
                merged[mergedCount++] = value;
                gained += value;
                i += 2;
            } else {
                merged[mergedCount++] = compact[i];
                ++i;
            }
        }

        // 3) 写回：剩余位置补 0
        for (int k = 0; k < kBoardSize; ++k) {
            next[rowOf[k]][colOf[k]] = (k < mergedCount) ? merged[k] : 0;
        }
    }

    if (next == m_grid) {
        return false;  // 没有任何变化
    }

    m_grid = next;
    m_score += gained;

    if (!m_won) {
        for (int r = 0; r < kBoardSize; ++r) {
            for (int c = 0; c < kBoardSize; ++c) {
                if (m_grid[r][c] >= config::kWinValue) {
                    m_won = true;
                    m_winPending = true;
                    break;
                }
            }
            if (m_won) {
                break;
            }
        }
    }
    return true;
}

bool Board::canMove() const {
    for (int r = 0; r < kBoardSize; ++r) {
        for (int c = 0; c < kBoardSize; ++c) {
            if (m_grid[r][c] == 0) {
                return true;
            }
            if (c + 1 < kBoardSize && m_grid[r][c] == m_grid[r][c + 1]) {
                return true;
            }
            if (r + 1 < kBoardSize && m_grid[r][c] == m_grid[r + 1][c]) {
                return true;
            }
        }
    }
    return false;
}

bool Board::isGameOver() const {
    return !canMove();
}

bool Board::hasWon() const {
    return m_won;
}

bool Board::isWinPending() const {
    return m_winPending;
}

void Board::acknowledgeWin() {
    m_winPending = false;
}

void Board::updateBestScore() {
    if (m_score > m_bestScore) {
        m_bestScore = m_score;
    }
}

int Board::elapsedSeconds() const {
    if (!m_timerRunning) {
        return m_elapsedBase;
    }
    // steady_clock 单调递增，不受系统时间调整影响
    const std::chrono::seconds delta =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                          m_startTime);
    return m_elapsedBase + static_cast<int>(delta.count());
}

void Board::stopTimer() {
    if (!m_timerRunning) {
        return;
    }
    m_elapsedBase = elapsedSeconds();
    m_timerRunning = false;
}

int Board::tile(int row, int col) const {
    if (row < 0 || row >= kBoardSize || col < 0 || col >= kBoardSize) {
        return 0;
    }
    return m_grid[row][col];
}

}  // namespace game
