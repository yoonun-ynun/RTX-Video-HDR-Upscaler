#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <stdexcept>

// Count frames delivered to the encoder, against monotonic wall-clock seconds.
// Keep one sample at/before the cutoff plus samples inside the rolling window.
class ProgressRate {
    struct Sample { double seconds; uint64_t frames; };
    std::deque<Sample> samples_{{0,0}};
public:
    struct Rates { double recent, average; };
    Rates Observe(double seconds,uint64_t frames) {
        if(!std::isfinite(seconds) || seconds<samples_.back().seconds || frames<samples_.back().frames)
            throw std::invalid_argument("Progress counters must be monotonic");
        samples_.push_back({seconds,frames});
        const double cutoff=seconds-5.0;
        while(samples_.size()>1 && samples_[1].seconds<=cutoff) samples_.pop_front();
        const double window=std::min(seconds,5.0);
        return {window>0 ? (frames-samples_.front().frames)/window : 0,
                seconds>0 ? frames/seconds : 0};
    }
};
