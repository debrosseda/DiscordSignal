# DiscordSignal

This project uses an ESP32 to display the discord server activity of a fixed set of users via addressable RGB leds (WS2812B).

Implements a basic Discord WebSocket client as documented at https://discord.com/developers/docs/topics/gateway.

Based on the ESP Discord WebSocket Client by Cimera42: https://github.com/Cimera42/esp-discord-client

## Installation

1. Install ESP board for Arduino as outlined on the [official documentation](https://arduino-esp8266.readthedocs.io/en/latest/installing.html)

2. Copy `privateConfig.template.h` and rename to `privateConfig.h`.

3. Set configuration in `config.h` and `privateConfig.h`.

    |Name|Value|Location|
    |-|-|-|
    |`bot_token`|Token to authenticate websocket connection<br/>Discord bots can be created and tokens generated at the [Developer Documentation](https://discord.com/developers/applications).|`privateConfig.h`|
    |`gateway_intents`|Data to be sent over websocket connection<br/>Options can be found in [`GatewayIntents.h`](./GatewayIntents.h)<br/>  - Bitwise OR (`\|`) for multiple.<br/>More info can be found at: https://discord.com/developers/docs/topics/gateway#gateway-intents|`config.h`|
    |`known_IDs`|Array of users who's activity you want to display|`privateConfig.h`|
    |`userHues`|Array of uint8_t numbers that match a user to a specific hue according the the FastLED default HSV model|`privateConfig.h`|
    |`portalSSID`|SSID to use when provisioning Wifi|`privateConfig.h`|

4. Build and upload to ESP32 board

- If there are any problems, feel free to create an issue on GitHub.

5. Add the discord bot account to the server, you may need admin permissions for this step.

6. Wire the LED strip to power, data and ground, and define the data pin in the sketch.

7. Use a smartphone or other device to connect to the ESP32 AP name chosen with 'portalSSID' and select the wifi network you want to use and enter the wifi password.

## Activity displayed:
-Messages, Typing, Reactions: A moving dot of the user's assigned hue circles in a random speed and direction until timed out. Speed and direction change with new activity

-Mentions: the users hue appears in three pulses of sparkling color

-Owner Mentions: One account is tracked and any mentions cause a permanent white pulse patter on all LEDs that normal circling activity displays over. A reaction or message by the owner resets the notification.

-Special Reactions: select reaction emojis display an effect over all LEDs before normal activity resumes.

-Voice Status: Entering a voice channel causes a permanently active circling dot, until the user leaves the voice channel.


## Potential Issues

Here's some common problems which may be preventing the client from working properly.

### Large servers

Due to the limited memory of the ESP32, some websocket messages may be too large and crash the program. For this reason, it is not recommended to use the `GUILDS_INTENT` intent, as it includes a message of the full state of the bot's guilds when connecting the websocket client.


## Used Libraries:
### [FastLED](https://github.com/FastLED/FastLED)
Supports multiple types of addressable RGB LED strips and panels

### [WebSockets2_Generic](https://https://github.com/khoih-prog/WebSockets2_Generic)
Connects to the Discord Gateway API

### [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
Used to decode JSON payloads from Discord

### [WifiManager](https://github.com/tzapu/WiFiManager)
Allows users to select a Wifi network and enter a password from a mobile device with the ESP32 acting as a Wifi AP

## Other Resources

Based on https://github.com/Cimera42/DiscordBot
