#include "fake_log.h"

#include "base/debug_log.h"
#include "storage/storage_domain.h"
#include "core/application.h"
#include "main/main_domain.h"

void base::LogFakeMain(const QString& message, const char *file) {
    if (Core::App().domain().local().IsAdvancedLoggingEnabled()) {
        LogWriteMain(QString("%1 (%2 : %3)").arg(
                message,
                QString::fromUtf8(file),
                QString::number(__LINE__)));
    }
}
