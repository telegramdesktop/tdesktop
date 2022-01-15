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

      FakePasscode(FakePasscode&& passcode) noexcept;

      virtual ~FakePasscode() = default;

      void Execute();

      [[nodiscard]] MTP::AuthKeyPtr GetEncryptedPasscode() const;
      [[nodiscard]] QByteArray GetPasscode() const;
      void SetPasscode(QByteArray passcode);

      rpl::producer<QString> GetName() const;
      const QString& GetCurrentName() const;
      void SetName(QString name);

      bool CheckPasscode(const QByteArray& passcode) const;

      void AddAction(std::shared_ptr<Action> action);
      void UpdateAction(std::shared_ptr<Action> action);
      void RemoveAction(std::shared_ptr<Action> action);
      bool ContainsAction(ActionType type) const;
      std::shared_ptr<Action> GetAction(ActionType type) const;

      [[nodiscard]] rpl::producer<std::vector<std::shared_ptr<Action>>> GetActions() const;
      const std::shared_ptr<Action>& operator[](size_t index) const;

      void SetSalt(QByteArray salt);
      const QByteArray& GetSalt() const;

      QByteArray SerializeActions() const;
      void DeSerializeActions(QByteArray serialized);

      bool operator==(const FakePasscode& other) const;
      bool operator!=(const FakePasscode& other) const;

      FakePasscode& operator=(FakePasscode&& passcode) noexcept;

   protected:
      QByteArray salt_;
      QByteArray fake_passcode_;
      std::vector<std::shared_ptr<Action>> actions_;
      QString name_;

      rpl::event_stream<> state_changed_;
  };
}

#endif //TELEGRAM_FAKE_PASSCODE_H
