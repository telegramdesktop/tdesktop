/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_offer_confirm_box.h"

#include "base/timer.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/gift_onchain_offer_box.h"
#include "boxes/star_gift_onchain_sale_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_star_gift.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_address_label.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"

#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

using Wallet::SendError;

constexpr auto kBasisPointsInOne = 10000;
constexpr auto kOfferFeeBasisPoints = 200;
constexpr auto kWalletRetryDelay = crl::time(1000);

enum class Stage : uchar {
	Estimating,
	Ready,
	Confirming,
	Waiting,
	Authorizing,
	Signing,
	Sending,
	Tracking,
	Closed,
};

struct OfferTerms {
	OnchainGiftOfferRequest request;
	Wallet::ServedOperation operation;
	TimeId expiresAt = 0;
};

struct OfferCompletion {
	std::shared_ptr<ChatHelpers::Show> show;
	base::weak_qptr<GenericBox> form;
	base::weak_ptr<Main::Session> session;
	int64 value = 0;
	TimeId expiresAt = 0;
};

class OnchainOfferDeployment final : public base::has_weak_ptr {
public:
	OnchainOfferDeployment(not_null<GenericBox*> box, OfferTerms terms);
	~OnchainOfferDeployment();

	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;
	[[nodiscard]] rpl::producer<bool> insufficientValue() const;
	[[nodiscard]] rpl::producer<bool> refusedValue() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;

	void start();
	void press();

private:
	[[nodiscard]] bool current() const;
	[[nodiscard]] bool ownGift() const;
	[[nodiscard]] bool expired() const;
	[[nodiscard]] bool usable();
	[[nodiscard]] bool walletReady() const;
	[[nodiscard]] OfferCompletion completion() const;
	void reestimate();
	void estimate();
	void estimated(Wallet::FeeResult result);
	[[nodiscard]] bool retry();
	void refused(SendError error);
	void abandon(SendError error);
	void ready();
	void awaitWallet();
	void resume();
	void waited();
	void stopWaiting();
	void authorize();
	void authorized(Wallet::KeyAuthorization auth);
	void submit();
	void sent(Wallet::ServedResult result);
	void accepted();
	void duplicate();
	void track(const std::string &operationId);
	void resolve();
	void keyChanged();
	void closeWithToast(const QString &text);
	void close();
	void closing();

	const not_null<GenericBox*> _box;
	const OfferTerms _terms;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	base::Timer _waitTimer;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	std::string _operationId;
	rpl::variable<Stage> _stage = Stage::Estimating;
	rpl::variable<std::optional<int64>> _fee;
	rpl::variable<SendError> _error = SendError::None;
	uint64 _owner = 0;
	int _retries = 0;
	Stage _resume = Stage::Estimating;
	bool _waitArmed = false;
	bool _polling = false;
	rpl::lifetime _waitLifetime;
	rpl::lifetime _trackLifetime;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

[[nodiscard]] bool Busy(Stage stage) {
	return (stage != Stage::Ready);
}

[[nodiscard]] bool Insufficient(SendError error) {
	return (error == SendError::InsufficientBalance)
		|| (error == SendError::InsufficientFees);
}

[[nodiscard]] QString OwnerText(const Data::UniqueGift &gift) {
	if (!gift.ownerName.isEmpty()) {
		return gift.ownerName;
	} else if (Wallet::ParseAddress(gift.ownerAddress)) {
		return Wallet::FormatAddressGroups(gift.ownerAddress);
	}
	return gift.ownerAddress;
}

[[nodiscard]] rpl::producer<TextWithEntities> QuestionText(
		const Data::UniqueGift &gift) {
	return tr::lng_gift_offer_onchain_confirm_text(
		lt_user,
		rpl::single(tr::bold(OwnerText(gift))),
		lt_name,
		rpl::single(tr::bold(Data::UniqueGiftName(gift))),
		tr::marked);
}

void AddTermsTable(not_null<GenericBox*> box, const OfferTerms &terms) {
	const auto &st = st::giveawayGiftMessage;
	const auto amount = terms.request.amount;
	const auto value = terms.operation.message.value;
	const auto fee = amount * kOfferFeeBasisPoints / kBasisPointsInOne;
	const auto table = box->addRow(
		object_ptr<TableLayout>(box, st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	auto helper = Text::CustomEmojiHelper();
	const auto mark = Wallet::GramMark(helper, st.style.font);
	const auto context = helper.context();
	const auto label = [&](TextWithEntities text) {
		return object_ptr<FlatLabel>(
			table,
			rpl::single(std::move(text)),
			st,
			st::defaultPopupMenu,
			context);
	};
	const auto gram = [&](int64 nanos) {
		auto text = mark;
		text.append(QChar(' ')).append(FormatTonAmount(nanos).full);
		return label(std::move(text));
	};
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_your_offer(),
		gram(amount - fee));
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_platform_fee(),
		MakeMultilineValueWithSmallButton(
			table,
			gram(fee).release(),
			rpl::single(QString::number(kOfferFeeBasisPoints / 100.) + '%')
		).widget);
	if (value > amount) {
		AddTableRow(
			table,
			tr::lng_gift_offer_onchain_escrow_fee(),
			gram(value - amount));
	}
	AddTableRow(table, tr::lng_gift_offer_onchain_you_pay(), gram(value));
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_you_receive(),
		label(tr::marked(Data::UniqueGiftName(*terms.request.gift))));
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_expires(),
		label(tr::marked(langDateTimeFull(
			base::unixtime::parse(terms.expiresAt)))));
}

[[nodiscard]] not_null<SlideWrap<FlatLabel>*> AddErrorLine(
		not_null<GenericBox*> box,
		rpl::producer<TextWithEntities> text) {
	const auto wrap = box->addRow(
		object_ptr<SlideWrap<FlatLabel>>(
			box,
			object_ptr<FlatLabel>(
				box,
				std::move(text),
				st::walletSendUserErrorLabel),
			st::walletConnectCaptionMargin),
		style::margins(),
		style::al_justify);
	wrap->entity()->setTryMakeSimilarLines(true);
	return wrap;
}

void FinishOffer(const OfferCompletion &completion, Toast::Config &&toast) {
	if (const auto form = completion.form.get()) {
		form->closeBox();
	}
	const auto session = completion.session.get();
	const auto &show = completion.show;
	if (session && show->valid() && (&show->session() == session)) {
		show->showToast(std::move(toast));
	}
}

void CompleteOffer(const OfferCompletion &completion) {
	FinishOffer(completion, {
		.title = tr::lng_gift_offer_onchain_sent_title(tr::now),
		.text = tr::lng_gift_offer_onchain_sent_text(
			tr::now,
			lt_amount,
			tr::bold(PrepareGramAmountText(completion.value)),
			lt_date,
			tr::marked(langDateTime(
				base::unixtime::parse(completion.expiresAt))),
			tr::marked),
	});
}

OnchainOfferDeployment::OnchainOfferDeployment(
	not_null<GenericBox*> box,
	OfferTerms terms)
: _box(box)
, _terms(std::move(terms))
, _session(&_terms.request.show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _waitTimer([=] { waited(); }) {
}

OnchainOfferDeployment::~OnchainOfferDeployment() {
	closing();
}

rpl::producer<std::optional<int64>> OnchainOfferDeployment::feeValue() const {
	return _fee.value();
}

rpl::producer<bool> OnchainOfferDeployment::insufficientValue() const {
	return _error.value() | rpl::map(Insufficient);
}

rpl::producer<bool> OnchainOfferDeployment::refusedValue() const {
	return _error.value() | rpl::map([](SendError error) {
		return (error != SendError::None) && !Insufficient(error);
	});
}

rpl::producer<bool> OnchainOfferDeployment::busyValue() const {
	return _stage.value(
	) | rpl::filter(
		rpl::mappers::_1 != Stage::Closed
	) | rpl::map(Busy) | rpl::distinct_until_changed();
}

void OnchainOfferDeployment::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_polling = true;
	_wallet->startPolling();
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	estimate();
}

void OnchainOfferDeployment::press() {
	if (Busy(_stage.current())) {
		return;
	}
	_retries = 0;
	_error = SendError::None;
	reestimate();
}

bool OnchainOfferDeployment::current() const {
	const auto &show = _terms.request.show;
	return (_stage.current() != Stage::Closed)
		&& _weakSession
		&& show->valid()
		&& (&show->session() == _session)
		&& _terms.request.form;
}

bool OnchainOfferDeployment::ownGift() const {
	return (LookupOwnOnchainGift(_session, *_terms.request.gift)
		!= OwnOnchainGift::None);
}

bool OnchainOfferDeployment::expired() const {
	return (base::unixtime::now() >= _terms.expiresAt);
}

bool OnchainOfferDeployment::usable() {
	const auto &slug = _terms.request.gift->slug;
	if (!current()) {
		close();
		return false;
	} else if (ownGift()) {
		LOG(("Wallet Error: the wallet holds %1, so it gets no offer."
			).arg(slug));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	} else if (expired()) {
		LOG(("Wallet Error: the on-chain offer for %1 expired at %2."
			).arg(slug
			).arg(_terms.expiresAt));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	}
	return true;
}

bool OnchainOfferDeployment::walletReady() const {
	using Wallet::DeviceMode;
	using Wallet::Presence;
	if (_wallet->presenceCurrent() != Presence::Ready
		|| !_wallet->transferWalletIdentity()) {
		return false;
	} else if (_resume != Stage::Signing) {
		return true;
	}
	return (_wallet->deviceCustodyState().mode == DeviceMode::Full)
		&& _wallet->signingReady();
}

OfferCompletion OnchainOfferDeployment::completion() const {
	return {
		.show = _terms.request.show,
		.form = _terms.request.form,
		.session = _weakSession,
		.value = _terms.operation.message.value,
		.expiresAt = _terms.expiresAt,
	};
}

void OnchainOfferDeployment::reestimate() {
	_stage = _fee.current().has_value()
		? Stage::Confirming
		: Stage::Estimating;
	estimate();
}

void OnchainOfferDeployment::estimate() {
	if (!usable()) {
		return;
	}
	_prepared = nullptr;
	_wallet->estimateServed(
		_owner,
		_terms.operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			estimated(std::move(result));
		}));
}

void OnchainOfferDeployment::estimated(Wallet::FeeResult result) {
	const auto stage = _stage.current();
	if (stage != Stage::Estimating && stage != Stage::Confirming) {
		return;
	} else if (!current()) {
		close();
		return;
	}
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (error != SendError::None) {
		refused(error);
	} else if (stage == Stage::Estimating
		|| _fee.current() != result.feeNano) {
		_error = SendError::None;
		_fee = result.feeNano;
		ready();
	} else {
		_prepared = std::move(result.prepared);
		if (_authorization.valid()) {
			_stage = Stage::Signing;
			submit();
		} else {
			authorize();
		}
	}
}

bool OnchainOfferDeployment::retry() {
	if (++_retries > Wallet::kSendRefusalRetries) {
		return false;
	}
	reestimate();
	return true;
}

void OnchainOfferDeployment::refused(SendError error) {
	if (error == SendError::QuoteExpired && retry()) {
		return;
	} else if (error == SendError::WalletNotReady
		&& (_waitArmed || ++_retries <= Wallet::kSendRefusalRetries)) {
		awaitWallet();
	} else if (error == SendError::KeyChanged) {
		keyChanged();
	} else if (error == SendError::Silent) {
		close();
	} else {
		abandon(error);
	}
}

void OnchainOfferDeployment::abandon(SendError error) {
	if (!Insufficient(error)) {
		LOG(("Wallet Error: the on-chain gift offer was refused with %1."
			).arg(int(error)));
	}
	_fee = std::nullopt;
	_error = error;
	ready();
}

void OnchainOfferDeployment::ready() {
	stopWaiting();
	_prepared = nullptr;
	_authorization = {};
	_stage = Stage::Ready;
}

void OnchainOfferDeployment::awaitWallet() {
	const auto stage = _stage.current();
	if (stage != Stage::Waiting) {
		_resume = stage;
		_stage = Stage::Waiting;
	}
	if (!_waitArmed) {
		_waitArmed = true;
		_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
		_wallet->refreshState();
	} else if (!_waitTimer.isActive()) {
		waited();
		return;
	}
	_waitLifetime.destroy();
	rpl::merge(
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->transferWalletIdentityChanges(),
		_wallet->custodyUpdates(),
		_wallet->signingReadyValue() | rpl::skip(1) | rpl::to_empty,
		base::timer_once(kWalletRetryDelay)
	) | rpl::filter([=] {
		return walletReady();
	}) | rpl::take(1) | rpl::on_next([=] {
		resume();
	}, _waitLifetime);
}

void OnchainOfferDeployment::resume() {
	if (_stage.current() != Stage::Waiting) {
		return;
	}
	const auto stage = _resume;
	_stage = stage;
	if (stage == Stage::Signing) {
		submit();
	} else {
		estimate();
	}
}

void OnchainOfferDeployment::waited() {
	if (_stage.current() == Stage::Waiting) {
		abandon(SendError::WalletNotReady);
	}
}

void OnchainOfferDeployment::stopWaiting() {
	_waitArmed = false;
	_waitTimer.cancel();
	_waitLifetime.destroy();
}

void OnchainOfferDeployment::authorize() {
	if (!usable()) {
		return;
	}
	stopWaiting();
	_keyLifetime.destroy();
	_stage = Stage::Authorizing;
	const auto weak = base::make_weak(this);
	Wallet::AcquireWalletKey(
		Main::MakeSessionShow(_box->uiShow(), _session),
		[=] {
			return weak
				&& (weak->_stage.current() == Stage::Authorizing)
				&& weak->current()
				&& !weak->ownGift()
				&& !weak->expired();
		},
		_keyLifetime,
		crl::guard(this, [=](Wallet::KeyAuthorization auth) {
			authorized(std::move(auth));
		}));
}

void OnchainOfferDeployment::authorized(Wallet::KeyAuthorization auth) {
	if (_stage.current() != Stage::Authorizing) {
		return;
	} else if (!current()) {
		close();
	} else if (!auth.valid()) {
		ready();
	} else {
		_authorization = std::move(auth);
		_stage = Stage::Signing;
		submit();
	}
}

void OnchainOfferDeployment::submit() {
	if (_stage.current() != Stage::Signing || !usable()) {
		return;
	} else if (!_wallet->signingReady()) {
		awaitWallet();
		return;
	}
	const auto refusal = _wallet->sendRefusal(_prepared, _authorization);
	if (refusal == SendError::SigningUnavailable && retry()) {
		return;
	} else if (refusal != SendError::None) {
		refused(refusal);
		return;
	}
	stopWaiting();
	_stage = Stage::Sending;
	const auto weak = base::make_weak(this);
	const auto completion = this->completion();
	_wallet->sendServed(
		_authorization,
		base::take(_prepared),
		crl::guard(_session, [=](Wallet::ServedResult result) {
			const auto strong = weak.get();
			if (strong && strong->_stage.current() == Stage::Sending) {
				strong->sent(std::move(result));
			} else if (result.outcome == Wallet::ServedOutcome::Accepted) {
				CompleteOffer(completion);
			}
		}));
}

void OnchainOfferDeployment::sent(Wallet::ServedResult result) {
	using Wallet::ServedOutcome;
	const auto refusal = (result.refusal == SendError::None)
		? SendError::Failed
		: result.refusal;
	switch (result.outcome) {
	case ServedOutcome::Accepted:
		accepted();
		return;
	case ServedOutcome::Uncertain:
		_authorization = {};
		track(result.operationId);
		return;
	case ServedOutcome::Duplicate:
		duplicate();
		return;
	case ServedOutcome::Rejected:
		if (refusal == SendError::KeyChanged) {
			keyChanged();
		} else if (refusal == SendError::Silent) {
			close();
		} else {
			LOG(("API Error: wallet.sendTransfer for the on-chain gift offer "
				"failed with %1.").arg(result.errorType));
			abandon(refusal);
		}
		return;
	case ServedOutcome::Refused:
		_stage = Stage::Confirming;
		refused(refusal);
		return;
	}
}

void OnchainOfferDeployment::accepted() {
	const auto completion = this->completion();
	close();
	CompleteOffer(completion);
}

void OnchainOfferDeployment::duplicate() {
	LOG(("Wallet Error: the on-chain gift offer for %1 was already sent."
		).arg(_terms.request.gift->slug));
	const auto completion = this->completion();
	close();
	FinishOffer(completion, {
		.text = tr::lng_gift_offer_onchain_already_sent(tr::now, tr::marked),
	});
}

void OnchainOfferDeployment::track(const std::string &operationId) {
	if (operationId.empty()) {
		LOG(("Wallet Error: the on-chain gift offer for %1 is uncertain "
			"without an operation id.").arg(_terms.request.gift->slug));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_operationId = operationId;
	_stage = Stage::Tracking;
	_wallet->historyUpdates() | rpl::on_next([=] {
		resolve();
	}, _trackLifetime);
	resolve();
}

void OnchainOfferDeployment::resolve() {
	using Status = Wallet::TransferItem::Status;
	if (_stage.current() != Stage::Tracking) {
		return;
	}
	const auto item = _wallet->trackedTransaction(_operationId);
	if (!item || item->status == Status::Pending) {
		return;
	} else if (item->status == Status::Success) {
		accepted();
	} else {
		LOG(("Wallet Error: the on-chain gift offer for %1 failed."
			).arg(_terms.request.gift->slug));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainOfferDeployment::keyChanged() {
	const auto show = _terms.request.show;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWalletKeyChanged(show);
	}
}

void OnchainOfferDeployment::closeWithToast(const QString &text) {
	const auto show = _terms.request.show;
	const auto valid = current();
	close();
	if (valid) {
		show->showToast(text);
	}
}

void OnchainOfferDeployment::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainOfferDeployment::closing() {
	if (_stage.current() == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	_waitTimer.cancel();
	if (_weakSession) {
		_wallet->cancelFeeEstimate(_owner);
		if (base::take(_polling)) {
			_wallet->stopPolling();
		}
	}
	_waitLifetime.destroy();
	_trackLifetime.destroy();
	_operationId.clear();
	_prepared = nullptr;
	_authorization = {};
	_keyLifetime.destroy();
}

void OnchainOfferConfirmBox(not_null<GenericBox*> box, OfferTerms terms) {
	const auto session = &terms.request.show->session();
	box->setWidth(st::boxWideWidth);
	AddGiftOnchainArtwork(
		box,
		session,
		terms.request.gift,
		terms.request.limitedCount,
		terms.request.limitedLeft);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_gift_offer_confirm_title(),
			st::giftBoxSubtitle),
		st::giftBoxSubtitleMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			QuestionText(*terms.request.gift),
			st::giftBoxAbout),
		st::giftBoxAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	AddTermsTable(box, terms);
	box->addSkip(st::walletConnectCardMargin.bottom()
		- st::giveawayGiftCodeTableMargin.bottom()
		- st::giftBoxAboutMargin.top());
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_gift_offer_onchain_refund_note(),
			st::giftBoxAbout),
		st::giftBoxAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	box->addSkip(st::walletConnectCardMargin.bottom()
		- st::giftBoxAboutMargin.bottom());

	const auto deployment = box->lifetime().make_state<OnchainOfferDeployment>(
		box,
		std::move(terms));
	const auto fee = AddGiftOnchainFeeLine(
		box,
		deployment->feeValue() | rpl::filter_optional());
	fee->toggleOn(deployment->feeValue(
	) | rpl::map([](const std::optional<int64> &value) {
		return value.has_value();
	}));
	const auto insufficient = AddErrorLine(
		box,
		OnchainOfferInsufficientText());
	insufficient->entity()->setClickHandlerFilter([=](const auto &...) {
		ShowOnchainOfferAddFunds(session);
		return false;
	});
	insufficient->toggleOn(deployment->insufficientValue());
	AddErrorLine(
		box,
		tr::lng_gift_buy_onchain_error(tr::marked)
	)->toggleOn(deployment->refusedValue());

	const auto press = crl::guard(deployment, [=] {
		deployment->press();
	});
	const auto confirm = box->addButton(
		tr::lng_wallet_connect_request_confirm(),
		press).data();
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	AddGiftOnchainBusySpinner(confirm, deployment->busyValue());
	deployment->busyValue() | rpl::on_next([=](bool busy) {
		confirm->setDisabled(busy);
	}, confirm->lifetime());
	Wallet::SubmitBoxOnEnter(box, press);
	deployment->start();
}

} // namespace

rpl::producer<TextWithEntities> OnchainOfferInsufficientText() {
	return tr::lng_gift_offer_onchain_insufficient(
		lt_link,
		tr::lng_gift_offer_onchain_add_funds(
			lt_arrow,
			rpl::single(Text::IconEmoji(&st::textMoreIconEmoji)),
			tr::link),
		tr::marked);
}

void ShowOnchainOfferAddFunds(not_null<Main::Session*> session) {
	const auto panel = Wallet::ShowWallet(session);
	Wallet::ShowWalletReceiveBox(session, panel->uiShow());
}

base::weak_qptr<GenericBox> ShowOnchainGiftOfferConfirmBox(
		OnchainGiftOfferRequest request,
		const MTPpayments_StarGiftOnchainOfferTx &answer) {
	const auto show = request.show;
	if (!show
		|| !show->valid()
		|| !request.gift
		|| !request.form
		|| (request.amount <= 0)) {
		return nullptr;
	}
	auto terms = OfferTerms{
		.request = std::move(request),
		.operation = Wallet::ServedOperationFromOffer(answer),
		.expiresAt = answer.data().vexpires_at().v,
	};
	const auto amount = terms.request.amount;
	const auto &message = terms.operation.message;
	if (message.destination.isEmpty()
		|| (message.value <= 0)
		|| (message.value < amount)
		|| (terms.expiresAt <= base::unixtime::now())) {
		LOG(("API Error: payments.sendStarGiftOnchainOffer answered value %1 "
			"for amount %2, expiring at %3, destination %4."
			).arg(message.value
			).arg(amount
			).arg(terms.expiresAt
			).arg(message.destination.isEmpty()
				? u"empty"_q
				: u"present"_q));
		show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
		return nullptr;
	} else if (LookupOwnOnchainGift(&show->session(), *terms.request.gift)
		!= OwnOnchainGift::None) {
		LOG(("Wallet Error: the wallet holds %1, so it gets no offer."
			).arg(terms.request.gift->slug));
		show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
		return nullptr;
	}
	return show->show(
		Box(OnchainOfferConfirmBox, std::move(terms)),
		LayerOption::KeepOther);
}

} // namespace Ui
