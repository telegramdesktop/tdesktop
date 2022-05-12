#include "clear_proxies.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "apiwrap.h"

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
    for (const auto&[_, account]: app.domain().accounts()) {
        if (account->sessionExists()) {
            account->session().api().refreshTopPromotion();
        }
    }
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
