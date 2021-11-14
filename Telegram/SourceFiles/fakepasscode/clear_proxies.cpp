#include "clear_proxies.h"

#include "core/application.h"

void FakePasscode::ClearProxies::Execute() const {
    Core::App().settings().proxy().list().clear();
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
