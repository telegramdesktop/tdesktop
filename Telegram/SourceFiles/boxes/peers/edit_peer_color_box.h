/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class GenericBox;
class ChatStyle;
class ChatTheme;
class VerticalLayout;
struct AskBoostReason;
} // namespace Ui

void EditPeerColorBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::ChatStyle> style = nullptr,
	std::shared_ptr<Ui::ChatTheme> theme = nullptr);

void AddPeerColorButton(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer);

void CheckBoostLevel(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	Fn<std::optional<Ui::AskBoostReason>(int level)> askMore,
	Fn<void()> cancel);
