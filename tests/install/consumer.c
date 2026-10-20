/* Installed-library consumer used by tools/install-check.sh. */
#include <pt/pt.h>

int main(void) {
    pt_app *app = pt_app_create("dev.pt.install.c");
    if (!app) {
        return 1;
    }
    pt_window *window = pt_window_create(app, "Installed C consumer", 320, 200);
    if (!window) {
        pt_app_destroy(app);
        return 2;
    }
    pt_widget *card = pt_card_create(pt_window_root(window));
    pt_label_create(card, "installed C consumer");
    pt_window_show(window);
    pt_app_run_for(app, 30);
    pt_app_destroy(app);
    return 0;
}
