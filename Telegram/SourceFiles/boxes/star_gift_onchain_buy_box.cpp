/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_buy_box.h"

#include "api/api_earn.h"
#include "api/api_premium.h"
#include "api/api_text_entities.h"
#include "base/timer.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_onchain_sale_box.h"
#include "boxes/star_gift_onchain_signing.h"
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
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

using Wallet::SendError;

constexpr auto kWalletWait = 15 * crl::time(1000);
constexpr auto kFixedSaleGasAllowance = int64(100'000'000);

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
	Signing,
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
	PeerId recipientId;
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
		not_null<PeerData*> to,
		base::weak_ptr<OnchainBuyMemory> memory,
		ResaleGiftMessage message,
		Fn<void()> bought);
	~OnchainPurchase();

	[[nodiscard]] rpl::producer<int64> priceValue() const;
	[[nodiscard]] rpl::producer<int64> totalValue() const;
	[[nodiscard]] rpl::producer<int64> gasValue() const;
	[[nodiscard]] rpl::producer<bool> fixedValue() const;
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
	[[nodiscard]] bool usable(OnchainSigningCheck check);
	[[nodiscard]] SendError estimated(const Wallet::FeeResult &result);
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
	void fixedAnswered(Wallet::ServedOperation operation);
	void reread();
	void reloaded(const MTPpayments_UniqueStarGift &result);
	void failed(const MTP::Error &error, const QString &method);
	void repriced(const Data::UniqueGift &gift);
	void sign();
	void refused(SendError error);
	void dismissed();
	void duplicate();
	void abandon(SendError error);
	void stopAttempt();
	void refresh();
	void closing();
	void stopPolling();

	const not_null<GenericBox*> _box;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::shared_ptr<Data::UniqueGift> _gift;
	const not_null<PeerData*> _to;
	const ResaleGiftMessage _message;
	const Fn<void()> _bought;
	const base::weak_ptr<OnchainBuyMemory> _memory;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	MTP::Sender _api;
	base::Timer _waitTimer;
	QString _address;
	std::optional<Wallet::TransferWalletIdentity> _identity;
	std::optional<Wallet::ServedOperation> _served;
	base::weak_qptr<GenericBox> _priceBox;
	base::weak_qptr<GenericBox> _earningsBox;
	Fn<void()> _afterPreflight;
	std::optional<int64> _spend;
	std::optional<int64> _earnings;
	rpl::variable<int64> _price;
	rpl::variable<int64> _total;
	rpl::variable<int64> _gas;
	rpl::variable<bool> _fixed;
	rpl::variable<std::optional<int64>> _fee;
	rpl::variable<QString> _refusal;
	rpl::variable<bool> _short = false;
	rpl::variable<bool> _busy = false;
	rpl::variable<bool> _pressable = false;
	SendError _error = SendError::None;
	Stage _stage = Stage::Waiting;
	mtpRequestId _requestId = 0;
	mtpRequestId _earningsRequestId = 0;
	int _retries = 0;
	bool _waitArmed = false;
	bool _stale = false;
	bool _polling = false;
	bool _offer = false;
	bool _earningsDone = false;
	bool _funding = false;
	rpl::lifetime _lifetime;
	OnchainSigning _signing;

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
	} else if (completion.recipientId) {
		const auto recipient = session->data().peer(completion.recipientId);
		show->showToast(tr::lng_gift_buy_onchain_done_recipient(
			tr::now,
			lt_user,
			tr::bold(recipient->shortName()),
			tr::marked));
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
	not_null<PeerData*> to,
	base::weak_ptr<OnchainBuyMemory> memory,
	ResaleGiftMessage message,
	Fn<void()> bought)
: _box(box)
, _show(std::move(show))
, _gift(std::move(gift))
, _to(to)
, _message(std::move(message))
, _bought(std::move(bought))
, _memory(std::move(memory))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _api(&_session->mtp())
, _waitTimer([=] { waited(); })
, _address(_gift->giftAddress)
, _price(_gift->nanoTonForResale)
, _total(_gift->nanoTonForResale)
, _fixed(_gift->fixedSale)
, _signing({
	.box = box,
	.show = _show,
	.order = OnchainSigningOrder::SamePress,
	.route = OnchainSigningRoute::Money,
	.wait = OnchainSigningWait::Attempt,
	.current = [=] { return current(); },
	.usable = [=](OnchainSigningCheck check) { return usable(check); },
	.estimated = [=](const Wallet::FeeResult &result) {
		return estimated(result);
	},
	.error = tr::lng_gift_buy_onchain_error,
	.subject = u"the on-chain gift purchase"_q,
	.failedSubject = u"the on-chain gift purchase transfer"_q,
	.transfer = u"the on-chain gift purchase"_q,
	.refused = [=](SendError error) { refused(error); },
	.dismissed = [=] { dismissed(); },
	.duplicate = [=] { duplicate(); },
	.completion = [=]() -> Fn<void()> {
		return [data = completion()] {
			CompletePurchase(data);
		};
	},
	.closing = [=] { closing(); },
}) {
}

OnchainPurchase::~OnchainPurchase() {
	_signing.closing();
}

rpl::producer<int64> OnchainPurchase::priceValue() const {
	return _price.value();
}

rpl::producer<int64> OnchainPurchase::totalValue() const {
	return _total.value();
}

rpl::producer<int64> OnchainPurchase::gasValue() const {
	return _gas.value();
}

rpl::producer<bool> OnchainPurchase::fixedValue() const {
	return _fixed.value();
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
	_signing.start();
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
			_total.current(),
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
		_signing.openMoney();
		return false;
	} else if (refusal == SendError::KeyChanged) {
		_signing.keyChanged();
		return false;
	} else if (refusal != SendError::None) {
		wait();
		return false;
	} else if (!_identity) {
		_identity = _wallet->transferWalletIdentity();
	}
	return true;
}

bool OnchainPurchase::usable(OnchainSigningCheck check) {
	if (check != OnchainSigningCheck::Estimate) {
		return true;
	} else if (!current()) {
		_signing.close();
		return false;
	}
	const auto refusal = walletRefusal();
	if (refusal != SendError::None) {
		_signing.refused(refusal);
		return false;
	}
	return true;
}

SendError OnchainPurchase::estimated(const Wallet::FeeResult &result) {
	_fee = result.feeNano;
	const auto refusal = walletRefusal();
	const auto funds = FundsRefusal(
		_served->messages.front().value,
		result.feeNano,
		_wallet->balanceNano());
	return (refusal != SendError::None) ? refusal : funds;
}

Completion OnchainPurchase::completion() const {
	return {
		.show = _show,
		.gift = _gift,
		.recipientId = _to->isSelf() ? PeerId() : _to->id,
		.bought = _bought,
		.session = _weakSession,
		.identity = _identity,
	};
}

void OnchainPurchase::subscribe() {
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
	} else if (_signing.submitted() || _stage == Stage::Closed) {
		return;
	} else if (!current()) {
		_signing.close();
		return;
	}
	const auto refusal = walletRefusal();
	if (refusal == SendError::NoWallet || refusal == SendError::KeyChanged) {
		_signing.refused(refusal);
	} else if (_stage == Stage::Signing && refusal == SendError::None) {
		_signing.resume();
	}
}

void OnchainPurchase::walletChanged() {
	if (!current()) {
		_signing.close();
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
		_signing.openMoney();
	}
}

void OnchainPurchase::waited() {
	if (_stage == Stage::Waiting) {
		_signing.openMoney();
	} else if (_stage == Stage::Funding) {
		walletChanged();
	}
}

void OnchainPurchase::preflight() {
	Expects(_identity.has_value());

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
		_signing.owner(),
		Wallet::ServedOperation{
			.messages = { Wallet::ServedMessage{
				.destination = (_address.isEmpty()
					? _identity->address
					: _address),
				.value = _total.current(),
			} },
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
		_signing.openMoney();
	} else if (error == SendError::WalletNotReady) {
		if (++_retries <= Wallet::kSendRefusalRetries) {
			wait();
		} else {
			_signing.openMoney();
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
		_signing.close();
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
		_signing.close();
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
		_signing.close();
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
		_signing.close();
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
		_total.current(),
		*fee,
		_wallet->balanceNano());
	if (funds != SendError::None) {
		abandon(funds);
	} else if (_served) {
		sign();
	} else {
		request();
	}
}

void OnchainPurchase::request() {
	using Flag = MTPpayments_BuyStarGiftOnchain::Flag;
	const auto forRecipient = !_to->isSelf();
	const auto expected = _price.current();
	const auto &message = _message.text;
	_stage = Stage::Buying;
	refresh();
	_requestId = _api.request(MTPpayments_BuyStarGiftOnchain(
		MTP_flags((forRecipient ? Flag::f_peer : Flag())
			| (message.empty() ? Flag() : Flag::f_message)
			| (_message.hideName ? Flag() : Flag::f_show_name)),
		MTP_string(_gift->slug),
		MTP_long(expected),
		forRecipient ? _to->input() : MTPInputPeer(),
		MTP_textWithEntities(
			MTP_string(message.text),
			Api::EntitiesToMTP(
				_session,
				message.entities,
				Api::ConvertOption::SkipLocal))
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
	if (answer.type() == mtpc_payments_starGiftOnchainFixedSalePayload) {
		fixedAnswered(std::move(operation));
		return;
	}
	const auto amount = operation.messages.front().value;
	if (amount != expected) {
		LOG(("API Error: payments.buyStarGiftOnchain answered %1 for %2."
			).arg(amount).arg(expected));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_served = std::move(operation);
	sign();
}

void OnchainPurchase::fixedAnswered(Wallet::ServedOperation operation) {
	const auto &messages = operation.messages;
	const auto &gasless = operation.gaslessMessages;
	const auto value = messages.empty() ? int64(0) : messages.front().value;
	if (messages.size() != 1 || !gasless.empty() || value <= 0) {
		LOG(("API Error: payments.buyStarGiftOnchain answered a fixed sale "
			"with %1 messages and %2 gasless, valued %3."
			).arg(messages.size()
			).arg(gasless.size()
			).arg(value));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	const auto price = _price.current();
	_served = std::move(operation);
	_total = value;
	_gas = std::max(value - price, int64(0));
	if (value - price <= kFixedSaleGasAllowance) {
		sign();
		return;
	}
	_afterPreflight = nullptr;
	_busy = false;
	_fee = std::nullopt;
	walletChanged();
}

void OnchainPurchase::reread() {
	if (!current()) {
		_signing.close();
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
		_signing.close();
		return;
	}
	const auto parsed = Api::FromTL(_session, data.vgift());
	const auto unique = parsed ? parsed->unique : nullptr;
	if (!unique
		|| (unique->slug != _gift->slug)
		|| !CanBuyOnchainGift(_to, *unique)) {
		_signing.closeWithToast(
			tr::lng_gift_buy_onchain_not_for_sale(tr::now));
		return;
	}
	_stage = Stage::Repricing;
	refresh();
	_priceBox = _show->show(MakeResaleGiftPriceChangeBox(
		Data::UniqueGiftResaleTon(*unique),
		crl::guard(this, [=] { repriced(*unique); }),
		crl::guard(this, [=] {
			if (_stage == Stage::Repricing) {
				_signing.close();
			}
		})));
	if (const auto box = _priceBox.get()) {
		Wallet::CloseByOutsideClick(box);
	}
}

void OnchainPurchase::failed(const MTP::Error &error, const QString &method) {
	if (MTP::IgnoreError(error)) {
		_signing.close();
		return;
	}
	const auto &type = error.type();
	const auto gone = std::array{
		u"STARGIFT_ONCHAIN_NOT_FOR_SALE"_q,
		u"STARGIFT_ONCHAIN_ITEM_NOT_FOUND"_q,
	};
	const auto inactive = _fixed.current()
		&& (type == u"STARGIFT_ONCHAIN_SALE_INACTIVE"_q);
	if (current() && ShowGiftErrorToast(_show, error)) {
		_signing.close();
	} else if (type == u"USER_DISALLOWED_STARGIFTS"_q) {
		_signing.closeWithToast(
			tr::lng_edit_privacy_gifts_restricted(tr::now));
	} else if (inactive || ranges::contains(gone, type)) {
		_signing.closeWithToast(
			tr::lng_gift_buy_onchain_not_for_sale(tr::now));
	} else {
		LOG(("API Error: %1 failed with %2.").arg(method, type));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainPurchase::repriced(const Data::UniqueGift &gift) {
	if (_stage != Stage::Repricing) {
		return;
	}
	_priceBox = nullptr;
	_afterPreflight = [=] { buy(); };
	_address = gift.giftAddress;
	_served = std::nullopt;
	_error = SendError::None;
	_fee = std::nullopt;
	_fixed = gift.fixedSale;
	_price = gift.nanoTonForResale;
	_total = gift.nanoTonForResale;
	_gas = 0;
	walletChanged();
}

void OnchainPurchase::sign() {
	_stage = Stage::Signing;
	refresh();
	_signing.sign(*_served);
}

void OnchainPurchase::refused(SendError error) {
	_retries = _signing.retries();
	abandon(error);
}

void OnchainPurchase::dismissed() {
	_retries = _signing.retries();
	stopAttempt();
	_fee = std::nullopt;
	walletChanged();
}

void OnchainPurchase::duplicate() {
	LOG(("Wallet Error: the on-chain gift purchase was already sent."));
	_signing.closeWithToast(tr::lng_gift_buy_onchain_already_sent(tr::now));
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
	_signing.stop();
	if (base::take(_funding) && !_waitArmed) {
		_waitTimer.cancel();
	}
	_afterPreflight = nullptr;
	if (base::take(_served)) {
		_total = _price.current();
		_gas = 0;
		_fee = std::nullopt;
		_spend = std::nullopt;
	}
	_busy = false;
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

void OnchainPurchase::closing() {
	_stage = Stage::Closed;
	_waitTimer.cancel();
	_api.request(base::take(_requestId)).cancel();
	_api.request(base::take(_earningsRequestId)).cancel();
	stopPolling();
	_funding = false;
	_afterPreflight = nullptr;
	_served = std::nullopt;
	const auto earningsBox = _earningsBox;
	if (const auto strong = _priceBox.get()) {
		strong->closeBox();
	}
	if (const auto strong = earningsBox.get()) {
		strong->closeBox();
	}
}

void OnchainPurchase::stopPolling() {
	if (base::take(_polling) && _weakSession) {
		_wallet->stopPolling();
	}
}

void AddPurchaseTerms(
		not_null<GenericBox*> box,
		not_null<OnchainPurchase*> purchase) {
	const auto terms = box->addRow(
		object_ptr<SlideWrap<TableLayout>>(
			box,
			object_ptr<TableLayout>(box, st::giveawayGiftCodeTable),
			st::giveawayGiftCodeTableMargin),
		style::margins());
	const auto table = terms->entity();
	auto helper = Text::CustomEmojiHelper();
	const auto mark = Wallet::GramMark(
		helper,
		st::giveawayGiftMessage.style.font);
	const auto context = helper.context();
	const auto gram = [=](rpl::producer<int64> value) {
		return object_ptr<FlatLabel>(
			table,
			std::move(value) | rpl::map([=](int64 nanos) {
				auto result = mark;
				result.append(QChar(' ')).append(Data::FormatGiftGramAmount(nanos));
				return result;
			}),
			st::giveawayGiftMessage,
			st::defaultPopupMenu,
			context);
	};
	AddTableRow(
		table,
		tr::lng_gift_resale_price(),
		gram(purchase->priceValue()));
	AddTableRow(
		table,
		tr::lng_gift_buy_onchain_total(),
		gram(purchase->totalValue()));
	const auto gas = table->lifetime().make_state<RpWidget*>(nullptr);
	purchase->gasValue() | rpl::on_next([=](int64 nanos) {
		if (nanos > 0 && !*gas) {
			auto label = object_ptr<FlatLabel>(
				table,
				tr::lng_gift_buy_onchain_gas(),
				table->st().defaultLabel);
			*gas = label.data();
			table->insertRow(
				table->rowsCount() - 1,
				std::move(label),
				gram(purchase->gasValue()),
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		} else if (nanos <= 0 && *gas) {
			delete base::take(*gas);
		}
	}, table->lifetime());
	terms->toggleOn(purchase->fixedValue());
	terms->finishAnimating();
}

void OnchainBuyBox(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		not_null<PeerData*> to,
		base::weak_ptr<OnchainBuyMemory> memory,
		ResaleGiftMessage message,
		Fn<void()> bought) {
	const auto purchase = box->lifetime().make_state<OnchainPurchase>(
		box,
		show,
		gift,
		to,
		std::move(memory),
		std::move(message),
		std::move(bought));
	const auto &padding = st::boxPadding;
	const auto margins = QMargins(padding.left(), 0, padding.right(), 0);

	box->addRow(
		CreateGiftTransfer(
			box->verticalLayout(),
			gift,
			to),
		QMargins(0, padding.top(), 0, 0));
	auto price = rpl::combine(
		purchase->totalValue(),
		rpl::single(rpl::empty) | rpl::then(Lang::Updated())
	) | rpl::map([](int64 nanos, rpl::empty_value) {
		return tr::bold(Data::FormatGiftGramAmountWithUnit(nanos));
	});
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			to->isSelf()
				? tr::lng_gift_buy_resale_confirm_self(
					lt_name,
					rpl::single(tr::bold(Data::UniqueGiftName(*gift))),
					lt_price,
					std::move(price),
					tr::marked)
				: tr::lng_gift_buy_resale_confirm(
					lt_name,
					rpl::single(tr::bold(Data::UniqueGiftName(*gift))),
					lt_price,
					std::move(price),
					lt_user,
					rpl::single(tr::bold(to->shortName())),
					tr::marked),
			st::boxLabel),
		QMargins(padding.left(), padding.top(), padding.right(), 0));
	AddPurchaseTerms(box, purchase);

	auto feeAmount = purchase->feeValue(
	) | rpl::filter([](std::optional<int64> fee) {
		return fee.has_value();
	}) | rpl::map([](std::optional<int64> fee) {
		return Data::FormatGiftGramAmount(*fee);
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
	buy->setText(purchase->totalValue(
	) | rpl::map([](int64 nanos) {
		return tr::lng_gift_buy_resale_button(
			lt_cost,
			rpl::single(Data::FormatGiftResaleTon(nanos)),
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

[[nodiscard]] bool ReadyToOpenPurchase(
		const std::shared_ptr<ChatHelpers::Show> &show,
		const std::shared_ptr<Data::UniqueGift> &gift,
		not_null<PeerData*> to) {
	if (!show
		|| !show->valid()
		|| !gift
		|| !CanBuyOnchainGift(to, *gift)) {
		return false;
	}
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	wallet->ensureLoaded();
	if (WalletReadiness(wallet) == Readiness::Absent) {
		Wallet::ShowWallet(session);
		return false;
	}
	return true;
}

} // namespace

OnchainBuyRefusal LookupOnchainBuyRefusal(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	using Refusal = OnchainBuyRefusal;
	if (gift.fixedSale && !gift.onchainSale) {
		return Refusal::SaleFlagsInvalid;
	} else if (!Data::UniqueGiftOnchainPurchase(gift)) {
		return Refusal::NotOnchainLot;
	} else if (!session->appConfig().walletAvailable()) {
		return Refusal::WalletUnavailable;
	} else if (session->supportMode()) {
		return Refusal::SupportMode;
	} else if (gift.slug.isEmpty()) {
		return Refusal::SlugMissing;
	} else if ((gift.ownerId == session->userPeerId())
		|| (LookupOwnOnchainGift(session, gift) != OwnOnchainGift::None)) {
		return Refusal::OwnGift;
	}
	return Refusal::None;
}

QString OnchainBuyRefusalText(OnchainBuyRefusal refusal) {
	using Refusal = OnchainBuyRefusal;
	const auto code = [&] {
		switch (refusal) {
		case Refusal::None:
		case Refusal::WalletUnavailable: return QString();
		case Refusal::SaleFlagsInvalid: return u"GIFT_BUY_FLAGS_INVALID"_q;
		case Refusal::NotOnchainLot: return u"GIFT_BUY_NOT_ONCHAIN"_q;
		case Refusal::SupportMode: return u"GIFT_BUY_SUPPORT_MODE"_q;
		case Refusal::SlugMissing: return u"GIFT_BUY_SLUG_EMPTY"_q;
		case Refusal::OwnGift: return u"GIFT_BUY_OWN_GIFT"_q;
		}
		Unexpected("Refusal in OnchainBuyRefusalText.");
	}();
	if (refusal == Refusal::None) {
		return QString();
	} else if (refusal == Refusal::WalletUnavailable) {
		return tr::lng_wallet_unavailable(tr::now);
	}
	return Wallet::ErrorWithType(
		tr::lng_gift_buy_onchain_error(tr::now),
		code);
}

bool CanBuyOnchainGift(
		not_null<PeerData*> recipient,
		const Data::UniqueGift &gift) {
	return (recipient->isSelf() || gift.fixedSale)
		&& (LookupOnchainBuyRefusal(&recipient->session(), gift)
			== OnchainBuyRefusal::None);
}

void ShowBuyOnchainGiftBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		not_null<PeerData*> to,
		base::weak_ptr<OnchainBuyMemory> memory,
		Fn<void()> bought) {
	if (!ReadyToOpenPurchase(show, gift, to)) {
		return;
	}
	const auto cost = Data::FormatGiftResaleTon(*gift);
	ShowResaleGiftComposer(
		show,
		gift,
		to,
		cost,
		[=](not_null<GenericBox*> composer, ResaleGiftMessage message)
		-> base::weak_qptr<GenericBox> {
			if (!ReadyToOpenPurchase(show, gift, to)) {
				return nullptr;
			}
			const auto weak = base::make_weak(composer);
			const auto done = [=] {
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
				if (bought) {
					bought();
				}
			};
			return show->show(Box(
				OnchainBuyBox,
				show,
				gift,
				to,
				memory,
				std::move(message),
				done));
		});
}

} // namespace Ui
