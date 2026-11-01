/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "rpl/lifetime.h"

#include <QtCore/QRect>

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ImportantTooltip;
} // namespace Ui

namespace Wallet {

class OwnCard;

class CollectiblesHint final {
public:
	CollectiblesHint(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		Fn<std::optional<QRect>()> target);
	~CollectiblesHint();

	void refresh();
	void record();

private:
	[[nodiscard]] OwnCard &ownCard() const;

	const not_null<Main::Session*> _session;
	const Fn<std::optional<QRect>()> _target;
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;
	bool _shown = false;
	bool _done = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
