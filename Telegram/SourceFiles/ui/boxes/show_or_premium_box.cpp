/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/show_or_premium_box.h"

#include "base/object_ptr.h"
#include "lang/lang_keys.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui {
namespace {

constexpr auto kShowOrLineOpacity = 0.3;

[[nodiscard]] object_ptr<RpWidget> MakeShowOrPremiumIcon(
		not_null<RpWidget*> parent,
		not_null<const style::icon*> icon) {
	const auto margin = st::showOrIconMargin;
	const auto padding = st::showOrIconPadding;
	const auto inner = padding.top() + icon->height() + padding.bottom();
	const auto full = margin.top() + inner + margin.bottom();
	auto result = object_ptr<FixedHeightWidget>(parent, full);
	const auto raw = result.data();

	raw->resize(st::boxWideWidth, full);
	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto width = raw->width();
		const auto position = QPoint((width - inner) / 2, margin.top());
		const auto rect = QRect(position, QSize(inner, inner));
		const auto shift = QPoint(padding.left(), padding.top());
		p.setPen(Qt::NoPen);
		p.setBrush(st::showOrIconBg);
		p.drawEllipse(rect);
		icon->paint(p, position + shift, width);
	}, raw->lifetime());

	return result;
}

} // namespace

object_ptr<RpWidget> MakeShowOrLabel(
		not_null<RpWidget*> parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<FlatLabel>(
		parent,
		std::move(text),
		st::showOrLabel);
	const auto raw = result.data();

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);

		const auto full = st::showOrLineWidth;
		const auto left = (raw->width() - full) / 2;
		const auto text = raw->textMaxWidth() + 2 * st::showOrLabelSkip;
		const auto fill = (full - text) / 2;
		const auto stroke = st::lineWidth;
		const auto top = st::showOrLineTop;
		p.setOpacity(kShowOrLineOpacity);
		p.fillRect(left, top, fill, stroke, st::windowSubTextFg);
		const auto start = left + full - fill;
		p.fillRect(start, top, fill, stroke, st::windowSubTextFg);
	}, raw->lifetime());

	return result;
}

void ShowOrPremiumBox(
		not_null<GenericBox*> box,
		ShowOrPremium type,
		QString shortName,
		Fn<void()> justShow,
		Fn<void()> toPremium) {
	struct Skin {
		rpl::producer<QString> showTitle;
		rpl::producer<TextWithEntities> showAbout;
		rpl::producer<QString> showButton;
		rpl::producer<QString> orPremium;
		rpl::producer<QString> premiumTitle;
		rpl::producer<TextWithEntities> premiumAbout;
		rpl::producer<QString> premiumButton;
		QString toast;
		const style::icon *icon = nullptr;
	};
	auto skin = (type == ShowOrPremium::LastSeen)
		? Skin{
			tr::lng_lastseen_show_title(),
			tr::lng_lastseen_show_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				Text::RichLangValue),
			tr::lng_lastseen_show_button(),
			tr::lng_lastseen_or(),
			tr::lng_lastseen_premium_title(),
			tr::lng_lastseen_premium_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				Text::RichLangValue),
			tr::lng_lastseen_premium_button(),
			tr::lng_lastseen_shown_toast(tr::now),
			&st::showOrIconLastSeen,
		}
		: (type == ShowOrPremium::ReadTime)
		? Skin{
			tr::lng_readtime_show_title(),
			tr::lng_readtime_show_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				Text::RichLangValue),
			tr::lng_readtime_show_button(),
			tr::lng_readtime_or(),
			tr::lng_readtime_premium_title(),
			tr::lng_readtime_premium_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				Text::RichLangValue),
			tr::lng_readtime_premium_button(),
			tr::lng_readtime_shown_toast(tr::now),
			&st::showOrIconReadTime,
		}
		: Skin();

	box->setStyle(st::showOrBox);
	box->setWidth(st::boxWideWidth);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	box->addRow(MakeShowOrPremiumIcon(box, skin.icon));
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.showTitle),
			st::boostCenteredTitle),
		st::showOrTitlePadding);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.showAbout),
			st::boostText),
		st::showOrAboutPadding);
	const auto show = box->addRow(
		object_ptr<RoundButton>(
			box,
			std::move(skin.showButton),
			st::showOrShowButton),
		QMargins(
			st::showOrBox.buttonPadding.left(),
			0,
			st::showOrBox.buttonPadding.right(),
			0));
	show->setTextTransform(RoundButton::TextTransform::NoTransform);
	box->addRow(
		MakeShowOrLabel(box, std::move(skin.orPremium)),
		st::showOrLabelPadding);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.premiumTitle),
			st::boostCenteredTitle),
		st::showOrTitlePadding);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.premiumAbout),
			st::boostText),
		st::showOrPremiumAboutPadding);

	const auto premium = CreateChild<GradientButton>(
		box.get(),
		Premium::ButtonGradientStops());

	premium->resize(st::showOrShowButton.width, st::showOrShowButton.height);

	const auto label = CreateChild<FlatLabel>(
		premium,
		std::move(skin.premiumButton),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		premium->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());

	box->setShowFinishedCallback([=] {
		premium->startGlareAnimation();
	});

	box->addButton(
		object_ptr<AbstractButton>::fromRaw(premium));

	show->setClickedCallback([box, justShow, toast = skin.toast] {
		justShow();
		box->uiShow()->showToast(toast);
		box->closeBox();
	});
	premium->setClickedCallback(std::move(toPremium));
}

} // namespace Ui
