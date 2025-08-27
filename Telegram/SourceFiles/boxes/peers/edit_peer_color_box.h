/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct SettingsButton;
} // namespace style

namespace st {
extern const style::SettingsButton &peerAppearanceButton;
} // namespace st

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
class GenericBox;
class ChatStyle;
class ChatTheme;
class VerticalLayout;
struct AskBoostReason;
class RpWidget;
class SettingsButton;
} // namespace Ui

void AddLevelBadge(
	int level,
	not_null<Ui::SettingsButton*> button,
	Ui::RpWidget *right,
	not_null<ChannelData*> channel,
	const QMargins &padding,
	rpl::producer<QString> text);

void EditPeerColorBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::ChatStyle> style = nullptr,
	std::shared_ptr<Ui::ChatTheme> theme = nullptr);

void AddPeerColorButton(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	const style::SettingsButton &st);

void CheckBoostLevel(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	Fn<std::optional<Ui::AskBoostReason>(int level)> askMore,
	Fn<void()> cancel);

struct ButtonWithEmoji {
	not_null<const style::SettingsButton*> st;
	int emojiWidth = 0;
	int noneWidth = 0;
	int added = 0;
};
[[nodiscard]] ButtonWithEmoji ButtonStyleWithRightEmoji(
	not_null<Ui::RpWidget*> parent,
	const QString &noneString,
	const style::SettingsButton &parentSt = st::peerAppearanceButton);
