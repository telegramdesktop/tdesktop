#ifndef TELEGRAM_FAKE_PASSCODE_H
#define TELEGRAM_FAKE_PASSCODE_H

#include <QByteArray>
#include "action.h"

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace FakePasscode {
  class FakePasscode {
   public:
      virtual ~FakePasscode() = default;

      virtual void Execute() = 0;

      [[nodiscard]] MTP::AuthKeyPtr GetPasscode() const;
      void SetPasscode(const QByteArray& passcode);

      void AddAction(std::unique_ptr<Action> action);

      void RemoveAction(std::unique_ptr<Action> action);

      [[nodiscard]] const std::vector<std::unique_ptr<Action>>& GetActions() const;
      const std::unique_ptr<Action>& operator[](size_t index) const;

      static void SetSalt(QByteArray salt);

   protected:
      MTP::AuthKeyPtr passcode_;
      static QByteArray salt_;
      std::vector<std::unique_ptr<Action>> actions_;
      static QByteArray real_passcode_; // Need to reduce overhead of memory
  };
}

#endif //TELEGRAM_FAKE_PASSCODE_H
