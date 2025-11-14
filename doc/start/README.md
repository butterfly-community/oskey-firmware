| WeChat                          | Telegram                    |
| ------------------------------- | --------------------------- |
| <img src="../image/start/wechat.jpg" alt="wechat" width="150" style="max-width:100%; height:auto;"> | <img src="../image/start/tg.png" alt="wechat" width="200" style="max-width:100%; height:auto;">

# How to Use

This document assumes that you have completed the development tutorial on the [homepage](https://github.com/butterfly-community/oskey-firmware) or have already flashed the latest firmware to your development board according to the [Firmware Quick Flash Guide](https://github.com/butterfly-community/oskey-firmware/tree/master/doc/board).

First, click to open the OSKey official website [https://www.oskey.xyz/settings](https://www.oskey.xyz/settings).

### Demo Video

Here is a demo video, which is the video version of the text and images below.

[![Open Hardware Wallet - Task 3](https://res.cloudinary.com/marcomontalbano/image/upload/v1736601213/video_to_markdown/images/youtube--Tk8S3mavd5I-c05b58ac6eb4c4700831b2b3070cd403.jpg)](https://www.youtube.com/watch?v=Tk8S3mavd5I "Open Hardware Wallet - Task 3")

## Check Version

Click the Connect button in the upper left corner, which should change from gray **Connect** to green **Connected**. If it doesn't change, there may be other tabs or apps that have opened the connection, please confirm and close them.

The OSKey Status bar should display the firmware version number in green, for example **OK Version: 0.3.0**. If it shows "Not Found OSKey firmware", please confirm that the firmware has been flashed or disconnect and try selecting another interface.

If you have completed the [Firmware Quick Flash Guide](https://github.com/butterfly-community/oskey-firmware/tree/master/doc/board) and still see this prompt, 🔴⚠️ please **re-plug** the hardware wallet or press the **RST** button on the hardware wallet to restart the application wallet firmware as described in the guide ⚠️🔴.

<img src="../image/start/start-7.png" alt="wechat" width="400" style="max-width:100%; height:auto;"> 

## Mnemonic Phrase

OSKey supports importing or generating mnemonic phrases.

<img src="../image/start/start-8.png" alt="wechat" width="400" style="max-width:100%; height:auto;"> 

### Generate Mnemonic Phrase

Click Generate to generate a 24-word mnemonic phrase by default. Please write it down on paper. Do not copy, screenshot, or take photos.

<br/>

### Import Mnemonic Phrase

If you already have a mnemonic phrase, you can choose the import function. Enter the mnemonic phrase separated by spaces and click import. Please note that the current version only supports **English** mnemonic phrases.

<br/>

### Note

The generated mnemonic phrase will only be displayed once, please write it down carefully. Refreshing the page will make it disappear **permanently** and cannot be recovered.

<br/>

## Generate Address

By default, it generates from the first address of the Ethereum path, i.e., **m/44'/60'/0'/0/0**. You can choose other paths. Click **Get Address** to get the address.

<img src="../image/start/start-9.png" alt="wechat" width="400" style="max-width:100%; height:auto;"> 

## Signature

Here you can perform EIP-191 signature on the input text. Click **Sign Message** to display the signature result from the private key of the corresponding address.

<img src="../image/start/start-10.png" alt="wechat" width="400" style="max-width:100%; height:auto;">

## Direct Connection

Using OpenBuild as an example, this explains how to use Web3 login and interact with Apps. [Click here](https://openbuild.xyz/) to open OpenBuild.

### Establish Connection

Click the upper right corner of the OpenBuild website to enter the login page, then click **Wallet** to log in. In the popup window, select **WalletConnect**. At this time, you can scan the QR code content to copy it or click the **OPEN** button below. Click the **marked area** in the image below to copy the connection.

<img src="../image/start/start-11.png" alt="wechat" width="400" style="max-width:100%; height:auto;">

<br/>

Next, add the obtained link to the **WalletConnect** section of the OSKey test page and click **Add**.

<br/>

<img src="../image/start/start-12.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

At this time, a connection confirmation prompt will appear. Click confirm to establish a connection with the OpenBuild website.

<img src="../image/start/start-13.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

### Interaction

Next, OpenBuild requests to **sign** a random number to verify ownership of the current address.

<img src="../image/start/start-14.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

After confirmation, wait 5-10 seconds to log into OpenBuild. At this point, you are connected to OpenBuild and can continue operating.

<img src="../image/start/start-15.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

## Client Connection

OSKey's client is still under development. For now, you can choose to use OneKey. OneKey is a well-known wallet manufacturer, and OSKey can integrate with OneKey to provide a complete experience. Thanks to OneKey's openness and provision of an easy-to-use wallet client. [Click to open OneKey official website](https://onekey.so).

Alternatively, you can choose a Trezor client and select to connect using walletconnect.  [Click to open Trezor official website](https://trezor.io/trezor-suite)

The following example shows a client using OneKey.

### Establish Connection

It has clients for all platforms. Here we use the browser extension wallet as an example to explain how to integrate and conduct transactions. After installing the wallet extension in your browser, click the extension, open this page, and select **Connect Wallet**.

<img src="../image/start/start-16.png" alt="wechat" width="400" style="max-width:100%; height:auto;">

<br/>

Click **EVM** -> **WalletConnect** in sequence, then click here or scan the QR code to get the link.

<img src="../image/start/start-17.png" alt="wechat" width="400" style="max-width:100%; height:auto;">

<br/>

Return to the OSKey settings page to **add** the link and **confirm** the connection.

<img src="../image/start/start-12.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

<img src="../image/start/start-19.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

At this point, the connection has been established, and the browser extension should **display the list of held assets**.

<img src="../image/start/start-20.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

### Initiate Transaction

Taking a transfer on Ethereum Sepolia Testnet as an example, click **Confirm**.

<img src="../image/start/start-21.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

The OSKey settings page receives a signature request, **confirm** it.

<img src="../image/start/start-22.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

Return to the extension wallet, the transaction is successful.

<img src="../image/start/start-23.png" alt="wechat" width="600" style="max-width:100%; height:auto;">

<br/>

You can also choose to use various Apps through OneKey, which will provide a better experience than direct connection.

## Special Notice

The WalletConnect service used for direct connection or client connection is unstable within mainland China. If disconnection occurs, please reconnect.

**OSKey and OneKey have no partnership relationship**.

