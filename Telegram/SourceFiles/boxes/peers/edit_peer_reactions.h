/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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

namespace Window {
class SessionNavigation;
} // namespace Window

void EditAllowedReactionsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	bool isGroup,
	const std::vector<Data::Reaction> &list,
	const Data::AllowedReactions &allowed,
	Fn<void(const Data::AllowedReactions &)> callback);

void SaveAllowedReactions(
	not_null<PeerData*> peer,
	const Data::AllowedReactions &allowed);
