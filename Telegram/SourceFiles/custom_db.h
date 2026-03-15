#pragma once

#include <QtCore/QString>
#include "base/basic_types.h"

class HistoryItem;

namespace CustomDB {

void Init();
void SaveMessage(not_null<HistoryItem*> item);

} // namespace CustomDB
