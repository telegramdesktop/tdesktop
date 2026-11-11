/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_onchain_offer_box.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/gift_premium_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/history.h"
#include "history/history_gift_onchain_offer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/table_rows.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"
#include "wallet/wallet_unlock.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace {

constexpr auto kBasisPointsInOne = 10000;
constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);

using SendError = Wallet::SendError;

struct OfferTerms {
	FullMsgId itemId;
	uint64 offerId = 0;
	std::shared_ptr<Data::UniqueGift> gift;
	int64 amount = 0;
	int feeBps = 0;
	TimeId expiresAt = 0;
	int limitedCount = 0;
	int limitedLeft = 0;
};

enum class SalePhase : uchar {
	Review,
	Preparing,
	Confirm,
	Insufficient,
	Signing,
	Sending,
	Declining,
	Done,
};

class SaleFlow final : public base::has_weak_ptr {
public:
	SaleFlow(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		OfferTerms terms);
	~SaleFlow();

	[[nodiscard]] rpl::producer<SalePhase> phaseValue() const;
	[[nodiscard]] rpl::producer<int64> feeValue() const;
	[[nodiscard]] bool closable() const;

	void sell();
	void decline();

private:
	[[nodiscard]] bool cardCurrent() const;
	[[nodiscard]] bool current() const;
	void declineConfirmed(Fn<void()> close);
	void declined();
	void declineFailed(const MTP::Error &error);
	void waitWallet();
	void walletChanged();
	void walletReady();
	void stopWaiting();
	void accept();
	void accepted(const MTPpayments_StarGiftOnchainOfferTx &result);
	void acceptFailed(const MTP::Error &error);
	void estimate();
	void estimated(Wallet::FeeResult result);
	[[nodiscard]] bool retry();
	void acquireKey();
	void keyReady(Wallet::KeyAuthorization auth);
	void submit();
	void awaitSigning();
	void sent(Wallet::ServedResult result);
	void refused(SendError error);
	void fail(const QString &diagnostic, bool keepAnswer);
	void toReview();
	void release();
	void finish(const QString &text);
	void closeBoxes();
	void stop();

	const not_null<Ui::GenericBox*> _box;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	const OfferTerms _terms;
	rpl::variable<SalePhase> _phase = SalePhase::Review;
	SalePhase _declineFrom = SalePhase::Review;
	base::weak_qptr<Ui::GenericBox> _declineBox;
	rpl::variable<std::optional<int64>> _fee;
	std::optional<Wallet::ServedOperation> _operation;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	TimeId _answerExpiresAt = 0;
	mtpRequestId _acceptRequestId = 0;
	uint64 _previewOwner = 0;
	int _refusals = 0;
	bool _polling = false;
	base::Timer _waitTimer;
	base::Timer _signingWait;
	rpl::lifetime _waitLifetime;
	rpl::lifetime _signingLifetime;
	rpl::lifetime _previewLifetime;
	rpl::lifetime _lifetime;
	// Last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

[[nodiscard]] std::optional<OfferTerms> LookupOfferTerms(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto item = session->data().message(itemId);
	if (!item
		|| !IsServerMsgId(itemId.msg)
		|| !item->history()->peer->isNotificationsUser()) {
		return std::nullopt;
	}
	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	if (!offer
		|| !GiftOnchainOfferReviewable(offer, base::unixtime::now())) {
		return std::nullopt;
	}
	return OfferTerms{
		.itemId = itemId,
		.offerId = offer->offerId,
		.gift = offer->gift,
		.amount = offer->amount,
		.feeBps = offer->feeBps,
		.expiresAt = offer->expiresAt,
		.limitedCount = offer->limitedCount,
		.limitedLeft = offer->limitedLeft,
	};
}

[[nodiscard]] bool SameTerms(const OfferTerms &a, const OfferTerms &b) {
	return (a.offerId == b.offerId)
		&& (a.gift->id == b.gift->id)
		&& (a.amount == b.amount)
		&& (a.feeBps == b.feeBps);
}

[[nodiscard]] bool ValidTerms(const OfferTerms &terms) {
	return (terms.amount >= 0)
		&& (terms.feeBps >= 0)
		&& (terms.feeBps <= kBasisPointsInOne);
}

[[nodiscard]] int64 ReceiveAmount(const OfferTerms &terms) {
	const auto whole = (terms.amount / kBasisPointsInOne) * terms.feeBps;
	const auto part = (terms.amount % kBasisPointsInOne) * terms.feeBps;
	return terms.amount - (whole + part / kBasisPointsInOne);
}

[[nodiscard]] bool SaleIdle(SalePhase phase) {
	return (phase == SalePhase::Review)
		|| (phase == SalePhase::Confirm)
		|| (phase == SalePhase::Insufficient);
}

[[nodiscard]] bool SaleBusy(SalePhase phase) {
	return (phase == SalePhase::Preparing)
		|| (phase == SalePhase::Signing)
		|| (phase == SalePhase::Sending);
}

[[nodiscard]] bool SaleFeeShown(SalePhase phase) {
	return (phase == SalePhase::Confirm)
		|| (phase == SalePhase::Signing)
		|| (phase == SalePhase::Sending);
}

SaleFlow::SaleFlow(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	OfferTerms terms)
: _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _terms(std::move(terms))
, _waitTimer([=] {
	fail(u"Wallet Error: Gift offer sale timed out on the wallet."_q, true);
})
, _signingWait([=] {
	fail(u"Wallet Error: Gift offer sale timed out on signing."_q, true);
}) {
	_box->boxClosing() | rpl::on_next([=] {
		stop();
	}, _lifetime);
	_session->data().itemRemoved(_terms.itemId) | rpl::on_next([=] {
		closeBoxes();
	}, _lifetime);
}

SaleFlow::~SaleFlow() {
	stop();
}

rpl::producer<SalePhase> SaleFlow::phaseValue() const {
	return _phase.value() | rpl::filter([](SalePhase phase) {
		return (phase != SalePhase::Declining) && (phase != SalePhase::Done);
	});
}

rpl::producer<int64> SaleFlow::feeValue() const {
	return _fee.value() | rpl::filter_optional();
}

bool SaleFlow::closable() const {
	return (_phase.current() != SalePhase::Sending);
}

void SaleFlow::sell() {
	const auto phase = _phase.current();
	if (!SaleIdle(phase)) {
		return;
	} else if (!current()) {
		closeBoxes();
		return;
	}
	_refusals = 0;
	if (phase == SalePhase::Confirm) {
		_phase = SalePhase::Signing;
		estimate();
	} else {
		_phase = SalePhase::Preparing;
		waitWallet();
	}
}

void SaleFlow::decline() {
	if (_declineBox || !SaleIdle(_phase.current())) {
		return;
	}
	const auto weak = base::make_weak(this);
	_declineBox = _show->show(Ui::MakeConfirmBox({
		.text = tr::lng_gift_onchain_offer_decline_sure(),
		.confirmed = [=](Fn<void()> close) {
			if (const auto strong = weak.get()) {
				strong->declineConfirmed(std::move(close));
			}
		},
		.confirmText = tr::lng_suggest_action_decline(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

bool SaleFlow::cardCurrent() const {
	const auto fresh = LookupOfferTerms(_session, _terms.itemId);
	return fresh && SameTerms(*fresh, _terms);
}

bool SaleFlow::current() const {
	return cardCurrent()
		&& (!_operation || base::unixtime::now() < _answerExpiresAt);
}

void SaleFlow::declineConfirmed(Fn<void()> close) {
	const auto phase = _phase.current();
	if (!SaleIdle(phase)) {
		return;
	} else if (!cardCurrent()) {
		closeBoxes();
		return;
	}
	_declineFrom = phase;
	_phase = SalePhase::Declining;
	const auto session = _session;
	const auto weak = base::make_weak(this);
	session->api().request(MTPpayments_DeclineStarGiftOnchainOffer(
		MTP_int(_terms.itemId.msg.bare)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		close();
		if (const auto strong = weak.get()) {
			strong->declined();
		}
	}).fail([=](const MTP::Error &error) {
		if (!MTP::IgnoreError(error)) {
			LOG(("API Error: %1 failed with %2."
				).arg(u"payments.declineStarGiftOnchainOffer"_q
				).arg(error.type()));
		}
		if (const auto strong = weak.get()) {
			strong->declineFailed(error);
		}
	}).handleFloodErrors().send();
}

void SaleFlow::declined() {
	if (_phase.current() == SalePhase::Declining) {
		closeBoxes();
	}
}

void SaleFlow::declineFailed(const MTP::Error &error) {
	if (_phase.current() != SalePhase::Declining) {
		return;
	}
	_phase = _declineFrom;
	if (!MTP::IgnoreError(error)) {
		_show->showToast(tr::lng_credits_currency_withdraw_error(tr::now));
	}
}

void SaleFlow::waitWallet() {
	if (!_polling) {
		_polling = true;
		_wallet->startPolling();
	}
	_waitTimer.callOnce(kWalletResolveTimeout);
	rpl::merge(
		_wallet->presenceValue() | rpl::to_empty,
		_wallet->transferWalletIdentityChanges(),
		_wallet->custodyUpdates()
	) | rpl::on_next([=] {
		walletChanged();
	}, _waitLifetime);
}

void SaleFlow::walletChanged() {
	using Presence = Wallet::Presence;
	if (_phase.current() != SalePhase::Preparing || !_waitTimer.isActive()) {
		return;
	}
	const auto presence = _wallet->presenceCurrent();
	if (presence == Presence::Ready) {
		const auto custody = _wallet->deviceCustodyState().mode;
		if (custody != Wallet::DeviceMode::Unknown) {
			stopWaiting();
			walletReady();
		}
	} else if (presence != Presence::Unknown
		&& presence != Presence::Provisioning) {
		fail(
			u"Wallet Error: Gift offer sale found presence %1."_q.arg(
				int(presence)),
			true);
	}
}

void SaleFlow::walletReady() {
	if (!current()) {
		closeBoxes();
	} else if (_operation) {
		estimate();
	} else {
		accept();
	}
}

void SaleFlow::stopWaiting() {
	_waitTimer.cancel();
	_waitLifetime.destroy();
}

void SaleFlow::accept() {
	_acceptRequestId = _session->api().request(
		MTPpayments_AcceptStarGiftOnchainOffer(
			MTP_int(_terms.itemId.msg.bare))
	).done(crl::guard(this, [=](
			const MTPpayments_StarGiftOnchainOfferTx &result) {
		_acceptRequestId = 0;
		accepted(result);
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		_acceptRequestId = 0;
		acceptFailed(error);
	})).handleFloodErrors().send();
}

void SaleFlow::accepted(const MTPpayments_StarGiftOnchainOfferTx &result) {
	if (_phase.current() != SalePhase::Preparing) {
		return;
	}
	const auto &data = result.data();
	if (data.voffer_id().v != _terms.offerId) {
		fail(
			u"API Error: %1 answered offer %2."_q.arg(
				u"payments.acceptStarGiftOnchainOffer"_q,
				QString::number(data.voffer_id().v)),
			false);
		return;
	}
	_operation = Wallet::ServedOperationFromOffer(result);
	_answerExpiresAt = data.vexpires_at().v;
	if (!current()) {
		closeBoxes();
		return;
	}
	estimate();
}

void SaleFlow::acceptFailed(const MTP::Error &error) {
	if (_phase.current() != SalePhase::Preparing) {
		return;
	}
	const auto &type = error.type();
	if (type == u"STARGIFT_ONCHAIN_SALE_ACTIVE"_q) {
		toReview();
		_show->showToast(tr::lng_gift_onchain_offer_on_sale(
			tr::now,
			lt_name,
			tr::bold(Data::UniqueGiftName(*_terms.gift)),
			tr::marked));
	} else if (MTP::IgnoreError(error)) {
		toReview();
	} else {
		fail(
			u"API Error: %1 failed with %2."_q.arg(
				u"payments.acceptStarGiftOnchainOffer"_q,
				type),
			false);
	}
}

void SaleFlow::estimate() {
	if (!_previewOwner) {
		_previewOwner = _wallet->createPreviewOwner(_previewLifetime);
	}
	_wallet->estimateServed(
		_previewOwner,
		*_operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			estimated(std::move(result));
		}));
}

void SaleFlow::estimated(Wallet::FeeResult result) {
	const auto phase = _phase.current();
	if (phase != SalePhase::Preparing && phase != SalePhase::Signing) {
		return;
	} else if (!current()) {
		closeBoxes();
		return;
	}
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (error == SendError::QuoteExpired && retry()) {
		return;
	} else if (error != SendError::None) {
		refused(error);
		return;
	} else if (phase == SalePhase::Preparing
		|| _fee.current() != result.feeNano) {
		_authorization = {};
		_fee = result.feeNano;
		_phase = SalePhase::Confirm;
		return;
	}
	_prepared = std::move(result.prepared);
	if (_authorization.valid()) {
		submit();
	} else {
		acquireKey();
	}
}

bool SaleFlow::retry() {
	if (++_refusals > Wallet::kSendRefusalRetries) {
		return false;
	}
	_prepared = nullptr;
	estimate();
	return true;
}

void SaleFlow::acquireKey() {
	_keyLifetime.destroy();
	const auto weak = base::make_weak(this);
	Wallet::AcquireWalletKey(
		Main::MakeSessionShow(_box->uiShow(), _session),
		[=] {
			return weak
				&& (weak->_phase.current() == SalePhase::Signing)
				&& weak->current();
		},
		_keyLifetime,
		crl::guard(this, [=](Wallet::KeyAuthorization auth) {
			keyReady(std::move(auth));
		}));
}

void SaleFlow::keyReady(Wallet::KeyAuthorization auth) {
	if (_phase.current() != SalePhase::Signing) {
		return;
	} else if (!current()) {
		closeBoxes();
	} else if (!auth.valid()) {
		_prepared = nullptr;
		_phase = SalePhase::Confirm;
	} else {
		_authorization = std::move(auth);
		submit();
	}
}

void SaleFlow::submit() {
	if (_phase.current() != SalePhase::Signing) {
		return;
	} else if (!current()) {
		closeBoxes();
		return;
	} else if (!_wallet->signingReady()) {
		awaitSigning();
		return;
	}
	const auto refusal = _wallet->sendRefusal(_prepared, _authorization);
	if ((refusal == SendError::QuoteExpired
		|| refusal == SendError::SigningUnavailable)
		&& retry()) {
		return;
	} else if (refusal != SendError::None) {
		refused(refusal);
		return;
	}
	_phase = SalePhase::Sending;
	_wallet->sendServed(
		base::take(_authorization),
		base::take(_prepared),
		crl::guard(this, [=](Wallet::ServedResult result) {
			sent(std::move(result));
		}));
}

void SaleFlow::awaitSigning() {
	if (_signingWait.isActive()) {
		return;
	}
	_signingLifetime.destroy();
	_signingWait.callOnce(Wallet::kSigningReadyTimeout);
	_wallet->signingReadyValue(
	) | rpl::filter(
		rpl::mappers::_1
	) | rpl::take(1) | rpl::on_next([=] {
		_signingWait.cancel();
		submit();
	}, _signingLifetime);
}

void SaleFlow::sent(Wallet::ServedResult result) {
	using Outcome = Wallet::ServedOutcome;
	if (_phase.current() != SalePhase::Sending) {
		return;
	} else if (result.outcome == Outcome::Accepted) {
		finish(tr::lng_action_gift_onchain_offer_selling(tr::now));
	} else if (result.outcome == Outcome::Refused) {
		refused(result.refusal);
	} else if (result.outcome == Outcome::Rejected) {
		fail(
			u"Wallet Error: Gift offer sale rejected with %1."_q.arg(
				result.errorType),
			false);
	} else {
		LOG(("Wallet Error: Gift offer sale ended with outcome %1."
			).arg(int(result.outcome)));
		finish(tr::lng_credits_currency_withdraw_error(tr::now));
	}
}

void SaleFlow::refused(SendError error) {
	if (error == SendError::InsufficientBalance
		|| error == SendError::InsufficientFees) {
		release();
		_phase = SalePhase::Insufficient;
		return;
	}
	const auto keepAnswer = (error != SendError::DataInvalid)
		&& (error != SendError::InvalidRequest)
		&& (error != SendError::UnsupportedSendMode);
	fail(
		u"Wallet Error: Gift offer sale refused with %1."_q.arg(int(error)),
		keepAnswer);
}

void SaleFlow::fail(const QString &diagnostic, bool keepAnswer) {
	LOG((diagnostic));
	if (!keepAnswer) {
		_operation = std::nullopt;
		_answerExpiresAt = 0;
	}
	toReview();
	_show->showToast(tr::lng_credits_currency_withdraw_error(tr::now));
}

void SaleFlow::toReview() {
	release();
	_phase = SalePhase::Review;
}

void SaleFlow::release() {
	stopWaiting();
	_signingWait.cancel();
	_signingLifetime.destroy();
	_prepared = nullptr;
	_authorization = {};
	_fee = std::nullopt;
}

void SaleFlow::finish(const QString &text) {
	const auto show = _show;
	closeBoxes();
	show->showToast(text);
}

void SaleFlow::closeBoxes() {
	const auto box = _box;
	stop();
	box->closeBox();
}

void SaleFlow::stop() {
	_phase = SalePhase::Done;
	release();
	_previewLifetime.destroy();
	_previewOwner = 0;
	if (_weakSession) {
		_session->api().request(base::take(_acceptRequestId)).cancel();
		if (base::take(_polling)) {
			_wallet->stopPolling();
		}
	}
	_keyLifetime.destroy();
	if (const auto box = _declineBox.get()) {
		box->closeBox();
	}
}

[[nodiscard]] rpl::producer<TextWithEntities> QuestionText(
		const OfferTerms &terms) {
	auto question = tr::lng_gift_onchain_offer_confirm_text(
		lt_name,
		rpl::single(tr::bold(Data::UniqueGiftName(*terms.gift))),
		lt_amount,
		rpl::single(tr::bold(PrepareGramAmountText(terms.amount))),
		tr::marked);
	if (!terms.feeBps) {
		return question;
	}
	auto receive = tr::lng_gift_offer_you_get(
		lt_cost,
		rpl::single(tr::bold(PrepareGramAmountText(ReceiveAmount(terms)))),
		tr::marked);
	return rpl::combine(
		std::move(question),
		std::move(receive)
	) | rpl::map([](TextWithEntities text, TextWithEntities suffix) {
		return text.append(u"\n\n"_q).append(std::move(suffix));
	});
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePriceValue(
		not_null<Ui::TableLayout*> table,
		not_null<Main::Session*> session,
		int64 amount) {
	const auto &st = st::giveawayGiftMessage;
	auto helper = Ui::Text::CustomEmojiHelper();
	auto text = Wallet::GramMark(helper, st.style.font);
	text.append(QChar(' ')).append(PrepareGramAmountWithFiat(
		session,
		amount,
		tr::marked(PrepareGramAmountText(amount))));
	return object_ptr<Ui::FlatLabel>(
		table,
		rpl::single(std::move(text)),
		st,
		st::defaultPopupMenu,
		helper.context());
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeQuantityValue(
		not_null<Ui::TableLayout*> table,
		const OfferTerms &terms) {
	const auto issued = terms.limitedCount - terms.limitedLeft;
	return object_ptr<Ui::FlatLabel>(
		table,
		tr::lng_gift_unique_availability(
			lt_count_decimal,
			rpl::single(issued * 1.),
			lt_amount,
			rpl::single(
				tr::marked(Lang::FormatCountDecimal(terms.limitedCount))),
			tr::marked),
		st::giveawayGiftMessage);
}

void AddTermsTable(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		const OfferTerms &terms) {
	const auto table = box->addRow(
		object_ptr<Ui::TableLayout>(box, st::giveawayGiftCodeTable),
		st::giveawayGiftCodeTableMargin);
	Ui::AddTableRow(
		table,
		tr::lng_suggest_action_price_label(),
		MakePriceValue(table, &show->session(), terms.amount));
	AddUniqueGiftPropertyRows(
		box->verticalLayout(),
		show,
		table,
		terms.gift,
		nullptr);
	if (terms.limitedCount > 0) {
		Ui::AddTableRow(
			table,
			tr::lng_gift_unique_availability_label(),
			MakeQuantityValue(table, terms));
	}
}

} // namespace

void AddGiftOnchainArtwork(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::shared_ptr<Data::UniqueGift> gift,
		int limitedCount,
		int limitedLeft) {
	using namespace Info::PeerGifts;

	const auto delegate = box->lifetime().make_state<Delegate>(
		session,
		GiftButtonMode::Minimal);
	const auto size = delegate->buttonSize();
	const auto extend = delegate->buttonExtend();
	const auto holder = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			extend.top() + size.height() + extend.bottom()),
		QMargins(0, st::boxPadding.top(), 0, 0));
	const auto button = Ui::CreateChild<GiftButton>(holder, delegate);
	button->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->setDescriptor(GiftTypeStars{
		.info = {
			.id = gift->id,
			.unique = gift,
			.document = gift->model.document,
			.limitedLeft = limitedLeft,
			.limitedCount = limitedCount,
		},
		.mine = true,
	}, GiftButtonMode::Minimal);
	button->setPlayback(GiftButtonPlayback::Once);
	holder->widthValue() | rpl::on_next([=](int width) {
		const auto left = (width - size.width()) / 2;
		button->setGeometry(
			QRect(QPoint(left, extend.top()), size),
			extend);
	}, holder->lifetime());
}

not_null<Ui::SlideWrap<Ui::FlatLabel>*> AddGiftOnchainFeeLine(
		not_null<Ui::GenericBox*> box,
		rpl::producer<int64> fee) {
	const auto wrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_gift_onchain_offer_network_fee(
					lt_amount,
					std::move(fee) | rpl::map(PrepareGramAmountText)),
				st::walletConnectCaptionLabel),
			st::walletConnectCaptionMargin),
		style::margins(),
		style::al_top);
	wrap->entity()->setTryMakeSimilarLines(true);
	wrap->hide(anim::type::instant);
	return wrap;
}

auto AddGiftOnchainInsufficientBlock(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session)
-> not_null<Ui::SlideWrap<Ui::VerticalLayout>*> {
	const auto wrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();
	const auto &padding = st::walletConnectCaptionMargin;
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_wallet_send_error_insufficient(),
			st::walletSendUserErrorLabel),
		style::margins(padding.left(), 0, padding.right(), 0),
		style::al_top
	)->setTryMakeSimilarLines(true);
	const auto deposit = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			tr::lng_wallet_add_funds(),
			st::defaultTableSmallButton),
		style::margins(
			0,
			st::walletDetailsAmountMinorSkip,
			0,
			padding.bottom()),
		style::al_top);
	deposit->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback([=] {
		const auto panel = Wallet::ShowWallet(session);
		Wallet::ShowWalletReceiveBox(session, panel->uiShow());
	});
	wrap->hide(anim::type::instant);
	return wrap;
}

void AddGiftOnchainBusySpinner(
		not_null<Ui::RoundButton*> button,
		rpl::producer<bool> busy) {
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		button->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(button, loading);
	loading->showOn(rpl::duplicate(busy));
	std::move(busy) | rpl::on_next([=](bool value) {
		button->setTextFgOverride(value
			? std::make_optional(QColor(Qt::transparent))
			: std::optional<QColor>());
	}, button->lifetime());
}

namespace {

void ConfirmSaleBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		OfferTerms terms) {
	const auto session = &show->session();
	const auto flow = box->lifetime().make_state<SaleFlow>(box, show, terms);
	box->setWidth(st::boxWideWidth);

	AddGiftOnchainArtwork(
		box,
		session,
		terms.gift,
		terms.limitedCount,
		terms.limitedLeft);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_onchain_offer_confirm_title(),
			st::giftBoxSubtitle),
		st::giftBoxSubtitleMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QuestionText(terms),
			st::giftBoxAbout),
		st::giftBoxAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	AddTermsTable(box, show, terms);
	box->addSkip(st::walletConnectCardMargin.bottom()
		- st::giveawayGiftCodeTableMargin.bottom());
	const auto fee = AddGiftOnchainFeeLine(box, flow->feeValue());
	const auto insufficient = AddGiftOnchainInsufficientBlock(box, session);

	const auto sell = box->addButton(tr::lng_gift_transfer_sell(), [=] {
		flow->sell();
	});
	box->addButton(tr::lng_cancel(), [=] {
		if (flow->closable()) {
			box->closeBox();
		}
	});
	box->addLeftButton(
		tr::lng_suggest_action_decline(),
		[=] { flow->decline(); },
		st::attentionBoxButton);
	AddGiftOnchainBusySpinner(
		sell.data(),
		flow->phaseValue(
		) | rpl::map(SaleBusy) | rpl::distinct_until_changed());
	flow->phaseValue() | rpl::on_next([=](SalePhase phase) {
		const auto sending = (phase == SalePhase::Sending);
		fee->toggle(SaleFeeShown(phase), anim::type::normal);
		insufficient->toggle(
			(phase == SalePhase::Insufficient),
			anim::type::normal);
		box->setCloseByEscape(!sending);
		box->setCloseByOutsideClick(!sending);
	}, box->lifetime());
}

} // namespace

void ShowGiftOnchainOfferReview(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto terms = LookupOfferTerms(&controller->session(), itemId);
	if (!terms) {
		return;
	} else if (!ValidTerms(*terms)) {
		LOG(("API Error: Bad on-chain gift offer amount %1 with fee %2."
			).arg(terms->amount
			).arg(terms->feeBps));
		controller->showToast(
			tr::lng_credits_currency_withdraw_error(tr::now));
		return;
	}
	controller->show(Box(ConfirmSaleBox, controller->uiShow(), *terms));
}
