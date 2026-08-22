/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Push-latency jitter of the real mailbox data structures under P producers.
 *
 *   bq  : BQueue        (one mutex)
 *   sbq : ShardedBQueue (N mutex lanes, round-robin)
 *   lf  : LockFreeMPSC  (Vyukov bounded ring, park-free push)
 *
 * Usage: ./jitter_contention <bq|sbq|lf> [num_producers]
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include "actors/BQueue.hpp"
#include "actors/ShardedBQueue.hpp"
#include "actors/LockFreeMPSC.hpp"

using actors::BQueue;
using actors::ShardedBQueue;
using actors::LockFreeMPSC;
using actors::Queue;
using clk = std::chrono::steady_clock;

static const uint64_t K = 1'000'000; // pushes per producer

static void producer(Queue<const void*>* q, std::vector<uint32_t>* lat) {
    lat->resize(K);
    for (uint64_t i = 0; i < K; i++) {
        auto t0 = clk::now();
        q->push(reinterpret_cast<const void*>(i + 1));
        auto t1 = clk::now();
        (*lat)[i] = (uint32_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "bq";
    int P = argc > 2 ? std::atoi(argv[2]) : 8;

    Queue<const void*>* q;
    const char* name;
    if (mode == "sbq")      { q = new ShardedBQueue<const void*>(8);        name = "ShardedBQueue"; }
    else if (mode == "lf")  { q = new LockFreeMPSC<const void*>((size_t)P * K); name = "LockFreeMPSC"; }
    else                    { q = new BQueue<const void*>((size_t)P * K);   name = "BQueue"; }

    printf("=== %-14s : %d producers x %llu pushes ===\n", name, P, (unsigned long long)K);

    std::vector<std::vector<uint32_t>> lat(P);
    std::vector<std::thread> th;
    for (int p = 0; p < P; p++) th.emplace_back(producer, q, &lat[p]);
    for (auto& t : th) t.join();

    std::vector<uint32_t> all;
    for (auto& v : lat) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());
    auto pct = [&](double p) { return all[(size_t)(p * (all.size() - 1))]; };
    double sum = 0; for (auto v : all) sum += v;
    printf("%-4s  mean %5.0f  p50 %5u  p99 %7u  p99.9 %8u  p99.99 %9u  (ns/push)\n",
           mode.c_str(), sum / all.size(), pct(0.50), pct(0.99), pct(0.999), pct(0.9999));
    return 0;
}
