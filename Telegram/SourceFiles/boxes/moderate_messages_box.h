/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"

class PeerData;

namespace Data {
class SavedSublist;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

extern const char kModerateCommonGroups[];

struct ModerateMessagesBoxOptions final {
	bool reportSpam = false;
	bool deleteAll = false;
	bool banUser = false;
};

struct ModerateReactionEntry {
	not_null<PeerData*> peer;
	MsgId msgId;
	not_null<PeerData*> participant;
	Data::ReactionId reaction;
};

struct ModerateMessagesBoxEntry {
	HistoryItemsList items;
	std::optional<ModerateReactionEntry> reaction;
};

[[nodiscard]] ModerateMessagesBoxOptions DefaultModerateMessagesBoxOptions();

void CreateModerateMessagesBox(
	not_null<Ui::GenericBox*> box,
	ModerateMessagesBoxEntry entry,
	Fn<void()> confirmed,
	ModerateMessagesBoxOptions options);

[[nodiscard]] bool CanCreateModerateMessagesBox(const HistoryItemsList &);

void DeleteChatBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);
void DeleteSublistBox(
	not_null<Ui::GenericBox*> box,
	not_null<Data::SavedSublist*> sublist);
