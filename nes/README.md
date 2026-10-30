# ALPHA i.MX6ULL NES 模拟器

FCEUmm 核心 + Linux fbdev / evdev / ALSA PCM 前端，适配本工作区
1024×600 LCD、USB 键盘和 WM8960。无需 SDL、RetroArch、X11、Wayland、
libasound 或 alsa-utils。核心源码固定在 vendor/fceumm，首次编译无需联网。

支持未压缩的 iNES `.nes` ROM，核心提供多种 mapper；魂斗罗、超级玛丽的
具体版本和改版兼容性需要使用实际 ROM 上板确认。没有附带商业游戏 ROM。
请将自己有权使用的游戏放在 roms/，不要把 zip 文件直接传给程序。

## 构建和部署

主机：

```sh
cd /home/woody/alpha/woody/app/nes
make                    # build/arm/nes + fceumm_libretro.so
make check              # 主机编译、合成 ROM 和前端检查
make dist               # dist/nes-armhf.tar.gz
```

默认 `arm-linux-gnueabihf-gcc`，可覆盖 `CROSS_COMPILE`、`USER_CC` 和 `BUILD`。
依赖 ARM hard-float glibc 运行库：`/lib/ld-linux-armhf.so.3`、libc、libm 等。
`nes` 通过 `$ORIGIN` 查找同目录的核心动态库，两者必须一起复制。
板端若提示 `not found` 而文件存在，检查 ARM 动态加载器；若提示
`GLIBC_x.y not found`，使用板端 rootfs 对应的工具链重新编译两个文件。
更换工具链或修改 vendor 后先 `make clean`。

将 dist/nes-armhf.tar.gz 复制到开发板可写目录，板端 root 执行：

```sh
tar -xzf nes-armhf.tar.gz
cd nes
# 将自己的 ROM 复制为 roms/contra.nes、roms/mario.nes
cat /proc/asound/cards
cat /proc/bus/input/devices
./nes roms/contra.nes
./nes roms/mario.nes
```

默认（或 `--pcm auto`）扫描 `/dev/snd/pcmC*D*p`，优先选择 card ID 包含
wm8960 的唯一播放设备；否则仅在只有一个播放设备时自动使用。
多个设备无法唯一确定时列出路径并要求 `--pcm`。没有设备时检查声卡驱动加载
和 devtmpfs/mdev；自动检测不能代替加载声卡驱动。显式 `--pcm PATH` 不会
在路径错误时悄悄切换到别的设备。

声卡编号以 `/proc/asound/cards` 中 alpha-wm8960 为准。例如声卡 1：

```sh
./nes --pcm /dev/snd/pcmC1D0p roms/contra.nes
```

启动前沿用已经验证的音频模块和混音器配置。如重启后路由未设置，使用
本工作区 driver/audio 的 audio_setup.sh 和 audio_ctl（两者放在一起）：

```sh
# 在板端已复制的音频工具目录运行，1 换成实际声卡编号
sh ./audio_setup.sh 1 headphone mic
# 使用外接喇叭时改为 speaker
```

程序不修改声卡音量。音频初始化失败会报错退出；静音排查可运行：

```sh
./nes --mute roms/mario.nes
```

## 键位

| 功能 | 玩家 1 | 玩家 2 |
| --- | --- | --- |
| 方向 | ↑ ↓ ← → | W S A D |
| A（跳跃） | X | H |
| B（射击/加速） | Z | G |
| Start | Enter | T |
| Select | 右 Shift | R |
| 退出 | ESC | ESC |

默认自动扫描 event0～event31，选择包含 Enter 和方向上键的键盘接口；
支持同一键盘双人操作，也可连接多个键盘，键位按玩家区分。
默认自动扫描支持 USB 键盘和手柄热插拔。某些键盘同时按多键会冲突，双人时可换键盘验证。
指定设备：

```sh
./nes --input /dev/input/event2 --pcm /dev/snd/pcmC1D0p roms/contra.nes
```

程序用 EVIOCGRAB 独占键盘，防止游戏按键输入登录终端，退出后自动释放。
应从串口或 SSH 启动；运行中 USB 键盘不可用于其他终端输入。
Ctrl+C（串口/SSH）和 SIGTERM 均会正常退出并保存 SRAM。
指定 `--input` 时只读取该接口，重新插拔后需重启；默认模式会自动重新发现。

## 显示和运行参数

默认将游戏画面以最近邻方式横向和纵向拉伸至整个 LCD（1024×600），
画面比例随屏幕变化。`--stretch` 显式启用此模式。
如需原来的整数倍居中模式，可使用 `--integer-scale` 或 `--scale 2`。
支持 truecolor 16/24/32bpp、framebuffer stride 和 x/y offset；默认 `/dev/fb0`。
本前端直接写当前 framebuffer，没有双缓冲，可能有撕裂。

```sh
./nes --fb /dev/fb0 --mute roms/mario.nes
./nes --frames 600 --mute roms/mario.nes   # 运行指定帧数后退出
```

建议从串口/SSH 启动并停止 LCD 上的 getty、图形程序及控制台输出。
fbcon 的文字或光标可能覆盖画面，隐藏光标只能处理光标；可按现有
LCD 测试流程临时解绑对应 fbcon（先记录原来的绑定状态，退出后恢复），
不要把模拟器日志重定向到 LCD 终端。

游戏按核心报告的 NTSC/PAL 帧率运行，单调时钟限速；音频固定
48000Hz / S16_LE / 双声道。有声时以 PCM 阻塞传输限速，静音时按核心报告的
帧率使用单调时钟限速；播放前先积累 3072 帧音频（64ms）。
发生音频 underrun 会 prepare 后恢复；其他音频错误会停止并报错。
不保证在尚未实测的板端上满帧，慢速时先 `--mute` 对比，再排查 CPU 负载、
频率和 framebuffer 写入速度。支持自动/手动跳过显示帧，没有即时存档菜单。

## 电池存档

有电池 RAM 的游戏在正常退出（ESC、Ctrl+C、SIGTERM 或 --frames 到期）时
保存到 ROM 路径加 `.sav`，如 `roms/game.nes.sav`，下次启动自动加载。
使用临时文件 + fsync + rename 替换存档，ROM 所在目录须可写。
没有电池 RAM 的游戏不生成存档；这是卡带存档，不是任意时刻的即时存档。
断电、SIGKILL 无法保存本次进度。

## 验证范围

已完成 ARM hard-float 交叉编译和主机 `-Wall -Wextra -Werror` 前端构建。
`make check` 使用原创 mapper-0 ROM 测试 120 帧运行、音频产生、SRAM 往返，
并检查 framebuffer 缩放、stride/offset、RGB565/XRGB8888 转换和双人键位。
120 帧生成 95835 个双声道音频帧（约 2 秒）。测试不使用商业游戏。
没有连接开发板，尚未验证实际游戏、帧率、LCD/evdev/PCM 实机联调。

无硬件核心冒烟测试（务必加 --mute；headless 不进行限速）：

```sh
./build/host/nes --headless --mute --frames 120 path/to/your-game.nes
```

源码来源和许可证见 UPSTREAM.md、vendor/fceumm/Copying、Authors。
部署包为板端运行文件；分发时应同时提供本目录完整源码及上游许可证。


## 音乐速度和音调异常

先退出模拟器，使用原 audio_test 进行独立的 5 秒播放测试（声卡编号换成实际值）：

```sh
cd /home/audio_test
./audio_test tone /dev/snd/pcmC1D0p 5 1000 both
```

程序报告的实际播放耗时应接近 5 秒。如果明显不同，先排查声卡时钟和实际加载的
DTB，见 driver/audio/README.md 的 PLL4 修复记录。不要只看 hw_params 的 48000。
如果此测试正常，再比较显示开关下的模拟器统计：

```sh
./nes --frames 600 roms/CONTRA.NES
./nes --headless --frames 600 roms/CONTRA.NES
```

第二条保留声音但不显示和读取键盘，600 帧后退出。NTSC 游戏约需 10 秒，
PAL 游戏约需 12 秒（存在音频队列预填充造成的小幅误差）。退出打印实际 fps、
PCM 写入帧数、underrun 次数、采样峰值和满幅样本数；静音 headless 不限速，
不能用于这个耗时对比。只有显示开启时 underrun 明显增长，可进一步排查
显示开销和 CPU 负载；无 underrun 也不能单凭此证明硬件时钟正确。


## 显示拖慢音频的优化与上板验证

旧版在显存中复制放大行。用户实测魂斗罗开启显示为 36.43 fps、600 帧耗时
16.468 秒、59 次 underrun；关闭显示为 60.39 fps、9.936 秒、0 次 underrun，
后者听音正常。因此优先优化显示路径，暂不修改声卡时钟。

新版在普通 RAM 内生成放大行，仅向 framebuffer 连续写入，避免从映射显存
读取；XRGB8888 到常见 32bpp framebuffer 使用直接像素复制路径。
默认 `--frameskip auto`：有声时若运行落后于模拟进度，跳过部分显示回调，
继续完整运行 CPU/APU 和输入，最多连续跳过 9 次显示。退出统计实际绘制和
跳过帧数。此算法不能弥补模拟器核心自身超出 CPU 算力的情况。

```sh
./nes --frames 600 roms/CONTRA.NES
# 若仍有欠载，固定隔一帧显示一次（约 30fps 显示，模拟目标仍约 60fps）：
./nes --frameskip 1 --frames 600 roms/CONTRA.NES
# 进一步降低显存写入量，原始大小 256×约240，居中显示：
./nes --scale 1 --frames 600 roms/CONTRA.NES
# 禁止跳帧，以便单独衡量优化后的完整绘制性能：
./nes --frameskip 0 --frames 600 roms/CONTRA.NES
```

`--frameskip N` 表示每 N+1 帧绘制一次（0～9）；`--scale N` 限制整数倍数
为 1～4，仍以屏幕能够容纳的大小为上限。静音模式默认不自动跳帧。
新增主机回归测试逐字节比较 27 种输入像素格式/输出 bpp/缩放组合，覆盖
stride、offset、黑边保护和跳帧分支。优化后的实际帧率和听音需板端确认。


## 全屏拉伸

默认已改为铺满 LCD，横纵方向独立缩放，显示完整的核心输出画面；
不是裁切放大。继续采用 RAM 行缓冲，显存只写不读，保留自动跳帧。

```sh
./nes roms/CONTRA.NES
# 验证全屏下声音和速度：
./nes --frames 600 roms/CONTRA.NES
# 恢复原来的整数倍居中显示：
./nes --integer-scale roms/CONTRA.NES
```

全屏比原先居中模式写入更多像素，开发板满速和音频连续性仍需重新验证。
新增测试覆盖 1024×600 的非整数纵向缩放、缩小、16/24/32bpp、
stride 和 offset，并逐字节检查可见区和周围内存。


## USB 游戏手柄

直接使用 Linux evdev，无需 /dev/input/js0 或 joydev。默认每秒扫描 event0～31，
支持手柄插入、拔出、重新插入；拔出时释放按键，避免方向卡住。
前两个检测到的手柄分配给玩家 1、2，键盘仍可同时控制。
支持 ABS_X/Y 左摇杆（带中心死区）、ABS_HAT0X/Y 帽开关、BTN_DPAD 方向键。
标准 BTN_SOUTH → A/跳跃，BTN_EAST → B/射击，BTN_START → Start，
BTN_SELECT → Select；另兼容 BTN_TRIGGER/THUMB 和 BTN_BASE/BASE2 的旧式 HID 编号。
北/西面键映射连发 A/B。不同手柄丝印和上报编号可能不一致。

```sh
./nes roms/CONTRA.NES
# 启动或插入后应出现：Input: /dev/input/eventN [...] gamepad -> player 1
```

如果手柄按键位置不符，先退出模拟器，查看输入接口并运行事件查看工具：

```sh
cat /proc/bus/input/devices
./nes-input /dev/input/eventN
```

将 eventN 换成 Zikway Gamepad 对应的 Handlers 中的 event 编号（不要选
Consumer Control 或 Keyboard 接口）。依次按方向、跳跃、射击、Start、Select，
记录工具输出，以便调整该手柄映射；Ctrl+C 退出查看工具。
指定 `--input /dev/input/eventN` 也支持手柄，但该模式不自动重连。

日志中的 HID Gamepad 注册成功说明该次枚举已完成。之前的 USB `-71` 枚举失败
与应用键位映射是两个问题；若持续出现断开重连，需要另查 USB 连接/供电/模式。
当前不需要修改内核。若手柄实际切换成 Xbox 045e:028e 模式，需要另外核对
xpad 驱动支持；该次失败发生在读取 USB 配置描述符阶段，加载 xpad 不能修复它。
应用层手柄测试覆盖按键、双人分配、轴死区、帽开关、事件丢失同步和拔出清理；
实际 Zikway 的事件码及热插拔效果仍需上板确认。
