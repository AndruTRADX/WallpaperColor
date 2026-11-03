#include "daemon/infrastructure/services/file_watch.hpp"

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include "utils/error.hpp"
#include "utils/inotify.hpp"
#include "utils/log.hpp"

namespace wpc {

namespace {

/// The inotify events for FileChanges.
uint32_t inotify_mask(uint32_t what) {
    uint32_t mask = 0;
    if (what & FILE_OPENED) mask |= IN_OPEN;
    if (what & FILE_WRITTEN) mask |= IN_CLOSE_WRITE | IN_MOVED_TO;
    if (what & FILE_CREATED) mask |= IN_CREATE;
    return mask;
}

}  // namespace

struct InotifyFileWatch::State {
    struct Watch {
        int id;
        int folder;  // inotify's watch id
        std::filesystem::path path;
        uint32_t mask;
        std::function<void(const FileChange&)> changed;
    };

    Inotify inotify;
    std::mutex mutex;
    std::vector<Watch> watches;
    int next_id = 1;
    bool stopped = false;

    uint32_t mask_of(int folder) const {
        uint32_t mask = 0;
        for (const Watch& w : watches) {
            if (w.folder == folder) mask |= w.mask;
        }
        return mask;
    }
};

InotifyFileWatch::InotifyFileWatch() : state_(std::make_shared<State>()) {
    std::thread([state = state_] {
        while (true) {
            std::vector<FileEvent> events;
            try {
                events = state->inotify.read_events();
            } catch (const Error& e) {
                log("file watch: {}", e.what());
                return;
            }
            for (const FileEvent& e : events) {
                std::vector<std::pair<std::function<void(const FileChange&)>, std::filesystem::path>> callbacks;
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopped) return;
                    for (const State::Watch& w : state->watches) {
                        if (w.folder == e.watch && (w.mask & e.mask)) callbacks.emplace_back(w.changed, w.path);
                    }
                }
                for (const auto& [callback, path] : callbacks) {
                    callback(FileChange{path, e.name, (e.mask & IN_OPEN) != 0, (e.mask & IN_CREATE) != 0,
                                        (e.mask & IN_ISDIR) != 0});
                }
            }
        }
    }).detach();
}

InotifyFileWatch::~InotifyFileWatch() {
    std::lock_guard lock(state_->mutex);
    state_->stopped = true;  // the thread stops at its next event
}

int InotifyFileWatch::watch(const std::filesystem::path& folder, uint32_t what,
                            std::function<void(const FileChange&)> changed) {
    std::lock_guard lock(state_->mutex);
    uint32_t mask = inotify_mask(what);
    // Watching a folder again gives the same inotify id; IN_MASK_ADD widens what it reports
    // instead of replacing it.
    int id_in_kernel = state_->inotify.add(folder, mask | IN_MASK_ADD);
    int id = state_->next_id++;
    state_->watches.push_back(State::Watch{id, id_in_kernel, folder, mask, std::move(changed)});
    return id;
}

int InotifyFileWatch::watch_tree(const std::filesystem::path& folder, uint32_t what,
                                 std::function<void(const FileChange&)> changed) {
    // `folder` and every folder below it (not following links).
    std::vector<std::filesystem::path> folders = {folder};
    for (size_t i = 0; i < folders.size(); ++i) {
        std::error_code ec;
        std::filesystem::directory_iterator it(folders[i], ec);
        if (ec) continue;
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code e;
            if (entry.symlink_status(e).type() == std::filesystem::file_type::directory) folders.push_back(entry.path());
        }
    }
    std::lock_guard lock(state_->mutex);
    uint32_t mask = inotify_mask(what);
    int id = state_->next_id++;
    for (const std::filesystem::path& f : folders) {
        int id_in_kernel = state_->inotify.add(f, mask | IN_MASK_ADD);
        state_->watches.push_back(State::Watch{id, id_in_kernel, f, mask, changed});
    }
    return id;
}

void InotifyFileWatch::unwatch(int id) {
    std::lock_guard lock(state_->mutex);
    auto& watches = state_->watches;
    std::vector<int> folders;
    for (const State::Watch& w : watches) {
        if (w.id == id) folders.push_back(w.folder);
    }
    std::erase_if(watches, [&](const State::Watch& w) { return w.id == id; });
    // The last watch of a folder stops the folder's; otherwise the folder keeps its wider mask
    // and the extra events are simply not delivered.
    for (int folder : folders) {
        if (state_->mask_of(folder) == 0) state_->inotify.remove(folder);
    }
}

}  // namespace wpc
