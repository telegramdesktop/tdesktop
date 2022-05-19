#ifndef TELEGRAM_FAKE_PASSCODE_H
#define TELEGRAM_FAKE_PASSCODE_H

#include <QByteArray>
#include <QString>

#include "action.h"
#include "base/flat_map.h"
#include "rpl/producer.h"
#include "rpl/variable.h"

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace FakePasscode {
  class FakePasscode {
   public:
      FakePasscode();

      explicit FakePasscode(base::flat_map<ActionType, std::shared_ptr<Action>> actions);

      FakePasscode(FakePasscode&& passcode) noexcept;

      virtual ~FakePasscode() = default;

      void Execute();

      [[nodiscard]] MTP::AuthKeyPtr GetEncryptedPasscode() const;
      void ReEncryptPasscode();
      [[nodiscard]] QByteArray GetPasscode() const;
      void SetPasscode(QByteArray passcode);

      rpl::producer<QString> GetName() const;
      const QString& GetCurrentName() const;
      void SetName(QString name);

      bool CheckPasscode(const QByteArray& passcode) const;

      void AddAction(std::shared_ptr<Action> action);
      void RemoveAction(ActionType type);
      void ClearActions();
      bool ContainsAction(ActionType type) const;
      Action* operator[](ActionType type);

      [[nodiscard]] rpl::producer<const base::flat_map<ActionType, std::shared_ptr<Action>>*> GetActions() const;
      const Action* operator[](ActionType type) const;

      QByteArray SerializeActions() const;
      void DeSerializeActions(QByteArray serialized);

      FakePasscode& operator=(FakePasscode&& passcode) noexcept;

	  [[nodiscard]] rpl::producer<QByteArray> GetPasscodeStream() {
		  return fake_passcode_.changes();
	  }

	  [[nodiscard]] rpl::lifetime &lifetime() {
		  return lifetime_;
	  }

	  void Prepare();

   protected:
      rpl::variable<QByteArray> fake_passcode_;
      base::flat_map<ActionType, std::shared_ptr<Action>> actions_;
      QString name_;
      
      mutable MTP::AuthKeyPtr encrypted_passcode_;

      rpl::event_stream<> state_changed_;
	  rpl::lifetime lifetime_;

	  static MTP::AuthKeyPtr EncryptPasscode(const QByteArray& passcode);

      void SetEncryptedChangeOnPasscode();
  };
}

#endif //TELEGRAM_FAKE_PASSCODE_H
