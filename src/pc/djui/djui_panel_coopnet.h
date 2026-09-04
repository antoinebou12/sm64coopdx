#pragma once

#include "djui.h"

#if defined(__3DS__) && defined(COOPNET)
void djui_panel_coopnet_create(struct DjuiBase* caller);
#endif
