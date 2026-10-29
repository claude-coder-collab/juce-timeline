#include "jtimeline_TimelineModel.h"

#include <algorithm>

namespace jtimeline
{

std::vector<SampleRange> mergeSpans(std::vector<SampleRange> spans, SamplePosition minGap)
{
    std::erase_if(spans, [](const SampleRange& span) { return span.isEmpty(); });
    std::ranges::sort(spans, {}, &SampleRange::start);
    std::vector<SampleRange> merged;
    for (const auto& span : spans)
    {
        const auto gap = merged.empty() ? minGap : span.start - merged.back().end;
        if (!merged.empty() && (gap <= 0 || gap < minGap))
            merged.back().end = std::max(merged.back().end, span.end);
        else
            merged.push_back(span);
    }
    return merged;
}

}
