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

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class GenericBox;

struct OnchainGiftOfferRequest {
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Data::UniqueGift> gift;
	base::weak_qptr<GenericBox> form;
	int64 amount = 0;
	int limitedCount = 0;
	int limitedLeft = 0;
};

[[nodiscard]] rpl::producer<TextWithEntities> OnchainOfferInsufficientText();
void ShowOnchainOfferAddFunds(not_null<Main::Session*> session);

base::weak_qptr<GenericBox> ShowOnchainGiftOfferConfirmBox(
	OnchainGiftOfferRequest request,
	const MTPpayments_StarGiftOnchainOfferTx &answer);

} // namespace Ui
