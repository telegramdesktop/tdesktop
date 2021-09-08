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
	QImage userpicSmall;
	QImage userpicLarge;
	std::pair<uint64, uint64> userpicKey = {};
	uint64 id = 0;

	static constexpr auto kMaxSmallUserpics = 3;
};

bool operator==(const WhoReadParticipant &a, const WhoReadParticipant &b);
bool operator!=(const WhoReadParticipant &a, const WhoReadParticipant &b);

enum class WhoReadType {
	Seen,
	Listened,
	Watched,
};

struct WhoReadContent {
	std::vector<WhoReadParticipant> participants;
	WhoReadType type = WhoReadType::Seen;
	bool unknown = false;
};

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhoReadContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Fn<void(uint64)> participantChosen);

} // namespace Ui
