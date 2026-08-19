/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"

#include <rpl/producer.h>

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ImportantTooltip;
class ScrollArea;
} // namespace Ui

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

class AnchoredTooltip final {
public:
	void show(
		not_null<QWidget*> scroll,
		rpl::producer<> scrolls,
		QRect globalArea,
		TextWithEntities text);
	void hide();

private:
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;

};

} // namespace HistoryView
