#pragma once

#include "main.h"

// --- Commands (api_commands.cpp) ---
command_que_item parseCommandFromJson(const JsonVariantConst &src);
void handleGetPollData();
void handleSendCommand();
void handleGetCommandQueue();
void handleAddCommand();
void handleEditCommand();
void handleDelCommand();
void handle_cmdq_file();

// --- Config (api_config.cpp) ---
void handleGetConfig();
void handleSetConfig();

// --- Hardware (api_hardware.cpp) ---
void handleGetHardware();
void handleSetHardware();

// --- Smart schedule (api_schedule.cpp) ---
void handleGetSmartSchedule();
void handleSetSmartSchedule();
void handleUpdateSmartSchedule();
void handleCancelSmartSchedule();

// --- System (api_system.cpp) ---
void handleRestart();
void handleUpdate();
time_t getBootTime();
