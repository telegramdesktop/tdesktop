/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/show_or_premium_box.h"

#include "base/object_ptr.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {

constexpr auto kShowOrLineOpacity = 0.3;

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
	) | rpl::on_next([=] {
		auto p = QPainter(raw);

		const auto full = st::showOrLineWidth;
		const auto left = (raw->width() - full) / 2;
		const auto text = raw->naturalWidth() + 2 * st::showOrLabelSkip;
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
		QString lottie;
	};
	auto skin = (type == ShowOrPremium::LastSeen)
		? Skin{
			tr::lng_lastseen_show_title(),
			tr::lng_lastseen_show_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				tr::rich),
			tr::lng_lastseen_show_button(),
			tr::lng_lastseen_or(),
			tr::lng_lastseen_premium_title(),
			tr::lng_lastseen_premium_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				tr::rich),
			tr::lng_lastseen_premium_button(),
			tr::lng_lastseen_shown_toast(tr::now),
			u"show_or_premium_lastseen"_q,
		}
		: Skin{
			tr::lng_readtime_show_title(),
			tr::lng_readtime_show_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				tr::rich),
			tr::lng_readtime_show_button(),
			tr::lng_readtime_or(),
			tr::lng_readtime_premium_title(),
			tr::lng_readtime_premium_about(
				lt_user,
				rpl::single(TextWithEntities{ shortName }),
				tr::rich),
			tr::lng_readtime_premium_button(),
			tr::lng_readtime_shown_toast(tr::now),
			u"show_or_premium_readtime"_q,
		};

	box->setStyle(st::showOrBox);
	box->setWidth(st::boxWideWidth);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	const auto buttonPadding = QMargins(
		st::showOrBox.buttonPadding.left(),
		0,
		st::showOrBox.buttonPadding.right(),
		0);

	auto icon = Settings::CreateLottieIcon(
		box,
		{
			.name = skin.lottie,
			.sizeOverride = st::normalBoxLottieSize
				- Size(st::showOrTitleIconMargin * 2),
		},
		{ 0, st::showOrTitleIconMargin, 0, st::showOrTitleIconMargin });
	Settings::AddLottieIconWithCircle(
		box->verticalLayout(),
		std::move(icon.widget),
		st::settingsBlockedListIconPadding,
		st::normalBoxLottieSize);
	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.showTitle),
			st::boostCenteredTitle),
		st::showOrTitlePadding + buttonPadding,
		style::al_top);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.showAbout),
			st::boostText),
		st::showOrAboutPadding + buttonPadding,
		style::al_top);
	const auto show = box->addRow(
		object_ptr<RoundButton>(
			box,
			std::move(skin.showButton),
			st::showOrShowButton),
		buttonPadding);
	show->setTextTransform(RoundButton::TextTransform::NoTransform);
	box->addRow(
		MakeShowOrLabel(box, std::move(skin.orPremium)),
		st::showOrLabelPadding + buttonPadding,
		style::al_justify);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.premiumTitle),
			st::boostCenteredTitle),
		st::showOrTitlePadding + buttonPadding,
		style::al_top);
	box->addRow(
		object_ptr<FlatLabel>(
			box,
			std::move(skin.premiumAbout),
			st::boostText),
		st::showOrPremiumAboutPadding + buttonPadding,
		style::al_top);

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
	) | rpl::on_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());

	box->setShowFinishedCallback([=, animate = std::move(icon.animate)] {
		premium->startGlareAnimation();
		animate(anim::repeat::once);
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
