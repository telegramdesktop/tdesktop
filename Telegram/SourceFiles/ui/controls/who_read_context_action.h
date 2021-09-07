/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
namespace Menu {
class ItemBase;
} // namespace Menu

class PopupMenu;

struct WhoReadParticipant {
	QString name;
	QImage userpic;
	std::pair<uint64, uint64> userpicKey = {};
	uint64 id = 0;
};

struct WhoReadContent {
	std::vector<WhoReadParticipant> participants;
	bool listened = false;
	bool unknown = false;
};

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhoReadContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Fn<void(uint64)> participantChosen);

} // namespace Ui
