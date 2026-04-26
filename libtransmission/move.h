// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "libtransmission/crypto-utils.h"

class tr_move_worker
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_sha1_digest_t const& info_hash() const = 0;

        virtual void on_move_queued() = 0;
        virtual void on_move_started() = 0;

        // Perform the actual file moves. Called from the worker thread.
        // Should update move_progress as files are moved.
        // Returns false on error or if abort_flag is set.
        virtual bool do_move(std::atomic<bool> const& abort_flag) = 0;

        virtual void on_move_done(bool aborted) = 0;
    };

    tr_move_worker() = default;
    ~tr_move_worker();

    tr_move_worker(tr_move_worker const&) = delete;
    tr_move_worker(tr_move_worker&&) = delete;
    tr_move_worker& operator=(tr_move_worker const&) = delete;
    tr_move_worker& operator=(tr_move_worker&&) = delete;

    void add(std::unique_ptr<Mediator> mediator);
    void remove(tr_sha1_digest_t const& info_hash);

private:
    struct Node
    {
        explicit Node(std::unique_ptr<Mediator> mediator_in) noexcept
            : mediator_{ std::move(mediator_in) }
        {
        }

        [[nodiscard]] bool matches(tr_sha1_digest_t const& h) const noexcept
        {
            return mediator_->info_hash() == h;
        }

        std::unique_ptr<Mediator> mediator_;
    };

    void move_thread_func();

    std::mutex move_mutex_;
    std::vector<Node> todo_;
    std::optional<Node> current_node_;
    std::optional<std::thread::id> move_thread_id_;
    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
