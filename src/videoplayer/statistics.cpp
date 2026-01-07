#include "VideoPlayer.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace stats
{
    namespace detail
    {
        template <class T>
        static double median_sorted_slice(const std::vector<T>& v, std::size_t lo, std::size_t hi)
        {
            const std::size_t len = hi - lo;
            if (len == 0) return 0.0;

            const std::size_t mid = lo + len / 2;
            if (len % 2 == 1) {
                return static_cast<double>(v[mid]);
            } else {
                const T a = v[mid - 1];
                const T b = v[mid];
                return (static_cast<double>(a) + static_cast<double>(b)) / 2.0;
            }
        }

        static std::string fmt_quart(double x)
        {
            // With integral input and Tukey hinges, quartiles/median are integral or end in .5.
            const double r = std::round(x);
            if (std::abs(x - r) < 1e-9) {
                std::ostringstream t;
                t << static_cast<std::int64_t>(r);
                return t.str();
            }
            std::ostringstream t;
            t.setf(std::ios::fixed);
            t.precision(1);
            t << x;
            return t.str();
        }
    } // namespace detail

    // Very short summary: "min/Q1/med/Q3/max μ=mean"
    // Quartiles use Tukey hinges (median of halves).
    template <class T>
    std::string short_stats(const std::vector<T>& data)
    {
        if (data.empty()) return "n=0";

        std::vector<T> v = data;
        std::sort(v.begin(), v.end());

        const std::size_t n = v.size();
        const T minv = v.front();
        const T maxv = v.back();

        const double med = detail::median_sorted_slice(v, 0, n);

        // Split into lower/upper halves (exclude median element when n is odd).
        const std::size_t half = n / 2;
        const double q1 = detail::median_sorted_slice(v, 0, half);
        const double q3 = detail::median_sorted_slice(v, (n % 2 == 0) ? half : (half + 1), n);

        // Mean in double; start at 0.0 to avoid integer overflow during accumulation.
        const double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(n);

        std::ostringstream oss;
        oss << minv << '/'
            << detail::fmt_quart(q1) << '/'
            << detail::fmt_quart(med) << '/'
            << detail::fmt_quart(q3) << '/'
            << maxv
            << " u="; // 

        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << mean;
        oss << " n=" << data.size();

        return oss.str();
    }

} // namespace stats

std::string VideoPlayer::short_stats(const std::vector<std::uint32_t>& data) const {
    return stats::short_stats<std::uint32_t>(data);
}
std::string VideoPlayer::short_stats(const std::vector<std::int32_t>& data) const {
    return stats::short_stats<std::int32_t>(data);
}