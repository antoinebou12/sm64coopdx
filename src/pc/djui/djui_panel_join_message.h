#pragma once
#include "djui.h"

extern bool gDjuiPanelJoinMessageVisible;
extern float gDownloadProgress;
extern float gDownloadProgressInf;
#define DOWNLOAD_ESTIMATE_LENGTH 32
extern char gDownloadEstimate[];

void djui_panel_join_message_error(const char* message);
void djui_panel_join_message_return_to_lobbies(const char* message);
void djui_panel_join_message_ready_to_join(void);
void djui_panel_join_message_create(struct DjuiBase* caller);
