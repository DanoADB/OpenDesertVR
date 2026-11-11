// SPDX-License-Identifier: MIT
#include "snapshot_window.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool value) {
    if (!value)
        throw std::runtime_error("snapshot_window_control_failed");
}
int main() {
    using odv::research::SnapshotWindow;
    try {
        for (const auto invalid : {"", "\n", "0", "00\n", "-1\n", " 1\n", "1 \n", "1\r\n", "4097\n", "99999\n", "1\n2"})
            require(!SnapshotWindow::parse(invalid));
        require(SnapshotWindow::parse("0\n") == 0u);
        require(SnapshotWindow::parse("4096\n") == 4096u);
        SnapshotWindow window;
        require(!window.take());
        for (const unsigned first : {0u, 128u, 4096u}) {
            window.begin(first);
            unsigned retained = 0;
            for (unsigned i = 0; i < 10000; ++i) {
                const auto ordinal = window.take();
                const bool expected = i >= first && i < first + 128;
                require(ordinal.has_value() == expected);
                if (ordinal) {
                    require(*ordinal == i);
                    ++retained;
                }
            }
            require(retained == 128 && window.truncated() && window.seen() == first + 129);
        }
        window.begin(128);
        for (unsigned i = 0; i < 100; ++i)
            require(!window.take());
        require(window.seen() == 100 && !window.truncated()); // Empty later window stays empty.
        require(window.finish_interval()); // Skipped callbacks are activity too.
        window.begin(0);
        require(!window.finish_interval() && !window.finish_interval() && !window.finish_interval());
        require(window.finish_interval() && window.seen() == 0); // Bounded idle timeout.
        window.begin(128);
        require(!window.finish_interval() && !window.finish_interval());
        require(!window.take()); // First eligible callback, still before requested offset.
        require(window.finish_interval() && window.seen() == 1); // Do not accumulate another active interval.
        window.begin(0);
        require(!window.finish_interval());
        require(window.take() == 0u && window.finish_interval()); // Normal admission after idle Present.
        window.begin(std::nullopt);
        require(!window.valid() && !window.take() && window.seen() == 0u && window.finish_interval());
        window.begin(4097);
        require(!window.valid() && !window.take());
        std::cout << "PASS: bounded recording windows, exact ordinals, saturation, empty interval and malformed selector rejection; game=NOT_RUN\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
