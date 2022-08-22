/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
struct Reaction;
struct AllowedReactions;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

void EditAllowedReactionsBox(
	not_null<Ui::GenericBox*> box,
	bool isGroup,
	const std::vector<Data::Reaction> &list,
	const Data::AllowedReactions &allowed,
	Fn<void(const std::vector<QString> &, bool all)> callback);

void SaveAllowedReactions(
	not_null<PeerData*> peer,
	const std::vector<QString> &allowed,
	bool all);
