/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_selection_modifier.h"

#include "base/qt/qt_key_modifiers.h"

namespace HistoryView {

TextSelectType MouseTextSelectType(TextSelectType type) {
	return (type == TextSelectType::Letters && base::IsAltPressed())
		? TextSelectType::Words
		: type;
}

} // namespace HistoryView
