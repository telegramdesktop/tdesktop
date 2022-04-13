#ifndef TELEGRAM_AUTODELETE_BOX_H
#define TELEGRAM_AUTODELETE_BOX_H

#include <base/basic_types.h>
#include <base/object_ptr.h>
#include <ui/boxes/choose_date_time.h>

namespace Api {
struct SendOptions;
}

namespace Ui {
class RpWidget;
class GenericBox;
}

namespace FakePasscode {

object_ptr<Ui::GenericBox> AutoDeleteBox(
    not_null<Ui::RpWidget*> parent,
    Fn<void(Api::SendOptions)> send,
    Ui::ChooseDateTimeStyleArgs style = Ui::ChooseDateTimeStyleArgs());

}

#endif //TELEGRAM_AUTODELETE_BOX_H
