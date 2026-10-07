# Device Capabilities for Defold

面向 Defold 的独立系统能力扩展，最低 Defold 1.14.0。导出目录为 `device_capabilities`。

- Android：前台精确定位、系统可观察的 VPN/代理与开发者设置、Play Integrity Standard API。
- iOS：Core Location 前台精确定位、系统代理与活动隧道接口信号。
- Windows/Web：提供同名 API，设备检测返回 unsupported，定位和 Integrity 返回 unavailable。

依赖 Android Google Play Integrity 1.6.0（Google Maven）；iOS 使用系统 CoreLocation、UIKit、CFNetwork。无后台定位权限，无持续位置存档，无 token/nonce/坐标日志。

## 接入

本地开发将仓库的 `device_capabilities/` 目录链接到游戏根目录同名位置。作为 Defold 库接入时，在游戏工程中添加本仓库的固定版本 ZIP 依赖。

`game.project` 的 `[device_capabilities] location_usage_description` 设置 iOS 定位用途文字，接入时须填写应用的实际定位用途。最终 Info.plist 须保留非空 `NSLocationWhenInUseUsageDescription`。定位仅在调用时申请，不在初始化时弹权限框，不申请 Always 权限。iOS 14+ 近似位置返回 permission_denied；用户可在系统设置中开启精确位置。

仓库根工程可直接用对应版本 Bob 构建 `example/main.collection`。示例仅验证接口存在和打印能力状态，不申请位置权限、不请求 Integrity。Android/iOS 的真实授权、系统暂停与 Play 配置必须在目标设备验证。

## Lua API

原生全局表 `device_capabilities`：

| API | 结果 |
| --- | --- |
| `device_info()` | `{status="observed"/"unsupported", vpn=?, proxy=?, usb_debugging=?, developer_mode=?}`，可观察字段为布尔，不可读取字段为 nil |
| `location_start(id)` | 受理返回 true，不可用返回 false/nil；正整数 id 标识请求 |
| `location_poll(id)` | permission / locating / success / permission_denied / disabled / failed / unavailable / timeout；success 另返回 latitude, longitude |
| `location_cancel(id)` | 取消并丢弃坐标；id=0 取消当前请求 |
| `integrity_start(id, cloud_project_number, request_data)` | Android 受理返回 true；project number 为正十进制字符串，request_data 是 1..4096 字节 UTF-8 文本 |
| `integrity_poll(id)` | pending / success / failed / unavailable / dialog / dialog_complete，另返回 token 字符串与错误/对话框结果码字符串 |
| `integrity_show_dialog(id, type)` | 使用当前成功 token 展示 Google 修复对话框；通过 poll 等待完成 |
| `integrity_cancel(id)` | 使异步结果失效并释放 token；id=0 取消当前请求 |

定位和 Integrity 各只有一个活动请求槽；新 start 替换前一个，调用方须使用不同请求 id、管理所有权及超时，final 时取消。旧 id 不会读到新请求结果。Google 请求不能真正中断，取消后结果被丢弃。Google provider 按 project number 复用，失效/修复后丢弃。定位成功、取消及扩展 final 释放系统订阅；iOS 后台停止定位，回前台检查权限再继续。权限选择不设置倒计时；消费方应限制已授权后的定位等待。

Integrity 在原生侧计算 `SHA256(UTF8(request_data))`，转 URL-safe、无 padding/no-wrap Base64 作为 Standard API `requestHash`。token 原样交给调用方，由业务服务器验证；扩展不会自行判定设备可信、读取服务端凭据或访问业务接口。调用方应在超时及业务完成后取消。`request_data` 的内容由接入方与其服务端约定。

## 可见性边界

VPN 字段是网络/隧道信号，不是防作弊证明；iOS 的 utun/ppp/ipsec 等活动接口可能由其他系统服务产生。Android Settings.Global 的 ADB_ENABLED 与 DEVELOPMENT_SETTINGS_ENABLED 在受限制系统上可固定返回 0，因此 false 只代表 API 未观察到开启，不能证明设备关闭了调试。iOS 不提供这两项公开对等 API，返回 nil，不用私有 API 冒充检测能力。

## 参考资料

- [Google Play Integrity 设置](https://developer.android.com/google/play/integrity/setup)
- [Standard API 与 requestHash](https://developer.android.com/google/play/integrity/standard)
- [Settings.Global 可见性](https://developer.android.com/reference/android/provider/Settings.Global)
- [Apple Core Location](https://developer.apple.com/documentation/corelocation/cllocationmanager)
- [Defold native extensions](https://defold.com/manuals/extensions/)

## 验证范围

根目录示例尚未单独运行，iOS 原生编译尚未验证。定位权限、真实坐标、后台恢复、设备信号及 Play Integrity/修复对话框仍待目标真机与服务端验收。
