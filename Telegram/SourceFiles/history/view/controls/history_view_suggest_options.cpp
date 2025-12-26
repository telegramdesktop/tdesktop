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
#include "history/history_item_helpers.h"
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
#include "ui/boxes/single_choice_box.h"
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

[[nodiscard]] not_null<Ui::RpWidget*> AddMoneyInputIcon(
		not_null<QWidget*> parent,
		Ui::Text::PaletteDependentEmoji emoji) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto text = helper.paletteDependent(std::move(emoji));
	return Ui::CreateChild<Ui::FlatLabel>(
		parent,
		rpl::single(std::move(text)),
		st::defaultFlatLabel,
		st::defaultPopupMenu,
		helper.context());
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
			? appConfig->currencySellRate()
			: (appConfig->starsSellRate() / 100.);
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
	usd->widthValue() | rpl::on_next(move, usd->lifetime());
}

not_null<Ui::NumberInput*> AddStarsInputField(
		not_null<Ui::VerticalLayout*> container,
		StarsInputFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editTagField.heightMin),
		st::boxRowPadding);
	const auto result = Ui::CreateChild<Ui::NumberInput>(
		wrap,
		st::editTagField,
		rpl::single(u"0"_q),
		args.value ? QString::number(*args.value) : QString(),
		args.max ? args.max : std::numeric_limits<int>::max());
	const auto icon = AddMoneyInputIcon(
		result,
		Ui::Earn::IconCreditsEmoji());

	wrap->widthValue() | rpl::on_next([=](int width) {
		icon->move(st::starsFieldIconPosition);
		result->move(0, 0);
		result->resize(width, result->height());
		wrap->resize(width, result->height());
	}, wrap->lifetime());

	return result;
}

not_null<Ui::InputField*> AddTonInputField(
		not_null<Ui::VerticalLayout*> container,
		TonInputFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editTagField.heightMin),
		st::boxRowPadding);
	const auto result = Ui::CreateTonAmountInput(
		wrap,
		rpl::single('0' + Ui::TonAmountSeparator() + '0'),
		args.value);
	const auto icon = AddMoneyInputIcon(
		result,
		Ui::Earn::IconCurrencyEmoji());

	wrap->widthValue() | rpl::on_next([=](int width) {
		icon->move(st::tonFieldIconPosition);
		result->move(0, 0);
		result->resize(width, result->height());
		wrap->resize(width, result->height());
	}, wrap->lifetime());

	return result;
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

	const auto starsField = AddStarsInputField(starsInner, {
		.value = ((args.price && args.price.stars())
			? args.price.whole()
			: std::optional<int64>()),
	});

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

	const auto tonField = AddTonInputField(tonInner, {
		.value = (args.price && args.price.ton())
			? (args.price.whole() * Ui::kNanosInOne + args.price.nano())
			: 0,
	});

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
		auto amount = CreditsAmount();
		const auto ton = state->ton.current();
		if (ton) {
			const auto text = tonField->getLastText();
			const auto now = Ui::ParseTonAmountString(text);
			amount = CreditsAmount(
				now.value_or(0) / Ui::kNanosInOne,
				now.value_or(0) % Ui::kNanosInOne,
				CreditsType::Ton);
			const auto bad = (!now || !*now)
				? (!args.allowEmpty)
				: ((*now < args.nanoTonMin) || (*now > args.nanoTonMax));
			if (!bad) {
				return amount;
			}
			tonField->showError();
		} else {
			const auto now = starsField->getLastText().toLongLong();
			amount = CreditsAmount(now);
			const auto bad = !now
				? (!args.allowEmpty)
				: ((now < args.starsMin) || (now > args.starsMax));
			if (!bad) {
				return amount;
			}
			starsField->showError();
		}
		if (const auto hook = args.errorHook) {
			hook(amount);
		}
		return {};
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
	) | rpl::on_next([=] {
		if (state->ton.current()) {
			updatePrice();
			updateStarsFromTon();
		}
	}, tonField->lifetime());

	state->ton.changes(
	) | rpl::on_next(updatePrice, container->lifetime());
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
		rpl::variable<TimeId> offerDuration;
		rpl::variable<bool> ton;
		Fn<std::optional<CreditsAmount>()> computePrice;
		Fn<void()> save;
		std::optional<CreditsAmount> lastSmallPrice;
		bool savePending = false;
		bool inButton = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->date = args.value.date;
	state->ton = (args.value.ton != 0);
	state->price = args.value.price();

	const auto peer = args.peer;
	[[maybe_unused]] const auto details = ComputePaymentDetails(peer, 1);

	const auto mode = args.mode;
	const auto gift = (mode == SuggestMode::Gift);
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

	auto title = gift
		? tr::lng_gift_offer_title()
		: (mode == SuggestMode::New)
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
	buttons->events() | rpl::on_next([=](not_null<QEvent*> e) {
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

	buttons->paintRequest() | rpl::on_next([=] {
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
			tr::lng_suggest_options_stars_warning(tr::rich)
		) | rpl::map([=](const QString &t1, const TextWithEntities &t2) {
			return TextWithEntities{ t1 }.append("\n\n").append(t2);
		})
		: gift
		? tr::lng_gift_offer_stars_about(
			lt_name,
			rpl::single(tr::marked(args.giftName)),
			tr::rich)
		: tr::lng_suggest_options_stars_price_about(tr::rich);
	auto tonAbout = admin
		? youGet(
			TonPriceValue(state->price.value()),
			false
		) | rpl::map(tr::rich)
		: gift
		? tr::lng_gift_offer_ton_about(
			lt_name,
			rpl::single(tr::marked(args.giftName)),
			tr::rich)
		: tr::lng_suggest_options_ton_price_about(tr::rich);
	const auto nanoTonMin = gift
		? appConfig.giftResaleNanoTonMin()
		: appConfig.suggestedPostNanoTonMin();
	const auto nanoTonMax = gift
		? appConfig.giftResaleNanoTonMax()
		: appConfig.suggestedPostNanoTonMax();
	const auto starsMin = gift
		? appConfig.giftResaleStarsMin()
		: appConfig.suggestedPostStarsMin();
	const auto starsMax = gift
		? appConfig.giftResaleStarsMax()
		: appConfig.suggestedPostStarsMax();
	const auto recordBadAmount = [=](CreditsAmount amount) {
		if (false
			|| (amount.ton()
				&& (amount.value()
					> (nanoTonMin + nanoTonMax) / (2. * Ui::kNanosInOne)))
			|| (!amount.ton()
				&& (amount.whole() >= starsMax))) {
			state->lastSmallPrice = {};
			return;
		}
		state->lastSmallPrice = amount;
	};
	auto priceInput = AddStarsTonPriceInput(container, {
		.session = session,
		.showTon = state->ton.value(),
		.price = args.value.price(),
		.starsMin = starsMin,
		.starsMax = starsMax,
		.nanoTonMin = nanoTonMin,
		.nanoTonMax = nanoTonMax,
		.allowEmpty = !gift,
		.errorHook = recordBadAmount,
		.starsAbout = std::move(starsAbout),
		.tonAbout = std::move(tonAbout),
	});
	state->price = std::move(priceInput.result);
	state->computePrice = std::move(priceInput.computeResult);
	box->setFocusCallback(std::move(priceInput.focusCallback));

	Ui::AddSkip(container);

	if (gift) {
		const auto day = 86400;
		auto durations = std::vector{
			day / 4,
			day / 2,
			day,
			day + day / 2,
			day * 2,
			day * 3,
		};
		if (peer->session().isTestMode()) {
			durations.insert(begin(durations), 120);
		}
		const auto durationToText = [](TimeId date) {
			return (date >= 3600)
				? tr::lng_hours(tr::now, lt_count, date / 3600)
				: tr::lng_minutes(tr::now, lt_count, date / 60);
		};
		state->offerDuration = day;
		const auto duration = Settings::AddButtonWithLabel(
			container,
			tr::lng_gift_offer_duration(),
			state->offerDuration.value() | rpl::map(durationToText),
			st::settingsButtonNoIcon);

		duration->setClickedCallback([=] {
			box->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
				const auto save = [=](int index) {
					state->offerDuration = durations[index];
				};
				auto options = durations
					| ranges::views::transform(durationToText)
					| ranges::to_vector;
				const auto selected = ranges::find(
					durations,
					state->offerDuration.current());
				SingleChoiceBox(box, {
					.title = tr::lng_gift_offer_duration(),
					.options = options,
					.initialSelection = int(selected - begin(durations)),
					.callback = save,
				});
			}));
		});

		Ui::AddSkip(container);
		Ui::AddDividerText(
			container,
			tr::lng_gift_offer_duration_about(
				lt_user,
				rpl::single(peer->shortName())));
	} else {
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
			const auto weak = std::make_shared<
				base::weak_qptr<Ui::BoxContent>
			>();
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
	}

	state->save = [=] {
		const auto ton = uint32(state->ton.current() ? 1 : 0);
		const auto price = state->computePrice();
		if (!price) {
			if (const auto amount = state->lastSmallPrice) {
				box->uiShow()->showToast(amount->ton()
					? tr::lng_gift_sell_min_price_ton(
						tr::now,
						lt_count,
						nanoTonMin / float64(Ui::kNanosInOne),
						tr::rich)
					: tr::lng_gift_sell_min_price(
						tr::now,
						lt_count,
						starsMin,
						tr::rich));
			}
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
		}
		const auto requiredStars = peer->starsPerMessageChecked()
			+ (ton ? 0 : int(base::SafeRound(value.value())));
		if (!admin && requiredStars) {
			if (!credits->loaded()) {
				state->savePending = true;
				return;
			}
			if (credits->balance() < CreditsAmount(requiredStars)) {
				using namespace Settings;
				const auto done = [=](SmallBalanceResult result) {
					if (result == SmallBalanceResult::Success
						|| result == SmallBalanceResult::Already) {
						state->save();
					}
				};
				const auto source = gift
					? Settings::SmallBalanceSource(SmallBalanceForOffer())
					: SmallBalanceForSuggest{ usePeer->id };
				MaybeRequestBalanceIncrease(
					Main::MakeSessionShow(box->uiShow(), session),
					requiredStars,
					source,
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
			.offerDuration = state->offerDuration.current(),
		});
	};

	const auto credits = &session->credits();
	rpl::combine(
		credits->tonBalanceValue(),
		credits->balanceValue()
	) | rpl::filter([=] {
		return state->savePending;
	}) | rpl::on_next([=] {
		state->savePending = false;
		if (const auto onstack = state->save) {
			onstack();
		}
	}, box->lifetime());

	std::move(
		priceInput.submits
	) | rpl::on_next(state->save, box->lifetime());

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
				tr::marked);
		} else if (price.empty()) {
			return tr::lng_suggest_options_offer_free(
				tr::now,
				tr::marked);
		} else if (price.ton()) {
			return tr::lng_suggest_options_offer(
				tr::now,
				lt_amount,
				Ui::Text::IconEmoji(&st::tonIconEmoji).append(
					Lang::FormatCreditsAmountDecimal(price)),
				tr::marked);
		}
		return tr::lng_suggest_options_offer(
			tr::now,
			lt_amount,
			Ui::Text::IconEmoji(&st::starIconEmoji).append(
				Lang::FormatCreditsAmountDecimal(price)),
			tr::marked);
	}));
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::suggestPriceBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=] {
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
		container->widthValue() | rpl::on_next([=](int) {
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
		) | rpl::on_next([=](const QSize &, const QSize &) {
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
		&& !item->Get<HistoryMessageSuggestion>()
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
			.sizeOverride = st::normalBoxLottieSize,
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
			tr::lng_suggest_low_ton_text(tr::rich),
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
	}) | rpl::on_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

SuggestOptionsBar::SuggestOptionsBar(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	SuggestOptions values,
	SuggestMode mode)
: _show(std::move(show))
, _peer(peer)
, _mode(mode)
, _values(values) {
	updateTexts();
}

SuggestOptionsBar::~SuggestOptionsBar() = default;

void SuggestOptionsBar::paintIcon(
		QPainter &p,
		int x,
		int y,
		int outerWidth) {
	st::historySuggestIconActive.paint(
		p,
		QPoint(x, y) + st::historySuggestIconPosition,
		outerWidth);
}

void SuggestOptionsBar::paintBar(QPainter &p, int x, int y, int outerWidth) {
	paintIcon(p, x, y, outerWidth);
	paintLines(p, x + st::historyReplySkip, y, outerWidth);
}

void SuggestOptionsBar::paintLines(
		QPainter &p,
		int x,
		int y,
		int outerWidth) {
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

void SuggestOptionsBar::edit() {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto apply = [=](SuggestOptions values) {
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

void SuggestOptionsBar::updateTexts() {
	_title.setText(
		st::semiboldTextStyle,
		((_mode == SuggestMode::New)
			? tr::lng_suggest_bar_title(tr::now)
			: tr::lng_suggest_options_change(tr::now)));

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto text = composeText(helper);
	_text.setMarkedText(
		st::defaultTextStyle,
		text,
		kMarkupTextOptions,
		helper.context());
}

TextWithEntities SuggestOptionsBar::composeText(
		Ui::Text::CustomEmojiHelper &helper) const {
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
		return tr::lng_suggest_bar_text(tr::now, tr::marked);
	} else if (!_values.date) {
		return tr::lng_suggest_bar_priced(
			tr::now,
			lt_amount,
			amount,
			tr::marked);
	} else if (!_values.price()) {
		return tr::lng_suggest_bar_dated(
			tr::now,
			lt_date,
			tr::marked(date),
			tr::marked);
	}
	return tr::marked().append(
		amount
	).append("   ").append(
		QString::fromUtf8("\xf0\x9f\x93\x86 ")
	).append(date);
}

SuggestOptions SuggestOptionsBar::values() const {
	auto result = _values;
	result.exists = 1;
	return result;
}

rpl::producer<> SuggestOptionsBar::updates() const {
	return _updates.events();
}

rpl::lifetime &SuggestOptionsBar::lifetime() {
	return _lifetime;
}

} // namespace HistoryView
