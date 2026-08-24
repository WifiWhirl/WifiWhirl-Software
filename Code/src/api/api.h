#pragma once

#include "main.h"

// --- Commands (api_commands.cpp) ---
bool parseCommandValidated(const JsonVariantConst &src, command_que_item &item, String &error);
void handleGetPollData();
void handleSendCommand();
void handleGetCommandQueue();
void handleAddCommand();
void handleEditCommand();
void handleDelCommand();
void handleSetCommandQueue();
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

// --- Expert / heat-loss calibration (api_expert.cpp) ---
void handleGetHlCal();
void handleStartHlCal();
void handleCancelHlCal();
void handleSetHeatLoss();

// --- System (api_system.cpp) ---
void handleRestart();
void handleUpdate();
time_t getBootTime();
