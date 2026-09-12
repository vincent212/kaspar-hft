#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <utility>
#include <arpa/inet.h>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/AddActor.hpp"
#include "actors/msg/ActorRemoved.hpp"
#include "actors/act/Group.hpp"
#include "mcast_recv/if/PCAPReader.hpp"

namespace fs = std::filesystem;

// Two Databento on-disk layouts. The naming choice in the directory drives
// both the file-globbing strategy and whether per-packet filtering is needed
// inside the reader actors.
enum class PcapFormat {
    // Old: one file per (10-min window × multicast group).
    //   dc3-glbx-a-<date>T<HHMMSS>_<mcast_ip>_<dst_port>.pcap.zst
    // Channel filter is done at file-selection time — the reader trusts the
    // filename. A separate "-snap-" file is prepended for instrument defs.
    PER_CHANNEL,
    // New (as of late-2024/early-2025 archive backfills): one file per 10-min
    // window with EVERY multicast group muxed together.
    //   dc3-glbx-a-<date>T<HHMMSS>.pcap.zst
    // No port in filename; no separate snap file (IR groups live in the same
    // bundle). Channel filter has to be applied per-packet via PCAPReader's
    // filter list, otherwise cross-channel packets would scramble the
    // MDP3 decoder's per-channel seqnum tracking.
    CONSOLIDATED,
};

class PcapFileManager : public actors::Actor
{
private:
    actors::Group* group_;
    actors::Actor* msg_buf_;
    actors::Actor* manager_;
    // Set by any scan_fail() / unexpected-ActorRemoved path. main() reads
    // this after the Manager thread joins and returns a non-zero exit code
    // so batch scripts don't mistake an aborted run for success.
    bool failed_ = false;
    std::string pcap_dir_;
    PcapFormat format_;
    // PER_CHANNEL bookkeeping.
    // Incremental: port alone resolves the channel (Databento filenames embed
    // it as the "_<port>.pcap.zst" suffix; "-snap-" is absent for incremental
    // captures). Snap: within a single channel, the same port is shared by the
    // MBP-snapshot IP and the IR (incremental-recovery) IP — only IR carries
    // instrument definitions, so we must filter by IP+port to pick the right
    // stream.
    // Sentinel values keep these non-empty when the format is CONSOLIDATED
    // so that an accidental call into scan_directory_per_channel — where
    // filename.find("") is always 0 (true) and would turn into a catch-all —
    // matches nothing instead.
    static constexpr const char* kPerChannelNeverMatch_ = "\xFF__never_match__\xFF";
    std::string incr_port_suffix_;  // e.g. "_14310.pcap.zst"
    std::string snap_filter_;       // e.g. "224.0.31.43_14310" (IR ip+port)
    // CONSOLIDATED bookkeeping.
    std::vector<std::pair<uint32_t, uint16_t>> consolidated_filter_;
    char consolidated_color_ = 'a';  // 'a' or 'b' — baked into the file regex
    bool allow_partial_day_ = false; // suppress IR-warmup warning
    std::vector<std::string> pcap_files_;
    size_t current_file_index_;
    std::string chanstr_;
    char name_[256];

public:
    // PER_CHANNEL ctor — original behaviour, unchanged contract.
    PcapFileManager(
        actors::Group* grp,
        actors::Actor* msg_buf,
        const std::string& pcap_dir,
        const std::string& chan,
        int incr_port,
        const std::string& snap_ip,
        int snap_port,
        actors::Actor* manager)
        : group_(grp),
          msg_buf_(msg_buf),
          manager_(manager),
          pcap_dir_(pcap_dir),
          format_(PcapFormat::PER_CHANNEL),
          incr_port_suffix_("_" + std::to_string(incr_port) + ".pcap.zst"),
          snap_filter_(snap_ip + "_" + std::to_string(snap_port)),
          current_file_index_(0),
          chanstr_(chan)
    {
        snprintf(name_, sizeof(name_), "PcapFileManager_%s", chan.c_str());

        MESSAGE_HANDLER(actors::msg::Start, start_handler);
        MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
        MESSAGE_HANDLER(actors::msg::ActorRemoved, actor_removed_handler);
    }

    // CONSOLIDATED ctor — new format. Caller supplies the (dst_ip, dst_port)
    // allowlist used by every per-file PCAPReader, the feed color (baked
    // into the filename regex so a directory holding both feeds doesn't
    // get all of them ingested for one --color), and an opt-out for the
    // partial-day-IR warning.
    PcapFileManager(
        actors::Group* grp,
        actors::Actor* msg_buf,
        const std::string& pcap_dir,
        const std::string& chan,
        const std::vector<std::pair<uint32_t, uint16_t>>& filter,
        char color,
        bool allow_partial_day,
        actors::Actor* manager)
        : group_(grp),
          msg_buf_(msg_buf),
          manager_(manager),
          pcap_dir_(pcap_dir),
          format_(PcapFormat::CONSOLIDATED),
          // Sentinels so a buggy fallthrough to scan_directory_per_channel
          // would still find no matches (find("") would otherwise be 0/true).
          incr_port_suffix_(kPerChannelNeverMatch_),
          snap_filter_(kPerChannelNeverMatch_),
          consolidated_filter_(filter),
          consolidated_color_(color),
          allow_partial_day_(allow_partial_day),
          current_file_index_(0),
          chanstr_(chan)
    {
        snprintf(name_, sizeof(name_), "PcapFileManager_%s", chan.c_str());

        MESSAGE_HANDLER(actors::msg::Start, start_handler);
        MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
        MESSAGE_HANDLER(actors::msg::ActorRemoved, actor_removed_handler);
    }

    const char* get_name() const override { return name_; }

    // Exposed so main() can turn an error-induced shutdown into rc != 0.
    bool failed() const { return failed_; }

private:
    // Fail via the manager-driven shutdown instead of std::exit, so the
    // group/logger/recorder still get a coordinated stop. Returns false if
    // the caller should abort.
    bool scan_fail(const std::string& msg)
    {
        std::cerr << "ERROR: " << msg << std::endl;
        failed_ = true;
        manager_->send(new actors::msg::Shutdown(), this);
        return false;
    }

    bool scan_directory()
    {
        if (!fs::exists(pcap_dir_)) {
            return scan_fail("Directory does not exist: " + pcap_dir_);
        }
        if (!fs::is_directory(pcap_dir_)) {
            return scan_fail("Path is not a directory: " + pcap_dir_);
        }

        if (format_ == PcapFormat::CONSOLIDATED) return scan_directory_consolidated();
        return scan_directory_per_channel();
    }

    bool scan_directory_per_channel()
    {
        // Format invariant — callers reach this only via scan_directory()
        // which dispatches on format_. The check is belt-and-suspenders:
        // if a future refactor calls this directly for a CONSOLIDATED
        // instance, the sentinel suffixes ensure no spurious matches, but
        // we'd rather fail loudly than write a definitions-only .bin.
        if (format_ != PcapFormat::PER_CHANNEL) {
            return scan_fail("scan_directory_per_channel() called on non-PER_CHANNEL "
                             "PcapFileManager (format invariant violation)");
        }

        std::vector<std::string> snap_files;
        for (const auto& entry : fs::directory_iterator(pcap_dir_)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (!filename.ends_with(".pcap.zst")) continue;

            bool is_snap = filename.find("-snap-") != std::string::npos;

            if (!is_snap && filename.find(incr_port_suffix_) != std::string::npos) {
                pcap_files_.push_back(entry.path().string());
            } else if (is_snap && filename.find(snap_filter_) != std::string::npos) {
                snap_files.push_back(entry.path().string());
            }
        }

        std::sort(pcap_files_.begin(), pcap_files_.end());
        std::sort(snap_files.begin(), snap_files.end());

        // Fail fast if the incremental filter matched nothing. A snap-only
        // match would otherwise write a definitions-only .bin that looks
        // legitimate to downstream stats / batch-script success checks.
        if (pcap_files_.empty()) {
            return scan_fail("No incremental files match *" + incr_port_suffix_
                           + " in " + pcap_dir_);
        }

        // Prepend just the first snap file (earliest timestamp) for instrument definitions.
        if (!snap_files.empty()) {
            pcap_files_.insert(pcap_files_.begin(), snap_files.front());
        }

        std::cout << "Format:             PER_CHANNEL" << std::endl;
        std::cout << "Incremental filter: *" << incr_port_suffix_ << " (non-snap)" << std::endl;
        std::cout << "Snap filter:        *" << snap_filter_ << " (snap; " << snap_files.size() << " files, using first)" << std::endl;
        std::cout << "Found " << pcap_files_.size() << " files to process in " << pcap_dir_ << std::endl;
        std::cout << "  First: " << fs::path(pcap_files_.front()).filename().string() << std::endl;
        std::cout << "  Last:  " << fs::path(pcap_files_.back()).filename().string() << std::endl;
        return true;
    }

    bool scan_directory_consolidated()
    {
        // Fail loudly if the caller forgot to populate the filter.
        // PCAPReader treats an empty filter as "accept everything", which
        // would let every CME channel's packets into the MDP3 decoder
        // and instantly scramble per-channel seqnum tracking.
        if (consolidated_filter_.empty()) {
            return scan_fail("CONSOLIDATED mode requires a non-empty per-packet "
                             "filter, but consolidated_filter_ is empty");
        }

        // Match consolidated naming for THIS color only. Accepts the plain
        // single-letter form (dc3-glbx-a-...) and the deduplicated form
        // (dc3-glbx-ab-dedup-...) which carries both feeds collapsed and
        // is therefore valid regardless of --color. Anchored so we won't
        // pick up per-channel files that might be co-located.
        //
        // Examples (color='a'):
        //   dc3-glbx-a-20241230T000000.pcap.zst        — accepted
        //   dc3-glbx-ab-dedup-20241230T000000.pcap.zst — accepted
        //   dc3-glbx-b-20241230T000000.pcap.zst        — rejected
        //   dc3-glbx-a-20241230T000000_224.0.31.1_14310.pcap.zst — rejected
        const std::string pattern =
            R"(^dc3-glbx-()" + std::string(1, consolidated_color_) +
            R"(|ab-dedup)-\d{8}T\d{6}\.pcap\.zst$)";
        const std::regex name_re(pattern);

        for (const auto& entry : fs::directory_iterator(pcap_dir_)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (!std::regex_match(filename, name_re)) continue;
            pcap_files_.push_back(entry.path().string());
        }
        std::sort(pcap_files_.begin(), pcap_files_.end());

        if (pcap_files_.empty()) {
            return scan_fail("No consolidated files match dc3-glbx-("
                           + std::string(1, consolidated_color_)
                           + "|ab-dedup)-<date>T<time>.pcap.zst in "
                           + pcap_dir_);
        }

        // Partial-day warning. Unlike per-channel mode (which prepends a
        // dedicated -snap- file so the IR / instrument-defs stream arrives
        // before any incremental update), CONSOLIDATED files mux IR inline
        // on a multi-second rebroadcast cadence. If the directory starts
        // mid-day, the first incremental packets may reference SecurityIDs
        // whose definitions haven't been seen yet. Full-day runs
        // (first file ends in T000000.pcap.zst) are safe; partial-day
        // runs print a loud warning unless explicitly opted-in.
        const std::string first = fs::path(pcap_files_.front()).filename().string();
        const bool starts_at_day = first.find("T000000.pcap.zst") != std::string::npos;
        if (!starts_at_day && !allow_partial_day_) {
            std::cerr << "\n*** WARNING: CONSOLIDATED directory does not start at "
                         "T000000 of the day. First file: " << first << "\n"
                         "    Instrument definitions (IR) are interleaved in each "
                         "consolidated file but only rebroadcast every few seconds; "
                         "incremental updates in the first ~30s may reference "
                         "SecurityIDs whose definitions haven't been seen yet, "
                         "producing dropped / mis-attributed rows in the output .bin. "
                         "Pass --allow-partial-day to silence this warning if you "
                         "understand the risk.\n\n";
        }

        std::cout << "Format:             CONSOLIDATED" << std::endl;
        std::cout << "Feed color:         " << consolidated_color_ << std::endl;
        std::cout << "Per-packet filter:  " << consolidated_filter_.size() << " (dst_ip, dst_port) entries" << std::endl;
        for (const auto& [ip_n, port] : consolidated_filter_) {
            if (ip_n == 0) {
                std::cout << "    any -> :" << port << std::endl;
            } else {
                in_addr a; a.s_addr = ip_n;
                char buf[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &a, buf, sizeof(buf));
                std::cout << "    " << buf << " -> :" << port << std::endl;
            }
        }
        std::cout << "Found " << pcap_files_.size() << " files to process in " << pcap_dir_ << std::endl;
        std::cout << "  First: " << first << std::endl;
        std::cout << "  Last:  " << fs::path(pcap_files_.back()).filename().string() << std::endl;
        return true;
    }

    void start_handler(const actors::msg::Start*) noexcept
    {
        std::cout << "\n=== PcapFileManager starting ===" << std::endl;
        if (!scan_directory()) return;  // Shutdown already signaled
        std::cout << "Total files to process: " << pcap_files_.size() << std::endl;
        send(new actors::msg::Continue(), this);
    }

    void continue_handler(const actors::msg::Continue*) noexcept
    {
        if (current_file_index_ >= pcap_files_.size()) {
            std::cout << "\n=== All files processed successfully! ===" << std::endl;
            manager_->send(new actors::msg::Shutdown(), this);
            return;
        }

        std::cout << "\n[" << (current_file_index_ + 1) << "/"
                  << pcap_files_.size() << "] Processing: "
                  << fs::path(pcap_files_[current_file_index_]).filename()
                  << std::endl;

        actors::Actor* pcap_reader = nullptr;
        if (format_ == PcapFormat::CONSOLIDATED) {
            pcap_reader = create_PCAPReader_32_0_filtered(
                chanstr_,
                msg_buf_,
                pcap_files_[current_file_index_],
                consolidated_filter_,
                false);
        } else {
            pcap_reader = create_PCAPReader_32_0(
                chanstr_,
                msg_buf_,
                pcap_files_[current_file_index_],
                false);
        }

        group_->send(new actors::msg::AddActor(pcap_reader), this);
    }

    void actor_removed_handler(const actors::msg::ActorRemoved* m) noexcept
    {
        // Only advance on per-file PCAPReader removals. Group member dropouts
        // (msg_buf/message_processor/binrec) must not silently skip a file
        // and must not be reported as success. We anchor with ends_with()
        // because PCAPReader names are constructed as "<chan>PCAPReader"
        // (see mcast_recv/include/mcast_recv/act/PCAPReader.hpp::snprintf at
        // ctor end). A substring match would also catch any future helper
        // like "DebugPCAPReaderTap" and spuriously advance current_file_index_.
        const std::string suffix = "PCAPReader";
        const bool is_pcap_reader =
            m->actor_name.size() >= suffix.size() &&
            m->actor_name.compare(m->actor_name.size() - suffix.size(),
                                  suffix.size(), suffix) == 0;
        if (!is_pcap_reader) {
            std::cerr << "ERROR: unexpected actor removed: " << m->actor_name
                      << " — aborting run" << std::endl;
            failed_ = true;
            manager_->send(new actors::msg::Shutdown(), this);
            return;
        }
        std::cout << "  Completed file " << (current_file_index_ + 1)
                  << "/" << pcap_files_.size()
                  << " (actor: " << m->actor_name << ")" << std::endl;
        current_file_index_++;
        send(new actors::msg::Continue(), this);
    }
};
