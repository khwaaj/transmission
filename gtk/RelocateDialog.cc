// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "RelocateDialog.h"

#include "GtkCompat.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

#include <giomm/file.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/liststore.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{

class LocationColumns : public Gtk::TreeModelColumnRecord
{
public:
    LocationColumns()
    {
        add(full_path);
        add(display_label);
    }

    Gtk::TreeModelColumn<std::string> full_path;
    Gtk::TreeModelColumn<Glib::ustring> display_label;
};

LocationColumns const location_cols;

// Returns the minimum trailing-component suffix of 'path' that is unique
// among all_paths. E.g. for {"/mnt/d1/Movies", "/mnt/d2/Movies"}:
//   "/mnt/d1/Movies" → "d1/Movies", "/mnt/d2/Movies" → "d2/Movies"
Glib::ustring compute_display_label(std::string const& path, std::vector<std::string> const& all_paths)
{
    if (path.empty())
        return {};

    auto split = [](std::string const& p)
    {
        auto parts = std::vector<std::string>{};
        size_t start = 0;
        while (start < p.size())
        {
            auto const pos = p.find('/', start);
            auto const len = (pos == std::string::npos) ? pos : pos - start;
            if (auto part = p.substr(start, len); !part.empty())
                parts.push_back(std::move(part));
            if (pos == std::string::npos)
                break;
            start = pos + 1;
        }
        return parts;
    };

    auto const comps = split(path);
    auto const n = comps.size();
    if (n == 0)
        return Glib::ustring{ path };

    for (size_t depth = 1; depth <= n; ++depth)
    {
        // Build the suffix label from the last 'depth' components
        auto label = std::string{};
        for (size_t i = n - depth; i < n; ++i)
        {
            if (i > n - depth)
                label += '/';
            label += comps[i];
        }

        // Check if any other path has the same suffix
        bool unique = true;
        for (auto const& other : all_paths)
        {
            if (other == path)
                continue;
            auto const other_comps = split(other);
            auto const m = other_comps.size();
            if (m < depth)
                continue;

            auto other_label = std::string{};
            for (size_t i = m - depth; i < m; ++i)
            {
                if (i > m - depth)
                    other_label += '/';
                other_label += other_comps[i];
            }

            if (label == other_label)
            {
                unique = false;
                break;
            }
        }

        if (unique)
            return Glib::ustring{ label };
    }

    return Glib::ustring{ path };
}

} // namespace

class RelocateDialog::Impl
{
public:
    Impl(
        RelocateDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

private:
    void onResponse(int response);
    void on_browse_clicked();
    void add_and_select_path(std::string const& path);
    void rebuild_labels();

    RelocateDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    std::vector<tr_torrent_id_t> torrent_ids_;

    Gtk::ComboBox* location_combo_ = nullptr;
    Gtk::Button* browse_button_ = nullptr;
    Gtk::CheckButton* move_tb_ = nullptr;

    Glib::RefPtr<Gtk::ListStore> location_model_;
    std::vector<std::string> paths_;
};

void RelocateDialog::Impl::onResponse(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(APPLY))
    {
        auto const iter = location_combo_->get_active();
        if (!iter)
            return;

        auto const location = iter->get_value(location_cols.full_path);
        if (location.empty())
            return;

        auto const do_move = move_tb_->get_active();
        gtr_save_recent_dir("relocate", core_, location);

        for (auto const id : torrent_ids_)
        {
            if (auto* const tor = core_->find_torrent(id); tor != nullptr)
            {
                tr_torrentSetLocation(tor, location.c_str(), do_move);
            }
        }
    }

    dialog_.close();
}

void RelocateDialog::Impl::rebuild_labels()
{
    auto const rows = location_model_->children();
    for (size_t i = 0; i < paths_.size(); ++i)
        rows[i].set_value(location_cols.display_label, compute_display_label(paths_[i], paths_));
}

void RelocateDialog::Impl::add_and_select_path(std::string const& path)
{
    // Remove duplicate entry if present, keeping paths_ and model in sync
    if (auto const it = std::find(paths_.begin(), paths_.end(), path); it != paths_.end())
    {
        auto model_iter = location_model_->children().begin();
        std::advance(model_iter, std::distance(paths_.begin(), it));
        location_model_->erase(model_iter);
        paths_.erase(it);
    }

    paths_.insert(paths_.begin(), path);
    auto const new_row = location_model_->prepend();
    new_row->set_value(location_cols.full_path, path);

    rebuild_labels();

    location_combo_->set_active(new_row);
    location_combo_->set_tooltip_text(path);
}

void RelocateDialog::Impl::on_browse_clicked()
{
    auto chooser = Gtk::FileChooserNative::create(
        _("Choose a folder"),
        TR_GTK_FILE_CHOOSER_ACTION(SELECT_FOLDER),
        _("_Select"),
        _("_Cancel"));
    chooser->set_transient_for(dialog_);
    chooser->set_modal(true);

    if (auto const iter = location_combo_->get_active(); iter)
    {
        if (auto const current = iter->get_value(location_cols.full_path); !current.empty())
            chooser->set_file(Gio::File::create_for_path(current));
    }

    chooser->signal_response().connect(
        [this, chooser](int response) mutable
        {
            if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
                add_and_select_path(chooser->get_file()->get_path());
            chooser.reset();
        });

    chooser->show();
}

RelocateDialog::RelocateDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<int> const& torrent_ids)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, torrent_ids))
{
    set_transient_for(parent);
}

RelocateDialog::~RelocateDialog() = default;

std::unique_ptr<RelocateDialog> RelocateDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("RelocateDialog.ui"));
    return std::unique_ptr<RelocateDialog>(
        gtr_get_widget_derived<RelocateDialog>(builder, "RelocateDialog", parent, core, torrent_ids));
}

RelocateDialog::Impl::Impl(
    RelocateDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : dialog_(dialog)
    , core_(core)
    , torrent_ids_(torrent_ids)
    , location_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "location_combo"))
    , browse_button_(gtr_get_widget<Gtk::Button>(builder, "browse_button"))
    , move_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "move_data_radio"))
    , location_model_(Gtk::ListStore::create(location_cols))
{
    dialog_.set_default_response(TR_GTK_RESPONSE_TYPE(CANCEL));
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::onResponse));
    browse_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_browse_clicked));

    // Seed path list from recent dirs, falling back to the default download dir
    auto recent_dirs = gtr_get_recent_dirs("relocate");
    if (recent_dirs.empty())
        recent_dirs.push_back(gtr_pref_string_get(TR_KEY_download_dir));

    for (auto const& dir : recent_dirs)
        paths_.push_back(dir);

    // Populate model (labels computed after all paths are known)
    for (auto const& p : paths_)
    {
        auto const row = location_model_->append();
        row->set_value(location_cols.full_path, p);
    }
    rebuild_labels();

    // Wire up the combo
    location_combo_->set_model(location_model_);

    auto* const renderer = Gtk::make_managed<Gtk::CellRendererText>();
    location_combo_->pack_start(*renderer, true);
    location_combo_->set_cell_data_func(
        *renderer,
        [renderer](Gtk::TreeModel::const_iterator const& iter)
        { renderer->property_text() = iter->get_value(location_cols.display_label); });

    location_combo_->set_active(0);

    // Keep tooltip in sync with the selected full path
    location_combo_->signal_changed().connect(
        [this]()
        {
            if (auto const iter = location_combo_->get_active(); iter)
                location_combo_->set_tooltip_text(iter->get_value(location_cols.full_path));
        });

    if (!paths_.empty())
        location_combo_->set_tooltip_text(paths_.front());
}
