#pragma once

#include "tpsi/session.hpp"

#include <string>

namespace tpsi {

SessionConfig read_session_config(const std::string &path);
void write_session_config(const SessionConfig &cfg, const std::string &path);

} // namespace tpsi

