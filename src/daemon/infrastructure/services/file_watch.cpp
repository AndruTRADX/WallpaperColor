#include "daemon/infrastructure/services/file_watch.hpp"

#include <algorithm>
#include <chrono>
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
    if (what & FILE_CREATED) mask |= IN_CREATE | IN_MOVED_TO;
    if (what & FILE_REMOVED) mask |= IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF;
    return mask;
}

/// The change an inotify event in `folder` is.
FileChange change_of(const FileEvent& e, const std::filesystem::path& folder) {
    bool self = (e.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0;
    return FileChange{.folder = folder,
                      .name = self ? "" : e.name,
                      .opened = (e.mask & IN_OPEN) != 0,
                      .created = (e.mask & (IN_CREATE | IN_MOVED_TO)) != 0,
                      .removed = (e.mask & (IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF)) != 0,
                      .is_folder = self || (e.mask & IN_ISDIR) != 0,
                      .overflow = false};
}

using Delivery = std::pair<std::function<void(const FileChange&)>, FileChange>;

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

    /// Who hears `e`, with what. Also forgets the watches the kernel has ended.
    void deliveries(const FileEvent& e, std::vector<Delivery>& out) {
        if (e.mask & IN_Q_OVERFLOW) {
            // Lost events, of any folder: every watch hears that anything may have changed.
            for (const Watch& w : watches) {
                out.emplace_back(w.changed, FileChange{.folder = w.path, .name = "", .opened = false, .created = false,
                                                                   .removed = false, .is_folder = true, .overflow = true});
            }
            return;
        }
        for (const Watch& w : watches) {
            if (w.folder == e.watch && (w.mask & e.mask)) out.emplace_back(w.changed, change_of(e, w.path));
        }
        // The folder is gone (or its watch was removed): the kernel may give its number to
        // another folder, so it must not stay here.
        if (e.mask & IN_IGNORED) std::erase_if(watches, [&](const Watch& w) { return w.folder == e.watch; });
    }

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
        bool failing = false;
        while (true) {
            std::vector<FileEvent> events;
            try {
                events = state->inotify.read_events();
                failing = false;
            } catch (const Error& e) {
                // Not expected; going on beats going deaf for good. Said once, tried every second.
                if (!failing) log("file watch: {} (trying again every second)", e.what());
                failing = true;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            for (const FileEvent& e : events) {
                std::vector<Delivery> deliveries;
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopped) return;
                    state->deliveries(e, deliveries);
                }
                for (const auto& [callback, change] : deliveries) callback(change);
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
