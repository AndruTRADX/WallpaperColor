// Model: the parts the terminal's features run with, one implementation of each contract, as
// main.cpp wires them. The commands and the UI hand each feature only the parts it uses.

#pragma once

#include "terminal/application/contracts/external/driver_modules.hpp"
#include "terminal/application/contracts/external/message_codec.hpp"
#include "terminal/application/contracts/external/pictures.hpp"
#include "terminal/application/contracts/external/service_socket.hpp"
#include "terminal/application/contracts/external/settings_file.hpp"

namespace wpc {

struct TerminalParts {
    ServiceSocket& service;
    MessageCodec& codec;
    DriverModules& drivers;
    Pictures& pictures;
    SettingsFormat& format;
};

}  // namespace wpc
