/*
 * Headless tests for the Pt Java (FFM) binding.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

package pt;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

@SuppressWarnings("restricted")
public final class PtTest {
    private PtTest() {}

    private static int checks = 0;
    private static int failures = 0;

    private static void check(boolean condition, String label) {
        checks += 1;
        if (!condition) {
            failures += 1;
            System.err.println("FAIL: " + label);
        }
    }

    /** Creates a libc pipe so the fd-watch test can dispatch a real event. */
    private static int[] nativePipe() {
        try {
            Linker linker = Linker.nativeLinker();
            MethodHandle pipe = linker.downcallHandle(
                    linker.defaultLookup().find("pipe").orElseThrow(),
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment descriptors = arena.allocate(ValueLayout.JAVA_INT, 2);
                if ((int) pipe.invokeExact(descriptors) != 0) {
                    return null;
                }
                return new int[] {descriptors.get(ValueLayout.JAVA_INT, 0),
                        descriptors.get(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT.byteSize())};
            }
        } catch (Throwable error) {
            return null;
        }
    }

    private static boolean nativeWrite(int fd, byte value) {
        try {
            Linker linker = Linker.nativeLinker();
            MethodHandle write = linker.downcallHandle(
                    linker.defaultLookup().find("write").orElseThrow(),
                    FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT,
                            ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment buffer = arena.allocate(ValueLayout.JAVA_BYTE);
                buffer.set(ValueLayout.JAVA_BYTE, 0, value);
                return (long) write.invokeExact(fd, buffer, 1L) == 1L;
            }
        } catch (Throwable error) {
            return false;
        }
    }

    /** True while the process still has an open descriptor with this number. */
    private static boolean descriptorOpen(int fd) {
        return java.nio.file.Files.exists(
                java.nio.file.Path.of("/proc/self/fd", Integer.toString(fd)));
    }

    private static void putBe32(java.io.ByteArrayOutputStream out, int value) {
        out.write(value >>> 24);
        out.write(value >>> 16);
        out.write(value >>> 8);
        out.write(value);
    }

    private static void pngChunk(java.io.ByteArrayOutputStream out, String type, byte[] data) {
        putBe32(out, data.length);
        byte[] tag = type.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        out.write(tag, 0, tag.length);
        out.write(data, 0, data.length);
        java.util.zip.CRC32 crc = new java.util.zip.CRC32();
        crc.update(tag);
        crc.update(data);
        putBe32(out, (int) crc.getValue());
    }

    /**
     * Encodes premultiplied ARGB pixels as an 8-bit RGBA PNG. The headless
     * clipboard and icon decoders reject anything but a real PNG, so the tests
     * build one with the JDK's own deflater instead of shipping a fixture.
     */
    private static byte[] png(int width, int height, int[] pixels) throws Exception {
        int stride = width * 4;
        byte[] raw = new byte[(stride + 1) * height];
        for (int y = 0; y < height; y++) {
            int row = y * (stride + 1);
            raw[row] = 0;
            for (int x = 0; x < width; x++) {
                int pixel = pixels[y * width + x];
                int at = row + 1 + x * 4;
                raw[at] = (byte) (pixel >>> 16);
                raw[at + 1] = (byte) (pixel >>> 8);
                raw[at + 2] = (byte) pixel;
                raw[at + 3] = (byte) (pixel >>> 24);
            }
        }
        java.util.zip.Deflater deflater = new java.util.zip.Deflater();
        deflater.setInput(raw);
        deflater.finish();
        byte[] compressed = new byte[raw.length + 256];
        int length = deflater.deflate(compressed);
        deflater.end();
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        out.write(new byte[] {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'});
        byte[] header = {
            (byte) (width >>> 24), (byte) (width >>> 16), (byte) (width >>> 8), (byte) width,
            (byte) (height >>> 24), (byte) (height >>> 16), (byte) (height >>> 8), (byte) height,
            8, 6, 0, 0, 0,
        };
        pngChunk(out, "IHDR", header);
        pngChunk(out, "IDAT", java.util.Arrays.copyOf(compressed, length));
        pngChunk(out, "IEND", new byte[0]);
        return out.toByteArray();
    }

    private static MemorySegment cstring(Arena arena, String value) {
        byte[] bytes = value.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        MemorySegment text = arena.allocate(bytes.length + 1L);
        MemorySegment.copy(bytes, 0, text, ValueLayout.JAVA_BYTE, 0, bytes.length);
        text.set(ValueLayout.JAVA_BYTE, bytes.length, (byte) 0);
        return text;
    }

    private static void nativeUnsetenv(String name) throws Throwable {
        Linker linker = Linker.nativeLinker();
        MethodHandle unset = linker.downcallHandle(
                linker.defaultLookup().find("unsetenv").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        try (Arena arena = Arena.ofConfined()) {
            int ignored = (int) unset.invokeExact(cstring(arena, name));
        }
    }

    private static void nativeSetenv(String name, String value) throws Throwable {
        Linker linker = Linker.nativeLinker();
        MethodHandle set = linker.downcallHandle(
                linker.defaultLookup().find("setenv").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                        ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        try (Arena arena = Arena.ofConfined()) {
            int ignored = (int) set.invokeExact(cstring(arena, name), cstring(arena, value), 1);
        }
    }

    private static MethodHandle bindSymbol(SymbolLookup lookup, String name,
            FunctionDescriptor descriptor) {
        return Linker.nativeLinker().downcallHandle(
                lookup.find(name).orElseThrow(
                        () -> new IllegalStateException("missing wire driver symbol " + name)),
                descriptor);
    }

    private static Path wireDriverPath() {
        String explicit = System.getProperty("pt.wire_driver");
        if (explicit != null && !explicit.isEmpty()) {
            return Path.of(explicit).toAbsolutePath();
        }
        String library = System.getProperty("pt.library");
        if (library != null && !library.isEmpty()) {
            Path parent = Path.of(library).toAbsolutePath().getParent();
            if (parent != null) {
                return parent.resolve("tests").resolve("libwire_driver.so");
            }
        }
        return Path.of("build", "tests", "libwire_driver.so").toAbsolutePath();
    }

    private static String wireError(MethodHandle wdError) throws Throwable {
        MemorySegment text = (MemorySegment) wdError.invokeExact();
        return text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0);
    }

    /**
     * 1.3 wave 2: injects real Wayland key/pointer events through the C wire
     * driver and asserts the Java upcall wrappers receive them. This closes
     * M19's synthetic-only raw dispatch coverage.
     */
    private static void wireDispatchTest(Arena arena) throws Throwable {
        final String previousHeadless = System.getenv("PT_HEADLESS");
        nativeUnsetenv("PT_HEADLESS");
        boolean started = false;
        MemorySegment wireApp = MemorySegment.NULL;
        MethodHandle wdStop = null;
        Arena driverArena = Arena.ofConfined();
        try {
            SymbolLookup lookup = SymbolLookup.libraryLookup(wireDriverPath(), driverArena);
            MethodHandle wdStart = bindSymbol(lookup, "wd_start",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            wdStop = bindSymbol(lookup, "wd_stop", FunctionDescriptor.ofVoid());
            MethodHandle wdError = bindSymbol(lookup, "wd_error",
                    FunctionDescriptor.of(ValueLayout.ADDRESS));
            MethodHandle wdSync = bindSymbol(lookup, "wd_sync",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            MethodHandle wdSettle = bindSymbol(lookup, "wd_settle",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            MethodHandle wdWaitConfigured = bindSymbol(lookup, "wd_wait_configured",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                            ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
            MethodHandle wdToplevelSurface = bindSymbol(lookup, "wd_toplevel_surface",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT));
            MethodHandle wdRequestOverflowed = bindSymbol(lookup, "wd_request_overflowed",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT));
            MethodHandle wdPendingFdCount = bindSymbol(lookup, "wd_pending_fd_count",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT));
            MethodHandle wdKeyboardEnter = bindSymbol(lookup, "wd_keyboard_enter",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdKeyboardLeave = bindSymbol(lookup, "wd_keyboard_leave",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdKeyboardKey = bindSymbol(lookup, "wd_keyboard_key",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdKeyboardModifiers = bindSymbol(lookup, "wd_keyboard_modifiers",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT));
            MethodHandle wdPointerEnter = bindSymbol(lookup, "wd_pointer_enter",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_DOUBLE, ValueLayout.JAVA_DOUBLE));
            MethodHandle wdPointerLeave = bindSymbol(lookup, "wd_pointer_leave",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdPointerMotion = bindSymbol(lookup, "wd_pointer_motion",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_DOUBLE,
                            ValueLayout.JAVA_DOUBLE));
            MethodHandle wdPointerButton = bindSymbol(lookup, "wd_pointer_button",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdPointerAxis = bindSymbol(lookup, "wd_pointer_axis",
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_DOUBLE));

            /* FC_OPT_ALL: mandatory globals plus every optional interface. */
            started = (int) wdStart.invokeExact(0x1FF) == 0;
            check(started, "wire driver start: " + wireError(wdError));
            if (!started) {
                return;
            }
            wireApp = Pt.app_create("pt.tests.java.wire");
            check(wireApp.address() != 0, "wire app_create");
            if (wireApp.address() == 0) {
                return;
            }
            check((int) wdSettle.invokeExact(wireApp) == 0, "wire settle");
            check(wireError(wdError).isEmpty(), "wire handshake clean");

            MemorySegment wireWindow = Pt.window_create(wireApp, "java wire", 320, 240);
            check(wireWindow.address() != 0, "wire window_create");
            MemorySegment entry = Pt.entry(Pt.window_root(wireWindow), "");
            check(Pt.widget_focus(entry), "wire entry focus");

            int[] keyEventCalls = {0};
            int[] keyCalls = {0};
            int[] keyValue = {0};
            int[] keyModifiers = {0};
            String[] keyText = {""};
            List<Pt.PointerEvent> pointerEvents = new ArrayList<>();
            Pt.window_set_key_event_handler(wireWindow,
                    (window, key, modifiers, text, data) -> {
                        keyEventCalls[0] += 1;
                        keyValue[0] = key;
                        keyModifiers[0] = modifiers;
                        keyText[0] = text;
                        return false;
                    }, arena);
            Pt.window_set_key_handler(wireWindow,
                    (window, key, modifiers, data) -> {
                        keyCalls[0] += 1;
                        keyValue[0] = key;
                        keyModifiers[0] = modifiers;
                        return false;
                    }, arena);
            Pt.window_set_pointer_handler(wireWindow,
                    (window, event, data) -> {
                        pointerEvents.add(event);
                        return false;
                    }, arena);

            Pt.window_show(wireWindow);
            check((int) wdWaitConfigured.invokeExact(wireApp, wireWindow, 300) == 1,
                    "wire window configured");
            check((int) wdSettle.invokeExact(wireApp) == 0, "wire settle after show");
            int surface = (int) wdToplevelSurface.invokeExact();
            check(surface != 0, "wire toplevel surface");

            wdKeyboardEnter.invokeExact(7100, surface);
            wdKeyboardModifiers.invokeExact(7101, 0, 0, 0, 0);
            wdKeyboardKey.invokeExact(7102, 20, 30, 1); /* KEY_A press */
            check((int) wdSync.invokeExact(wireApp) == 0, "wire key sync");
            check(keyEventCalls[0] == 1, "wire key_event calls");
            check(keyValue[0] == 0x61, "wire keysym a");
            check(keyModifiers[0] == 0, "wire key modifiers 0");
            check("a".equals(keyText[0]), "wire key text a");
            check(keyCalls[0] == 1, "wire legacy key calls");
            check("a".equals(Pt.widget_get_text(entry)), "wire entry text a");

            wdKeyboardModifiers.invokeExact(7103, 1, 0, 0, 0); /* shift depressed */
            wdKeyboardKey.invokeExact(7104, 21, 30, 1); /* KEY_A with shift */
            check((int) wdSync.invokeExact(wireApp) == 0, "wire shift sync");
            check(keyEventCalls[0] == 2, "wire shift key_event calls");
            check(keyValue[0] == 0x41, "wire shifted keysym A");
            check(keyModifiers[0] == Pt.key_modifier_shift, "wire shift modifier");
            check("A".equals(keyText[0]), "wire shifted text A");
            check("aA".equals(Pt.widget_get_text(entry)), "wire entry text aA");

            wdKeyboardKey.invokeExact(7105, 22, 30, 0); /* release */
            wdKeyboardModifiers.invokeExact(7106, 0, 0, 0, 0);
            check((int) wdSync.invokeExact(wireApp) == 0, "wire release sync");
            wdKeyboardLeave.invokeExact(7107, surface);

            wdPointerEnter.invokeExact(7001, surface, 40.0, 50.0);
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer enter sync");
            check(pointerEvents.size() == 1, "wire pointer enter count");
            if (pointerEvents.size() == 1) {
                Pt.PointerEvent enter = pointerEvents.get(0);
                check(enter.phase() == Pt.pointer_phase_enter, "wire pointer enter phase");
                check(Math.abs(enter.x() - 40.0f) < 0.001f, "wire pointer enter x");
                check(Math.abs(enter.y() - 50.0f) < 0.001f, "wire pointer enter y");
            }
            wdPointerMotion.invokeExact(17, 60.0, 80.0);
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer motion sync");
            check(pointerEvents.size() == 2, "wire pointer motion count");
            if (pointerEvents.size() == 2) {
                Pt.PointerEvent motion = pointerEvents.get(1);
                check(motion.phase() == Pt.pointer_phase_motion, "wire pointer motion phase");
                check(Math.abs(motion.x() - 60.0f) < 0.001f, "wire pointer motion x");
                check(Math.abs(motion.y() - 80.0f) < 0.001f, "wire pointer motion y");
                check(Math.abs(motion.delta_x() - 20.0f) < 0.001f, "wire motion delta x");
                check(Math.abs(motion.delta_y() - 30.0f) < 0.001f, "wire motion delta y");
            }
            wdPointerButton.invokeExact(7002, 18, 0x110, 1); /* BTN_LEFT down */
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer down sync");
            check(pointerEvents.size() == 3, "wire pointer down count");
            if (pointerEvents.size() == 3) {
                Pt.PointerEvent down = pointerEvents.get(2);
                check(down.phase() == Pt.pointer_phase_down, "wire pointer down phase");
                check(down.button() == 0x110, "wire pointer down button");
                check(down.pressed(), "wire pointer down pressed");
                check(down.click_count() == 1, "wire pointer down click count");
            }
            wdPointerButton.invokeExact(7003, 19, 0x110, 0); /* BTN_LEFT up */
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer up sync");
            check(pointerEvents.size() == 4, "wire pointer up count");
            if (pointerEvents.size() == 4) {
                Pt.PointerEvent up = pointerEvents.get(3);
                check(up.phase() == Pt.pointer_phase_up, "wire pointer up phase");
                check(up.button() == 0x110, "wire pointer up button");
                check(!up.pressed(), "wire pointer up released");
            }
            wdPointerAxis.invokeExact(20, 0, 150.0); /* vertical scroll: 150/10 */
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer axis sync");
            check(pointerEvents.size() == 5, "wire pointer axis count");
            if (pointerEvents.size() == 5) {
                Pt.PointerEvent axis = pointerEvents.get(4);
                check(axis.phase() == Pt.pointer_phase_axis, "wire pointer axis phase");
                check(Math.abs(axis.delta_y() - 15.0f) < 0.001f, "wire axis delta y");
            }
            wdPointerLeave.invokeExact(7004, surface);
            check((int) wdSync.invokeExact(wireApp) == 0, "wire pointer leave sync");
            check(pointerEvents.size() == 6, "wire pointer leave count");
            if (pointerEvents.size() == 6) {
                check(pointerEvents.get(5).phase() == Pt.pointer_phase_leave,
                        "wire pointer leave phase");
            }

            check(wireError(wdError).isEmpty(), "wire protocol clean");
            check((int) wdRequestOverflowed.invokeExact() == 0, "wire request log ok");
            check((int) wdPendingFdCount.invokeExact() == 0, "wire fds drained");
        } finally {
            try {
                if (wireApp.address() != 0) {
                    Pt.app_destroy(wireApp);
                }
                if (started && wdStop != null) {
                    wdStop.invokeExact();
                }
            } finally {
                driverArena.close();
                if (previousHeadless != null) {
                    nativeSetenv("PT_HEADLESS", previousHeadless);
                } else {
                    nativeUnsetenv("PT_HEADLESS");
                }
            }
        }
    }

    /**
     * 2.4 wave: activation-token and foreign-handle delivery over the same fake
     * compositor. The driver forwards xdg_activation_token_v1.done and
     * zxdg_exported_v2.handle, so the Java upcalls observe a real wire event
     * rather than a synthetic C call.
     */
    private static void activationForeignWireTest(Arena arena) throws Throwable {
        final String previousHeadless = System.getenv("PT_HEADLESS");
        nativeUnsetenv("PT_HEADLESS");
        boolean started = false;
        MemorySegment wireApp = MemorySegment.NULL;
        MemorySegment wireWindow = MemorySegment.NULL;
        MethodHandle wdStop = null;
        Arena driverArena = Arena.ofConfined();
        try {
            SymbolLookup lookup = SymbolLookup.libraryLookup(wireDriverPath(), driverArena);
            MethodHandle wdStart = bindSymbol(lookup, "wd_start",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            wdStop = bindSymbol(lookup, "wd_stop", FunctionDescriptor.ofVoid());
            MethodHandle wdError = bindSymbol(lookup, "wd_error",
                    FunctionDescriptor.of(ValueLayout.ADDRESS));
            MethodHandle wdSync = bindSymbol(lookup, "wd_sync",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            MethodHandle wdSettle = bindSymbol(lookup, "wd_settle",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            MethodHandle wdWaitConfigured = bindSymbol(lookup, "wd_wait_configured",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                            ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
            MethodHandle wdLiveId = bindSymbol(lookup, "wd_live_id",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdWaitObject = bindSymbol(lookup, "wd_wait_object",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdGlobalBound = bindSymbol(lookup, "wd_global_bound",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
            MethodHandle wdRequestCount = bindSymbol(lookup, "wd_request_count",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                            ValueLayout.JAVA_INT));
            MethodHandle wdTokenDone = bindSymbol(lookup, "wd_activation_token_done",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            MethodHandle wdExportedHandle = bindSymbol(lookup, "wd_exported_handle",
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

            /* FC_OPT_ALL | FC_OPT_ACTIVATION | FC_OPT_FOREIGN. */
            started = (int) wdStart.invokeExact(0x1FF | (1 << 11) | (1 << 12)) == 0;
            check(started, "2.4 wire driver start: " + wireError(wdError));
            if (!started) {
                return;
            }
            wireApp = Pt.app_create("pt.tests.java.m26.wire");
            check(wireApp.address() != 0, "2.4 wire app_create");
            if (wireApp.address() == 0) {
                return;
            }
            check((int) wdSettle.invokeExact(wireApp) == 0, "2.4 wire settle");
            wireWindow = Pt.window_create(wireApp, "java 2.4 wire", 320, 240);
            check(wireWindow.address() != 0, "2.4 wire window_create");
            if (wireWindow.address() == 0) {
                return;
            }
            Pt.window_show(wireWindow);
            check((int) wdWaitConfigured.invokeExact(wireApp, wireWindow, 300) == 1,
                    "2.4 wire window configured");
            check((int) wdSettle.invokeExact(wireApp) == 0, "2.4 wire settle after show");
            check((int) wdGlobalBound.invokeExact(44) == 1, "2.4 activation global bound");
            check((int) wdGlobalBound.invokeExact(46) == 1, "2.4 exporter global bound");
            check((int) wdGlobalBound.invokeExact(48) == 1, "2.4 importer global bound");

            final int[] tokenCalls = {0};
            final String[] tokenSeen = {""};
            Pt.ActivationTokenHandler tokenHandler = (target, token, data) -> {
                tokenCalls[0] += 1;
                tokenSeen[0] = token;
            };
            check(Pt.window_request_activation_token(wireWindow, "pt.tests.java.m26",
                    tokenHandler, arena), "2.4 wire token request");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire token request sync");
            check((int) wdRequestCount.invokeExact(44, 1) == 1,
                    "2.4 get_activation_token sent");
            check((int) wdLiveId.invokeExact(45) != 0, "2.4 token object live");
            check(tokenCalls[0] == 0, "2.4 token not delivered before done");
            check((int) wdTokenDone.invokeExact(arena.allocateFrom("java-token")) == 1,
                    "2.4 token done injected");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire token done sync");
            check(tokenCalls[0] == 1, "2.4 token delivered once");
            check("java-token".equals(tokenSeen[0]), "2.4 token string delivered");
            /* The token destroy is queued while the done event dispatches, so
             * it only reaches the fake server on a later flush. Wait for the
             * live table before injecting the late event. */
            check((int) wdWaitObject.invokeExact(wireApp, 45, 0, 8) == 1
                    && (int) wdLiveId.invokeExact(45) == 0,
                    "2.4 token object destroyed before late done");
            check((int) wdTokenDone.invokeExact(arena.allocateFrom("late-token")) == 0,
                    "2.4 late token done is a no-op");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire late token sync");
            check(tokenCalls[0] == 1, "2.4 late token not delivered");

            check(Pt.window_activate(wireWindow, "opaque-token"),
                    "2.4 wire activate sends token");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire activate sync");
            check((int) wdRequestCount.invokeExact(44, 2) == 1,
                    "2.4 activate request sent once");

            final int[] handleCalls = {0};
            final String[] handleSeen = {""};
            Pt.ForeignHandleHandler handleHandler = (target, handle, data) -> {
                handleCalls[0] += 1;
                handleSeen[0] = handle;
            };
            check(Pt.window_export_foreign_handle(wireWindow, handleHandler, arena),
                    "2.4 wire export");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire export sync");
            check((int) wdRequestCount.invokeExact(46, 1) == 1, "2.4 export_toplevel sent");
            check((int) wdLiveId.invokeExact(47) != 0, "2.4 exported object live");
            check(handleCalls[0] == 0, "2.4 handle not delivered before event");
            check((int) wdExportedHandle.invokeExact(arena.allocateFrom("java-handle")) == 1,
                    "2.4 exported handle injected");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire handle sync");
            check(handleCalls[0] == 1, "2.4 handle delivered once");
            check("java-handle".equals(handleSeen[0]), "2.4 handle string delivered");
            Pt.window_unexport_foreign_handle(wireWindow);
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire unexport sync");
            check((int) wdLiveId.invokeExact(47) == 0, "2.4 exported object destroyed");
            check((int) wdExportedHandle.invokeExact(arena.allocateFrom("late-handle")) == 0,
                    "2.4 late handle is a no-op");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire late handle sync");
            check(handleCalls[0] == 1, "2.4 late handle not delivered");

            check(Pt.window_set_foreign_parent(wireWindow, "opaque-parent"),
                    "2.4 wire import parent");
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire import sync");
            check((int) wdLiveId.invokeExact(49) != 0, "2.4 imported object live");
            Pt.window_clear_foreign_parent(wireWindow);
            check((int) wdSync.invokeExact(wireApp) == 0, "2.4 wire clear parent sync");
            check((int) wdLiveId.invokeExact(49) == 0, "2.4 imported object destroyed");
            check(wireError(wdError).isEmpty(), "2.4 wire protocol clean");
        } finally {
            try {
                if (wireApp.address() != 0) {
                    Pt.app_destroy(wireApp);
                }
                if (started && wdStop != null) {
                    wdStop.invokeExact();
                }
            } finally {
                driverArena.close();
                if (previousHeadless != null) {
                    nativeSetenv("PT_HEADLESS", previousHeadless);
                } else {
                    nativeUnsetenv("PT_HEADLESS");
                }
            }
        }
    }

    /**
     * Pins a private Pt MethodHandle's FFM signature: boolean result and one
     * MemorySegment (pointer) argument.
     */
    private static boolean descriptorPinned(String fieldName) {
        try {
            java.lang.reflect.Field field = Pt.class.getDeclaredField(fieldName);
            field.setAccessible(true);
            MethodHandle handle = (MethodHandle) field.get(null);
            return handle.type().returnType() == boolean.class
                    && handle.type().parameterCount() == 1
                    && handle.type().parameterType(0) == MemorySegment.class;
        } catch (ReflectiveOperationException error) {
            return false;
        }
    }

    public static void main(String[] args) throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            wireDispatchTest(arena);
            activationForeignWireTest(arena);
            MemorySegment app = Pt.app_create("pt.tests.java");
            check(app.address() != 0, "app_create");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_light);
            Pt.app_set_accent_color(app, Pt.rgb(0x4C9AFF));
            Pt.app_set_font_family(app, "monospace");
            Pt.app_set_reduced_motion(app, false);
            check(!Pt.app_get_reduced_motion(app), "reduced motion off");
            Pt.app_set_reduced_motion(app, true);
            check(Pt.app_get_reduced_motion(app), "reduced motion on");
            Pt.app_set_reduced_motion(app, false);
            check(Pt.app_get_a11y_status(app) == Pt.a11y_status_disabled, "a11y disabled");
            boolean a11y_enabled = Pt.app_enable_a11y(app);
            check(a11y_enabled == (Pt.app_get_a11y_status(app) == Pt.a11y_status_active),
                    "a11y enable status");
            check(Pt.app_get_a11y_status(app) != Pt.a11y_status_disabled, "a11y started");
            Pt.app_disable_a11y(app);
            check(Pt.app_get_a11y_status(app) == Pt.a11y_status_disabled, "a11y stopped");
            check(Math.abs(Pt.ease(Pt.easing_linear, 0.5f) - 0.5f) < 0.0001f, "linear ease");
            check(Math.abs(Pt.ease_value(Pt.easing_linear, 10.0f, 30.0f, 0.25f) - 15.0f)
                    < 0.0001f, "ease value");
            check(Pt.app_get_theme_color(app, Pt.theme_accent) == Pt.rgb(0x4C9AFF),
                    "theme accent");
            check(Pt.app_get_theme_color(app, Pt.theme_control)
                    != Pt.app_get_theme_color(app, Pt.theme_track), "theme tokens");

            MemorySegment window = Pt.window_create(app, "java binding", 420, 320);
            check(window.address() != 0, "window_create");
            check(Pt.window_get_width(window) == 420, "window width");
            check(Pt.window_get_height(window) == 320, "window height");
            check(Math.abs(Pt.app_get_theme_metric(app, Pt.theme_metric_radius_small)
                    - 6.0f) < 0.001f, "radius metric");
            Pt.app_set_theme_metric(app, Pt.theme_metric_chip_height, 30.0f);
            check(Math.abs(Pt.app_get_theme_metric(app, Pt.theme_metric_chip_height)
                    - 30.0f) < 0.001f, "app metric override");
            Pt.window_set_theme_color(window, Pt.theme_background, 0x102030FF);
            Pt.window_set_theme_metric(window, Pt.theme_metric_focus_width, 3.0f);
            check(Pt.window_get_theme_color(window, Pt.theme_background) == 0x102030FF,
                    "window color override");
            check(Math.abs(Pt.window_get_theme_metric(window, Pt.theme_metric_focus_width)
                    - 3.0f) < 0.001f, "window metric override");
            check(Pt.app_get_theme_color(app, Pt.theme_background) != 0x102030FF,
                    "window override isolation");
            Pt.window_clear_theme_overrides(window);
            check(Pt.window_get_theme_color(window, Pt.theme_background)
                    == Pt.app_get_theme_color(app, Pt.theme_background),
                    "window override clear");

            MemorySegment root = Pt.window_root(window);
            check(root.address() != 0, "window root");
            MemorySegment column = Pt.column(root);
            Pt.widget_set_gap(column, 8.0f);
            Pt.widget_set_padding(column, 12.0f, 12.0f, 12.0f, 12.0f);

            /* ---- 2.0 direction properties: zero defaults and storage ---- */
            check(Pt.ui_direction_inherit == 0 && Pt.ui_direction_ltr == 1
                    && Pt.ui_direction_rtl == 2, "ui direction constants");
            check(Pt.text_direction_auto == 0 && Pt.text_direction_ltr == 1
                    && Pt.text_direction_rtl == 2, "text direction constants");
            MemorySegment directionLabel = Pt.label(column, "direction");
            check(Pt.app_get_ui_direction(app) == Pt.ui_direction_inherit,
                    "app direction default");
            check(Pt.window_get_ui_direction(window) == Pt.ui_direction_inherit,
                    "window direction default");
            check(Pt.widget_get_ui_direction(column) == Pt.ui_direction_inherit,
                    "widget direction default");
            check(Pt.widget_get_text_direction(directionLabel) == Pt.text_direction_auto,
                    "text direction default");
            /* 2.1 logical padding: the wrapper binds and runs; the mapping is
             * asserted in tests/test_layout.c. */
            MemorySegment logicalCard = Pt.column(root);
            Pt.widget_set_width(logicalCard, 200.0f);
            Pt.widget_set_height(logicalCard, 80.0f);
            Pt.widget_set_padding_logical(logicalCard, 4.0f, 16.0f, 6.0f, 8.0f);
            Pt.widget_set_ui_direction(logicalCard, Pt.ui_direction_rtl);
            Pt.widget_set_padding(logicalCard, 1.0f, 2.0f, 3.0f, 4.0f);

            Pt.app_set_ui_direction(app, Pt.ui_direction_rtl);
            check(Pt.app_get_ui_direction(app) == Pt.ui_direction_rtl,
                    "app direction stored");
            Pt.window_set_ui_direction(window, Pt.ui_direction_ltr);
            check(Pt.window_get_ui_direction(window) == Pt.ui_direction_ltr,
                    "window direction stored");
            Pt.widget_set_ui_direction(column, Pt.ui_direction_rtl);
            Pt.widget_set_text_direction(column, Pt.text_direction_rtl);
            check(Pt.widget_get_ui_direction(column) == Pt.ui_direction_rtl,
                    "widget direction stored");
            check(Pt.widget_get_text_direction(column) == Pt.text_direction_rtl,
                    "widget text direction stored");
            Pt.widget_set_ui_direction(directionLabel, Pt.ui_direction_ltr);
            Pt.widget_set_text_direction(directionLabel, Pt.text_direction_ltr);
            check(Pt.widget_get_ui_direction(directionLabel) == Pt.ui_direction_ltr,
                    "label direction stored");
            check(Pt.widget_get_text_direction(directionLabel) == Pt.text_direction_ltr,
                    "label text direction stored");
            /* Out-of-range values are rejected by the C entry point. */
            Pt.app_set_ui_direction(app, 99);
            Pt.window_set_ui_direction(window, 99);
            Pt.widget_set_text_direction(column, 99);
            check(Pt.app_get_ui_direction(app) == Pt.ui_direction_rtl,
                    "app direction rejects invalid");
            check(Pt.window_get_ui_direction(window) == Pt.ui_direction_ltr,
                    "window direction rejects invalid");
            check(Pt.widget_get_text_direction(column) == Pt.text_direction_rtl,
                    "text direction rejects invalid");
            Pt.app_set_ui_direction(app, Pt.ui_direction_inherit);
            Pt.window_set_ui_direction(window, Pt.ui_direction_inherit);
            Pt.widget_set_ui_direction(column, Pt.ui_direction_inherit);
            Pt.widget_set_text_direction(column, Pt.text_direction_auto);
            Pt.widget_set_ui_direction(directionLabel, Pt.ui_direction_inherit);
            Pt.widget_set_text_direction(directionLabel, Pt.text_direction_auto);
            check(Pt.app_get_ui_direction(app) == Pt.ui_direction_inherit,
                    "app direction reset");

            MemorySegment label = Pt.label(column, "hello");
            check(Pt.widget_get_text(label).equals("hello"), "label text");
            Pt.widget_set_text(label, "changed");
            check(Pt.widget_get_text(label).equals("changed"), "label set_text");
            Pt.widget_set_font_size(label, 18.0f);
            Pt.widget_set_font_weight(label, Pt.font_weight_bold);
            Pt.widget_set_text(label, "styled label");
            Pt.TextSpan[] spans = {new Pt.TextSpan(0, 6, Pt.font_weight_bold,
                    Pt.font_slant_italic, 0xFF2288FF,
                    Pt.text_span_underline | Pt.text_span_has_weight
                            | Pt.text_span_has_slant | Pt.text_span_has_color
                            | Pt.text_span_has_family | Pt.text_span_has_size,
                    "DejaVu Serif", 24.0f)};
            check(Pt.widget_set_text_spans(label, spans), "set text spans");
            check(Pt.widget_get_text_span_count(label) == 1, "text span count");
            Pt.TextSpan stored = Pt.widget_get_text_span(label, 0);
            check(stored != null, "get text span");
            check(stored != null && stored.start() == 0 && stored.length() == 6,
                    "text span range");
            check(stored != null && stored.weight() == Pt.font_weight_bold
                    && stored.slant() == Pt.font_slant_italic, "text span attrs");
            check(stored != null && stored.color() == 0xFF2288FF, "text span color");
            check(stored != null && stored.family().equals("DejaVu Serif")
                    && Math.abs(stored.size() - 24.0f) < 0.0001f, "text span font");
            check(Pt.widget_get_text_span(label, 1) == null, "text span bounds");
            Pt.widget_clear_text_spans(label);
            check(Pt.widget_get_text_span_count(label) == 0, "clear text spans");
            Pt.widget_set_text(label, "changed");

            MemorySegment button = Pt.button(column, "Press");
            Pt.button_set_variant(button, Pt.button_primary);
            Pt.button_set_on_click(button, (widget, data) -> { }, arena);
            Pt.widget_set_busy(button, true);
            check(Pt.widget_get_busy(button), "widget busy");
            Pt.widget_set_busy(button, false);
            check(!Pt.widget_get_busy(button), "widget idle");
            Pt.widget_set_invalid(button, true);
            check(Pt.widget_get_invalid(button), "widget invalid");
            Pt.widget_set_invalid(button, false);
            check(!Pt.widget_get_invalid(button), "widget valid");

            MemorySegment chip = Pt.chip(column, "filter");
            Pt.chip_set_removable(chip, true);
            Pt.chip_set_selected(chip, true);
            Pt.chip_set_icon(chip, Pt.icon_check);
            check(Pt.chip_get_removable(chip) && Pt.chip_get_selected(chip), "chip state");
            Pt.chip_set_on_click(chip, (widget, data) -> { }, arena);
            Pt.chip_set_on_remove(chip, (widget, data) -> { }, arena);
            check(Pt.skeleton(column).address() != 0, "skeleton");

            MemorySegment edit_table = Pt.table(column);
            Pt.table_set_column_count(edit_table, 3);
            Pt.table_set_column_title(edit_table, 0, "Name");
            Pt.table_set_row_count(edit_table, 5);
            final int[] edit = {-1, -1};
            final String[] edit_text = {""};
            Pt.table_set_on_cell_edited(edit_table, (target, row, column_index, text, data) -> {
                edit[0] = row;
                edit[1] = column_index;
                edit_text[0] = text;
            }, arena);
            Pt.table_set_editable(edit_table, true);
            check(Pt.table_begin_edit(edit_table, 1, 0), "begin cell edit");
            check(Pt.table_is_editing(edit_table), "table editing");
            check(Pt.table_commit_edit(edit_table), "commit cell edit");
            check(!Pt.table_is_editing(edit_table), "table edit finished");
            check(edit[0] == 1 && edit[1] == 0, "cell edited callback");
            Pt.table_select_range(edit_table, 1, 3);
            check(Pt.table_get_selection_count(edit_table) == 3, "selection count");
            int[] selected = Pt.table_get_selection(edit_table);
            check(selected.length == 3 && selected[0] == 1 && selected[2] == 3,
                    "selection rows");
            check(Pt.table_is_row_selected(edit_table, 2), "row selected");
            Pt.table_select_toggle(edit_table, 4);
            check(Pt.table_get_selection_count(edit_table) == 4, "table toggle added");
            check(Pt.table_get_selected(edit_table) == 4, "table primary toggled");
            Pt.table_select_toggle(edit_table, 2);
            check(Pt.table_get_selection_count(edit_table) == 3, "table toggle removed");
            check(!Pt.table_is_row_selected(edit_table, 2), "row deselected");
            Pt.table_clear_selection(edit_table);
            check(Pt.table_get_selection_count(edit_table) == 0, "selection cleared");
            final int[] reorder = {-1, -1};
            Pt.table_set_on_columns_reordered(edit_table, (target, from, to, data) -> {
                reorder[0] = from;
                reorder[1] = to;
            }, arena);
            Pt.table_set_on_row_drag(edit_table, (target, row, data) -> { }, arena);
            Pt.table_set_columns_reorderable(edit_table, true);
            check(Pt.table_get_columns_reorderable(edit_table), "columns reorderable");
            check(Pt.table_move_column(edit_table, 0, 2), "move column");
            check(reorder[0] == 0 && reorder[1] == 2, "reorder callback");

            MemorySegment rename_tree = Pt.tree(column);
            MemorySegment rename_node = Pt.tree_add_node(rename_tree, MemorySegment.NULL, "before");
            Pt.tree_set_renamable(rename_tree, true);
            check(Pt.tree_get_renamable(rename_tree), "tree renamable");
            final String[] renamed = {""};
            Pt.tree_set_on_rename(rename_tree, (target, target_node, text, data) -> {
                renamed[0] = text;
            }, arena);
            check(Pt.tree_begin_rename(rename_tree, rename_node), "begin rename");
            check(Pt.tree_is_renaming(rename_tree), "tree renaming");
            check(Pt.tree_commit_rename(rename_tree), "commit rename");
            check(!Pt.tree_is_renaming(rename_tree), "rename finished");
            check(renamed[0].equals("before"), "rename callback");
            check(!Pt.tree_commit_rename(rename_tree), "commit without editor");
            check(Pt.tree_get_selection_count(rename_tree) == 1, "tree selection count");
            check(Pt.tree_is_selected(rename_tree, rename_node), "tree node selected");
            check(Pt.tree_get_selection(rename_tree).length == 1, "tree selection list");
            Pt.tree_select_toggle(rename_tree, rename_node);
            check(Pt.tree_get_selection_count(rename_tree) == 0, "tree toggle cleared");
            Pt.tree_select_range(rename_tree, 0, 0);
            check(Pt.tree_get_selection_count(rename_tree) == 1, "tree range selected");
            Pt.tree_clear_selection(rename_tree);
            check(Pt.tree_get_selection_count(rename_tree) == 0, "tree selection cleared");
            check(Pt.tree_get_selected(rename_tree).address() == 0, "tree primary cleared");

            MemorySegment entry = Pt.entry(column, "placeholder");
            Pt.entry_set_password(entry, true);
            Pt.entry_set_on_change(entry, (widget, text, data) -> { }, arena);
            Pt.widget_set_input_purpose(entry, Pt.input_purpose_email);
            check(Pt.widget_get_input_purpose(entry) == Pt.input_purpose_email,
                    "input purpose");

            MemorySegment toggle = Pt.toggle(column);
            Pt.toggle_set_checked(toggle, true);
            check(Pt.toggle_get_checked(toggle), "toggle checked");
            Pt.toggle_set_checked(toggle, false);
            check(!Pt.toggle_get_checked(toggle), "toggle unchecked");

            MemorySegment slider = Pt.slider(column);
            Pt.slider_set_value(slider, 0.375f);
            check(Math.abs(Pt.slider_get_value(slider) - 0.375f) < 0.0001f, "slider value");

            MemorySegment checkbox = Pt.checkbox(column, "Subscribe");
            Pt.widget_set_checked(checkbox, true);
            check(Pt.widget_get_checked(checkbox), "checkbox checked");
            Pt.widget_set_on_checked(checkbox, (widget, value, data) -> { }, arena);

            MemorySegment radio = Pt.radio(column, "Comfort");
            MemorySegment other = Pt.radio(column, "Compact");
            Pt.widget_set_checked(radio, true);
            check(Pt.widget_get_checked(radio), "radio checked");
            Pt.widget_set_checked(other, true);
            check(!Pt.widget_get_checked(radio), "radio exclusivity");

            MemorySegment progress = Pt.progress(column);
            Pt.progress_set_value(progress, 0.625f);
            check(Math.abs(Pt.progress_get_value(progress) - 0.625f) < 0.0001f,
                    "progress value");
            Pt.progress_set_indeterminate(progress, true);
            check(Pt.progress_get_indeterminate(progress), "progress indeterminate");
            check(Pt.spinner(column).address() != 0, "spinner");

            MemorySegment segmented = Pt.segmented(column,
                    new String[] {"Dense", "Cozy", "Compact"});
            Pt.segmented_set_selected(segmented, 1);
            check(Pt.segmented_get_selected(segmented) == 1, "segmented selected");
            Pt.segmented_set_on_change(segmented, (widget, index, data) -> { }, arena);

            Pt.widget_set_text(entry, "alpha beta gamma");
            Pt.entry_select_all(entry);
            int[] selection = Pt.entry_get_selection(entry);
            check(selection != null && selection[0] == 0 && selection[1] == 16,
                    "entry select all");
            Pt.entry_set_selection(entry, 10, 6);
            selection = Pt.entry_get_selection(entry);
            check(selection != null && selection[0] == 6 && selection[1] == 10,
                    "entry selection order");
            check(!Pt.entry_undo(entry), "entry undo empty");
            check(!Pt.entry_redo(entry), "entry redo empty");

            check(Pt.entry_get_drag_enabled(entry), "entry drag enabled by default");
            Pt.entry_set_drag_enabled(entry, false);
            check(!Pt.entry_get_drag_enabled(entry), "entry drag disabled");
            Pt.entry_set_drag_enabled(entry, true);
            check(!Pt.widget_get_drop_hover(entry), "drop hover idle");
            Pt.widget_set_drop_target(entry, (widget, mime, data) -> true,
                    (widget, x, y, data) -> { }, (widget, data) -> { },
                    (widget, mime, text, x, y, data) -> { }, arena);
            Pt.widget_clear_drop_target(entry);

            Pt.window_set_gesture_handler(window, (w, event, data) -> { }, arena);
            Pt.window_set_long_press_delay(window, 320);
            check(Pt.window_get_momentum_scrolling(window), "momentum default");
            Pt.window_set_momentum_scrolling(window, false);
            check(!Pt.window_get_momentum_scrolling(window), "momentum off");
            Pt.window_set_momentum_scrolling(window, true);
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/plain;charset=utf-8", "dragged", 0L)}),
                    "drag source headless");
            check(Pt.window_get_popup_policy(window) == Pt.popup_policy_auto,
                    "popup policy default");
            Pt.window_set_popup_policy(window, Pt.popup_policy_window);
            check(Pt.window_get_popup_policy(window) == Pt.popup_policy_window,
                    "popup policy set");
            Pt.window_set_popup_policy(window, Pt.popup_policy_auto);
            check(!Pt.window_get_drag_active(window), "drag idle");
            check(Pt.window_get_drag_timeout(window) == 0, "drag timeout default");
            Pt.window_set_drag_timeout(window, 1500);
            check(Pt.window_get_drag_timeout(window) == 1500, "drag timeout set");
            Pt.window_cancel_drag(window);
            check(!Pt.window_get_drag_active(window), "drag cancel");
            Pt.window_set_drag_timeout(window, 0);

            Pt.widget_set_tooltip(button, "Press me");
            Pt.widget_set_tooltip_delay(button, 0);
            check(Pt.widget_get_a11y_role(label) == Pt.a11y_role_label, "a11y label role");
            check(Pt.widget_get_a11y_name(label).equals("changed"), "a11y label name");
            check((Pt.widget_get_a11y_state(label) & Pt.a11y_state_visible) != 0,
                    "a11y visible state");
            Pt.widget_set_a11y_name(label, "Greeting");
            Pt.widget_set_a11y_description(label, "Title text");
            check(Pt.widget_get_a11y_name(label).equals("Greeting"), "a11y name override");
            check(Pt.widget_get_a11y_description(label).equals("Title text"),
                    "a11y description override");
            Pt.A11yText label_text = Pt.widget_get_a11y_text(label);
            check(label_text != null && label_text.text().equals("changed")
                    && label_text.caret() == 0, "a11y label text");
            check(Pt.widget_get_a11y_text(column) == null, "a11y text only for text widgets");
            Pt.entry_set_password(entry, false);
            Pt.A11yText entry_text = Pt.widget_get_a11y_text(entry);
            check(entry_text != null && entry_text.text().equals("alpha beta gamma"),
                    "a11y entry text");
            Pt.entry_set_password(entry, true);
            check(Pt.widget_get_a11y_text(entry) == null, "a11y password hidden");
            check(Pt.widget_get_a11y_value(slider) != null
                    && Math.abs(Pt.widget_get_a11y_value(slider) - 0.375f) < 0.0001f,
                    "a11y slider value");
            Pt.widget_set_checked(checkbox, true);
            check(Pt.widget_get_a11y_value(checkbox) != null
                    && Pt.widget_get_a11y_value(checkbox) > 0.5f, "a11y checkbox value");
            Pt.widget_set_checked(checkbox, false);

            final int[] menu_clicks = {0};
            MemorySegment menu = Pt.menu_create(window);
            Pt.menu_add_item(menu, "Open", "Ctrl+O", (widget, data) -> menu_clicks[0]++, arena);
            Pt.menu_add_separator(menu);
            Pt.menu_add_item(menu, "Quit", null, null, arena);
            Pt.menu_show_at(menu, 20.0f, 20.0f);
            check(Pt.menu_is_open(menu), "menu open");
            Pt.menu_dismiss(menu);
            check(!Pt.menu_is_open(menu), "menu dismissed");
            check(menu_clicks[0] == 0, "menu callback deferred");
            Pt.menu_destroy(menu);

            final int[] closed = {0};
            final int[] action = {0};
            MemorySegment dialog = Pt.dialog_create(window, "Rename", "Choose a label");
            Pt.dialog_add_action(dialog, "Cancel", false, (widget, data) -> action[0]++, arena);
            Pt.dialog_add_action(dialog, "Save", true, null, arena);
            Pt.dialog_set_on_close(dialog, (handle, data) -> closed[0]++, arena);
            Pt.dialog_show(dialog);
            check(Pt.dialog_is_open(dialog), "dialog open");
            Pt.dialog_set_title(dialog, "Rename file");
            Pt.dialog_set_message(dialog, "Pick a name");
            Pt.dialog_close(dialog);
            check(!Pt.dialog_is_open(dialog), "dialog closed");
            check(closed[0] == 1, "dialog close handler");
            check(action[0] == 0, "dialog action untouched");
            Pt.dialog_destroy(dialog);

            MemorySegment toast = Pt.window_show_toast(window, "Saved", Pt.toast_success, 0);
            check(toast.address() != 0 && Pt.toast_is_visible(toast), "toast visible");
            Pt.toast_dismiss(toast);
            check(!Pt.toast_is_visible(toast), "toast dismissed");

            final int[] a11y_events = {0};
            Pt.app_set_a11y_handler(app, (source, widget, event, data) -> {
                if (event == Pt.a11y_event_text_changed) {
                    a11y_events[0]++;
                }
            }, arena);
            Pt.widget_set_text(entry, "logged");
            check(a11y_events[0] == 1, "a11y text event");
            Pt.app_set_a11y_handler(app, null, arena);

            check(Pt.window_clipboard_set(window, "copied text"), "clipboard set");
            check(Pt.window_clipboard_get(window).equals("copied text"), "clipboard get");
            Pt.widget_set_checked(checkbox, false);
            check(!Pt.widget_get_checked(checkbox), "checkbox cleared");

            MemorySegment card = Pt.card(column);
            Pt.widget_set_radius(card, 12.0f);
            Pt.widget_set_background(card, Pt.rgb(0x161D28));
            MemorySegment row = Pt.row(card);
            Pt.box_set_align(row, Pt.align_center);
            Pt.box_set_justify(row, Pt.justify_center);
            Pt.spacer(row);
            Pt.divider(row);
            MemorySegment scroll = Pt.scroll(column);
            Pt.widget_set_height(scroll, 60.0f);
            Pt.scroll_set_offset(scroll, 32.0f);
            check(Pt.scroll_get_offset(scroll) >= 0.0f, "scroll offset");
            check(Pt.scroll_get_max_offset(scroll) >= 0.0f, "scroll max offset");

            MemorySegment notes = Pt.text_area(column, "Notes");
            check(notes.address() != 0, "text area create");
            Pt.widget_set_text(notes, "alpha\nbeta");
            Pt.text_area_set_placeholder(notes, "Write");
            Pt.text_area_set_on_change(notes, (widget, text, data) -> { }, arena);
            Pt.text_area_select_all(notes);
            int[] area_selection = Pt.text_area_get_selection(notes);
            check(area_selection != null && area_selection[0] == 0 && area_selection[1] == 10,
                    "text area select all");
            Pt.text_area_set_selection(notes, 6, 3);
            area_selection = Pt.text_area_get_selection(notes);
            check(area_selection != null && area_selection[0] == 3 && area_selection[1] == 6,
                    "text area selection order");
            check(!Pt.text_area_undo(notes), "text area undo empty");
            check(!Pt.text_area_redo(notes), "text area redo empty");
            Pt.text_area_set_grow_to_content(notes, true);
            check(Pt.text_area_get_grow_to_content(notes), "text area grow");
            Pt.text_area_set_grow_to_content(notes, false);
            Pt.text_area_scroll_to_caret(notes);
            check(Pt.widget_get_a11y_role(notes) == Pt.a11y_role_text_frame,
                    "text area role");

            MemorySegment wrap = Pt.wrap(column);
            Pt.wrap_set_gap(wrap, 8.0f, 6.0f);
            MemorySegment tile = Pt.card(wrap);
            Pt.widget_set_size(tile, 80.0f, 40.0f);
            Pt.widget_set_align_self(tile, Pt.align_start);

            MemorySegment grid = Pt.grid(column, 3);
            Pt.grid_set_gap(grid, 10.0f, 12.0f);
            check(Pt.grid_get_columns(grid) == 3, "grid columns");
            Pt.grid_set_columns(grid, 2);
            check(Pt.grid_get_columns(grid) == 2, "grid columns updated");
            MemorySegment wide = Pt.card(grid);
            Pt.widget_set_grid_span(wide, 2);
            check(Pt.widget_get_grid_span(wide) == 2, "grid span");

            MemorySegment list = Pt.list_view(column);
            Pt.list_view_set_row_height(list, 36.0f);
            check(Math.abs(Pt.list_view_get_row_height(list) - 36.0f) < 0.0001f,
                    "list row height");
            Pt.list_view_set_row_count(list, 50);
            check(Pt.list_view_get_row_count(list) == 50, "list row count");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list no selection");
            Pt.list_view_set_on_bind_row(list, (handle, row_widget, index, data) -> {
            }, arena);
            Pt.list_view_set_on_select(list, (widget, index, data) -> { }, arena);
            Pt.list_view_set_on_activate(list, (widget, index, data) -> { }, arena);
            Pt.list_view_set_selected(list, 7);
            check(Pt.list_view_get_selected(list) == 7, "list selected");
            Pt.list_view_scroll_to_row(list, 49);
            Pt.list_view_refresh(list);
            Pt.list_view_set_row_key_handler(list,
                    (view, row_index, data) -> 1000L + row_index * 10L, arena);
            check(Pt.list_view_get_selected_key(list) == 1070L, "list selected key");
            Pt.list_view_set_row_count(list, 50);
            check(Pt.list_view_get_selected(list) == 7, "list keyed selection preserved");
            Pt.list_view_set_selected(list, 3);
            check(Pt.list_view_get_selected_key(list) == 1030L, "list key follows selection");
            check(!Pt.list_view_set_selected_key(list, 999999L), "list unknown key");
            check(Pt.list_view_get_selected(list) == 3, "list selection unchanged");
            check(Pt.list_view_set_selected_key(list, 1040L), "list select by key");
            check(Pt.list_view_get_selected(list) == 4, "list key row");
            check(Pt.list_view_get_selected_key(list) == 1040L, "list key value");
            Pt.list_view_refresh_rows(list, 0, 4);
            check(Pt.list_view_get_row_count(list) == 50, "list refresh rows");
            check(Pt.list_view_set_selected_key(list, Pt.row_key_none), "list clear key");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list cleared selection");
            check(Pt.list_view_get_selected_key(list) == Pt.row_key_none,
                    "list cleared key");
            check(Pt.list_view_set_selected_key(list, 1030L), "list reselect key");
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list keyed selection cleared");
            Pt.list_view_set_selected(list, 1);
            check(Pt.list_view_get_selected_key(list) == 1010L, "list in-range key");
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == 1, "list in-range key preserved");
            Pt.list_view_set_row_text_handler(list, (view, row_index, data) -> "item " + row_index,
                    arena);
            Pt.list_view_set_selected(list, Pt.list_index_none);
            Pt.list_view_set_row_count(list, 2);
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list selection cleared");
            check(Pt.widget_get_a11y_role(list) == Pt.a11y_role_list, "list role");
            Pt.list_view_set_row_count(list, 8);
            check(Pt.list_view_get_selection_count(list) == 0, "list no multi selection");
            check(Pt.list_view_get_selection(list).length == 0, "list empty selection");
            Pt.list_view_select_range(list, 2, 4);
            check(Pt.list_view_get_selection_count(list) == 3, "list range count");
            int[] list_rows = Pt.list_view_get_selection(list);
            check(list_rows.length == 3 && list_rows[0] == 2 && list_rows[2] == 4,
                    "list range rows");
            check(Pt.list_view_is_row_selected(list, 3), "list row selected");
            check(!Pt.list_view_is_row_selected(list, 5), "list row unselected");
            check(Pt.list_view_get_selected(list) == 2, "list primary row");
            Pt.list_view_select_toggle(list, 6);
            check(Pt.list_view_get_selection(list).length == 4, "list toggled on");
            Pt.list_view_select_toggle(list, 3);
            check(Pt.list_view_get_selection(list).length == 3, "list toggled off");
            Pt.list_view_clear_selection(list);
            check(Pt.list_view_get_selection_count(list) == 0, "list selection cleared");
            check(Pt.list_view_get_selected(list) == Pt.list_index_none,
                    "list primary cleared");

            MemorySegment icon = Pt.icon(column, Pt.icon_search);
            check(Pt.widget_get_icon(icon) == Pt.icon_search, "icon glyph");
            check(Math.abs(Pt.icon_get_size(icon) - 20.0f) < 0.0001f, "icon default size");
            Pt.widget_set_icon(icon, Pt.icon_bell);
            Pt.icon_set_size(icon, 28.0f);
            check(Pt.widget_get_icon(icon) == Pt.icon_bell, "icon set");
            check(Math.abs(Pt.icon_get_size(icon) - 28.0f) < 0.0001f, "icon size set");
            check(Pt.widget_get_a11y_role(icon) == Pt.a11y_role_icon, "icon role");
            MemorySegment icon_button = Pt.icon_button(column, Pt.icon_plus, "Add item");
            check(Pt.widget_get_a11y_name(icon_button).equals("Add item"),
                    "icon button name");
            check(Pt.widget_get_a11y_role(icon_button) == Pt.a11y_role_push_button,
                    "icon button role");

            MemorySegment table = Pt.table(column);
            Pt.widget_set_height(table, 180.0f);
            check(Pt.widget_get_a11y_role(table) == Pt.a11y_role_table, "table role");
            Pt.table_set_column_count(table, 3);
            check(Pt.table_get_column_count(table) == 3, "table column count");
            Pt.table_set_column_title(table, 0, "Name");
            Pt.table_set_column_title(table, 1, "Kind");
            Pt.table_set_column_width(table, 0, 140.0f);
            Pt.table_set_column_align(table, 1, Pt.text_align_center);
            Pt.table_set_column_resizable(table, 2, false);
            check(Math.abs(Pt.table_get_column_width(table, 0) - 140.0f) < 0.0001f,
                    "table column width");
            Pt.table_set_header_height(table, 34.0f);
            Pt.table_set_row_count(table, 40);
            Pt.table_set_row_height(table, 32.0f);
            check(Pt.table_get_row_count(table) == 40, "table row count");
            check(Math.abs(Pt.table_get_row_height(table) - 32.0f) < 0.0001f,
                    "table row height");
            Pt.table_set_on_bind_cell(table, (t, cell, row_index, column_index, data) -> {
            }, arena);
            Pt.table_set_on_select(table, (widget, index, data) -> { }, arena);
            Pt.table_set_on_activate(table, (widget, index, data) -> { }, arena);
            Pt.table_set_on_sort(table, (widget, column_index, direction, data) -> {
            }, arena);
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table no selection");
            Pt.table_set_selected(table, 3);
            check(Pt.table_get_selected(table) == 3, "table selection");
            Pt.table_set_sort(table, 1, Pt.sort_ascending);
            int[] table_sort = Pt.table_get_sort(table);
            check(table_sort[0] == Pt.sort_ascending && table_sort[1] == 1, "table sort");
            check(!Pt.table_get_column_hidden(table, 1), "column visible");
            Pt.table_set_column_hidden(table, 1, true);
            check(Pt.table_get_column_hidden(table, 1), "column hidden");
            Pt.table_set_column_hidden(table, 1, false);
            check(!Pt.table_get_column_hidden(table, 1), "column restored");
            check(!Pt.table_get_column_hidden(table, 7), "column hidden out of range");
            Pt.table_set_on_sort_columns(table, (handle, count, columns, directions, data) -> {
            }, arena);
            Pt.table_set_sort_columns(table, new int[] {2, 0},
                    new int[] {Pt.sort_descending, Pt.sort_ascending});
            int[] sort_keys = Pt.table_get_sort_columns(table);
            check(sort_keys.length == 4 && sort_keys[0] == 2
                    && sort_keys[1] == Pt.sort_descending && sort_keys[2] == 0
                    && sort_keys[3] == Pt.sort_ascending, "table sort columns");
            table_sort = Pt.table_get_sort(table);
            check(table_sort[0] == Pt.sort_descending && table_sort[1] == 2,
                    "table primary sort");
            Pt.table_set_sort_columns(table, new int[] {1, 1, 7},
                    new int[] {Pt.sort_ascending, Pt.sort_descending, Pt.sort_ascending});
            sort_keys = Pt.table_get_sort_columns(table);
            check(sort_keys.length == 2 && sort_keys[0] == 1
                    && sort_keys[1] == Pt.sort_ascending, "table sort filters");
            Pt.table_set_row_key_handler(table,
                    (view, row_index, data) -> 1000L + row_index * 10L, arena);
            check(Pt.table_get_selected(table) == 3, "table selection before keys");
            check(Pt.table_get_selected_key(table) == 1030L, "table selected key");
            Pt.table_set_row_count(table, 40);
            check(Pt.table_get_selected(table) == 3, "table keyed selection preserved");
            Pt.table_set_selected(table, 0);
            check(Pt.table_get_selected_key(table) == 1000L, "table key follows row");
            check(!Pt.table_set_selected_key(table, 999999L), "table unknown key");
            check(Pt.table_get_selected(table) == 0, "table selection unchanged");
            check(Pt.table_set_selected_key(table, 1050L), "table select by key");
            check(Pt.table_get_selected(table) == 5, "table key row");
            check(Pt.table_get_selected_key(table) == 1050L, "table key value");
            check(Pt.table_set_selected_key(table, Pt.row_key_none), "table clear key");
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table cleared selection");
            check(Pt.table_set_selected_key(table, 1010L), "table reselect key");
            Pt.table_refresh_rows(table, 0, 3);
            check(Pt.table_get_row_count(table) == 40, "table refresh rows");
            Pt.table_set_row_text_handler(table, (view, row_index, data) -> "row " + row_index,
                    arena);
            Pt.table_set_selected(table, 3);
            Pt.table_scroll_to_row(table, 39);
            Pt.table_scroll_to_column(table, 2);
            Pt.table_refresh(table);
            Pt.table_set_row_count(table, 2);
            check(Pt.table_get_selected(table) == Pt.list_index_none,
                    "table selection clamped");

            MemorySegment tree = Pt.tree(column);
            check(Pt.widget_get_a11y_role(tree) == Pt.a11y_role_tree, "tree role");
            MemorySegment alpha = Pt.tree_add_node(tree, null, "alpha");
            MemorySegment beta = Pt.tree_add_node(tree, null, "beta");
            MemorySegment leaf = Pt.tree_add_node(tree, alpha, "leaf");
            check(Pt.tree_get_root_count(tree) == 2, "tree roots");
            check(Pt.tree_node_get_text(alpha).equals("alpha"), "tree node text");
            check(Pt.tree_node_has_children(alpha), "tree node children");
            check(Pt.tree_node_get_child_count(alpha) == 1, "tree child count");
            check(Pt.tree_node_get_child(alpha, 0).address() == leaf.address(),
                    "tree child handle");
            check(Pt.tree_node_parent(leaf).address() == alpha.address(), "tree parent");
            check(Pt.tree_node_get_depth(leaf) == 1, "tree depth");
            MemorySegment payload = arena.allocate(4);
            Pt.tree_node_set_data(leaf, payload);
            check(Pt.tree_node_get_data(leaf).address() == payload.address(),
                    "tree node data");
            check(Pt.tree_get_visible_count(tree) == 2, "tree visible collapsed");
            Pt.tree_node_set_expanded(alpha, true);
            check(Pt.tree_node_get_expanded(alpha), "tree expanded");
            check(Pt.tree_get_visible_count(tree) == 3, "tree visible expanded");
            check(Pt.tree_get_visible_node(tree, 1).address() == leaf.address(),
                    "tree visible node");
            Pt.tree_set_on_select(tree, (handle, node, data) -> { }, arena);
            Pt.tree_set_on_activate(tree, (handle, node, data) -> { }, arena);
            Pt.tree_set_selected(tree, leaf);
            check(Pt.tree_get_selected(tree).address() == leaf.address(), "tree selected");
            Pt.tree_set_row_height(tree, 30.0f);
            Pt.tree_set_indent(tree, 16.0f);
            Pt.tree_scroll_to_node(tree, beta);
            Pt.tree_refresh(tree);
            Pt.tree_node_set_text(beta, "beta renamed");
            check(Pt.tree_node_get_text(beta).equals("beta renamed"), "tree rename");
            Pt.tree_clear(tree);
            check(Pt.tree_get_root_count(tree) == 0, "tree cleared");

            MemorySegment tabs = Pt.tabs(column);
            MemorySegment first_page = Pt.tabs_add_page(tabs, "General");
            MemorySegment second_page = Pt.tabs_add_page(tabs, "Advanced");
            check(Pt.tabs_get_page_count(tabs) == 2, "tabs page count");
            check(Pt.tabs_get_page_title(tabs, 0).equals("General"), "tabs title");
            Pt.tabs_set_page_title(tabs, 1, "Options");
            check(Pt.tabs_get_page_title(tabs, 1).equals("Options"), "tabs rename");
            check(Pt.tabs_get_active(tabs) == 0, "tabs active default");
            Pt.tabs_set_active(tabs, 1);
            check(Pt.tabs_get_active(tabs) == 1, "tabs active set");
            Pt.tabs_set_on_change(tabs, (widget, index, data) -> { }, arena);
            Pt.tabs_set_strip_height(tabs, 40.0f);
            check(Pt.tabs_get_page(tabs, 0).address() == first_page.address(),
                    "tabs page handle");
            check(Pt.tabs_get_page(tabs, 2).address() == 0, "tabs out of range");
            Pt.tabs_remove_page(tabs, 0);
            check(Pt.tabs_get_page_count(tabs) == 1, "tabs removed");
            check(Pt.tabs_get_page_title(tabs, 0).equals("Options"), "tabs shift");
            check(Pt.tabs_get_active(tabs) == 0, "tabs active clamped");
            check(second_page.address() != first_page.address(), "tabs pages distinct");

            MemorySegment sidebar = Pt.sidebar(column);
            MemorySegment home = Pt.sidebar_add_item(sidebar, Pt.icon_home, "Home");
            Pt.sidebar_add_item(sidebar, Pt.icon_folder, "Files");
            check(Pt.sidebar_get_item_count(sidebar) == 2, "sidebar count");
            check(Pt.sidebar_get_active(sidebar) == Pt.list_index_none,
                    "sidebar no active");
            Pt.sidebar_set_active(sidebar, 1);
            check(Pt.sidebar_get_active(sidebar) == 1, "sidebar active");
            Pt.sidebar_set_compact(sidebar, true);
            check(Pt.sidebar_get_compact(sidebar), "sidebar compact");
            Pt.sidebar_set_compact(sidebar, false);
            Pt.sidebar_set_header(sidebar, "Workspace");
            check(Pt.sidebar_get_header(sidebar).equals("Workspace"), "sidebar header");
            Pt.sidebar_set_on_select(sidebar, (widget, index, data) -> { }, arena);
            check(Pt.widget_get_a11y_name(home).equals("Home"), "sidebar item name");
            check(Pt.sidebar_get_item(sidebar, 1).address() != 0, "sidebar item handle");

            MemorySegment combo = Pt.combobox(column);
            check(Pt.combobox_get_item_count(combo) == 0, "combo empty");
            check(Pt.combobox_get_selected(combo) == Pt.list_index_none, "combo no choice");
            check(Pt.combobox_get_text(combo).isEmpty(), "combo no text");
            check(Pt.combobox_get_mode(combo) == Pt.combobox_select_only, "combo mode default");
            check(!Pt.combobox_is_open(combo), "combo closed");
            check(Math.abs(Pt.combobox_get_popup_max_height(combo) - 260.0f) < 0.0001f,
                    "combo popup default height");
            Pt.combobox_add_item(combo, "Red");
            Pt.combobox_add_item(combo, "Green");
            Pt.combobox_insert_item(combo, 1, "Yellow");
            check(Pt.combobox_get_item_count(combo) == 3, "combo item count");
            check(Pt.combobox_get_item_text(combo, 1).equals("Yellow"), "combo item text");
            Pt.combobox_set_item_text(combo, 2, "Blue");
            check(Pt.combobox_get_item_text(combo, 2).equals("Blue"), "combo item rename");
            Pt.combobox_set_placeholder(combo, "Colour");
            check(Pt.combobox_get_placeholder(combo).equals("Colour"), "combo placeholder");
            try (Arena combo_box_arena = Arena.ofConfined()) {
                MemorySegment box = combo_box_arena.allocate(ValueLayout.JAVA_INT, 0x1234);
                Pt.combobox_set_item_data(combo, 2, box);
                check(Pt.combobox_get_item_data(combo, 2).address() == box.address(),
                        "combo item data");
                check(Pt.combobox_get_item_data(combo, 0).address() == 0,
                        "combo empty item data");
            }
            check(!Pt.combobox_get_item_disabled(combo, 1), "combo item enabled");
            Pt.combobox_set_item_disabled(combo, 1, true);
            check(Pt.combobox_get_item_disabled(combo, 1), "combo item disabled");
            Pt.combobox_set_item_disabled(combo, 1, false);
            final int[] combo_choice = {Pt.list_index_none};
            final int[] combo_typed = {0};
            Pt.combobox_set_on_change(combo, (widget, index, data) -> combo_choice[0] = index,
                    arena);
            Pt.combobox_set_on_text_change(combo, (widget, text, data) -> {
                if (text.address() != 0) {
                    combo_typed[0] += 1;
                }
            }, arena);
            Pt.combobox_set_selected(combo, 2);
            check(Pt.combobox_get_selected(combo) == 2, "combo selection");
            check(Pt.combobox_get_text(combo).equals("Blue"), "combo text follows selection");
            check(combo_choice[0] == Pt.list_index_none, "combo silent set_selected");
            Pt.combobox_set_mode(combo, Pt.combobox_filterable);
            check(Pt.combobox_get_mode(combo) == Pt.combobox_filterable, "combo filterable");
            Pt.combobox_set_text(combo, "gr");
            check(Pt.combobox_get_text(combo).equals("gr"), "combo typed text");
            check(combo_typed[0] == 0, "combo silent set_text");
            Pt.combobox_open(combo);
            check(Pt.combobox_is_open(combo), "combo popup open");
            Pt.combobox_set_popup_max_height(combo, 120.0f);
            check(Math.abs(Pt.combobox_get_popup_max_height(combo) - 120.0f) < 0.0001f,
                    "combo popup height");
            Pt.combobox_close(combo);
            check(!Pt.combobox_is_open(combo), "combo popup closed");
            check(Pt.widget_get_a11y_role(combo) == Pt.a11y_role_combo_box, "combo role");
            Pt.combobox_remove_item(combo, 0);
            check(Pt.combobox_get_item_count(combo) == 2, "combo item removed");
            check(Pt.combobox_get_item_text(combo, 0).equals("Yellow"), "combo removal shifts");
            Pt.combobox_clear(combo);
            check(Pt.combobox_get_item_count(combo) == 0, "combo cleared");
            check(Pt.combobox_get_selected(combo) == Pt.list_index_none,
                    "combo selection cleared");

            String uri_text = "file:///tmp/a.txt\r\n# comment\r\nfile:///tmp/b%20c.txt\r\n";
            check(Pt.uri_list_count(uri_text) == 2, "uri list count");
            check(Pt.uri_list_get(uri_text, 0).equals("file:///tmp/a.txt"), "uri list get");
            check(Pt.uri_list_get(uri_text, 2) == null, "uri list out of range");
            check(Pt.uri_list(uri_text).length == 2, "uri list parse");
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/uri-list", "file:///tmp/a.txt\r\n", 0L)}),
                    "uri drag headless");
            Pt.window_set_drag_image(window, new int[] {0xFF0000FF, 0x00FF00FF}, 1, 2);
            Pt.window_clear_drag_image(window);
            boolean drag_image_rejected = false;
            try {
                Pt.window_set_drag_image(window, new int[] {0xFF0000FF}, 2, 2);
            } catch (IllegalArgumentException expected) {
                drag_image_rejected = true;
            }
            check(drag_image_rejected, "drag image size validation");
            Pt.window_set_stylus_handler(window, (handle, event, data) -> { }, arena);
            check(Pt.stylus_phase_down == 2 && Pt.stylus_tool_eraser == 1,
                    "stylus constants");
            check(Pt.a11y_state_expandable == 1 << 10, "a11y expandable state");

            check(Pt.drag_action_copy == 1 && Pt.drag_action_move == 2 && Pt.drag_action_ask == 4
                    && (Pt.drag_action_copy | Pt.drag_action_move) == 3, "drag action flags");
            check(Pt.drop_stream_data == 0 && Pt.drop_stream_done == 1
                    && Pt.drop_stream_cancelled == 2 && Pt.drop_stream_error == 3,
                    "drop stream states");
            check(!Pt.window_begin_drag(window,
                    new Pt.DragData[] {new Pt.DragData("text/plain", "structured", 0L)}),
                    "structured drag headless");
            check(!Pt.window_begin_drag(window, new Pt.DragData[0]), "structured drag empty");
            check(!Pt.window_begin_drag(window, new Pt.DragData[] {
                    new Pt.DragData("text/plain", "structured", 10L),
                    new Pt.DragData("text/uri-list", "file:///tmp/a.txt", 0L)}),
                    "structured drag multi type headless");
            check(Pt.window_get_drag_result(window) == Pt.drag_action_none,
                    "drag result idle");
            final int[] finished_action = {Pt.drag_action_none};
            Pt.window_set_drag_finished_handler(window,
                    (handle, dropped, chosen, data) -> finished_action[0] = chosen, arena);
            Pt.window_set_drag_finished_handler(window, null, arena);
            check(finished_action[0] == Pt.drag_action_none, "drag finished untouched");

            check(Pt.drop_offer_mime_count(MemorySegment.NULL) == 0, "offer count null");
            check(Pt.drop_offer_mime(MemorySegment.NULL, 0) == null, "offer mime null");
            check(!Pt.drop_offer_has_mime(MemorySegment.NULL, "text/plain"),
                    "offer has mime null");
            check(!Pt.drop_offer_has_mime(MemorySegment.NULL, null),
                    "offer has mime null text");
            check(Pt.drop_offer_selected_mime(MemorySegment.NULL) == null,
                    "offer selected mime null");
            check(Pt.drop_offer_actions(MemorySegment.NULL) == 0, "offer actions null");
            check(Pt.drop_offer_read(MemorySegment.NULL, "text/plain", 0L,
                    (handle, stream, state, chunk, mime, data) -> { }, arena).address() == 0,
                    "offer read null");
            check(Pt.drop_stream_mime(MemorySegment.NULL) == null, "stream mime null");
            check(Pt.drop_stream_size(MemorySegment.NULL) == 0L, "stream size null");
            check(!Pt.drop_stream_active(MemorySegment.NULL), "stream active null");
            Pt.drop_stream_cancel(MemorySegment.NULL);
            check(!Pt.window_get_drag_active(window), "drag still idle after null streams");

            /* Decoration requests and the maximize capability fallback. */
            check(Pt.window_get_decoration_mode(window) == Pt.decoration_client,
                    "decoration default client");
            check(Pt.window_get_effective_decoration_mode(window) == Pt.decoration_client,
                    "effective decoration default client");
            Pt.window_set_decorated(window, false);
            check(Pt.window_get_decoration_mode(window) == Pt.decoration_client,
                    "set_decorated never requests SSD");
            check(Pt.window_get_effective_decoration_mode(window) == Pt.decoration_client,
                    "effective unaffected by set_decorated");
            Pt.window_set_decorated(window, true);
            Pt.window_set_decoration_mode(window, Pt.decoration_server);
            check(Pt.window_get_decoration_mode(window) == Pt.decoration_server,
                    "decoration server requested");
            check(Pt.window_get_effective_decoration_mode(window) == Pt.decoration_server,
                    "headless effective follows request");
            Pt.window_set_decoration_mode(window, Pt.decoration_auto);
            check(Pt.window_get_decoration_mode(window) == Pt.decoration_auto,
                    "decoration auto requested");
            check(Pt.window_get_effective_decoration_mode(window) == Pt.decoration_client,
                    "auto falls back to client");
            Pt.window_set_decoration_mode(window, 99);
            check(Pt.window_get_decoration_mode(window) == Pt.decoration_auto,
                    "invalid decoration mode ignored");
            check(Pt.window_maximize(window), "maximize default resizable");
            Pt.window_set_resizable(window, false);
            check(!Pt.window_maximize(window), "maximize non-resizable rejected");
            Pt.window_set_resizable(window, true);
            check(Pt.window_maximize(window), "maximize resizable again");
            check(!Pt.window_maximize(MemorySegment.NULL), "maximize null window");
            check(Pt.window_get_decoration_mode(MemorySegment.NULL) == Pt.decoration_auto,
                    "decoration null window");
            check(Pt.window_get_effective_decoration_mode(MemorySegment.NULL)
                    == Pt.decoration_auto, "effective decoration null window");

            /* Offscreen painting drives the custom-paint callback without a
             * compositor, so the whole paint context surface is exercised. */
            MemorySegment offscreen = Pt.window_create(app, "offscreen", 64, 48);
            check(offscreen.address() != 0, "offscreen window");
            final int[] paint_calls = {0};
            final boolean[] paint_contract = {true};
            Pt.widget_set_paint_callback(Pt.window_root(offscreen),
                    (widget, context, data) -> {
                        paint_calls[0] += 1;
                        if (!(Pt.paint_width(context) >= 1.0f)
                                || !(Pt.paint_height(context) >= 1.0f)
                                || !(Pt.paint_scale(context) > 0.0f)) {
                            paint_contract[0] = false;
                        }
                        float[] clip = Pt.paint_clip_rect(context);
                        if (clip != null && (clip[2] <= 0.0f || clip[3] <= 0.0f)) {
                            paint_contract[0] = false;
                        }
                        Pt.paint_push_clip(context, 0.0f, 0.0f, 32.0f, 24.0f);
                        Pt.paint_fill_rect(context, 0.0f, 0.0f, 12.0f, 12.0f,
                                Pt.rgb(0xAA3300));
                        Pt.paint_fill_rounded_rect(context, 12.0f, 0.0f, 12.0f, 12.0f,
                                3.0f, Pt.rgb(0x224466));
                        Pt.paint_stroke_rect(context, 1.0f, 1.0f, 20.0f, 10.0f, 1.0f,
                                Pt.rgb(0xFFFFFF));
                        Pt.paint_draw_line(context, 0.0f, 20.0f, 20.0f, 20.0f, 1.0f,
                                Pt.rgb(0x000000));
                        Pt.paint_draw_text(context, "DejaVu Sans", 10.0f,
                                Pt.font_weight_regular, Pt.font_slant_upright, 2.0f, 14.0f,
                                "Pt", Pt.rgb(0x000000));
                        Pt.paint_font_has_glyph(context, "DejaVu Sans", 10.0f, 0x50);
                        Pt.paint_measure_text(context, "DejaVu Sans", 10.0f,
                                Pt.font_weight_regular, Pt.font_slant_upright, "Pt");
                        Pt.FontMetrics metrics = Pt.paint_font_metrics(context,
                                "DejaVu Sans", 10.0f, Pt.font_weight_regular,
                                Pt.font_slant_upright);
                        if (metrics != null && metrics.line_height() <= 0.0f) {
                            paint_contract[0] = false;
                        }
                        Pt.paint_pop_clip(context);
                    }, arena);
            int[] pixels = new int[64 * 48];
            check(Pt.window_paint_pixels(offscreen, pixels, 64, 48), "offscreen paint");
            check(paint_calls[0] == 1, "paint callback dispatch");
            check(paint_contract[0], "paint context contract");
            int mark = Pt.rgb(0xAA3300);
            int expected = ((mark & 0xFF) << 24) | ((mark >> 8) & 0xFFFFFF);
            boolean marked = false;
            for (int pixel : pixels) {
                if (pixel == expected) {
                    marked = true;
                    break;
                }
            }
            check(marked, "paint pixels landed");
            check(!Pt.window_paint_pixels(MemorySegment.NULL, pixels, 64, 48),
                    "paint null window");
            check(!Pt.window_paint_pixels(offscreen, pixels, 0, 48), "paint invalid width");
            check(!Pt.window_paint_pixels(offscreen, pixels, 64, -1), "paint invalid height");
            boolean short_buffer_rejected = false;
            try {
                Pt.window_paint_pixels(offscreen, new int[1], 64, 48);
            } catch (IllegalArgumentException expected_error) {
                short_buffer_rejected = true;
            }
            check(short_buffer_rejected, "paint buffer validation");

            check(Pt.paint_pixels_desc_layout.byteSize() == 24, "paint descriptor layout");
            check(Pt.PaintPixelsDesc.class.isRecord(), "paint descriptor is a record");
            int[] tightPixels = new int[64 * 48];
            check(Pt.window_paint_pixels_ex(offscreen, tightPixels, 64, 48),
                    "paint ex default");
            int[] paddedPixels = new int[64 * 48];
            check(Pt.window_paint_pixels_ex(offscreen, paddedPixels, 64, 48,
                    new Pt.PaintPixelsDesc(80, 0.0f)), "paint ex stride");
            check(java.util.Arrays.equals(tightPixels, paddedPixels),
                    "paint ex stride rows match tight snapshot");
            int[] scaledPixels = new int[128 * 96];
            check(Pt.window_paint_pixels_ex(offscreen, scaledPixels, 128, 96,
                    new Pt.PaintPixelsDesc(0, 2.0f)), "paint ex scale 2");
            boolean scaledMarked = false;
            for (int pixel : scaledPixels) {
                if (pixel == expected) {
                    scaledMarked = true;
                    break;
                }
            }
            check(scaledMarked, "paint ex scale landed");
            check(!Pt.window_paint_pixels_ex(MemorySegment.NULL, tightPixels, 64, 48),
                    "paint ex null window");
            check(!Pt.window_paint_pixels_ex(offscreen, tightPixels, 0, 48),
                    "paint ex invalid width");
            boolean ex_format_rejected = false;
            try {
                Pt.window_paint_pixels_ex(offscreen, tightPixels, 64, 48,
                        new Pt.PaintPixelsDesc(0, 0.0f, 1, new int[3]));
            } catch (IllegalArgumentException expectedError) {
                ex_format_rejected = true;
            }
            check(ex_format_rejected, "paint ex format validation");
            boolean ex_reserved_rejected = false;
            try {
                Pt.window_paint_pixels_ex(offscreen, tightPixels, 64, 48,
                        new Pt.PaintPixelsDesc(0, 0.0f, 0, new int[] {1, 0, 0}));
            } catch (IllegalArgumentException expectedError) {
                ex_reserved_rejected = true;
            }
            check(ex_reserved_rejected, "paint ex reserved validation");
            boolean ex_stride_rejected = false;
            try {
                Pt.window_paint_pixels_ex(offscreen, tightPixels, 64, 48,
                        new Pt.PaintPixelsDesc(63, 0.0f));
            } catch (IllegalArgumentException expectedError) {
                ex_stride_rejected = true;
            }
            check(ex_stride_rejected, "paint ex stride validation");
            boolean ex_scale_rejected = false;
            try {
                Pt.window_paint_pixels_ex(offscreen, tightPixels, 64, 48,
                        new Pt.PaintPixelsDesc(0, -1.0f));
            } catch (IllegalArgumentException expectedError) {
                ex_scale_rejected = true;
            }
            check(ex_scale_rejected, "paint ex scale validation");
            boolean ex_short_rejected = false;
            try {
                Pt.window_paint_pixels_ex(offscreen, new int[1], 64, 48);
            } catch (IllegalArgumentException expectedError) {
                ex_short_rejected = true;
            }
            check(ex_short_rejected, "paint ex buffer validation");
            int callsBeforeClear = paint_calls[0];

            Pt.widget_request_redraw(Pt.window_root(offscreen));
            Pt.widget_request_redraw_rect(Pt.window_root(offscreen), 0.0f, 0.0f, 4.0f, 4.0f);
            Pt.widget_clear_paint_callback(Pt.window_root(offscreen));
            check(Pt.window_paint_pixels(offscreen, pixels, 64, 48), "paint after clear");
            check(paint_calls[0] == callsBeforeClear, "paint callback cleared");

            /* Primary selection mirrors the clipboard ownership contract. */
            check(Pt.window_primary_get(window).isEmpty(), "primary empty");
            // A programmatic set has no triggering event serial: it publishes
            // nothing and reports false. The explicit serial path is used
            // from the pointer-release callback in a real application.
            check(!Pt.window_primary_set(window, "middle-click text"),
                    "programmatic primary rejected");
            check(Pt.window_primary_get(window).isEmpty(), "primary untouched");
            check(!Pt.window_primary_set_serial(window, "middle-click text", 0),
                    "primary zero serial rejected");
            check(Pt.window_primary_set_serial(window, "middle-click text", 7),
                    "primary serial set");
            check(Pt.window_primary_get(window).equals("middle-click text"), "primary get");
            check(Pt.window_primary_set_serial(window, "", 8), "primary clear");
            check(Pt.window_primary_get(window).isEmpty(), "primary cleared");
            check(!Pt.window_primary_set(MemorySegment.NULL, "no window"),
                    "primary null window");
            check(!Pt.window_primary_set_serial(MemorySegment.NULL, "no window", 9),
                    "primary null window serial");
            check(Pt.window_primary_get(MemorySegment.NULL).isEmpty(), "primary null get");

            /* Input callbacks have no public injection entry point; assert that
             * registration, replacement and the headless event loop keep them
             * idle instead of firing spuriously. */
            final int[] focus_calls = {0};
            final int[] key_event_calls = {0};
            final int[] pointer_calls = {0};
            final int[] ime_commits = {0};
            final int[] ime_preedits = {0};
            final int[] ime_bounds = {0, 0};
            final String[] ime_text = {null};
            Pt.window_set_focus_handler(window, (target, focused, data) -> focus_calls[0] += 1,
                    arena);
            Pt.window_set_key_event_handler(window, (target, key, modifiers, utf8, data) -> {
                key_event_calls[0] += 1;
                return false;
            }, arena);
            Pt.window_set_pointer_handler(window, (target, event, data) -> {
                pointer_calls[0] += 1;
                return false;
            }, arena);
            Pt.window_set_ime_callbacks(window, (target, text, data) -> ime_commits[0] += 1,
                    (target, text, begin, end, data) -> {
                        ime_preedits[0] += 1;
                        ime_text[0] = text;
                        ime_bounds[0] = begin;
                        ime_bounds[1] = end;
                    }, arena);
            Pt.window_set_ime_enabled(window, true);
            Pt.window_set_ime_cursor_rect(window, 4, 6, 8, 18);
            Pt.window_set_ime_enabled(window, false);
            check(ime_preedits[0] == 1 && "".equals(ime_text[0]) && ime_bounds[0] == -1
                    && ime_bounds[1] == -1, "ime disable dispatches empty preedit");
            Pt.window_set_ime_cursor_rect(window, 0, 0, 0, 0);
            Pt.window_set_focus_handler(window, null, arena);
            Pt.window_set_key_event_handler(window, null, arena);
            Pt.window_set_pointer_handler(window, null, arena);
            Pt.window_set_ime_callbacks(window, null, null, arena);
            check(Pt.pointer_phase_enter == 0 && Pt.pointer_phase_down == 3
                    && Pt.pointer_phase_cancel == 6, "pointer phase constants");

            /* Input panel requests report desired-state changes; a NULL window
             * has no recorded state. The handles themselves pin the FFM
             * descriptor (boolean result, one pointer). */
            check(Pt.window_show_input_panel(window), "show input panel first");
            check(!Pt.window_show_input_panel(window), "show input panel repeat");
            check(Pt.window_hide_input_panel(window), "hide input panel first");
            check(!Pt.window_hide_input_panel(window), "hide input panel repeat");
            check(!Pt.window_show_input_panel(MemorySegment.NULL), "show input panel null");
            check(!Pt.window_hide_input_panel(MemorySegment.NULL), "hide input panel null");
            check(descriptorPinned("window_show_input_panel"),
                    "show input panel descriptor");
            check(descriptorPinned("window_hide_input_panel"),
                    "hide input panel descriptor");
            check(Pt.a11y_role_spin_button == 36, "a11y spin button role");

            /* A real pipe proves the fd watch dispatches through the poll loop. */
            check(Pt.fd_event_read == 1 && Pt.fd_event_write == 2
                    && Pt.fd_event_error == 4 && Pt.fd_event_hangup == 8,
                    "fd event constants");
            int[] descriptors = nativePipe();
            check(descriptors != null, "native pipe");
            if (descriptors != null) {
                final int[] fd_events = {0};
                long watch = Pt.app_watch_fd(app, descriptors[0], Pt.fd_event_read,
                        (source, events, data) -> fd_events[0] = events, arena);
                check(watch != 0, "fd watch id");
                check(Pt.app_watch_fd(app, descriptors[0], Pt.fd_event_read,
                        (source, events, data) -> { }, arena) == 0,
                        "duplicate fd watch rejected");
                check(Pt.app_watch_fd(app, -1, Pt.fd_event_read,
                        (source, events, data) -> { }, arena) == 0, "fd watch invalid fd");
                check(nativeWrite(descriptors[1], (byte) 1), "fd pipe write");
                check(Pt.app_run_for(app, 80) == 0, "fd watch run_for");
                check((fd_events[0] & Pt.fd_event_read) != 0, "fd callback dispatch");
                Pt.app_unwatch_fd(app, watch);
                Pt.app_unwatch_fd(app, watch);

                /* An owned watch closes the descriptor on unwatch. */
                int[] ownedDescriptors = nativePipe();
                check(ownedDescriptors != null, "owned native pipe");
                if (ownedDescriptors != null) {
                    final int[] owned_events = {0};
                    long ownedWatch = Pt.app_watch_fd_owned(app, ownedDescriptors[0],
                            Pt.fd_event_read,
                            (source, events, data) -> owned_events[0] = events, arena);
                    check(ownedWatch != 0, "owned fd watch id");
                    check(Pt.app_watch_fd(app, ownedDescriptors[0], Pt.fd_event_read,
                            (source, events, data) -> { }, arena) == 0,
                            "owned fd duplicate rejected");
                    check(descriptorOpen(ownedDescriptors[0]), "owned fd open");
                    check(nativeWrite(ownedDescriptors[1], (byte) 2), "owned pipe write");
                    check(Pt.app_run_for(app, 80) == 0, "owned watch run_for");
                    check((owned_events[0] & Pt.fd_event_read) != 0,
                            "owned fd callback dispatch");
                    Pt.app_unwatch_fd(app, ownedWatch);
                    check(!descriptorOpen(ownedDescriptors[0]), "unwatch closes owned fd");
                }
            }
            check(focus_calls[0] == 0 && key_event_calls[0] == 0 && pointer_calls[0] == 0
                    && ime_commits[0] == 0 && ime_preedits[0] == 1,
                    "input callbacks idle headless");

            boolean offer_target_accepted = true;
            try {
                Pt.widget_set_drop_target(button, null, (widget, x, y, data) -> { },
                        (widget, data) -> { }, null,
                        (widget, offer, data) -> Pt.drop_offer_mime(offer, 0),
                        (widget, offer, mime, x, y, data) -> { }, arena);
                Pt.widget_clear_drop_target(button);
            } catch (RuntimeException | LinkageError error) {
                offer_target_accepted = false;
            }
            check(offer_target_accepted, "structured drop target registers and clears");

            try (Pt.Transition transition = new Pt.Transition(0.0f)) {
                transition.to(10.0f, 100, Pt.easing_linear);
                check(transition.active(), "transition active");
                transition.advance(0.05f);
                check(Math.abs(transition.value() - 5.0f) < 0.01f, "transition midpoint");
                transition.to(20.0f, 100, Pt.easing_linear);
                transition.advance(0.05f);
                check(transition.value() > 5.0f, "transition retarget");
                transition.set(30.0f);
                check(!transition.active()
                        && Math.abs(transition.value() - 30.0f) < 0.0001f,
                        "transition set");
            }

            final boolean[] fired = {false};
            Pt.app_add_timeout(app, 5, (target, data) -> fired[0] = true, arena);
            final boolean[] cancelled = {false};
            long cancel_id = Pt.app_add_timeout(app, 40,
                    (target, data) -> cancelled[0] = true, arena);
            Pt.app_cancel_timeout(app, cancel_id);

            Pt.window_show(window);
            check(Pt.app_run_for(app, 80) == 0, "run_for");
            check(fired[0], "timer fired");
            check(!cancelled[0], "timer cancelled");
            check(Pt.window_is_open(window), "window open");

            /* ---------- 1.2 popups, fallbacks, clipboard, columns, icons ---------- */

            /* Application-owned popups. */
            check(Pt.popup_max_open == 16, "popup max open constant");
            MemorySegment popupContent = Pt.box(MemorySegment.NULL, Pt.direction_column);
            MemorySegment popup = Pt.popup_create(window, popupContent);
            check(popup.address() != 0, "popup create");
            check(!Pt.popup_is_open(popup), "popup starts closed");
            check(Pt.popup_content(popup).address() == popupContent.address(), "popup content");
            check(Pt.popup_get_placement(popup) == Pt.popup_placement_below,
                    "popup placement default");
            check(Pt.popup_show(popup, new Pt.PopupAnchor(100.0f, 80.0f, 40.0f, 20.0f)),
                    "popup show");
            check(Pt.popup_is_open(popup), "popup open");
            check(Pt.popup_show(popup, new Pt.PopupAnchor(120.0f, 90.0f, 0.0f, 0.0f)),
                    "popup re-show reposition");
            check(Pt.popup_reposition(popup, new Pt.PopupAnchor(40.0f, 60.0f, 10.0f, 10.0f)),
                    "popup reposition open");
            Pt.popup_set_placement(popup, Pt.popup_placement_above);
            check(Pt.popup_get_placement(popup) == Pt.popup_placement_above,
                    "popup placement above");
            Pt.popup_set_placement(popup, 99);
            check(Pt.popup_get_placement(popup) == Pt.popup_placement_below,
                    "popup invalid placement falls back");
            final int[] popupDismissals = {0};
            final long[] popupDismissHandle = {0};
            final boolean[] popupClosedInCallback = {false};
            Pt.popup_set_on_dismiss(popup, (target, data) -> {
                popupDismissals[0] += 1;
                popupDismissHandle[0] = target.address();
                popupClosedInCallback[0] = !Pt.popup_is_open(target);
            }, arena);
            Pt.popup_dismiss(popup);
            check(popupDismissals[0] == 1, "popup dismiss callback");
            check(popupDismissHandle[0] == popup.address(), "popup dismiss handle");
            check(popupClosedInCallback[0], "popup closed before callback");
            check(!Pt.popup_is_open(popup), "popup closed");
            Pt.popup_dismiss(popup);
            check(popupDismissals[0] == 1, "popup dismiss closed is silent");
            check(!Pt.popup_reposition(popup, new Pt.PopupAnchor(0.0f, 0.0f, 1.0f, 1.0f)),
                    "popup reposition closed");
            check(!Pt.popup_show(popup, new Pt.PopupAnchor(Float.NaN, 0.0f, 1.0f, 1.0f)),
                    "popup NaN anchor rejected");
            check(!Pt.popup_show(popup, new Pt.PopupAnchor(0.0f, 0.0f, -1.0f, 1.0f)),
                    "popup negative anchor rejected");
            check(!Pt.last_error().isEmpty(), "popup error recorded");
            check(Pt.popup_show(popup, new Pt.PopupAnchor(10.0f, 10.0f, 5.0f, 5.0f)),
                    "popup show after rejection");
            Pt.popup_destroy(popup);
            check(popupDismissals[0] == 1, "popup destroy silent");

            /* A callback stub lives in its caller's arena; clearing it through
             * the public setter before the next dismissal keeps a closed arena
             * out of the native dispatch path. */
            MemorySegment lifetimePopup = Pt.popup_create(window,
                    Pt.box(MemorySegment.NULL, Pt.direction_column));
            final int[] lifetimeDismissals = {0};
            try (Arena callbackArena = Arena.ofConfined()) {
                Pt.popup_set_on_dismiss(lifetimePopup,
                        (target, data) -> lifetimeDismissals[0] += 1, callbackArena);
                check(Pt.popup_show(lifetimePopup,
                        new Pt.PopupAnchor(20.0f, 20.0f, 12.0f, 12.0f)),
                        "popup lifetime show");
                Pt.popup_dismiss(lifetimePopup);
                check(lifetimeDismissals[0] == 1, "popup callback before arena close");
            }
            Pt.popup_set_on_dismiss(lifetimePopup, null, arena);
            check(Pt.popup_show(lifetimePopup,
                    new Pt.PopupAnchor(20.0f, 20.0f, 12.0f, 12.0f)), "popup lifetime re-show");
            Pt.popup_dismiss(lifetimePopup);
            check(lifetimeDismissals[0] == 1, "no callback after arena close");
            Pt.popup_destroy(lifetimePopup);

            /* Open-limit rejection and null tolerance. */
            MemorySegment[] limitPopups = new MemorySegment[Pt.popup_max_open + 1];
            for (int index = 0; index < limitPopups.length; index++) {
                limitPopups[index] = Pt.popup_create(window,
                        Pt.box(MemorySegment.NULL, Pt.direction_column));
                check(limitPopups[index].address() != 0, "popup limit create");
            }
            for (int index = 0; index < Pt.popup_max_open; index++) {
                check(Pt.popup_show(limitPopups[index],
                        new Pt.PopupAnchor(20.0f + index, 20.0f, 8.0f, 8.0f)),
                        "popup limit show");
            }
            check(!Pt.popup_show(limitPopups[Pt.popup_max_open],
                    new Pt.PopupAnchor(20.0f, 20.0f, 8.0f, 8.0f)), "popup limit rejected");
            check(!Pt.popup_is_open(limitPopups[Pt.popup_max_open]), "popup limit stays closed");
            Pt.popup_dismiss(limitPopups[0]);
            check(Pt.popup_show(limitPopups[Pt.popup_max_open],
                    new Pt.PopupAnchor(20.0f, 20.0f, 8.0f, 8.0f)), "popup limit admits after close");
            for (MemorySegment handle : limitPopups) {
                Pt.popup_destroy(handle);
            }
            check(!Pt.popup_is_open(MemorySegment.NULL), "popup null is closed");
            check(Pt.popup_content(MemorySegment.NULL).address() == 0, "popup null content");
            Pt.popup_dismiss(MemorySegment.NULL);
            Pt.popup_destroy(MemorySegment.NULL);

            /* Font fallback chains and per-family overrides (UTF-8 names). */
            Pt.app_set_font_fallbacks(app,
                    new String[] {"Noto Sans CJK SC", "DejaVu Sans", "", "Noto Sans"});
            check(Pt.app_get_font_fallback_count(app) == 3, "fallback blank skipped");
            check(Pt.app_get_font_fallback(app, 0).equals("Noto Sans CJK SC"),
                    "fallback first");
            check(Pt.app_get_font_fallback(app, 2).equals("Noto Sans"), "fallback last");
            check(Pt.app_get_font_fallback(app, 3) == null, "fallback out of range");
            check(Pt.app_get_font_fallback(app, -1) == null, "fallback negative index");
            String[] manyFallbacks = new String[10];
            for (int index = 0; index < manyFallbacks.length; index++) {
                manyFallbacks[index] = "fam" + index;
            }
            Pt.app_set_font_fallbacks(app, manyFallbacks);
            check(Pt.app_get_font_fallback_count(app) == Pt.font_fallback_max_families,
                    "fallback clamp");
            check(Pt.app_get_font_fallback(app, 7).equals("fam7"), "fallback clamped last");
            Pt.app_set_font_fallbacks(app, null);
            check(Pt.app_get_font_fallback_count(app) == 0, "fallback reset");
            String[] unicodeFallback = {"Noto Sans 汉字", "DejaVu Sans"};
            Pt.app_set_font_fallbacks(app, unicodeFallback);
            check(Pt.app_get_font_fallback(app, 0).equals("Noto Sans 汉字"),
                    "fallback UTF-8 round trip");
            Pt.app_set_font_fallbacks_for_family(app, "serif",
                    new String[] {"Liberation Serif", "DejaVu Serif"});
            check(Pt.app_get_font_fallback_count_for_family(app, "serif") == 2,
                    "family fallback count");
            check(Pt.app_get_font_fallback_for_family(app, "serif", 0)
                    .equals("Liberation Serif"), "family fallback first");
            check(Pt.app_get_font_fallback_for_family(app, "serif", 2) == null,
                    "family fallback out of range");
            check(Pt.app_get_font_fallback_count_for_family(app, "monospace") == 0,
                    "family fallback isolation");
            check(Pt.app_get_font_fallback_count(app) == 2, "family leaves app chain");
            Pt.app_set_font_fallbacks_for_family(app, "serif", null);
            check(Pt.app_get_font_fallback_count_for_family(app, "serif") == 0,
                    "family fallback cleared");
            check(Pt.app_font_uncovered_count(app, null, "") == 0, "uncovered empty text");
            check(Pt.app_font_uncovered_count(app, null, "\u0378") == 1,
                    "uncovered unassigned codepoint");
            check(Pt.app_font_uncovered_count(app, "sans-serif", "\u0378") == 1,
                    "uncovered explicit family");
            Pt.app_set_font_fallbacks(app, null);
            Pt.app_set_font_fallbacks_for_family(app, "serif", null);

            /* Clipboard image and URI-list media. */
            int[] clipboardPixels = {
                0x00000000, 0x00FFFFFF, 0x01FFFFFF, 0x80402010,
                0x7F3F1F0F, 0xFF102030, 0xFFFFFFFF, 0x80FF0000,
            };
            check(Pt.window_clipboard_set_image_png(window, clipboardPixels, 4, 2),
                    "clipboard image set");
            Pt.ClipboardImage decoded = Pt.window_clipboard_get_image_png(window);
            check(decoded != null, "clipboard image get");
            if (decoded != null) {
                int[] clipboardExpected = {
                    0x00000000, 0x00000000, 0x01010101, 0x80402010,
                    0x7F3F1F0F, 0xFF102030, 0xFFFFFFFF, 0x80800000,
                };
                check(decoded.width() == 4 && decoded.height() == 2,
                        "clipboard image dimensions");
                check(decoded.pixels().length == 8, "clipboard image pixel count");
                boolean same = true;
                for (int index = 0; index < clipboardExpected.length; index++) {
                    same &= decoded.pixels()[index] == clipboardExpected[index];
                }
                check(same, "clipboard image premultiplied round trip");
            }
            check(Pt.window_clipboard_get(window).isEmpty(), "image replaces text");
            check(!Pt.window_clipboard_set_image_png(window, clipboardPixels, 0, 1),
                    "clipboard image zero width");
            check(!Pt.window_clipboard_set_image_png(window, clipboardPixels, -1, 1),
                    "clipboard image negative width");
            boolean shortBuffer = false;
            try {
                Pt.window_clipboard_set_image_png(window, clipboardPixels, 4, 3);
            } catch (IllegalArgumentException error) {
                shortBuffer = true;
            }
            check(shortBuffer, "clipboard image short buffer");
            int[] oversized = new int[Pt.clipboard_image_max_dimension + 1];
            check(!Pt.window_clipboard_set_image_png(window, oversized,
                    Pt.clipboard_image_max_dimension + 1, 1), "clipboard image oversized");
            check(Pt.window_clipboard_set_uri_list(window,
                    new String[] {"/tmp/a b.txt", "https://example.test/x"}),
                    "clipboard uri list set");
            check(Pt.window_clipboard_get_image_png(window) == null,
                    "uri list replaces image");
            check(!Pt.window_clipboard_set_uri_list(window, new String[] {"relative.txt"}),
                    "clipboard uri relative rejected");
            check(!Pt.window_clipboard_set_uri_list(window, new String[0]),
                    "clipboard uri empty rejected");
            boolean nullUri = false;
            try {
                Pt.window_clipboard_set_uri_list(window, new String[] {null});
            } catch (NullPointerException error) {
                nullUri = true;
            }
            check(nullUri, "clipboard uri null entry");

            /* Column persistence: keys, round trip, atomic rejection. */
            MemorySegment sourceTable = Pt.table(column);
            Pt.table_set_column_count(sourceTable, 3);
            String[] columnKeys = {"id", "name", "size"};
            for (int index = 0; index < columnKeys.length; index++) {
                Pt.table_set_column_title(sourceTable, index,
                        columnKeys[index].substring(0, 1).toUpperCase()
                                + columnKeys[index].substring(1));
                Pt.table_set_column_key(sourceTable, index, columnKeys[index]);
            }
            check(Pt.table_get_column_key(sourceTable, 1).equals("name"), "column key get");
            check(Pt.table_get_column_key(sourceTable, 9).isEmpty(), "column key out of range");
            Pt.table_set_column_width(sourceTable, 0, 90.0f);
            Pt.table_set_column_width(sourceTable, 1, 220.5f);
            Pt.table_set_column_width(sourceTable, 2, 77.25f);
            Pt.table_set_column_hidden(sourceTable, 2, true);
            Pt.table_set_sort_columns(sourceTable, new int[] {1},
                    new int[] {Pt.sort_descending});
            check(Pt.table_move_column(sourceTable, 0, 2), "column move");
            String blob = Pt.table_columns_save(sourceTable);
            check(blob.startsWith("PTCOLS 1"), "columns save magic");
            check(blob.contains("6e616d65"), "columns save contains key");

            MemorySegment targetTable = Pt.table(column);
            Pt.table_set_column_count(targetTable, 3);
            for (int index = 0; index < columnKeys.length; index++) {
                Pt.table_set_column_key(targetTable, index, columnKeys[index]);
            }
            Pt.table_set_column_width(targetTable, 0, 10.0f);
            check(Pt.table_columns_restore(targetTable, blob), "columns restore");
            check(Pt.table_get_column_key(targetTable, 0).equals("name"),
                    "columns restore order first");
            check(Pt.table_get_column_key(targetTable, 2).equals("id"),
                    "columns restore order last");
            check(Math.abs(Pt.table_get_column_width(targetTable, 0) - 220.5f) < 0.001f,
                    "columns restore width");
            check(Pt.table_get_column_hidden(targetTable, 1), "columns restore hidden");
            check(!Pt.table_get_column_hidden(targetTable, 0), "columns restore visible");
            int[] restoredSort = Pt.table_get_sort_columns(targetTable);
            check(restoredSort.length == 2 && restoredSort[0] == 0
                    && restoredSort[1] == Pt.sort_descending, "columns restore sort");
            check(Pt.table_columns_save(targetTable).equals(blob),
                    "columns save is stable");
            check(!Pt.table_columns_restore(targetTable, "not a layout"),
                    "columns restore rejects malformed");
            check(Math.abs(Pt.table_get_column_width(targetTable, 0) - 220.5f) < 0.001f,
                    "columns restore rejection is atomic");
            MemorySegment shortTable = Pt.table(column);
            Pt.table_set_column_count(shortTable, 2);
            Pt.table_set_column_key(shortTable, 0, "id");
            Pt.table_set_column_key(shortTable, 1, "name");
            check(!Pt.table_columns_restore(shortTable, blob), "columns restore count mismatch");
            MemorySegment keyTable = Pt.table(column);
            Pt.table_set_column_count(keyTable, 2);
            Pt.table_set_column_key(keyTable, 0, "alpha");
            Pt.table_set_column_key(keyTable, 1, "beta");
            Pt.table_set_column_key(keyTable, 1, "alpha");
            check(Pt.table_get_column_key(keyTable, 1).equals("beta"),
                    "column duplicate key rejected");
            String longKey = "x".repeat(70);
            Pt.table_set_column_key(keyTable, 1, longKey);
            check(Pt.table_get_column_key(keyTable, 1).length()
                    == Pt.table_column_key_max, "column key truncation");
            Pt.table_set_column_key(keyTable, 0, "标识");
            check(Pt.table_get_column_key(keyTable, 0).equals("标识"),
                    "column UTF-8 key");
            MemorySegment notTable = Pt.label(column, "not a table");
            check(Pt.table_columns_save(notTable).isEmpty(), "columns save non-table");
            Pt.widget_destroy(sourceTable);
            Pt.widget_destroy(targetTable);
            Pt.widget_destroy(shortTable);
            Pt.widget_destroy(keyTable);

            /* Reference-counted raster icons, button and window slots. */
            int[] iconPixels = new int[16];
            for (int index = 0; index < iconPixels.length; index++) {
                iconPixels[index] = 0x80FF0000 | index;
            }
            byte[] iconBytes = png(4, 4, iconPixels);
            MemorySegment rasterIcon = Pt.icon_image_load_memory(iconBytes);
            check(rasterIcon.address() != 0, "icon load memory");
            check(Pt.icon_image_width(rasterIcon) == 4 && Pt.icon_image_height(rasterIcon) == 4,
                    "icon dimensions");
            MemorySegment iconRef = Pt.icon_image_ref(rasterIcon);
            check(iconRef.address() == rasterIcon.address(), "icon ref identity");
            check(Pt.icon_image_width(MemorySegment.NULL) == 0, "icon null width");
            check(Pt.icon_image_height(MemorySegment.NULL) == 0, "icon null height");
            Pt.icon_image_ref(MemorySegment.NULL);
            Pt.icon_image_destroy(MemorySegment.NULL);
            check(Pt.icon_image_load_memory(new byte[0]).address() == 0,
                    "icon empty bytes rejected");
            check(Pt.icon_image_load_memory(new byte[] {1, 2, 3}).address() == 0,
                    "icon corrupt bytes rejected");
            java.nio.file.Path iconPath = java.nio.file.Files.createTempFile("pt-icon", ".png");
            try {
                java.nio.file.Files.write(iconPath, iconBytes);
                MemorySegment fileIcon = Pt.icon_image_load_file(iconPath.toString());
                check(fileIcon.address() != 0, "icon load file");
                if (fileIcon.address() != 0) {
                    check(Pt.icon_image_width(fileIcon) == 4, "icon file dimensions");
                    Pt.icon_image_destroy(fileIcon);
                }
            } finally {
                java.nio.file.Files.deleteIfExists(iconPath);
            }
            check(Pt.icon_image_load_file("/tmp/pt-definitely-missing-icon.png").address() == 0,
                    "icon missing file rejected");
            check(Pt.icon_image_from_name("pt-definitely-missing", 32).address() == 0,
                    "icon missing name rejected");
            check(Pt.icon_image_from_name("../escape", 32).address() == 0,
                    "icon path name rejected");
            MemorySegment iconButton = Pt.button(column, "Save");
            Pt.button_set_icon_image(iconButton, rasterIcon);
            check(Pt.button_get_icon_image(iconButton).address() == rasterIcon.address(),
                    "button icon slot");
            Pt.button_set_icon_tint(iconButton, 0xFF3366FF);
            Pt.button_set_icon_tint(iconButton, 0);
            Pt.icon_image_destroy(rasterIcon);
            check(Pt.icon_image_width(Pt.button_get_icon_image(iconButton)) == 4,
                    "button keeps icon reference");
            Pt.window_set_icon(window, iconRef);
            check(Pt.window_get_icon(window).address() == iconRef.address(),
                    "window icon slot");
            Pt.icon_image_destroy(iconRef);
            check(Pt.icon_image_width(Pt.window_get_icon(window)) == 4,
                    "window keeps icon reference");
            Pt.button_set_icon_image(iconButton, MemorySegment.NULL);
            check(Pt.button_get_icon_image(iconButton).address() == 0,
                    "button icon cleared");
            Pt.window_set_icon(window, MemorySegment.NULL);
            check(Pt.window_get_icon(window).address() == 0, "window icon cleared");

            /* ---------- 2.3 inline image runs ---------- */
            check(Pt.text_image_align_baseline == 0, "image align baseline enum");
            check(Pt.text_image_align_middle == 1, "image align middle enum");
            Pt.TextImage defaults = new Pt.TextImage(0, 0, 0.0f, 0.0f,
                    Pt.text_image_align_baseline);
            check(defaults.alt().isEmpty(), "image defaults alt empty");
            MemorySegment runIcon = Pt.icon_image_load_memory(iconBytes);
            check(runIcon.address() != 0, "image run icon");

            MemorySegment imageLabel = Pt.label(column, "ab\uFFFCcd");
            Pt.TextImage run = new Pt.TextImage(2, runIcon.address(), 20.0f, 12.0f,
                    Pt.text_image_align_middle, "first");
            check(Pt.widget_set_text_images(imageLabel, new Pt.TextImage[] {run}),
                    "image set accepted");
            check(Pt.widget_get_text_image_count(imageLabel) == 1, "image set count");
            Pt.TextImage storedRun = Pt.widget_get_text_image(imageLabel, 0);
            check(storedRun != null, "image get stored");
            if (storedRun != null) {
                check(storedRun.start() == 2, "image get start");
                check(storedRun.align() == Pt.text_image_align_middle, "image get align");
                check(Math.abs(storedRun.width() - 20.0f) < 0.001f, "image get width");
                check(Math.abs(storedRun.height() - 12.0f) < 0.001f, "image get height");
                check(storedRun.alt().equals("first"), "image get alt");
                check(storedRun.image() == runIcon.address(), "image get borrowed ptr");
            }
            check(Pt.widget_get_text_image(imageLabel, 1) == null, "image get out of range");
            check(Pt.widget_get_text_image(imageLabel, -1) == null, "image get negative");
            check(Pt.widget_set_text_images(imageLabel, new Pt.TextImage[0]),
                    "image set empty accepted");
            check(Pt.widget_get_text_image_count(imageLabel) == 0, "image set empty clears");
            check(Pt.widget_set_text_images(imageLabel, null), "image set null accepted");
            check(Pt.widget_get_text_image_count(imageLabel) == 0, "image set null clears");
            check(Pt.widget_set_text_images(imageLabel, new Pt.TextImage[] {run}),
                    "image re-set");
            Pt.widget_clear_text_images(imageLabel);
            check(Pt.widget_get_text_image_count(imageLabel) == 0, "image clear count");

            /* Unsupported kinds report the touch outcome instead of swallowing. */
            MemorySegment imageButton = Pt.button(column, "Img");
            check(!Pt.widget_set_text_images(imageButton, new Pt.TextImage[] {run}),
                    "image set on button rejected");
            check(Pt.widget_get_text_image_count(imageButton) == 0, "image button stays empty");

            /* NULL image and non-placeholder starts are dropped; duplicates
             * keep the last entry. */
            MemorySegment placeholders = Pt.label(column, "\uFFFC\uFFFC");
            Pt.TextImage good_a = new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "a");
            Pt.TextImage good_b = new Pt.TextImage(3, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "b");
            Pt.TextImage nullImage = new Pt.TextImage(0, 0L, 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "n");
            Pt.TextImage interior = new Pt.TextImage(1, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "c");
            Pt.TextImage offEnd = new Pt.TextImage(6, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "d");
            check(Pt.widget_set_text_images(placeholders,
                    new Pt.TextImage[] {good_b, nullImage, interior, good_a, offEnd}),
                    "image drop set accepted");
            check(Pt.widget_get_text_image_count(placeholders) == 2, "image drop count");
            Pt.TextImage dropped0 = Pt.widget_get_text_image(placeholders, 0);
            Pt.TextImage dropped1 = Pt.widget_get_text_image(placeholders, 1);
            check(dropped0 != null && dropped0.start() == 0 && dropped0.alt().equals("a"),
                    "image drop first");
            check(dropped1 != null && dropped1.start() == 3 && dropped1.alt().equals("b"),
                    "image drop second");
            Pt.TextImage firstWins = new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "first");
            Pt.TextImage lastWins = new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                    Pt.text_image_align_baseline, "last");
            check(Pt.widget_set_text_images(placeholders,
                    new Pt.TextImage[] {firstWins, lastWins}), "image duplicate set");
            check(Pt.widget_get_text_image_count(placeholders) == 1, "image duplicate count");
            check("last".equals(Pt.widget_get_text_image(placeholders, 0).alt()),
                    "image duplicate keeps last");
            check(Pt.widget_set_text_images(placeholders, new Pt.TextImage[] {nullImage}),
                    "image only-null accepted");
            check(Pt.widget_get_text_image_count(placeholders) == 0, "image only-null empty");

            /* Alt truncation walks UTF-8 boundaries (C re-truncates too). */
            MemorySegment altArea = Pt.text_area(column, "\uFFFC");
            Pt.widget_set_text(altArea, "\uFFFC");
            String ascii127 = "a".repeat(127);
            check(Pt.widget_set_text_images(altArea,
                    new Pt.TextImage[] {new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, ascii127)}), "image alt ascii set");
            check(Pt.widget_get_text_image(altArea, 0).alt().length() == 127, "image alt 127");
            check(Pt.widget_set_text_images(altArea,
                    new Pt.TextImage[] {new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "日本")}), "image alt utf8 set");
            check("日本".equals(Pt.widget_get_text_image(altArea, 0).alt()),
                    "image alt utf8 round trip");
            /* 128 ASCII bytes truncate to the 127-byte capacity. */
            check(Pt.widget_set_text_images(altArea,
                    new Pt.TextImage[] {new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "a".repeat(128))}), "image alt 128 set");
            check(Pt.widget_get_text_image(altArea, 0).alt().length() == 127,
                    "image alt 128 truncates");
            /* A multi-byte run straddling the 127-byte limit ends on a
             * character boundary (42 * 3 = 126 bytes, not 127). */
            String wideAlt = "日".repeat(43);
            check(Pt.widget_set_text_images(altArea,
                    new Pt.TextImage[] {new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, wideAlt)}), "image alt wide set");
            String wideStored = Pt.widget_get_text_image(altArea, 0).alt();
            check(wideStored.equals("日".repeat(42)), "image alt wide boundary");

            /* insert_image: offsets, shifting and caller can release its handle. */
            MemorySegment insertArea = Pt.text_area(column, "");
            Pt.widget_set_text(insertArea, "hello");
            Pt.text_area_set_selection(insertArea, 5, 5);
            int firstOffset = Pt.text_area_insert_image(insertArea,
                    new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "icon"));
            check(firstOffset == 5, "image insert offset");
            check(Pt.widget_get_text(insertArea).equals("hello\uFFFC"), "image insert text");
            check(Pt.widget_get_text_image_count(insertArea) == 1, "image insert count");
            check(Pt.widget_get_text_image(insertArea, 0).start() == 5, "image insert start");
            Pt.text_area_set_selection(insertArea, 0, 0);
            int headOffset = Pt.text_area_insert_image(insertArea,
                    new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "head"));
            check(headOffset == 0, "image insert head offset");
            check(Pt.widget_get_text(insertArea).equals("\uFFFChello\uFFFC"),
                    "image insert head text");
            check(Pt.widget_get_text_image_count(insertArea) == 2, "image insert shift count");
            check(Pt.widget_get_text_image(insertArea, 0).start() == 0, "image insert shift 0");
            check(Pt.widget_get_text_image(insertArea, 1).start() == 8, "image insert shift 1");

            /* Rejections: null descriptor, NULL image, wrong kind, caret inside
             * an existing placeholder. */
            check(Pt.text_area_insert_image(insertArea, null) == -1, "image insert null arg");
            check(Pt.text_area_insert_image(insertArea,
                    new Pt.TextImage(0, 0L, 0.0f, 0.0f, Pt.text_image_align_baseline, "x"))
                    == -1, "image insert null image");
            check(Pt.text_area_insert_image(imageLabel,
                    new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "x")) == -1, "image insert wrong kind");
            Pt.text_area_set_selection(insertArea, 1, 1);  /* interior of placeholder */
            String beforeInterior = Pt.widget_get_text(insertArea);
            check(Pt.text_area_insert_image(insertArea,
                    new Pt.TextImage(0, runIcon.address(), 0.0f, 0.0f,
                            Pt.text_image_align_baseline, "x")) == -1, "image insert interior");
            check(Pt.widget_get_text(insertArea).equals(beforeInterior),
                    "image insert interior unchanged");
            /* The widget takes its own reference: releasing the handle keeps
             * the table entry alive. */
            Pt.icon_image_destroy(runIcon);
            check(Pt.widget_get_text_image_count(insertArea) == 2,
                    "image insert survives caller release");
            check(Pt.widget_get_text_image(insertArea, 0).image() != 0,
                    "image insert borrowed still non-null");

            /* Searchable select. */
            MemorySegment select = Pt.select_create(column);
            check(select.address() != 0, "select create");
            check(Pt.select_get_placeholder(select).isEmpty(), "select placeholder default");
            Pt.select_set_placeholder(select, "Pick one");
            check(Pt.select_get_placeholder(select).equals("Pick one"), "select placeholder");
            Pt.select_add_item(select, "Apple");
            Pt.select_add_item(select, "Banana");
            Pt.select_insert_item(select, 1, "Apricot");
            Pt.select_insert_item(select, 99, "Cherry");
            check(Pt.select_get_item_count(select) == 4, "select item count");
            check(Pt.select_get_item_text(select, 1).equals("Apricot"), "select item text");
            check(Pt.select_get_item_text(select, 99).isEmpty(), "select item text out of range");
            check(Pt.select_get_item_data(select, 0).address() == 0, "select item data null");
            try (Arena dataArena = Arena.ofConfined()) {
                MemorySegment selectPayload = dataArena.allocate(ValueLayout.JAVA_INT, 0xCAFE);
                Pt.select_add_item_with_data(select, "Date", selectPayload);
                check(Pt.select_get_item_data(select, 4).address() == selectPayload.address(),
                        "select item data");
                Pt.select_set_item_data(select, 0, selectPayload);
                check(Pt.select_get_item_data(select, 0).address() == selectPayload.address(),
                        "select item data set");
                Pt.select_set_item_data(select, 99, selectPayload);
            }
            check(Pt.select_get_selected(select) == Pt.list_index_none,
                    "select no selection");
            Pt.select_set_selected(select, 2);
            check(Pt.select_get_selected(select) == 2, "select selection");
            Pt.select_set_selected(select, 99);
            check(Pt.select_get_selected(select) == Pt.list_index_none,
                    "select out-of-range clears");
            Pt.select_set_selected(select, 1);
            Pt.select_clear_selection(select);
            check(Pt.select_get_selected(select) == Pt.list_index_none,
                    "select clear selection");
            Pt.select_set_query(select, "ap");
            check(Pt.select_get_query(select).equals("ap"), "select query");
            check(Pt.select_get_match_count(select) == 2, "select match count");
            check(Pt.select_get_match_item(select, 0) == 0, "select match first");
            check(Pt.select_get_match_item(select, 99) == Pt.list_index_none,
                    "select match out of range");
            Pt.select_set_query(select, "");
            check(Pt.select_get_match_count(select) == 5, "select query cleared");
            check(Pt.select_get_max_popup_rows(select) == 8, "select popup rows default");
            Pt.select_set_max_popup_rows(select, 0);
            check(Pt.select_get_max_popup_rows(select) == 1, "select popup rows clamp low");
            Pt.select_set_max_popup_rows(select, 500);
            check(Pt.select_get_max_popup_rows(select) == Pt.picker_max_candidates,
                    "select popup rows clamp high");
            final int[] selectChoice = {Pt.list_index_none};
            Pt.select_set_on_change(select, (widget, index, data) -> selectChoice[0] = index,
                    arena);
            Pt.select_set_selected(select, 2);
            check(selectChoice[0] == Pt.list_index_none, "select programmatic silent");
            Pt.select_open(select);
            check(Pt.select_is_open(select), "select open");
            Pt.select_close(select);
            check(!Pt.select_is_open(select), "select close");
            Pt.select_remove_item(select, 0);
            check(Pt.select_get_item_count(select) == 4, "select remove item");
            Pt.select_remove_item(select, 99);
            check(Pt.select_get_item_count(select) == 4, "select remove out of range");
            Pt.select_clear(select);
            check(Pt.select_get_item_count(select) == 0, "select clear");
            Pt.select_open(select);
            check(!Pt.select_is_open(select), "select open empty model is a no-op");

            /* Date picker and pt_date by value. */
            Pt.PtDate today = Pt.date_today();
            check(!today.isEmpty() && today.year() >= 1 && today.year() <= 9999,
                    "date today");
            check(Pt.date_is_valid(today), "date today valid");
            check(Pt.date_is_valid(new Pt.PtDate(2024, 2, 29)), "date leap day valid");
            check(!Pt.date_is_valid(new Pt.PtDate(2023, 2, 29)), "date non-leap day invalid");
            check(!Pt.date_is_valid(new Pt.PtDate(2024, 13, 1)), "date month invalid");
            check(!Pt.date_is_valid(new Pt.PtDate(2024, 1, 0)), "date day invalid");
            check(!Pt.date_is_valid(Pt.PtDate.empty()), "date empty invalid");
            check(Pt.PtDate.empty().isEmpty() && new Pt.PtDate(2024, 1, 1).isEmpty() == false,
                    "date empty predicate");
            MemorySegment datePicker = Pt.date_picker_create(column);
            check(datePicker.address() != 0, "date picker create");
            check(Pt.date_picker_get_date(datePicker).isEmpty(), "date picker starts empty");
            check(Pt.date_picker_get_text(datePicker).isEmpty(), "date picker empty text");
            check(Pt.date_picker_get_format(datePicker) == Pt.date_format_iso,
                    "date picker default format");
            Pt.PtDate committed = new Pt.PtDate(2025, 12, 31);
            check(Pt.date_picker_set_date(datePicker, committed), "date picker set date");
            check(Pt.date_picker_get_date(datePicker).equals(committed), "date picker date");
            check(Pt.date_picker_get_text(datePicker).equals("2025-12-31"),
                    "date picker ISO text");
            Pt.date_picker_set_format(datePicker, Pt.date_format_dmy);
            check(Pt.date_picker_get_format(datePicker) == Pt.date_format_dmy,
                    "date picker format");
            check(Pt.date_picker_get_text(datePicker).equals("31/12/2025"),
                    "date picker DMY text");
            Pt.date_picker_set_format(datePicker, 99);
            check(Pt.date_picker_get_format(datePicker) == Pt.date_format_iso,
                    "date picker invalid format");
            check(Pt.date_picker_set_text(datePicker, "2026-11-22"), "date picker set ISO text");
            check(Pt.date_picker_get_date(datePicker).equals(new Pt.PtDate(2026, 11, 22)),
                    "date picker parsed text");
            check(!Pt.date_picker_set_text(datePicker, "2026-02-30"),
                    "date picker invalid text rejected");
            check(Pt.date_picker_get_text(datePicker).equals("2026-11-22"),
                    "date picker invalid text keeps value");
            check(!Pt.date_picker_set_date(datePicker, new Pt.PtDate(2026, 2, 30)),
                    "date picker invalid date rejected");
            check(Pt.date_picker_get_date(datePicker).equals(new Pt.PtDate(2026, 11, 22)),
                    "date picker invalid date keeps value");
            check(Pt.date_picker_set_text(datePicker, ""), "date picker empty text clears");
            check(Pt.date_picker_get_date(datePicker).isEmpty(), "date picker cleared by text");
            Pt.date_picker_set_placeholder(datePicker, "Pick a date");
            check(Pt.date_picker_get_placeholder(datePicker).equals("Pick a date"),
                    "date picker placeholder");
            final int[] dateChanges = {0};
            final int[] dateSeen = {0, 0, 0};
            Pt.date_picker_set_on_change(datePicker, (widget, date, data) -> {
                dateChanges[0] += 1;
                dateSeen[0] = date.year();
                dateSeen[1] = date.month();
                dateSeen[2] = date.day();
            }, arena);
            Pt.date_picker_set_date(datePicker, new Pt.PtDate(2027, 1, 2));
            check(dateChanges[0] == 0, "date picker programmatic silent");
            Pt.date_picker_open(datePicker);
            check(Pt.date_picker_is_open(datePicker), "date picker open");
            Pt.date_picker_close(datePicker);
            check(!Pt.date_picker_is_open(datePicker), "date picker close");
            Pt.date_picker_clear(datePicker);
            check(Pt.date_picker_get_date(datePicker).isEmpty(), "date picker clear");

            /* Number picker: clamping, step callback and text parsing. */
            MemorySegment numberPicker = Pt.number_picker_create(column);
            check(numberPicker.address() != 0, "number picker create");
            check(!Pt.number_picker_has_value(numberPicker), "number picker starts empty");
            check(Math.abs(Pt.number_picker_get_min(numberPicker)) < 0.0001
                    && Math.abs(Pt.number_picker_get_max(numberPicker) - 100.0) < 0.0001,
                    "number picker default range");
            check(Math.abs(Pt.number_picker_get_step(numberPicker) - 1.0) < 0.0001,
                    "number picker default step");
            check(Pt.number_picker_get_value(numberPicker) == 0.0
                    && Pt.number_picker_get_text(numberPicker).isEmpty(),
                    "number picker empty value");
            Pt.number_picker_set_range(numberPicker, 0.0, 10.0);
            check(Math.abs(Pt.number_picker_get_max(numberPicker) - 10.0) < 0.0001,
                    "number picker range");
            Pt.number_picker_set_range(numberPicker, 20.0, 10.0);
            check(Math.abs(Pt.number_picker_get_max(numberPicker) - 10.0) < 0.0001,
                    "number picker inverted range rejected");
            Pt.number_picker_set_range(numberPicker, Double.NaN, 10.0);
            check(Math.abs(Pt.number_picker_get_min(numberPicker)) < 0.0001,
                    "number picker NaN range rejected");
            Pt.number_picker_set_value(numberPicker, 20.0);
            check(Math.abs(Pt.number_picker_get_value(numberPicker) - 10.0) < 0.0001,
                    "number picker value clamps");
            check(Pt.number_picker_has_value(numberPicker), "number picker has value");
            check(Pt.number_picker_get_text(numberPicker).equals("10"),
                    "number picker text follows value");
            Pt.number_picker_set_value(numberPicker, Double.NaN);
            check(Math.abs(Pt.number_picker_get_value(numberPicker) - 10.0) < 0.0001,
                    "number picker NaN value ignored");
            Pt.number_picker_set_step(numberPicker, 0.5);
            check(Math.abs(Pt.number_picker_get_step(numberPicker) - 0.5) < 0.0001,
                    "number picker step");
            Pt.number_picker_set_step(numberPicker, 0.0);
            check(Math.abs(Pt.number_picker_get_step(numberPicker) - 0.5) < 0.0001,
                    "number picker zero step rejected");
            final int[] numberChanges = {0};
            final double[] numberSeen = {Double.NaN};
            Pt.number_picker_set_on_change(numberPicker, (widget, value, data) -> {
                numberChanges[0] += 1;
                numberSeen[0] = value;
            }, arena);
            Pt.number_picker_step_by(numberPicker, -2.0);
            check(numberChanges[0] == 1 && Math.abs(numberSeen[0] - 9.0) < 0.0001,
                    "number picker step callback");
            check(Math.abs(Pt.number_picker_get_value(numberPicker) - 9.0) < 0.0001,
                    "number picker step value");
            Pt.number_picker_set_text(numberPicker, "3,5");
            check(Math.abs(Pt.number_picker_get_value(numberPicker) - 3.5) < 0.0001,
                    "number picker comma decimal");
            check(Pt.number_picker_get_text(numberPicker).equals("3.5"),
                    "number picker canonical text");
            check(!Pt.number_picker_set_text(numberPicker, "abc"),
                    "number picker invalid text rejected");
            check(Pt.number_picker_get_text(numberPicker).equals("3.5"),
                    "number picker invalid text keeps value");
            check(Pt.number_picker_set_text(numberPicker, ""), "number picker empty text");
            check(!Pt.number_picker_has_value(numberPicker), "number picker text clears value");
            Pt.number_picker_step_by(numberPicker, 1.0);
            check(Math.abs(Pt.number_picker_get_value(numberPicker) - 0.5) < 0.0001,
                    "number picker step from empty");
            Pt.number_picker_set_placeholder(numberPicker, "Amount");
            check(Pt.number_picker_get_placeholder(numberPicker).equals("Amount"),
                    "number picker placeholder");
            Pt.number_picker_clear(numberPicker);
            check(!Pt.number_picker_has_value(numberPicker)
                    && Pt.number_picker_get_text(numberPicker).isEmpty(),
                    "number picker clear");
            final int[] lifetimeSteps = {0};
            MemorySegment lifetimeNumber = Pt.number_picker_create(column);
            try (Arena callbackArena = Arena.ofConfined()) {
                Pt.number_picker_set_on_change(lifetimeNumber,
                        (widget, value, data) -> lifetimeSteps[0] += 1, callbackArena);
                Pt.number_picker_step_by(lifetimeNumber, 1.0);
                check(lifetimeSteps[0] == 1, "number callback before arena close");
            }
            Pt.number_picker_set_on_change(lifetimeNumber, null, arena);
            Pt.number_picker_step_by(lifetimeNumber, 1.0);
            check(lifetimeSteps[0] == 1, "number no callback after arena close");

            /* Colour picker: hex parsing, presets and silent programmatic paths. */
            MemorySegment colorPicker = Pt.color_picker_create(column);
            check(colorPicker.address() != 0, "color picker create");
            check(Pt.color_picker_get_color(colorPicker) == 0x000000FF,
                    "color picker default black");
            check(Pt.color_picker_get_hex(colorPicker).equals("#000000"),
                    "color picker default hex");
            check(Pt.color_picker_get_preset_count(colorPicker) == 24,
                    "color picker default presets");
            Pt.color_picker_set_color(colorPicker, 0x112233FF);
            check(Pt.color_picker_get_color(colorPicker) == 0x112233FF,
                    "color picker set color");
            check(Pt.color_picker_get_hex(colorPicker).equals("#112233"),
                    "color picker opaque hex");
            check(Pt.color_picker_set_hex(colorPicker, "#10203040"),
                    "color picker set alpha hex");
            check(Pt.color_picker_get_color(colorPicker) == 0x10203040,
                    "color picker alpha hex color");
            check(Pt.color_picker_get_hex(colorPicker).equals("#10203040"),
                    "color picker alpha hex text");
            check(Pt.color_picker_set_hex(colorPicker, "A1B2C3"),
                    "color picker hex without hash");
            check(Pt.color_picker_get_color(colorPicker) == 0xA1B2C3FF,
                    "color picker hex without hash color");
            check(!Pt.color_picker_set_hex(colorPicker, "zzz"),
                    "color picker invalid hex rejected");
            check(Pt.color_picker_get_hex(colorPicker).equals("#A1B2C3"),
                    "color picker invalid hex keeps text");
            Pt.color_picker_add_preset(colorPicker, 0xFF0000FF);
            check(Pt.color_picker_get_preset_count(colorPicker) == 25,
                    "color picker preset add");
            check(Pt.color_picker_get_preset(colorPicker, 24) == 0xFF0000FF,
                    "color picker preset value");
            Pt.color_picker_clear_presets(colorPicker);
            check(Pt.color_picker_get_preset_count(colorPicker) == 0,
                    "color picker presets cleared");
            for (int index = 0; index < Pt.color_max_presets + 1; index++) {
                Pt.color_picker_add_preset(colorPicker, 0x010203FF + index);
            }
            check(Pt.color_picker_get_preset_count(colorPicker) == Pt.color_max_presets,
                    "color picker preset cap");
            check(Pt.color_picker_get_preset(colorPicker, Pt.color_max_presets) == 0,
                    "color picker preset out of range");
            final int[] colorChanges = {0};
            final int[] colorSeen = {0};
            Pt.color_picker_set_on_change(colorPicker, (widget, color, data) -> {
                colorChanges[0] += 1;
                colorSeen[0] = color;
            }, arena);
            Pt.color_picker_set_color(colorPicker, 0x556677FF);
            check(colorChanges[0] == 0, "color picker programmatic silent");
            Pt.color_picker_set_hex(colorPicker, "#010203");
            check(colorChanges[0] == 0, "color picker hex silent");
            Pt.color_picker_open(colorPicker);
            check(Pt.color_picker_is_open(colorPicker), "color picker open");
            Pt.color_picker_close(colorPicker);
            check(!Pt.color_picker_is_open(colorPicker), "color picker close");
            check(Pt.select_get_item_count(MemorySegment.NULL) == 0, "select null count");
            check(!Pt.select_is_open(MemorySegment.NULL), "select null closed");
            check(Pt.date_picker_get_date(MemorySegment.NULL).isEmpty(), "date picker null");
            check(!Pt.number_picker_has_value(MemorySegment.NULL), "number picker null");
            check(Pt.color_picker_get_preset_count(MemorySegment.NULL) == 0,
                    "color picker null");
            Pt.widget_destroy(select);
            Pt.widget_destroy(datePicker);
            Pt.widget_destroy(numberPicker);
            Pt.widget_destroy(lifetimeNumber);
            Pt.widget_destroy(colorPicker);

            /* ---- 2.4 activation tokens and foreign handles (headless) ---- */
            /* The absent-global/headless fallback is documented: every request
             * reports false and never delivers, and the tear-downs are no-ops
             * that may be called any number of times. */
            final int[] tokenCalls = {0};
            Pt.ActivationTokenHandler tokenHandler =
                    (target, token, data) -> tokenCalls[0] += 1;
            check(!Pt.window_request_activation_token(window, null, tokenHandler, arena),
                    "2.4 activation token headless false");
            check(!Pt.window_request_activation_token(window, "pt.tests.target",
                    tokenHandler, arena), "2.4 activation token app id headless false");
            check(!Pt.window_request_activation_token(window, "", tokenHandler, arena),
                    "2.4 activation token empty app id headless false");
            check(!Pt.window_activate(window, "opaque-token"),
                    "2.4 activate headless false");
            check(!Pt.window_activate(window, ""), "2.4 activate empty token rejected");
            check(!Pt.window_activate(window, null), "2.4 activate null token rejected");
            final int[] handleCalls = {0};
            Pt.ForeignHandleHandler handleHandler =
                    (target, handle, data) -> handleCalls[0] += 1;
            check(!Pt.window_export_foreign_handle(window, handleHandler, arena),
                    "2.4 export foreign handle headless false");
            Pt.window_unexport_foreign_handle(window);
            Pt.window_unexport_foreign_handle(window); /* idempotent no-op */
            check(!Pt.window_set_foreign_parent(window, "opaque-handle"),
                    "2.4 set foreign parent headless false");
            check(!Pt.window_set_foreign_parent(window, ""),
                    "2.4 set foreign parent empty rejected");
            check(!Pt.window_set_foreign_parent(window, null),
                    "2.4 set foreign parent null rejected");
            Pt.window_clear_foreign_parent(window);
            Pt.window_clear_foreign_parent(window); /* idempotent no-op */
            check(tokenCalls[0] == 0, "2.4 activation never delivers headless");
            check(handleCalls[0] == 0, "2.4 foreign handle never delivers headless");
            /* A NULL callback is rejected by the native layer without creating
             * an object; the wrappers pass it through as MemorySegment.NULL. */
            check(!Pt.window_request_activation_token(window, "pt.tests", null, arena),
                    "2.4 activation NULL callback rejected");
            check(!Pt.window_export_foreign_handle(window, null, arena),
                    "2.4 export NULL callback rejected");
            /* NULL window short-circuits in every entry point. */
            check(!Pt.window_activate(MemorySegment.NULL, "opaque-token"),
                    "2.4 activate null window false");
            check(!Pt.window_set_foreign_parent(MemorySegment.NULL, "opaque-handle"),
                    "2.4 set parent null window false");

            final boolean[] allowed = {false};
            Pt.window_set_close_handler(window, (target, data) -> allowed[0], arena);
            Pt.window_close(window);
            check(Pt.window_is_open(window), "close veto");

            allowed[0] = true;
            Pt.window_close(window);

            int[] destroyDescriptors = nativePipe();
            check(destroyDescriptors != null, "destroy native pipe");
            if (destroyDescriptors != null) {
                long destroyWatch = Pt.app_watch_fd_owned(app, destroyDescriptors[0],
                        Pt.fd_event_read, (source, events, data) -> { }, arena);
                check(destroyWatch != 0, "destroy owned watch id");
                check(descriptorOpen(destroyDescriptors[0]), "destroy owned fd open");
            }
            Pt.app_destroy(app);
            if (destroyDescriptors != null) {
                check(!descriptorOpen(destroyDescriptors[0]),
                        "app destroy closes owned fd");
            }

            if (failures != 0) {
                System.err.println("java binding: " + checks + " checks, " + failures
                        + " failure(s)");
                System.exit(1);
            }
            System.out.println("java binding: " + checks + " checks, 0 failures");
        }
    }
}
