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
      FakePasscode() = default;

      explicit FakePasscode(std::vector<std::shared_ptr<Action>> actions);

      virtual ~FakePasscode() = default;

      void Execute() const;

      [[nodiscard]] MTP::AuthKeyPtr GetEncryptedPasscode() const;
      [[nodiscard]] QByteArray GetPasscode() const;
      void SetPasscode(const QByteArray& passcode);
      void SetPasscode(MTP::AuthKeyPtr passcode);

      const QByteArray &getSalt() const;
      void setSalt(const QByteArray &salt);

      const QByteArray &getRealPasscode() const;
      void setRealPasscode(const QByteArray &realPasscode);

      const QString &GetName() const;
      void SetName(QString name);

      bool CheckPasscode(const QByteArray& passcode) const;

      void AddAction(std::shared_ptr<Action> action);
      void RemoveAction(std::shared_ptr<Action> action);
      bool ContainsAction(ActionType type) const;

      [[nodiscard]] const std::vector<std::shared_ptr<Action>>& GetActions() const;
      const std::shared_ptr<Action>& operator[](size_t index) const;

      void SetSalt(QByteArray salt);

      QByteArray SerializeActions() const;
      void DeSerializeActions(QByteArray serialized);

      bool operator==(const FakePasscode& other) const;
      bool operator!=(const FakePasscode& other) const;

   protected:
      MTP::AuthKeyPtr passcode_;
      QByteArray salt_;
      QByteArray fake_passcode_;
      std::vector<std::shared_ptr<Action>> actions_;
      QByteArray real_passcode_; // No chance without :(
      QString name_;
  };
}

#endif //TELEGRAM_FAKE_PASSCODE_H
