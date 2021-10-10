#ifndef TELEGRAM_CLEAR_PROXIES_H
#define TELEGRAM_CLEAR_PROXIES_H

#include "action.h"

namespace FakePasscode {
  class ClearProxies : public Action {
   public:
      void Execute() const override;
  };
}

#endif //TELEGRAM_CLEAR_PROXIES_H
