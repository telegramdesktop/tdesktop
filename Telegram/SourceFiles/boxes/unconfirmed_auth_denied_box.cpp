/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/unconfirmed_auth_denied_box.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Dialogs {

void ShowAuthDeniedBox(
		not_null<Ui::GenericBox*> box,
		float64 count,
		const QString &messageText) {
	box->setStyle(st::showOrBox);
	box->setWidth(st::boxWideWidth);
	const auto buttonPadding = QMargins(
		st::showOrBox.buttonPadding.left(),
		0,
		st::showOrBox.buttonPadding.right(),
		0);
	auto icon = Settings::CreateLottieIcon(
		box,
		{
			.name = u"ban"_q,
			.sizeOverride = st::dialogsSuggestionDeniedAuthLottie,
		},
		st::dialogsSuggestionDeniedAuthLottieMargins);
	Settings::AddLottieIconWithCircle(
		box->verticalLayout(),
		std::move(icon.widget),
		st::settingsBlockedListIconPadding,
		st::dialogsSuggestionDeniedAuthLottieCircle);
	box->setShowFinishedCallback([=, animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	});
	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_unconfirmed_auth_denied_title(
				lt_count,
				rpl::single(count)),
			st::boostCenteredTitle),
		st::showOrTitlePadding + buttonPadding,
		style::al_top);
	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			messageText,
			st::boostText),
		st::showOrAboutPadding + buttonPadding,
		style::al_top);
	Ui::AddSkip(box->verticalLayout());
	const auto warning = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_unconfirmed_auth_denied_warning(tr::bold),
			st::boostText),
		st::showOrAboutPadding + buttonPadding
			+ QMargins(st::boostTextSkip, 0, st::boostTextSkip, 0),
		style::al_top);
	warning->setTextColorOverride(st::attentionButtonFg->c);
	const auto warningBg = Ui::CreateChild<Ui::RpWidget>(
		box->verticalLayout());
	warning->geometryValue() | rpl::on_next([=](QRect r) {
		warningBg->setGeometry(r + Margins(st::boostTextSkip));
	}, warningBg->lifetime());
	warningBg->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::attentionButtonBgOver);
		p.drawRoundedRect(
			warningBg->rect(),
			st::buttonRadius,
			st::buttonRadius);
	});
	warningBg->show();
	warning->raise();
	warningBg->stackUnder(warning);
	const auto confirm = box->addButton(
		object_ptr<Ui::RoundButton>(
			box,
			rpl::single(QString()),
			st::defaultActiveButton));
	confirm->setClickedCallback([=] {
		box->closeBox();
	});
	confirm->resize(
		st::showOrShowButton.width,
		st::showOrShowButton.height);

	const auto textLabel = Ui::CreateChild<Ui::FlatLabel>(
		confirm,
		tr::lng_archive_hint_button(),
		st::defaultSubsectionTitle);
	textLabel->setTextColorOverride(st::defaultActiveButton.textFg->c);
	textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto timerLabel = Ui::CreateChild<Ui::FlatLabel>(
		confirm,
		rpl::single(QString()),
		st::defaultSubsectionTitle);
	timerLabel->setTextColorOverride(
		anim::with_alpha(st::defaultActiveButton.textFg->c, 0.75));
	timerLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	constexpr auto kTimer = 5;
	const auto remaining = confirm->lifetime().make_state<int>(kTimer);
	const auto timerLifetime
		= confirm->lifetime().make_state<rpl::lifetime>();
	const auto timer = timerLifetime->make_state<base::Timer>([=] {
		if ((*remaining) > 0) {
			timerLabel->setText(QString::number((*remaining)--));
		} else {
			timerLabel->hide();
			confirm->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			box->setCloseByEscape(true);
			box->setCloseByOutsideClick(true);
			timerLifetime->destroy();
		}
	});
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	confirm->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	timerLabel->setText(QString::number((*remaining)));
	timer->callEach(1000);

	rpl::combine(
		confirm->sizeValue(),
		textLabel->sizeValue(),
		timerLabel->sizeValue(),
		timerLabel->shownValue()
	) | rpl::on_next([=](QSize btn, QSize text, QSize timer, bool shown) {
		const auto skip = st::normalFont->spacew;
		const auto totalWidth = shown
			? (text.width() + skip + timer.width())
			: text.width();
		const auto left = (btn.width() - totalWidth) / 2;
		textLabel->moveToLeft(left, (btn.height() - text.height()) / 2);
		timerLabel->moveToLeft(
			left + text.width() + skip,
			(btn.height() - timer.height()) / 2);
	}, confirm->lifetime());
}

} // namespace Dialogs
