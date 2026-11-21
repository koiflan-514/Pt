/*
 * Pt popup demo: an application-owned popup surface with custom content,
 * runtime placement changes and a dismiss handler.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (src/pt/Pt.java, include/pt/popup.h):
 *
 *  - popup_create adopts the content widget and its subtree. The content is
 *    built detached (card(NULL)) and handed over exactly once; the application
 *    never uses or destroys it afterwards.
 *  - PopupAnchor is a record in parent-window logical pixels. The placement is
 *    a preference: the popup flips and slides to stay inside the work area.
 *  - The dismiss handler runs after the open state clears; it may re-show the
 *    popup. Destroying the popup or its window does not run it.
 *  - The upcall stub lives in the confined arena, which outlives the popup.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;

public final class PopupDemo {
    private PopupDemo() {}

    private static final class State {
        MemorySegment app;
        MemorySegment window;
        MemorySegment popup;
        MemorySegment trigger;
        MemorySegment placementButton;
        MemorySegment status;
        Pt.PopupAnchor anchor = new Pt.PopupAnchor(0.0f, 0.0f, 1.0f, 1.0f);
        int placement = Pt.popup_placement_below;
        int dismissals;
        int shows;
    }

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    private static String placementName(int placement) {
        return switch (placement) {
            case Pt.popup_placement_below -> "below";
            case Pt.popup_placement_above -> "above";
            case Pt.popup_placement_right -> "right";
            case Pt.popup_placement_left -> "left";
            default -> "?";
        };
    }

    private static void refreshStatus(State state, String what) {
        Pt.widget_set_text(state.status, what + " · placement "
                + placementName(state.placement) + " · open "
                + (Pt.popup_is_open(state.popup) ? "yes" : "no") + " · shown "
                + state.shows + ", dismissed " + state.dismissals);
    }

    private static void updateAnchor(State state) {
        float[] bounds = Pt.widget_get_bounds(state.trigger);
        state.anchor = new Pt.PopupAnchor(bounds[0], bounds[1], bounds[2], bounds[3]);
    }

    private static void openPopup(State state) {
        updateAnchor(state);
        if (Pt.popup_show(state.popup, state.anchor)) {
            state.shows++;
            refreshStatus(state, "Popup opened with the current placement");
        } else {
            Pt.widget_set_text(state.status, "Could not open the popup: " + Pt.last_error());
        }
    }

    private static void cyclePlacement(State state) {
        state.placement = switch (Pt.popup_get_placement(state.popup)) {
            case Pt.popup_placement_below -> Pt.popup_placement_above;
            case Pt.popup_placement_above -> Pt.popup_placement_right;
            case Pt.popup_placement_right -> Pt.popup_placement_left;
            default -> Pt.popup_placement_below;
        };
        Pt.popup_set_placement(state.popup, state.placement);
        Pt.widget_set_text(state.placementButton, placementName(state.placement));
        if (Pt.popup_is_open(state.popup)) {
            updateAnchor(state);
            Pt.popup_reposition(state.popup, state.anchor);
        }
        refreshStatus(state, "Placement preference changed");
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            State state = new State();
            state.app = Pt.app_create("dev.pt.examples.popup_demo.java");
            if (state.app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(state.app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(state.app, Pt.rgb(0x7AA2F7));

            state.window = Pt.window_create(state.app, "Pt Popup Demo", 620, 460);
            MemorySegment root = Pt.window_root(state.window);
            Pt.widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment eyebrow = Pt.label(root, "POPUP  ·  APPLICATION-OWNED SURFACE");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            MemorySegment heading = Pt.label(root, "Custom content, real placement");
            Pt.widget_set_font_size(heading, 28.0f);
            Pt.widget_set_font_weight(heading, Pt.font_weight_semibold);

            MemorySegment intro = Pt.label(root,
                    "The popup is a Pt surface built from ordinary widgets. The anchor is the "
                    + "trigger button rectangle; the placement flips and slides to stay inside "
                    + "the work area.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment card = Pt.card(root);
            Pt.widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
            Pt.widget_set_gap(card, 12.0f);

            MemorySegment actions = Pt.row(card);
            Pt.widget_set_gap(actions, 10.0f);
            Pt.widget_set_align_self(actions, Pt.align_center);

            state.trigger = Pt.button(actions, "Open popup");
            Pt.button_set_variant(state.trigger, Pt.button_primary);
            Pt.widget_set_tooltip(state.trigger, "Anchors the popup to this button");

            MemorySegment toggleButton = Pt.button(actions, "Toggle");
            Pt.button_set_variant(toggleButton, Pt.button_secondary);

            MemorySegment repositionButton = Pt.button(actions, "Reposition");
            Pt.button_set_variant(repositionButton, Pt.button_ghost);

            state.placementButton = Pt.button(actions, placementName(state.placement));
            Pt.button_set_variant(state.placementButton, Pt.button_soft);
            Pt.widget_set_tooltip(state.placementButton, "Cycle below → above → right → left");

            /* Detached content: the popup adopts it and owns it from then on. */
            MemorySegment content = Pt.card(MemorySegment.NULL);
            Pt.widget_set_padding(content, 16.0f, 16.0f, 16.0f, 16.0f);
            Pt.widget_set_gap(content, 10.0f);
            Pt.widget_set_background(content, Pt.rgb(0x161D28));
            Pt.widget_set_border_width(content, 1.0f);
            Pt.widget_set_border_color(content, Pt.rgb(0x30415C));
            Pt.widget_set_radius(content, 12.0f);
            Pt.widget_set_min_width(content, 260.0f);
            Pt.widget_set_a11y_name(content, "Popup content");

            MemorySegment contentTitle = Pt.label(content, "Popup content");
            Pt.widget_set_font_size(contentTitle, 16.0f);
            Pt.widget_set_font_weight(contentTitle, Pt.font_weight_semibold);
            MemorySegment contentBody = Pt.label(content,
                    "This card lives in the popup tree. Escape or a click outside dismisses it "
                    + "and fires the handler below.");
            Pt.widget_set_font_size(contentBody, 12.0f);
            Pt.widget_set_wrap(contentBody, true);
            Pt.widget_set_foreground(contentBody, Pt.rgb(0x9AA7B8));

            MemorySegment contentActions = Pt.row(content);
            Pt.widget_set_gap(contentActions, 8.0f);
            MemorySegment pingButton = Pt.button(contentActions, "Ping");
            Pt.button_set_variant(pingButton, Pt.button_secondary);
            MemorySegment closeButton = Pt.button(contentActions, "Dismiss");
            Pt.button_set_variant(closeButton, Pt.button_ghost);

            state.popup = Pt.popup_create(state.window, content);
            if (state.popup.address() == 0) {
                System.err.println(Pt.last_error());
                Pt.app_destroy(state.app);
                System.exit(1);
            }

            Pt.PopupDismissHandler onDismiss = (popup, data) -> {
                state.dismissals++;
                System.out.println("popup_demo: dismiss #" + state.dismissals + " (is_open="
                        + (Pt.popup_is_open(popup) ? 1 : 0) + ")");
                refreshStatus(state, "Dismiss handler fired after the popup closed");
            };
            Pt.popup_set_on_dismiss(state.popup, onDismiss, arena);
            Pt.popup_set_placement(state.popup, state.placement);

            Pt.ClickHandler onOpen = (widget, data) -> openPopup(state);
            Pt.ClickHandler onToggle = (widget, data) -> {
                if (Pt.popup_is_open(state.popup)) {
                    Pt.popup_dismiss(state.popup);
                    refreshStatus(state, "popup_dismiss called");
                    return;
                }
                openPopup(state);
            };
            Pt.ClickHandler onReposition = (widget, data) -> {
                updateAnchor(state);
                state.anchor = new Pt.PopupAnchor(state.anchor.x() + 24.0f,
                        state.anchor.y() + 18.0f, state.anchor.width(), state.anchor.height());
                if (Pt.popup_reposition(state.popup, state.anchor)) {
                    refreshStatus(state, "popup_reposition moved the open popup");
                } else {
                    refreshStatus(state, "Popup is closed; reposition is a no-op");
                }
            };
            Pt.ClickHandler onCycle = (widget, data) -> cyclePlacement(state);
            Pt.ClickHandler onPing = (widget, data) ->
                    refreshStatus(state, "Ping from the popup content");
            Pt.ClickHandler onClose = (widget, data) -> {
                Pt.popup_dismiss(state.popup);
                refreshStatus(state, "Close button inside the popup");
            };

            Pt.button_set_on_click(state.trigger, onOpen, arena);
            Pt.button_set_on_click(toggleButton, onToggle, arena);
            Pt.button_set_on_click(repositionButton, onReposition, arena);
            Pt.button_set_on_click(state.placementButton, onCycle, arena);
            Pt.button_set_on_click(pingButton, onPing, arena);
            Pt.button_set_on_click(closeButton, onClose, arena);

            state.status = Pt.label(root, "");
            Pt.widget_set_font_size(state.status, 12.0f);
            Pt.widget_set_wrap(state.status, true);
            Pt.widget_set_foreground(state.status, Pt.rgb(0x9ECE6A));
            refreshStatus(state, "Ready; the popup starts closed");

            MemorySegment footer = Pt.label(root,
                    "popup_content returns a borrowed widget valid until the popup is "
                    + "destroyed. Destroying the popup or its window does not run the dismiss "
                    + "handler.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            Pt.window_show(state.window);
            Pt.window_focus(state.window, state.trigger);

            int status;
            if (!headless()) {
                status = Pt.app_run(state.app);
                Pt.popup_destroy(state.popup);
                Pt.app_destroy(state.app);
                System.exit(status);
            }

            onOpen.handle(MemorySegment.NULL, MemorySegment.NULL);
            System.out.println("popup_demo: open=" + (Pt.popup_is_open(state.popup) ? 1 : 0)
                    + " placement=" + placementName(Pt.popup_get_placement(state.popup))
                    + " content=" + Pt.popup_content(state.popup).address());
            onCycle.handle(MemorySegment.NULL, MemorySegment.NULL);
            onReposition.handle(MemorySegment.NULL, MemorySegment.NULL);
            onPing.handle(MemorySegment.NULL, MemorySegment.NULL);
            onToggle.handle(MemorySegment.NULL, MemorySegment.NULL);
            onOpen.handle(MemorySegment.NULL, MemorySegment.NULL);
            Pt.popup_dismiss(state.popup);
            System.out.println("popup_demo: after dismiss open="
                    + (Pt.popup_is_open(state.popup) ? 1 : 0) + " dismissals="
                    + state.dismissals);
            status = Pt.app_run_for(state.app, 250);
            /* Still safe while the window is alive; this does not run the handler. */
            Pt.popup_destroy(state.popup);
            Pt.app_destroy(state.app);
            System.exit(status);
        }
    }
}