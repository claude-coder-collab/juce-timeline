#include "SyntheticReader.h"

#include <jwaveform/core/jwaveform_PeakBuilder.h>
#include <jwaveform/core/jwaveform_PeakColumns.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <thread>
#include <vector>

TEST_CASE("Partial PeakData can be read while it is built", "[concurrency]")
{
    SyntheticReader reader{ 2'000'000, 2, [](int channel, std::int64_t position) { return std::sin(static_cast<float>(position + channel) * 0.0003f); } };
    const auto data = jwaveform::PeakBuilder::createDataFor(reader);
    std::atomic<bool> cancel{ false };
    std::thread builder{ [&] { jwaveform::PeakBuilder{ reader, *data, 8192 }.run(cancel); } };

    std::vector<jwaveform::ColumnPeak> columns(500);
    auto validSeen = 0;
    while (!data->isComplete())
    {
        for (const auto samplesPerColumn : { 4000.0, 300.0, 40.0 })
        {
            jwaveform::computeColumns(*data, jwaveform::allChannels, 0.0, samplesPerColumn, columns);
            for (const auto& column : columns)
            {
                if (!column.valid)
                    continue;
                ++validSeen;
                REQUIRE(column.min <= column.max);
                REQUIRE(column.min >= -1.0f);
                REQUIRE(column.max <= 1.0f);
            }
        }
    }
    builder.join();
    jwaveform::computeColumns(*data, jwaveform::allChannels, 0.0, 4000.0, columns);
    CHECK(std::ranges::all_of(columns, [](const auto& column) { return column.valid; }));
    CHECK(validSeen > 0);
}
