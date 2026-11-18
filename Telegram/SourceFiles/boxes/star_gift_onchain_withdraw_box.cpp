/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_withdraw_box.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/star_gift_onchain_signing.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/history_gift_onchain_offer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"

#include "styles/style_layers.h"

namespace Ui {
namespace {

using Wallet::SendError;

struct WithdrawalTerms {
	Wallet::ServedOperation operation;
	int64 fine = 0;
	int64 amount = 0;
	TimeId expiresAt = 0;
};

void SetWithdrawing(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		bool withdrawing) {
	if (const auto item = session->data().message(itemId)) {
		if (const auto offer = item->Get<HistoryServiceGiftOnchainOffer>()) {
			offer->withdrawing = withdrawing;
		}
	}
}

void ShowWithdrawError(const std::shared_ptr<ChatHelpers::Show> &show) {
	if (show->valid()) {
		show->showToast(tr::lng_gift_onchain_offer_withdraw_error(tr::now));
	}
}

[[nodiscard]] TextWithEntities WithdrawText(int64 fine, int64 amount) {
	auto result = tr::marked();
	if (fine > 0) {
		result.append(tr::lng_gift_onchain_offer_withdraw_fine(
			tr::now,
			lt_amount,
			tr::bold(Data::FormatGiftGramAmountWithUnit(fine)),
			tr::marked)).append(u"\n\n"_q);
	}
	result.append(tr::lng_gift_onchain_offer_withdraw_rest(
		tr::now,
		lt_amount,
		tr::bold(Data::FormatGiftGramAmountWithUnit(amount)),
		tr::marked));
	return result;
}

class OnchainOfferWithdrawal final : public base::has_weak_ptr {
public:
	OnchainOfferWithdrawal(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		FullMsgId itemId,
		Wallet::ServedOperation operation,
		TimeId expiresAt);
	~OnchainOfferWithdrawal();

	void start();
	void press();

private:
	[[nodiscard]] bool current() const;
	[[nodiscard]] bool offerUsable();
	void refused(SendError error);
	void duplicate();
	void uncertain();
	void release();

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const FullMsgId _itemId;
	const Wallet::ServedOperation _operation;
	const TimeId _expiresAt = 0;
	OnchainSigning _signing;

};

OnchainOfferWithdrawal::OnchainOfferWithdrawal(
	not_null<GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	FullMsgId itemId,
	Wallet::ServedOperation operation,
	TimeId expiresAt)
: _show(std::move(show))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _itemId(itemId)
, _operation(std::move(operation))
, _expiresAt(expiresAt)
, _signing({
	.box = box,
	.show = _show,
	.order = OnchainSigningOrder::SamePress,
	.route = OnchainSigningRoute::Money,
	.wait = OnchainSigningWait::Estimate,
	.current = [=] { return current(); },
	.usable = [=](OnchainSigningCheck) { return offerUsable(); },
	.error = tr::lng_gift_onchain_offer_withdraw_error,
	.transfer = u"the on-chain offer withdrawal"_q,
	.refused = [=](SendError error) { refused(error); },
	.duplicate = [=] { duplicate(); },
	.uncertain = [=] { uncertain(); },
	.closing = [=] { release(); },
}) {
	SetWithdrawing(_session, _itemId, true);
}

OnchainOfferWithdrawal::~OnchainOfferWithdrawal() {
	_signing.closing();
}

void OnchainOfferWithdrawal::start() {
	_signing.start();
}

void OnchainOfferWithdrawal::press() {
	_signing.sign(_operation);
}

bool OnchainOfferWithdrawal::current() const {
	return _weakSession
		&& _show->valid()
		&& (&_show->session() == _session);
}

bool OnchainOfferWithdrawal::offerUsable() {
	const auto now = base::unixtime::now();
	const auto item = current() ? _session->data().message(_itemId) : nullptr;
	if (!item || !CanWithdrawGiftOnchainOffer(item, now)) {
		_signing.close();
		return false;
	} else if ((_expiresAt > 0) && (now >= _expiresAt)) {
		LOG(("Wallet Error: the prepared on-chain offer withdrawal "
			"has expired."));
		_signing.closeWithToast(
			tr::lng_gift_onchain_offer_withdraw_error(tr::now));
		return false;
	}
	return true;
}

void OnchainOfferWithdrawal::refused(SendError error) {
	if (error == SendError::InsufficientBalance
		|| error == SendError::InsufficientFees) {
		_signing.closeWithToast(Wallet::SendErrorText(error, 0));
	} else {
		_signing.closeWithToast(
			tr::lng_gift_onchain_offer_withdraw_error(tr::now));
	}
}

void OnchainOfferWithdrawal::duplicate() {
	LOG(("Wallet Error: the on-chain offer withdrawal was already sent."));
	_signing.close();
}

void OnchainOfferWithdrawal::uncertain() {
	LOG(("Wallet Info: the on-chain offer withdrawal is pending."));
	_signing.close();
}

void OnchainOfferWithdrawal::release() {
	if (_weakSession && !_signing.submitted()) {
		SetWithdrawing(_session, _itemId, false);
	}
}

void WithdrawOfferBox(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		FullMsgId itemId,
		WithdrawalTerms terms) {
	const auto fine = terms.fine;
	const auto amount = terms.amount;
	const auto withdrawal = box->lifetime().make_state<OnchainOfferWithdrawal>(
		box,
		show,
		itemId,
		std::move(terms.operation),
		terms.expiresAt);
	auto confirmText = (fine > 0)
		? tr::lng_gift_onchain_offer_withdraw_for(
			lt_amount,
			rpl::single(Data::FormatGiftGramAmountWithUnit(fine)))
		: tr::lng_gift_onchain_offer_withdraw();
	box->setWidth(st::boxWideWidth);
	ConfirmBox(box, {
		.text = WithdrawText(fine, amount),
		.confirmed = Fn<void()>(crl::guard(withdrawal, [=] {
			withdrawal->press();
		})),
		.confirmText = std::move(confirmText),
		.title = tr::lng_gift_onchain_offer_withdraw_title(),
	});
	withdrawal->start();
}

void ShowWithdrawal(
		not_null<Main::Session*> session,
		std::shared_ptr<ChatHelpers::Show> show,
		FullMsgId itemId,
		const MTPpayments_StarGiftOnchainOfferTx &result) {
	const auto item = session->data().message(itemId);
	const auto offer = item
		? item->Get<HistoryServiceGiftOnchainOffer>()
		: nullptr;
	const auto &data = result.data();
	if (!show->valid()
		|| (&show->session() != session)
		|| !offer
		|| !CanWithdrawGiftOnchainOffer(item, base::unixtime::now())) {
		SetWithdrawing(session, itemId, false);
		return;
	} else if (data.voffer_id().v != offer->offerId) {
		LOG(("API Error: payments.cancelStarGiftOnchainOffer answered "
			"offer %1 for offer %2."
			).arg(data.voffer_id().v
			).arg(offer->offerId));
		SetWithdrawing(session, itemId, false);
		ShowWithdrawError(show);
		return;
	}
	const auto fine = data.vfine() ? int64(data.vfine()->v) : int64();
	const auto amount = offer->amount;
	SetWithdrawing(session, itemId, false);
	show->show(Box(
		WithdrawOfferBox,
		show,
		itemId,
		WithdrawalTerms{
			.operation = Wallet::ServedOperationFromOffer(result),
			.fine = fine,
			.amount = amount,
			.expiresAt = data.vexpires_at().v,
		}));
}

void WithdrawalFailed(
		not_null<Main::Session*> session,
		std::shared_ptr<ChatHelpers::Show> show,
		FullMsgId itemId,
		const MTP::Error &error) {
	SetWithdrawing(session, itemId, false);
	if (MTP::IgnoreError(error)) {
		return;
	}
	LOG(("API Error: payments.cancelStarGiftOnchainOffer failed with %1."
		).arg(error.type()));
	ShowWithdrawError(show);
}

} // namespace

void WithdrawOnchainGiftOffer(
		std::shared_ptr<ChatHelpers::Show> show,
		FullMsgId itemId) {
	if (!show || !show->valid()) {
		return;
	}
	const auto session = &show->session();
	const auto item = session->data().message(itemId);
	const auto offer = item
		? item->Get<HistoryServiceGiftOnchainOffer>()
		: nullptr;
	if (!offer
		|| offer->withdrawing
		|| !CanWithdrawGiftOnchainOffer(item, base::unixtime::now())) {
		return;
	}
	offer->withdrawing = true;
	session->api().request(MTPpayments_CancelStarGiftOnchainOffer(
		MTP_int(item->id.bare)
	)).done(crl::guard(session, [=](
			const MTPpayments_StarGiftOnchainOfferTx &result) {
		ShowWithdrawal(session, show, itemId, result);
	})).fail(crl::guard(session, [=](const MTP::Error &error) {
		WithdrawalFailed(session, show, itemId, error);
	})).handleFloodErrors().send();
}

} // namespace Ui
