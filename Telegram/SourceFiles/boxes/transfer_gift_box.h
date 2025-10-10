/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
class SavedStarGiftId;
} // namespace Data

namespace Ui {
class ChatTheme;
} // namespace Ui

void ShowTransferToBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId savedId,
	Fn<void()> closeParentBox);

void ShowTransferGiftBox(
	not_null<Window::SessionController*> window,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId savedId);

void ShowBuyResaleGiftBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	bool forceTon,
	not_null<PeerData*> to,
	Fn<void(bool ok)> closeParentBox);

bool ShowResaleGiftLater(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift);
bool ShowTransferGiftLater(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift);

void SetThemeFromUniqueGift(
	not_null<Window::SessionController*> window,
	std::shared_ptr<Data::UniqueGift> unique);
void SendPeerThemeChangeRequest(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &token,
	const std::shared_ptr<Data::UniqueGift> &unique,
	bool locallySet = false);
void SetPeerTheme(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &token,
	const std::shared_ptr<Ui::ChatTheme> &theme);

void ShowActionLocked(
	std::shared_ptr<ChatHelpers::Show> show,
	const QString &slug);
