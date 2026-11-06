# LEGO Racers — native DualSense hook

为《LEGO Racers》(1999) 添加原生 USB DualSense 输入和音频触觉反馈的实验性 x86 Hook。
通过本地 `dinput.dll` 代理加载，再用 MinHook 对游戏内存做 inline hook；正常的
DirectInput 创建调用转发至 Windows 系统 DLL。不会修改磁盘上的游戏 EXE，也不需要
JoyToKey、DSX 或虚拟 Xbox 手柄。

## 已实现

| 操作 | 输入 |
| --- | --- |
| 连续模拟转向 | 左摇杆 |
| 连续油门 | R2（无扳机振动） |
| 使用道具 | L2；仅持有道具时启用开火阻力 |
| 漂移 | R1 |
| 刹车／倒车 | 圆圈 |
| 菜单方向／确认／返回 | 十字键或左摇杆／叉键／圆圈 |
| 暂停 | Options |

握把触觉在独立四声道 WASAPI 音频端点上合成，前两声道静音，后两声道驱动左右振子：

- 从游戏音效 PCM 缓冲区取得音效；音乐的 `DirectMusicStream` 路径不进入音效 Hook。
- 根据实际车速生成轻微发动机底震。
- 根据接触轮胎的材质标签和物理参数生成草地、沙地、碎石、雪／冰、水、路沿等粗糙反馈。
- 根据腾空时间与落地前沿地面法线的冲击速度生成轻重不同的落地冲击。
- 根据轮胎摩擦声／胎痕状态和实际横向速度生成有起伏的粗糙横滑震动。
- 持续横滑后，横向滑移明显收敛时补一个短促的回抓顿挫。

回抓与高频“黏滑”触感是根据游戏状态推导／合成的提示，不是测得的真实轮胎接触力。
合成包含平滑包络和总峰值限幅；暂停、切出窗口及离地等状态会抑制相应的持续效果。

## 支持的游戏版本与硬件

- Windows 11 + Visual C++ x86 原版游戏。
- `LEGORacers.exe`: 802,816 bytes，SHA-256：
  `fe9e881cfa3ffc208db23ab8057ecc5697bad94d2752e8eccf6c79b10a5c1103`。
- 只在上述版本做过实机验证。启动时检查基址与各 Hook 点原始指令，不匹配就拒绝安装 Hook。
  当前运行时检查是指令签名检查，不是完整文件 SHA-256 检查。
- 实测设备是 USB DualSense。代码也识别 DualSense Edge 的 PID，但 Edge 未做实机验证。
- 本项目没有实现蓝牙传输；声音触觉要求可用的 USB 四声道音频端点。
- 当前只针对第一位本地人类玩家；没有验证多人手柄输入。

## 构建

需要 Git、Visual Studio 2022 的“使用 C++ 的桌面开发”、Windows SDK 和 CMake 工具。
在仓库目录运行：

```bat
build.cmd
```

脚本自动定位 VS 自带 CMake（或 PATH 中的 CMake），配置 Win32 Release，编译并运行纯逻辑测试。
首次构建会从官方 Git 仓库拉取固定提交版本的 MinHook。产物：

```text
build/Release/dinput.dll
build/Release/probe.exe
```

也可手动运行：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 检查与验证

`build.cmd` 中的逻辑检查不需要游戏文件或手柄，覆盖原生模拟输入、L2 道具门控、
R2 无振动、底震／地形／横滑波形、落地判定、回抓状态机和混合限幅。

检查实际 USB HID 与四声道格式支持：

```bat
build\Release\probe.exe
```

此检查不会自动播放测试震动。退出码 2 表示没有找到可访问的支持手柄。
自动检查不能证明所有游戏场景或实际触感正常，仍需在游戏中测试。

`signatures.cpp` 已包含支持版本的指令签名，正常构建不需要游戏 EXE。
若要核对并重新生成，可以使用自己的游戏文件：

```bat
python signatures.py "D:\Games\LEGO Racers\LEGORacers.exe"
```

该脚本首先校验完整 SHA-256，只接受上面列出的原版版本。

## 安装和回退

1. 退出游戏，将游戏目录和存档备份。
2. 若已有其他 `dinput.dll`，先备份并排查冲突；本项目不能自动链式加载其他同名代理。
3. 将构建的 `dinput.dll` 放到 `LEGORacers.exe` 同级目录。
4. 用 USB 连接手柄。关闭 DSX、Steam Input 等可能改写此手柄输出的程序，关闭启动器的
   JoyToKey 支持，避免重复输入或输出所有权冲突。
5. 保留已有图形兼容组件，通过原来的方式启动游戏。Hook 记录 `ds5-native.log` 于游戏工作目录。
   普通声音仍应从音箱／耳机输出，手柄端点用于四声道触觉。
6. 回退时退出游戏，移走本项目的 `dinput.dll`，恢复原文件及原控制器设置。

开发时在隔离的游戏副本中配合已有 MrGameFixer 游戏资源及 dgVoodoo 图形组件进行了用户实测。
尚未完成对原始启动器所有选项、全屏切换和正式安装流程的完整兼容验证。
不要把该项目当成适用于任意游戏版本的通用补丁。

## 实现和已知限制

- 所有游戏对象访问在游戏线程进行；音频线程只混合复制的 PCM、查询已 AddRef 的音效缓冲区和消费合成事件。
- 在游戏的控制／菜单事件函数层接入输入，不向操作系统模拟键盘按键。
- 材质按已知游戏标签、滚动阻力和轮胎粒子参数分类；未知且无粗糙物理标记的材质不会被直接视为非道路。
- USB HID 会重试连接；音频端点断开后的自动恢复仍不完整，重新插拔后可能需要重启游戏。
- 正常退出会尝试解除扳机效果；强制终止进程不能保证执行退出清理。
- 参数目前集中在 `native.cpp`，尚未提供设置界面。

## 依赖与致谢

见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。仓库只包含 Hook 自有实现、少量版本识别指令签名、
构建脚本及依赖说明，不包含游戏可执行文件、关卡、音乐、音效、存档或商业启动器。
