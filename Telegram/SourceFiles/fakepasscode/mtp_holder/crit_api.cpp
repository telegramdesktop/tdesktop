#include "crit_api.h"
#include "mtp_holder.h"

#include <core/application.h>
#include <mtproto/sender.h>
#include <main/main_account.h>
#include <main/main_session.h>

#include "fakepasscode/log/fake_log.h"

namespace FakePasscode {
namespace details {

criticalRequestRegister::criticalRequestRegister(MTP::Instance *instance)
    : _instance(instance){
    Expects(instance != nullptr);
}

criticalRequestRegister &criticalRequestRegister::operator=(mtpRequestId request) {
    Expects(request != 0);
    _id = request;
    FAKE_LOG(qsl("Set request %1 as critical").arg(request));
    Core::App().GetFakeMtpHolder()->RegisterCriticalRequest(_instance, request);
    FAKE_LOG(qsl("Set request %1 as critical, success").arg(request));
    return *this;
}

criticalRequestRegister registerCriticalRequest(MTP::Sender *sender) {
    return registerCriticalRequest(&sender->instance());
}

criticalRequestRegister registerCriticalRequest(Main::Account *account) {
    return registerCriticalRequest(&account->mtp());
}

criticalRequestRegister registerCriticalRequest(Main::Session *session) {
    return registerCriticalRequest(&session->mtp());
}

criticalRequestRegister registerCriticalRequest(Main::Session &session) {
    return registerCriticalRequest(&session.mtp());
}

}}
