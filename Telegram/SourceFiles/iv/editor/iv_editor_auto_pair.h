/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QKeyEvent;

namespace Ui {
class InputField;
} // namespace Ui

namespace Iv::Editor {

[[nodiscard]] bool HandleAutoPairKey(
	not_null<Ui::InputField*> field,
	not_null<QKeyEvent*> e);

} // namespace Iv::Editor
