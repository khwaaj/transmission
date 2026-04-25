// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include "FilterListModel.hh"
#include "HigWorkarea.h" // GUI_PAD
#include "ListModelAdapter.h"
#include "Prefs.h"
#include "Session.h" // torrent_cols
#include "Torrent.h"
#include "TorrentFilter.h"
#include "Utils.h"

#include <libtransmission-app/display-modes.h>

#include <libtransmission/tr-macros.h>

#include <giomm/mount.h>
#include <giomm/volumemonitor.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/unicode.h>
#include <glibmm/ustring.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treerowreference.h>
#include <gtkmm/treestore.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/filterlistmodel.h>
#endif

#include <fmt/format.h>

#include <algorithm> // std::transform()
#include <array>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>

using namespace tr::app;

namespace
{
using TrackerType = TorrentFilter::Tracker;
using LabelType = TorrentFilter::Label;
using VolumeType = TorrentFilter::Volume;

constexpr auto ShowModeSeparator = static_cast<ShowMode>(-1);
constexpr auto TrackerSeparator = static_cast<TrackerType>(-1);
constexpr auto LabelSeparator = static_cast<LabelType>(-1);
constexpr auto VolumeSeparator = static_cast<VolumeType>(-1);
} // namespace

class FilterBar::Impl
{
    using FilterModel = IF_GTKMM4(Gtk::FilterListModel, Gtk::TreeModelFilter);

public:
    Impl(FilterBar& widget, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    [[nodiscard]] Glib::RefPtr<FilterModel> get_filter_model() const;

private:
    template<typename T>
    T* get_template_child(char const* name) const;

    void show_mode_combo_box_init(Gtk::ComboBox& combo);
    static void render_show_mode_pixbuf_func(
        Gtk::CellRendererPixbuf& cell_renderer,
        Gtk::TreeModel::const_iterator const& iter);

    void tracker_combo_box_init(Gtk::ComboBox& combo);
    static void render_pixbuf_func(Gtk::CellRendererPixbuf& cell_renderer, Gtk::TreeModel::const_iterator const& iter);
    static void render_number_func(Gtk::CellRendererText& cell_renderer, Gtk::TreeModel::const_iterator const& iter);

    void label_combo_box_init(Gtk::ComboBox& combo);
    void volume_combo_box_init(Gtk::ComboBox& combo);

    void update_filter_show_mode();
    void update_filter_tracker();
    void update_filter_label();
    void update_filter_volume();
    void update_filter_text();

    bool show_mode_filter_model_update();

    bool tracker_filter_model_update();
    void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, Gtk::TreeModel::Path const& path);

    bool label_filter_model_update();

    bool volume_filter_model_update();

    void update_filter_models(Torrent::ChangeFlags changes);
    void update_filter_models_idle(Torrent::ChangeFlags changes);

    void update_count_label_idle();
    bool update_count_label();

    void prefsChanged(tr_quark key);

    static Glib::RefPtr<Gtk::ListStore> show_mode_filter_model_new();
    static void status_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    static bool show_mode_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);

    static Glib::RefPtr<Gtk::TreeStore> tracker_filter_model_new();
    static void tracker_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    static bool is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);

    static Glib::RefPtr<Gtk::ListStore> label_filter_model_new();
    static void label_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    static bool label_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);

    static Glib::RefPtr<Gtk::ListStore> volume_filter_model_new();
    static void volume_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    static bool volume_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);

    static Glib::ustring get_name_from_host(std::string const& host);

    static Gtk::CellRendererText* number_renderer_new();

private:
    FilterBar& widget_;
    Glib::RefPtr<Session> const core_;

    Glib::RefPtr<Gtk::ListStore> const show_mode_model_;
    Glib::RefPtr<Gtk::TreeStore> const tracker_model_;
    Glib::RefPtr<Gtk::ListStore> const label_model_;
    Glib::RefPtr<Gtk::ListStore> const volume_model_;

    Gtk::ComboBox* show_mode_ = nullptr;
    Gtk::ComboBox* tracker_ = nullptr;
    Gtk::ComboBox* label_ = nullptr;
    Gtk::ComboBox* volume_ = nullptr;
    Gtk::Entry* entry_ = nullptr;
    Gtk::Label* show_lb_ = nullptr;

    sigc::connection volume_monitor_mount_added_tag_;
    sigc::connection volume_monitor_mount_removed_tag_;
    Glib::RefPtr<TorrentFilter> filter_ = TorrentFilter::create();
    Glib::RefPtr<FilterListModel<Torrent>> filter_model_;

    sigc::connection update_count_label_tag_;
    sigc::connection update_filter_models_tag_;
    sigc::connection update_filter_models_on_add_remove_tag_;
    sigc::connection update_filter_models_on_change_tag_;
    sigc::connection pref_handler_id_;
};

// --- TRACKERS

namespace
{
class TrackerFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TrackerFilterModelColumns() noexcept
    {
        add(displayname);
        add(count);
        add(type);
        add(sitename);
        add(pixbuf);
    }

    Gtk::TreeModelColumn<Glib::ustring> displayname; /* human-readable name; ie, Legaltorrents */
    Gtk::TreeModelColumn<int> count; /* how many matches there are */
    Gtk::TreeModelColumn<TrackerType> type;
    Gtk::TreeModelColumn<Glib::ustring> sitename; // pattern-matching text; see tr_parsed_url.sitename
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> pixbuf;
};

TrackerFilterModelColumns const tracker_filter_cols;

} // namespace

/* human-readable name; ie, Legaltorrents */
Glib::ustring FilterBar::Impl::get_name_from_host(std::string const& host)
{
    std::string name = host;

    if (!name.empty())
    {
        name.front() = Glib::Ascii::toupper(name.front());
    }

    return name;
}

void FilterBar::Impl::tracker_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(tracker_filter_cols.count))
    {
        iter->set_value(tracker_filter_cols.count, n);
    }
}

void FilterBar::Impl::favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, Gtk::TreeModel::Path const& path)
{
    if (pixbuf != nullptr && *pixbuf != nullptr)
    {
        if (auto const iter = tracker_model_->get_iter(path); iter)
        {
            iter->set_value(tracker_filter_cols.pixbuf, *pixbuf);
        }
    }
}

bool FilterBar::Impl::tracker_filter_model_update()
{
    struct site_info
    {
        int count = 0;
        std::string host;
        std::string sitename;
        std::string announce_url;

        TR_CONSTEXPR_STR auto operator<=>(site_info const& that) const noexcept
        {
            return sitename <=> that.sitename;
        }

        TR_CONSTEXPR_STR auto operator==(site_info const& that) const
        {
            return sitename == that.sitename;
        }
    };

    auto const torrents_model = core_->get_model();

    /* Walk through all the torrents, tallying how many matches there are
     * for the various categories. Also make a sorted list of all tracker
     * hosts s.t. we can merge it with the existing list */
    auto n_torrents = 0;
    auto site_infos = std::unordered_map<std::string /*site*/, site_info>{};
    for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
    {
        auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
        if (torrent == nullptr)
        {
            continue;
        }

        auto const& raw_torrent = torrent->get_underlying();

        auto site_to_host_and_announce = std::map<std::string, std::pair<std::string, std::string>>{};
        for (size_t j = 0, n = tr_torrentTrackerCount(&raw_torrent); j < n; ++j)
        {
            auto const view = tr_torrentTracker(&raw_torrent, j);
            site_to_host_and_announce.try_emplace(std::data(view.sitename), view.host_and_port, view.announce);
        }

        for (auto const& [sitename, host_and_announce] : site_to_host_and_announce)
        {
            auto& info = site_infos[sitename];
            info.host = host_and_announce.first;
            info.announce_url = host_and_announce.second;
            info.sitename = sitename;
            ++info.count;
        }

        ++n_torrents;
    }

    auto const n_sites = std::size(site_infos);
    auto sites_v = std::vector<site_info>(n_sites);
    std::ranges::transform(site_infos, std::begin(sites_v), [](auto const& it) { return it.second; });
    std::ranges::sort(sites_v);

    // update the "all" count
    auto iter = tracker_model_->children().begin();
    if (iter)
    {
        tracker_model_update_count(iter, n_torrents);
    }

    // offset past the "All" and the separator
    ++iter;
    ++iter;

    size_t i = 0;
    for (;;)
    {
        // are we done yet?
        bool const new_sites_done = i >= n_sites;
        bool const old_sites_done = !iter;
        if (new_sites_done && old_sites_done)
        {
            break;
        }

        // decide what to do
        bool remove_row = false;
        bool insert_row = false;
        if (new_sites_done)
        {
            remove_row = true;
        }
        else if (old_sites_done)
        {
            insert_row = true;
        }
        else
        {
            auto const sitename = iter->get_value(tracker_filter_cols.sitename);
            int const cmp = sitename.raw().compare(sites_v.at(i).sitename);

            if (cmp < 0)
            {
                remove_row = true;
            }
            else if (cmp > 0)
            {
                insert_row = true;
            }
        }

        // do something
        if (remove_row)
        {
            iter = tracker_model_->erase(iter);
        }
        else if (insert_row)
        {
            auto const& site = sites_v.at(i);
            auto const add = tracker_model_->insert(iter);
            add->set_value(tracker_filter_cols.sitename, Glib::ustring{ site.sitename });
            add->set_value(tracker_filter_cols.displayname, get_name_from_host(site.sitename));
            add->set_value(tracker_filter_cols.count, site.count);
            add->set_value(tracker_filter_cols.type, TrackerType::HOST);
            auto path = tracker_model_->get_path(add);
            core_->favicon_cache().load(
                site.announce_url,
                [this, path = std::move(path)](auto const* pixbuf) { favicon_ready_cb(pixbuf, path); });
            ++i;
        }
        else // update row
        {
            tracker_model_update_count(iter, sites_v.at(i).count);
            ++iter;
            ++i;
        }
    }

    return false;
}

Glib::RefPtr<Gtk::TreeStore> FilterBar::Impl::tracker_filter_model_new()
{
    auto store = Gtk::TreeStore::create(tracker_filter_cols);

    auto iter = store->append();
    iter->set_value(tracker_filter_cols.displayname, Glib::ustring(_("All")));
    iter->set_value(tracker_filter_cols.type, TrackerType::ALL);

    iter = store->append();
    iter->set_value(tracker_filter_cols.type, TrackerSeparator);

    return store;
}

bool FilterBar::Impl::is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(tracker_filter_cols.type) == TrackerSeparator;
}

void FilterBar::Impl::render_pixbuf_func(Gtk::CellRendererPixbuf& cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    cell_renderer.property_width() = TrackerType{ iter->get_value(tracker_filter_cols.type) } == TrackerType::HOST ? 20 : 0;
}

void FilterBar::Impl::render_number_func(Gtk::CellRendererText& cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    auto const count = iter->get_value(tracker_filter_cols.count);
    cell_renderer.property_text() = count >= 0 ? fmt::format("{:L}", count) : "";
}

Gtk::CellRendererText* FilterBar::Impl::number_renderer_new()
{
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();

    r->property_alignment() = TR_PANGO_ALIGNMENT(RIGHT);
    r->property_weight() = TR_PANGO_WEIGHT(ULTRALIGHT);
    r->property_xalign() = 1.0;
    r->property_xpad() = GUI_PAD;

    return r;
}

void FilterBar::Impl::tracker_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(tracker_model_);
    combo.set_row_separator_func(sigc::hide<0>(&Impl::is_it_a_separator));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        combo.pack_start(*r, false);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_pixbuf_func(*r, iter); });
        combo.add_attribute(r->property_pixbuf(), tracker_filter_cols.pixbuf);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_text(), tracker_filter_cols.displayname);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }
}

// --- Labels

namespace
{

class LabelFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    LabelFilterModelColumns() noexcept
    {
        add(displayname);
        add(count);
        add(type);
        add(name);
    }

    Gtk::TreeModelColumn<Glib::ustring> displayname;
    Gtk::TreeModelColumn<int> count;
    Gtk::TreeModelColumn<LabelType> type;
    Gtk::TreeModelColumn<Glib::ustring> name;
};

LabelFilterModelColumns const label_filter_cols;

} // namespace

void FilterBar::Impl::label_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(label_filter_cols.count))
    {
        iter->set_value(label_filter_cols.count, n);
    }
}

bool FilterBar::Impl::label_filter_model_update()
{
    auto const torrents_model = core_->get_model();

    auto label_counts = std::map<Glib::ustring, int>{};
    auto n_torrents = 0;
    auto n_unlabelled = 0;

    for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
    {
        auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
        if (torrent == nullptr)
        {
            continue;
        }

        auto const& labels = torrent->get_labels();
        if (labels.empty())
        {
            ++n_unlabelled;
        }
        else
        {
            for (auto const& label : labels)
            {
                ++label_counts[label];
            }
        }

        ++n_torrents;
    }

    // Update the "All" row count
    auto iter = label_model_->children().begin();
    if (iter)
    {
        label_model_update_count(iter, n_torrents);
    }

    // Update the "No Label" row count
    ++iter;
    if (iter)
    {
        label_model_update_count(iter, n_unlabelled);
    }

    // Skip past separator to first label row
    ++iter;
    ++iter;

    // Merge sorted label list into the model
    auto sorted_labels = std::vector<std::pair<Glib::ustring, int>>(label_counts.begin(), label_counts.end());

    size_t i = 0;
    auto const n_labels = sorted_labels.size();

    for (;;)
    {
        bool const new_done = i >= n_labels;
        bool const old_done = !iter;

        if (new_done && old_done)
        {
            break;
        }

        bool remove_row = false;
        bool insert_row = false;

        if (new_done)
        {
            remove_row = true;
        }
        else if (old_done)
        {
            insert_row = true;
        }
        else
        {
            auto const existing = iter->get_value(label_filter_cols.name);
            int const cmp = existing.raw().compare(sorted_labels.at(i).first.raw());

            if (cmp < 0)
            {
                remove_row = true;
            }
            else if (cmp > 0)
            {
                insert_row = true;
            }
        }

        if (remove_row)
        {
            iter = label_model_->erase(iter);
        }
        else if (insert_row)
        {
            auto const& [label_name, label_count] = sorted_labels.at(i);
            auto const add = label_model_->insert(iter);
            add->set_value(label_filter_cols.name, label_name);
            add->set_value(label_filter_cols.displayname, label_name);
            add->set_value(label_filter_cols.count, label_count);
            add->set_value(label_filter_cols.type, LabelType::LABEL);
            ++i;
        }
        else // update count
        {
            label_model_update_count(iter, sorted_labels.at(i).second);
            ++iter;
            ++i;
        }
    }

    return false;
}

Glib::RefPtr<Gtk::ListStore> FilterBar::Impl::label_filter_model_new()
{
    auto store = Gtk::ListStore::create(label_filter_cols);

    auto iter = store->append();
    iter->set_value(label_filter_cols.displayname, Glib::ustring(_("All")));
    iter->set_value(label_filter_cols.type, LabelType::ALL);

    iter = store->append();
    iter->set_value(label_filter_cols.displayname, Glib::ustring(_("No Label")));
    iter->set_value(label_filter_cols.type, LabelType::NO_LABEL);

    iter = store->append();
    iter->set_value(label_filter_cols.type, LabelSeparator);

    return store;
}

bool FilterBar::Impl::label_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(label_filter_cols.type) == LabelSeparator;
}

void FilterBar::Impl::label_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(label_model_);
    combo.set_row_separator_func(sigc::hide<0>(&Impl::label_is_it_a_separator));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_text(), label_filter_cols.displayname);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }
}

// --- Volumes

namespace
{

class VolumeFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    VolumeFilterModelColumns() noexcept
    {
        add(displayname);
        add(count);
        add(type);
        add(path);
    }

    Gtk::TreeModelColumn<Glib::ustring> displayname;
    Gtk::TreeModelColumn<int> count;
    Gtk::TreeModelColumn<VolumeType> type;
    Gtk::TreeModelColumn<Glib::ustring> path;
};

VolumeFilterModelColumns const volume_filter_cols;

} // namespace

void FilterBar::Impl::volume_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(volume_filter_cols.count))
    {
        iter->set_value(volume_filter_cols.count, n);
    }
}

bool FilterBar::Impl::volume_filter_model_update()
{
    // Build a map of volume path -> torrent count
    auto const torrents_model = core_->get_model();
    auto volume_counts = std::map<Glib::ustring, int>{};
    auto n_torrents = 0;

    for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
    {
        auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
        if (torrent == nullptr)
        {
            continue;
        }

        auto const download_dir = Glib::ustring{ std::string{ tr_torrentGetDownloadDir(&torrent->get_underlying()) } };

        // Assign torrent to the most-specific matching volume
        auto best_match = Glib::ustring{};
        for (auto const& [vpath, _] : volume_counts)
        {
            if (TorrentFilter::match_volume(*torrent, VolumeType::VOLUME, vpath) &&
                vpath.size() > best_match.size())
            {
                best_match = vpath;
            }
        }

        if (!best_match.empty())
        {
            ++volume_counts[best_match];
        }
        else
        {
            // Torrent doesn't match any existing volume entry yet; will be counted
            // once we know the full volume list — handled below
            (void)download_dir;
        }

        ++n_torrents;
    }

    // Rebuild volume list from GIO and count torrents per volume
    auto const monitor = Gio::VolumeMonitor::get();
    struct VolumeInfo
    {
        Glib::ustring displayname;
        Glib::ustring path;
        int count = 0;
    };
    auto volume_infos = std::vector<VolumeInfo>{};

    for (auto const& mount : monitor->get_mounts())
    {
        auto const root = mount->get_root();
        if (!root)
        {
            continue;
        }
        auto const path = Glib::ustring{ root->get_path() };
        if (path.empty() || path == "/")
        {
            continue; // skip root filesystem
        }
        auto count = 0;
        for (auto i = 0U, n = torrents_model->get_n_items(); i < n; ++i)
        {
            auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
            if (torrent != nullptr && TorrentFilter::match_volume(*torrent, VolumeType::VOLUME, path))
            {
                ++count;
            }
        }
        volume_infos.push_back({ mount->get_name(), path, count });
    }
    std::ranges::sort(volume_infos, [](auto const& a, auto const& b) { return a.path < b.path; });

    // Update "All" row
    auto iter = volume_model_->children().begin();
    if (iter)
    {
        volume_model_update_count(iter, n_torrents);
    }

    // Skip past separator to first volume row
    ++iter;
    ++iter;

    // Merge sorted volume list into model
    size_t i = 0;
    auto const n_volumes = volume_infos.size();

    for (;;)
    {
        bool const new_done = i >= n_volumes;
        bool const old_done = !iter;

        if (new_done && old_done)
        {
            break;
        }

        bool remove_row = false;
        bool insert_row = false;

        if (new_done)
        {
            remove_row = true;
        }
        else if (old_done)
        {
            insert_row = true;
        }
        else
        {
            auto const existing_path = iter->get_value(volume_filter_cols.path);
            int const cmp = existing_path.raw().compare(volume_infos.at(i).path.raw());
            if (cmp < 0)
            {
                remove_row = true;
            }
            else if (cmp > 0)
            {
                insert_row = true;
            }
        }

        if (remove_row)
        {
            iter = volume_model_->erase(iter);
        }
        else if (insert_row)
        {
            auto const& info = volume_infos.at(i);
            auto const add = volume_model_->insert(iter);
            add->set_value(volume_filter_cols.displayname, info.displayname);
            add->set_value(volume_filter_cols.path, info.path);
            add->set_value(volume_filter_cols.count, info.count);
            add->set_value(volume_filter_cols.type, VolumeType::VOLUME);
            ++i;
        }
        else // update count
        {
            volume_model_update_count(iter, volume_infos.at(i).count);
            ++iter;
            ++i;
        }
    }

    return false;
}

Glib::RefPtr<Gtk::ListStore> FilterBar::Impl::volume_filter_model_new()
{
    auto store = Gtk::ListStore::create(volume_filter_cols);

    auto iter = store->append();
    iter->set_value(volume_filter_cols.displayname, Glib::ustring(_("All")));
    iter->set_value(volume_filter_cols.type, VolumeType::ALL);

    iter = store->append();
    iter->set_value(volume_filter_cols.type, VolumeSeparator);

    return store;
}

bool FilterBar::Impl::volume_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(volume_filter_cols.type) == VolumeSeparator;
}

void FilterBar::Impl::volume_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(volume_model_);
    combo.set_row_separator_func(sigc::hide<0>(&Impl::volume_is_it_a_separator));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_text(), volume_filter_cols.displayname);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }
}

namespace
{

// --- Show Mode

class ShowModeFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    ShowModeFilterModelColumns() noexcept
    {
        add(name);
        add(count);
        add(show_mode);
        add(icon_name);
    }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<int> count;
    Gtk::TreeModelColumn<ShowMode> show_mode;
    Gtk::TreeModelColumn<Glib::ustring> icon_name;
};

ShowModeFilterModelColumns const show_mode_filter_cols;

} // namespace

bool FilterBar::Impl::show_mode_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(show_mode_filter_cols.show_mode) == ShowModeSeparator;
}

void FilterBar::Impl::status_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(show_mode_filter_cols.count))
    {
        iter->set_value(show_mode_filter_cols.count, n);
    }
}

bool FilterBar::Impl::show_mode_filter_model_update()
{
    auto const torrents_model = core_->get_model();

    for (auto& row : show_mode_model_->children())
    {
        auto const type = row.get_value(show_mode_filter_cols.show_mode);
        if (type == ShowModeSeparator)
        {
            continue;
        }

        auto hits = 0;

        for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
        {
            auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
            if (torrent != nullptr && TorrentFilter::match_mode(*torrent, static_cast<ShowMode>(type)))
            {
                ++hits;
            }
        }

        status_model_update_count(TR_GTK_TREE_MODEL_CHILD_ITER(row), hits);
    }

    return false;
}

Glib::RefPtr<Gtk::ListStore> FilterBar::Impl::show_mode_filter_model_new()
{
    struct FilterTypeInfo
    {
        ShowMode show_mode;
        char const* context;
        char const* name;
        char const* icon_name;
    };

    static auto constexpr types = std::array<FilterTypeInfo, 9>({ {
        { .show_mode = ShowMode::ShowAll, .context = nullptr, .name = N_("All"), .icon_name = nullptr },
        { .show_mode = ShowModeSeparator, .context = nullptr, .name = nullptr, .icon_name = nullptr },
        { .show_mode = ShowMode::ShowActive, .context = nullptr, .name = N_("Active"), .icon_name = "system-run" },
        { .show_mode = ShowMode::ShowDownloading,
          .context = "Verb",
          .name = NC_("Verb", "Downloading"),
          .icon_name = "network-receive" },
        { .show_mode = ShowMode::ShowSeeding,
          .context = "Verb",
          .name = NC_("Verb", "Seeding"),
          .icon_name = "network-transmit" },
        { .show_mode = ShowMode::ShowPaused, .context = nullptr, .name = N_("Paused"), .icon_name = "media-playback-pause" },
        { .show_mode = ShowMode::ShowFinished, .context = nullptr, .name = N_("Finished"), .icon_name = "media-playback-stop" },
        { .show_mode = ShowMode::ShowVerifying,
          .context = "Verb",
          .name = NC_("Verb", "Verifying"),
          .icon_name = "view-refresh" },
        { .show_mode = ShowMode::ShowError, .context = nullptr, .name = N_("Error"), .icon_name = "dialog-error" },
    } });

    auto store = Gtk::ListStore::create(show_mode_filter_cols);

    for (auto const& type : types)
    {
        auto const name = type.name != nullptr ?
            Glib::ustring(type.context != nullptr ? g_dpgettext2(nullptr, type.context, type.name) : _(type.name)) :
            Glib::ustring();
        auto const iter = store->append();
        iter->set_value(show_mode_filter_cols.name, name);
        iter->set_value(show_mode_filter_cols.show_mode, type.show_mode);
        iter->set_value(show_mode_filter_cols.icon_name, Glib::ustring(type.icon_name != nullptr ? type.icon_name : ""));
    }

    return store;
}

void FilterBar::Impl::render_show_mode_pixbuf_func(
    Gtk::CellRendererPixbuf& cell_renderer,
    Gtk::TreeModel::const_iterator const& iter)
{
    auto const type = ShowMode{ iter->get_value(show_mode_filter_cols.show_mode) };
    cell_renderer.property_width() = type == ShowMode::ShowAll ? 0 : 20;
    cell_renderer.property_ypad() = type == ShowMode::ShowAll ? 0 : 2;
}

void FilterBar::Impl::show_mode_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(show_mode_model_);
    combo.set_row_separator_func(sigc::hide<0>(&Impl::show_mode_is_it_a_separator));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_icon_name(), show_mode_filter_cols.icon_name);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_show_mode_pixbuf_func(*r, iter); });
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, true);
        combo.add_attribute(r->property_text(), show_mode_filter_cols.name);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }
}

void FilterBar::Impl::update_filter_text()
{
    filter_->set_text(entry_->get_text());
}

void FilterBar::Impl::update_filter_label()
{
    if (auto const iter = label_->get_active(); iter)
    {
        filter_->set_label(
            static_cast<LabelType>(iter->get_value(label_filter_cols.type)),
            iter->get_value(label_filter_cols.name));
    }
    else
    {
        filter_->set_label(LabelType::ALL, {});
    }
}

void FilterBar::Impl::update_filter_volume()
{
    if (auto const iter = volume_->get_active(); iter)
    {
        filter_->set_volume(
            static_cast<VolumeType>(iter->get_value(volume_filter_cols.type)),
            iter->get_value(volume_filter_cols.path));
    }
    else
    {
        filter_->set_volume(VolumeType::ALL, {});
    }
}

void FilterBar::Impl::update_filter_show_mode()
{
    /* set active_show_mode_type_ from the show_mode combobox */
    if (auto const iter = show_mode_->get_active(); iter)
    {
        filter_->set_mode(ShowMode{ iter->get_value(show_mode_filter_cols.show_mode) });
    }
    else
    {
        filter_->set_mode(ShowMode::ShowAll);
    }
}

void FilterBar::Impl::update_filter_tracker()
{
    /* set the active tracker type & host from the tracker combobox */
    if (auto const iter = tracker_->get_active(); iter)
    {
        filter_->set_tracker(
            static_cast<TrackerType>(iter->get_value(tracker_filter_cols.type)),
            iter->get_value(tracker_filter_cols.sitename));
    }
    else
    {
        filter_->set_tracker(TrackerType::ALL, {});
    }
}

bool FilterBar::Impl::update_count_label()
{
    /* get the visible count */
    auto const visibleCount = static_cast<int>(filter_model_->get_n_items());

    /* get the tracker count */
    int trackerCount = 0;
    if (auto const iter = tracker_->get_active(); iter)
    {
        trackerCount = iter->get_value(tracker_filter_cols.count);
    }

    /* get the label count */
    int labelCount = 0;
    if (auto const iter = label_->get_active(); iter)
    {
        labelCount = iter->get_value(label_filter_cols.count);
    }

    /* get the volume count */
    int volumeCount = 0;
    if (auto const iter = volume_->get_active(); iter)
    {
        volumeCount = iter->get_value(volume_filter_cols.count);
    }

    /* get the mode count */
    int modeCount = 0;
    if (auto const iter = show_mode_->get_active(); iter)
    {
        modeCount = iter->get_value(show_mode_filter_cols.count);
    }

    /* set the text */
    if (auto const new_markup = visibleCount == std::min({ modeCount, trackerCount, labelCount, volumeCount }) ?
            _("_Show:") :
            fmt::format(fmt::runtime(_("_Show {count:L} of:")), fmt::arg("count", visibleCount));
        new_markup != show_lb_->get_label().raw())
    {
        show_lb_->set_markup_with_mnemonic(new_markup);
    }

    return false;
}

void FilterBar::Impl::update_count_label_idle()
{
    if (!update_count_label_tag_.connected())
    {
        update_count_label_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::update_count_label));
    }
}

void FilterBar::Impl::update_filter_models(Torrent::ChangeFlags changes)
{
    static auto TR_CONSTEXPR23 show_mode_flags = Torrent::ChangeFlag::ACTIVE_PEERS_DOWN | Torrent::ChangeFlag::ACTIVE_PEERS_UP |
        Torrent::ChangeFlag::ACTIVE | Torrent::ChangeFlag::ACTIVITY | Torrent::ChangeFlag::ERROR_CODE |
        Torrent::ChangeFlag::FINISHED;
    static auto constexpr tracker_flags = Torrent::ChangeFlag::TRACKERS;
    static auto constexpr label_flags = Torrent::ChangeFlag::LABELS;

    if (changes.test(show_mode_flags))
    {
        show_mode_filter_model_update();
    }

    if (changes.test(tracker_flags))
    {
        tracker_filter_model_update();
    }

    if (changes.test(label_flags))
    {
        label_filter_model_update();
    }

    // Volume counts are always refreshed since download_dir has no ChangeFlag
    volume_filter_model_update();

    filter_->update(changes);

    if (changes.test(show_mode_flags | tracker_flags | label_flags))
    {
        update_count_label_idle();
    }
}

void FilterBar::Impl::update_filter_models_idle(Torrent::ChangeFlags changes)
{
    if (!update_filter_models_tag_.connected())
    {
        update_filter_models_tag_ = Glib::signal_idle().connect(
            [this, changes]()
            {
                update_filter_models(changes);
                return false;
            });
    }
}

/***
****
***/

FilterBarExtraInit::FilterBarExtraInit()
    : ExtraClassInit(&FilterBarExtraInit::class_init, nullptr, &FilterBarExtraInit::instance_init)
{
}

void FilterBarExtraInit::class_init(void* klass, void* /*user_data*/)
{
    auto* const widget_klass = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_klass, gtr_get_full_resource_path("FilterBar.ui").c_str());

    gtk_widget_class_bind_template_child_full(widget_klass, "show_mode_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "tracker_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "label_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "volume_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "text_entry", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "show_label", FALSE, 0);
}

void FilterBarExtraInit::instance_init(GTypeInstance* instance, void* /*klass*/)
{
    gtk_widget_init_template(GTK_WIDGET(instance));
}

/***
****
***/

FilterBar::FilterBar()
    : Glib::ObjectBase(typeid(FilterBar))
{
}

FilterBar::FilterBar(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& /*builder*/,
    Glib::RefPtr<Session> const& core)
    : Glib::ObjectBase(typeid(FilterBar))
    , Gtk::Box(cast_item)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

FilterBar::~FilterBar() = default;

FilterBar::Impl::Impl(FilterBar& widget, Glib::RefPtr<Session> const& core)
    : widget_(widget)
    , core_(core)
    , show_mode_model_(show_mode_filter_model_new())
    , tracker_model_(tracker_filter_model_new())
    , label_model_(label_filter_model_new())
    , volume_model_(volume_filter_model_new())
    , show_mode_(get_template_child<Gtk::ComboBox>("show_mode_combo"))
    , tracker_(get_template_child<Gtk::ComboBox>("tracker_combo"))
    , label_(get_template_child<Gtk::ComboBox>("label_combo"))
    , volume_(get_template_child<Gtk::ComboBox>("volume_combo"))
    , entry_(get_template_child<Gtk::Entry>("text_entry"))
    , show_lb_(get_template_child<Gtk::Label>("show_label"))
{
    update_filter_models_on_add_remove_tag_ = core_->get_model()->signal_items_changed().connect(
        [this](guint /*position*/, guint /*removed*/, guint /*added*/) { update_filter_models_idle(~Torrent::ChangeFlags()); });
    update_filter_models_on_change_tag_ = core_->signal_torrents_changed().connect(
        sigc::hide<0>(sigc::mem_fun(*this, &Impl::update_filter_models_idle)));

    show_mode_filter_model_update();
    tracker_filter_model_update();
    label_filter_model_update();
    volume_filter_model_update();

    show_mode_combo_box_init(*show_mode_);
    tracker_combo_box_init(*tracker_);
    label_combo_box_init(*label_);
    volume_combo_box_init(*volume_);

    filter_->signal_changed().connect([this](auto /*changes*/) { update_count_label_idle(); });

    filter_model_ = FilterListModel<Torrent>::create(core_->get_sorted_model(), filter_);

    tracker_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_tracker));
    label_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_label));
    volume_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_volume));
    show_mode_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_show_mode));

    // Rebuild volume list when drives are mounted or unmounted
    auto const monitor = Gio::VolumeMonitor::get();
    volume_monitor_mount_added_tag_ = monitor->signal_mount_added().connect(
        [this](auto const& /*mount*/) { volume_filter_model_update(); });
    volume_monitor_mount_removed_tag_ = monitor->signal_mount_removed().connect(
        [this](auto const& /*mount*/) { volume_filter_model_update(); });

    prefsChanged(TR_KEY_show_tracker_combo);
    prefsChanged(TR_KEY_show_label_combo);
    prefsChanged(TR_KEY_show_volume_combo);
    pref_handler_id_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &Impl::prefsChanged));

#if GTKMM_CHECK_VERSION(4, 0, 0)
    entry_->signal_icon_release().connect([this](auto /*icon_position*/) { entry_->set_text({}); });
#else
    entry_->signal_icon_release().connect([this](auto /*icon_position*/, auto const* /*event*/) { entry_->set_text({}); });
#endif
    entry_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_text));
}

void FilterBar::Impl::prefsChanged(tr_quark const key)
{
    switch (key)
    {
    case TR_KEY_show_tracker_combo:
        tracker_->set_visible(gtr_pref_flag_get(key));
        break;

    case TR_KEY_show_label_combo:
        label_->set_visible(gtr_pref_flag_get(key));
        break;

    case TR_KEY_show_volume_combo:
        volume_->set_visible(gtr_pref_flag_get(key));
        break;

    default:
        break;
    }
}

FilterBar::Impl::~Impl()
{
    volume_monitor_mount_removed_tag_.disconnect();
    volume_monitor_mount_added_tag_.disconnect();
    pref_handler_id_.disconnect();
    update_filter_models_on_change_tag_.disconnect();
    update_filter_models_on_add_remove_tag_.disconnect();
    update_filter_models_tag_.disconnect();
    update_count_label_tag_.disconnect();
}

Glib::RefPtr<FilterBar::Model> FilterBar::get_filter_model() const
{
    return impl_->get_filter_model();
}

Glib::RefPtr<FilterBar::Impl::FilterModel> FilterBar::Impl::get_filter_model() const
{
    return filter_model_;
}

template<typename T>
T* FilterBar::Impl::get_template_child(char const* name) const
{
    auto full_type_name = std::string("gtkmm__CustomObject_");
    Glib::append_canonical_typename(full_type_name, typeid(FilterBar).name());

    return Glib::wrap(G_TYPE_CHECK_INSTANCE_CAST(
        gtk_widget_get_template_child(GTK_WIDGET(widget_.gobj()), g_type_from_name(full_type_name.c_str()), name),
        T::get_base_type(),
        typename T::BaseObjectType));
}
