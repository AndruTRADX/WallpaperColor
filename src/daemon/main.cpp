// wallpapercolord: paints every exposed RGB device with the wallpaper on screen.
//
// One event loop on the main thread, fed by helper threads that sleep until something happens:
// - D-Bus: Plasma's `wallpaperChanged` signal and plasmashell (re)starts;
// - a Unix socket for the terminal client (status, pause, resume, apply);
// - inotify: files opened in the slideshow folders (plasmashell opens the next slide), and
//   changes to the desktop settings (plasma-org.kde.plasma.desktop-appletsrc), kdeglobals
//   (light/dark wallpaper variants) and our config file.
//
// Events only *trigger* a check: the picture on screen always comes from Plasma over D-Bus,
// so a stray file access cannot paint the wrong colour (same picture → nothing is sent).
// Bursts are coalesced; nothing runs between events except retries while OpenLinkHub or
// plasmashell is unreachable.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>

#include <malloc.h>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "core/config.hpp"
#include "core/arc.hpp"
#include "core/extract.hpp"
#include "core/ipc.hpp"
#include "core/paint.hpp"
#include "kde/plasma.hpp"
#include "openrgb/client.hpp"
#include "util/channel.hpp"
#include "util/error.hpp"
#include "util/inotify.hpp"
#include "util/log.hpp"
#include "util/socket.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using namespace wpc;
using Clock = std::chrono::steady_clock;

namespace {

/// Quiet time that ends a burst of events (a slideshow folder rescan opens every file).
constexpr auto SETTLE = 800ms;
constexpr auto RETRY_MIN = 2s;
constexpr auto RETRY_MAX = 60s;
/// Frames per second of a fade, and its longest duration (requests from the terminal UI wait
/// while a fade runs, and give up after 2 s).
constexpr int FADE_FPS = 25;
constexpr double FADE_MAX_SECONDS = 1.5;
/// Where plasmashell saves the desktop settings, wallpaper included.
constexpr std::string_view APPLETSRC = "plasma-org.kde.plasma.desktop-appletsrc";
const std::vector<std::string_view> IMAGE_EXTENSIONS = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
/// Watch mask for the config folders.
constexpr uint32_t CONFIG_EVENTS = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;

// ---------------------------------------------------------------------------------------------
// Events

/// Plasma's wallpaper settings changed or plasmashell (re)started.
struct PlasmaEvent {};

/// A client request and where to send the answer.
struct IpcEvent {
    ipc::Request request;
    std::shared_ptr<Channel<ipc::Response>> reply;
};

using Event = std::variant<PlasmaEvent, FileEvent, IpcEvent>;

/// What a burst of events asks for.
struct Todo {
    /// The picture may have changed.
    bool check = false;
    /// Re-read the config.
    bool reload = false;
    /// Send even if nothing changed.
    bool force = false;
};

/// A pending retry: when, and how long the last wait was (it doubles each time).
struct Retry {
    Clock::time_point at;
    Clock::duration delay;
};

Config load_config(const std::optional<fs::path>& path) {
    if (!path) return Config{};
    try {
        return Config::load(*path);
    } catch (const Error& e) {
        log("{}; using the defaults", e.what());
        return Config{};
    }
}

/// `dir` and every folder below it.
std::vector<fs::path> subdirs(const fs::path& dir) {
    std::vector<fs::path> out = {dir};
    for (size_t i = 0; i < out.size(); ++i) {
        std::error_code ec;
        fs::directory_iterator it(out[i], ec);
        if (ec) continue;
        for (const fs::directory_entry& entry : it) {
            std::error_code e;
            if (entry.symlink_status(e).type() == fs::file_type::directory) out.push_back(entry.path());
        }
    }
    return out;
}

/// Lower-case extension without the dot ("" if none).
std::string extension_of(const std::string& name) {
    std::string ext = fs::path(name).extension().string();
    if (!ext.empty()) ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// ---------------------------------------------------------------------------------------------
// Helper threads. Each one sleeps until its source has something, then sends an Event.

/// Answers clients on the socket: one JSON request line in, one JSON response line out.
void start_ipc_listener(Channel<Event>& events) {
    fs::path path = ipc::socket_path();
    std::error_code ignored;
    fs::remove(path, ignored);  // stale socket of a previous run
    auto listener = std::make_shared<Fd>(unix_listen(path));

    std::thread([listener, &events] {
        while (true) {
            Fd client;
            try {
                client = accept_client(*listener);
            } catch (const Error&) {
                continue;
            }
            std::string line;
            try {
                set_timeouts(client, Seconds(2), Seconds(0));
                line = read_line(client);
            } catch (const Error&) {
                continue;  // nothing readable: drop the connection
            }

            ipc::Response response;
            try {
                ipc::Request request = ipc::request_from_json(line);
                auto reply = std::make_shared<Channel<ipc::Response>>();
                events.send(IpcEvent{request, reply});
                response = reply->receive_for(2s).value_or(ipc::Response::error("service busy"));
            } catch (const Error& e) {
                response = ipc::Response::error(std::string("bad request: ") + e.what());
            }

            std::string out;
            try {
                out = ipc::to_json(response);
            } catch (const std::exception&) {
                out = "";  // a path that is not valid UTF-8: send an empty line
            }
            try {
                write_all(client, out + "\n");
            } catch (const Error&) {
            }
        }
    }).detach();
}

void start_inotify_reader(std::shared_ptr<Inotify> inotify, Channel<Event>& events) {
    std::thread([inotify, &events] {
        while (true) {
            std::vector<FileEvent> batch;
            try {
                batch = inotify->read_events();
            } catch (const Error& e) {
                log("inotify: {}", e.what());
                return;
            }
            for (FileEvent& e : batch) events.send(std::move(e));
        }
    }).detach();
}

// ---------------------------------------------------------------------------------------------

class Daemon {
public:
    explicit Daemon(std::optional<fs::path> config_path);
    [[noreturn]] void run();

private:
    void classify(Event event, Todo& todo);
    ipc::Response answer(ipc::Request request, Todo& todo);
    void on_file(const FileEvent& e, Todo& todo);
    ipc::Status status() const;
    void sync(bool force);
    std::vector<ipc::DeviceInfo> send(const std::vector<Rgb>& stops);
    void fade(openrgb::Client& client, const std::vector<openrgb::Controller>& controllers,
              const std::vector<std::vector<Rgb>>& targets);
    void fail(const std::string& message);
    void watch_slides(const std::vector<fs::path>& dirs);
    bool watching_config_dir(const fs::path& dir) const;

    std::optional<fs::path> config_path_;
    Config config_;
    kde::Plasma plasma_;
    Channel<Event> events_;
    std::shared_ptr<Inotify> inotify_;
    /// Slideshow folders being watched, by watch id.
    std::map<int, fs::path> slide_dirs_;
    /// ~/.config (for kdeglobals) and the folder of our config file, by watch id.
    std::map<int, fs::path> config_dirs_;
    /// What was last sent: picture, its mtime and the settings used.
    std::optional<std::string> last_;
    std::optional<Retry> retry_;
    /// What each device shows now (what we sent last), by "index name": where fades start.
    std::map<std::string, std::vector<Rgb>> shown_;
    bool paused_ = false;
    /// What clients see.
    ipc::Status status_;
};

Daemon::Daemon(std::optional<fs::path> config_path)
    : config_path_(std::move(config_path)), config_(load_config(config_path_)), plasma_([] {
          try {
              return kde::Plasma::connect();
          } catch (const Error& e) {
              throw Error(std::string("cannot reach the session bus: ") + e.what());
          }
      }()) {
    plasma_.listen([this] { events_.send(PlasmaEvent{}); });
    start_ipc_listener(events_);
    inotify_ = std::make_shared<Inotify>();
    start_inotify_reader(inotify_, events_);

    std::vector<fs::path> dirs;
    if (std::optional<fs::path> kdeglobals = kde::kdeglobals_path()) dirs.push_back(kdeglobals->parent_path());
    if (config_path_) dirs.push_back(config_path_->parent_path());
    for (const fs::path& dir : dirs) {
        if (watching_config_dir(dir)) continue;
        try {
            config_dirs_[inotify_->add(dir, CONFIG_EVENTS)] = dir;
        } catch (const Error& e) {
            log("not watching {}: {}", dir.string(), e.what());
        }
    }

    log("wallpapercolord {}: mode {}, sending to {}", WPC_VERSION, mode_name(config_.led.mode),
        config_.openrgb.address);
    status_.version = WPC_VERSION;
}

void Daemon::run() {
    sync(false);
    while (true) {
        std::optional<Event> event;
        if (retry_) {
            Clock::duration wait = std::max(retry_->at - Clock::now(), Clock::duration::zero());
            event = events_.receive_for(wait);
        } else {
            event = events_.receive();
        }

        Todo todo;
        todo.check = !event.has_value();  // retry time
        if (event) classify(std::move(*event), todo);
        if (!todo.check && !todo.reload && !todo.force) continue;

        // coalesce the rest of the burst (client requests are still answered at once)
        if (!todo.force) {
            while (std::optional<Event> more = events_.receive_for(SETTLE)) classify(std::move(*more), todo);
        }
        if (todo.reload) {
            config_ = load_config(config_path_);
            log("config reloaded: mode {}", mode_name(config_.led.mode));
        }
        sync(todo.reload || todo.force);
    }
}

/// Decides what an event asks for; answers client requests right away.
void Daemon::classify(Event event, Todo& todo) {
    if (std::holds_alternative<PlasmaEvent>(event)) {
        todo.check = true;
    } else if (IpcEvent* ipc_event = std::get_if<IpcEvent>(&event)) {
        ipc_event->reply->send(answer(ipc_event->request, todo));
    } else {
        on_file(std::get<FileEvent>(event), todo);
    }
}

ipc::Response Daemon::answer(ipc::Request request, Todo& todo) {
    switch (request) {
        case ipc::Request::Status:
            return ipc::Response::of(status());
        case ipc::Request::Pause:
            paused_ = true;
            log("paused");
            return ipc::Response::ok();
        case ipc::Request::Resume:
            paused_ = false;
            todo.force = true;
            log("resumed");
            return ipc::Response::ok();
        case ipc::Request::Apply:
            todo.force = true;
            return ipc::Response::ok();
    }
    return ipc::Response::ok();
}

void Daemon::on_file(const FileEvent& e, Todo& todo) {
    if (auto slide_dir = slide_dirs_.find(e.watch); slide_dir != slide_dirs_.end()) {
        // Opening the picture on screen is not a new slide: it is usually us, reading it to take
        // its colours. Without this, each retry while OpenLinkHub is down would trigger the next
        // one 0.8 s later instead of backing off.
        bool opened_current = (e.mask & IN_OPEN) && status_.image == slide_dir->second / e.name;
        if (opened_current) return;

        bool is_image = std::find(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end(), extension_of(e.name)) !=
                        IMAGE_EXTENSIONS.end();
        bool new_folder = (e.mask & IN_CREATE) && (e.mask & IN_ISDIR);
        if (is_image || new_folder) todo.check = true;
        return;
    }
    auto found = config_dirs_.find(e.watch);
    if (found == config_dirs_.end()) return;
    fs::path dir = found->second;

    // kdeglobals: a light/dark switch picks another wallpaper package variant.
    // appletsrc: wallpaper changed in the settings, which does not always emit
    // wallpaperChanged (e.g. switching from a slideshow to a single picture).
    if (e.name == "kdeglobals" || e.name == APPLETSRC) todo.check = true;

    if (!config_path_) return;
    fs::path config_dir = config_path_->parent_path();
    if (config_dir == dir && config_path_->filename() == e.name) todo.reload = true;

    // our config folder was created after we started: watch it now
    if (config_dir.parent_path() == dir && config_dir.filename() == e.name && !watching_config_dir(config_dir)) {
        try {
            config_dirs_[inotify_->add(config_dir, CONFIG_EVENTS)] = config_dir;
            todo.reload = true;
        } catch (const Error&) {
        }
    }
}

bool Daemon::watching_config_dir(const fs::path& dir) const {
    return std::any_of(config_dirs_.begin(), config_dirs_.end(), [&](const auto& entry) { return entry.second == dir; });
}

ipc::Status Daemon::status() const {
    ipc::Status s = status_;
    s.paused = paused_;
    s.mode = config_.led.mode;
    return s;
}

/// Asks Plasma for the picture on screen and sends its colours if anything changed.
void Daemon::sync(bool force) {
    kde::Wallpaper wallpaper;
    try {
        wallpaper = plasma_.wallpaper(0);
    } catch (const Error& e) {
        return fail(std::string("cannot ask Plasma for the wallpaper: ") + e.what());
    }
    status_.plugin = wallpaper.plugin;
    watch_slides(wallpaper.is_slideshow() ? wallpaper.slide_paths : std::vector<fs::path>{});

    std::optional<fs::path> image;
    if (wallpaper.image) image = kde::resolve_image(*wallpaper.image, kde::dark_mode());
    if (!image) {
        retry_.reset();
        return log("no picture to take colours from (plugin {})", wallpaper.plugin);
    }

    status_.image = *image;
    std::error_code ec;
    fs::file_time_type mtime = fs::last_write_time(*image, ec);
    std::string mtime_text = ec ? "none" : std::to_string(mtime.time_since_epoch().count());
    const ExtractConfig& x = config_.extract;
    std::string key = std::format("{} {} {} {} {} {} {} {} {} {}", image->string(), mtime_text,
                                  mode_name(config_.led.mode), x.palette_size, x.sample_size,
                                  x.min_saturation, x.min_chroma, x.min_share, x.skin_weight, x.pastel_gain);
    if (!force && !retry_ && last_ == key) return;

    std::vector<Rgb> stops;
    try {
        stops = extract_path(*image, config_.extract).stops(config_.led.mode);
        malloc_trim(0);  // give the decoded picture's memory back to the system
    } catch (const Error& e) {
        malloc_trim(0);
        retry_.reset();
        status_.error = std::format("{}: {}", image->string(), e.what());
        return log("{}: {}", image->string(), e.what());
    }
    std::vector<std::string> colours;
    for (Rgb c : stops) colours.push_back(c.hex());
    status_.stops = colours;
    if (paused_) {
        last_.reset();  // send on resume
        return;
    }

    try {
        std::vector<ipc::DeviceInfo> devices = send(stops);
        std::string joined;
        for (size_t i = 0; i < colours.size(); ++i) joined += (i ? " " : "") + colours[i];
        log("{} → {} device(s) from {}", joined, devices.size(), image->string());
        status_.devices = devices;
        status_.error.reset();
        status_.last_sent = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        last_ = key;
        retry_.reset();
    } catch (const Error& e) {
        fail(std::format("cannot reach the OpenRGB server at {}: {}", config_.openrgb.address, e.what()));
    }
}

std::vector<ipc::DeviceInfo> Daemon::send(const std::vector<Rgb>& stops) {
    openrgb::Client client = openrgb::Client::connect(config_.openrgb.address, config_.openrgb.client_name);
    std::vector<openrgb::Controller> controllers = client.controllers();
    Painter painter(stops, config_.led.mode, config_);
    std::vector<std::vector<Rgb>> targets;
    for (const openrgb::Controller& c : controllers) targets.push_back(c.paint(painter));
    fade(client, controllers, targets);

    std::vector<ipc::DeviceInfo> devices;
    for (const openrgb::Controller& c : controllers) {
        ipc::DeviceInfo d{c.name, c.kind, {}};
        for (const openrgb::Zone& z : c.zones) d.zones.push_back(ipc::ZoneInfo{z.name, z.leds});
        devices.push_back(std::move(d));
    }
    return devices;
}

/// Moves every device from what it shows to `targets` over `[led] fade` seconds.
void Daemon::fade(openrgb::Client& client, const std::vector<openrgb::Controller>& controllers,
                  const std::vector<std::vector<Rgb>>& targets) {
    // Where each device starts: what we sent it last, else what it reports showing.
    std::vector<std::vector<Rgb>> from;
    bool changes = false;
    for (size_t i = 0; i < controllers.size(); ++i) {
        const openrgb::Controller& c = controllers[i];
        auto last = shown_.find(std::format("{} {}", c.index, c.name));
        if (last != shown_.end() && last->second.size() == targets[i].size()) {
            from.push_back(last->second);
        } else if (c.colors.size() == targets[i].size()) {
            from.push_back(c.colors);
        } else {
            from.push_back(targets[i]);  // unknown: no fade for this one
        }
        changes = changes || from[i] != targets[i];
    }

    double seconds = std::clamp(config_.led.fade, 0.0, FADE_MAX_SECONDS);
    int frames = changes ? std::max(1, static_cast<int>(std::round(seconds * FADE_FPS))) : 1;
    Clock::time_point start = Clock::now();
    for (int f = 1; f <= frames; ++f) {
        float t = static_cast<float>(f) / static_cast<float>(frames);
        t = t * t * (3.0f - 2.0f * t);  // ease in and out
        for (size_t i = 0; i < controllers.size(); ++i) {
            std::vector<Rgb> frame(targets[i].size());
            for (size_t led = 0; led < frame.size(); ++led) {
                frame[led] = (f == frames) ? targets[i][led] : mix(from[i][led], targets[i][led], t);
            }
            client.set_leds(controllers[i].index, frame);
        }
        if (f < frames) std::this_thread::sleep_until(start + f * std::chrono::milliseconds(1000 / FADE_FPS));
    }
    for (size_t i = 0; i < controllers.size(); ++i) {
        shown_[std::format("{} {}", controllers[i].index, controllers[i].name)] = targets[i];
    }
}

/// Logs and schedules a retry with exponential backoff.
void Daemon::fail(const std::string& message) {
    Clock::duration delay = retry_ ? std::min<Clock::duration>(retry_->delay * 2, RETRY_MAX) : RETRY_MIN;
    log("{} (retrying in {}s)", message, std::chrono::duration_cast<std::chrono::seconds>(delay).count());
    status_.error = message;
    retry_ = Retry{Clock::now() + delay, delay};
}

/// Watches exactly `dirs` and their subfolders (Plasma slideshows are recursive).
void Daemon::watch_slides(const std::vector<fs::path>& dirs) {
    std::vector<fs::path> wanted;
    for (const fs::path& d : dirs) {
        for (fs::path& sub : subdirs(d)) wanted.push_back(std::move(sub));
    }
    bool unchanged = wanted.size() == slide_dirs_.size() &&
                     std::all_of(wanted.begin(), wanted.end(), [&](const fs::path& w) {
                         return std::any_of(slide_dirs_.begin(), slide_dirs_.end(),
                                            [&](const auto& entry) { return entry.second == w; });
                     });
    if (unchanged) return;

    for (const auto& [watch, dir] : slide_dirs_) inotify_->remove(watch);
    slide_dirs_.clear();
    for (const fs::path& dir : wanted) {
        try {
            slide_dirs_[inotify_->add(dir, IN_OPEN | IN_CREATE)] = dir;
        } catch (const Error& e) {
            log("not watching {}: {}", dir.string(), e.what());
        }
    }
    if (!slide_dirs_.empty()) log("watching {} slideshow folder(s)", slide_dirs_.size());
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<fs::path> config_path = (argc > 1) ? std::optional<fs::path>(argv[1]) : Config::default_path();
    try {
        Daemon daemon(config_path);
        daemon.run();
    } catch (const Error& e) {
        log("wallpapercolord: {}", e.what());
        return EXIT_FAILURE;
    }
}
