// sinfo — Services Information v1.0.1
// A GTK4 (gtkmm-4.0) front-end to list and manage systemd services.
//
// Author: Jean-Francois Lachance-Caumartin
// Repository: https://github.com/effjy/sinfo/
// License: MIT
//
// Privileged actions (start/stop/enable/disable/mask/unmask) are performed by
// running `pkexec systemctl ...`, so the application itself runs as a normal
// user and only elevates the individual operation through polkit.

#include <gtkmm.h>
#include <glibmm.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Run a command and capture stdout. Uses an explicit argv vector so no shell
// parsing / quoting is involved.
Glib::ustring run_capture(const std::vector<std::string>& argv) {
    std::string out;
    std::string err;
    int status = 0;
    try {
        Glib::spawn_sync(std::string(), argv, Glib::SpawnFlags::SEARCH_PATH,
                         Glib::SlotSpawnChildSetup(), &out, &err, &status);
    } catch (const Glib::Error& e) {
        return Glib::ustring();
    }
    return Glib::ustring(out);
}

struct ServiceInfo {
    Glib::ustring unit;    // foo.service
    Glib::ustring load;    // loaded / not-found
    Glib::ustring active;  // active / inactive / failed
    Glib::ustring sub;     // running / dead / exited
    Glib::ustring state;   // enabled / disabled / masked / static
    Glib::ustring desc;    // human description
};

}  // namespace

class SinfoWindow : public Gtk::ApplicationWindow {
public:
    SinfoWindow();

private:
    class Columns : public Gtk::TreeModel::ColumnRecord {
    public:
        Columns() {
            add(unit);
            add(load);
            add(active);
            add(sub);
            add(state);
            add(desc);
        }
        Gtk::TreeModelColumn<Glib::ustring> unit;
        Gtk::TreeModelColumn<Glib::ustring> load;
        Gtk::TreeModelColumn<Glib::ustring> active;
        Gtk::TreeModelColumn<Glib::ustring> sub;
        Gtk::TreeModelColumn<Glib::ustring> state;
        Gtk::TreeModelColumn<Glib::ustring> desc;
    };

    Columns m_cols;
    Glib::RefPtr<Gtk::ListStore> m_store;
    Gtk::TreeView m_tree;
    Gtk::SearchEntry m_search;
    Gtk::Label m_status;
    Gtk::AboutDialog m_about;
    std::vector<ServiceInfo> m_all;

    void build_ui();
    void setup_about();
    void refresh();
    void populate(const Glib::ustring& filter);
    bool get_selected_unit(Glib::ustring& unit);
    void run_action(const Glib::ustring& verb);
    void on_about();
};

SinfoWindow::SinfoWindow() {
    build_ui();
    setup_about();
    refresh();
}

void SinfoWindow::build_ui() {
    set_title("sinfo — Services Information");
    set_default_size(1150, 620);

    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    set_child(*root);

    // ---- Left: control panel -------------------------------------------------
    auto* left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    left->set_margin(10);
    left->set_size_request(190, -1);

    auto* title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<b>Controls</b>");
    title->set_xalign(0.0f);
    left->append(*title);

    auto* refresh_btn = Gtk::make_managed<Gtk::Button>("⟳  Refresh");
    refresh_btn->signal_clicked().connect(sigc::mem_fun(*this, &SinfoWindow::refresh));
    left->append(*refresh_btn);

    auto section = [&](const char* text) {
        left->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(Glib::ustring("<small><b>") + text + "</b></small>");
        l->set_xalign(0.0f);
        left->append(*l);
    };

    auto action_btn = [&](const char* label, const Glib::ustring& verb) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        b->signal_clicked().connect([this, verb] { run_action(verb); });
        left->append(*b);
    };

    section("Runtime state");
    action_btn("▶  Start", "start");
    action_btn("■  Stop", "stop");

    section("Boot behaviour");
    action_btn("✔  Enable", "enable");
    action_btn("✘  Disable", "disable");

    section("Masking");
    action_btn("🔒  Mask", "mask");
    action_btn("🔓  Unmask", "unmask");

    auto* spacer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    spacer->set_vexpand(true);
    left->append(*spacer);

    auto* about_btn = Gtk::make_managed<Gtk::Button>("About");
    about_btn->signal_clicked().connect(sigc::mem_fun(*this, &SinfoWindow::on_about));
    left->append(*about_btn);

    root->append(*left);
    root->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

    // ---- Right: services list ------------------------------------------------
    auto* right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    right->set_margin(10);
    right->set_hexpand(true);
    right->set_vexpand(true);

    m_search.set_placeholder_text("Search services…");
    m_search.signal_search_changed().connect(
        [this] { populate(m_search.get_text()); });
    right->append(m_search);

    m_store = Gtk::ListStore::create(m_cols);
    m_tree.set_model(m_store);
    m_tree.append_column("Service", m_cols.unit);
    m_tree.append_column("Load", m_cols.load);
    m_tree.append_column("Active", m_cols.active);
    m_tree.append_column("Sub", m_cols.sub);
    m_tree.append_column("Boot State", m_cols.state);
    m_tree.append_column("Description", m_cols.desc);

    if (auto* c = m_tree.get_column(0)) c->set_sort_column(m_cols.unit);
    if (auto* c = m_tree.get_column(2)) c->set_sort_column(m_cols.active);
    if (auto* c = m_tree.get_column(4)) c->set_sort_column(m_cols.state);
    for (int i = 0; i < static_cast<int>(m_tree.get_n_columns()); ++i) {
        if (auto* c = m_tree.get_column(i)) c->set_resizable(true);
    }
    m_tree.set_enable_search(true);
    m_tree.set_search_column(m_cols.unit);

    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller->set_child(m_tree);
    scroller->set_hexpand(true);
    scroller->set_vexpand(true);
    right->append(*scroller);

    m_status.set_xalign(0.0f);
    m_status.add_css_class("dim-label");
    right->append(m_status);

    root->append(*right);
}

void SinfoWindow::setup_about() {
    m_about.set_transient_for(*this);
    m_about.set_modal(true);
    m_about.set_hide_on_close(true);
    m_about.set_program_name("Services Information (sinfo)");
    m_about.set_version("1.0.1");
    m_about.set_logo_icon_name("sinfo");
    m_about.set_comments(
        "A GTK4 front-end for managing systemd services.\n\n"
        "Features:\n"
        "• List every service known to the machine\n"
        "• Start and stop services\n"
        "• Enable and disable services at boot\n"
        "• Mask and unmask services\n"
        "• Live search / filtering\n"
        "• Privileged actions performed safely through pkexec (polkit)");
    m_about.set_copyright("© 2026 Jean-Francois Lachance-Caumartin");
    m_about.set_authors({"Jean-Francois Lachance-Caumartin"});
    m_about.set_website("https://github.com/effjy/sinfo/");
    m_about.set_website_label("github.com/effjy/sinfo");
    m_about.set_license_type(Gtk::License::MIT_X11);
}

void SinfoWindow::on_about() { m_about.present(); }

void SinfoWindow::refresh() {
    std::map<std::string, ServiceInfo> table;

    // 1) Loaded/running units: UNIT LOAD ACTIVE SUB DESCRIPTION
    Glib::ustring units = run_capture({"systemctl", "list-units", "--type=service",
                                       "--all", "--no-legend", "--no-pager", "--plain"});
    {
        std::istringstream is(units.raw());
        std::string line;
        while (std::getline(is, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string unit, load, active, sub;
            if (!(ls >> unit >> load >> active >> sub)) continue;
            std::string desc;
            std::getline(ls, desc);
            size_t p = desc.find_first_not_of(" \t");
            desc = (p == std::string::npos) ? std::string() : desc.substr(p);

            ServiceInfo si;
            si.unit = unit;
            si.load = load;
            si.active = active;
            si.sub = sub;
            si.desc = desc;
            si.state = "-";
            table[unit] = si;
        }
    }

    // 2) Installed unit files: UNIT_FILE STATE [PRESET]
    Glib::ustring files = run_capture({"systemctl", "list-unit-files", "--type=service",
                                       "--no-legend", "--no-pager"});
    {
        std::istringstream is(files.raw());
        std::string line;
        while (std::getline(is, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string unit, state;
            if (!(ls >> unit >> state)) continue;
            auto it = table.find(unit);
            if (it != table.end()) {
                it->second.state = state;
            } else {
                ServiceInfo si;
                si.unit = unit;
                si.load = "-";
                si.active = "-";
                si.sub = "-";
                si.state = state;
                table[unit] = si;
            }
        }
    }

    m_all.clear();
    m_all.reserve(table.size());
    for (auto& kv : table) m_all.push_back(kv.second);
    std::sort(m_all.begin(), m_all.end(),
              [](const ServiceInfo& a, const ServiceInfo& b) { return a.unit < b.unit; });

    populate(m_search.get_text());
}

void SinfoWindow::populate(const Glib::ustring& filter) {
    m_store->clear();
    Glib::ustring needle = filter.lowercase();
    int shown = 0;
    for (const auto& s : m_all) {
        if (!needle.empty()) {
            Glib::ustring hay = (s.unit + " " + s.desc).lowercase();
            if (hay.find(needle) == Glib::ustring::npos) continue;
        }
        auto row = *(m_store->append());
        row[m_cols.unit] = s.unit;
        row[m_cols.load] = s.load;
        row[m_cols.active] = s.active;
        row[m_cols.sub] = s.sub;
        row[m_cols.state] = s.state;
        row[m_cols.desc] = s.desc;
        ++shown;
    }
    m_status.set_text(Glib::ustring::format(shown) + " of " +
                      Glib::ustring::format(m_all.size()) + " services shown");
}

bool SinfoWindow::get_selected_unit(Glib::ustring& unit) {
    auto sel = m_tree.get_selection();
    if (!sel) return false;
    auto it = sel->get_selected();
    if (!it) return false;
    unit = (*it)[m_cols.unit];
    return true;
}

void SinfoWindow::run_action(const Glib::ustring& verb) {
    Glib::ustring unit;
    if (!get_selected_unit(unit)) {
        m_status.set_text("Select a service first.");
        return;
    }

    std::string out, err;
    int status = 0;
    try {
        Glib::spawn_sync(std::string(),
                         std::vector<std::string>{"pkexec", "systemctl",
                                                  verb.raw(), unit.raw()},
                         Glib::SpawnFlags::SEARCH_PATH, Glib::SlotSpawnChildSetup(),
                         &out, &err, &status);
    } catch (const Glib::Error& e) {
        m_status.set_text(Glib::ustring("Error: ") + e.what());
        return;
    }

    if (status == 0) {
        m_status.set_text(verb + " " + unit + " — done");
    } else {
        Glib::ustring msg = Glib::ustring(err);
        if (msg.empty()) msg = "operation failed or was cancelled";
        m_status.set_text(verb + " " + unit + " — " + msg);
    }
    refresh();
}

// ---------------------------------------------------------------------------

class SinfoApp : public Gtk::Application {
protected:
    SinfoApp() : Gtk::Application("org.effjy.sinfo") {}

public:
    static Glib::RefPtr<SinfoApp> create() {
        return Glib::make_refptr_for_instance(new SinfoApp());
    }

protected:
    void on_activate() override {
        auto* win = new SinfoWindow();
        add_window(*win);
        win->signal_close_request().connect(
            [win]() -> bool {
                delete win;
                return false;
            },
            false);
        win->present();
    }
};

int main(int argc, char** argv) {
    auto app = SinfoApp::create();
    return app->run(argc, argv);
}
