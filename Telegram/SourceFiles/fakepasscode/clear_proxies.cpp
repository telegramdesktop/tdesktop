#include "clear_proxies.h"

#include "core/application.h"

void FakePasscode::ClearProxies::Execute() const {
    auto& proxies = Core::App().settings().proxy();
    proxies.list().clear();
    proxies.setUseProxyForCalls(false);
    proxies.setTryIPv6(false);
    proxies.setSelected(MTP::ProxyData());
    proxies.setSettings(MTP::ProxyData::Settings::Disabled);
    Core::App().saveSettings();
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
