#include "clear_proxies.h"

#include "core/application.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::ClearProxies::Execute() {
    FAKE_LOG(("Remove proxies, setup disabled proxy"));
    auto& app = Core::App();
    auto& proxies = app.settings().proxy();
    proxies.list().clear();
    if (proxies.settings() == MTP::ProxyData::Settings::Enabled) {
        proxies.setUseProxyForCalls(false);
        proxies.setTryIPv6(false);
        app.setCurrentProxy(MTP::ProxyData(), MTP::ProxyData::Settings::Disabled);
    }
    app.saveSettings();
}

QByteArray FakePasscode::ClearProxies::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::ClearProxy);
    return result;
}

FakePasscode::ActionType FakePasscode::ClearProxies::GetType() const {
    return FakePasscode::ActionType::ClearProxy;
}
