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

      explicit FakePasscode(base::flat_map<ActionType, std::shared_ptr<Action>> actions);

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
      void RemoveAction(ActionType type);
      bool ContainsAction(ActionType type) const;
      Action* operator[](ActionType type);

      [[nodiscard]] rpl::producer<const base::flat_map<ActionType, std::shared_ptr<Action>>*> GetActions() const;
      const Action* operator[](ActionType type) const;

      QByteArray SerializeActions() const;
      void DeSerializeActions(QByteArray serialized);

      FakePasscode& operator=(FakePasscode&& passcode) noexcept;

	  [[nodiscard]] rpl::producer<> GetPasscodeStream() {
		  return passcode_changed_.events();
	  }

	  [[nodiscard]] rpl::lifetime &lifetime() {
		  return lifetime_;
	  }

   protected:
      QByteArray fake_passcode_;
      base::flat_map<ActionType, std::shared_ptr<Action>> actions_;
      QString name_;

      rpl::event_stream<> state_changed_;
	  rpl::event_stream<> passcode_changed_;
	  rpl::lifetime lifetime_;

	  static MTP::AuthKeyPtr EncryptPasscode(const QByteArray& passcode);
  };
}

#endif //TELEGRAM_FAKE_PASSCODE_H
