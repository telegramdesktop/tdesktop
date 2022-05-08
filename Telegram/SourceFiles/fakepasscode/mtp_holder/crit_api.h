#ifndef TELEGRAM_CRIT_API_H
#define TELEGRAM_CRIT_API_H

namespace Main {
class Account;
class Session;
}

namespace MTP {
class Instance;
class Sender;
}

namespace FakePasscode {
namespace details {

class criticalRequestRegister final {
public:
    explicit criticalRequestRegister(MTP::Instance* instance);
    criticalRequestRegister& operator=(mtpRequestId request);
    inline operator mtpRequestId() const {
        Expects(_id != 0);
        return _id;
    }

    criticalRequestRegister(const criticalRequestRegister& other) = delete;
    criticalRequestRegister(criticalRequestRegister&& other) = delete;
    criticalRequestRegister& operator=(const criticalRequestRegister& other) = delete;
    criticalRequestRegister& operator=(criticalRequestRegister&& other) = delete;

private:
    MTP::Instance* _instance;
    mtpRequestId _id = 0;
};

inline criticalRequestRegister registerCriticalRequest(MTP::Instance* instance) {
    return criticalRequestRegister(instance);
}

criticalRequestRegister registerCriticalRequest(MTP::Sender* sender);
criticalRequestRegister registerCriticalRequest(Main::Account* account);
criticalRequestRegister registerCriticalRequest(Main::Session* session);
criticalRequestRegister registerCriticalRequest(Main::Session& session);

}}

#define FAKE_CRITICAL_REQUEST(owner) ::FakePasscode::details::registerCriticalRequest(owner) =

#endif //TELEGRAM_CRIT_API_H
