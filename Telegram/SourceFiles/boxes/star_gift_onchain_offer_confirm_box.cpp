/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_offer_confirm_box.h"

#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/gift_onchain_offer_box.h"
#include "boxes/star_gift_onchain_sale_box.h"
#include "boxes/star_gift_onchain_signing.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_star_gift.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/table_rows.h"
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
	[[nodiscard]] OfferCompletion completion() const;
	void refused(SendError error);
	void duplicate();
	void closing();

	const OfferTerms _terms;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	rpl::variable<SendError> _error = SendError::None;
	bool _polling = false;
	OnchainSigning _signing;

};

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
	const auto value = terms.operation.messages.front().value;
	const auto feeBps = terms.request.gift->feeBps;
	const auto receive = Data::PriceAfterSaleFee(amount, feeBps);
	const auto fee = receive
		? std::make_optional(amount - *receive)
		: std::nullopt;
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
	const auto gram = [&](std::optional<int64> nanos) {
		if (!nanos) {
			return label(tr::marked(u"—"_q));
		}
		auto text = mark;
		text.append(QChar(' ')).append(Data::FormatGiftGramAmount(*nanos));
		return label(std::move(text));
	};
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_owner_receives(),
		gram(receive));
	AddTableRow(
		table,
		tr::lng_gift_offer_onchain_platform_fee(),
		MakeMultilineValueWithSmallButton(
			table,
			gram(fee).release(),
			rpl::single(fee
				? (QString::number(*feeBps / 100.) + '%')
				: u"—"_q)
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
			tr::bold(Data::FormatGiftGramAmountWithUnit(completion.value)),
			lt_date,
			tr::marked(langDateTime(
				base::unixtime::parse(completion.expiresAt))),
			tr::marked),
	});
}

OnchainOfferDeployment::OnchainOfferDeployment(
	not_null<GenericBox*> box,
	OfferTerms terms)
: _terms(std::move(terms))
, _session(&_terms.request.show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _signing({
	.box = box,
	.show = _terms.request.show,
	.order = OnchainSigningOrder::ConfirmFee,
	.route = OnchainSigningRoute::InBox,
	.wait = OnchainSigningWait::Confirmation,
	.current = [=] { return current(); },
	.usable = [=](OnchainSigningCheck) { return usable(); },
	.signable = [=] { return !ownGift() && !expired(); },
	.error = tr::lng_gift_buy_onchain_error,
	.subject = u"the on-chain gift offer for "_q + _terms.request.gift->slug,
	.transfer = u"the on-chain gift offer"_q,
	.quoted = [=] { _error = SendError::None; },
	.refused = [=](SendError error) { refused(error); },
	.duplicate = [=] { duplicate(); },
	.completion = [=]() -> Fn<void()> {
		return [data = completion()] {
			CompleteOffer(data);
		};
	},
	.closing = [=] { closing(); },
}) {
}

OnchainOfferDeployment::~OnchainOfferDeployment() {
	_signing.closing();
}

rpl::producer<std::optional<int64>> OnchainOfferDeployment::feeValue() const {
	return _signing.feeValue();
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
	return _signing.busyValue();
}

void OnchainOfferDeployment::start() {
	_signing.start();
	_polling = true;
	_wallet->startPolling();
	_signing.quote(_terms.operation);
}

void OnchainOfferDeployment::press() {
	if (_signing.busy()) {
		return;
	}
	_error = SendError::None;
	_signing.confirm();
}

bool OnchainOfferDeployment::current() const {
	const auto &show = _terms.request.show;
	return _weakSession
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
		_signing.close();
		return false;
	} else if (ownGift()) {
		LOG(("Wallet Error: the wallet holds %1, so it gets no offer."
			).arg(slug));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	} else if (expired()) {
		LOG(("Wallet Error: the on-chain offer for %1 expired at %2."
			).arg(slug
			).arg(_terms.expiresAt));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	}
	return true;
}

OfferCompletion OnchainOfferDeployment::completion() const {
	return {
		.show = _terms.request.show,
		.form = _terms.request.form,
		.session = _weakSession,
		.value = _terms.operation.messages.front().value,
		.expiresAt = _terms.expiresAt,
	};
}

void OnchainOfferDeployment::refused(SendError error) {
	const auto refusal = (error == SendError::None)
		? SendError::Failed
		: error;
	if (!Insufficient(refusal)) {
		LOG(("Wallet Error: the on-chain gift offer was refused with %1."
			).arg(int(refusal)));
	}
	_error = refusal;
}

void OnchainOfferDeployment::duplicate() {
	LOG(("Wallet Error: the on-chain gift offer for %1 was already sent."
		).arg(_terms.request.gift->slug));
	const auto completion = this->completion();
	_signing.close();
	FinishOffer(completion, {
		.text = tr::lng_gift_offer_onchain_already_sent(tr::now, tr::marked),
	});
}

void OnchainOfferDeployment::closing() {
	if (_weakSession && base::take(_polling)) {
		_wallet->stopPolling();
	}
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
	const auto &message = terms.operation.messages.front();
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
