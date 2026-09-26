/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui {

[[nodiscard]] object_ptr<RpWidget> MakeCustomEmojiToastIcon(
	not_null<Main::Session*> session,
	DocumentId emojiId,
	QSize size);

} // namespace Ui
