<!-- SPDX-License-Identifier: MPL-2.0 -->

# OSKey 产品展示

从一块开发板开始，把钱包、身份认证、连接与交互组合成属于自己的硬件。这里用完整的设备画面，带你走过 OSKey 的每一项能力。

![OSKey 钱包、签名、身份认证、安全芯片与外设总览](images/overview.png)

## 一览

| 模块 | 功能 |
| --- | --- |
| [从开机到模块总览](#从开机到模块总览) | 设备能力总览、钱包与通行密钥首页，以及按需组合的连接和外设模块。 |
| [创建、恢复与解锁钱包](#创建恢复与解锁钱包) | 钱包创建与恢复，12 / 18 / 24 词助记词、备份验证、口令保护与 PIN 解锁。 |
| [让随机性来自你的世界](#让随机性来自你的世界) | 硬件随机数、触摸、运动、摄像头和环境声音多源熵混合，支持自定义熵。 |
| [地址派生、消息与交易签名](#地址派生消息与交易签名) | HD 地址派生与二维码、以太坊消息和交易签名，以及设备端交易审阅与授权。 |
| [用二维码完成气隙签名](#用二维码完成气隙签名) | 通过二维码传递交易请求与签名结果，在设备上完成扫描、审阅、确认与签名。 |
| [以 Google 为例的 FIDO2 身份认证](#以-google-为例的-fido2-身份认证) | FIDO2 通行密钥注册与登录、用户在场确认、权限授权和 PIN 管理；以 www.google.com 为例。 |
| [NXP A5000 安全芯片管理](#nxp-a5000-安全芯片管理) | A5000R2HQ1 seed 安全存储、PIN 解锁、重试次数保护、锁定与钱包清除。 |
| [连接你的设备与应用](#连接你的设备与应用) | Wi-Fi 扫描与配网、蓝牙加密连接与地址隐私、WebUSB / FIDO2，以及 MQTT 消息通信。 |
| [摄像头与二维码](#摄像头与二维码) | 摄像头预览、二维码识别，以及地址与交互请求读取。 |
| [声音也是一种交互](#声音也是一种交互) | 提示音播放、扬声器音量调节与麦克风输入电平。 |
| [把设备运动变成可见姿态](#把设备运动变成可见姿态) | 六轴运动采集、姿态融合与立体模型实时显示。 |
| [浏览随身文件](#浏览随身文件) | SD 卡目录浏览、文件信息查看与目录切换。 |
| [签名固件更新](#签名固件更新) | 蓝牙 / UART 固件传输、MCUboot 签名校验、版本保护与更新重启。 |
| [设备维护](#设备维护) | 设备重启、数据清除、存储恢复与敏感操作确认。 |

## 从开机到模块总览

开机即可查看设备能力，进入钱包与通行密钥首页，再按使用习惯选择连接和外设模块。

| 启动画面 | 设备能力总览 | 钱包与通行密钥首页 |
| --- | --- | --- |
| <a href="images/01-startup.png"><img src="images/01-startup.png" alt="启动画面" width="240"></a> | <a href="images/02-capabilities.png"><img src="images/02-capabilities.png" alt="设备能力总览" width="240"></a> | <a href="images/23-wallet-home.png"><img src="images/23-wallet-home.png" alt="钱包与通行密钥首页" width="240"></a> |

| 模块与设备设置 | 自由选择与组合模块 |
| --- | --- |
| <a href="images/48-device-settings.png"><img src="images/48-device-settings.png" alt="模块与设备设置" width="240"></a> | <a href="images/80-module-composition.png"><img src="images/80-module-composition.png" alt="自由选择与组合模块" width="240"></a> |


## 创建、恢复与解锁钱包

从 PIN 创建到助记词备份，完整呈现钱包初始化、12 / 18 / 24 词选择、短语验证、导入、口令与触摸解锁的操作流程。

| 创建钱包 PIN | PIN 输入与遮蔽 | 确认钱包 PIN |
| --- | --- | --- |
| <a href="images/03-create-pin.png"><img src="images/03-create-pin.png" alt="创建钱包 PIN" width="240"></a> | <a href="images/04-pin-entered.png"><img src="images/04-pin-entered.png" alt="PIN 输入与遮蔽" width="240"></a> | <a href="images/05-confirm-pin.png"><img src="images/05-confirm-pin.png" alt="确认钱包 PIN" width="240"></a> |

| 创建或恢复钱包 | 选择助记词长度 | 12 词助记词 |
| --- | --- | --- |
| <a href="images/06-recovery-source.png"><img src="images/06-recovery-source.png" alt="创建或恢复钱包" width="240"></a> | <a href="images/07-mnemonic-length.png"><img src="images/07-mnemonic-length.png" alt="选择助记词长度" width="240"></a> | <a href="images/16-mnemonic-12.png"><img src="images/16-mnemonic-12.png" alt="12 词助记词" width="240"></a> |

| 18 词助记词 | 24 词助记词 | 核对恢复短语 |
| --- | --- | --- |
| <a href="images/17-mnemonic-18.png"><img src="images/17-mnemonic-18.png" alt="18 词助记词" width="240"></a> | <a href="images/18-mnemonic-24.png"><img src="images/18-mnemonic-24.png" alt="24 词助记词" width="240"></a> | <a href="images/19-verify-mnemonic.png"><img src="images/19-verify-mnemonic.png" alt="核对恢复短语" width="240"></a> |

| 导入已有钱包 | 设置助记词口令 | 确认助记词口令 |
| --- | --- | --- |
| <a href="images/20-import-wallet.png"><img src="images/20-import-wallet.png" alt="导入已有钱包" width="240"></a> | <a href="images/21-passphrase.png"><img src="images/21-passphrase.png" alt="设置助记词口令" width="240"></a> | <a href="images/22-confirm-passphrase.png"><img src="images/22-confirm-passphrase.png" alt="确认助记词口令" width="240"></a> |

| PIN 解锁钱包 | 触摸键盘输入 | 软件钱包存储配置 |
| --- | --- | --- |
| <a href="images/24-unlock-wallet.png"><img src="images/24-unlock-wallet.png" alt="PIN 解锁钱包" width="240"></a> | <a href="images/25-unlock-keyboard.png"><img src="images/25-unlock-keyboard.png" alt="触摸键盘输入" width="240"></a> | <a href="images/81-software-wallet.png"><img src="images/81-software-wallet.png" alt="软件钱包存储配置" width="240"></a> |


## 让随机性来自你的世界

硬件随机数与触摸轨迹、设备运动、摄像头画面和环境声音一起参与熵混合；也可以逐位设置自定义熵。

| 选择熵生成方式 | 组合四种辅助熵源 | 触摸轨迹采集 |
| --- | --- | --- |
| <a href="images/08-entropy-method.png"><img src="images/08-entropy-method.png" alt="选择熵生成方式" width="240"></a> | <a href="images/09-entropy-sources.png"><img src="images/09-entropy-sources.png" alt="组合四种辅助熵源" width="240"></a> | <a href="images/10-entropy-touch.png"><img src="images/10-entropy-touch.png" alt="触摸轨迹采集" width="240"></a> |

| 设备运动采集 | 摄像头画面采集 | 环境声音采集 |
| --- | --- | --- |
| <a href="images/11-entropy-motion.png"><img src="images/11-entropy-motion.png" alt="设备运动采集" width="240"></a> | <a href="images/12-entropy-camera.png"><img src="images/12-entropy-camera.png" alt="摄像头画面采集" width="240"></a> | <a href="images/13-entropy-microphone.png"><img src="images/13-entropy-microphone.png" alt="环境声音采集" width="240"></a> |

| 多源熵混合 | 逐位自定义熵 | 自定义熵与助记词生成入口 |
| --- | --- | --- |
| <a href="images/14-entropy-ready.png"><img src="images/14-entropy-ready.png" alt="多源熵混合" width="240"></a> | <a href="images/15-custom-entropy.png"><img src="images/15-custom-entropy.png" alt="逐位自定义熵" width="240"></a> | <a href="images/84-custom-entropy-generate.png"><img src="images/84-custom-entropy-generate.png" alt="自定义熵与助记词生成入口" width="240"></a> |


## 地址派生、消息与交易签名

查看 HD 派生路径和地址二维码，在设备上审阅以太坊消息、收款地址、金额与网络。签名前确认私钥访问，随后查看公钥、哈希和签名结果。

| HD 派生路径与地址二维码 | 设备端私钥授权 | EIP-191 消息签名确认 |
| --- | --- | --- |
| <a href="images/26-hd-account.png"><img src="images/26-hd-account.png" alt="HD 派生路径与地址二维码" width="240"></a> | <a href="images/27-private-key-confirmation.png"><img src="images/27-private-key-confirmation.png" alt="设备端私钥授权" width="240"></a> | <a href="images/28-ethereum-message.png"><img src="images/28-ethereum-message.png" alt="EIP-191 消息签名确认" width="240"></a> |

| 消息哈希与派生路径 | 消息签名结果 | 公钥与签名详情 |
| --- | --- | --- |
| <a href="images/29-message-details.png"><img src="images/29-message-details.png" alt="消息哈希与派生路径" width="240"></a> | <a href="images/30-message-signature.png"><img src="images/30-message-signature.png" alt="消息签名结果" width="240"></a> | <a href="images/31-message-signature-details.png"><img src="images/31-message-signature-details.png" alt="公钥与签名详情" width="240"></a> |

| EIP-2930 交易签名确认 | Nonce、Gas 与交易哈希 | 交易签名结果 |
| --- | --- | --- |
| <a href="images/32-ethereum-transaction.png"><img src="images/32-ethereum-transaction.png" alt="EIP-2930 交易签名确认" width="240"></a> | <a href="images/33-transaction-details.png"><img src="images/33-transaction-details.png" alt="Nonce、Gas 与交易哈希" width="240"></a> | <a href="images/34-transaction-signature.png"><img src="images/34-transaction-signature.png" alt="交易签名结果" width="240"></a> |

| 合约创建确认 | 合约调用与输入数据确认 | 合约方法与输入哈希 |
| --- | --- | --- |
| <a href="images/35-contract-creation.png"><img src="images/35-contract-creation.png" alt="合约创建确认" width="240"></a> | <a href="images/82-contract-call.png"><img src="images/82-contract-call.png" alt="合约调用与输入数据确认" width="240"></a> | <a href="images/83-contract-call-details.png"><img src="images/83-contract-call-details.png" alt="合约方法与输入哈希" width="240"></a> |


## 用二维码完成气隙签名

钱包应用展示请求二维码，OSKey 扫描并在屏幕上确认交易，签名结果再通过二维码回到钱包应用。三个画面串起完整的交互过程。

| 扫描气隙签名请求 | 设备端确认气隙交易 | 二维码返回签名结果 |
| --- | --- | --- |
| <a href="images/36-airgap-request.png"><img src="images/36-airgap-request.png" alt="扫描气隙签名请求" width="240"></a> | <a href="images/37-airgap-review.png"><img src="images/37-airgap-review.png" alt="设备端确认气隙交易" width="240"></a> | <a href="images/38-airgap-signature.png"><img src="images/38-airgap-signature.png" alt="二维码返回签名结果" width="240"></a> |


## 以 Google 为例的 FIDO2 身份认证

以 www.google.com 为服务、demo@example.com 为账号，展示通行密钥注册、登录签名、用户在场确认、权限授权与 PIN 管理。每一次私钥操作都在设备屏幕上确认。

| Google 通行密钥私钥授权 | Google 通行密钥注册 | 返回通行密钥注册结果 |
| --- | --- | --- |
| <a href="images/39-google-passkey-permission.png"><img src="images/39-google-passkey-permission.png" alt="Google 通行密钥私钥授权" width="240"></a> | <a href="images/40-google-create-passkey.png"><img src="images/40-google-create-passkey.png" alt="Google 通行密钥注册" width="240"></a> | <a href="images/41-google-passkey-result.png"><img src="images/41-google-passkey-result.png" alt="返回通行密钥注册结果" width="240"></a> |

| Google 通行密钥登录确认 | Google FIDO 签名结果 | 通行密钥公钥与签名 |
| --- | --- | --- |
| <a href="images/42-google-authenticate.png"><img src="images/42-google-authenticate.png" alt="Google 通行密钥登录确认" width="240"></a> | <a href="images/43-google-signature.png"><img src="images/43-google-signature.png" alt="Google FIDO 签名结果" width="240"></a> | <a href="images/44-google-signature-details.png"><img src="images/44-google-signature-details.png" alt="通行密钥公钥与签名" width="240"></a> |

| 确认用户在场 | 授权通行密钥访问 | 恢复 FIDO PIN 尝试次数 |
| --- | --- | --- |
| <a href="images/45-google-presence.png"><img src="images/45-google-presence.png" alt="确认用户在场" width="240"></a> | <a href="images/46-google-authorize.png"><img src="images/46-google-authorize.png" alt="授权通行密钥访问" width="240"></a> | <a href="images/47-fido-pin-recovery.png"><img src="images/47-fido-pin-recovery.png" alt="恢复 FIDO PIN 尝试次数" width="240"></a> |

| FIDO PIN 次数保护 | Google 通行密钥凭据详情 |
| --- | --- |
| <a href="images/61-fido-pin-protection.png"><img src="images/61-fido-pin-protection.png" alt="FIDO PIN 次数保护" width="240"></a> | <a href="images/85-google-credential-details.png"><img src="images/85-google-credential-details.png" alt="Google 通行密钥凭据详情" width="240"></a> |


## NXP A5000 安全芯片管理

A5000R2HQ1 为 seed 提供安全存储。专属页面呈现初始化、钱包锁定与 PIN 解锁、次数保护、连接刷新和钱包清除，让安全芯片的状态与操作一目了然。

| A5000 初始化 | 安全芯片钱包解锁状态 | 安全芯片 PIN 解锁入口 |
| --- | --- | --- |
| <a href="images/49-nxp-initialize.png"><img src="images/49-nxp-initialize.png" alt="A5000 初始化" width="240"></a> | <a href="images/50-nxp-unlocked.png"><img src="images/50-nxp-unlocked.png" alt="安全芯片钱包解锁状态" width="240"></a> | <a href="images/51-nxp-locked.png"><img src="images/51-nxp-locked.png" alt="安全芯片 PIN 解锁入口" width="240"></a> |

| PIN 次数保护与恢复入口 | 清除钱包并重新初始化 |
| --- | --- |
| <a href="images/52-nxp-pin-protection.png"><img src="images/52-nxp-pin-protection.png" alt="PIN 次数保护与恢复入口" width="240"></a> | <a href="images/53-nxp-erase.png"><img src="images/53-nxp-erase.png" alt="清除钱包并重新初始化" width="240"></a> |


## 连接你的设备与应用

Wi-Fi 提供网络扫描、密码输入、热点配网与地址信息；蓝牙提供发现、加密连接和地址隐私；USB 汇集 WebUSB 与 FIDO2；MQTT 将设备状态与消息带入应用。

| Wi-Fi 连接与地址信息 | 附近网络与安全类型 | Wi-Fi 密码配置 |
| --- | --- | --- |
| <a href="images/54-wifi-connected.png"><img src="images/54-wifi-connected.png" alt="Wi-Fi 连接与地址信息" width="240"></a> | <a href="images/55-wifi-networks.png"><img src="images/55-wifi-networks.png" alt="附近网络与安全类型" width="240"></a> | <a href="images/56-wifi-password.png"><img src="images/56-wifi-password.png" alt="Wi-Fi 密码配置" width="240"></a> |

| 接入热点与配网入口 | 蓝牙发现与地址隐私 | 加密蓝牙连接 |
| --- | --- | --- |
| <a href="images/57-wifi-access-point.png"><img src="images/57-wifi-access-point.png" alt="接入热点与配网入口" width="240"></a> | <a href="images/58-bluetooth-discovery.png"><img src="images/58-bluetooth-discovery.png" alt="蓝牙发现与地址隐私" width="240"></a> | <a href="images/59-bluetooth-connected.png"><img src="images/59-bluetooth-connected.png" alt="加密蓝牙连接" width="240"></a> |

| USB、WebUSB 与 FIDO2 | MQTT 消息通信 |
| --- | --- |
| <a href="images/60-usb-interfaces.png"><img src="images/60-usb-interfaces.png" alt="USB、WebUSB 与 FIDO2" width="240"></a> | <a href="images/72-mqtt.png"><img src="images/72-mqtt.png" alt="MQTT 消息通信" width="240"></a> |


## 摄像头与二维码

从摄像头状态进入扫码预览，识别地址或交互请求，直接在屏幕上查看二维码内容和识别耗时。

| 摄像头管理 | 二维码实时预览 | 二维码识别结果 |
| --- | --- | --- |
| <a href="images/67-camera.png"><img src="images/67-camera.png" alt="摄像头管理" width="240"></a> | <a href="images/68-qr-scanner.png"><img src="images/68-qr-scanner.png" alt="二维码实时预览" width="240"></a> | <a href="images/69-qr-decoded.png"><img src="images/69-qr-decoded.png" alt="二维码识别结果" width="240"></a> |


## 声音也是一种交互

播放提示音、调节扬声器音量，查看麦克风输入电平，让听觉反馈和环境声音成为设备的一部分。

| 扬声器与音量调节 | 音频播放状态 | 麦克风与输入电平 |
| --- | --- | --- |
| <a href="images/62-audio-volume.png"><img src="images/62-audio-volume.png" alt="扬声器与音量调节" width="240"></a> | <a href="images/63-audio-playback.png"><img src="images/63-audio-playback.png" alt="音频播放状态" width="240"></a> | <a href="images/64-microphone.png"><img src="images/64-microphone.png" alt="麦克风与输入电平" width="240"></a> |


## 把设备运动变成可见姿态

六轴加速度与陀螺仪数据经过姿态融合，在屏幕上变成随设备倾斜转动的立体模型。

| 六轴姿态融合 | 设备倾斜可视化 |
| --- | --- |
| <a href="images/65-imu-orientation.png"><img src="images/65-imu-orientation.png" alt="六轴姿态融合" width="240"></a> | <a href="images/66-imu-tilt.png"><img src="images/66-imu-tilt.png" alt="设备倾斜可视化" width="240"></a> |


## 浏览随身文件

通过 SD 卡文件浏览器查看目录、文件名与文件大小，在根目录和子目录之间自由切换。

| SD 卡根目录与文件大小 | 文件夹浏览与返回 |
| --- | --- |
| <a href="images/70-sd-files.png"><img src="images/70-sd-files.png" alt="SD 卡根目录与文件大小" width="240"></a> | <a href="images/71-sd-directory.png"><img src="images/71-sd-directory.png" alt="文件夹浏览与返回" width="240"></a> |


## 签名固件更新

通过蓝牙或 UART 传输固件，展示 MCUboot 签名校验、版本保护、更新进度与重启应用的流程。

| 签名固件与更新通道 | 固件传输进度 | 固件校验与重启应用 |
| --- | --- | --- |
| <a href="images/73-firmware-update.png"><img src="images/73-firmware-update.png" alt="签名固件与更新通道" width="240"></a> | <a href="images/74-update-progress.png"><img src="images/74-update-progress.png" alt="固件传输进度" width="240"></a> | <a href="images/75-update-ready.png"><img src="images/75-update-ready.png" alt="固件校验与重启应用" width="240"></a> |


## 设备维护

重启、清除设备数据和存储恢复操作集中呈现，敏感操作在设备上再次确认。

| 设备维护入口 | 重启确认 | 设备数据清除确认 |
| --- | --- | --- |
| <a href="images/76-device-maintenance.png"><img src="images/76-device-maintenance.png" alt="设备维护入口" width="240"></a> | <a href="images/77-restart.png"><img src="images/77-restart.png" alt="重启确认" width="240"></a> | <a href="images/78-reset.png"><img src="images/78-reset.png" alt="设备数据清除确认" width="240"></a> |

| 存储维护与恢复操作 |
| --- |
| <a href="images/79-storage-recovery.png"><img src="images/79-storage-recovery.png" alt="存储维护与恢复操作" width="240"></a> |

## 自由组合，定义自己的设备

每一项能力都是可以按需选择的模块。你可以做一枚随身携带的签名钥匙，也可以组合出带屏幕、摄像头、声音和运动感知的桌面伙伴。从功能的取舍，到 PCB 的布局，再到外壳握在掌心的感觉，设备最终的模样，由你决定。

## 演示资料

查看 [完整截图清单](manifest.json)。
