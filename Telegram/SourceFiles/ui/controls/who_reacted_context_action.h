/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/text/text_block.h"

namespace Ui {
namespace Menu {
class ItemBase;
} // namespace Menu

class PopupMenu;

struct WhoReadParticipant {
	QString name;
	QString customEntityData;
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
	QString singleCustomEntityData;
	int fullReactionsCount = 0;
	int fullReadCount = 0;
	bool unknown = false;
};

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhoReactedContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Text::CustomEmojiFactory factory,
	Fn<void(uint64)> participantChosen,
	Fn<void()> showAllChosen);

class WhoReactedListMenu final {
public:
	WhoReactedListMenu(
		Text::CustomEmojiFactory factory,
		Fn<void(uint64)> participantChosen,
		Fn<void()> showAllChosen);

	void clear();
	void populate(
		not_null<PopupMenu*> menu,
		const WhoReadContent &content,
		Fn<void()> refillTopActions = nullptr,
		int addedToBottom = 0,
		Fn<void()> appendBottomActions = nullptr);

private:
	class EntryAction;

	const Text::CustomEmojiFactory _customEmojiFactory;
	const Fn<void(uint64)> _participantChosen;
	const Fn<void()> _showAllChosen;

	std::vector<not_null<EntryAction*>> _actions;

};

} // namespace Ui
