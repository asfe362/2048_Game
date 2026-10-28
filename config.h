#ifndef CONFIG_H_
#define CONFIG_H_

// 全局可调参数：所有模块共享的常量集中放在这里，避免魔法数字散落各处
namespace config {

constexpr int kBoardSize = 4;       // 4x4 网格
constexpr int kWinValue = 2048;     // 胜利目标：合成该数值的方块即获胜

constexpr int kMinClientW = 460;    // 客户区最小宽度
constexpr int kMinClientH = 560;    // 客户区最小高度
constexpr int kDefaultClientW = 640;
constexpr int kDefaultClientH = 760;

constexpr int kMinGridSize = 280;   // 网格边长下限（窗口缩小时）
constexpr int kMaxGridSize = 1600;  // 网格边长上限（窗口放大时）
constexpr int kMinCellSize = 52;    // 单个方块最小边长
constexpr int kMaxCellSize = 360;   // 单个方块最大边长

// 基准网格边长：默认窗口尺寸下网格的边长，界面所有组件尺寸都以该网格为参照按比例缩放
constexpr int kBaseGridSize = 600;

constexpr double kMinScale = 0.75;  // 缩放系数下限
constexpr double kMaxScale = 2.50;  // 缩放系数上限

}  // namespace config

#endif  // CONFIG_H_
