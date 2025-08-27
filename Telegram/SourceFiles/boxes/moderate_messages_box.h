/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
class SavedSublist;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

void CreateModerateMessagesBox(
	not_null<Ui::GenericBox*> box,
	const HistoryItemsList &items,
	Fn<void()> confirmed);

[[nodiscard]] bool CanCreateModerateMessagesBox(const HistoryItemsList &);

void DeleteChatBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);
void DeleteSublistBox(
	not_null<Ui::GenericBox*> box,
	not_null<Data::SavedSublist*> sublist);
