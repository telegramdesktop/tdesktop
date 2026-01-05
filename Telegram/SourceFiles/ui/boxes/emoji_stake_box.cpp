/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/emoji_stake_box.h"

#include "base/event_filter.h"
#include "base/object_ptr.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/components/credits.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h" // CreateLottieIcon
#include "settings/settings_credits_graphics.h" // AddBalanceWidget
#include "ui/controls/ton_common.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/text_lottie_custom_emoji.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h" // confcallLinkFooterOr
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h" // creditsHistoryRightSkip
#include "styles/style_layers.h"
#include "styles/style_settings.h" // settingsCloudPasswordIconSize
#include "styles/style_widgets.h"

#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

[[nodiscard]] not_null<RpWidget*> AddMoneyInputIcon(
		not_null<QWidget*> parent,
		Text::PaletteDependentEmoji emoji) {
	auto helper = Text::CustomEmojiHelper();
	auto text = helper.paletteDependent(std::move(emoji));
	return CreateChild<FlatLabel>(
		parent,
		rpl::single(std::move(text)),
		st::defaultFlatLabel,
		st::defaultPopupMenu,
		helper.context());
}

[[nodiscard]] QImage MakeEmojiFrame(int index, int size) {
	const auto path = u":/gui/dice/dice%1.svg"_q.arg(index);

	const auto ratio = style::DevicePixelRatio();
	auto svg = QSvgRenderer(path);
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto p = QPainter(&result);
	svg.render(&p, QRect(0, 0, size, size));
	p.end();

	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeLogo(not_null<GenericBox*> box) {
	const auto &size = st::settingsCloudPasswordIconSize;
	auto icon = Settings::CreateLottieIcon(
		box->verticalLayout(),
		{ .name = u"dice_6"_q, .sizeOverride = { size, size } },
		QMargins());
	const auto animate = std::move(icon.animate);
	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		animate(anim::repeat::once);
	}, box->lifetime());
	return std::move(icon.widget);
}

[[nodiscard]] object_ptr<RpWidget> MakeTable(
		not_null<RpWidget*> parent,
		const EmojiGameStakeArgs &args) {
	auto result = object_ptr<RpWidget>(parent);
	const auto raw = result.data();

	struct State {
		std::array<QImage, 6> dices;
		std::array<Text::String, 7> multiplicators;
	};
	const auto state = raw->lifetime().make_state<State>();
	const auto serialize = [&](int milliReward) {
		return QString(QChar(0xD7)) + QString::number(milliReward / 1000.);
	};
	const auto esize = st::stakeEmojiSize;
	for (auto i = 0; i != 6; ++i) {
		state->dices[i] = MakeEmojiFrame(i + 1, esize);

		const auto value = (i < args.milliRewards.size())
			? args.milliRewards[i]
			: 0;
		state->multiplicators[i].setText(
			st::semiboldTextStyle,
			serialize(value));
	}
	state->multiplicators[6].setText(
		st::semiboldTextStyle,
		serialize(args.jackpotMilliReward));

	raw->paintOn([=](QPainter &p) {
		auto path = QPainterPath();
		const auto &st = st::defaultTable;
		const auto border = st.border;
		const auto half = border / 2.;
		const auto add = QMargins(border, border, border, border);
		const auto inner = raw->rect().marginsRemoved(add);
		path.addRoundedRect(inner, st.radius, st.radius);
		{
			const auto y = border + inner.height() / 2.;
			path.moveTo(border + half, y);
			path.lineTo(border + inner.width() - half, y);
		}
		{
			const auto x = border + inner.width() / 2.;
			path.moveTo(x, border + half);
			path.lineTo(x, border + inner.height() - half);
		}
		{
			const auto x = border + (inner.width() - border) / 4.;
			path.moveTo(x, border + half);
			path.lineTo(x, border + inner.height() - half);
		}
		{
			const auto x = border
				+ (inner.width() / 2.)
				+ (inner.width() - border) / 4.;
			path.moveTo(x, border + half);
			path.lineTo(x, border + (inner.height() / 2.) - half);
		}
		auto pen = st.borderFg->p;
		pen.setWidth(st.border);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		auto hq = PainterHighQualityEnabler(p);
		p.drawPath(path);

		const auto etop = st::stakeEmojiTop;
		const auto ttop = etop + esize;
		{
			auto left = border + half;
			const auto width = (inner.width() - 3 * border) / 4.;
			auto top = border + half;
			for (auto i = 0; i != 7; ++i, left += width + border) {
				if (i == 4) {
					left -= 4 * (width + border);
					top += border + half + inner.height() / 2.;
				}
				const auto x = int(base::SafeRound(left));
				const auto y = int(base::SafeRound(top));
				const auto right = (i < 6)
					? int(base::SafeRound(left + width))
					: int(base::SafeRound(left + 2 * width + border));
				const auto w = (right - x);

				if (i < 6) {
					p.drawImage(
						x + (w - esize) / 2,
						y + etop,
						state->dices[i]);
				} else {
					const auto &last = state->dices.back();
					for (auto j = 0; j != 3; ++j) {
						p.drawImage(
							x + (w - 3 * esize) / 2 + (esize * j),
							y + etop,
							last);
					}
				}

				p.setPen(st::windowBoldFg);
				const auto &mul = state->multiplicators[i];
				mul.draw(p, {
					.position = { x + (w - mul.maxWidth()) / 2, y + ttop },
				});
			}
		}
	});
	raw->widthValue() | rpl::on_next([=](int width) {
		raw->resize(width, width / 3);
	}, raw->lifetime());
	return result;
}

} // namespace

not_null<NumberInput*> AddStarsInputField(
		not_null<VerticalLayout*> container,
		StarsInputFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<FixedHeightWidget>(
			container,
			st::editTagField.heightMin),
		st::boxRowPadding);
	const auto result = CreateChild<NumberInput>(
		wrap,
		st::editTagField,
		rpl::single(u"0"_q),
		args.value ? QString::number(*args.value) : QString(),
		args.max ? args.max : std::numeric_limits<int>::max());
	const auto icon = AddMoneyInputIcon(
		result,
		Earn::IconCreditsEmoji());

	wrap->widthValue() | rpl::on_next([=](int width) {
		icon->move(st::starsFieldIconPosition);
		result->move(0, 0);
		result->resize(width, result->height());
		wrap->resize(width, result->height());
	}, wrap->lifetime());

	return result;
}

not_null<InputField*> AddTonInputField(
		not_null<VerticalLayout*> container,
		TonInputFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<FixedHeightWidget>(
			container,
			st::editTagField.heightMin),
		st::boxRowPadding);
	const auto result = CreateTonAmountInput(
		wrap,
		rpl::single('0' + TonAmountSeparator() + '0'),
		args.value);
	const auto icon = AddMoneyInputIcon(
		result,
		Earn::IconCurrencyEmoji());

	wrap->widthValue() | rpl::on_next([=](int width) {
		icon->move(st::tonFieldIconPosition);
		result->move(0, 0);
		result->resize(width, result->height());
		wrap->resize(width, result->height());
	}, wrap->lifetime());

	return result;
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

void InsufficientTonBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
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
	const auto add = required - session->credits().tonBalance();
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

void AddStakePresets(
		not_null<VerticalLayout*> container,
		not_null<Main::Session*> session,
		Fn<void(int64)> callback) {
	const auto presets = session->appConfig().stakeDiceNanoTonSuggested();
	if (presets.empty()) {
		return;
	}
	constexpr auto kPerRow = 4;
	const auto count = int(presets.size());
	const auto rows = (count + kPerRow - 1) / kPerRow;
	const auto smallrow = count / rows;
	const auto bigrow = smallrow + 1;
	const auto bigrows = count % rows;
	struct State {
		std::vector<std::vector<not_null<RoundButton*>>> buttons;
	};
	const auto wrap = container->add(
		object_ptr<FixedHeightWidget>(
			container,
			(rows * st::stakePresetButton.height
				+ (rows - 1) * st::stakePresetButtonSkip.y())),
		(st::boxRowPadding
			+ QMargins(0, st::stakeBox.buttonPadding.top(), 0, 0)));
	const auto diamond = QString::fromUtf8(" \xf0\x9f\x92\x8e");
	const auto state = wrap->lifetime().make_state<State>();
	for (auto i = 0; i != rows; ++i) {
		const auto big = (i < bigrows);
		const auto cols = big ? bigrow : smallrow;
		auto &row = state->buttons.emplace_back();
		row.reserve(cols);
		for (auto j = 0; j != cols; ++j) {
			const auto index = i * bigrow + j - std::max(0, i - bigrows);
			const auto nanoTon = presets[index];
			const auto button = CreateChild<RoundButton>(
				wrap,
				rpl::single(FormatTonAmount(nanoTon).full + diamond),
				st::stakePresetButton);
			button->setClickedCallback([=] {
				callback(nanoTon);
			});
			row.push_back(button);
		}
	}
	wrap->widthValue() | rpl::on_next([=](int width) {
		auto y = 0;
		const auto xskip = st::stakePresetButtonSkip.x();
		const auto yskip = st::stakePresetButtonSkip.y();
		for (auto i = 0; i != state->buttons.size(); ++i) {
			const auto &row = state->buttons[i];
			const auto cols = int(row.size());
			const auto singlew = (width - (cols - 1) * xskip) / cols;
			auto x = 0;
			for (const auto button : row) {
				button->setFullWidth(singlew);
				button->move(x, y);
				x += singlew + xskip;
			}
			y += st::stakePresetButton.height + yskip;
		}
	}, wrap->lifetime());
}

void EmojiGameStakeBox(
		not_null<GenericBox*> box,
		EmojiGameStakeArgs &&args) {
	box->setStyle(st::stakeBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	const auto container = box->verticalLayout();

	const auto skip = st::confcallLinkHeaderIconPadding.bottom();
	box->addRow(
		MakeLogo(box),
		st::boxRowPadding + QMargins(0, 0, 0, skip),
		style::al_top);

	auto helper = Text::CustomEmojiHelper();
	const auto beta = helper.paletteDependent(
		Text::CustomEmojiTextBadge(
			tr::lng_stake_game_beta(tr::now).toUpper(),
			st::customEmojiTextBadge));
	const auto sixText = helper.image({
		.image = MakeEmojiFrame(6, st::emojiSize),
		.textColor = false,
	});
	auto title = tr::lng_stake_game_title(
		tr::marked
	) | rpl::map([=](TextWithEntities &&text) {
		return text.append(' ').append(beta);
	});
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(title),
			st::boxTitle,
			st::defaultPopupMenu,
			helper.context()),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_stake_game_about(tr::rich),
			st::confcallLinkCenteredText),
		st::boxRowPadding + QMargins(0, 0, 0, skip),
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto sub = st::boxRowPadding - st::defaultSubsectionTitlePadding;
	const auto subtitlePadding = QMargins(sub.left(), 0, sub.right(), 0);
	AddSubsectionTitle(
		container,
		tr::lng_stake_game_results(),
		subtitlePadding);

	const auto half = (st::defaultTable.border + 1) / 2;
	box->addRow(
		MakeTable(box, args),
		st::boxRowPadding + QMargins(-half, 0, -half, skip / 2),
		style::al_top);

	const auto sixContext = helper.context();
	const auto factory = [=](
			QStringView data,
			const Text::MarkedContext &context) {
		if (auto result = sixContext.customEmojiFactory(data, context)) {
			return std::make_unique<Text::LimitedLoopsEmoji>(
				std::move(result),
				0,
				true);
		}
		return std::unique_ptr<Text::LimitedLoopsEmoji>();
	};
	const auto context = Text::MarkedContext{
		.customEmojiFactory = factory,
	};
	const auto resets = box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_stake_game_resets(
				lt_emoji,
				rpl::single(sixText),
				tr::rich),
			st::confcallLinkFooterOr,
			st::defaultPopupMenu,
			context),
		st::boxRowPadding,
		style::al_top);
	resets->setTryMakeSimilarLines(true);
	resets->setAnimationsPausedCallback([=] {
		return FlatLabel::WhichAnimationsPaused::All;
	});

	AddSubsectionTitle(
		container,
		tr::lng_stake_game_your(),
		subtitlePadding + QMargins(0, skip, 0, sub.bottom()));
	const auto field = AddTonInputField(container, {
		.value = args.currentStake,
	});
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	const auto fromNanoTon = [](int64 nanoton) {
		return CreditsAmount(
			nanoton / Ui::kNanosInOne,
			nanoton % Ui::kNanosInOne,
			CreditsType::Ton);
	};
	const auto price = field->lifetime().make_state<
		rpl::variable<CreditsAmount>
	>(rpl::single(
		args.currentStake
	) | rpl::then(field->changes() | rpl::map([=] {
		return Ui::ParseTonAmountString(field->getLastText()).value_or(0);
	})) | rpl::map(fromNanoTon));
	AddApproximateUsd(
		field,
		args.session,
		price->value());

	AddStakePresets(container, args.session, [=](int64 nanoTon) {
		field->setText(Ui::FormatTonAmount(nanoTon).full);
	});

	const auto submit = args.submit;
	const auto callback = [=] {
		const auto text = field->getLastText();
		const auto now = price->current();
		const auto credits = &args.session->credits();
		const auto appConfig = &args.session->appConfig();
		const auto min = fromNanoTon(appConfig->stakeDiceNanoTonMin());
		const auto max = fromNanoTon(appConfig->stakeDiceNanoTonMax());
		if (!now || now < min || now > max) {
			field->showError();
		} else if (credits->tonBalance() < now) {
			box->uiShow()->show(Box(InsufficientTonBox, args.session, now));
		} else {
			box->closeBox();
			submit(now.whole() * Ui::kNanosInOne + now.nano());
		}
	};
	field->submits() | rpl::on_next(callback, field->lifetime());
	const auto button = box->addButton(rpl::single(QString()), callback);

	button->setText(tr::lng_stake_game_save_and_roll(
	) | rpl::map([=](QString text) {
		return Text::IconEmoji(
			&st::stakeEmojiIcon,
			QString::fromUtf8("\xf0\x9f\x8e\xb2")
		).append(' ').append(text);
	}));

	const auto close = CreateChild<IconButton>(
		container,
		st::boxTitleClose);
	close->setClickedCallback([=] { box->closeBox(); });
	container->widthValue() | rpl::on_next([=](int) {
		close->moveToRight(0, 0);
	}, close->lifetime());

	const auto session = args.session;
	session->credits().tonLoad(true);
	const auto balance = Settings::AddBalanceWidget(
		container,
		session,
		session->credits().tonBalanceValue(),
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

Toast::Config MakeEmojiGameStakeToast(
		std::shared_ptr<Show> show,
		EmojiGameStakeArgs &&args) {
	auto config = Toast::Config{
		.st = &st::historyDiceToast,
		.duration = Ui::Toast::kDefaultDuration * 2,
	};
	auto helper = Text::CustomEmojiHelper();
	static const auto makeBg = [] {
		auto result = st::mediaviewTextLinkFg->c;
		result.setAlphaF(0.12);
		return result;
	};
	struct State {
		State() : bg(makeBg), badge(st::stakeChangeBadge) {
			badge.textBg = badge.textBgOver = bg.color();
		}

		style::complex_color bg;
		style::RoundButton badge;
	};
	auto state = std::make_shared<State>();
	const auto badge = helper.paletteDependent(
		Ui::Text::CustomEmojiTextBadge(
			tr::lng_about_random_stake_change(tr::now),
			state->badge,
			st::stateChangeBadgeMargin));
	const auto diamond = QString::fromUtf8("\xf0\x9f\x92\x8e");
	config.text.append(
		tr::lng_about_random_stake(
			tr::now,
			lt_amount,
			tr::bold(diamond + Ui::FormatTonAmount(args.currentStake).full),
			tr::marked)
	).append(' ').append(
		tr::link(badge, u"internal:stake_change"_q)
	).append(u" "_q.repeated(10)).append(tr::link(
		tr::semibold(tr::lng_about_random_send(tr::now)),
		u"internal:stake_send"_q));
	config.textContext = helper.context();

	config.filter = [=, state = std::move(state)](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			const auto url = handler ? handler->url() : QString();
			if (url == u"internal:stake_change"_q) {
				show->show(Ui::MakeEmojiGameStakeBox(base::duplicate(args)));
			} else if (url == u"internal:stake_send"_q) {
				args.submit(args.currentStake);
			}
		}
		return false;
	};

	return config;
}

} // namespace Ui
