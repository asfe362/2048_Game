#ifndef WINDOW_H_
#define WINDOW_H_

#include <windows.h>

namespace window {

// 注册窗口类、创建主窗口并进入消息循环；返回进程退出码
int run(HINSTANCE hInstance, int nCmdShow);

}  // namespace window

#endif  // WINDOW_H_
