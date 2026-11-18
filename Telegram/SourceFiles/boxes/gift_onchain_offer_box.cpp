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
#include "boxes/star_gift_onchain_signing.h"
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
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"
#include "window/window_session_controller.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace {

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

	void prepare();
	void sell();
	void decline();

private:
	[[nodiscard]] bool cardCurrent() const;
	[[nodiscard]] bool current() const;
	[[nodiscard]] bool usable(Ui::OnchainSigningCheck check);
	void declineConfirmed(Fn<void()> close);
	void declined();
	void declineFailed(const MTP::Error &error);
	void walletChanged();
	void walletReady();
	void stopWaiting();
	void requestTransaction();
	void accepted(const MTPpayments_StarGiftOnchainOfferTx &result);
	void acceptFailed(const MTP::Error &error);
	void ended(Wallet::ServedOutcome outcome);
	void refused(SendError error);
	void rejected(SendError error, const QString &type);
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
	std::optional<Wallet::ServedOperation> _operation;
	TimeId _answerExpiresAt = 0;
	mtpRequestId _acceptRequestId = 0;
	bool _polling = false;
	base::Timer _waitTimer;
	rpl::lifetime _waitLifetime;
	rpl::lifetime _lifetime;
	Ui::OnchainSigning _signing;

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

[[nodiscard]] bool SaleCanDecline(SalePhase phase) {
	return SaleIdle(phase) || (phase == SalePhase::Preparing);
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
, _signing({
	.box = box,
	.show = _show,
	.order = Ui::OnchainSigningOrder::ConfirmFee,
	.route = Ui::OnchainSigningRoute::Flow,
	.wait = Ui::OnchainSigningWait::Signing,
	.current = [=] { return _weakSession && current(); },
	.usable = [=](Ui::OnchainSigningCheck check) { return usable(check); },
	.refused = [=](SendError error) { refused(error); },
	.rejected = [=](SendError error, const QString &type) {
		rejected(error, type);
	},
	.duplicate = [=] { ended(Wallet::ServedOutcome::Duplicate); },
	.uncertain = [=] { ended(Wallet::ServedOutcome::Uncertain); },
	.completion = [=]() -> Fn<void()> {
		return [show = _show] {
			show->showToast(
				tr::lng_action_gift_onchain_offer_selling(tr::now));
		};
	},
	.timedOut = [=] {
		fail(u"Wallet Error: Gift offer sale timed out on signing."_q, true);
	},
	.closing = [=] { stop(); },
}) {
	_box->boxClosing() | rpl::on_next([=] {
		_signing.closing();
	}, _lifetime);
	_session->data().itemRemoved(_terms.itemId) | rpl::on_next([=] {
		closeBoxes();
	}, _lifetime);
	_signing.busyValue(
	) | rpl::filter(
		!rpl::mappers::_1
	) | rpl::on_next([=] {
		_phase = SalePhase::Confirm;
	}, _lifetime);
	_signing.sendingValue(
	) | rpl::filter(
		rpl::mappers::_1
	) | rpl::on_next([=] {
		_phase = SalePhase::Sending;
	}, _lifetime);
}

SaleFlow::~SaleFlow() {
	_signing.closing();
}

rpl::producer<SalePhase> SaleFlow::phaseValue() const {
	return _phase.value() | rpl::filter([](SalePhase phase) {
		return (phase != SalePhase::Declining) && (phase != SalePhase::Done);
	});
}

rpl::producer<int64> SaleFlow::feeValue() const {
	return _signing.feeValue() | rpl::filter_optional();
}

bool SaleFlow::closable() const {
	return (_phase.current() != SalePhase::Sending);
}

void SaleFlow::sell() {
	const auto phase = _phase.current();
	if (!SaleIdle(phase)) {
		return;
	} else if (!cardCurrent()) {
		closeBoxes();
	} else if (phase == SalePhase::Confirm) {
		_phase = SalePhase::Signing;
		if (base::unixtime::now() < _answerExpiresAt) {
			_signing.confirm();
		} else {
			requestTransaction();
		}
	} else {
		prepare();
	}
}

void SaleFlow::decline() {
	if (_declineBox || !SaleCanDecline(_phase.current())) {
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

bool SaleFlow::usable(Ui::OnchainSigningCheck check) {
	if (check != Ui::OnchainSigningCheck::Submit || current()) {
		return true;
	}
	closeBoxes();
	return false;
}

void SaleFlow::declineConfirmed(Fn<void()> close) {
	const auto phase = _phase.current();
	if (!SaleCanDecline(phase)) {
		return;
	} else if (!cardCurrent()) {
		closeBoxes();
		return;
	}
	if (phase == SalePhase::Preparing) {
		_session->api().request(base::take(_acceptRequestId)).cancel();
		toReview();
	}
	_declineFrom = _phase.current();
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

void SaleFlow::prepare() {
	_phase = SalePhase::Preparing;
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
	if (!cardCurrent()) {
		closeBoxes();
	} else if (_operation && base::unixtime::now() < _answerExpiresAt) {
		_signing.quote(*_operation);
	} else {
		requestTransaction();
	}
}

void SaleFlow::stopWaiting() {
	_waitTimer.cancel();
	_waitLifetime.destroy();
}

void SaleFlow::requestTransaction() {
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
	const auto phase = _phase.current();
	if (phase != SalePhase::Preparing && phase != SalePhase::Signing) {
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
	} else if (phase == SalePhase::Signing) {
		_signing.confirm(*_operation);
	} else {
		_signing.quote(*_operation);
	}
}

void SaleFlow::acceptFailed(const MTP::Error &error) {
	const auto phase = _phase.current();
	if (phase != SalePhase::Preparing && phase != SalePhase::Signing) {
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

void SaleFlow::ended(Wallet::ServedOutcome outcome) {
	LOG(("Wallet Error: Gift offer sale ended with outcome %1."
		).arg(int(outcome)));
	finish(tr::lng_credits_currency_withdraw_error(tr::now));
}

void SaleFlow::refused(SendError error) {
	if (error == SendError::InsufficientBalance
		|| error == SendError::InsufficientFees) {
		release();
		_phase = SalePhase::Insufficient;
		return;
	}
	const auto diagnostic
		= u"Wallet Error: Gift offer sale refused with %1."_q.arg(int(error));
	if (error == SendError::NoWallet
		|| error == SendError::SigningUnavailable) {
		LOG((diagnostic));
		_signing.openMoney();
		return;
	}
	if (error == SendError::KeyChanged) {
		LOG((diagnostic));
		_signing.keyChanged();
		return;
	}
	const auto keepAnswer = (error != SendError::DataInvalid)
		&& (error != SendError::InvalidRequest)
		&& (error != SendError::UnsupportedSendMode);
	fail(diagnostic, keepAnswer);
}

void SaleFlow::rejected(SendError error, const QString &type) {
	const auto diagnostic
		= u"Wallet Error: Gift offer sale rejected with %1."_q.arg(type);
	if (error == SendError::KeyChanged) {
		LOG((diagnostic));
		_signing.keyChanged();
	} else {
		fail(diagnostic, false);
	}
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
	_signing.stop();
}

void SaleFlow::finish(const QString &text) {
	const auto show = _show;
	closeBoxes();
	show->showToast(text);
}

void SaleFlow::closeBoxes() {
	_signing.close();
}

void SaleFlow::stop() {
	_phase = SalePhase::Done;
	release();
	if (_weakSession) {
		_session->api().request(base::take(_acceptRequestId)).cancel();
		if (base::take(_polling)) {
			_wallet->stopPolling();
		}
	}
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
		rpl::single(tr::bold(Data::FormatGiftGramAmountWithUnit(terms.amount))),
		tr::marked);
	const auto proceeds = Data::PriceAfterSaleFee(terms.amount, terms.feeBps);
	if (!terms.feeBps || !proceeds) {
		return question;
	}
	auto receive = tr::lng_gift_offer_you_get(
		lt_cost,
		rpl::single(tr::bold(Data::FormatGiftGramAmountWithUnit(*proceeds))),
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
		tr::marked(Data::FormatGiftGramAmountWithUnit(amount))));
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
					std::move(fee) | rpl::map(Data::FormatGiftGramAmountWithUnit)),
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
	flow->prepare();
}

} // namespace

void ShowGiftOnchainOfferReview(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto terms = LookupOfferTerms(&controller->session(), itemId);
	if (!terms) {
		return;
	} else if (!Data::PriceAfterSaleFee(terms->amount, terms->feeBps)) {
		LOG(("API Error: Bad on-chain gift offer amount %1 with fee %2."
			).arg(terms->amount
			).arg(terms->feeBps));
		controller->showToast(
			tr::lng_credits_currency_withdraw_error(tr::now));
		return;
	}
	controller->show(Box(ConfirmSaleBox, controller->uiShow(), *terms));
}
