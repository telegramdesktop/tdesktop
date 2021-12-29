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
	QString reaction;
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
	Reacted,
};

struct WhoReadContent {
	std::vector<WhoReadParticipant> participants;
	WhoReadType type = WhoReadType::Seen;
	QString mostPopularReaction;
	int fullReactionsCount = 0;
	bool unknown = false;
};

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhoReactedContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Fn<void(uint64)> participantChosen,
	Fn<void()> showAllChosen);

} // namespace Ui
