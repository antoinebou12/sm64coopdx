#pragma once
#include "djui.h"

struct DjuiFlowLayout {
    struct DjuiBase base;
    enum DjuiFlowDirection flowDirection;
    struct DjuiScreenValue margin;
    /* Optional vertical scroll (used on New 3DS when body content overflows). */
    bool scrollEnabled;
    f32 scrollY;
    f32 contentHeight;
    f32 viewHeight;
    f32 contentStartY;
};

void djui_flow_layout_set_flow_direction(struct DjuiFlowLayout* layout, enum DjuiFlowDirection flowDirection);
void djui_flow_layout_set_margin(struct DjuiFlowLayout* layout, f32 margin);
void djui_flow_layout_set_margin_type(struct DjuiFlowLayout* layout, enum DjuiScreenValueType marginType);
void djui_flow_layout_enable_scroll(struct DjuiFlowLayout* layout, bool enabled);
void djui_flow_layout_scroll_by(struct DjuiFlowLayout* layout, f32 delta);

struct DjuiFlowLayout* djui_flow_layout_create(struct DjuiBase* parent);
