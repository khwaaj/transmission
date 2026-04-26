// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "libtransmission/move.h"

#include <chrono>
#include <utility>

using namespace std::chrono_literals;

void tr_move_worker::move_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::scoped_lock{ move_mutex_ };

            if (stop_current_)
            {
                stop_current_ = false;
                stop_current_cv_.notify_one();
            }

            if (std::empty(todo_))
            {
                current_node_.reset();
                move_thread_id_.reset();
                return;
            }

            current_node_ = std::move(todo_.front());
            todo_.erase(std::begin(todo_));
        }

        current_node_->mediator_->on_move_started();
        bool const ok = current_node_->mediator_->do_move(stop_current_);
        current_node_->mediator_->on_move_done(!ok || stop_current_.load());
    }
}

void tr_move_worker::add(std::unique_ptr<Mediator> mediator)
{
    auto const lock = std::scoped_lock{ move_mutex_ };

    mediator->on_move_queued();
    todo_.emplace_back(std::move(mediator));

    if (!move_thread_id_)
    {
        auto thread = std::thread(&tr_move_worker::move_thread_func, this);
        move_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_move_worker::remove(tr_sha1_digest_t const& info_hash)
{
    auto lock = std::unique_lock(move_mutex_);

    if (current_node_ && current_node_->matches(info_hash))
    {
        stop_current_ = true;
        stop_current_cv_.wait(lock, [this]() { return !stop_current_; });
    }
    else if (auto const iter = std::ranges::find_if(
                 todo_,
                 [&info_hash](auto const& node) { return node.matches(info_hash); });
             iter != std::ranges::end(todo_))
    {
        iter->mediator_->on_move_done(true /*aborted*/);
        todo_.erase(iter);
    }
}

tr_move_worker::~tr_move_worker()
{
    {
        auto const lock = std::scoped_lock{ move_mutex_ };
        stop_current_ = true;
        todo_.clear();
    }

    while (move_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}
