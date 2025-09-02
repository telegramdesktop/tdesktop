/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_suggest_options.h"

#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/components/credits.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/controls/ton_common.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace HistoryView {
namespace {

[[nodiscard]] rpl::producer<CreditsAmount> StarsPriceValue(
		rpl::producer<CreditsAmount> full) {
	return rpl::single(
		CreditsAmount()
	) | rpl::then(std::move(
		full
	) | rpl::filter([=](CreditsAmount amount) {
		return amount.stars();
	}));
}

[[nodiscard]] rpl::producer<CreditsAmount> TonPriceValue(
		rpl::producer<CreditsAmount> full) {
	return rpl::single(
		CreditsAmount()
	) | rpl::then(std::move(
		full
	) | rpl::filter([=](CreditsAmount amount) {
		return amount.ton();
	}));
}

} // namespace

void ChooseSuggestTimeBox(
		not_null<Ui::GenericBox*> box,
		SuggestTimeBoxArgs &&args) {
	const auto now = base::unixtime::now();
	const auto min = args.session->appConfig().suggestedPostDelayMin() + 60;
	const auto max = args.session->appConfig().suggestedPostDelayMax();
	const auto value = args.value
		? std::clamp(args.value, now + min, now + max)
		: (now + 86400);
	const auto done = args.done;
	Ui::ChooseDateTimeBox(box, {
		.title = ((args.mode == SuggestMode::New
			|| args.mode == SuggestMode::Publish)
			? tr::lng_suggest_options_date()
			: tr::lng_suggest_menu_edit_time()),
		.submit = ((args.mode == SuggestMode::Publish)
			? tr::lng_suggest_options_date_publish()
			: (args.mode == SuggestMode::New)
			? tr::lng_settings_save()
			: tr::lng_suggest_options_update_date()),
		.done = done,
		.min = [=] { return now + min; },
		.time = value,
		.max = [=] { return now + max; },
	});

	box->addLeftButton((args.mode == SuggestMode::Publish)
		? tr::lng_suggest_options_date_now()
		: tr::lng_suggest_options_date_any(), [=] {
		done(TimeId());
	});
}

void AddApproximateUsd(
		not_null<QWidget*> field,
		not_null<Main::Session*> session,
		rpl::producer<CreditsAmount> price) {
	auto value = std::move(price) | rpl::map([=](CreditsAmount amount) {
		if (!amount) {
			return QString();
		}
		const auto appConfig = &session->appConfig();
		const auto rate = amount.ton()
			? appConfig->currencyWithdrawRate()
			: (appConfig->starsWithdrawRate() / 100.);
		return Info::ChannelEarn::ToUsd(amount, rate, 2);
	});
	const auto usd = Ui::CreateChild<Ui::FlatLabel>(
		field,
		std::move(value),
		st::suggestPriceEstimate);
	const auto move = [=] {
		usd->moveToRight(0, st::suggestPriceEstimateTop);
	};
	base::install_event_filter(field, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			move();
		}
		return base::EventFilterResult::Continue;
	});
	usd->widthValue() | rpl::start_with_next(move, usd->lifetime());
}

StarsTonPriceInput AddStarsTonPriceInput(
		not_null<Ui::VerticalLayout*> container,
		StarsTonPriceArgs &&args) {
	struct State {
		rpl::variable<bool> ton;
		rpl::variable<CreditsAmount> price;
		rpl::event_stream<> updates;
		rpl::event_stream<> submits;
	};
	const auto state = container->lifetime().make_state<State>();
	state->ton = std::move(args.showTon);
	state->price = args.price;

	const auto session = args.session;
	const auto added = st::boxRowPadding - st::defaultSubsectionTitlePadding;
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto makeIcon = [&](
			not_null<QWidget*> parent,
			Ui::Text::PaletteDependentEmoji emoji) {
		auto text = helper.paletteDependent(std::move(emoji));
		return Ui::CreateChild<Ui::FlatLabel>(
			parent,
			rpl::single(std::move(text)),
			st::defaultFlatLabel,
			st::defaultPopupMenu,
			helper.context());
	};

	const auto starsWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto starsInner = starsWrap->entity();

	Ui::AddSubsectionTitle(
		starsInner,
		tr::lng_suggest_options_stars_price(),
		QMargins(
			added.left(),
			0,
			added.right(),
			-st::defaultSubsectionTitlePadding.bottom()));

	const auto starsFieldWrap = starsInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			starsInner,
			st::editTagField.heightMin),
		st::boxRowPadding);
	auto ownedStarsField = object_ptr<Ui::NumberInput>(
		starsFieldWrap,
		st::editTagField,
		rpl::single(u"0"_q),
		((args.price && args.price.stars())
			? QString::number(args.price.whole())
			: QString()),
		args.starsMax);
	const auto starsField = ownedStarsField.data();
	const auto starsIcon = makeIcon(
		starsField,
		Ui::Earn::IconCreditsEmoji());

	starsFieldWrap->widthValue() | rpl::start_with_next([=](int width) {
		starsIcon->move(st::starsFieldIconPosition);
		starsField->move(0, 0);
		starsField->resize(width, starsField->height());
		starsFieldWrap->resize(width, starsField->height());
	}, starsFieldWrap->lifetime());

	AddApproximateUsd(
		starsField,
		session,
		StarsPriceValue(state->price.value()));

	Ui::AddSkip(starsInner);
	Ui::AddSkip(starsInner);
	if (args.starsAbout) {
		Ui::AddDividerText(starsInner, std::move(args.starsAbout));
	}

	const auto tonWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto tonInner = tonWrap->entity();

	Ui::AddSubsectionTitle(
		tonInner,
		tr::lng_suggest_options_ton_price(),
		QMargins(
			added.left(),
			0,
			added.right(),
			-st::defaultSubsectionTitlePadding.bottom()));

	const auto tonFieldWrap = tonInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			tonInner,
			st::editTagField.heightMin),
		st::boxRowPadding);
	auto ownedTonField = object_ptr<Ui::InputField>::fromRaw(
		Ui::CreateTonAmountInput(
			tonFieldWrap,
			rpl::single('0' + Ui::TonAmountSeparator() + '0'),
			((args.price && args.price.ton())
				? (args.price.whole() * Ui::kNanosInOne + args.price.nano())
				: 0)));
	const auto tonField = ownedTonField.data();
	const auto tonIcon = makeIcon(tonField, Ui::Earn::IconCurrencyEmoji());

	tonFieldWrap->widthValue() | rpl::start_with_next([=](int width) {
		tonIcon->move(st::tonFieldIconPosition);
		tonField->move(0, 0);
		tonField->resize(width, tonField->height());
		tonFieldWrap->resize(width, tonField->height());
	}, tonFieldWrap->lifetime());

	AddApproximateUsd(
		tonField,
		session,
		TonPriceValue(state->price.value()));

	Ui::AddSkip(tonInner);
	Ui::AddSkip(tonInner);
	if (args.tonAbout) {
		Ui::AddDividerText(tonInner, std::move(args.tonAbout));
	}

	tonWrap->toggleOn(state->ton.value(), anim::type::instant);
	starsWrap->toggleOn(
		state->ton.value() | rpl::map(!rpl::mappers::_1),
		anim::type::instant);

	auto computeResult = [=]() -> std::optional<CreditsAmount> {
		auto nanos = int64();
		const auto ton = state->ton.current();
		if (ton) {
			const auto text = tonField->getLastText();
			const auto now = Ui::ParseTonAmountString(text);
			if (now
				&& *now
				&& ((*now < args.nanoTonMin) || (*now > args.nanoTonMax))) {
				tonField->showError();
				return {};
			}
			nanos = now.value_or(0);
		} else {
			const auto now = starsField->getLastText().toLongLong();
			if (now && (now < args.starsMin || now > args.starsMax)) {
				starsField->showError();
				return {};
			}
			nanos = now * Ui::kNanosInOne;
		}
		return CreditsAmount(
			nanos / Ui::kNanosInOne,
			nanos % Ui::kNanosInOne,
			ton ? CreditsType::Ton : CreditsType::Stars);
	};

	const auto updatePrice = [=] {
		if (auto result = computeResult()) {
			state->price = *result;
		}
		state->updates.fire({});
	};
	const auto updateTonFromStars = [=] {
		if (auto result = computeResult(); result && result->stars()) {
			const auto v = Ui::TonFromStars(session, *result);
			const auto amount = v.whole() * Ui::kNanosInOne + v.nano();
			tonField->setText(
				Ui::FormatTonAmount(amount, Ui::TonFormatFlag::Simple).full);
		}
	};
	const auto updateStarsFromTon = [=] {
		if (auto result = computeResult(); result && result->ton()) {
			const auto v = Ui::StarsFromTon(session, *result);
			starsField->setText(QString::number(v.whole()));
		}
	};
	QObject::connect(starsField, &Ui::NumberInput::changed, starsField, [=] {
		if (!state->ton.current()) {
			updatePrice();
			updateTonFromStars();
		}
	});
	tonField->changes(
	) | rpl::start_with_next([=] {
		if (state->ton.current()) {
			updatePrice();
			updateStarsFromTon();
		}
	}, tonField->lifetime());

	state->ton.changes(
	) | rpl::start_with_next(updatePrice, container->lifetime());
	if (state->ton.current()) {
		updateStarsFromTon();
	} else {
		updateTonFromStars();
	}

	QObject::connect(
		starsField,
		&Ui::NumberInput::submitted,
		container,
		[=] { state->submits.fire({}); });
	tonField->submits(
	) | rpl::to_empty | rpl::start_to_stream(
		state->submits,
		tonField->lifetime());

	auto focusCallback = [=] {
		if (state->ton.current()) {
			tonField->selectAll();
			tonField->setFocusFast();
		} else {
			starsField->selectAll();
			starsField->setFocusFast();
		}
	};

	return {
		.focusCallback = std::move(focusCallback),
		.computeResult = std::move(computeResult),
		.submits = state->submits.events(),
		.updates = state->updates.events(),
		.result = state->price.value(),
	};
}

void ChooseSuggestPriceBox(
		not_null<Ui::GenericBox*> box,
		SuggestPriceBoxArgs &&args) {
	struct Button {
		QRect geometry;
		Ui::Text::String text;
		bool active = false;
	};
	struct State {
		std::vector<Button> buttons;
		rpl::event_stream<> fieldsChanges;
		rpl::variable<CreditsAmount> price;
		rpl::variable<TimeId> date;
		rpl::variable<bool> ton;
		Fn<std::optional<CreditsAmount>()> computePrice;
		Fn<void()> save;
		bool savePending = false;
		bool inButton = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->date = args.value.date;
	state->ton = (args.value.ton != 0);
	state->price = args.value.price();

	const auto peer = args.peer;
	const auto admin = peer->amMonoforumAdmin();
	const auto broadcast = peer->monoforumBroadcast();
	const auto usePeer = broadcast ? broadcast : peer;
	const auto session = &peer->session();
	const auto &appConfig = session->appConfig();
	if (!admin) {
		session->credits().load();
		session->credits().tonLoad();
	}
	const auto container = box->verticalLayout();

	box->setStyle(st::suggestPriceBox);

	auto title = (args.mode == SuggestMode::New)
		? tr::lng_suggest_options_title()
		: tr::lng_suggest_options_change();
	if (admin) {
		box->setTitle(std::move(title));
	} else {
		box->setNoContentMargin(true);

		Ui::AddSkip(container, st::boxTitleHeight * 1.1);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(title),
				st::settingsPremiumUserTitle),
			style::al_top);
	}

	state->buttons.push_back({
		.text = Ui::Text::String(
			st::semiboldTextStyle,
			(admin
				? tr::lng_suggest_options_stars_request(tr::now)
				: tr::lng_suggest_options_stars_offer(tr::now))),
		.active = !state->ton.current(),
	});
	state->buttons.push_back({
		.text = Ui::Text::String(
			st::semiboldTextStyle,
			(admin
				? tr::lng_suggest_options_ton_request(tr::now)
				: tr::lng_suggest_options_ton_offer(tr::now))),
		.active = state->ton.current(),
	});

	auto x = 0;
	auto y = st::giftBoxTabsMargin.top();
	const auto padding = st::giftBoxTabPadding;
	for (auto &button : state->buttons) {
		const auto width = button.text.maxWidth();
		const auto height = st::semiboldTextStyle.font->height;
		const auto r = QRect(0, 0, width, height).marginsAdded(padding);
		button.geometry = QRect(QPoint(x, y), r.size());
		x += r.width() + st::giftBoxTabSkip;
	}
	const auto buttonsSkip = admin ? 0 : st::normalFont->height;
	const auto buttons = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		(st::boxRowPadding
			- QMargins(
				padding.left() / 2,
				-buttonsSkip,
				padding.right() / 2,
				0)));
	const auto height = y
		+ state->buttons.back().geometry.height()
		+ st::giftBoxTabsMargin.bottom();
	buttons->resize(buttons->width(), height);

	buttons->setMouseTracking(true);
	buttons->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		switch (type) {
		case QEvent::MouseMove: {
			const auto in = [&] {
				const auto me = static_cast<QMouseEvent*>(e.get());
				const auto position = me->pos();
				for (const auto &button : state->buttons) {
					if (button.geometry.contains(position)) {
						return true;
					}
				}
				return false;
			}();
			if (state->inButton != in) {
				state->inButton = in;
				buttons->setCursor(in
					? style::cur_pointer
					: style::cur_default);
			}
		} break;
		case QEvent::MouseButtonPress: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			const auto position = me->pos();
			for (auto i = 0, c = int(state->buttons.size()); i != c; ++i) {
				if (state->buttons[i].geometry.contains(position)) {
					state->ton = (i != 0);
					state->buttons[i].active = true;
					state->buttons[1 - i].active = false;
					buttons->update();
					break;
				}
			}
		} break;
		}
	}, buttons->lifetime());

	buttons->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(buttons);
		auto hq = PainterHighQualityEnabler(p);
		const auto padding = st::giftBoxTabPadding;
		for (const auto &button : state->buttons) {
			const auto geometry = button.geometry;
			if (button.active) {
				p.setBrush(st::giftBoxTabBgActive);
				p.setPen(Qt::NoPen);
				const auto radius = geometry.height() / 2.;
				p.drawRoundedRect(geometry, radius, radius);
				p.setPen(st::giftBoxTabFgActive);
			} else {
				p.setPen(st::giftBoxTabFg);
			}
			button.text.draw(p, {
				.position = geometry.marginsRemoved(padding).topLeft(),
				.availableWidth = button.text.maxWidth(),
			});
		}
	}, buttons->lifetime());

	Ui::AddSkip(container);

	const auto computePrice = [session](CreditsAmount amount) {
		return PriceAfterCommission(session, amount).value();
	};
	const auto formatCommission = [session](CreditsAmount amount) {
		return FormatAfterCommissionPercent(session, amount);
	};
	const auto youGet = [=](rpl::producer<CreditsAmount> price, bool stars) {
		return (stars
			? tr::lng_suggest_options_you_get_stars
			: tr::lng_suggest_options_you_get_ton)(
				lt_count_decimal,
				rpl::duplicate(price) | rpl::map(computePrice),
				lt_percent,
				rpl::duplicate(price) | rpl::map(formatCommission));
	};
	auto starsAbout = admin
		? rpl::combine(
			youGet(StarsPriceValue(state->price.value()), true),
			tr::lng_suggest_options_stars_warning(Ui::Text::RichLangValue)
		) | rpl::map([=](const QString &t1, const TextWithEntities &t2) {
			return TextWithEntities{ t1 }.append("\n\n").append(t2);
		})
		: tr::lng_suggest_options_stars_price_about(Ui::Text::WithEntities);
	auto tonAbout = admin
		? youGet(
			TonPriceValue(state->price.value()),
			false
		) | Ui::Text::ToWithEntities()
		: tr::lng_suggest_options_ton_price_about(Ui::Text::WithEntities);
	auto priceInput = AddStarsTonPriceInput(container, {
		.session = session,
		.showTon = state->ton.value(),
		.price = args.value.price(),
		.starsMin = appConfig.suggestedPostStarsMin(),
		.starsMax = appConfig.suggestedPostStarsMax(),
		.nanoTonMin = appConfig.suggestedPostNanoTonMin(),
		.nanoTonMax = appConfig.suggestedPostNanoTonMax(),
		.starsAbout = std::move(starsAbout),
		.tonAbout = std::move(tonAbout),
	});
	state->price = std::move(priceInput.result);
	state->computePrice = std::move(priceInput.computeResult);
	box->setFocusCallback(std::move(priceInput.focusCallback));

	Ui::AddSkip(container);

	const auto time = Settings::AddButtonWithLabel(
		container,
		tr::lng_suggest_options_date(),
		state->date.value() | rpl::map([](TimeId date) {
			return date
				? langDateTime(base::unixtime::parse(date))
				: tr::lng_suggest_options_date_any(tr::now);
		}),
		st::settingsButtonNoIcon);

	time->setClickedCallback([=] {
		const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
		const auto parentWeak = base::make_weak(box);
		const auto done = [=](TimeId result) {
			if (parentWeak) {
				state->date = result;
			}
			if (const auto strong = weak->get()) {
				strong->closeBox();
			}
		};
		auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
			.session = session,
			.done = done,
			.value = state->date.current(),
			.mode = args.mode,
		});
		*weak = dateBox.data();
		box->uiShow()->show(std::move(dateBox));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_suggest_options_date_about());

	state->save = [=] {
		const auto ton = uint32(state->ton.current() ? 1 : 0);
		const auto price = state->computePrice();
		if (!price) {
			return;
		}
		const auto value = *price;
		const auto credits = &session->credits();
		if (!admin && ton) {
			if (!credits->tonLoaded()) {
				state->savePending = true;
				return;
			} else if (credits->tonBalance() < value) {
				box->uiShow()->show(Box(InsufficientTonBox, usePeer, value));
				return;
			}
		} else if (!admin) {
			if (!credits->loaded()) {
				state->savePending = true;
				return;
			}
			const auto required = peer->starsPerMessageChecked()
				+ int(base::SafeRound(value.value()));
			if (credits->balance() < CreditsAmount(required)) {
				using namespace Settings;
				const auto done = [=](SmallBalanceResult result) {
					if (result == SmallBalanceResult::Success
						|| result == SmallBalanceResult::Already) {
						state->save();
					}
				};
				MaybeRequestBalanceIncrease(
					Main::MakeSessionShow(box->uiShow(), session),
					required,
					SmallBalanceForSuggest{ usePeer->id },
					done);
				return;
			}
		}
		args.done({
			.exists = true,
			.priceWhole = uint32(value.whole()),
			.priceNano = uint32(value.nano()),
			.ton = ton,
			.date = state->date.current(),
		});
	};

	const auto credits = &session->credits();
	rpl::combine(
		credits->tonBalanceValue(),
		credits->balanceValue()
	) | rpl::filter([=] {
		return state->savePending;
	}) | rpl::start_with_next([=] {
		state->savePending = false;
		if (const auto onstack = state->save) {
			onstack();
		}
	}, box->lifetime());

	std::move(
		priceInput.submits
	) | rpl::start_with_next(state->save, box->lifetime());

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto button = box->addButton(rpl::single(QString()), state->save);
	const auto coloredTonIcon = helper.paletteDependent(
		Ui::Earn::IconCurrencyEmoji());
	button->setContext(helper.context());
	button->setText(state->price.value(
	) | rpl::map([=](CreditsAmount price) {
		if (args.mode == SuggestMode::Change) {
			return tr::lng_suggest_options_update(
				tr::now,
				Ui::Text::WithEntities);
		} else if (price.empty()) {
			return tr::lng_suggest_options_offer_free(
				tr::now,
				Ui::Text::WithEntities);
		} else if (price.ton()) {
			return tr::lng_suggest_options_offer(
				tr::now,
				lt_amount,
				Ui::Text::IconEmoji(&st::tonIconEmoji).append(
					Lang::FormatCreditsAmountDecimal(price)),
				Ui::Text::WithEntities);
		}
		return tr::lng_suggest_options_offer(
			tr::now,
			lt_amount,
			Ui::Text::IconEmoji(&st::starIconEmoji).append(
				Lang::FormatCreditsAmountDecimal(price)),
			Ui::Text::WithEntities);
	}));
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::suggestPriceBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());

	if (admin) {
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});
	} else {
		const auto close = Ui::CreateChild<Ui::IconButton>(
			container,
			st::boxTitleClose);
		close->setClickedCallback([=] { box->closeBox(); });
		container->widthValue() | rpl::start_with_next([=](int) {
			close->moveToRight(0, 0);
		}, close->lifetime());

		session->credits().load(true);
		session->credits().tonLoad(true);
		const auto balance = Settings::AddBalanceWidget(
			container,
			session,
			rpl::conditional(
				state->ton.value(),
				session->credits().tonBalanceValue(),
				session->credits().balanceValue()),
			false);
		rpl::combine(
			balance->sizeValue(),
			container->sizeValue()
		) | rpl::start_with_next([=](const QSize &, const QSize &) {
			balance->moveToLeft(
				st::creditsHistoryRightSkip * 2,
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}
}

bool CanEditSuggestedMessage(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return !media || media->allowsEditCaption();
}

bool CanAddOfferToMessage(not_null<HistoryItem*> item) {
	const auto history = item->history();
	const auto broadcast = history->peer->monoforumBroadcast();
	return broadcast
		&& !history->amMonoforumAdmin()
		&& !item->Get<HistoryMessageSuggestedPost>()
		&& !item->groupId()
		&& item->isRegular()
		&& !item->isService()
		&& !item->errorTextForForwardIgnoreRights(
			history->owner().history(broadcast)).has_value();
}

CreditsAmount PriceAfterCommission(
		not_null<Main::Session*> session,
		CreditsAmount price) {
	const auto appConfig = &session->appConfig();
	const auto mul = price.stars()
		? appConfig->suggestedPostCommissionStars()
		: appConfig->suggestedPostCommissionTon();
	const auto exact = price.multiplied(mul / 1000.);
	return price.stars()
		? CreditsAmount(exact.whole(), 0, CreditsType::Stars)
		: exact;
}

QString FormatAfterCommissionPercent(
		not_null<Main::Session*> session,
		CreditsAmount price) {
	const auto appConfig = &session->appConfig();
	const auto mul = price.stars()
		? appConfig->suggestedPostCommissionStars()
		: appConfig->suggestedPostCommissionTon();
	return QString::number(mul / 10.) + '%';
}

void InsufficientTonBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		CreditsAmount required) {
	box->setStyle(st::suggestPriceBox);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	auto icon = Settings::CreateLottieIcon(
		box->verticalLayout(),
		{
			.name = u"diamond"_q,
			.sizeOverride = Size(st::changePhoneIconSize),
		},
		{});
	box->setShowFinishedCallback([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	});
	box->addRow(std::move(icon.widget), st::lowTonIconPadding);
	const auto add = required - peer->session().credits().tonBalance();
	const auto nano = add.whole() * Ui::kNanosInOne + add.nano();
	const auto amount = Ui::FormatTonAmount(nano).full;
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_suggest_low_ton_title(tr::now, lt_amount, amount),
			st::boxTitle),
		st::boxRowPadding + st::lowTonTitlePadding,
		style::al_top);
	const auto label = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_suggest_low_ton_text(Ui::Text::RichLangValue),
			st::lowTonText),
		st::boxRowPadding + st::lowTonTextPadding,
		style::al_top);
	label->setTryMakeSimilarLines(true);
	label->resizeToWidth(
		st::boxWidth - st::boxRowPadding.left() - st::boxRowPadding.right());

	const auto url = tr::lng_suggest_low_ton_fragment_url(tr::now);
	const auto button = box->addButton(
		tr::lng_suggest_low_ton_fragment(),
		[=] { UrlClickHandler::Open(url); });
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::suggestPriceBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

SuggestOptions::SuggestOptions(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	SuggestPostOptions values,
	SuggestMode mode)
: _show(std::move(show))
, _peer(peer)
, _mode(mode)
, _values(values) {
	updateTexts();
}

SuggestOptions::~SuggestOptions() = default;

void SuggestOptions::paintIcon(QPainter &p, int x, int y, int outerWidth) {
	st::historySuggestIconActive.paint(
		p,
		QPoint(x, y) + st::historySuggestIconPosition,
		outerWidth);
}

void SuggestOptions::paintBar(QPainter &p, int x, int y, int outerWidth) {
	paintIcon(p, x, y, outerWidth);
	paintLines(p, x + st::historyReplySkip, y, outerWidth);
}

void SuggestOptions::paintLines(QPainter &p, int x, int y, int outerWidth) {
	auto available = outerWidth
		- x
		- st::historyReplyCancel.width
		- st::msgReplyPadding.right();
	p.setPen(st::windowActiveTextFg);
	_title.draw(p, {
		.position = QPoint(x, y + st::msgReplyPadding.top()),
		.availableWidth = available,
	});
	p.setPen(st::windowSubTextFg);
	_text.draw(p, {
		.position = QPoint(
			x,
			y + st::msgReplyPadding.top() + st::msgServiceNameFont->height),
		.availableWidth = available,
	});
}

void SuggestOptions::edit() {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto apply = [=](SuggestPostOptions values) {
		_values = values;
		updateTexts();
		_updates.fire({});
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	*weak = _show->show(Box(ChooseSuggestPriceBox, SuggestPriceBoxArgs{
		.peer = _peer,
		.done = apply,
		.value = _values,
		.mode = _mode,
	}));
}

void SuggestOptions::updateTexts() {
	_title.setText(
		st::semiboldTextStyle,
		((_mode == SuggestMode::New)
			? tr::lng_suggest_bar_title(tr::now)
			: tr::lng_suggest_options_change(tr::now)));
	_text.setMarkedText(
		st::defaultTextStyle,
		composeText(),
		kMarkupTextOptions,
		Core::TextContext({ .session = &_peer->session() }));
}

TextWithEntities SuggestOptions::composeText() const {
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto amount = _values.price().ton()
		? helper.paletteDependent(Ui::Earn::IconCurrencyEmoji({
			.size = st::suggestBarTonIconSize,
			.margin = st::suggestBarTonIconMargins,
		})).append(Lang::FormatCreditsAmountDecimal(_values.price()))
		: helper.paletteDependent(
			Ui::Earn::IconCreditsEmojiSmall()
		).append(Lang::FormatCreditsAmountDecimal(_values.price()));
	const auto date = langDateTime(base::unixtime::parse(_values.date));
	if (!_values.price() && !_values.date) {
		return tr::lng_suggest_bar_text(tr::now, Ui::Text::WithEntities);
	} else if (!_values.date) {
		return tr::lng_suggest_bar_priced(
			tr::now,
			lt_amount,
			amount,
			Ui::Text::WithEntities);
	} else if (!_values.price()) {
		return tr::lng_suggest_bar_dated(
			tr::now,
			lt_date,
			TextWithEntities{ date },
			Ui::Text::WithEntities);
	}
	return TextWithEntities().append(
		amount
	).append("   ").append(
		QString::fromUtf8("\xf0\x9f\x93\x86 ")
	).append(date);
}

SuggestPostOptions SuggestOptions::values() const {
	auto result = _values;
	result.exists = 1;
	return result;
}

rpl::producer<> SuggestOptions::updates() const {
	return _updates.events();
}

rpl::lifetime &SuggestOptions::lifetime() {
	return _lifetime;
}

} // namespace HistoryView
