"""Installed-binding consumer used by tools/install-check.sh."""

import pt

app = pt.App("dev.pt.install.python")
window = pt.Window(app, "Installed Python consumer", 320, 200)
card = pt.card(window.root)
pt.label(card, "installed Python consumer")
window.show()
status = app.run_for(30)
app.destroy()
raise SystemExit(status)
