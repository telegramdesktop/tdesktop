/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_suggest_options.h"

#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/controls/ton_common.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace HistoryView {

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
		.title = ((args.mode == SuggestMode::New)
			? tr::lng_suggest_options_date()
			: tr::lng_suggest_menu_edit_time()),
		.submit = ((args.mode == SuggestMode::New)
			? tr::lng_settings_save()
			: tr::lng_suggest_options_update()),
		.done = done,
		.min = [=] { return now + min; },
		.time = value,
		.max = [=] { return now + max; },
	});

	box->addLeftButton(tr::lng_suggest_options_date_any(), [=] {
		done(TimeId());
	});
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
		rpl::variable<TimeId> date;
		rpl::variable<bool> ton;
		bool inButton = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->date = args.value.date;
	state->ton = (args.value.ton != 0);

	const auto limit = args.session->appConfig().suggestedPostStarsMax();

	box->setTitle((args.mode == SuggestMode::New)
		? tr::lng_suggest_options_title()
		: tr::lng_suggest_options_change());

	const auto container = box->verticalLayout();
	state->buttons.push_back({
		.text = Ui::Text::String(
			st::semiboldTextStyle,
			(args.mode == SuggestMode::ChangeAdmin
				? tr::lng_suggest_options_stars_request(tr::now)
				: tr::lng_suggest_options_stars_offer(tr::now))),
		.active = !state->ton.current(),
	});
	state->buttons.push_back({
		.text = Ui::Text::String(
			st::semiboldTextStyle,
			(args.mode == SuggestMode::ChangeAdmin
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
	const auto buttons = box->addRow(object_ptr<Ui::RpWidget>(box));
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

	const auto added = st::boxRowPadding - st::defaultSubsectionTitlePadding;
	const auto manager = &args.session->data().customEmojiManager();
	const auto makeIcon = [&](
			not_null<QWidget*> parent,
			TextWithEntities text) {
		return Ui::CreateChild<Ui::FlatLabel>(
			parent,
			rpl::single(text),
			st::defaultFlatLabel,
			st::defaultPopupMenu,
			Core::TextContext({ .session = args.session }));
	};

	const auto starsWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto starsInner = starsWrap->entity();

	Ui::AddSubsectionTitle(
		starsInner,
		tr::lng_suggest_options_stars_price(),
		QMargins(added.left(), 0, added.right(), 0));

	const auto starsFieldWrap = starsInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::editTagField.heightMin),
		st::boxRowPadding);
	auto ownedStarsField = object_ptr<Ui::NumberInput>(
		starsFieldWrap,
		st::editTagField,
		rpl::single(u"0"_q),
		((args.value.exists && args.value.priceWhole && !args.value.ton)
			? QString::number(args.value.priceWhole)
			: QString()),
		limit);
	const auto starsField = ownedStarsField.data();
	const auto starsIcon = makeIcon(starsField, manager->creditsEmoji());

	starsFieldWrap->widthValue() | rpl::start_with_next([=](int width) {
		starsIcon->move(st::starsFieldIconPosition);
		starsField->move(0, 0);
		starsField->resize(width, starsField->height());
		starsFieldWrap->resize(width, starsField->height());
	}, starsFieldWrap->lifetime());

	Ui::AddSkip(starsInner);
	Ui::AddSkip(starsInner);
	Ui::AddDividerText(
		starsInner,
		tr::lng_suggest_options_stars_price_about());

	const auto tonWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto tonInner = tonWrap->entity();

	Ui::AddSubsectionTitle(
		tonInner,
		tr::lng_suggest_options_ton_price(),
		QMargins(added.left(), 0, added.right(), 0));

	const auto tonFieldWrap = tonInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::editTagField.heightMin),
		st::boxRowPadding);
	auto ownedTonField = object_ptr<Ui::InputField>::fromRaw(
		Ui::CreateTonAmountInput(
			tonFieldWrap,
			rpl::single('0' + Ui::TonAmountSeparator() + '0'),
			((args.value.price() && args.value.ton)
				? (int64(args.value.priceWhole) * Ui::kNanosInOne
					+ int64(args.value.priceNano))
				: 0)));
	const auto tonField = ownedTonField.data();
	const auto tonIcon = makeIcon(tonField, Ui::Text::SingleCustomEmoji(
		manager->registerInternalEmoji(
			Ui::Earn::IconCurrencyColored(
				st::tonFieldIconSize,
				st::windowActiveTextFg->c),
			st::channelEarnCurrencyCommonMargins,
			false)));

	tonFieldWrap->widthValue() | rpl::start_with_next([=](int width) {
		tonIcon->move(st::tonFieldIconPosition);
		tonField->move(0, 0);
		tonField->resize(width, tonField->height());
		tonFieldWrap->resize(width, tonField->height());
	}, tonFieldWrap->lifetime());

	Ui::AddSkip(tonInner);
	Ui::AddSkip(tonInner);
	Ui::AddDividerText(
		tonInner,
		tr::lng_suggest_options_ton_price_about());

	tonWrap->toggleOn(state->ton.value(), anim::type::instant);
	starsWrap->toggleOn(
		state->ton.value() | rpl::map(!rpl::mappers::_1),
		anim::type::instant);

	box->setFocusCallback([=] {
		if (state->ton.current()) {
			tonField->selectAll();
			tonField->setFocusFast();
		} else {
			starsField->selectAll();
			starsField->setFocusFast();
		}
	});

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
		const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
		const auto parentWeak = Ui::MakeWeak(box);
		const auto done = [=](TimeId result) {
			if (parentWeak) {
				state->date = result;
			}
			if (const auto strong = weak->data()) {
				strong->closeBox();
			}
		};
		auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
			.session = args.session,
			.done = done,
			.value = state->date.current(),
			.mode = args.mode,
		});
		*weak = dateBox.data();
		box->uiShow()->show(std::move(dateBox));
	});

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_suggest_options_date_about());
	AssertIsDebug()//tr::lng_suggest_options_offer
	const auto save = [=] {
		auto nanos = int64();
		if (state->ton.current()) {
			const auto now = Ui::ParseTonAmountString(
				tonField->getLastText());
			if (!now || (*now < 0) || (*now > limit * Ui::kNanosInOne)) {
				tonField->showError();
				return;
			}
			nanos = *now;
		} else {
			const auto now = starsField->getLastText().toLongLong();
			if (now < 0 || now > limit) {
				starsField->showError();
				return;
			}
			nanos = now * Ui::kNanosInOne;
		}
		const auto value = CreditsAmount(
			nanos / Ui::kNanosInOne,
			nanos % Ui::kNanosInOne);
		args.done({
			.exists = true,
			.priceWhole = uint32(value.whole()),
			.priceNano = uint32(value.nano()),
			.ton = uint32(state->ton.current() ? 1 : 0),
			.date = state->date.current(),
		});
	};

	QObject::connect(starsField, &Ui::NumberInput::submitted, box, save);
	tonField->submits() | rpl::start_with_next(save, tonField->lifetime());

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
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
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto apply = [=](SuggestPostOptions values) {
		_values = values;
		updateTexts();
		_updates.fire({});
		if (const auto strong = weak->data()) {
			strong->closeBox();
		}
	};
	*weak = _show->show(Box(ChooseSuggestPriceBox, SuggestPriceBoxArgs{
		.session = &_peer->session(),
		.done = apply,
		.value = _values,
	}));
}

void SuggestOptions::updateTexts() {
	_title.setText(
		st::semiboldTextStyle,
		((_mode == SuggestMode::New)
			? tr::lng_suggest_bar_title(tr::now)
			: tr::lng_suggest_options_change(tr::now)));
	_text.setMarkedText(st::defaultTextStyle, composeText());
}

TextWithEntities SuggestOptions::composeText() const {
	if (!_values.price() && !_values.date) {
		return tr::lng_suggest_bar_text(tr::now, Ui::Text::WithEntities);
	} else if (!_values.date && _values.price().ton()) {
		return tr::lng_suggest_bar_priced(AssertIsDebug()
			tr::now,
			lt_amount,
			TextWithEntities{ Lang::FormatCreditsAmountDecimal(_values.price()) + " TON" },
			Ui::Text::WithEntities);
	} else if (!_values.date) {
		return tr::lng_suggest_bar_priced(
			tr::now,
			lt_amount,
			TextWithEntities{ Lang::FormatCreditsAmountDecimal(_values.price()) + " stars" },
			Ui::Text::WithEntities);
	} else if (!_values.price()) {
		return tr::lng_suggest_bar_dated(
			tr::now,
			lt_date,
			TextWithEntities{
				langDateTime(base::unixtime::parse(_values.date)),
			},
			Ui::Text::WithEntities);
	} else if (_values.price().ton()) {
		return tr::lng_suggest_bar_priced_dated(
			tr::now,
			lt_amount,
			TextWithEntities{ Lang::FormatCreditsAmountDecimal(_values.price()) + " TON," },
			lt_date,
			TextWithEntities{
				langDateTime(base::unixtime::parse(_values.date)),
			},
			Ui::Text::WithEntities);
	}
	return tr::lng_suggest_bar_priced_dated(
		tr::now,
		lt_amount,
		TextWithEntities{ Lang::FormatCreditsAmountDecimal(_values.price()) + " stars," },
		lt_date,
		TextWithEntities{
			langDateTime(base::unixtime::parse(_values.date)),
		},
		Ui::Text::WithEntities);
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
