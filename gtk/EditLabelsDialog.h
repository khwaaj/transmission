// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <glibmm/refptr.h>
#include <gtkmm/dialog.h>
#include <gtkmm/window.h>

#include <memory>
#include <vector>

class Session;

class EditLabelsDialog : public Gtk::Dialog
{
public:
    EditLabelsDialog(
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    EditLabelsDialog(EditLabelsDialog&&) = delete;
    EditLabelsDialog(EditLabelsDialog const&) = delete;
    EditLabelsDialog& operator=(EditLabelsDialog&&) = delete;
    EditLabelsDialog& operator=(EditLabelsDialog const&) = delete;
    ~EditLabelsDialog() override;

    static std::unique_ptr<EditLabelsDialog> create(
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
