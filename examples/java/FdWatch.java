/*
 * Pt fd watch example - Java bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * An eventfd drives the UI through Pt's poll loop. This example uses
 * Pt.app_watch_fd_owned: ownership of the descriptor moves to Pt, which closes
 * it when the watch is released (including the self-unwatch from inside the
 * callback). The repeating timer is only an event source; the interesting path
 * is fd -> poll -> callback.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

import pt.Pt;

@SuppressWarnings("restricted")
public final class FdWatch {
    private FdWatch() {}

    private static final int TICK_TARGET = 8;
    private static final int TICK_INTERVAL_MS = 250;
    private static final int EFD_NONBLOCK = 0x00000800;
    private static final int EFD_CLOEXEC = 0x00080000;

    private static final MethodHandle EVENTFD = nativeHandle("eventfd",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_INT));
    private static final MethodHandle READ = nativeHandle("read",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle WRITE = nativeHandle("write",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle CLOSE = nativeHandle("close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));

    private static MethodHandle nativeHandle(String name, FunctionDescriptor descriptor) {
        try {
            Linker linker = Linker.nativeLinker();
            return linker.downcallHandle(
                    linker.defaultLookup().find(name).orElseThrow(), descriptor);
        } catch (Throwable error) {
            return null;
        }
    }

    private static int createEventfd() {
        if (EVENTFD == null) {
            return -1;
        }
        try {
            return (int) EVENTFD.invokeExact(0, EFD_NONBLOCK | EFD_CLOEXEC);
        } catch (Throwable error) {
            return -1;
        }
    }

    private static boolean writeEvent(int fd) {
        if (WRITE == null || fd < 0) {
            return false;
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment one = arena.allocate(ValueLayout.JAVA_LONG);
            one.set(ValueLayout.JAVA_LONG, 0, 1L);
            return (long) WRITE.invokeExact(fd, one, 8L) == 8L;
        } catch (Throwable error) {
            return false;
        }
    }

    private static long readCounter(int fd) {
        if (READ == null || fd < 0) {
            return -1L;
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment counter = arena.allocate(ValueLayout.JAVA_LONG);
            long got = (long) READ.invokeExact(fd, counter, 8L);
            return got == 8L ? counter.get(ValueLayout.JAVA_LONG, 0) : -1L;
        } catch (Throwable error) {
            return -1L;
        }
    }

    private static void closeFd(int fd) {
        if (CLOSE == null || fd < 0) {
            return;
        }
        try {
            CLOSE.invokeExact(fd);
        } catch (Throwable error) {
            // Nothing useful to do in a demo; the process is exiting.
        }
    }

    private static final class WatchState {
        MemorySegment app;
        MemorySegment window;
        MemorySegment progress;
        MemorySegment counter;
        MemorySegment status;
        MemorySegment arm;
        int fd = -1;
        long watchId = 0L;
        long ticks = 0L;
        boolean feederPending = false;
    }

    private static void setStatus(WatchState state, String text) {
        Pt.widget_set_text(state.status, text);
    }

    private static void renderTicks(WatchState state) {
        float value = (float) state.ticks / (float) TICK_TARGET;
        Pt.progress_set_value(state.progress, Math.min(value, 1.0f));
        Pt.widget_set_text(state.counter, state.ticks + " / " + TICK_TARGET + " events");
    }

    private static void releaseWatch(WatchState state, String message) {
        long oldId = state.watchId;
        state.watchId = 0L;
        /* Self-unwatch: Pt removes the watch after this dispatch round and
         * closes the owned descriptor. */
        if (oldId != 0L) {
            Pt.app_unwatch_fd(state.app, oldId);
        }
        state.fd = -1;
        Pt.widget_set_text(state.arm, "Arm another watch");
        setStatus(state, message);
    }

    private static void onFeeder(MemorySegment app, WatchState state, Arena arena) {
        state.feederPending = false;
        if (state.watchId == 0L || state.fd < 0) {
            return;
        }
        writeEvent(state.fd);
        scheduleFeeder(app, state, arena);
    }

    private static void scheduleFeeder(MemorySegment app, WatchState state, Arena arena) {
        if (state.feederPending) {
            return;
        }
        state.feederPending = true;
        Pt.app_add_timeout(app, TICK_INTERVAL_MS,
                (target, data) -> onFeeder(target, state, arena), arena);
    }

    private static void onFd(MemorySegment app, int events, WatchState state) {
        if ((events & (Pt.fd_event_error | Pt.fd_event_hangup)) != 0) {
            releaseWatch(state,
                    "The descriptor reported an error; the watch was released.");
            return;
        }
        if ((events & Pt.fd_event_read) == 0 || state.fd < 0) {
            return;
        }
        long counter = readCounter(state.fd);
        if (counter < 0L) {
            setStatus(state, "read() on the watched eventfd failed.");
            return;
        }
        state.ticks += counter;
        renderTicks(state);
        setStatus(state, "Read " + counter + " event(s) from owned fd " + state.fd + ".");
        System.out.printf("fd_watch (Java): %d/%d events%n", state.ticks, TICK_TARGET);
        if (state.ticks >= TICK_TARGET) {
            releaseWatch(state,
                    "Reached 8 events. The watch released itself and Pt closed the fd.");
            Pt.window_show_toast(state.window, "fd watch released itself", Pt.toast_success,
                    1800);
            System.out.printf(
                    "fd_watch (Java): watch released after %d events; Pt closed the owned fd%n",
                    state.ticks);
        }
    }

    private static void armWatch(WatchState state, Arena arena) {
        if (state.watchId != 0L) {
            setStatus(state, "A watch is already armed; wait for it to release itself.");
            return;
        }
        int fd = createEventfd();
        if (fd < 0) {
            setStatus(state, "eventfd() failed.");
            return;
        }
        state.fd = fd;
        state.ticks = 0L;
        renderTicks(state);
        long watchId = Pt.app_watch_fd_owned(state.app, fd, Pt.fd_event_read,
                (app, events, data) -> onFd(app, events, state), arena);
        if (watchId == 0L) {
            closeFd(fd);
            state.fd = -1;
            setStatus(state, "Pt.app_watch_fd_owned() rejected the descriptor.");
            return;
        }
        state.watchId = watchId;
        Pt.widget_set_text(state.arm, "Watch armed");
        setStatus(state, "Pt owns eventfd " + fd
                + "; the feeder writes an event every " + TICK_INTERVAL_MS + " ms.");
        scheduleFeeder(state.app, state, arena);
        System.out.printf("fd_watch (Java): armed owned fd %d for %d events%n", fd,
                TICK_TARGET);
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.fd_watch.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            MemorySegment window = Pt.window_create(app, "Pt fd Watch - Java", 640, 460);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            WatchState state = new WatchState();
            state.app = app;
            state.window = window;

            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment title = Pt.label(root, "File descriptor watch");
            Pt.widget_set_font_size(title, 26.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_semibold);
            MemorySegment subtitle = Pt.label(root,
                    "An eventfd is transferred to Pt and wakes its poll loop. The callback "
                            + "reads it without blocking and self-unwatches after 8 events; "
                            + "Pt closes the owned descriptor.");
            Pt.widget_set_font_size(subtitle, 13.0f);
            Pt.widget_set_foreground(subtitle, Pt.rgb(0x9AA7B8));
            Pt.widget_set_wrap(subtitle, true);

            MemorySegment card = Pt.card(root);
            Pt.widget_set_padding(card, 22.0f, 22.0f, 22.0f, 22.0f);
            Pt.widget_set_gap(card, 14.0f);
            Pt.widget_set_background(card, Pt.rgb(0x141B26));
            Pt.widget_set_border_width(card, 1.0f);
            Pt.widget_set_border_color(card, Pt.rgb(0x253040));

            state.counter = Pt.label(card, "0 / 8 events");
            Pt.widget_set_font_size(state.counter, 28.0f);
            Pt.widget_set_font_weight(state.counter, Pt.font_weight_semibold);
            state.progress = Pt.progress(card);
            Pt.widget_set_height(state.progress, 10.0f);
            Pt.progress_set_value(state.progress, 0.0f);

            state.status = Pt.label(card, "Arming the first watch…");
            Pt.widget_set_font_size(state.status, 12.0f);
            Pt.widget_set_foreground(state.status, Pt.rgb(0x9ECE6A));
            Pt.widget_set_wrap(state.status, true);

            MemorySegment actions = Pt.row(card);
            Pt.widget_set_gap(actions, 10.0f);
            state.arm = Pt.button(actions, "Watch armed");
            Pt.button_set_variant(state.arm, Pt.button_primary);
            Pt.button_set_on_click(state.arm, (widget, data) -> armWatch(state, arena),
                    arena);
            Pt.widget_set_tooltip(state.arm,
                    "Creates a fresh eventfd and transfers it to Pt for read events");
            MemorySegment send = Pt.button(actions, "Send one event");
            Pt.button_set_variant(send, Pt.button_secondary);
            Pt.button_set_on_click(send, (widget, data) -> {
                if (state.watchId == 0L || state.fd < 0) {
                    setStatus(state, "Arm a watch before sending an event.");
                } else if (writeEvent(state.fd)) {
                    setStatus(state, "Wrote one event to the owned fd from a button callback.");
                } else {
                    setStatus(state, "Writing to the owned fd failed.");
                }
            }, arena);
            Pt.widget_set_tooltip(send,
                    "Writes to the same owned eventfd from a click callback");

            MemorySegment footer = Pt.label(root,
                    "The callback never blocks: it drains the eventfd with a non-blocking "
                            + "read, updates widgets and returns. Removing a watch from inside "
                            + "its own callback is supported; Pt closes an owned descriptor, "
                            + "so the example never closes it twice.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));
            Pt.widget_set_wrap(footer, true);

            Pt.window_show(window);
            armWatch(state, arena);
            Pt.app_run(app);
            if (state.watchId != 0L) {
                Pt.app_unwatch_fd(app, state.watchId);
                state.watchId = 0L;
            }
            Pt.app_destroy(app);
        }
    }
}