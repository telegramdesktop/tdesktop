/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_offer_buy_box.h"

#include "base/random.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "boxes/star_gift_onchain_offer_confirm_box.h"
#include "boxes/star_gift_onchain_sale_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_star_gift.h"
#include "history/history_item_helpers.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/sender.h"
#include "settings/settings_common.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/emoji_stake_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {

constexpr auto kHour = TimeId(3600);
constexpr auto kDay = 24 * kHour;
constexpr auto kDefaultDuration = 2 * kDay;
constexpr auto kDurations = std::array{
	kDay / 4,
	kDay / 2,
	kDay,
	kDay + kDay / 2,
	2 * kDay,
	3 * kDay,
};
constexpr auto kMaxOffer = 1'000'000 * kNanosInOne;

enum class FormState : uchar {
	Empty,
	Below,
	Above,
	Unknown,
	Insufficient,
	Ready,
};

struct OfferDraft {
	int64 amount = 0;
	TimeId duration = 0;

	friend inline auto operator<=>(OfferDraft, OfferDraft) = default;
	friend inline bool operator==(OfferDraft, OfferDraft) = default;
};

class OnchainOfferForm final : public base::has_weak_ptr {
public:
	OnchainOfferForm(
		not_null<GenericBox*> box,
		OnchainGiftOfferRequest request);
	~OnchainOfferForm();

	[[nodiscard]] int64 minimum() const;
	[[nodiscard]] TimeId duration() const;
	[[nodiscard]] rpl::producer<std::optional<int64>> amountValue() const;
	[[nodiscard]] rpl::producer<TimeId> durationValue() const;
	[[nodiscard]] rpl::producer<FormState> stateValue() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;
	[[nodiscard]] rpl::producer<CreditsAmount> balanceValue() const;

	void setAmount(std::optional<int64> amount);
	void setDuration(TimeId duration);
	void start();
	void press();

private:
	[[nodiscard]] bool current() const;
	[[nodiscard]] FormState state() const;
	[[nodiscard]] std::optional<OfferDraft> draft() const;
	[[nodiscard]] uint64 randomId(OfferDraft draft);
	void answered(const MTPpayments_StarGiftOnchainOfferTx &result);
	void failed(const MTP::Error &error);
	void walletAbsent();
	void close();
	void closing();

	const not_null<GenericBox*> _box;
	const OnchainGiftOfferRequest _request;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	const int64 _minimum = 0;
	MTP::Sender _api;
	base::flat_map<OfferDraft, uint64> _randomIds;
	base::weak_qptr<GenericBox> _confirmation;
	rpl::variable<std::optional<int64>> _amount;
	rpl::variable<TimeId> _duration = kDefaultDuration;
	rpl::variable<bool> _busy = false;
	OfferDraft _requested;
	mtpRequestId _requestId = 0;
	bool _polling = false;
	bool _closed = false;
	rpl::lifetime _lifetime;

};

[[nodiscard]] bool Absent(Wallet::Presence presence) {
	using Wallet::Presence;
	return (presence == Presence::Missing)
		|| (presence == Presence::Unavailable)
		|| (presence == Presence::AddressUnreadable);
}

[[nodiscard]] bool Failing(FormState state) {
	return (state == FormState::Below)
		|| (state == FormState::Above)
		|| (state == FormState::Insufficient);
}

[[nodiscard]] FormState Evaluate(
		std::optional<int64> amount,
		int64 minimum,
		Wallet::Presence presence,
		int64 balance) {
	if (!amount) {
		return FormState::Empty;
	} else if (*amount < minimum) {
		return FormState::Below;
	} else if (*amount > kMaxOffer) {
		return FormState::Above;
	} else if (presence != Wallet::Presence::Ready) {
		return FormState::Unknown;
	} else if (*amount > balance) {
		return FormState::Insufficient;
	}
	return FormState::Ready;
}

[[nodiscard]] std::optional<int64> ParseAmount(const QString &text) {
	const auto result = ParseTonAmountString(text);
	return (result && *result >= 0) ? result : std::nullopt;
}

[[nodiscard]] CreditsAmount TonCredits(int64 nanos) {
	return CreditsAmount(
		nanos / kNanosInOne,
		nanos % kNanosInOne,
		CreditsType::Ton);
}

[[nodiscard]] rpl::producer<TextWithEntities> NoteText(
		FormState state,
		int64 minimum,
		const QString &name) {
	if (state == FormState::Below) {
		return tr::lng_gift_offer_onchain_min(
			lt_amount,
			rpl::single(tr::marked(PrepareGramAmountText(minimum))),
			tr::marked);
	} else if (state == FormState::Above) {
		return tr::lng_gift_offer_onchain_max(
			lt_amount,
			rpl::single(tr::marked(PrepareGramAmountText(kMaxOffer))),
			tr::marked);
	} else if (state == FormState::Insufficient) {
		return OnchainOfferInsufficientText();
	}
	return tr::lng_gift_offer_onchain_about(
		lt_name,
		rpl::single(tr::bold(name)),
		tr::marked);
}

OnchainOfferForm::OnchainOfferForm(
	not_null<GenericBox*> box,
	OnchainGiftOfferRequest request)
: _box(box)
, _request(std::move(request))
, _session(&_request.show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _minimum(std::max(kNanosInOne, _request.gift->nanoTonMinOffer))
, _api(&_session->mtp()) {
}

OnchainOfferForm::~OnchainOfferForm() {
	closing();
}

int64 OnchainOfferForm::minimum() const {
	return _minimum;
}

TimeId OnchainOfferForm::duration() const {
	return _duration.current();
}

rpl::producer<std::optional<int64>> OnchainOfferForm::amountValue() const {
	return _amount.value();
}

rpl::producer<TimeId> OnchainOfferForm::durationValue() const {
	return _duration.value();
}

rpl::producer<FormState> OnchainOfferForm::stateValue() const {
	const auto minimum = _minimum;
	return rpl::combine(
		_amount.value(),
		_wallet->presenceValue(),
		_wallet->balanceNanoValue()
	) | rpl::map([=](
			std::optional<int64> amount,
			Wallet::Presence presence,
			int64 balance) {
		return Evaluate(amount, minimum, presence, balance);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> OnchainOfferForm::busyValue() const {
	return _busy.value();
}

rpl::producer<CreditsAmount> OnchainOfferForm::balanceValue() const {
	return rpl::combine(
		_wallet->stateKnownValue(),
		_wallet->balanceNanoValue()
	) | rpl::filter([](bool known, int64) {
		return known;
	}) | rpl::map([](bool, int64 balance) {
		return TonCredits(balance);
	});
}

void OnchainOfferForm::setAmount(std::optional<int64> amount) {
	_amount = amount;
}

void OnchainOfferForm::setDuration(TimeId duration) {
	_duration = duration;
}

void OnchainOfferForm::start() {
	_polling = true;
	_wallet->startPolling();
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	_wallet->presenceValue(
	) | rpl::filter(Absent) | rpl::take(1) | rpl::to_empty | rpl::on_next([=] {
		walletAbsent();
	}, _lifetime);
}

void OnchainOfferForm::press() {
	const auto draft = this->draft();
	if (_requestId
		|| _confirmation
		|| !current()
		|| !draft
		|| (state() != FormState::Ready)) {
		return;
	}
	const auto &gift = _request.gift;
	if (!CanOfferOnchainGift(_session, *gift)) {
		LOG(("Wallet Error: %1 takes no on-chain offer now.").arg(gift->slug));
		const auto show = _request.show;
		close();
		show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
		return;
	}
	_requested = *draft;
	_busy = true;
	_requestId = _api.request(MTPpayments_SendStarGiftOnchainOffer(
		MTP_string(gift->slug),
		MTP_long(_requested.amount),
		MTP_int(_requested.duration),
		MTP_long(randomId(_requested))
	)).done([=](const MTPpayments_StarGiftOnchainOfferTx &result) {
		answered(result);
	}).fail([=](const MTP::Error &error) {
		failed(error);
	}).handleFloodErrors().send();
}

bool OnchainOfferForm::current() const {
	const auto &show = _request.show;
	return !_closed
		&& _weakSession
		&& show->valid()
		&& (&show->session() == _session);
}

FormState OnchainOfferForm::state() const {
	return Evaluate(
		_amount.current(),
		_minimum,
		_wallet->presenceCurrent(),
		_wallet->balanceNano());
}

std::optional<OfferDraft> OnchainOfferForm::draft() const {
	if (const auto amount = _amount.current()) {
		return OfferDraft{
			.amount = *amount,
			.duration = _duration.current(),
		};
	}
	return std::nullopt;
}

uint64 OnchainOfferForm::randomId(OfferDraft draft) {
	const auto i = _randomIds.find(draft);
	return (i != end(_randomIds))
		? i->second
		: _randomIds.emplace(draft, base::RandomValue<uint64>()).first->second;
}

void OnchainOfferForm::answered(
		const MTPpayments_StarGiftOnchainOfferTx &result) {
	_requestId = 0;
	_busy = false;
	if (!current() || (draft() != _requested)) {
		return;
	}
	auto request = _request;
	request.amount = _requested.amount;
	_confirmation = ShowOnchainGiftOfferConfirmBox(std::move(request), result);
}

void OnchainOfferForm::failed(const MTP::Error &error) {
	_requestId = 0;
	_busy = false;
	if (_closed || MTP::IgnoreError(error)) {
		return;
	}
	LOG(("API Error: payments.sendStarGiftOnchainOffer failed with %1."
		).arg(error.type()));
	if (current()) {
		_request.show->showToast(tr::lng_gift_buy_onchain_error(tr::now));
	}
}

void OnchainOfferForm::walletAbsent() {
	if (_closed) {
		return;
	}
	const auto session = _weakSession;
	close();
	if (const auto strong = session.get()) {
		Wallet::ShowWallet(strong);
	}
}

void OnchainOfferForm::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainOfferForm::closing() {
	if (_closed) {
		return;
	}
	_closed = true;
	_api.request(base::take(_requestId)).cancel();
	if (const auto confirmation = _confirmation.get()) {
		confirmation->closeBox();
	}
	if (_weakSession && base::take(_polling)) {
		_wallet->stopPolling();
	}
}

void ChooseDuration(
		not_null<GenericBox*> box,
		not_null<OnchainOfferForm*> form) {
	const auto current = form->duration();
	const auto save = crl::guard(form, [=](int index) {
		form->setDuration(kDurations[index]);
	});
	box->uiShow()->show(Box([=](not_null<GenericBox*> chooser) {
		const auto options = kDurations
			| ranges::views::transform([](TimeId duration) {
				return tr::lng_hours(tr::now, lt_count, duration / kHour);
			})
			| ranges::to_vector;
		const auto selected = ranges::find(kDurations, current);
		SingleChoiceBox(chooser, {
			.title = tr::lng_gift_offer_duration(),
			.options = options,
			.initialSelection = int(selected - begin(kDurations)),
			.callback = save,
		});
	}));
}

void OnchainOfferBox(
		not_null<GenericBox*> box,
		OnchainGiftOfferRequest request) {
	const auto session = &request.show->session();
	const auto name = Data::UniqueGiftName(*request.gift);
	request.form = base::make_weak(box);
	const auto form = box->lifetime().make_state<OnchainOfferForm>(
		box,
		std::move(request));
	const auto minimum = form->minimum();
	const auto container = box->verticalLayout();

	box->setStyle(st::suggestPriceBox);
	box->setNoContentMargin(true);

	AddSkip(container, st::boxTitleHeight * 1.1);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_gift_offer_title(),
			st::settingsPremiumUserTitle),
		style::al_top);
	AddSkip(container);

	const auto added = st::boxRowPadding - st::defaultSubsectionTitlePadding;
	AddSubsectionTitle(
		container,
		tr::lng_gift_offer_onchain_field(),
		QMargins(
			added.left(),
			0,
			added.right(),
			-st::defaultSubsectionTitlePadding.bottom()));
	const auto field = AddTonInputField(container, {});
	AddApproximateUsd(
		field,
		session,
		form->amountValue() | rpl::map([](std::optional<int64> amount) {
			return TonCredits(amount.value_or(0));
		}));
	AddSkip(container);
	AddSkip(container);
	const auto note = AddDividerText(
		container,
		form->stateValue() | rpl::map([=](FormState state) {
			return NoteText(state, minimum, name);
		}) | rpl::flatten_latest());
	note->setClickHandlerFilter([=](const auto &...) {
		ShowOnchainOfferAddFunds(session);
		return false;
	});
	AddSkip(container);

	const auto duration = Settings::AddButtonWithLabel(
		container,
		tr::lng_gift_offer_duration(),
		tr::lng_hours(
			lt_count,
			form->durationValue() | rpl::map([](TimeId seconds) {
				return seconds / kHour;
			}) | tr::to_count()),
		st::settingsButtonNoIcon);
	duration->setClickedCallback([=] {
		ChooseDuration(box, form);
	});
	AddSkip(container);
	AddDividerText(container, tr::lng_gift_offer_onchain_duration_about());

	const auto press = crl::guard(form, [=] {
		form->press();
	});
	field->changes() | rpl::on_next([=] {
		form->setAmount(ParseAmount(field->getLastText()));
	}, field->lifetime());
	field->submits() | rpl::to_empty | rpl::on_next(press, field->lifetime());
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	const auto button = box->addButton(
		form->amountValue(
		) | rpl::map([](std::optional<int64> amount) {
			return amount
				? tr::lng_suggest_options_offer(
					lt_amount,
					rpl::single(Data::FormatGiftGramAmountWithUnit(*amount)))
				: tr::lng_gift_offer_button();
		}) | rpl::flatten_latest(),
		press).data();
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::suggestPriceBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		button->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(button, loading);
	loading->showOn(form->busyValue());
	rpl::combine(
		form->stateValue(),
		form->busyValue(),
		rpl::merge(
			rpl::single(rpl::empty),
			style::PaletteChanged(),
			field->changes())
	) | rpl::on_next([=](FormState state, bool busy, rpl::empty_value) {
		const auto ready = (state == FormState::Ready);
		const auto failing = Failing(state);
		button->setDisabled(busy || !ready);
		button->setTextFgOverride(busy
			? std::make_optional(QColor(Qt::transparent))
			: ready
			? std::optional<QColor>()
			: std::make_optional(st::windowSubTextFg->c));
		note->setTextColorOverride(failing
			? std::make_optional(st::boxTextFgError->c)
			: std::optional<QColor>());
		if (failing) {
			field->showErrorNoFocus();
		} else {
			field->hideError();
		}
	}, box->lifetime());

	const auto close = CreateChild<IconButton>(container, st::boxTitleClose);
	close->setClickedCallback([=] {
		box->closeBox();
	});
	container->widthValue() | rpl::on_next([=](int) {
		close->moveToRight(0, 0);
	}, close->lifetime());

	const auto balance = Settings::AddBalanceWidget(
		container,
		session,
		form->balanceValue(),
		false);
	const auto money = CreateChild<AbstractButton>(container);
	money->setClickedCallback([=] {
		Wallet::ShowWallet(session);
	});
	rpl::combine(
		balance->sizeValue(),
		container->sizeValue()
	) | rpl::on_next([=] {
		balance->moveToLeft(
			st::creditsHistoryRightSkip * 2,
			st::creditsHistoryRightSkip);
		balance->update();
	}, balance->lifetime());
	balance->geometryValue() | rpl::on_next([=](QRect geometry) {
		money->setGeometry(geometry);
	}, money->lifetime());

	form->start();
}

} // namespace

bool CanOfferOnchainGift(
		not_null<Main::Session*> session,
		const Data::UniqueGift &gift) {
	return !gift.ownerId
		&& !gift.ownerAddress.isEmpty()
		&& !gift.giftAddress.isEmpty()
		&& !gift.slug.isEmpty()
		&& !gift.burned
		&& !Data::UniqueGiftOnSale(gift)
		&& session->appConfig().walletAvailable()
		&& !session->supportMode()
		&& session->appConfig().giftOnchainOffersEnabled()
		&& (LookupOwnOnchainGift(session, gift) == OwnOnchainGift::None);
}

void ShowOnchainGiftOfferBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		int limitedCount,
		int limitedLeft) {
	if (!show
		|| !show->valid()
		|| !gift
		|| !CanOfferOnchainGift(&show->session(), *gift)) {
		return;
	}
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	wallet->ensureLoaded();
	if (Absent(wallet->presenceCurrent())) {
		Wallet::ShowWallet(session);
		return;
	}
	show->show(Box(OnchainOfferBox, OnchainGiftOfferRequest{
		.show = show,
		.gift = std::move(gift),
		.limitedCount = limitedCount,
		.limitedLeft = limitedLeft,
	}));
}

} // namespace Ui
