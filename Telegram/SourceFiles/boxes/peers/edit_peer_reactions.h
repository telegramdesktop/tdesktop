/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class PeerData;

namespace Data {
struct Reaction;
} // namespace Data

void EditAllowedReactionsBox(
	not_null<Ui::GenericBox*> box,
	bool isGroup,
	const std::vector<Data::Reaction> &list,
	const std::vector<Data::Reaction> &selected,
	Fn<void(const std::vector<QString> &)> callback);

void SaveAllowedReactions(
	not_null<PeerData*> peer,
	const std::vector<QString> &allowed);
