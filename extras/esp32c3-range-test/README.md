# PN5321 全协议卡片识别 / 手动距离测试（UART）

适用合宙 LuatOS CORE-ESP32-C3，PN532 HSU/UART，默认自动扫描并读取卡号。
用于逐张样品手工测试识别距离，完整源码位于 [PN532_All_Cards_UART_Range_Test](PN532_All_Cards_UART_Range_Test)。
这是独立测试工程，自带有界 HSU 驱动，无需 NDEF 或额外 NFC 库。
本目录位于 `extras`，以区别于库的通用 Arduino 示例；它仅适用于这里指定的 ESP32-C3 接线和板载 LED。

## PN5321 支持什么

本项目 PN5321 V5 使用 NXP PN532。按 PN532/C106 固件 1.6 和 NXP 数据手册，
被动读卡协议为 ISO14443A、ISO14443B、FeliCa、Innovision Jewel。
新版使用这些协议的通用识别入口，不用芯片型号白名单拒绝兼容卡。
另加入 ST SR 系列专用 INITIATE/SELECT/GET_UID 序列，覆盖普通 REQB/ATTRIB 无法选择的 ST B 类存储卡。

| 卡片芯片 / 系列（常见样品） | 距离测试命令 | 本程序可测的操作 |
| --- | --- | --- |
| MIFARE Mini / S20 | `a` | UID；`c` 默认 Key A 认证读取第 4 块 |
| MIFARE Classic 1K / S50、4K / S70、Classic EV1 | `a` | UID；`c` 认证读取第 4 块 |
| FM11RF08、FM11RF32 等 Classic 兼容芯片 | `a` | 按实际 ISO14443A 应答识别；默认密钥可用时 `c` 读块 |
| MIFARE Ultralight、Ultralight C、Ultralight EV1 / AES | `a` | UID；未受保护时 `t` 读页；部分支持 `v` 查版本 |
| NTAG203、210、212、213、215、216、NTAG I2C / I2C Plus 等 Type 2 | `a` | UID；`t` 读第 4～7 页；`v` 可精确报告标准 NTAG213/215/216 版本应答 |
| MIFARE DESFire / EV1 / EV2 / EV3 | `a` | ISO14443A / ISO-DEP 激活、UID、ATS；不读取加密应用文件 |
| MIFARE Plus S / X / EV1 / EV2 等 | `a` | 当前安全级别下的 ISO14443A 识别；SL1 兼容模式才适用 `c` |
| NTAG424 DNA、ST25TA 等 ISO14443-4A / NFC Type 4A | `a` | 通用 UID / ATS；不读取加密应用文件 |
| 其他 ISO14443A 兼容 CPU 卡 / Type 4A 卡 | `a` | 通用 UID / ATS，未知 SAK 也可做识别距离测试 |
| 符合 ISO14443-4B 的 CPU 卡 / NFC Type 4B 标签 | `b` | 标准 REQB/ATTRIB 激活、ATQB、PUPI；不凭 PUPI 判断永久身份或精确芯片型号 |
| SRT512、SRI512、SRIX4K、ST25TB 系列兼容存储卡 | `k` | 独立 ST SR 读 UID 序列；此软件路径仍待实卡验证 |
| Sony FeliCa、FeliCa Lite / Lite-S 等 Type 3 兼容芯片 | `f` / `g` | 212 / 424 kbps 轮询，读 IDm、PMm 和系统码；不读取加密服务内容 |
| Innovision / Broadcom Jewel、Topaz / Topaz512 等 Type 1 兼容芯片 | `j` | 通用 Jewel ID 和 SENS_RES |

表中所有产品都须符合相应协议、天线正确且处于可响应状态。型号示例不是根据 UID/SAK 即可保证精确确认的白名单。
特别是 DESFire/Plus/其他 Type 4A 会显示通用家族提示，Type 2 的 SAK=00 也不能唯一说明是 NTAG213。
FeliCa 具体芯片支持的 RF 速率以其规格为准，只支持 212kbps 的样品应使用 `f`。
用 `n <名称>` 手动标记实物型号；`[VERSION]` 是卡片报告的版本，也不是芯片真伪鉴定。

**不支持 ISO15693 / NFC Type 5**，例如 ICODE SLIX / SLIX2、ST25TV。
不支持 125kHz 的 EM4100/TK4100/T5577，也不支持 UHF EPC。
NFC-IP1/P2P 和卡模拟是 PN532 的另一些工作模式，未纳入本次被动卡片距离测试。
专有 B'/iCLASS 等低层扩展不属于本版上述通用入口，不能把“ISO14443B”理解成任意 B 类变种自动可读。

资料冲突说明：ELECHOUSE PN5321 MINI 页面/手册中的 ISO15693 条目与 NXP 芯片资料冲突。
本程序以 NXP 原厂支持范围为准，没有添加无硬件支持的 ISO15693 扫描选项。

## 接线与串口

与已经验证的 UART 版本保持相同接线：

| PN532 V5 | LuatOS ESP32-C3 |
| --- | --- |
| TX / SDA | GPIO4 / IO04（UART1 RX） |
| RX / SCL | GPIO5 / IO05（UART1 TX） |
| VCC | 3.3V |
| GND | GND |

V5 的 J1、J2 均断开，HSU/UART 模式，改模式后 PN532 整体断电重上电。
PN532 链路和电脑串口均为 115200、8N1。主控 D4=GPIO12，高电平亮；D5=GPIO13 保持灭。

## 测试判据

默认 `AUTO + ID_ONLY`：依次扫描 A106、B106、F212、F424、Jewel、ST SR，
找到一张卡即结束本轮；只有完整一轮没有发现卡才记一次 NO_CARD。
根据 PN532 固件 support 字节跳过不支持的 Type B 模式。
发现卡号并完整解析应答才点亮 D4，失败/无卡熄灭。

实际测距离时**先固定协议**，避免 AUTO 的不同扫描时长影响观察。
每次尝试都关闭 RF 场 20ms，再打开等待 10ms，使静止的卡也能独立重新激活。
ISO-DEP 卡的高层激活可能含 RATS/ATS，因此这里测的是完整识别/激活距离。

成功分开记录：

- `ID_ONLY`：完整识别并读出 UID / PUPI / IDm，不要求卡内业务密钥。
- `CLASSIC_READ4`：默认 Key A `FFFFFFFFFFFF` 认证 + 读 16 字节。
- `TYPE2_READ_PAGE4`：读第 4～7 页，共 16 字节。
- `TYPE2_GET_VERSION`：版本查询命令，旧卡不支持此命令不代表无法识别。

Classic 密钥错误、Type 2 页被保护、版本查询不支持，都不能直接当作识别距离失败。
Type B 的 PUPI 和某些卡的 UID 可能每次激活变化。一次只放一张卡，用实物标签管理样品。
卡片内容不写入；SR 路径只暂时配置读卡器寄存器，结束后恢复。
SR 路径按 ST 协议进行非标准 Type B 选卡，需用户以实物完成最终验证。

## 串口命令（每条后发送回车或换行）

| 命令 | 功能 |
| --- | --- |
| `x` | AUTO 全协议扫描，ID 判据 |
| `a` | 固定 Type A / 106 kbps，ID 判据 |
| `b` | 固定 Type B / 106 kbps，ID 判据 |
| `f` / `g` | 固定 FeliCa 212 / 424 kbps，ID 判据 |
| `j` | 固定 Jewel / Topaz，ID 判据 |
| `k` | 固定 ST SR/SRI/SRT/ST25TB 专用识别 |
| `u` | 当前协议切回 ID_ONLY |
| `c` | Type A + Classic 第 4 块认证读 |
| `t` | Type A + Type 2 第 4～7 页读 |
| `v` | Type A + Type 2 GET_VERSION；标准 NTAG213/215/216 和部分 Ultralight EV1 可报告型号 |
| `n NTAG213_01` | 手动样品名称，最多 39 个 ASCII 字符；特殊字符替换为下划线 |
| `d 35` | 手工用尺量的 35mm，清空统计开始新测点；`d -1` 表示未知距离 |
| `r` | 清空统计并重新初始化 |
| `s` / `h` | 当前统计 / 帮助 |

改协议/判据/样品会清空统计并把距离恢复为未知（-1）；改距离会清空统计。
未知命令、错误距离或超长命令不改变当前测点。

例：测 NTAG213 在 30mm 的识别稳定度，每行发送回车：

```text
a
n NTAG213_01
u
d 30
```

保持卡片位置，等待 `[WINDOW20] pass=20 no_card=0 errors=0`。
再输入 `d 35`，等下一组 20 次。找到可重复达到 20/20 的最远测点，
并记录卡片平面与读卡器天线平面的实际间距、方向、供电、金属环境。
`d` 只是人工记录，PN532 不会自动测距；20 次仅是一个观测窗口，不代表长期可靠性保证。
其他芯片换相应协议和样品名称；一个协议下的不同芯片无需反复改固件。

`[CARD]` 输出协议、卡号、ATQA/SAK、家族提示、ATS/ATQB/PMm 等原始信息。
`[WINDOW20]` 输出一组 20 次的成功/无卡/错误数。
`[STATS]` 输出当前测点累计成功率和连续成功次数。
`RESULT,...` 是 CSV 格式的单次测试行，含样品、人工距离、协议、判据、标识、结果和耗时，方便保存串口日志。
示例行只说明格式，不代表实测：

```text
RESULT,4,NTAG213_01,30,A106,ID_ONLY,A106,UID,04112233445566,Type2/NTAG/Ultralight-compatible,PASS,75,1
```

## 编译与测试

安装 Arduino ESP32 core **3.3.8**，目标为 `ESP32C3 Dev Module`。
在 Arduino IDE 打开 `PN532_All_Cards_UART_Range_Test/PN532_All_Cards_UART_Range_Test.ino`。
配置 DIO、40MHz、4MB Flash、禁用 USB CDC On Boot；本板使用 CH343 连接 UART0。
无需安装本仓库的 NFC 库依赖即可编译这个独立测试工程。

仓库根目录 PowerShell：

```powershell
.\tools\build-esp32c3-range.ps1
.\tests\esp32c3-range\run.cmd
```

编译脚本默认使用 `D:\GreenSoftware\Arduino_cli\arduino-cli.exe`，找不到时使用 PATH 中的 `arduino-cli`。
也可以通过 `-ArduinoCli <路径>` 指定安装位置。脚本只编译，不连接串口或烧写。
编译产物在 `.artifacts/esp32c3-range`，临时编译文件在 `.build/esp32c3-range`。
Windows 模拟测试需要 Visual Studio C++ Build Tools；Linux/macOS 使用：

```sh
sh tests/esp32c3-range/run.sh
```

手动烧录示例（将 COM8 换为实际端口）：

```powershell
arduino-cli upload --fqbn "esp32:esp32:esp32c3:FlashMode=dio,FlashFreq=40,FlashSize=4M,CDCOnBoot=default,UploadSpeed=115200" --port COM8 --input-dir .artifacts/esp32c3-range extras/esp32c3-range-test/PN532_All_Cards_UART_Range_Test
```

### 验证范围

2026-11-11：Arduino ESP32 core 3.3.8 编译通过，20 项主机模拟检查通过。
实机验证为 LuatOS CORE-ESP32-C3、PN532 固件 1.6，默认 AUTO + ID_ONLY，
一张 Type A / SAK=18 兼容卡在 12 秒内识别成功 42 次，0 错误、0 初始化重试。
这验证了当前 Type A 识别路径；Type B、FeliCa、Jewel/Topaz、ST SR 等仍需相应实物逐项测试。
主机测试使用实际 `.ino` 和驱动配合模拟 PN532 帧，不能替代实卡距离测试。
板载 LED 的 GPIO12 置高已由代码与日志核对，没有通过摄像头观察物理灯。

## 原始资料

- [NXP PN532 用户手册，7.3.5 各种寻卡参数和返回格式](https://www.nxp.com/docs/en/user-guide/141520.pdf)
- [NXP PN532 数据手册](https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf)
- [NXP 官方培训资料：PN532 不支持 ISO15693](https://community.nxp.com/pwmxy87654/attachments/pwmxy87654/tech-days/274/1/AMF-CNS-T2777.pdf)
- [NXP MIFARE 卡型识别 AN10833](https://www.nxp.com/docs/en/application-note/AN10833.pdf)
- [NXP NTAG213/215/216 GET_VERSION 和 READ](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)
- [NXP Ultralight EV1](https://www.nxp.com/docs/en/data-sheet/MF0ULX1.pdf)
- [ST 原厂工具手册，SRI/SRT/ST25TB 的初始化与 GET_UID](https://www.st.com/resource/en/user_manual/um1084-cr95hf-development-software-stmicroelectronics.pdf)
- [libnfc 原始实现：PN53x 协议参数与 ST SR 寄存器配置的交叉核对依据](https://github.com/nfc-tools/libnfc/blob/master/libnfc/chips/pn53x.c)
- [ELECHOUSE PN5321 MINI 页面（部分规格与原厂资料存在上述差异）](https://www.elechouse.com/docs/pn5321-mini/)

代码按协议独立实现；没有复制 libnfc 的源代码进入固件。
