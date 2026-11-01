#pragma once

namespace informationcontent {
constexpr bool DEVELOPMENT_BUILD = true;
constexpr const char* DEVELOPMENT_BUILD_TEXT = " 测试版构建，可能不稳定 \n Sunch Client beta version";
constexpr const char* version = "1.2.0 测试";
constexpr const char* updates[] = {
    "MCPE0.14 Sunch Client",
    "作者Sunch233 QQ2125696621",
    "更新QQ群 649857110",
    "感谢「皓月当空」提供的native mod底包",
    "",
};
constexpr const char* credits[] = {
    "1.2.0: ",
    " - 最大视距提升到32并且可自由选择",
    " - 鼠标灵敏度Fov、亮度、按钮大小现在显示具体数值",
    "1.1.3: - 新增Win10 UI 切换",
    "1.1.2: - 新增JsonUI 布局调试功能",
    "1.1.1: - 实现了 glsl着色器 / JsonUI 错误处理",
    "1.1.0: ",
    " - 兼容了鼠标输入",
    " - 实现了强制疾跑",
    " - 重构代码，分离功能实现",
    "1.0.0: ",
    " - 实现了帧数显示 垂直同步 游戏内信息显示 动态光源功能",
};
}
