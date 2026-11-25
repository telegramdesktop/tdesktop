/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_sale_box.h"

#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/gift_onchain_offer_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_onchain_signing.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "lang/lang_keys.h"
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
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_served.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

using Wallet::SendError;

enum class SaleKind : uchar {
	List,
	Change,
	Unlist,
};

enum class Ownership : uchar {
	Own,
	Other,
	Unknown,
	NoWallet,
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
	bool fixed = false;
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Data::UniqueGift> gift;
	int64 price = 0;
	QString saleAddress;
	base::weak_qptr<GenericBox> priceBox;
	base::weak_ptr<Main::Session> session;
};

struct FixedSaleListings {
	base::flat_set<QString> pendingSlugs;
	base::flat_set<QString> saleAddresses;
};

class OnchainSale final : public base::has_weak_ptr {
public:
	OnchainSale(not_null<GenericBox*> box, SaleArgs args);
	~OnchainSale();

	[[nodiscard]] rpl::producer<int64> amountValue() const;
	[[nodiscard]] rpl::producer<bool> fixedValue() const;
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
	[[nodiscard]] SaleCompletion completion() const;
	void request();
	void answered(const MTPpayments_StarGiftOnchainSalePayload &result);
	void fixedAnswered(Wallet::ServedOperation operation);
	void quote();
	void failed(const MTP::Error &error);
	void unlistFirst();
	[[nodiscard]] SendError estimated(const Wallet::FeeResult &result);
	void refused(SendError error);
	void duplicate();
	void walletAbsent();
	void closing();

	const SaleArgs _args;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	MTP::Sender _api;
	std::optional<Wallet::ServedOperation> _operation;
	rpl::variable<bool> _requesting = false;
	rpl::variable<int64> _amount = 0;
	rpl::variable<bool> _fixed = false;
	rpl::variable<SendError> _error = SendError::None;
	mtpRequestId _requestId = 0;
	bool _polling = false;
	rpl::lifetime _lifetime;
	OnchainSigning _signing;

};

[[nodiscard]] FixedSaleListings &SessionFixedSaleListings(
		not_null<Main::Session*> session) {
	static auto Map = base::flat_map<
		not_null<Main::Session*>,
		FixedSaleListings>();
	auto i = Map.find(session);
	if (i == end(Map)) {
		i = Map.emplace(session, FixedSaleListings()).first;
		session->lifetime().add([=] { Map.remove(session); });
	}
	return i->second;
}

[[nodiscard]] Ownership LookupOwnership(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	if (gift.slug.isEmpty()
		|| gift.burned
		|| !session->appConfig().walletAvailable()
		|| session->supportMode()) {
		return Ownership::Other;
	} else if (gift.mySale) {
		return session->wallet().address()
			? Ownership::Own
			: Ownership::Unknown;
	} else if ((gift.ownerId && !gift.fixedSale)
		|| gift.ownerAddress.isEmpty()) {
		return Ownership::Other;
	}
	const auto owner = Wallet::CanonicalAddress(gift.ownerAddress);
	if (owner.isEmpty()) {
		// A TON DNS name is served instead of the owner address.
		const auto wallet = &session->wallet();
		if (!wallet->address()) {
			return Ownership::Other;
		}
		const auto &list = wallet->collectibles();
		const auto record = Wallet::FindListedGift(list, gift);
		return (record && !Wallet::OnOwnSale(*record))
			? Ownership::Own
			: Ownership::Other;
	}
	const auto own = session->wallet().address();
	if (!own) {
		return Ownership::Unknown;
	}
	const auto wallet = Wallet::CanonicalAddress(*own);
	const auto &sales = SessionFixedSaleListings(session).saleAddresses;
	return ((!wallet.isEmpty()
		&& owner.compare(wallet, Qt::CaseInsensitive) == 0)
		|| sales.contains(owner))
		? Ownership::Own
		: Ownership::Other;
}

[[nodiscard]] bool ProfileGift(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	return (gift.ownerId == session->userPeerId());
}

[[nodiscard]] bool Absent(Wallet::Presence presence) {
	using Wallet::Presence;
	return (presence == Presence::Missing)
		|| (presence == Presence::Unavailable)
		|| (presence == Presence::AddressUnreadable);
}

[[nodiscard]] Ownership LookupSaleOwnership(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	if (!ProfileGift(session, gift)
		|| gift.slug.isEmpty()
		|| gift.burned
		|| !session->appConfig().walletAvailable()
		|| session->supportMode()) {
		return LookupOwnership(session, gift);
	}
	const auto wallet = &session->wallet();
	return wallet->address()
		? Ownership::Own
		: Absent(wallet->presenceCurrent())
		? Ownership::NoWallet
		: Ownership::Unknown;
}

[[nodiscard]] bool CheckSaleEntry(
		const std::shared_ptr<ChatHelpers::Show> &show,
		const Data::UniqueGift &gift) {
	const auto session = &show->session();
	if (!gift.onchainSale || HasPendingGiftSale(session, gift)) {
		return false;
	} else if (!ProfileGift(session, gift)) {
		return HasOnchainSaleEntries(LookupOwnOnchainGift(session, gift));
	} else if (!session->appConfig().walletAvailable()
		|| session->supportMode()) {
		LOG(("Wallet Error: on-chain sale of %1 is unavailable."
			).arg(gift.slug));
		show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	}
	const auto wallet = &session->wallet();
	wallet->ensureLoaded();
	if (Absent(wallet->presenceCurrent())) {
		Wallet::ShowWallet(session);
		return false;
	}
	return (LookupSaleOwnership(session, gift) != Ownership::Other);
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

[[nodiscard]] std::optional<int64> MessagesValue(
		const std::vector<Wallet::ServedMessage> &messages) {
	auto result = int64(0);
	for (const auto &message : messages) {
		const auto room = std::numeric_limits<int64>::max() - result;
		if (message.value < 0 || message.value > room) {
			return std::nullopt;
		}
		result += message.value;
	}
	return result;
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
		rpl::single(tr::bold(Data::FormatGiftGramAmountWithUnit(price))),
		tr::marked);
}

[[nodiscard]] QString SaleToastText(const SaleCompletion &completion) {
	const auto name = Data::UniqueGiftName(*completion.gift);
	switch (completion.kind) {
	case SaleKind::List:
		return (completion.fixed
			? tr::lng_gift_sell_onchain_pending
			: tr::lng_gift_sell_onchain_toast)(
				tr::now,
				lt_name,
				name,
				lt_price,
				Data::FormatGiftGramAmountWithUnit(completion.price));
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
	const auto pending = completion.fixed
		&& (completion.kind == SaleKind::List);
	if (pending) {
		auto &listings = SessionFixedSaleListings(session);
		listings.pendingSlugs.emplace(gift->slug);
		const auto address = Wallet::CanonicalAddress(completion.saleAddress);
		if (!address.isEmpty()) {
			listings.saleAddresses.emplace(address);
		}
	} else if (completion.kind == SaleKind::Unlist) {
		gift->starsForResale = 0;
		gift->nanoTonForResale = 0;
		SessionFixedSaleListings(session).pendingSlugs.remove(gift->slug);
	} else {
		const auto price = CreditsAmount(
			0,
			completion.price,
			CreditsType::Ton);
		gift->starsForResale = StarsFromTon(session, price).whole();
		gift->nanoTonForResale = completion.price;
	}
	session->data().notifyGiftUpdate({
		.slug = gift->slug,
		.action = Data::GiftUpdate::Action::ResaleChange,
		.listingChanged = (completion.kind != SaleKind::Change),
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
			ShowOwnGiftUnlistBox(show, gift, Data::SavedStarGiftId());
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
			result.append(QChar(' ')).append(Data::FormatGiftGramAmount(value));
			return result;
		}),
		helper.context());
}

void FillSaleTerms(
		not_null<TableLayout*> table,
		SaleKind kind,
		int64 price,
		bool listing,
		std::optional<int64> receive,
		int64 amount) {
	if (kind != SaleKind::Unlist) {
		AddGramRow(table, tr::lng_gift_resale_price(), rpl::single(price));
	}
	if (receive) {
		AddGramRow(
			table,
			tr::lng_gift_sell_onchain_you_receive(),
			rpl::single(*receive));
	} else if (listing) {
		AddTableRow(
			table,
			tr::lng_gift_sell_onchain_you_receive(),
			rpl::single(tr::marked(u"—"_q)));
	}
	AddGramRow(
		table,
		((kind == SaleKind::Unlist)
			? tr::lng_gift_sell_onchain_unlist_fee()
			: tr::lng_gift_sell_onchain_listing_fee()),
		rpl::single(amount));
}

OnchainSale::OnchainSale(not_null<GenericBox*> box, SaleArgs args)
: _args(std::move(args))
, _session(&_args.show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _api(&_session->mtp())
, _signing({
	.box = box,
	.show = _args.show,
	.order = OnchainSigningOrder::ConfirmFee,
	.route = OnchainSigningRoute::InBox,
	.wait = OnchainSigningWait::Confirmation,
	.current = [=] { return current(); },
	.usable = [=](OnchainSigningCheck) { return usable(); },
	.signable = [=] { return (ownership() == Ownership::Own); },
	.request = [=] { request(); },
	.estimated = [=](const Wallet::FeeResult &result) {
		return estimated(result);
	},
	.error = tr::lng_gift_buy_onchain_error,
	.subject = u"the transfer for "_q + RequestMethod(_args.kind),
	.transfer = RequestMethod(_args.kind),
	.quoted = [=] { _error = SendError::None; },
	.refused = [=](SendError error) { refused(error); },
	.duplicate = [=] { duplicate(); },
	.completion = [=]() -> Fn<void()> {
		return [data = completion()] {
			CompleteSale(data);
		};
	},
	.closing = [=] { closing(); },
}) {
}

OnchainSale::~OnchainSale() {
	_signing.closing();
}

rpl::producer<int64> OnchainSale::amountValue() const {
	return _amount.value();
}

rpl::producer<bool> OnchainSale::fixedValue() const {
	return _fixed.value();
}

rpl::producer<std::optional<int64>> OnchainSale::feeValue() const {
	return _signing.feeValue();
}

rpl::producer<bool> OnchainSale::insufficientValue() const {
	return _error.value() | rpl::map(Insufficient);
}

rpl::producer<QString> OnchainSale::refusalValue() const {
	return _error.value() | rpl::map(RefusalText);
}

rpl::producer<bool> OnchainSale::busyValue() const {
	return rpl::combine(
		_requesting.value(),
		_signing.busyValue()
	) | rpl::map(
		rpl::mappers::_1 || rpl::mappers::_2
	) | rpl::distinct_until_changed();
}

void OnchainSale::start() {
	_signing.start();
	_polling = true;
	_wallet->startPolling();
	if (ProfileGift(_session, *_args.gift)) {
		_wallet->presenceValue(
		) | rpl::filter(
			Absent
		) | rpl::take(1) | rpl::to_empty | rpl::on_next([=] {
			walletAbsent();
		}, _lifetime);
	}
	request();
}

void OnchainSale::press() {
	if (_requesting.current() || _signing.busy()) {
		return;
	}
	_error = SendError::None;
	if (_operation) {
		_signing.confirm();
	} else {
		request();
	}
}

bool OnchainSale::current() const {
	return _weakSession
		&& _args.show->valid()
		&& (&_args.show->session() == _session);
}

Ownership OnchainSale::ownership() const {
	return LookupSaleOwnership(_session, *_args.gift);
}

bool OnchainSale::usable() {
	if (!current()) {
		_signing.close();
		return false;
	}
	const auto &gift = *_args.gift;
	if (!gift.onchainSale) {
		LOG(("Wallet Error: %1 is no longer sold on-chain for %2."
			).arg(gift.slug, RequestMethod(_args.kind)));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	}
	const auto ownership = this->ownership();
	if (ownership == Ownership::Unknown) {
		_signing.awaitWallet();
		return false;
	} else if (ownership == Ownership::NoWallet) {
		walletAbsent();
		return false;
	} else if (ownership == Ownership::Other) {
		LOG(("Wallet Error: the wallet no longer holds %1 for %2."
			).arg(gift.slug, RequestMethod(_args.kind)));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return false;
	} else if (_args.kind != SaleKind::Unlist
		&& Data::UniqueGiftOnSale(gift)
		&& !gift.fixedSale) {
		unlistFirst();
		return false;
	}
	return true;
}

SaleCompletion OnchainSale::completion() const {
	return {
		.kind = _args.kind,
		.fixed = _fixed.current(),
		.show = _args.show,
		.gift = _args.gift,
		.price = _args.price,
		.saleAddress = (_operation ? _operation->saleAddress : QString()),
		.priceBox = _args.priceBox,
		.session = _weakSession,
	};
}

void OnchainSale::request() {
	_requesting = true;
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
	if (!_requesting.current()) {
		return;
	}
	auto operation = Wallet::ServedOperationFromSale(result);
	if (result.type() == mtpc_payments_starGiftOnchainFixedSalePayload) {
		fixedAnswered(std::move(operation));
		return;
	}
	const auto &message = operation.messages.front();
	const auto amount = message.value;
	if (message.destination.isEmpty() || amount <= 0) {
		LOG(("API Error: %1 answered a malformed sale payload (amount %2)."
			).arg(RequestMethod(_args.kind)
			).arg(amount));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_operation = std::move(operation);
	_amount = amount;
	quote();
}

void OnchainSale::fixedAnswered(Wallet::ServedOperation operation) {
	const auto amount = MessagesValue(operation.messages).value_or(0);
	const auto gasless = MessagesValue(operation.gaslessMessages);
	if (amount <= 0 || !gasless || (*gasless > amount)) {
		LOG(("API Error: %1 answered a malformed fixed sale "
			"(%2 messages, price %3)."
			).arg(RequestMethod(_args.kind)
			).arg(operation.messages.size()
			).arg(_args.price));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_operation = std::move(operation);
	_fixed = true;
	_amount = amount;
	quote();
}

void OnchainSale::quote() {
	const auto weak = base::make_weak(this);
	_signing.quote(*_operation);
	if (weak) {
		_requesting = false;
	}
}

void OnchainSale::failed(const MTP::Error &error) {
	if (!_requesting.current()) {
		return;
	}
	const auto &type = error.type();
	const auto listing = (_args.kind != SaleKind::Unlist);
	if (MTP::IgnoreError(error)) {
		_signing.close();
	} else if (listing && type == u"STARGIFT_ONCHAIN_PRICE_INVALID"_q) {
		const auto invalid = _args.invalid;
		_signing.close();
		if (invalid) {
			invalid();
		}
	} else if (listing && type == u"STARGIFT_ONCHAIN_SALE_ACTIVE"_q) {
		unlistFirst();
	} else if (!listing && type == u"STARGIFT_ONCHAIN_SALE_INACTIVE"_q) {
		const auto completion = this->completion();
		_signing.close();
		ApplySale(completion);
	} else {
		LOG(("API Error: %1 failed with %2."
			).arg(RequestMethod(_args.kind), type));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainSale::unlistFirst() {
	const auto show = _args.show;
	const auto gift = _args.gift;
	const auto priceBox = _args.priceBox;
	const auto valid = current();
	_signing.close();
	if (valid) {
		ShowUnlistFirst(show, gift, priceBox);
	}
}

SendError OnchainSale::estimated(const Wallet::FeeResult &result) {
	if (result.error == SendError::DataInvalid && _fixed.current()) {
		LOG(("Wallet Error: %1 answered a fixed sale "
			"the wallet cannot prepare.").arg(RequestMethod(_args.kind)));
		_signing.closeWithToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
	return SendError::None;
}

void OnchainSale::refused(SendError error) {
	_error = error;
	_requesting = false;
}

void OnchainSale::duplicate() {
	LOG(("Wallet Error: the transfer for %1 was already sent."
		).arg(RequestMethod(_args.kind)));
	const auto priceBox = _args.priceBox;
	_signing.closeWithToast(tr::lng_gift_sell_onchain_already_sent(tr::now));
	if (const auto strong = priceBox.get()) {
		strong->closeBox();
	}
}

void OnchainSale::walletAbsent() {
	if (const auto priceBox = _args.priceBox.get()) {
		priceBox->closeBox();
	}
	_signing.openMoney();
}

void OnchainSale::closing() {
	_lifetime.destroy();
	if (_weakSession) {
		_api.request(base::take(_requestId)).cancel();
		if (base::take(_polling)) {
			_wallet->stopPolling();
		}
	}
	_operation = std::nullopt;
}

void OnchainSaleBox(not_null<GenericBox*> box, SaleArgs args) {
	const auto session = &args.show->session();
	const auto kind = args.kind;
	const auto price = args.price;
	const auto feeBps = args.gift->feeBps;
	const auto receive = (kind != SaleKind::Unlist)
		? Data::PriceAfterSaleFee(price, feeBps)
		: std::nullopt;
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
	rpl::combine(
		sale->amountValue(),
		sale->fixedValue()
	) | rpl::filter([](int64 amount, bool) {
		return amount > 0;
	}) | rpl::take(1) | rpl::on_next([=](int64 amount, bool fixed) {
		const auto listing = fixed && (kind != SaleKind::Unlist);
		FillSaleTerms(
			terms->entity(),
			kind,
			price,
			listing,
			listing ? receive : std::nullopt,
			amount);
	}, terms->lifetime());
	terms->toggleOn(sale->amountValue() | rpl::map(rpl::mappers::_1 > 0));
	box->addSkip(st::walletConnectCardMargin.bottom() - padding.bottom());
	if (receive) {
		const auto percent = QString::number(*feeBps / 100.) + '%';
		const auto caption = box->addRow(
			object_ptr<SlideWrap<FlatLabel>>(
				box,
				object_ptr<FlatLabel>(
					box,
					tr::lng_gift_sell_onchain_fee_deducted(
						lt_percent,
						rpl::single(percent)),
					st::walletConnectCaptionLabel),
				st::walletConnectCaptionMargin),
			style::margins(),
			style::al_top);
		caption->entity()->setTryMakeSimilarLines(true);
		caption->toggleOn(sale->fixedValue());
	}

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
	if (LookupOwnership(session, gift) != Ownership::Own) {
		return OwnOnchainGift::None;
	} else if (Data::UniqueGiftOnSale(gift)) {
		return OwnOnchainGift::Listed;
	}
	return HasPendingGiftSale(session, gift)
		? OwnOnchainGift::Pending
		: OwnOnchainGift::Held;
}

bool HasOnchainSaleEntries(OwnOnchainGift own) {
	return (own == OwnOnchainGift::Held) || (own == OwnOnchainGift::Listed);
}

bool HasPendingGiftSale(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	const auto &pending = SessionFixedSaleListings(session).pendingSlugs;
	return !Data::UniqueGiftOnSale(gift) && pending.contains(gift.slug);
}

void ShowOnchainGiftSellBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		Settings::GiftWearBoxStyleOverride st) {
	if (!show
		|| !show->valid()
		|| !gift
		|| !CheckSaleEntry(show, *gift)) {
		return;
	} else if (Data::UniqueGiftOnSale(*gift) && !gift->fixedSale) {
		ShowUnlistFirst(show, gift, {});
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
		const auto listed = Data::UniqueGiftOnSale(*gift);
		const auto kind = listed ? SaleKind::Change : SaleKind::List;
		if (!gift->onchainSale) {
			LOG(("Wallet Error: %1 is no longer sold on-chain for %2."
				).arg(gift->slug, RequestMethod(kind)));
			if (const auto price = dialog->price.get()) {
				price->closeBox();
			}
			show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
			return;
		} else if (listed && !gift->fixedSale) {
			ShowUnlistFirst(show, gift, dialog->price);
			return;
		}
		dialog->confirmation = show->show(Box(OnchainSaleBox, SaleArgs{
			.kind = kind,
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
	const auto session = &show->session();
	const auto box = dialog->price.get();
	if (!box || !ProfileGift(session, *gift)) {
		return;
	}
	const auto weak = base::make_weak(session);
	rpl::merge(
		box->boxClosing() | rpl::map_to(false),
		session->wallet().presenceValue(
		) | rpl::filter(Absent) | rpl::map_to(true)
	) | rpl::take(1) | rpl::filter(
		rpl::mappers::_1
	) | rpl::to_empty | rpl::on_next([=] {
		if (const auto confirmation = dialog->confirmation.get()) {
			confirmation->closeBox();
		}
		if (const auto price = dialog->price.get()) {
			price->closeBox();
		}
		if (const auto strong = weak.get()) {
			Wallet::ShowWallet(strong);
		}
	}, box->lifetime());
}

void ShowOwnGiftUnlistBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId) {
	if (!show || !show->valid() || !gift) {
		return;
	} else if (!gift->onchainSale) {
		const auto name = Data::UniqueGiftName(*gift);
		const auto confirm = [=](Fn<void()> close) {
			close();
			UpdateGiftSellPrice(show, gift, savedId, {});
		};
		show->show(MakeConfirmBox({
			.text = tr::lng_gift_sell_unlist_sure(),
			.confirmed = confirm,
			.confirmText = tr::lng_gift_transfer_unlist(),
			.title = tr::lng_gift_sell_unlist_title(
				lt_name,
				rpl::single(name)),
		}));
		return;
	} else if (!CheckSaleEntry(show, *gift)) {
		return;
	}
	show->show(Box(OnchainSaleBox, SaleArgs{
		.kind = SaleKind::Unlist,
		.show = show,
		.gift = std::move(gift),
	}), LayerOption::KeepOther);
}

} // namespace Ui
