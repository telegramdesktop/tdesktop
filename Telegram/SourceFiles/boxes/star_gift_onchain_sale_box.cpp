/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_sale_box.h"

#include "base/timer.h"
#include "base/timer_rpl.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/gift_onchain_offer_box.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/table_rows.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

using Wallet::SendError;

constexpr auto kWalletRetryDelay = crl::time(1000);

enum class SaleKind : uchar {
	List,
	Change,
	Unlist,
};

enum class Stage : uchar {
	Idle,
	Requesting,
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

enum class Ownership : uchar {
	Own,
	Other,
	Unknown,
};

struct SaleArgs {
	SaleKind kind = SaleKind::List;
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Data::UniqueGift> gift;
	int64 price = 0;
	Fn<void()> invalid;
	base::weak_qptr<GenericBox> priceBox;
};

struct SaleCompletion {
	SaleKind kind = SaleKind::List;
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Data::UniqueGift> gift;
	int64 price = 0;
	base::weak_qptr<GenericBox> priceBox;
	base::weak_ptr<Main::Session> session;
};

class OnchainSale final : public base::has_weak_ptr {
public:
	OnchainSale(not_null<GenericBox*> box, SaleArgs args);
	~OnchainSale();

	[[nodiscard]] rpl::producer<int64> amountValue() const;
	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;
	[[nodiscard]] rpl::producer<bool> insufficientValue() const;
	[[nodiscard]] rpl::producer<QString> refusalValue() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;

	void start();
	void press();

private:
	[[nodiscard]] bool current() const;
	[[nodiscard]] Ownership ownership() const;
	[[nodiscard]] bool usable();
	[[nodiscard]] bool walletReady() const;
	[[nodiscard]] SaleCompletion completion() const;
	void request();
	void answered(const MTPpayments_StarGiftOnchainSalePayload &result);
	void failed(const MTP::Error &error);
	void unlistFirst();
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
	const SaleArgs _args;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	MTP::Sender _api;
	base::Timer _waitTimer;
	std::optional<Wallet::ServedOperation> _operation;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	std::string _operationId;
	rpl::variable<Stage> _stage = Stage::Idle;
	rpl::variable<int64> _amount = 0;
	rpl::variable<std::optional<int64>> _fee;
	rpl::variable<SendError> _error = SendError::None;
	uint64 _owner = 0;
	mtpRequestId _requestId = 0;
	int _retries = 0;
	Stage _resume = Stage::Idle;
	bool _waitArmed = false;
	bool _polling = false;
	rpl::lifetime _waitLifetime;
	rpl::lifetime _trackLifetime;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

[[nodiscard]] Ownership LookupOwnership(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	if (gift.ownerId
		|| gift.ownerAddress.isEmpty()
		|| gift.slug.isEmpty()
		|| gift.burned
		|| !session->appConfig().walletAvailable()
		|| session->supportMode()) {
		return Ownership::Other;
	}
	const auto owner = Wallet::CanonicalAddress(gift.ownerAddress);
	if (owner.isEmpty()) {
		return Ownership::Other;
	}
	const auto own = session->wallet().address();
	if (!own) {
		return Ownership::Unknown;
	}
	const auto wallet = Wallet::CanonicalAddress(*own);
	return (!wallet.isEmpty()
		&& owner.compare(wallet, Qt::CaseInsensitive) == 0)
		? Ownership::Own
		: Ownership::Other;
}

[[nodiscard]] bool Busy(Stage stage) {
	return (stage != Stage::Idle) && (stage != Stage::Ready);
}

[[nodiscard]] bool Insufficient(SendError error) {
	return (error == SendError::InsufficientBalance)
		|| (error == SendError::InsufficientFees);
}

[[nodiscard]] QString RefusalText(SendError error) {
	if (error == SendError::None || Insufficient(error)) {
		return QString();
	}
	const auto text = Wallet::SendErrorText(error, 0);
	return text.isEmpty() ? tr::lng_gift_buy_onchain_error(tr::now) : text;
}

[[nodiscard]] QString RequestMethod(SaleKind kind) {
	return (kind == SaleKind::Unlist)
		? u"payments.cancelStarGiftOnchainSale"_q
		: u"payments.updateStarGiftOnchainPrice"_q;
}

[[nodiscard]] rpl::producer<QString> TitleText(
		SaleKind kind,
		const QString &name) {
	switch (kind) {
	case SaleKind::List: return tr::lng_gift_sell_put();
	case SaleKind::Change: return tr::lng_gift_transfer_update();
	case SaleKind::Unlist: break;
	}
	return tr::lng_gift_sell_unlist_title(lt_name, rpl::single(name));
}

[[nodiscard]] rpl::producer<TextWithEntities> QuestionText(
		SaleKind kind,
		const QString &name,
		int64 price) {
	if (kind == SaleKind::Unlist) {
		return tr::lng_gift_sell_unlist_sure(tr::marked);
	}
	return tr::lng_gift_sell_onchain_confirm(
		lt_name,
		rpl::single(tr::bold(name)),
		lt_price,
		rpl::single(tr::bold(PrepareGramAmountText(price))),
		tr::marked);
}

[[nodiscard]] QString SaleToastText(const SaleCompletion &completion) {
	const auto name = Data::UniqueGiftName(*completion.gift);
	switch (completion.kind) {
	case SaleKind::List:
		return tr::lng_gift_sell_onchain_toast(
			tr::now,
			lt_name,
			name,
			lt_price,
			PrepareGramAmountText(completion.price));
	case SaleKind::Change:
		return tr::lng_gift_sell_updated(tr::now, lt_name, name);
	case SaleKind::Unlist:
		break;
	}
	return tr::lng_gift_sell_removed(tr::now, lt_name, name);
}

void ApplySale(const SaleCompletion &completion) {
	const auto session = completion.session.get();
	if (!session) {
		return;
	}
	const auto &gift = completion.gift;
	if (completion.kind == SaleKind::Unlist) {
		gift->starsForResale = 0;
		gift->nanoTonForResale = 0;
		gift->onlyAcceptTon = false;
		gift->onchainSale = false;
	} else {
		const auto price = CreditsAmount(
			0,
			completion.price,
			CreditsType::Ton);
		gift->starsForResale = StarsFromTon(session, price).whole();
		gift->nanoTonForResale = completion.price;
		gift->onlyAcceptTon = true;
		gift->onchainSale = true;
	}
	session->data().notifyGiftUpdate({
		.slug = gift->slug,
		.action = Data::GiftUpdate::Action::ResaleChange,
	});
	if (const auto box = completion.priceBox.get()) {
		box->closeBox();
	}
}

void CompleteSale(const SaleCompletion &completion) {
	ApplySale(completion);
	const auto session = completion.session.get();
	const auto &show = completion.show;
	if (!session || !show->valid() || (&show->session() != session)) {
		return;
	}
	show->showToast(SaleToastText(completion));
}

void ShowUnlistFirst(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		base::weak_qptr<GenericBox> priceBox) {
	const auto name = Data::UniqueGiftName(*gift);
	show->show(MakeConfirmBox({
		.text = tr::lng_gift_sell_onchain_unlist_first(
			lt_name,
			rpl::single(tr::bold(name)),
			tr::marked),
		.confirmed = [=](Fn<void()> close) {
			close();
			if (const auto strong = priceBox.get()) {
				strong->closeBox();
			}
			ShowOnchainGiftUnlistBox(show, gift);
		},
		.confirmText = tr::lng_gift_transfer_unlist(),
	}));
}

void AddGramRow(
		not_null<TableLayout*> table,
		rpl::producer<QString> label,
		rpl::producer<int64> nanos) {
	auto helper = Text::CustomEmojiHelper();
	const auto mark = Wallet::GramMark(
		helper,
		table->st().defaultValue.style.font);
	AddTableRow(
		table,
		std::move(label),
		std::move(nanos) | rpl::map([=](int64 value) {
			auto result = mark;
			result.append(QChar(' ')).append(
				Lang::FormatCreditsAmountDecimal(
					CreditsAmount(0, value, CreditsType::Ton)));
			return result;
		}),
		helper.context());
}

OnchainSale::OnchainSale(not_null<GenericBox*> box, SaleArgs args)
: _box(box)
, _args(std::move(args))
, _session(&_args.show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _api(&_session->mtp())
, _waitTimer([=] { waited(); }) {
}

OnchainSale::~OnchainSale() {
	closing();
}

rpl::producer<int64> OnchainSale::amountValue() const {
	return _amount.value();
}

rpl::producer<std::optional<int64>> OnchainSale::feeValue() const {
	return _fee.value();
}

rpl::producer<bool> OnchainSale::insufficientValue() const {
	return _error.value() | rpl::map(Insufficient);
}

rpl::producer<QString> OnchainSale::refusalValue() const {
	return _error.value() | rpl::map(RefusalText);
}

rpl::producer<bool> OnchainSale::busyValue() const {
	return _stage.value(
	) | rpl::filter(
		rpl::mappers::_1 != Stage::Closed
	) | rpl::map(Busy) | rpl::distinct_until_changed();
}

void OnchainSale::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_polling = true;
	_wallet->startPolling();
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	if (_args.kind != SaleKind::Unlist) {
		_stage = Stage::Requesting;
		request();
	}
}

void OnchainSale::press() {
	const auto stage = _stage.current();
	if (Busy(stage)) {
		return;
	}
	_retries = 0;
	_error = SendError::None;
	if (stage == Stage::Idle) {
		_stage = Stage::Requesting;
		request();
	} else {
		reestimate();
	}
}

bool OnchainSale::current() const {
	return (_stage.current() != Stage::Closed)
		&& _weakSession
		&& _args.show->valid()
		&& (&_args.show->session() == _session);
}

Ownership OnchainSale::ownership() const {
	return LookupOwnership(_session, *_args.gift);
}

bool OnchainSale::usable() {
	if (!current()) {
		close();
		return false;
	}
	const auto ownership = this->ownership();
	if (ownership == Ownership::Unknown) {
		awaitWallet();
		return false;
	} else if (ownership == Ownership::Other) {
		LOG(("Wallet Error: the wallet no longer holds %1 for %2."
			).arg(_args.gift->slug, RequestMethod(_args.kind)));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	}
	return true;
}

bool OnchainSale::walletReady() const {
	using Wallet::DeviceMode;
	using Wallet::Presence;
	if (_wallet->presenceCurrent() != Presence::Ready) {
		return false;
	} else if (_resume == Stage::Requesting) {
		return true;
	} else if (!_wallet->transferWalletIdentity()) {
		return false;
	} else if (_resume != Stage::Signing) {
		return true;
	}
	return (_wallet->deviceCustodyState().mode == DeviceMode::Full)
		&& _wallet->signingReady();
}

SaleCompletion OnchainSale::completion() const {
	return {
		.kind = _args.kind,
		.show = _args.show,
		.gift = _args.gift,
		.price = _args.price,
		.priceBox = _args.priceBox,
		.session = _weakSession,
	};
}

void OnchainSale::request() {
	if (!usable()) {
		return;
	}
	const auto done = [=](
			const MTPpayments_StarGiftOnchainSalePayload &result) {
		_requestId = 0;
		answered(result);
	};
	const auto fail = [=](const MTP::Error &error) {
		_requestId = 0;
		failed(error);
	};
	const auto slug = MTP_string(_args.gift->slug);
	if (_args.kind == SaleKind::Unlist) {
		_requestId = _api.request(MTPpayments_CancelStarGiftOnchainSale(
			slug
		)).done(done).fail(fail).handleFloodErrors().send();
	} else {
		_requestId = _api.request(MTPpayments_UpdateStarGiftOnchainPrice(
			slug,
			MTP_long(_args.price)
		)).done(done).fail(fail).handleFloodErrors().send();
	}
}

void OnchainSale::answered(
		const MTPpayments_StarGiftOnchainSalePayload &result) {
	if (_stage.current() != Stage::Requesting) {
		return;
	}
	if (result.type() == mtpc_payments_starGiftOnchainFixedSalePayload) {
		LOG(("API Error: %1 answered "
			"payments.starGiftOnchainFixedSalePayload."
			).arg(RequestMethod(_args.kind)));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	auto operation = Wallet::ServedOperationFromSale(result);
	const auto &message = operation.messages.front();
	const auto amount = message.value;
	if (message.destination.isEmpty() || amount <= 0) {
		LOG(("API Error: %1 answered a malformed sale payload (amount %2)."
			).arg(RequestMethod(_args.kind)
			).arg(amount));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_operation = std::move(operation);
	_amount = amount;
	_stage = Stage::Estimating;
	estimate();
}

void OnchainSale::failed(const MTP::Error &error) {
	if (_stage.current() != Stage::Requesting) {
		return;
	}
	const auto &type = error.type();
	const auto listing = (_args.kind != SaleKind::Unlist);
	if (MTP::IgnoreError(error)) {
		close();
	} else if (listing && type == u"STARGIFT_ONCHAIN_PRICE_INVALID"_q) {
		const auto invalid = _args.invalid;
		close();
		if (invalid) {
			invalid();
		}
	} else if (listing && type == u"STARGIFT_ONCHAIN_SALE_ACTIVE"_q) {
		unlistFirst();
	} else if (!listing && type == u"STARGIFT_ONCHAIN_SALE_INACTIVE"_q) {
		const auto completion = this->completion();
		close();
		ApplySale(completion);
	} else {
		LOG(("API Error: %1 failed with %2."
			).arg(RequestMethod(_args.kind), type));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainSale::unlistFirst() {
	const auto show = _args.show;
	const auto gift = _args.gift;
	const auto priceBox = _args.priceBox;
	const auto valid = current();
	close();
	if (valid) {
		ShowUnlistFirst(show, gift, priceBox);
	}
}

void OnchainSale::reestimate() {
	_stage = _fee.current().has_value()
		? Stage::Confirming
		: Stage::Estimating;
	estimate();
}

void OnchainSale::estimate() {
	if (!usable()) {
		return;
	}
	_prepared = nullptr;
	_wallet->estimateServed(
		_owner,
		*_operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			estimated(std::move(result));
		}));
}

void OnchainSale::estimated(Wallet::FeeResult result) {
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

bool OnchainSale::retry() {
	if (++_retries > Wallet::kSendRefusalRetries) {
		return false;
	}
	reestimate();
	return true;
}

void OnchainSale::refused(SendError error) {
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

void OnchainSale::abandon(SendError error) {
	_fee = std::nullopt;
	_error = error;
	ready();
}

void OnchainSale::ready() {
	stopWaiting();
	_prepared = nullptr;
	_authorization = {};
	_stage = _operation ? Stage::Ready : Stage::Idle;
}

void OnchainSale::awaitWallet() {
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

void OnchainSale::resume() {
	if (_stage.current() != Stage::Waiting) {
		return;
	}
	const auto stage = _resume;
	_stage = stage;
	if (stage == Stage::Requesting) {
		request();
	} else if (stage == Stage::Signing) {
		submit();
	} else {
		estimate();
	}
}

void OnchainSale::waited() {
	if (_stage.current() == Stage::Waiting) {
		abandon(SendError::WalletNotReady);
	}
}

void OnchainSale::stopWaiting() {
	_waitArmed = false;
	_waitTimer.cancel();
	_waitLifetime.destroy();
}

void OnchainSale::authorize() {
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
				&& (weak->ownership() == Ownership::Own);
		},
		_keyLifetime,
		crl::guard(this, [=](Wallet::KeyAuthorization auth) {
			authorized(std::move(auth));
		}));
}

void OnchainSale::authorized(Wallet::KeyAuthorization auth) {
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

void OnchainSale::submit() {
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
				CompleteSale(completion);
			}
		}));
}

void OnchainSale::sent(Wallet::ServedResult result) {
	using Wallet::ServedOutcome;
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
		if (result.refusal == SendError::KeyChanged) {
			keyChanged();
		} else if (result.refusal == SendError::Silent) {
			close();
		} else {
			LOG(("API Error: wallet.sendTransfer for %1 failed with %2."
				).arg(RequestMethod(_args.kind), result.errorType));
			abandon(result.refusal);
		}
		return;
	case ServedOutcome::Refused:
		_stage = Stage::Confirming;
		refused(result.refusal);
		return;
	}
}

void OnchainSale::accepted() {
	const auto completion = this->completion();
	close();
	CompleteSale(completion);
}

void OnchainSale::duplicate() {
	LOG(("Wallet Error: the transfer for %1 was already sent."
		).arg(RequestMethod(_args.kind)));
	const auto priceBox = _args.priceBox;
	closeWithToast(tr::lng_gift_sell_onchain_already_sent(tr::now));
	if (const auto strong = priceBox.get()) {
		strong->closeBox();
	}
}

void OnchainSale::track(const std::string &operationId) {
	if (operationId.empty()) {
		LOG(("Wallet Error: the transfer for %1 is uncertain "
			"without an operation id.").arg(RequestMethod(_args.kind)));
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

void OnchainSale::resolve() {
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
		LOG(("Wallet Error: the transfer for %1 failed."
			).arg(RequestMethod(_args.kind)));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainSale::keyChanged() {
	const auto show = _args.show;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWalletKeyChanged(show);
	}
}

void OnchainSale::closeWithToast(const QString &text) {
	const auto show = _args.show;
	const auto valid = current();
	close();
	if (valid) {
		show->showToast(text);
	}
}

void OnchainSale::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainSale::closing() {
	if (_stage.current() == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	_waitTimer.cancel();
	if (_weakSession) {
		_api.request(base::take(_requestId)).cancel();
		_wallet->cancelFeeEstimate(_owner);
		if (base::take(_polling)) {
			_wallet->stopPolling();
		}
	}
	_waitLifetime.destroy();
	_trackLifetime.destroy();
	_operation = std::nullopt;
	_prepared = nullptr;
	_authorization = {};
	_keyLifetime.destroy();
}

void OnchainSaleBox(not_null<GenericBox*> box, SaleArgs args) {
	const auto session = &args.show->session();
	const auto kind = args.kind;
	const auto price = args.price;
	const auto name = Data::UniqueGiftName(*args.gift);
	const auto sale = box->lifetime().make_state<OnchainSale>(
		box,
		std::move(args));
	const auto &padding = st::boxPadding;
	box->setWidth(st::boxWideWidth);
	box->setTitle(TitleText(kind, name));
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			QuestionText(kind, name, price),
			st::boxLabel),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()));

	const auto terms = box->addRow(
		object_ptr<SlideWrap<TableLayout>>(
			box,
			object_ptr<TableLayout>(box, st::defaultTable),
			padding),
		style::margins());
	if (kind != SaleKind::Unlist) {
		AddGramRow(
			terms->entity(),
			tr::lng_gift_resale_price(),
			rpl::single(price));
	}
	AddGramRow(
		terms->entity(),
		((kind == SaleKind::Unlist)
			? tr::lng_gift_sell_onchain_unlist_fee()
			: tr::lng_gift_sell_onchain_listing_fee()),
		sale->amountValue());
	terms->toggleOn(sale->amountValue() | rpl::map(rpl::mappers::_1 > 0));
	box->addSkip(st::walletConnectCardMargin.bottom() - padding.bottom());

	const auto fee = AddGiftOnchainFeeLine(
		box,
		sale->feeValue() | rpl::filter_optional());
	fee->toggleOn(sale->feeValue(
	) | rpl::map([](const std::optional<int64> &value) {
		return value.has_value();
	}));
	const auto insufficient = AddGiftOnchainInsufficientBlock(box, session);
	insufficient->toggleOn(sale->insufficientValue());
	const auto refusal = box->addRow(
		object_ptr<SlideWrap<FlatLabel>>(
			box,
			object_ptr<FlatLabel>(
				box,
				sale->refusalValue(),
				st::walletSendUserErrorLabel),
			st::walletConnectCaptionMargin),
		style::margins(),
		style::al_justify);
	refusal->entity()->setTryMakeSimilarLines(true);
	refusal->toggleOn(sale->refusalValue(
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));

	const auto press = crl::guard(sale, [=] {
		sale->press();
	});
	const auto confirm = box->addButton(
		((kind == SaleKind::Unlist)
			? tr::lng_gift_transfer_unlist()
			: tr::lng_wallet_connect_request_confirm()),
		press).data();
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	AddGiftOnchainBusySpinner(confirm, sale->busyValue());
	sale->busyValue() | rpl::on_next([=](bool busy) {
		confirm->setDisabled(busy);
	}, confirm->lifetime());
	Wallet::SubmitBoxOnEnter(box, press);
	sale->start();
}

} // namespace

OwnOnchainGift LookupOwnOnchainGift(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	return (LookupOwnership(session, gift) != Ownership::Own)
		? OwnOnchainGift::None
		: gift.onchainSale
		? OwnOnchainGift::Listed
		: OwnOnchainGift::Held;
}

void ShowOnchainGiftSellBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		Settings::GiftWearBoxStyleOverride st) {
	if (!show
		|| !show->valid()
		|| !gift
		|| (LookupOwnOnchainGift(&show->session(), *gift)
			== OwnOnchainGift::None)) {
		return;
	}
	struct Dialog {
		base::weak_qptr<GenericBox> price;
		base::weak_qptr<GenericBox> confirmation;
	};
	const auto dialog = std::make_shared<Dialog>();
	const auto submit = [=](int64 nanos, Fn<void()> invalid) {
		if (dialog->confirmation || !show->valid()) {
			return;
		}
		dialog->confirmation = show->show(Box(OnchainSaleBox, SaleArgs{
			.kind = (gift->onchainSale ? SaleKind::Change : SaleKind::List),
			.show = show,
			.gift = gift,
			.price = nanos,
			.invalid = std::move(invalid),
			.priceBox = dialog->price,
		}), LayerOption::KeepOther);
	};
	dialog->price = show->show(Box(
		UniqueGiftSellBox,
		show,
		gift,
		Data::SavedStarGiftId(),
		0,
		st,
		submit));
}

void ShowOnchainGiftUnlistBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift) {
	if (!show
		|| !show->valid()
		|| !gift
		|| (LookupOwnOnchainGift(&show->session(), *gift)
			== OwnOnchainGift::None)) {
		return;
	}
	show->show(Box(OnchainSaleBox, SaleArgs{
		.kind = SaleKind::Unlist,
		.show = show,
		.gift = std::move(gift),
	}), LayerOption::KeepOther);
}

} // namespace Ui
