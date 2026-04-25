// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "EditLabelsDialog.h"

#include "GtkCompat.h"
#include "Session.h"
#include "Torrent.h"
#include "Utils.h"

#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class EditLabelsDialog::Impl
{
public:
    Impl(
        EditLabelsDialog& dialog,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

private:
    struct LabelRow
    {
        Glib::ustring name;
        Gtk::CheckButton* check; // owned by the ListBox
    };

    void add_label_row(Glib::ustring const& name, bool active, bool inconsistent);
    void on_add_button_clicked();
    void on_response(int response);

    EditLabelsDialog& dialog_;
    Glib::RefPtr<Session> core_;
    std::vector<tr_torrent_id_t> torrent_ids_;

    Gtk::ListBox* list_box_ = nullptr;
    Gtk::Entry* new_label_entry_ = nullptr;
    std::vector<LabelRow> rows_;
};

void EditLabelsDialog::Impl::add_label_row(
    Glib::ustring const& name,
    bool active,
    bool inconsistent)
{
    auto* check = Gtk::make_managed<Gtk::CheckButton>(name);
    check->set_active(active);
    check->set_inconsistent(inconsistent);
    check->show();

    // Clicking an inconsistent button should toggle to checked
    check->signal_toggled().connect(
        [check]()
        {
            if (check->get_inconsistent())
            {
                check->set_inconsistent(false);
                check->set_active(true);
            }
        });

    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_activatable(false);
    IF_GTKMM4(row->set_child(*check), row->add(*check));
    IF_GTKMM4(row->show(), row->show_all());
    IF_GTKMM4(list_box_->append(*row), list_box_->add(*row));

    rows_.push_back({ name, check });
}

void EditLabelsDialog::Impl::on_add_button_clicked()
{
    Glib::ustring const name = new_label_entry_->get_text();
    if (name.empty())
    {
        return;
    }

    // Don't add duplicates
    for (auto const& row : rows_)
    {
        if (row.name == name)
        {
            new_label_entry_->set_text({});
            return;
        }
    }

    add_label_row(name, /*active=*/true, /*inconsistent=*/false);
    new_label_entry_->set_text({});
}

void EditLabelsDialog::Impl::on_response(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
    {
        // Build the desired label state from the checklist
        // active + !inconsistent  → add to all selected torrents
        // !active + !inconsistent → remove from all selected torrents
        // inconsistent            → leave each torrent's existing state

        struct LabelAction
        {
            bool add_to_all = false;
            bool remove_from_all = false;
        };

        std::map<Glib::ustring, LabelAction> actions;
        for (auto const& row : rows_)
        {
            LabelAction act;
            if (!row.check->get_inconsistent())
            {
                if (row.check->get_active())
                {
                    act.add_to_all = true;
                }
                else
                {
                    act.remove_from_all = true;
                }
            }
            actions[row.name] = act;
        }

        // Apply per-torrent
        for (auto const id : torrent_ids_)
        {
            tr_torrent* const tor = core_->find_torrent(id);
            if (tor == nullptr)
            {
                continue;
            }

            // Collect current labels
            std::set<Glib::ustring> current_set;
            for (auto i = size_t{ 0 }, n = tr_torrentLabelCount(tor); i < n; ++i)
            {
                current_set.emplace(std::string{ tr_torrentLabel(tor, i) });
            }

            // Apply actions
            for (auto const& [label, act] : actions)
            {
                if (act.add_to_all)
                {
                    current_set.insert(label);
                }
                else if (act.remove_from_all)
                {
                    current_set.erase(label);
                }
                // inconsistent: leave current_set unchanged for this label
            }

            // Build variant and call torrent-set
            auto labels_vec = tr_variant::Vector{};
            labels_vec.reserve(current_set.size());
            for (auto const& lbl : current_set)
            {
                labels_vec.emplace_back(std::string{ lbl });
            }

            auto params = tr_variant::Map{ 2U };
            params.try_emplace(TR_KEY_labels, std::move(labels_vec));
            params.try_emplace(TR_KEY_ids, Session::to_variant(std::vector{ id }));
            core_->exec(TR_KEY_torrent_set, std::move(params));
        }
    }

    dialog_.close();
}

EditLabelsDialog::Impl::Impl(
    EditLabelsDialog& dialog,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : dialog_(dialog)
    , core_(core)
    , torrent_ids_(torrent_ids)
{
    dialog_.set_title(_("Edit Labels"));
    dialog_.set_default_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
    dialog_.add_button(_("_Cancel"), TR_GTK_RESPONSE_TYPE(CANCEL));
    dialog_.add_button(_("_Apply"), TR_GTK_RESPONSE_TYPE(ACCEPT));
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::on_response));

    // Seed label_count with every label known across all torrents (count = 0)
    std::map<Glib::ustring, int> label_count;
    auto const all_model = core_->get_model();
    for (auto i = 0U, n = all_model->get_n_items(); i < n; ++i)
    {
        if (auto const torrent = gtr_ptr_dynamic_cast<Torrent>(all_model->get_object(i)); torrent)
        {
            for (auto const& lbl : torrent->get_labels())
            {
                label_count.try_emplace(lbl, 0);
            }
        }
    }

    // Count how many selected torrents have each label
    int const n_torrents = static_cast<int>(torrent_ids.size());
    for (auto const id : torrent_ids)
    {
        tr_torrent* const tor = core_->find_torrent(id);
        if (tor == nullptr)
        {
            continue;
        }
        for (auto i = size_t{ 0 }, n = tr_torrentLabelCount(tor); i < n; ++i)
        {
            ++label_count[Glib::ustring{ std::string{ tr_torrentLabel(tor, i) } }];
        }
    }

    // Build the list box
    list_box_ = Gtk::make_managed<Gtk::ListBox>();
    list_box_->set_selection_mode(TR_GTK_SELECTION_MODE(NONE));
    list_box_->show();

    for (auto const& [name, count] : label_count)
    {
        bool const active = (count == n_torrents);
        bool const inconsistent = (count > 0 && count < n_torrents);
        add_label_row(name, active, inconsistent);
    }

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(TR_GTK_POLICY_TYPE(NEVER), TR_GTK_POLICY_TYPE(AUTOMATIC));
    scroll->set_min_content_height(120);
    scroll->set_max_content_height(300);
    scroll->set_propagate_natural_height(true);
    IF_GTKMM4(scroll->set_child(*list_box_), scroll->add(*list_box_));
    scroll->show();

    // "Add new label" row
    new_label_entry_ = Gtk::make_managed<Gtk::Entry>();
    new_label_entry_->set_placeholder_text(_("New label…"));
    new_label_entry_->set_hexpand(true);
    new_label_entry_->show();

    auto* add_button = Gtk::make_managed<Gtk::Button>(_("_Add"), true);
    add_button->show();

    auto* add_box = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(HORIZONTAL), 6);
    IF_GTKMM4(add_box->append(*new_label_entry_), add_box->pack_start(*new_label_entry_, true, true, 0));
    IF_GTKMM4(add_box->append(*add_button), add_box->pack_start(*add_button, false, false, 0));
    add_box->set_margin_top(6);
    add_box->show();

    add_button->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_add_button_clicked));
    new_label_entry_->signal_activate().connect(sigc::mem_fun(*this, &Impl::on_add_button_clicked));

    auto* content = dialog_.get_content_area();
    content->set_spacing(6);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(12);
    content->set_margin_bottom(6);
    IF_GTKMM4(content->append(*scroll), content->pack_start(*scroll, true, true, 0));
    IF_GTKMM4(content->append(*add_box), content->pack_start(*add_box, false, false, 0));
}

EditLabelsDialog::EditLabelsDialog(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : Gtk::Dialog()
    , impl_(std::make_unique<Impl>(*this, core, torrent_ids))
{
    set_transient_for(parent);
    set_modal(true);
    set_resizable(false);
}

EditLabelsDialog::~EditLabelsDialog() = default;

std::unique_ptr<EditLabelsDialog> EditLabelsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
{
    return std::make_unique<EditLabelsDialog>(parent, core, torrent_ids);
}
