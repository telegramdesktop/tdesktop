#ifndef TELEGRAM_CLEAR_PROXIES_H
#define TELEGRAM_CLEAR_PROXIES_H

#include "fakepasscode/action.h"

namespace FakePasscode {
  class ClearProxies : public Action {
   public:
      void Execute() override;

      QByteArray Serialize() const override;

      ActionType GetType() const override;
  };
}

#endif //TELEGRAM_CLEAR_PROXIES_H
