/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

struct HistoryMessageSuggestion;
class Painter;

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

namespace MTP {
class Error;
} // namespace MTP

namespace Ui {
class BoxContent;
class ChatTheme;
class GenericBox;
class Show;
} // namespace Ui

namespace Ui::Toast {
struct Config;
} // namespace Ui::Toast

void ShowTransferToBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId savedId,
	Fn<void()> closeParentBox);

void ShowTransferGiftBox(
	not_null<Window::SessionController*> window,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId savedId,
	bool shownOnProfile);

void ShowGiftSaleAcceptBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<HistoryMessageSuggestion*> suggestion);
void ShowGiftSaleRejectBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	not_null<HistoryMessageSuggestion*> suggestion);

struct ResaleGiftMessage {
	TextWithEntities text;
	bool hideName = false;
};
void ShowResaleGiftComposer(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	not_null<PeerData*> to,
	TextWithEntities cost,
	Fn<base::weak_qptr<Ui::GenericBox>(
		not_null<Ui::GenericBox*> composer,
		ResaleGiftMessage message)> confirm);
[[nodiscard]] bool CanBuyResaleGiftByInvoice(const Data::UniqueGift &gift);
void ShowBuyResaleGiftBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	bool forceTon,
	not_null<PeerData*> to,
	Fn<void(bool ok)> closeParentBox);

[[nodiscard]] object_ptr<Ui::GenericBox> MakeResaleGiftPriceChangeBox(
	CreditsAmount price,
	Fn<void()> confirmed,
	Fn<void()> cancelled);

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

void PaintGiftMoveUserpic(
	Painter &p,
	int x,
	int y,
	int outerWidth,
	int size,
	const style::icon &icon);
void ShowToastWithAction(
	std::shared_ptr<Ui::Show> show,
	Ui::Toast::Config &&config,
	rpl::producer<QString> action,
	Fn<void()> callback);
void ShowGiftMovedToast(
	std::shared_ptr<Ui::Show> show,
	not_null<DocumentData*> sticker,
	TextWithEntities text,
	Fn<void()> view);
[[nodiscard]] TextWithEntities WalletCollectiblesPath();

struct GiftPasswordStep {
	Fn<void(
		const QByteArray &tmpPassword,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail)> send;
	Fn<void()> boxShown;
	Fn<void()> succeeded;
	Fn<void(const MTP::Error &)> failed;
	Fn<void()> dismissed;
	bool closeOnFail = false;
};
void RequestWithGiftPassword(
	not_null<Window::SessionController*> window,
	not_null<Ui::BoxContent*> confirmation,
	GiftPasswordStep step);
