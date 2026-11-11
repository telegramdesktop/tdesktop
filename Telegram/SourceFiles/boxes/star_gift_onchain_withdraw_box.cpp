/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_withdraw_box.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_session.h"
#include "history/history_gift_onchain_offer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"

#include "styles/style_layers.h"

namespace Ui {
namespace {

using Wallet::SendError;

enum class Stage : uchar {
	Confirming,
	Preparing,
	Authorizing,
	Waiting,
	Sending,
	Closed,
};

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
			tr::bold(PrepareGramAmountText(fine)),
			tr::marked)).append(u"\n\n"_q);
	}
	result.append(tr::lng_gift_onchain_offer_withdraw_rest(
		tr::now,
		lt_amount,
		tr::bold(PrepareGramAmountText(amount)),
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
	void prepare();
	void prepared(Wallet::FeeResult result);
	void authorize();
	void authorized(Wallet::KeyAuthorization auth);
	void awaitWallet();
	void waited();
	void submit();
	void sent(Wallet::ServedResult result);
	void refused(SendError error);
	void stopAttempt();
	void openMoney();
	void keyChanged();
	void closeWithToast(const QString &text);
	void close();
	void closing();

	const not_null<GenericBox*> _box;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	const FullMsgId _itemId;
	const Wallet::ServedOperation _operation;
	const TimeId _expiresAt = 0;
	base::Timer _waitTimer;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	Stage _stage = Stage::Confirming;
	uint64 _owner = 0;
	int _retries = 0;
	bool _submitted = false;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

OnchainOfferWithdrawal::OnchainOfferWithdrawal(
	not_null<GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	FullMsgId itemId,
	Wallet::ServedOperation operation,
	TimeId expiresAt)
: _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _itemId(itemId)
, _operation(std::move(operation))
, _expiresAt(expiresAt)
, _waitTimer([=] { waited(); }) {
	SetWithdrawing(_session, _itemId, true);
}

OnchainOfferWithdrawal::~OnchainOfferWithdrawal() {
	closing();
}

void OnchainOfferWithdrawal::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	rpl::merge(
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->custodyUpdates(),
		_wallet->transferWalletIdentityChanges(),
		_wallet->signingReadyValue() | rpl::skip(1) | rpl::to_empty
	) | rpl::filter([=] {
		return (_stage == Stage::Waiting);
	}) | rpl::on_next([=] {
		prepare();
	}, _lifetime);
}

void OnchainOfferWithdrawal::press() {
	if (_stage != Stage::Confirming) {
		return;
	}
	_retries = 0;
	prepare();
}

bool OnchainOfferWithdrawal::current() const {
	return (_stage != Stage::Closed)
		&& _weakSession
		&& _show->valid()
		&& (&_show->session() == _session);
}

bool OnchainOfferWithdrawal::offerUsable() {
	const auto now = base::unixtime::now();
	const auto item = current() ? _session->data().message(_itemId) : nullptr;
	if (!item || !CanWithdrawGiftOnchainOffer(item, now)) {
		close();
		return false;
	} else if ((_expiresAt > 0) && (now >= _expiresAt)) {
		LOG(("Wallet Error: the prepared on-chain offer withdrawal "
			"has expired."));
		closeWithToast(tr::lng_gift_onchain_offer_withdraw_error(tr::now));
		return false;
	}
	return true;
}

void OnchainOfferWithdrawal::prepare() {
	if (!offerUsable()) {
		return;
	} else if (_authorization.valid() && !_wallet->signingReady()) {
		awaitWallet();
		return;
	}
	_waitTimer.cancel();
	_stage = Stage::Preparing;
	_prepared = nullptr;
	_wallet->estimateServed(
		_owner,
		_operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			prepared(std::move(result));
		}));
}

void OnchainOfferWithdrawal::prepared(Wallet::FeeResult result) {
	if (_stage != Stage::Preparing) {
		return;
	}
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (error != SendError::None) {
		refused(error);
		return;
	} else if (!current()) {
		close();
		return;
	}
	_prepared = std::move(result.prepared);
	if (_authorization.valid()) {
		submit();
	} else {
		authorize();
	}
}

void OnchainOfferWithdrawal::authorize() {
	_keyLifetime.destroy();
	_stage = Stage::Authorizing;
	const auto weak = base::make_weak(this);
	Wallet::AcquireWalletKey(
		_show,
		[=] {
			return weak
				&& (weak->_stage == Stage::Authorizing)
				&& weak->current();
		},
		_keyLifetime,
		crl::guard(this, [=](Wallet::KeyAuthorization auth) {
			authorized(std::move(auth));
		}));
}

void OnchainOfferWithdrawal::authorized(Wallet::KeyAuthorization auth) {
	if (_stage != Stage::Authorizing) {
		return;
	} else if (!auth.valid()) {
		stopAttempt();
		return;
	}
	_authorization = std::move(auth);
	prepare();
}

void OnchainOfferWithdrawal::awaitWallet() {
	_stage = Stage::Waiting;
	if (!_waitTimer.isActive()) {
		_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
	}
	_wallet->refreshState();
}

void OnchainOfferWithdrawal::waited() {
	if (_stage == Stage::Waiting) {
		openMoney();
	}
}

void OnchainOfferWithdrawal::submit() {
	if (!offerUsable()) {
		return;
	}
	_stage = Stage::Sending;
	_submitted = true;
	_wallet->sendServed(
		_authorization,
		base::take(_prepared),
		crl::guard(this, [=](Wallet::ServedResult result) {
			if (_stage == Stage::Sending) {
				sent(std::move(result));
			}
		}));
}

void OnchainOfferWithdrawal::sent(Wallet::ServedResult result) {
	using Wallet::ServedOutcome;
	switch (result.outcome) {
	case ServedOutcome::Accepted:
		close();
		return;
	case ServedOutcome::Uncertain:
		LOG(("Wallet Info: the on-chain offer withdrawal is pending."));
		close();
		return;
	case ServedOutcome::Duplicate:
		LOG(("Wallet Error: the on-chain offer withdrawal was already sent."));
		close();
		return;
	case ServedOutcome::Rejected:
		_submitted = false;
		if (result.refusal == SendError::KeyChanged) {
			keyChanged();
		} else if (result.refusal == SendError::Silent) {
			close();
		} else {
			LOG(("API Error: wallet.sendTransfer for the on-chain offer "
				"withdrawal failed with %1.").arg(result.errorType));
			closeWithToast(
				tr::lng_gift_onchain_offer_withdraw_error(tr::now));
		}
		return;
	case ServedOutcome::Refused:
		_submitted = false;
		refused(result.refusal);
		return;
	}
}

void OnchainOfferWithdrawal::refused(SendError error) {
	if (error == SendError::KeyChanged) {
		keyChanged();
	} else if (error == SendError::NoWallet
		|| error == SendError::SigningUnavailable) {
		openMoney();
	} else if (error == SendError::WalletNotReady) {
		if (++_retries <= Wallet::kSendRefusalRetries) {
			awaitWallet();
		} else {
			openMoney();
		}
	} else if (error == SendError::QuoteExpired
		&& ++_retries <= Wallet::kSendRefusalRetries) {
		prepare();
	} else if (error == SendError::Silent) {
		close();
	} else if (error == SendError::InsufficientBalance
		|| error == SendError::InsufficientFees) {
		closeWithToast(Wallet::SendErrorText(error, 0));
	} else {
		closeWithToast(tr::lng_gift_onchain_offer_withdraw_error(tr::now));
	}
}

void OnchainOfferWithdrawal::stopAttempt() {
	_waitTimer.cancel();
	_prepared = nullptr;
	_authorization = {};
	_stage = Stage::Confirming;
}

void OnchainOfferWithdrawal::openMoney() {
	const auto session = _session;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWallet(session);
	}
}

void OnchainOfferWithdrawal::keyChanged() {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWalletKeyChanged(show);
	}
}

void OnchainOfferWithdrawal::closeWithToast(const QString &text) {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		show->showToast(text);
	}
}

void OnchainOfferWithdrawal::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainOfferWithdrawal::closing() {
	if (_stage == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	_waitTimer.cancel();
	if (_weakSession) {
		_wallet->cancelFeeEstimate(_owner);
		if (!_submitted) {
			SetWithdrawing(_session, _itemId, false);
		}
	}
	_prepared = nullptr;
	_authorization = {};
	_keyLifetime.destroy();
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
			rpl::single(PrepareGramAmountText(fine)))
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
