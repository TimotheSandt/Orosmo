#include <orosmo/core/file_watcher.hpp>

#include <algorithm>

namespace orosmo::core
{

namespace fs = std::filesystem;

std::vector<std::pair<fs::path, FileWatcher::Stamp>> FileWatcher::scan(const Watch &w, bool &complete)
{
    complete = true;
    std::vector<std::pair<fs::path, Stamp>> out;
    std::error_code ec;
    auto accept = [&](const fs::path &p) {
        if (w.extensions.empty())
            return true;
        return std::find(w.extensions.begin(), w.extensions.end(), p.extension().string()) != w.extensions.end();
    };
    auto add = [&](const fs::path &p) {
        std::error_code e1, e2;
        const auto time = fs::last_write_time(p, e1);
        const auto size = fs::file_size(p, e2);
        if (!e1 && !e2)
            out.push_back({p, Stamp{time, size}});
    };
    if (fs::is_regular_file(w.root, ec))
        add(w.root);
    else if (fs::is_directory(w.root, ec))
    {
        // Pas de skip_permission_denied : un sous-répertoire illisible doit rendre le scan incomplet, pas faire croire que
        // ses fichiers ont disparu.
        for (fs::recursive_directory_iterator it(w.root, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code e3;
            if (it->is_regular_file(e3) && accept(it->path()))
                add(it->path());
        }
        if (ec)
            complete = false;
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    return out;
}

std::vector<std::pair<fs::path, FileWatcher::Stamp>> FileWatcher::scanTracked(const Watch &w)
{
    std::vector<std::pair<fs::path, Stamp>> out;
    out.reserve(w.known.size());
    for (const auto &entry : w.known)
    {
        std::error_code e1, e2;
        const auto time = fs::last_write_time(entry.first, e1);
        const auto size = fs::file_size(entry.first, e2);
        if (!e1 && !e2)
            out.push_back({entry.first, Stamp{time, size}});
    }
    return out; // l'ordre de `known` (trié) est conservé
}

FileWatcher::WatchId FileWatcher::watch(fs::path path, Callback callback, std::vector<std::string> extensions)
{
    Watch w{++mNextId, std::move(path), std::move(callback), std::move(extensions), {}};
    bool complete = true;
    w.known = scan(w, complete);
    const WatchId id = w.id;
    mWatches.push_back(std::move(w));
    return id;
}

void FileWatcher::unwatch(WatchId id)
{
    std::erase_if(mWatches, [id](const Watch &w) { return w.id == id; });
}

std::size_t FileWatcher::poll()
{
    if (mPollInterval.count() > 0)
    {
        const auto now = std::chrono::steady_clock::now();
        if (mLastPoll && now - *mLastPoll < mPollInterval)
            return 0;
        mLastPoll = now;
    }
    ++mStats.polls;
    std::size_t count = 0;
    // Copie des identifiants : un rappel peut appeler watch()/unwatch().
    std::vector<WatchId> ids;
    for (const auto &w : mWatches)
        ids.push_back(w.id);
    for (WatchId id : ids)
    {
        auto it = std::find_if(mWatches.begin(), mWatches.end(), [id](const Watch &w) { return w.id == id; });
        if (it == mWatches.end())
            continue;
        bool complete = true;
        // Parcours complet : fichier racine (un seul stat), état connu vide (fichier ou dossier qui n'existait pas), ou un poll() sur
        // mFullScanEvery ; sinon seuls les fichiers déjà suivis sont vérifiés.
        std::error_code rootEc;
        const bool full = mFullScanEvery <= 1 || it->known.empty() || ++it->sinceFullScan >= mFullScanEvery || fs::is_regular_file(it->root, rootEc);
        std::vector<std::pair<fs::path, Stamp>> now;
        if (full)
        {
            it->sinceFullScan = 0;
            ++mStats.fullScans;
            now = scan(*it, complete);
        }
        else
        {
            ++mStats.trackedScans;
            now = scanTracked(*it);
        }
        if (!complete)
            continue; // scan interrompu : on garde l'état connu et on réessaie au prochain poll()
        std::vector<FileEvent> events;
        auto &old = it->known;
        std::size_t i = 0, j = 0;
        while (i < old.size() || j < now.size())
        {
            if (j == now.size() || (i < old.size() && old[i].first < now[j].first))
                events.push_back({old[i++].first, FileChange::kRemoved});
            else if (i == old.size() || now[j].first < old[i].first)
                events.push_back({now[j++].first, FileChange::kCreated});
            else
            {
                if (!(old[i].second == now[j].second))
                    events.push_back({now[j].first, FileChange::kModified});
                ++i;
                ++j;
            }
        }
        it->known = std::move(now);
        Callback cb = it->callback; // copie : le rappel peut désinscrire cette observation
        for (const FileEvent &e : events)
            cb(e);
        count += events.size();
    }
    return count;
}

} // namespace orosmo::core
