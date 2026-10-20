# Test protocols

`virtual-keyboard-unstable-v1.xml` is the wlroots virtual-keyboard protocol
description (MIT licensed, copyright retained in the file), vendored only for
`tests/vkbd_type.c`. Upstream:
<https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/protocol/virtual-keyboard-unstable-v1.xml>.

The library does not use it; `make` generates its client code into
`build/protocols/` only when `make check-typing` builds the injector.
