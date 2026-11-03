#include "daemon/presentation/requests/requests.hpp"

#include "daemon/application/features/pause/pause.hpp"
#include "daemon/application/features/report_status/report_status.hpp"
#include "daemon/application/features/white_test/white_test.hpp"

namespace wpc {

ipc::Response run_request(ipc::Request request, ServiceState& state, bool& paint_now) {
    switch (request) {
        case ipc::Request::Status:
            return ipc::Response::of(report_status(state));
        case ipc::Request::Pause:
            pause(state);
            return ipc::Response::ok();
        case ipc::Request::Resume:
            resume(state);
            break;
        case ipc::Request::Apply:
            break;
        case ipc::Request::Test:
            start_white_test(state);
            break;
        case ipc::Request::TestOff:
            stop_white_test(state);
            break;
    }
    paint_now = true;
    return ipc::Response::ok();
}

}  // namespace wpc
