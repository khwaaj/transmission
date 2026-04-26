// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "RelocateDialog.h"

#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/checkbutton.h>

#include <memory>
#include <string>

namespace
{

std::string targetLocation;

}

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

    RelocateDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    std::vector<tr_torrent_id_t> torrent_ids_;

    PathButton* chooser_ = nullptr;
    Gtk::CheckButton* move_tb_ = nullptr;
};

void RelocateDialog::Impl::onResponse(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(APPLY))
    {
        auto const location = chooser_->get_filename();
        auto const do_move = move_tb_->get_active();

        targetLocation = location;
        gtr_save_recent_dir("relocate", core_, location);

        for (auto const id : torrent_ids_)
        {
            if (auto* const tor = core_->find_torrent(id); tor != nullptr)
            {
                tr_torrentSetLocation(tor, targetLocation.c_str(), do_move, nullptr);
            }
        }
    }

    dialog_.close();
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
    , chooser_(gtr_get_widget_derived<PathButton>(builder, "new_location_button"))
    , move_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "move_data_radio"))
{
    dialog_.set_default_response(TR_GTK_RESPONSE_TYPE(CANCEL));
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::onResponse));

    auto recent_dirs = gtr_get_recent_dirs("relocate");
    if (recent_dirs.empty())
    {
        chooser_->set_filename(gtr_pref_string_get(TR_KEY_download_dir));
    }
    else
    {
        chooser_->set_filename(recent_dirs.front());
        recent_dirs.pop_front();
        chooser_->set_shortcut_folders(recent_dirs);
    }
}
