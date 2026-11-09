# Device Capabilities for Defold

面向 Defold 的独立系统能力扩展，当前版本 **0.2.1**，最低 Defold 1.14.0。导出目录为 `device_capabilities`。

- Android: 设备信息快照、公开风险信号、广告标识只读获取与 Play Integrity Standard API.
- iOS: 设备信息快照、公开风险信号、已授权广告标识与供应商标识只读获取.
- Windows/Web：提供同名 API，设备检测返回 unsupported，Integrity 返回 unavailable。

Android 依赖 Google Play Integrity 1.6.0 与 play-services-ads-identifier 18.2.0. iOS 使用系统框架. 快照可提供当前网络接口及其本地 IP, 不读取地理位置或声明定位权限. 标识、token 与 nonce 不写日志.

## 接入

本地开发将仓库的 `device_capabilities/` 目录链接到游戏根目录同名位置。作为 Defold 库接入时，在游戏工程中添加本仓库的固定版本 ZIP 依赖。0.2.1 发行包统一命名为 `0.2.1.zip`，不带 `v` 前缀。

0.2.0 移除了 `location_start/location_poll/location_cancel`，不保留空桩；调用方需同步移除定位逻辑。删除旧 `[device_capabilities] location_usage_description` 配置，并核实游戏自有 Android Manifest / iOS Info.plist 中没有残留的定位声明。设备信号与 Integrity API 保持原语义。

仓库根工程可直接用对应版本 Bob 构建 `example/main.collection`。示例仅验证接口存在和打印能力状态，不请求 Integrity。设备信号与 Play 配置必须在目标设备验证。

## Lua API

原生全局表 `device_capabilities`：

| API | 结果 |
| --- | --- |
| `snapshot_start(id)` | 开始独立设备快照, 正 id. 不支持平台返回 false |
| `snapshot_poll(id)` | 返回 pending / ready / unavailable / unsupported 及已取得字段表, pending 可有部分数据 |
| `snapshot_cancel(id)` | 使本次快照失效并释放结果, id=0 取消当前请求 |
| `device_info()` | `{status="observed"/"unsupported", vpn=?, proxy=?, usb_debugging=?, developer_mode=?}`，可观察字段为布尔，不可读取字段为 nil |
| `integrity_start(id, cloud_project_number, request_data)` | Android 受理返回 true；project number 为正十进制字符串，request_data 是 1..4096 字节 UTF-8 文本 |
| `integrity_poll(id)` | pending / success / failed / unavailable / dialog / dialog_complete，另返回 token 字符串与错误/对话框结果码字符串 |
| `integrity_show_dialog(id, type)` | 使用当前成功 token 展示 Google 修复对话框；通过 poll 等待完成 |
| `integrity_cancel(id)` | 使异步结果失效并释放 token；id=0 取消当前请求 |

Integrity 只有一个活动请求槽；新 start 替换前一个，调用方须使用不同请求 id、管理所有权及超时，final 时取消。旧 id 不会读到新请求结果。Google 请求不能真正中断，取消后结果被丢弃。Google provider 按 project number 复用，失效/修复后丢弃。

Integrity 在原生侧计算 `SHA256(UTF8(request_data))`，转 URL-safe、无 padding/no-wrap Base64 作为 Standard API `requestHash`。token 原样交给调用方，由业务服务器验证；扩展不会自行判定设备可信、读取服务端凭据或访问业务接口。调用方应在超时及业务完成后取消。`request_data` 的内容由接入方与其服务端约定。

## 快照契约

新增快照 API 随本次开发源码提供. 接入时检查实际接口, 不单凭版本字符串判断能力. 调用者为每次请求分配独立 id, 最多等待 1 秒并取消, 不支持、读取失败、授权拒绝时字段缺失. 快照与 Integrity 分别持有请求槽和所有权. Android 工作线程无排队任务, 服务阻塞时保留部分结果并丢弃取消后的结果. 稳定硬件信息在进程内缓存, 标识与动态信号每次读取.

字段采用 DeviceInfo 的名称. 布尔信号为 0/1, int64 为十进制字符串, float 为 number, gpuFamilies 为字符串列表. networkInterface 是供调用者用引擎接口地址枚举匹配的辅助字段. cpuArch 是运行进程 ABI. appSignatureHash 为 APK 单签名证书 DER 的 SHA-256 小写十六进制, 多签名时缺失. bootTime 是 Android 开机以来时长毫秒, 磁盘容量为应用内部文件所在卷字节数.

iOS 不读取用于登录上传的开机时间、磁盘容量或首次安装日期. OAID 与 iOS App Attest 未纳入本轮. IDFA getter 只检查当前 ATT 授权, 不发起弹窗. Android GAID 尊重限制状态, 无服务或已删除时缺失/全零, 调用者按协议默认值处理.

Android Manifest 自带 INTERNET, ACCESS_NETWORK_STATE, AD_ID. iOS PrivacyInfo.xcprivacy 声明功能用途的 DeviceID/OtherDiagnosticData 与身份关联. 宿主如通过 MAX 请求 ATT, 由对应流程提供用途描述并核查最终包. 扩展只返回平台数据, 使用方负责实际上传用途和最终合并声明.

## 可见性边界

VPN 字段是网络/隧道信号，不是防作弊证明；iOS 的 utun/ppp/ipsec 等活动接口可能由其他系统服务产生。Android Settings.Global 的 ADB_ENABLED 与 DEVELOPMENT_SETTINGS_ENABLED 在受限制系统上可固定返回 0，因此 false 只代表 API 未观察到开启，不能证明设备关闭了调试。iOS 不提供这两项公开对等 API，返回 nil，不用私有 API 冒充检测能力。

## 参考资料

- [Google Play Integrity 设置](https://developer.android.com/google/play/integrity/setup)
- [Standard API 与 requestHash](https://developer.android.com/google/play/integrity/standard)
- [Settings.Global 可见性](https://developer.android.com/reference/android/provider/Settings.Global)
- [Defold native extensions](https://defold.com/manuals/extensions/)

## 验证范围

2026-11-09 使用 Defold/Bob 1.14.0, engine SHA a81dbb0478e8b73931dc2d671563204f6ac0ef82 完成 SDK 独立 Web 构建及 Android arm64 Debug 编译/权限检查. iOS 构建服务不支持 arm64-ios, 本轮未验证 iOS 编译. 根目录示例和目标真机信号、授权、Integrity/修复对话框仍需相应环境验证.
