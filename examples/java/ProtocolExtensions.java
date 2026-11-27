/*
 * Pt protocol extensions - Java 25 Foreign Function & Memory API.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The six 2.4.0 calls take ActivationTokenHandler / ForeignHandleHandler
 * functional interfaces. The upcall decodes a callback-scoped String, and the
 * Arena passed to the request must outlive the callback. Tokens and handles are
 * never retained; unexport and clear are idempotent.
 *
 * Without the xdg-activation or xdg-foreign globals, or on a headless platform,
 * every call returns false and no callback runs. PT_HEADLESS=1 asserts that
 * fallback and exits 0; the live path runs only with a compositor.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class ProtocolExtensions {
    private ProtocolExtensions() {}

    private static void addText(MemorySegment root, String text, float size, int color) {
        MemorySegment label = Pt.label(root, text);
        Pt.widget_set_font_size(label, size);
        Pt.widget_set_foreground(label, color);
        Pt.widget_set_wrap(label, true);
    }

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.protocol_extensions.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            MemorySegment window =
                    Pt.window_create(app, "Pt Protocol Extensions - Java", 720, 420);
            MemorySegment dialog = Pt.window_create(app, "Pt Foreign Dialog - Java", 420, 240);
            if (window.address() == 0 || dialog.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(root, 10.0f);

            MemorySegment eyebrow =
                    Pt.label(root, "JAVA BINDING  ·  ACTIVATION  ·  XDG-FOREIGN-V2");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            addText(
                    root,
                    "The window requests a one-shot activation token, activates with the "
                            + "borrowed string, exports a foreign toplevel handle and imports "
                            + "it on the second window.",
                    13.0f,
                    Pt.rgb(0x9AA7B8));
            addText(
                    root,
                    "A token or handle callback is valid only during the call. True from "
                            + "activate means sent, not focused, and the handle is never "
                            + "parsed.",
                    12.0f,
                    Pt.rgb(0x6F7C8F));

            boolean[] tokenSeen = {false};
            boolean[] handleSeen = {false};

            Pt.ActivationTokenHandler tokenHandler = (source, token, data) -> {
                tokenSeen[0] = true;
                if (token != null && !token.isEmpty()) {
                    Pt.window_activate(source, token);
                }
            };
            Pt.ForeignHandleHandler handleHandler = (exporter, handle, data) -> {
                handleSeen[0] = handle != null && !handle.isEmpty();
                if (handleSeen[0]) {
                    Pt.window_set_foreign_parent(dialog, handle);
                }
            };

            Pt.window_show(window);
            Pt.window_show(dialog);

            int failures = 0;
            int runStatus;
            if (headless()) {
                boolean tokenRequest = Pt.window_request_activation_token(
                        window, "dev.pt.examples.protocol_extensions.java", tokenHandler, arena);
                boolean activated = Pt.window_activate(window, "headless-token");
                boolean exported = Pt.window_export_foreign_handle(
                        window, handleHandler, arena);
                Pt.window_unexport_foreign_handle(window);
                Pt.window_unexport_foreign_handle(window);
                boolean parented =
                        Pt.window_set_foreign_parent(dialog, "headless-handle");
                Pt.window_clear_foreign_parent(dialog);
                Pt.window_clear_foreign_parent(dialog);

                if (tokenRequest || activated || exported || parented) {
                    failures++;
                }
                if (tokenSeen[0] || handleSeen[0]) {
                    failures++;
                }
                System.out.printf(
                        "protocol_extensions: headless request=%d activate=%d export=%d "
                                + "parent=%d callbacks=%d/%d failures=%d%n",
                        tokenRequest ? 1 : 0, activated ? 1 : 0, exported ? 1 : 0,
                        parented ? 1 : 0, tokenSeen[0] ? 1 : 0, handleSeen[0] ? 1 : 0,
                        failures);
                System.out.flush();
                runStatus = Pt.app_run_for(app, 250);
            } else {
                boolean tokenRequest = Pt.window_request_activation_token(
                        window, "dev.pt.examples.protocol_extensions.java", tokenHandler, arena);
                boolean exported = Pt.window_export_foreign_handle(
                        window, handleHandler, arena);
                System.out.printf("protocol_extensions: live request=%d export=%d%n",
                        tokenRequest ? 1 : 0, exported ? 1 : 0);
                System.out.flush();
                runStatus = Pt.app_run(app);
            }

            Pt.app_destroy(app);
            if (failures > 0) {
                System.exit(1);
            }
            if (runStatus != 0) {
                System.exit(runStatus);
            }
        }
    }
}