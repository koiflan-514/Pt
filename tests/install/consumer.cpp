// Installed-header consumer used by tools/install-check.sh.
#include <pt/pt.hpp>

int main() {
    pt::app app("dev.pt.install.cpp");
    pt::window window(app, "Installed C++ consumer", 320, 200);
    pt::widget card = pt::card(window.root());
    pt::label(card, "installed C++ consumer");
    window.show();
    return app.run_for(30);
}
