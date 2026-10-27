/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_balance_pill.h"

#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_user_wallet_card.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_session.h"

#include "styles/style_info.h"
#include "styles/style_info_profile_top_bar.h"

namespace Info::Profile {
namespace {

constexpr auto kFadeStart = 0.6;

[[nodiscard]] float64 FigureHeight(const style::font &font) {
	return -font->metrics().tightBoundingRect(u"0123456789"_q).top();
}

[[nodiscard]] Wallet::AmountParts BalanceParts(int64 balance) {
	return {
		.whole = Ui::FormatTonAmount(balance).wholeString,
		.fraction = (balance % Ui::kNanosInOne)
			? Wallet::GramMinorPart(balance)
			: QString(),
	};
}

[[nodiscard]] rpl::producer<std::optional<int64>> ProfileBalanceValue(
		not_null<UserData*> user) {
	auto cardBalance = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::WalletCard
	) | rpl::map([=]() -> std::optional<int64> {
		const auto card = user->walletCard();
		if (!card || card->balance < 0
			|| (!user->isSelf() && !card->balance)) {
			return std::nullopt;
		}
		return card->balance;
	});
	if (!user->isSelf()) {
		return cardBalance;
	}
	const auto session = &user->session();
	return rpl::combine(
		std::move(cardBalance),
		session->wallet().presenceValue(),
		session->wallet().balanceNanoValue(),
		session->appConfig().value() | rpl::map([=] {
			return session->appConfig().walletAvailable();
		})
	) | rpl::map([=](
			std::optional<int64> card,
			Wallet::Presence presence,
			int64 balance,
			bool available) -> std::optional<int64> {
		if (user->walletCard()) {
			return card;
		}
		return (available && presence == Wallet::Presence::Ready && balance >= 0)
			? std::make_optional(balance)
			: std::nullopt;
	});
}

} // namespace

BalancePill::BalancePill(
	QWidget *parent,
	not_null<UserData*> user,
	rpl::producer<std::optional<QColor>> edgeColor)
: AbstractButton(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();

	const auto figure = FigureHeight(st::infoProfileBalancePillAmountFont);
	const auto arrow = st::infoProfileBalancePillArrow.height();
	_baseline = int(base::SafeRound(
		(st::infoProfileBalancePillHeight + figure) / 2.));
	_arrowTop = int(base::SafeRound(_baseline - (figure + arrow) / 2.));

	std::move(
		edgeColor
	) | rpl::on_next([=](std::optional<QColor> color) {
		if (_edgeColor != color || _icon.isNull()) {
			_edgeColor = color;
			refreshIcon();
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		refreshIcon();
	}, lifetime());

	rpl::combine(
		tr::lng_wallet_profile_balance(),
		ProfileBalanceValue(user) | rpl::distinct_until_changed()
	) | rpl::on_next([=](
			const QString &label,
			std::optional<int64> balance) {
		setContent(label, balance);
	}, lifetime());
}

void BalancePill::place(int outerWidth, int statusTop, float64 progress) {
	const auto opacity = std::clamp(
		(progress - kFadeStart) / (1. - kFadeStart),
		0.,
		1.);
	if (_opacity != opacity) {
		_opacity = opacity;
		++_appearanceRevision;
		update();
	}
	const auto transparent = (_opacity <= 0.);
	if (testAttribute(Qt::WA_TransparentForMouseEvents) != transparent) {
		setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
		if (transparent) {
			clearState();
		}
	}
	_top = statusTop + st::infoProfileTopBarBalanceSkip;
	if (_outerWidth != outerWidth) {
		_outerWidth = outerWidth;
		refreshGeometry();
	} else {
		move((_outerWidth - width()) / 2, _top);
	}
}

bool BalancePill::actionable() const {
	return !isHidden() && (_opacity > 0.);
}

uint64 BalancePill::appearanceRevision() const {
	return _appearanceRevision;
}

QImage BalancePill::grabEmpty() {
	const auto was = std::exchange(_contentHidden, true);
	const auto restore = gsl::finally([&] {
		_contentHidden = was;
	});
	return Ui::GrabWidgetToImage(parentWidget(), geometry());
}

void BalancePill::paintContentAlone(QPainter &p) const {
	if (_opacity <= 0.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto was = p.opacity();
	p.setOpacity(was * _opacity);
	paintContent(p);
	p.setOpacity(was);
}

void BalancePill::setContent(
		const QString &label,
		std::optional<int64> balance) {
	if (!balance) {
		if (!isHidden()) {
			++_appearanceRevision;
		}
		hide();
		return;
	}
	const auto parts = BalanceParts(*balance);
	if (isHidden()
		|| _label != label
		|| _amount.parts().whole != parts.whole
		|| _amount.parts().fraction != parts.fraction) {
		++_appearanceRevision;
	}
	_label = label;
	_amount.setContent({
		.big = st::infoProfileBalancePillAmountFont,
		.small = st::infoProfileBalancePillFractionFont,
	}, parts);
	refreshGeometry();
	accessibilityNameChanged();
	update();
	show();
}

void BalancePill::refreshIcon() {
	++_appearanceRevision;
	const auto tint = _edgeColor
		? Ui::BlendColors(
			*_edgeColor,
			st::groupCallMembersFg->c,
			st::infoProfileBalancePillColoredIconTint)
		: st::currencyFg->c;
	_icon = Ui::Earn::IconCurrencyTwoTone(
		st::infoProfileBalancePillIconSize,
		tint);
	const auto &font = st::infoProfileBalancePillAmountFont;
	const auto ratio = style::DevicePixelRatio();
	const auto top = _baseline
		- font->ascent
		+ Ui::Earn::AlignedMarkTop(font, _icon);
	_iconTop = std::round(top * ratio) / ratio;
	update();
}

void BalancePill::refreshGeometry() {
	if (_outerWidth <= 0) {
		return;
	}
	const auto &font = st::infoProfileBalancePillLabelFont;
	const auto &padding = st::infoProfileBalancePillPadding;
	const auto labelSkip = st::infoProfileBalancePillLabelSkip;
	const auto fixed = padding.left()
		+ st::infoProfileBalancePillIconSize
		+ st::infoProfileBalancePillIconSkip
		+ st::infoProfileBalancePillArrowSkip
		+ st::infoProfileBalancePillArrow.width()
		+ padding.right();
	const auto available = _outerWidth
		- rect::m::sum::h(st::infoProfileTopBarActionButtonsPadding)
		- fixed;
	const auto labelAvailable = available
		- _amount.naturalWidth()
		- labelSkip;
	_labelShown = (font->width(_label) <= labelAvailable)
		? _label
		: (labelAvailable >= font->elidew)
		? font->elided(_label, labelAvailable)
		: QString();
	_labelSpace = _labelShown.isEmpty()
		? 0
		: (font->width(_labelShown) + labelSkip);
	_amount.setAvailableWidth(std::max(available - _labelSpace, 1));
	resize(
		fixed + _labelSpace + int(std::ceil(_amount.size().width())),
		st::infoProfileBalancePillHeight);
	move((_outerWidth - width()) / 2, _top);
}

void BalancePill::paintEvent(QPaintEvent *e) {
	if (_opacity <= 0.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_opacity);
	paintBackground(p);
	if (!_contentHidden) {
		paintContent(p);
	}
}

void BalancePill::paintBackground(QPainter &p) const {
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(_edgeColor
		? anim::with_alpha(
			st::groupCallMembersFg->c,
			st::infoProfileBalancePillColoredBgOpacity)
		: anim::with_alpha(
			st::boxBg->c,
			1. - st::infoProfileTopBarActionButtonBgOpacity));
	p.drawRoundedRect(rect(), radius, radius);
}

void BalancePill::paintContent(QPainter &p) const {
	const auto colored = _edgeColor.has_value();
	const auto fg = colored
		? st::groupCallMembersFg->c
		: st::windowBoldFg->c;
	const auto &padding = st::infoProfileBalancePillPadding;
	if (!_labelShown.isEmpty()) {
		p.setFont(st::infoProfileBalancePillLabelFont);
		p.setPen(colored
			? st::groupCallVideoSubTextFg->c
			: st::windowSubTextFg->c);
		p.drawText(padding.left(), _baseline, _labelShown);
	}
	const auto iconLeft = padding.left() + _labelSpace;
	p.drawImage(QPointF(iconLeft, _iconTop), _icon);

	const auto amountLeft = iconLeft
		+ st::infoProfileBalancePillIconSize
		+ st::infoProfileBalancePillIconSkip;
	const auto amountTop = _baseline - _amount.baseline() * _amount.scale();
	_amount.paint(p, QPointF(amountLeft, amountTop), { .digits = fg });

	const auto &arrow = st::infoProfileBalancePillArrow;
	arrow.paint(
		p,
		width() - padding.right() - arrow.width(),
		_arrowTop,
		width(),
		fg);
}

QString BalancePill::accessibilityName() {
	const auto &parts = _amount.parts();
	return tr::lng_wallet_send_balance(
		tr::now,
		lt_amount,
		parts.whole + parts.fraction);
}

} // namespace Info::Profile
