#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

class RequestMetrics {
    static constexpr std::size_t MAX_LATENCY_SAMPLES = 4096;
    std::uint64_t requests_total_ = 0;
    std::uint64_t responses_2xx_ = 0;
    std::uint64_t responses_4xx_ = 0;
    std::uint64_t responses_5xx_ = 0;
    std::vector<double> latencies_ms_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();

    static double percentile(std::vector<double> values, double p) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const auto rank = static_cast<std::size_t>(
            std::ceil((p / 100.0) * static_cast<double>(values.size())));
        const auto index = rank == 0 ? 0 : std::min(rank - 1, values.size() - 1);
        return values[index];
    }

    double uptime_seconds() const {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_).count();
    }

public:
    void observe(int status, double latency_ms) {
        ++requests_total_;
        if (status >= 200 && status < 300) ++responses_2xx_;
        else if (status >= 400 && status < 500) ++responses_4xx_;
        else if (status >= 500 && status < 600) ++responses_5xx_;

        if (latencies_ms_.size() >= MAX_LATENCY_SAMPLES)
            latencies_ms_.erase(latencies_ms_.begin());
        latencies_ms_.push_back(latency_ms);
    }

    std::string json_fields() const {
        double avg = 0.0;
        if (!latencies_ms_.empty()) {
            avg = std::accumulate(latencies_ms_.begin(), latencies_ms_.end(), 0.0) /
                  static_cast<double>(latencies_ms_.size());
        }

        const double uptime = uptime_seconds();
        const double process_rps =
            uptime > 0.0 ? static_cast<double>(requests_total_) / uptime : 0.0;

        std::ostringstream out;
        out << std::fixed << std::setprecision(3)
            << ",\"requests_total\":" << requests_total_
            << ",\"responses_2xx\":" << responses_2xx_
            << ",\"responses_4xx\":" << responses_4xx_
            << ",\"responses_5xx\":" << responses_5xx_
            << ",\"uptime_seconds\":" << uptime
            << ",\"process_avg_rps\":" << process_rps
            << ",\"latency_samples\":" << latencies_ms_.size()
            << ",\"latency_ms\":{"
            << "\"avg\":" << avg
            << ",\"p50\":" << percentile(latencies_ms_, 50.0)
            << ",\"p95\":" << percentile(latencies_ms_, 95.0)
            << ",\"p99\":" << percentile(latencies_ms_, 99.0)
            << "}";
        return out.str();
    }
};
