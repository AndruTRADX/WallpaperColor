// Feature: pause and resume. While paused, colours are taken but not painted; the devices keep
// the last ones. Only in memory: a restart resumes.

#pragma once

#include "daemon/application/models/service_state.hpp"

namespace wpc {

void pause(ServiceState& state);
void resume(ServiceState& state);

}  // namespace wpc
