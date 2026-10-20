import java.lang.foreign.MemorySegment;
import pt.Pt;

/** Installed-header consumer used by tools/install-check.sh. */
public final class Consumer {
    private Consumer() {
    }

    public static void main(String[] args) {
        MemorySegment app = Pt.app_create("dev.pt.install.java");
        if (app.address() == 0) {
            throw new IllegalStateException("pt_app_create failed");
        }
        MemorySegment window = Pt.window_create(app, "Installed Java consumer", 320, 200);
        MemorySegment card = Pt.card(Pt.window_root(window));
        Pt.label(card, "installed Java consumer");
        Pt.window_show(window);
        Pt.app_run_for(app, 30);
        Pt.app_destroy(app);
    }
}
