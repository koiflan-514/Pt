# Protocol extensions: activation and foreign toplevels

English | [简体中文](zh-CN/protocol-extensions.md)

Status: delivered in 2.4.0. Two optional Wayland protocols extend the window
lifecycle without changing the text, layout or paint path:
**xdg-activation-v1** (staging) asks the compositor to activate a toplevel, and
**xdg-foreign-unstable-v2** (unstable, experimental) links a toplevel to a
parent in another process. This guide records the contract frozen in
`include/pt/window.h`; [m26-design.md](m26-design.md) records the design input
and [compositor-matrix.md](compositor-matrix.md) the live evidence and the
fallback rows.

Related reading: [bindings.md](bindings.md) covers the wrapper callback and
string rules, [standards.md](standards.md) section 8 covers optional-global
negotiation, and [architecture.md](architecture.md) places the platform layer
in the toolkit.

## Best-effort by design

- **No global, no request.** Every new call checks the optional global first.
  Without `xdg_activation_v1`, `zxdg_exporter_v2` or `zxdg_importer_v2` it
  returns `false` (or is a silent no-op) before creating any protocol object.
  A headless platform takes the same path.
- **No application branch.** An application may call the API unconditionally:
  a missing protocol is a behavioural downgrade, not an error. The window still
  opens as an ordinary toplevel.
- **Requests, not promises.** `pt_window_activate` reports that the request was
  sent. The compositor may ignore a stale token; Pt cannot observe whether focus
  moved. Use the focus callback for the observed keyboard focus.
- **Strings are borrowed.** A token or handle passed to a callback is valid only
  during that call. Copy it if it must outlive the callback; an empty string
  means the compositor returned an invalid value.
- **Idempotent teardown.** `pt_window_unexport_foreign_handle` and
  `pt_window_clear_foreign_parent` are no-ops when nothing is exported or
  imported, and window destruction releases everything.

## Activation tokens (xdg-activation-v1, staging)

The launcher requests a one-shot token and passes it to the client that should
be activated, either through `XDG_ACTIVATION_TOKEN` or out of band (for example
a D-Bus `activation-token` field). The receiving client calls
`pt_window_activate`. The token object is asynchronous: the request returns
before `done`, and only one request may be in flight per window.

- `pt_window_request_activation_token` creates the token object. `app_id` may
  be NULL; when set it names the client that will be activated. The request
  attaches the latest pointer-button or key-press serial when one exists and
  omits it otherwise, so a compositor that requires a recent serial may still
  decline a request made without an input event.
- The callback runs once on the UI thread. Only one request can be in flight:
  a new request destroys the pending token object and drops its callback.
- `pt_window_activate` sends the token once and does not retain it. Empty and
  NULL tokens, an absent global and a headless platform return `false`.
- Re-entrant requests from inside the callback are safe because the window
  fields are cleared before the callback runs.

## Startup token from the environment

Startup activation is automatic and has no extra function. After the first
registry roundtrip, when `xdg_activation_v1` is bound, the platform reads
`XDG_ACTIVATION_TOKEN` and unsets it, so it is not inherited by unrelated
children. The first window that reaches the mapped state consumes the token
exactly once; a second window does not repeat the request.

Without the global the environment variable is neither read nor unset, so an
unsupported compositor sees no behaviour change. `pt_window_activate` does not
consume the startup token, so an application that handles startup explicitly
can still call it.

## Quick start: request and activate

```c
#include <pt/pt.h>

#include <stdio.h>

static void on_token(pt_window *window, const char *token, void *user_data) {
    (void)user_data;
    /* The token is borrowed for this call. A launcher copies it and passes the
     * copy to the child out of band; here it activates the same window. */
    if (token && token[0] != '\0') {
        pt_window_activate(window, token);
    }
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.activation");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Activation", 480, 320);
    if (!window) {
        pt_app_destroy(app);
        return 1;
    }
    pt_window_show(window);

    /* A false return means no protocol object was created: the global is
     * absent, the platform is headless, or window/callback is NULL. */
    if (!pt_window_request_activation_token(window, "dev.pt.activation",
                                            on_token, NULL)) {
        fprintf(stderr, "activation: request unavailable\n");
    }

    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

`app_id` describes the client that will be activated, not necessarily the
caller. A launcher that knows the child's app id passes it; a self-activation
may pass its own id or NULL. The callback is invoked once even when the
compositor returns an empty token, so an empty string is the "no token" signal.

## Foreign toplevel handles (xdg-foreign-unstable-v2, experimental)

The exporter shares an opaque handle for one toplevel; the importer sets it as
another window's parent. The relationship has the same stacking and positioning
semantics as `xdg_toplevel.set_parent`. It is not embedding, not a widget
reparent and not a pixel transfer.

- `pt_window_export_foreign_handle` creates an export object and delivers its
  handle once. The export stays alive until
  `pt_window_unexport_foreign_handle` or window destruction. Exporting again
  first destroys the previous export, drops its pending callback and
  invalidates every import built from the old handle.
- The handle is opaque and compositor-defined: never parse, log or persist it
  beyond the exported object's lifetime.
- `pt_window_set_foreign_parent` imports the handle and makes this window its
  child. It replaces a previous imported parent, so a window has at most one.
  A NULL or empty handle, a missing importer and headless return `false`; an
  invalid handle is cleared silently when the compositor sends `destroyed`,
  and a later import can still succeed.
- `pt_window_unexport_foreign_handle` and `pt_window_clear_foreign_parent`
  revoke the protocol object and are idempotent. Window destruction also
  destroys both sides; the compositor invalidates imports asynchronously.
- Only v2 is bound. v1 is the deprecated predecessor; a compositor that
  exposes only v1 (or neither) gets the no-op fallback.

## Quick start: export and parent

```c
#include <pt/pt.h>

#include <stdio.h>

static void on_handle(pt_window *exporter, const char *handle, void *user_data) {
    pt_window *dialog = user_data;
    (void)exporter;
    /* The handle is opaque and borrowed. Import it before returning; another
     * process would receive the same string out of band. */
    if (handle && handle[0] != '\0') {
        pt_window_set_foreign_parent(dialog, handle);
    }
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.foreign");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *caller = pt_window_create(app, "Caller", 640, 400);
    pt_window *dialog = pt_window_create(app, "Dialog", 360, 220);
    if (!caller || !dialog) {
        pt_app_destroy(app);
        return 1;
    }
    pt_window_show(caller);
    pt_window_show(dialog);

    /* A false return means there is no exporter global or the platform is
     * headless; the dialog stays an independent toplevel. */
    if (!pt_window_export_foreign_handle(caller, on_handle, dialog)) {
        fprintf(stderr, "foreign: export unavailable\n");
    }

    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

A real out-of-process dialog receives the handle through its own IPC channel,
imports it in the child process and calls `pt_window_set_foreign_parent`. The
same-client sequence above is the one exercised by the live test; the protocol
does not require the two windows to share a connection.

## Ownership and lifetimes

- `pt_window_request_activation_token` and
  `pt_window_export_foreign_handle` deliver exactly one callback. The callback's
  token or handle string is borrowed for the call, like the key-event `utf8`
  argument; it becomes invalid when the callback returns.
- The export object, not the string, owns the handle's validity. Unexporting it
  or destroying the window destroys the object and invalidates imports.
- `pt_window_activate` and `pt_window_set_foreign_parent` read their string
  synchronously and retain nothing, so the caller may free it after the call
  returns.
- The import object belongs to the child window. It is replaced by the next
  `pt_window_set_foreign_parent`, cleared by `pt_window_clear_foreign_parent`
  or destroyed with the window.

## Compositor support

The rows come from [compositor-matrix.md](compositor-matrix.md). niri 26.04 is
grade B: the nested registry advertised both activation and foreign v2, and a
live client exercised a token request/activate and an
export/import/set_parent_of roundtrip. The other rows are grade C from the
public support matrix. Weston lacks both globals in this snapshot.

| Compositor (version) | xdg-activation | xdg-foreign v2 | Pt behaviour |
| --- | --- | --- | --- |
| niri 26.04 | 1 (live B) | 1 (live B) | requests sent |
| Mutter 51 (GNOME) | 1 | 1 | requests sent |
| KWin 6.7 (Plasma) | 1 | 1 | requests sent |
| Sway 1.11 (wlroots) | 1 | 1 | requests sent |
| Hyprland 0.57.2 | 1 | 1 | requests sent |
| COSMIC 1.11.0 | 1 | 1 | requests sent |
| Weston 15.0.1 | — | — | false / no-op |
| Muffin 6.7.4 (Cinnamon) | 1 | 1 | requests sent |

Availability is not efficacy: a bound global can still ignore an activation
request or reject an import. The fallback is a behavioural downgrade, not an
error, and it never changes pixels, damage or input handling.

## Bindings

C++ exposes the six calls as `pt::window` methods. Token and handle inputs use
`std::string_view`; the wrapper copies them before the C call, so the view does
not need to outlive the call. Callbacks stay raw `pt_*_callback` pointers with
the same `user_data` convention, and the borrowed token or handle is never
retained.

Python exposes `request_activation_token`, `activate`,
`export_foreign_handle`, `unexport_foreign_handle`, `set_foreign_parent` and
`clear_foreign_parent`. Its `CFUNCTYPE` trampolines are kept in the callback
roots until the window is destroyed, so a token or handle delivered later does
not call a collected function; the callback argument is a Python `str` valid
only during the call.

Java exposes the same six methods with `ActivationTokenHandler` and
`ForeignHandleHandler` functional interfaces. The upcall decodes a
callback-scoped `String`, and the `Arena` passed to the request must outlive the
callback, matching every other Java callback in the binding.

## Examples

- [C protocol extensions](../examples/c/protocol_extensions.c)
- [C++ protocol extensions](../examples/cpp/protocol_extensions.cpp)
- [Python protocol extensions](../examples/python/protocol_extensions.py)
- [Java protocol extensions](../examples/java/ProtocolExtensions.java)

Each example requests an activation token, exports a toplevel handle and
imports it on a second window. Set `PT_HEADLESS=1` to run the fallback checks
without a compositor: every request returns `false`, the callbacks are never
invoked, the idempotent teardown calls are safe, and the process exits 0. The
live path is exercised only when a compositor offers the globals.

## Compatibility and limitations

- Added in 2.4.0 as six functions and two callbacks. No enum, struct or define
  changes; `ABI_MAJOR` stays 1 and the SONAME stays `libpt.so.1`.
- xdg-activation is **staging**, not stable. An incompatible change needs a new
  major interface version and would be a new design, not a silent upgrade.
- xdg-foreign is **unstable and experimental**. The protocol may change; v1 is
  not supported. Handles are not portable between compositors and must not be
  persisted.
- Activation never guarantees focus, and xdg-foreign never embeds pixels,
  reparents widgets or transfers input. Both remain toplevel-level requests.
- On an unsupported or headless platform every call returns `false` or no-ops;
  no protocol object is created and no rendering path changes.