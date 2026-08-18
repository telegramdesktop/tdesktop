/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/restore_windows_offer.h"

#include "dialogs/ui/dialogs_pill.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Dialogs {
namespace {

[[nodiscard]] int ButtonTextMargin() {
	const auto width = st::dialogsUnconfirmedAuthButton.width;
	return (width < 0) ? (-width / 2) : 0;
}

[[nodiscard]] int CloseRippleOverhang() {
	const auto &st = st::restoreWindowsOfferClose;
	return st.width - st.rippleAreaPosition.x() - st.rippleAreaSize;
}

} // namespace

RestoreWindowsOffer::RestoreWindowsOffer(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _always(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_always(),
	st::dialogsUnconfirmedAuthButton))
, _restore(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_restore(),
	st::dialogsUnconfirmedAuthButton))
, _never(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_never(),
	st::dialogsUnconfirmedAuthButtonNo))
, _wideAlways(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_always(),
	st::defaultLightButton))
, _wideRestore(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_restore(),
	st::defaultLightButton))
, _wideNever(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_restore_windows_never(),
	st::attentionBoxButton))
, _close(Ui::CreateChild<Ui::IconButton>(
	this,
	st::restoreWindowsOfferClose))
, _shadow(st::dialogsTopBarSuggestionShadow) {
	_question.setText(
		st::restoreWindowsOfferQuestionStyle,
		tr::lng_restore_windows_question(tr::now));
	const auto choose = [=](RestoreWindowsChoice choice) {
		return [=] { _chosen.fire_copy(choice); };
	};
	_always->setClickedCallback(choose(RestoreWindowsChoice::Always));
	_restore->setClickedCallback(choose(RestoreWindowsChoice::Once));
	_never->setClickedCallback(choose(RestoreWindowsChoice::Never));
	_wideAlways->setClickedCallback(choose(RestoreWindowsChoice::Always));
	_wideRestore->setClickedCallback(choose(RestoreWindowsChoice::Once));
	_wideNever->setClickedCallback(choose(RestoreWindowsChoice::Never));
	_close->setClickedCallback(choose(RestoreWindowsChoice::Dismiss));
}

void RestoreWindowsOffer::setAvailableWidth(int available) {
	if (_maxWidth != available) {
		_maxWidth = available;
		relayout();
	}
}

rpl::producer<RestoreWindowsChoice> RestoreWindowsOffer::chosen() const {
	return _chosen.events();
}

Ui::Text::GeometryDescriptor RestoreWindowsOffer::questionGeometry() const {
	const auto first = _questionFirstLine;
	const auto other = _questionOther;
	return { .layout = [=](int line) {
		return Ui::Text::LineGeometry{ .width = line ? other : first };
	} };
}

void RestoreWindowsOffer::relayout() {
	const auto &margins = st::dialogsTopBarSuggestionMargins;
	const auto &padding = st::restoreWindowsOfferPadding;
	const auto textMargin = ButtonTextMargin();
	const auto buttonSkip = st::restoreWindowsOfferButtonSkip;
	const auto frame = margins.left()
		+ padding.left()
		+ padding.right()
		+ margins.right();
	const auto lineHeight
		= st::restoreWindowsOfferQuestionStyle.font->height;
	const auto closeReserve = _close->width()
		- textMargin
		- CloseRippleOverhang()
		+ buttonSkip;
	const auto buttonsInner = _always->width()
		+ buttonSkip
		+ _restore->width()
		+ buttonSkip
		+ _never->width()
		- 2 * textMargin;
	const auto questionInner = _question.maxWidth() + closeReserve;
	const auto wide = std::max(buttonsInner, questionInner);
	const auto horizontal = (frame + wide <= _maxWidth);
	const auto inner = horizontal
		? wide
		: std::max(_maxWidth - frame, closeReserve + lineHeight);
	_questionFirstLine = inner - closeReserve;
	_questionOther = inner;
	const auto question = std::max(
		_question.countDimensions(questionGeometry()).height,
		lineHeight);
	const auto block = std::max(question, _close->height());
	const auto questionTop = margins.top()
		+ padding.top()
		+ std::max((block - question) / 2, 0);
	const auto buttonsTop = margins.top()
		+ padding.top()
		+ block
		+ st::restoreWindowsOfferSkip;
	const auto buttonHeight = _always->height();
	const auto wideHeight = _wideAlways->height()
		+ buttonSkip
		+ _wideRestore->height()
		+ buttonSkip
		+ _wideNever->height();
	const auto bottom = padding.bottom() + margins.bottom();
	const auto totalWidth = frame + inner;
	for (const auto &button : { _always, _restore, _never }) {
		button->setVisible(horizontal);
	}
	for (const auto &button : { _wideAlways, _wideRestore, _wideNever }) {
		button->setVisible(!horizontal);
	}
	resize(
		totalWidth,
		horizontal
			? (buttonsTop + buttonHeight + bottom)
			: (buttonsTop + wideHeight + bottom));
	_questionPosition = QPoint(
		margins.left() + padding.left(),
		questionTop);
	_close->moveToRight(
		margins.right()
			+ padding.right()
			- textMargin
			- CloseRippleOverhang(),
		questionTop + (lineHeight - _close->height()) / 2);
	const auto left = margins.left() + padding.left() - textMargin;
	if (horizontal) {
		_always->moveToLeft(left, buttonsTop);
		_never->moveToRight(
			margins.right() + padding.right() - textMargin,
			buttonsTop);
		const auto alwaysEnd = left + _always->width();
		const auto neverStart = totalWidth
			- (margins.right() + padding.right() - textMargin)
			- _never->width();
		_restore->moveToLeft(
			(alwaysEnd + neverStart - _restore->width()) / 2,
			buttonsTop);
	} else {
		const auto wideWidth = inner;
		auto top = buttonsTop;
		for (const auto &button : { _wideAlways, _wideRestore, _wideNever }) {
			button->resizeToWidth(wideWidth);
			button->moveToLeft(margins.left() + padding.left(), top);
			top += button->height() + buttonSkip;
		}
	}
}

void RestoreWindowsOffer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto pill = rect() - st::dialogsTopBarSuggestionMargins;
	const auto radius = std::min({
		PillRadius(),
		pill.width() / 2,
		pill.height() / 2,
	});
	_shadow.paint(p, pill, radius);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::dialogsBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(pill, radius, radius);
	}
	PaintPillOutline(p, pill, radius);
	p.setPen(st::windowFg);
	_question.draw(p, {
		.position = _questionPosition,
		.outerWidth = width(),
		.availableWidth = _questionOther,
		.geometry = questionGeometry(),
	});
}

} // namespace Dialogs
