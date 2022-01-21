#pragma once

#include "base/debug_log.h"

namespace base {

void LogFakeMain(const QString& message, const char *file);

}

#define FAKE_LOG(message) (::base::LogFakeMain(QString message, SOURCE_FILE_BASENAME))
