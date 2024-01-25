#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "privateConfig.h"
#include "GatewayIntents.h"

// Intent options can be found in GatewayIntents.h
const uint16_t gateway_intents =  GUILD_MESSAGES_INTENT | GUILD_MESSAGE_TYPING_INTENT | GUILD_VOICE_STATES_INTENT | GUILD_MESSAGE_REACTIONS_INTENT | GUILD_MEMBERS_INTENT;

const char* websockets_connection_string = "wss://gateway.discord.gg/"; //Enter server adress
#define USING_INSECURE_MODE     true

#endif //CONFIG_H
