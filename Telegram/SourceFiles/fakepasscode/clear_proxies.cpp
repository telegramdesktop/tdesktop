#include "clear_proxies.h"

#include "core/application.h"

void FakePasscode::ClearProxies::Execute() const {
    Core::App().settings().proxy().list().clear();
    Core::App().saveSettings();
}
