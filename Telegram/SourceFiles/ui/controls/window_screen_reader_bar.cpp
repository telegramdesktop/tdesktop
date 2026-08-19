/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/window_screen_reader_bar.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/screen_reader_mode.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_window.h"

namespace Ui {
namespace {

class DisableButton final : public RippleButton {
public:
	explicit DisableButton(QWidget *parent);

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

private:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	QString _text;
	int _textWidth = 0;

};

class InaccessibleLabel final : public FlatLabel {
public:
	using FlatLabel::FlatLabel;

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

};

class Bar final : public RpWidget {
public:
	explicit Bar(not_null<QWidget*> parent);

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

	int resizeGetHeight(int newWidth) override;

	[[nodiscard]] rpl::producer<> disableClicks() const;

private:
	void paintEvent(QPaintEvent *e) override;
	int accessibilityChildCount() const override;

	object_ptr<InaccessibleLabel> _label;
	object_ptr<DisableButton> _disable;

};

DisableButton::DisableButton(QWidget *parent)
: RippleButton(parent, st::windowScreenReaderButtonRipple) {
	_text = tr::lng_screen_reader_bar_disable(tr::now);
	_textWidth = st::windowScreenReaderDisableTextStyle.font->width(_text);
	const auto padding = st::windowScreenReaderButtonPadding;
	setFixedSize(
		padding.left() + _textWidth + padding.right(),
		st::windowScreenReaderButtonHeight);
}

void DisableButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(e->rect(), st::activeButtonBg);
	paintRipple(p, 0, 0);

	auto hq = PainterHighQualityEnabler(p);
	const auto border = st::windowScreenReaderButtonBorderWidth;
	const auto half = border / 2.;
	const auto radius = st::windowScreenReaderButtonRadius;

	p.setPen(QPen(st::activeButtonFg, border));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(rect()).marginsRemoved(QMarginsF(half, half, half, half)),
		radius,
		radius);

	const auto &font = st::windowScreenReaderDisableTextStyle.font;
	p.setFont(font);
	p.setPen(st::activeButtonFg);
	p.drawText(
		(width() - _textWidth) / 2,
		(height() - font->height) / 2 + font->ascent,
		_text);
}

QImage DisableButton::prepareRippleMask() const {
	return RippleAnimation::RoundRectMask(
		size(),
		st::windowScreenReaderButtonRadius);
}

QPoint DisableButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QAccessible::Role DisableButton::accessibilityRole() {
	return QAccessible::NoRole;
}

QString DisableButton::accessibilityName() {
	return QString();
}

QAccessible::Role InaccessibleLabel::accessibilityRole() {
	return QAccessible::Role::NoRole;
}

QString InaccessibleLabel::accessibilityName() {
	return QString();
}

Bar::Bar(not_null<QWidget*> parent)
: RpWidget(parent)
, _label(
	this,
	tr::lng_screen_reader_bar_text(),
	st::windowScreenReaderLabel)
, _disable(object_ptr<DisableButton>(this)) {
}

int Bar::resizeGetHeight(int newWidth) {
	const auto padding = st::windowScreenReaderPadding;
	const auto available = newWidth
		- padding.left()
		- padding.right()
		- _disable->width()
		- padding.left();

	_label->resizeToWidth(available);

	const auto contentHeight = std::max(
		_label->height(),
		_disable->height());
	const auto height = contentHeight + padding.top() + padding.bottom();

	_label->moveToLeft(
		padding.left(),
		padding.top() + (contentHeight - _label->height()) / 2,
		newWidth);

	_disable->moveToRight(
		padding.right(),
		padding.top() + (contentHeight - _disable->height()) / 2,
		newWidth);

	return height;
}

void Bar::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), st::activeButtonBg);
}

rpl::producer<> Bar::disableClicks() const {
	return _disable->clicks() | rpl::to_empty;
}

QAccessible::Role Bar::accessibilityRole() {
	return QAccessible::NoRole;
}

QString Bar::accessibilityName() {
	return QString();
}

int Bar::accessibilityChildCount() const {
	return 0;
}

} // namespace

object_ptr<RpWidget> CreateScreenReaderBar(
		not_null<QWidget*> parent,
		Fn<void()> disableCallback) {
	auto result = object_ptr<SlideWrap<Bar>>(
		parent.get(),
		object_ptr<Bar>(parent.get()));
	const auto wrap = result.data();

	wrap->entity()->disableClicks(
	) | rpl::on_next([=] {
		if (disableCallback) {
			disableCallback();
		}
	}, wrap->lifetime());

	Ui::ScreenReaderModeActiveValue(
	) | rpl::on_next([=](bool active) {
		wrap->toggle(active, anim::type::normal);
	}, wrap->lifetime());

	wrap->toggle(Ui::ScreenReaderModeActive(), anim::type::instant);

	wrap->entity()->resizeToWidth(st::windowMinWidth);

	return result;
}

} // namespace Ui
