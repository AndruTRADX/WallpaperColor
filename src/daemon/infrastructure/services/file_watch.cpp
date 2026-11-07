#include "daemon/infrastructure/services/file_watch.hpp"

#include <algorithm>
#include <vector>

#include "utils/error.hpp"
#include "utils/event_loop.hpp"
#include "utils/inotify.hpp"

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
    std::vector<Watch> watches;
    int next_id = 1;

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

InotifyFileWatch::InotifyFileWatch(EventLoop& loop) : loop_(loop), state_(std::make_unique<State>()) {
    loop_.add(state_->inotify.fd(), EPOLLIN, [this] { deliver(); });
}

InotifyFileWatch::~InotifyFileWatch() {
    loop_.remove(state_->inotify.fd());
}

void InotifyFileWatch::deliver() {
    std::vector<FileEvent> events;
    try {
        events = state_->inotify.read_events();
    } catch (const Error& e) {
        // Not expected. Going on would wake the loop again at once, for ever: the service stops
        // and says why, and systemd starts it again (R5).
        throw Error(std::string("file watch: ") + e.what());
    }
    for (const FileEvent& e : events) {
        // Who hears it is decided first: a callback may watch or unwatch.
        std::vector<Delivery> deliveries;
        state_->deliveries(e, deliveries);
        for (const auto& [callback, change] : deliveries) callback(change);
    }
}

int InotifyFileWatch::watch(const std::filesystem::path& folder, uint32_t what,
                            std::function<void(const FileChange&)> changed) {
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
    uint32_t mask = inotify_mask(what);
    int id = state_->next_id++;
    for (const std::filesystem::path& f : folders) {
        int id_in_kernel = state_->inotify.add(f, mask | IN_MASK_ADD);
        state_->watches.push_back(State::Watch{id, id_in_kernel, f, mask, changed});
    }
    return id;
}

void InotifyFileWatch::unwatch(int id) {
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
