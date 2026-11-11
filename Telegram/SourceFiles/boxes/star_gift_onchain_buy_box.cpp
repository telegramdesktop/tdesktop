/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_buy_box.h"

#include "api/api_earn.h"
#include "api/api_premium.h"
#include "base/timer.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/components/credits.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "history/history_item_helpers.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

using Wallet::SendError;

constexpr auto kWalletWait = 15 * crl::time(1000);

enum class Stage : uchar {
	Waiting,
	Estimating,
	Assessing,
	Ready,
	Offering,
	Withdrawing,
	Funding,
	Buying,
	Rereading,
	Repricing,
	Preparing,
	Authorizing,
	Signing,
	Sending,
	Tracking,
	Closed,
};

enum class Readiness : uchar {
	Unknown,
	Ready,
	Absent,
};

struct Completion {
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Data::UniqueGift> gift;
	Fn<void()> bought;
	base::weak_ptr<Main::Session> session;
	std::optional<Wallet::TransferWalletIdentity> identity;
};

class OnchainPurchase final : public base::has_weak_ptr {
public:
	OnchainPurchase(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		base::weak_ptr<OnchainBuyMemory> memory,
		Fn<void()> bought);
	~OnchainPurchase();

	[[nodiscard]] rpl::producer<int64> priceValue() const;
	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;
	[[nodiscard]] rpl::producer<bool> shortValue() const;
	[[nodiscard]] rpl::producer<QString> refusalValue() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;
	[[nodiscard]] rpl::producer<bool> pressableValue() const;

	void start();
	void press();
	void addFunds();

private:
	[[nodiscard]] bool current() const;
	[[nodiscard]] bool idle() const;
	[[nodiscard]] bool assessable() const;
	[[nodiscard]] bool offerable() const;
	[[nodiscard]] bool covers(int64 earnings) const;
	[[nodiscard]] SendError walletRefusal() const;
	[[nodiscard]] bool walletUsable();
	[[nodiscard]] Completion completion() const;
	void subscribe();
	void walletUpdated();
	void walletChanged();
	void wait();
	void waited();
	void preflight();
	void preflighted(Wallet::FeeResult result);
	void settle(
		SendError error,
		std::optional<int64> fee,
		std::optional<int64> spend);
	void requestEarnings();
	void earningsLoaded(int64 earnings);
	void ready();
	void offer();
	void showOffer();
	void offerCancelled();
	void offerProceeded(int64 earnings);
	void withdraw(
		not_null<Window::SessionController*> window,
		int64 earnings);
	void withdrawn();
	void awaitFunds();
	void earningsUnavailable();
	void withdrawalEnded();
	void recheck();
	void buy();
	void request();
	void answered(
		const MTPpayments_StarGiftOnchainSalePayload &answer,
		int64 expected);
	void reread();
	void reloaded(const MTPpayments_UniqueStarGift &result);
	void failed(const MTP::Error &error, const QString &method);
	void repriced(int64 price, const QString &address);
	void prepare();
	void prepared(Wallet::FeeResult result);
	void authorize();
	void authorized(Wallet::KeyAuthorization auth);
	void awaitSigning();
	void signingWaited();
	void submit();
	void sent(Wallet::ServedResult result);
	void refused(SendError error);
	void abandon(SendError error);
	void stopAttempt();
	void accepted();
	void track(const std::string &operationId);
	void resolve();
	void refresh();
	void openMoney();
	void keyChanged();
	void closeWithToast(const QString &text);
	void close();
	void closing();
	void stopPolling();

	const not_null<GenericBox*> _box;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::shared_ptr<Data::UniqueGift> _gift;
	const Fn<void()> _bought;
	const base::weak_ptr<OnchainBuyMemory> _memory;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	MTP::Sender _api;
	base::Timer _waitTimer;
	base::Timer _signingWait;
	QString _address;
	std::optional<Wallet::TransferWalletIdentity> _identity;
	std::optional<Wallet::ServedOperation> _served;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	std::string _operationId;
	base::weak_qptr<GenericBox> _priceBox;
	base::weak_qptr<GenericBox> _earningsBox;
	Fn<void()> _afterPreflight;
	std::optional<int64> _spend;
	std::optional<int64> _earnings;
	rpl::variable<int64> _price;
	rpl::variable<std::optional<int64>> _fee;
	rpl::variable<QString> _refusal;
	rpl::variable<bool> _short = false;
	rpl::variable<bool> _busy = false;
	rpl::variable<bool> _pressable = false;
	SendError _error = SendError::None;
	Stage _stage = Stage::Waiting;
	uint64 _owner = 0;
	mtpRequestId _requestId = 0;
	mtpRequestId _earningsRequestId = 0;
	int _retries = 0;
	bool _waitArmed = false;
	bool _signingArmed = false;
	bool _stale = false;
	bool _polling = false;
	bool _offer = false;
	bool _earningsDone = false;
	bool _funding = false;
	rpl::lifetime _trackLifetime;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

[[nodiscard]] Readiness WalletReadiness(not_null<Wallet::Session*> wallet) {
	using Wallet::DeviceMode;
	using Wallet::Presence;
	const auto presence = wallet->presenceCurrent();
	const auto mode = wallet->deviceCustodyState().mode;
	if (presence != Presence::Unknown && presence != Presence::Ready) {
		return Readiness::Absent;
	} else if (presence == Presence::Unknown || mode == DeviceMode::Unknown) {
		return Readiness::Unknown;
	}
	return (mode == DeviceMode::Full) ? Readiness::Ready : Readiness::Absent;
}

[[nodiscard]] bool SameWallet(
		const Wallet::TransferWalletIdentity &a,
		const Wallet::TransferWalletIdentity &b) {
	return (a.address == b.address) && (a.publicKey == b.publicKey);
}

[[nodiscard]] bool Insufficient(SendError error) {
	return (error == SendError::InsufficientBalance)
		|| (error == SendError::InsufficientFees);
}

[[nodiscard]] SendError FundsRefusal(int64 value, int64 fee, int64 balance) {
	return (value > balance)
		? SendError::InsufficientBalance
		: (fee > balance - value)
		? SendError::InsufficientFees
		: SendError::None;
}

[[nodiscard]] bool EarningsCoverShortfall(
		int64 value,
		int64 spend,
		int64 balance,
		int64 earnings) {
	if (spend <= 0
		|| balance < 0
		|| earnings <= 0
		|| earnings > std::numeric_limits<int64>::max() - balance
		|| FundsRefusal(value, spend, balance) == SendError::None) {
		return false;
	}
	return FundsRefusal(value, spend, balance + earnings) == SendError::None;
}

[[nodiscard]] int64 AvailableEarnings(
		const MTPpayments_StarsRevenueStats &result) {
	const auto &status = result.data().vstatus().data();
	const auto amount = CreditsAmountFromTL(status.vavailable_balance());
	if (!amount.ton()) {
		return 0;
	}
	const auto nanos = amount.whole() * kNanosInOne + amount.nano();
	return std::max(nanos, int64(0));
}

[[nodiscard]] TextWithEntities GramCost(int64 nanos) {
	return Text::IconEmoji(&st::tonIconEmoji).append(
		Lang::FormatCreditsAmountDecimal(
			CreditsAmount(0, nanos, CreditsType::Ton)));
}

[[nodiscard]] QString RefusalText(SendError error) {
	const auto text = Wallet::SendErrorText(error, 0);
	return text.isEmpty()
		? tr::lng_gift_buy_onchain_error(tr::now)
		: text;
}

void FollowCollectibles(
		not_null<SeparatePanel*> panel,
		not_null<Wallet::Session*> wallet,
		Wallet::TransferWalletIdentity identity) {
	const auto lifetime = panel->lifetime().make_state<rpl::lifetime>();
	const auto select = [=] {
		const auto current = wallet->transferWalletIdentity();
		if (!current) {
			return;
		} else if (!SameWallet(*current, identity)) {
			lifetime->destroy();
		} else if (!wallet->collectibles().empty()) {
			lifetime->destroy();
			wallet->setCollectiblesTab(true);
		}
	};
	wallet->collectiblesUpdates() | rpl::on_next(select, *lifetime);
	select();
}

void ShowBoughtGift(
		base::weak_ptr<Main::Session> weak,
		std::optional<Wallet::TransferWalletIdentity> identity) {
	const auto session = weak.get();
	if (!session) {
		return;
	}
	const auto panel = Wallet::ShowWallet(session);
	if (identity) {
		FollowCollectibles(panel, &session->wallet(), *identity);
	}
}

void CompletePurchase(Completion completion) {
	const auto session = completion.session.get();
	if (!session) {
		return;
	}
	const auto &gift = completion.gift;
	gift->starsForResale = 0;
	gift->nanoTonForResale = 0;
	session->data().notifyGiftUpdate({
		.slug = gift->slug,
		.action = Data::GiftUpdate::Action::ResaleChange,
	});
	if (completion.bought) {
		completion.bought();
	}
	const auto &show = completion.show;
	if (!completion.session
		|| !show->valid()
		|| (&show->session() != session)) {
		return;
	}
	ShowGiftMovedToast(
		show,
		gift->model.document,
		tr::lng_gift_buy_onchain_done(
			tr::now,
			lt_path,
			WalletCollectiblesPath(),
			tr::marked),
		[weak = completion.session, identity = completion.identity] {
			ShowBoughtGift(weak, identity);
		});
}

OnchainPurchase::OnchainPurchase(
	not_null<GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	base::weak_ptr<OnchainBuyMemory> memory,
	Fn<void()> bought)
: _box(box)
, _show(std::move(show))
, _gift(std::move(gift))
, _bought(std::move(bought))
, _memory(std::move(memory))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _api(&_session->mtp())
, _waitTimer([=] { waited(); })
, _signingWait([=] { signingWaited(); })
, _address(_gift->giftAddress)
, _price(_gift->nanoTonForResale) {
}

OnchainPurchase::~OnchainPurchase() {
	closing();
}

rpl::producer<int64> OnchainPurchase::priceValue() const {
	return _price.value();
}

rpl::producer<std::optional<int64>> OnchainPurchase::feeValue() const {
	return _fee.value();
}

rpl::producer<bool> OnchainPurchase::shortValue() const {
	return _short.value();
}

rpl::producer<QString> OnchainPurchase::refusalValue() const {
	return _refusal.value();
}

rpl::producer<bool> OnchainPurchase::busyValue() const {
	return _busy.value();
}

rpl::producer<bool> OnchainPurchase::pressableValue() const {
	return _pressable.value();
}

void OnchainPurchase::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_polling = true;
	_wallet->startPolling();
	subscribe();
	walletChanged();
}

void OnchainPurchase::press() {
	if (_stage != Stage::Ready
		|| _busy.current()
		|| !_pressable.current()) {
		return;
	}
	_busy = true;
	_retries = 0;
	refresh();
	if (_offer) {
		offer();
	} else if (base::take(_stale)) {
		_afterPreflight = [=] { buy(); };
		_error = SendError::None;
		_fee = std::nullopt;
		walletChanged();
	} else {
		buy();
	}
}

void OnchainPurchase::addFunds() {
	if (!idle() || !current() || !walletUsable()) {
		return;
	}
	const auto session = _session;
	const auto panel = Wallet::ShowWallet(session);
	Wallet::ShowWalletReceiveBox(session, panel->uiShow());
}

bool OnchainPurchase::current() const {
	return (_stage != Stage::Closed)
		&& _weakSession
		&& _show->valid()
		&& (&_show->session() == _session);
}

bool OnchainPurchase::idle() const {
	return (_stage == Stage::Estimating)
		|| (_stage == Stage::Assessing)
		|| (_stage == Stage::Ready);
}

bool OnchainPurchase::assessable() const {
	const auto memory = _memory.get();
	return Insufficient(_error)
		&& _spend.has_value()
		&& memory
		&& !memory->withdrawalDisabled
		&& !_earningsDone
		&& _show->canResolveWindow();
}

bool OnchainPurchase::offerable() const {
	return _earnings.has_value() && covers(*_earnings);
}

bool OnchainPurchase::covers(int64 earnings) const {
	return assessable()
		&& EarningsCoverShortfall(
			_price.current(),
			*_spend,
			_wallet->balanceNano(),
			earnings);
}

SendError OnchainPurchase::walletRefusal() const {
	switch (WalletReadiness(_wallet)) {
	case Readiness::Absent:
		return SendError::NoWallet;
	case Readiness::Unknown:
		return SendError::WalletNotReady;
	case Readiness::Ready:
		break;
	}
	const auto identity = _wallet->transferWalletIdentity();
	if (!identity) {
		return SendError::WalletNotReady;
	} else if (_identity && !SameWallet(*identity, *_identity)) {
		return SendError::KeyChanged;
	}
	return SendError::None;
}

bool OnchainPurchase::walletUsable() {
	const auto refusal = walletRefusal();
	if (refusal == SendError::NoWallet) {
		openMoney();
		return false;
	} else if (refusal == SendError::KeyChanged) {
		keyChanged();
		return false;
	} else if (refusal != SendError::None) {
		wait();
		return false;
	} else if (!_identity) {
		_identity = _wallet->transferWalletIdentity();
	}
	return true;
}

Completion OnchainPurchase::completion() const {
	return {
		.show = _show,
		.gift = _gift,
		.bought = _bought,
		.session = _weakSession,
		.identity = _identity,
	};
}

void OnchainPurchase::subscribe() {
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	_wallet->balanceNanoValue(
	) | rpl::skip(1) | rpl::filter([=] {
		return idle() || (_stage == Stage::Funding);
	}) | rpl::on_next([=] {
		_fee = std::nullopt;
		walletChanged();
	}, _lifetime);
	rpl::merge(
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->custodyUpdates(),
		_wallet->transferWalletIdentityChanges(),
		_wallet->sendStateValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->signingReadyValue() | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		walletUpdated();
	}, _lifetime);
	Lang::Updated() | rpl::on_next([=] {
		refresh();
	}, _lifetime);
}

void OnchainPurchase::walletUpdated() {
	if (idle() || _stage == Stage::Waiting) {
		walletChanged();
		return;
	} else if (_stage == Stage::Sending
		|| _stage == Stage::Tracking
		|| _stage == Stage::Closed) {
		return;
	} else if (!current()) {
		close();
		return;
	}
	const auto refusal = walletRefusal();
	if (refusal == SendError::NoWallet || refusal == SendError::KeyChanged) {
		refused(refusal);
	} else if (_stage == Stage::Signing
		&& refusal == SendError::None
		&& (_wallet->signingReady() || !_authorization.valid())) {
		prepare();
	}
}

void OnchainPurchase::walletChanged() {
	if (!current()) {
		close();
	} else if (walletUsable()) {
		preflight();
	}
}

void OnchainPurchase::wait() {
	_stage = Stage::Waiting;
	_error = SendError::None;
	_fee = std::nullopt;
	refresh();
	if (!_waitArmed) {
		_waitArmed = true;
		_waitTimer.callOnce(kWalletWait);
		_wallet->refreshState();
	} else if (!_waitTimer.isActive()) {
		openMoney();
	}
}

void OnchainPurchase::waited() {
	if (_stage == Stage::Waiting) {
		openMoney();
	} else if (_stage == Stage::Funding) {
		walletChanged();
	}
}

void OnchainPurchase::preflight() {
	if (_funding && !_waitArmed) {
		_waitTimer.cancel();
	}
	_stage = Stage::Estimating;
	_spend = std::nullopt;
	if (base::take(_offer)) {
		_error = SendError::None;
	}
	refresh();
	_wallet->estimateServed(
		_owner,
		Wallet::ServedOperation{
			.message = {
				.destination = _address,
				.value = _price.current(),
			},
		},
		crl::guard(this, [=](Wallet::FeeResult result) {
			preflighted(std::move(result));
		}));
}

void OnchainPurchase::preflighted(Wallet::FeeResult result) {
	if (_stage != Stage::Estimating) {
		return;
	}
	const auto error = result.error;
	if (error == SendError::NoWallet
		|| error == SendError::SigningUnavailable) {
		openMoney();
	} else if (error == SendError::WalletNotReady) {
		if (++_retries <= Wallet::kSendRefusalRetries) {
			wait();
		} else {
			openMoney();
		}
	} else if (error == SendError::QuoteExpired
		&& ++_retries <= Wallet::kSendRefusalRetries) {
		preflight();
	} else {
		const auto fee = (error == SendError::None)
			? std::make_optional(result.feeNano)
			: std::nullopt;
		const auto spend = Insufficient(error)
			? result.shortfallFeeNano
			: std::nullopt;
		settle(error, fee, spend);
	}
}

void OnchainPurchase::settle(
		SendError error,
		std::optional<int64> fee,
		std::optional<int64> spend) {
	_waitArmed = false;
	_waitTimer.cancel();
	_retries = 0;
	_stale = false;
	_error = error;
	_spend = spend;
	if (_funding && Insufficient(error)) {
		awaitFunds();
		return;
	}
	_funding = false;
	_fee = fee;
	if (assessable() && !_earnings) {
		_stage = Stage::Assessing;
		refresh();
		requestEarnings();
	} else {
		ready();
	}
}

void OnchainPurchase::requestEarnings() {
	if (!current()) {
		close();
		return;
	} else if (_earningsRequestId) {
		return;
	}
	using Flag = MTPpayments_getStarsRevenueStats::Flag;
	_earningsRequestId = _api.request(MTPpayments_GetStarsRevenueStats(
		MTP_flags(Flag::f_ton),
		MTP_inputPeerSelf()
	)).done([=](const MTPpayments_StarsRevenueStats &result) {
		_earningsRequestId = 0;
		earningsLoaded(AvailableEarnings(result));
	}).fail([=] {
		_earningsRequestId = 0;
		earningsLoaded(0);
	}).handleAllErrors().send();
}

void OnchainPurchase::earningsLoaded(int64 earnings) {
	if (_stage == Stage::Closed) {
		return;
	}
	_earnings = earnings;
	if (_stage != Stage::Assessing && _stage != Stage::Offering) {
		return;
	} else if (!current()) {
		close();
	} else if (_stage == Stage::Offering) {
		showOffer();
	} else {
		ready();
	}
}

void OnchainPurchase::ready() {
	_stage = Stage::Ready;
	_offer = offerable();
	if (_offer) {
		_fee = _spend;
	}
	refresh();
	if (const auto continuation = base::take(_afterPreflight)) {
		continuation();
	}
}

void OnchainPurchase::offer() {
	_stage = Stage::Offering;
	refresh();
	requestEarnings();
}

void OnchainPurchase::showOffer() {
	if (!offerable()) {
		recheck();
		return;
	}
	const auto earnings = *_earnings;
	const auto proceed = crl::guard(this, [=] {
		offerProceeded(earnings);
	});
	const auto cancel = crl::guard(this, [=] {
		offerCancelled();
	});
	_earningsBox = _show->show(MakeConfirmBox({
		.text = tr::lng_gift_buy_onchain_earnings_text(
			tr::now,
			lt_amount,
			tr::bold(PrepareGramAmountText(earnings)),
			tr::marked),
		.confirmed = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
			close();
			proceed();
		}),
		.cancelled = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
			close();
			cancel();
		}),
		.confirmText = tr::lng_gift_buy_onchain_earnings_proceed(),
		.title = tr::lng_gift_buy_onchain_earnings_title(),
	}));
	if (const auto box = _earningsBox.get()) {
		Wallet::CloseByOutsideClick(box);
	}
}

void OnchainPurchase::offerCancelled() {
	if (_stage != Stage::Offering) {
		return;
	}
	_earningsBox = nullptr;
	recheck();
}

void OnchainPurchase::offerProceeded(int64 earnings) {
	if (_stage != Stage::Offering) {
		return;
	}
	_earningsBox = nullptr;
	if (!current()) {
		close();
		return;
	}
	const auto window = covers(earnings) ? _show->resolveWindow() : nullptr;
	if (!window || (&window->session() != _session)) {
		recheck();
		return;
	}
	withdraw(window, earnings);
}

void OnchainPurchase::withdraw(
		not_null<Window::SessionController*> window,
		int64 earnings) {
	_stage = Stage::Withdrawing;
	refresh();
	const auto session = _weakSession;
	const auto memory = _memory;
	const auto done = crl::guard(this, [=] { withdrawn(); });
	const auto unavailable = crl::guard(this, [=] { earningsUnavailable(); });
	const auto ended = crl::guard(this, [=] { withdrawalEnded(); });
	Api::WithdrawEarningsToWallet(window, _box, earnings, {
		.withdrawn = [=] {
			if (const auto strong = session.get()) {
				strong->credits().tonLoad(true);
			}
			done();
		},
		.disabled = [=] {
			if (const auto strong = memory.get()) {
				strong->withdrawalDisabled = true;
			}
			unavailable();
		},
		.failed = ended,
		.dismissed = ended,
	});
}

void OnchainPurchase::withdrawn() {
	if (_stage != Stage::Withdrawing) {
		return;
	}
	_earningsDone = true;
	_earnings = std::nullopt;
	_funding = true;
	_stage = Stage::Funding;
	_afterPreflight = [=] { buy(); };
	_fee = std::nullopt;
	walletChanged();
}

void OnchainPurchase::awaitFunds() {
	_stage = Stage::Funding;
	_fee = _spend;
	refresh();
	_waitTimer.callOnce(kWalletWait);
}

void OnchainPurchase::earningsUnavailable() {
	if (_stage == Stage::Withdrawing) {
		abandon(_error);
	}
}

void OnchainPurchase::withdrawalEnded() {
	if (_stage == Stage::Withdrawing) {
		recheck();
	}
}

void OnchainPurchase::recheck() {
	stopAttempt();
	_fee = std::nullopt;
	walletChanged();
}

void OnchainPurchase::buy() {
	const auto fee = _fee.current();
	if (!current()) {
		close();
		return;
	} else if (_stage != Stage::Ready || _error != SendError::None || !fee) {
		stopAttempt();
		refresh();
		return;
	}
	const auto weak = base::make_weak(this);
	if (!walletUsable()) {
		if (weak && _stage != Stage::Closed) {
			stopAttempt();
			refresh();
		}
		return;
	}
	const auto funds = FundsRefusal(
		_price.current(),
		*fee,
		_wallet->balanceNano());
	if (funds != SendError::None) {
		abandon(funds);
		return;
	}
	request();
}

void OnchainPurchase::request() {
	const auto expected = _price.current();
	_stage = Stage::Buying;
	refresh();
	_requestId = _api.request(MTPpayments_BuyStarGiftOnchain(
		MTP_string(_gift->slug),
		MTP_long(expected)
	)).done([=](const MTPpayments_StarGiftOnchainSalePayload &result) {
		_requestId = 0;
		answered(result, expected);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (_stage != Stage::Buying) {
			return;
		} else if (error.type() == u"STARGIFT_ONCHAIN_PRICE_CHANGED"_q) {
			reread();
		} else {
			failed(error, u"payments.buyStarGiftOnchain"_q);
		}
	}).handleFloodErrors().send();
}

void OnchainPurchase::answered(
		const MTPpayments_StarGiftOnchainSalePayload &answer,
		int64 expected) {
	if (_stage != Stage::Buying) {
		return;
	}
	auto operation = Wallet::ServedOperationFromSale(answer);
	const auto &message = operation.message;
	if (message.value != expected || !message.body.isEmpty()) {
		LOG(("API Error: payments.buyStarGiftOnchain answered %1 "
			"for %2 with a %3-byte payload."
			).arg(message.value
			).arg(expected
			).arg(message.body.size()));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_served = std::move(operation);
	prepare();
}

void OnchainPurchase::reread() {
	if (!current()) {
		close();
		return;
	}
	_stage = Stage::Rereading;
	refresh();
	_requestId = _api.request(MTPpayments_GetUniqueStarGift(
		MTP_string(_gift->slug)
	)).done([=](const MTPpayments_UniqueStarGift &result) {
		_requestId = 0;
		reloaded(result);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (_stage == Stage::Rereading) {
			failed(error, u"payments.getUniqueStarGift"_q);
		}
	}).handleFloodErrors().send();
}

void OnchainPurchase::reloaded(const MTPpayments_UniqueStarGift &result) {
	if (_stage != Stage::Rereading) {
		return;
	}
	const auto weak = base::make_weak(this);
	const auto &data = result.data();
	_session->data().processUsers(data.vusers());
	_session->data().processChats(data.vchats());
	if (!weak || _stage != Stage::Rereading) {
		return;
	} else if (!current()) {
		close();
		return;
	}
	const auto parsed = Api::FromTL(_session, data.vgift());
	const auto unique = parsed ? parsed->unique : nullptr;
	if (!unique
		|| (unique->slug != _gift->slug)
		|| !unique->onchainSale
		|| (unique->nanoTonForResale <= 0)) {
		closeWithToast(tr::lng_gift_buy_onchain_not_for_sale(tr::now));
		return;
	}
	const auto price = unique->nanoTonForResale;
	const auto address = unique->giftAddress;
	_stage = Stage::Repricing;
	refresh();
	_priceBox = _show->show(MakeResaleGiftPriceChangeBox(
		CreditsAmount(0, price, CreditsType::Ton),
		crl::guard(this, [=] { repriced(price, address); }),
		crl::guard(this, [=] {
			if (_stage == Stage::Repricing) {
				close();
			}
		})));
	if (const auto box = _priceBox.get()) {
		Wallet::CloseByOutsideClick(box);
	}
}

void OnchainPurchase::failed(const MTP::Error &error, const QString &method) {
	if (MTP::IgnoreError(error)) {
		close();
		return;
	}
	const auto &type = error.type();
	const auto gone = std::array{
		u"STARGIFT_ONCHAIN_NOT_FOR_SALE"_q,
		u"STARGIFT_ONCHAIN_ITEM_NOT_FOUND"_q,
	};
	if (ranges::contains(gone, type)) {
		closeWithToast(tr::lng_gift_buy_onchain_not_for_sale(tr::now));
	} else {
		LOG(("API Error: %1 failed with %2.").arg(method, type));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainPurchase::repriced(int64 price, const QString &address) {
	if (_stage != Stage::Repricing) {
		return;
	}
	_priceBox = nullptr;
	_afterPreflight = [=] { buy(); };
	_address = address;
	_error = SendError::None;
	_fee = std::nullopt;
	_price = price;
	walletChanged();
}

void OnchainPurchase::prepare() {
	if (!current()) {
		close();
		return;
	}
	const auto refusal = walletRefusal();
	if (refusal != SendError::None) {
		refused(refusal);
		return;
	} else if (_authorization.valid() && !_wallet->signingReady()) {
		awaitSigning();
		return;
	}
	_stage = Stage::Preparing;
	_prepared = nullptr;
	refresh();
	_wallet->estimateServed(
		_owner,
		*_served,
		crl::guard(this, [=](Wallet::FeeResult result) {
			prepared(std::move(result));
		}));
}

void OnchainPurchase::prepared(Wallet::FeeResult result) {
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
	_fee = result.feeNano;
	const auto refusal = walletRefusal();
	const auto funds = FundsRefusal(
		_served->message.value,
		result.feeNano,
		_wallet->balanceNano());
	if (refusal != SendError::None) {
		refused(refusal);
	} else if (funds != SendError::None) {
		refused(funds);
	} else if (_authorization.valid()) {
		submit();
	} else {
		authorize();
	}
}

void OnchainPurchase::authorize() {
	_keyLifetime.destroy();
	_stage = Stage::Authorizing;
	refresh();
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

void OnchainPurchase::authorized(Wallet::KeyAuthorization auth) {
	if (_stage != Stage::Authorizing) {
		return;
	} else if (!auth.valid()) {
		stopAttempt();
		_fee = std::nullopt;
		walletChanged();
		return;
	}
	_authorization = std::move(auth);
	prepare();
}

void OnchainPurchase::awaitSigning() {
	_stage = Stage::Signing;
	refresh();
	if (!_signingArmed) {
		_signingArmed = true;
		_signingWait.callOnce(Wallet::kSigningReadyTimeout);
	} else if (!_signingWait.isActive()) {
		openMoney();
	}
}

void OnchainPurchase::signingWaited() {
	if (_stage == Stage::Signing) {
		openMoney();
	}
}

void OnchainPurchase::submit() {
	_stage = Stage::Sending;
	refresh();
	const auto weak = base::make_weak(this);
	const auto completion = this->completion();
	_wallet->sendServed(
		_authorization,
		base::take(_prepared),
		crl::guard(_session, [=](Wallet::ServedResult result) {
			const auto strong = weak.get();
			if (strong && strong->_stage == Stage::Sending) {
				strong->sent(std::move(result));
			} else if (result.outcome == Wallet::ServedOutcome::Accepted) {
				CompletePurchase(completion);
			}
		}));
}

void OnchainPurchase::sent(Wallet::ServedResult result) {
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
		LOG(("Wallet Error: the on-chain gift purchase was already sent."));
		closeWithToast(tr::lng_gift_buy_onchain_already_sent(tr::now));
		return;
	case ServedOutcome::Rejected:
		if (result.refusal == SendError::KeyChanged) {
			keyChanged();
		} else if (result.refusal == SendError::Silent) {
			close();
		} else {
			LOG(("API Error: wallet.sendTransfer for the on-chain gift "
				"purchase failed with %1.").arg(result.errorType));
			closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		}
		return;
	case ServedOutcome::Refused:
		refused(result.refusal);
		return;
	}
}

void OnchainPurchase::refused(SendError error) {
	if (error == SendError::KeyChanged) {
		keyChanged();
	} else if (error == SendError::NoWallet
		|| error == SendError::SigningUnavailable) {
		openMoney();
	} else if (error == SendError::WalletNotReady) {
		if (++_retries <= Wallet::kSendRefusalRetries) {
			awaitSigning();
		} else {
			openMoney();
		}
	} else if (error == SendError::QuoteExpired
		&& ++_retries <= Wallet::kSendRefusalRetries) {
		prepare();
	} else {
		abandon(error);
	}
}

void OnchainPurchase::abandon(SendError error) {
	stopAttempt();
	_stage = Stage::Ready;
	_error = error;
	_fee = std::nullopt;
	_offer = false;
	_stale = !Insufficient(error);
	refresh();
}

void OnchainPurchase::stopAttempt() {
	_signingWait.cancel();
	_signingArmed = false;
	if (base::take(_funding) && !_waitArmed) {
		_waitTimer.cancel();
	}
	_afterPreflight = nullptr;
	_served = std::nullopt;
	_prepared = nullptr;
	_authorization = {};
	_busy = false;
}

void OnchainPurchase::accepted() {
	const auto completion = this->completion();
	close();
	CompletePurchase(completion);
}

void OnchainPurchase::track(const std::string &operationId) {
	if (operationId.empty()) {
		LOG(("Wallet Error: the on-chain gift purchase is uncertain "
			"without an operation id."));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_stage = Stage::Tracking;
	_operationId = operationId;
	refresh();
	_wallet->historyUpdates() | rpl::on_next([=] {
		resolve();
	}, _trackLifetime);
	resolve();
}

void OnchainPurchase::resolve() {
	using Status = Wallet::TransferItem::Status;
	if (_stage != Stage::Tracking) {
		return;
	}
	const auto item = _wallet->trackedTransaction(_operationId);
	if (!item || item->status == Status::Pending) {
		return;
	} else if (item->status == Status::Success) {
		accepted();
	} else {
		LOG(("Wallet Error: the on-chain gift purchase transfer failed."));
		closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainPurchase::refresh() {
	const auto funds = Insufficient(_error);
	_short = funds
		&& !_offer
		&& !_funding
		&& (_stage != Stage::Assessing);
	_refusal = (funds || _error == SendError::None)
		? QString()
		: RefusalText(_error);
	_pressable = (_stage == Stage::Ready)
		&& !_busy.current()
		&& (_offer
			|| (!funds
				&& (_stale
					|| ((_error == SendError::None)
						&& _fee.current().has_value()))));
}

void OnchainPurchase::openMoney() {
	const auto session = _session;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWallet(session);
	}
}

void OnchainPurchase::keyChanged() {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWalletKeyChanged(show);
	}
}

void OnchainPurchase::closeWithToast(const QString &text) {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		show->showToast(text);
	}
}

void OnchainPurchase::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainPurchase::closing() {
	if (_stage == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	_trackLifetime.destroy();
	_waitTimer.cancel();
	_signingWait.cancel();
	_api.request(base::take(_requestId)).cancel();
	_api.request(base::take(_earningsRequestId)).cancel();
	if (_weakSession) {
		_wallet->cancelFeeEstimate(_owner);
	}
	stopPolling();
	_funding = false;
	_afterPreflight = nullptr;
	_served = std::nullopt;
	_prepared = nullptr;
	_authorization = {};
	const auto weak = base::make_weak(this);
	const auto earningsBox = _earningsBox;
	if (const auto strong = _priceBox.get()) {
		strong->closeBox();
	}
	if (const auto strong = earningsBox.get()) {
		strong->closeBox();
	}
	if (weak) {
		_keyLifetime.destroy();
	}
}

void OnchainPurchase::stopPolling() {
	if (base::take(_polling) && _weakSession) {
		_wallet->stopPolling();
	}
}

void OnchainBuyBox(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		base::weak_ptr<OnchainBuyMemory> memory,
		Fn<void()> bought) {
	const auto purchase = box->lifetime().make_state<OnchainPurchase>(
		box,
		show,
		gift,
		std::move(memory),
		std::move(bought));
	const auto &padding = st::boxPadding;
	const auto margins = QMargins(padding.left(), 0, padding.right(), 0);

	box->addRow(
		CreateGiftTransfer(
			box->verticalLayout(),
			gift,
			show->session().user()),
		QMargins(0, padding.top(), 0, 0));
	auto price = purchase->priceValue() | rpl::map([](int64 nanos) {
		return nanos / float64(kNanosInOne);
	});
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_gift_buy_resale_confirm_self(
				lt_name,
				rpl::single(tr::bold(Data::UniqueGiftName(*gift))),
				lt_price,
				tr::lng_action_gift_for_ton(
					lt_count_decimal,
					std::move(price),
					tr::bold),
				tr::marked),
			st::boxLabel),
		QMargins(padding.left(), padding.top(), padding.right(), 0));

	auto feeAmount = purchase->feeValue(
	) | rpl::filter([](std::optional<int64> fee) {
		return fee.has_value();
	}) | rpl::map([](std::optional<int64> fee) {
		return FormatTonAmount(*fee).full;
	});
	const auto fee = box->addRow(
		object_ptr<SlideWrap<FlatLabel>>(
			box,
			object_ptr<FlatLabel>(
				box,
				tr::lng_wallet_send_network_fee(
					lt_amount,
					std::move(feeAmount)),
				st::walletSendUserBalanceLabel)),
		margins,
		style::al_justify);
	fee->toggleOn(purchase->feeValue(
	) | rpl::map([](std::optional<int64> fee) {
		return fee.has_value();
	}));
	fee->finishAnimating();

	const auto shortage = box->addRow(
		object_ptr<SlideWrap<VerticalLayout>>(
			box,
			object_ptr<VerticalLayout>(box)),
		margins,
		style::al_justify);
	const auto inner = shortage->entity();
	inner->add(
		object_ptr<FlatLabel>(
			inner,
			tr::lng_wallet_send_error_insufficient(),
			st::walletSendUserErrorLabel),
		style::al_justify);
	const auto deposit = inner->add(
		object_ptr<RoundButton>(
			inner,
			tr::lng_wallet_add_funds(),
			st::defaultTableSmallButton),
		style::margins(0, st::walletDetailsAmountMinorSkip, 0, 0),
		style::al_top);
	deposit->setTextTransform(RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback(crl::guard(purchase, [=] {
		purchase->addFunds();
	}));
	shortage->toggleOn(purchase->shortValue());
	shortage->finishAnimating();

	const auto refusal = box->addRow(
		object_ptr<SlideWrap<FlatLabel>>(
			box,
			object_ptr<FlatLabel>(
				box,
				purchase->refusalValue(),
				st::walletSendUserErrorLabel)),
		margins,
		style::al_justify);
	refusal->toggleOn(purchase->refusalValue(
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));
	refusal->finishAnimating();
	box->addSkip(padding.bottom());

	const auto buy = box->addButton(
		rpl::single(QString()),
		crl::guard(purchase, [=] { purchase->press(); })).data();
	buy->setText(purchase->priceValue(
	) | rpl::map([](int64 nanos) {
		return tr::lng_gift_buy_resale_button(
			lt_cost,
			rpl::single(GramCost(nanos)),
			tr::marked);
	}) | rpl::flatten_latest());
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		buy,
		buy->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(buy, loading);
	loading->showOn(purchase->busyValue());
	rpl::combine(
		purchase->busyValue(),
		purchase->pressableValue(),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::on_next([=](bool busy, bool pressable, rpl::empty_value) {
		buy->setDisabled(busy || !pressable);
		buy->setTextFgOverride(busy
			? std::make_optional(QColor(Qt::transparent))
			: pressable
			? std::optional<QColor>()
			: std::make_optional(st::windowSubTextFg->c));
	}, buy->lifetime());
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	Wallet::SubmitBoxOnEnter(box, crl::guard(purchase, [=] {
		purchase->press();
	}));
	purchase->start();
}

} // namespace

bool CanBuyOnchainGift(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	return gift.onchainSale
		&& (gift.nanoTonForResale > 0)
		&& !gift.slug.isEmpty()
		&& (gift.ownerId != session->userPeerId())
		&& session->appConfig().walletAvailable()
		&& !session->supportMode();
}

void ShowBuyOnchainGiftBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		base::weak_ptr<OnchainBuyMemory> memory,
		Fn<void()> bought) {
	if (!show
		|| !show->valid()
		|| !gift
		|| !CanBuyOnchainGift(&show->session(), *gift)) {
		return;
	}
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	wallet->ensureLoaded();
	if (WalletReadiness(wallet) == Readiness::Absent) {
		Wallet::ShowWallet(session);
		return;
	}
	show->show(Box(
		OnchainBuyBox,
		show,
		std::move(gift),
		std::move(memory),
		std::move(bought)));
}

} // namespace Ui
