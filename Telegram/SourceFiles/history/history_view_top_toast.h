/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace HistoryView {

class InfoTooltip final {
public:
	InfoTooltip();

	void show(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const TextWithEntities &text,
		Fn<void()> hiddenCallback);
	void hide(anim::type animated);

private:
	base::weak_ptr<Ui::Toast::Instance> _topToast;

};

} // namespace HistoryView
