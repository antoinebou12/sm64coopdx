#include "djui.h"
#include "djui_interactable.h"
#include "engine/math_util.h"

  ////////////////
 // properties //
////////////////

void djui_flow_layout_set_flow_direction(struct DjuiFlowLayout* layout, enum DjuiFlowDirection flowDirection) {
    layout->flowDirection = flowDirection;
}

void djui_flow_layout_set_margin(struct DjuiFlowLayout* layout, f32 margin) {
    layout->margin.value = margin;
}

void djui_flow_layout_set_margin_type(struct DjuiFlowLayout* layout, enum DjuiScreenValueType marginType) {
    layout->margin.type = marginType;
}

void djui_flow_layout_enable_scroll(struct DjuiFlowLayout* layout, bool enabled) {
    layout->scrollEnabled = enabled;
    if (!enabled) {
        layout->scrollY = 0.0f;
    }
}

void djui_flow_layout_scroll_by(struct DjuiFlowLayout* layout, f32 delta) {
    if (layout == NULL || !layout->scrollEnabled) { return; }
    f32 maxScroll = fmax(0.0f, layout->contentHeight - layout->viewHeight);
    layout->scrollY = clamp(layout->scrollY + delta, 0.0f, maxScroll);
}

  ////////////
 // events //
////////////

static void djui_flow_layout_on_child_render(struct DjuiBase* base, struct DjuiBase* child) {
    if (!child->visible) { return; }
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    switch (layout->flowDirection) {
        case DJUI_FLOW_DIR_DOWN:
            base->comp.y      += (child->elem.height + layout->margin.value);
            base->comp.height -= (child->elem.height + layout->margin.value);
            if (layout->scrollEnabled) {
                layout->contentHeight = (base->comp.y - layout->contentStartY) + layout->scrollY;
            }
            break;
        case DJUI_FLOW_DIR_UP:
            base->comp.height -= (child->elem.height + layout->margin.value);
            break;
        case DJUI_FLOW_DIR_RIGHT:
            base->comp.x     += (child->elem.width + layout->margin.value);
            base->comp.width -= (child->elem.width + layout->margin.value);
            break;
        case DJUI_FLOW_DIR_LEFT:
            base->comp.width -= (child->elem.width + layout->margin.value);
            break;
    }
}

static bool djui_flow_layout_render(struct DjuiBase* base) {
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    if (layout->scrollEnabled) {
        layout->viewHeight = base->comp.height;
        layout->contentStartY = base->comp.y;
        layout->contentHeight = layout->margin.value;

        /* C-Stick Y scrolls overflowing menu bodies (D-Pad still moves the cursor). */
        if (gInteractableOverridePad) {
            s8 stickY = gInteractablePad.ext_stick_y;
            if (stickY > 40) {
                djui_flow_layout_scroll_by(layout, -12.0f);
            } else if (stickY < -40) {
                djui_flow_layout_scroll_by(layout, 12.0f);
            }
            if (gInteractablePad.button & L_TRIG) {
                djui_flow_layout_scroll_by(layout, -24.0f);
            }
            if (gInteractablePad.button & R_TRIG) {
                djui_flow_layout_scroll_by(layout, 24.0f);
            }
        }

        f32 maxScroll = fmax(0.0f, layout->contentHeight - layout->viewHeight);
        layout->scrollY = clamp(layout->scrollY, 0.0f, maxScroll);
        base->comp.y -= layout->scrollY;
        /* Grow layout height so clipped children still lay out past the view. */
        if (layout->contentHeight > layout->viewHeight) {
            base->comp.height = layout->contentHeight + 8.0f;
        }
    }
    return true;
}

static void djui_flow_layout_destroy(struct DjuiBase* base) {
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    free(layout);
}

struct DjuiFlowLayout* djui_flow_layout_create(struct DjuiBase* parent) {
    struct DjuiFlowLayout* layout = calloc(1, sizeof(struct DjuiFlowLayout));
    struct DjuiBase* base = &layout->base;

    djui_base_init(parent, base, djui_flow_layout_render, djui_flow_layout_destroy);
    djui_base_set_size_type(base, DJUI_SVT_RELATIVE, DJUI_SVT_RELATIVE);
    djui_base_set_size(base, 1.0f, 1.0f);
    djui_base_set_color(base, 0, 0, 0, 0);

    djui_flow_layout_set_flow_direction(layout, DJUI_FLOW_DIR_DOWN);
    djui_flow_layout_set_margin(layout, 8);
    layout->scrollEnabled = false;
    layout->scrollY = 0.0f;

    layout->base.on_child_render = djui_flow_layout_on_child_render;

    return layout;
}
