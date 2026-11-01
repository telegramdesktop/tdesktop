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
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_fiat.h"

#include "styles/style_info_profile_top_bar.h"

namespace Info::Profile {
namespace {

[[nodiscard]] int FixedWidth() {
	const auto &padding = st::infoProfileBalancePillPadding;
	return padding.left()
		+ st::infoProfileBalancePillIconSize
		+ st::infoProfileBalancePillIconSkip
		+ st::infoProfileBalancePillArrowSkip
		+ st::infoProfileBalancePillArrow.width()
		+ padding.right();
}

[[nodiscard]] Wallet::AmountParts BalanceParts(int64 balance) {
	return {
		.whole = Ui::FormatTonAmount(balance).wholeString,
		.fraction = (balance % Ui::kNanosInOne)
			? Wallet::GramMinorPart(balance)
			: QString(),
	};
}

} // namespace

BalancePill::BalancePill(
	QWidget *parent,
	not_null<UserData*> user,
	rpl::producer<std::optional<QColor>> edgeColor)
: AbstractButton(parent)
, _colors(ComputePillColors(std::nullopt))
, _baseline(PillTextBaseline())
, _arrowTop(PillArrowTop()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();

	std::move(
		edgeColor
	) | rpl::on_next([=](std::optional<QColor> color) {
		if (_edgeColor != color || _icon.isNull()) {
			_edgeColor = color;
			refreshColors();
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		refreshColors();
	}, lifetime());

	rpl::combine(
		tr::lng_wallet_profile_balance(),
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::WalletCard
		) | rpl::map([=] {
			const auto card = user->walletCard();
			return card ? card->balance : int64();
		}) | rpl::distinct_until_changed()
	) | rpl::on_next([=](const QString &label, int64 balance) {
		setContent(label, balance);
	}, lifetime());
}

void BalancePill::setOpacity(float64 opacity) {
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
}

int BalancePill::fittedWidth() const {
	return _fittedWidth;
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

void BalancePill::setContent(const QString &label, int64 balance) {
	if (balance <= 0) {
		if (!isHidden()) {
			++_appearanceRevision;
		}
		hide();
		return;
	}
	const auto parts = BalanceParts(balance);
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
	const auto labelSpace = _label.isEmpty()
		? 0
		: (st::infoProfileBalancePillLabelFont->width(_label)
			+ st::infoProfileBalancePillLabelSkip);
	setNaturalWidth(FixedWidth() + labelSpace + _amount.naturalWidth());
	refreshLayout(width());
	accessibilityNameChanged();
	update();
	show();
}

void BalancePill::refreshColors() {
	++_appearanceRevision;
	_colors = ComputePillColors(_edgeColor);
	const auto tint = _edgeColor
		? Ui::BlendColors(
			*_edgeColor,
			_colors.primary,
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

int BalancePill::resizeGetHeight(int newWidth) {
	refreshLayout(newWidth);
	return st::infoProfileBalancePillHeight;
}

void BalancePill::refreshLayout(int newWidth) {
	const auto &font = st::infoProfileBalancePillLabelFont;
	const auto labelSkip = st::infoProfileBalancePillLabelSkip;
	const auto available = newWidth - FixedWidth();
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
	const auto amountAvailable = std::max(available - _labelSpace, 1);
	_amount.setAvailableWidth(amountAvailable);
	_fittedWidth = std::min(
		newWidth,
		(FixedWidth()
			+ _labelSpace
			+ std::min(_amount.naturalWidth(), amountAvailable)));
}

void BalancePill::paintEvent(QPaintEvent *e) {
	if (_opacity <= 0.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_opacity);
	PaintPillBackground(p, rect(), _colors);
	if (!_contentHidden) {
		paintContent(p);
	}
}

void BalancePill::paintContent(QPainter &p) const {
	const auto &fg = _colors.primary;
	const auto &padding = st::infoProfileBalancePillPadding;
	if (!_labelShown.isEmpty()) {
		p.setFont(st::infoProfileBalancePillLabelFont);
		p.setPen(_colors.secondary);
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
