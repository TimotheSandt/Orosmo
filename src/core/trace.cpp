#include <engine/core/trace.hpp>

#include <algorithm>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

namespace engine::core
{

TraceRecorder::TraceRecorder(const IClock &clock, std::size_t maxSamples) : mClock(&clock), mMax(maxSamples) {}

std::uint64_t TraceRecorder::threadNumber()
{
    // Appelé avec mMutex tenu.
    const auto id = std::this_thread::get_id();
    for (std::size_t i = 0; i < mThreads.size(); ++i)
        if (mThreads[i] == id)
            return i + 1;
    mThreads.push_back(id);
    return mThreads.size();
}

void TraceRecorder::addSample(std::string_view name, Nanoseconds start, Nanoseconds duration, TraceTrack track)
{
    if (!mEnabled.load())
        return;
    std::lock_guard lock(mMutex);
    if (mSamples.size() >= mMax)
    {
        ++mDropped;
        return;
    }
    mSamples.push_back({std::string(name), start, duration, track == TraceTrack::kGpu ? 0 : threadNumber(), track});
}

std::vector<TraceSample> TraceRecorder::samples() const
{
    std::lock_guard lock(mMutex);
    return mSamples;
}

std::uint64_t TraceRecorder::dropped() const
{
    std::lock_guard lock(mMutex);
    return mDropped;
}

void TraceRecorder::clear()
{
    std::lock_guard lock(mMutex);
    mSamples.clear();
    mDropped = 0;
}

std::vector<TraceStats> TraceRecorder::stats() const
{
    std::map<std::string, TraceStats> byName;
    {
        std::lock_guard lock(mMutex);
        for (const auto &s : mSamples)
        {
            TraceStats &st = byName[s.name];
            if (st.count == 0)
            {
                st.name = s.name;
                st.min = st.max = s.duration;
            }
            ++st.count;
            st.total += s.duration;
            st.min = std::min(st.min, s.duration);
            st.max = std::max(st.max, s.duration);
        }
    }
    std::vector<TraceStats> out;
    out.reserve(byName.size());
    for (auto &[k, v] : byName)
        out.push_back(std::move(v));
    std::sort(out.begin(), out.end(), [](const TraceStats &a, const TraceStats &b) { return a.total > b.total; });
    return out;
}

std::string TraceRecorder::chromeTraceJson() const
{
    nlohmann::json events = nlohmann::json::array();
    for (const auto &s : samples())
    {
        const bool gpu = s.track == TraceTrack::kGpu;
        events.push_back({{"name", s.name},
                          {"cat", gpu ? "gpu" : "cpu"},
                          {"ph", "X"},
                          {"ts", std::chrono::duration<double, std::micro>(s.start).count()},
                          {"dur", std::chrono::duration<double, std::micro>(s.duration).count()},
                          {"pid", gpu ? 2 : 1},
                          {"tid", s.threadId}});
    }
    return nlohmann::json{{"traceEvents", std::move(events)}, {"displayTimeUnit", "ms"}}.dump();
}

Result<> TraceRecorder::writeChromeTrace(const std::filesystem::path &path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return makeError(ErrorCode::kIo, "cannot write trace: " + path.string());
    out << chromeTraceJson();
    out.close(); // une erreur de vidage n'apparaît qu'à la fermeture
    return out ? Result<>{} : makeError(ErrorCode::kIo, "trace write failed: " + path.string());
}

} // namespace engine::core
