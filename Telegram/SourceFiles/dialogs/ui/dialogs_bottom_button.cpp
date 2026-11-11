/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_bottom_button.h"

#include "ui/color_contrast.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_dialogs.h"
#include "styles/style_dialogs_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

[[nodiscard]] QImage UpdateIcon() {
	const auto iconSize = st::dialogsInstallUpdateIconSize;
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		Size(iconSize) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();

		const auto fullRect = QRectF(0, 0, iconSize, iconSize);
		const auto rect = fullRect
			- Margins(st::dialogsInstallUpdateIconInnerMargin);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawEllipse(fullRect);

		p.setCompositionMode(QPainter::CompositionMode_Clear);

		auto pen = QPen(Qt::black);
		pen.setWidthF(style::ConvertFloatScale(2.));
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);

		using namespace arc;
		constexpr auto kShift = int(20 * 16);
		p.drawArc(rect, -kShift, kQuarterLength + kShift);
		p.drawArc(rect, kHalfLength - kShift, kQuarterLength + kShift);

		const auto side1 = st::dialogsInstallUpdateIconSide1;
		const auto side2 = st::dialogsInstallUpdateIconSide2;
		const auto top = rect.y() - side1;
		const auto bottom = rect::bottom(rect) - side1;
		const auto centerX = rect::center(rect).x();
		path.moveTo(centerX, bottom + side1 + side2);
		path.lineTo(centerX, bottom + side1 - side2);
		path.lineTo(centerX + side2, bottom + side1);
		path.closeSubpath();

		path.moveTo(centerX, top + side1 + side2);
		path.lineTo(centerX, top + side1 - side2);
		path.lineTo(centerX - side2, top + side1);
		path.closeSubpath();

		p.fillPath(path, Qt::black);
	}
	return result;
}

} // namespace

BottomButton::BottomButton(
	QWidget *parent,
	const QString &text,
	const style::FlatButton &st,
	const style::icon &icon,
	const style::icon &iconOver,
	bool hasTextIcon)
: RippleButton(parent, st.ripple)
, _text(text)
, _st(st)
, _icon(icon)
, _iconOver(iconOver)
, _hasTextIcon(hasTextIcon) {
	resize(st::columnMinimalWidthLeft, _st.height);

	if (_hasTextIcon) {
		rpl::single(rpl::empty_value()) | rpl::then(
			style::PaletteChanged()
		) | rpl::on_next([this] {
			_quiet = !Ui::IsLightBackground(st::dialogsBg->c);
			_textIcon = _quiet
				? style::colorizeImage(UpdateIcon(), st::groupCallLive1)
				: UpdateIcon();
		}, lifetime());
	}
}

void BottomButton::setText(const QString &text) {
	_text = text;
	update();
}

void BottomButton::radialAnimationCallback() {
	if (!anim::Disabled() && width() < st::columnMinimalWidthLeft) {
		update();
	}
}

void BottomButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if ((was & StateFlag::Disabled) != (state() & StateFlag::Disabled)) {
		_loading = isDisabled()
			? std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::dialogsLoadMoreLoading)
			: nullptr;
		if (_loading) {
			_loading->start();
		}
	}
	update();
}

void BottomButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto over = isOver() && !isDisabled();

	auto r = QRect(0, height() - _st.height, width(), _st.height);

	if (_hasTextIcon && _quiet) {
		p.fillRect(r, over ? st::dialogsRippleBg : st::dialogsBgOver);
		if (!isDisabled()) {
			paintRipple(p, 0, 0, &st::universalRippleAnimation.color->c);
		}
	} else if (_hasTextIcon) {
		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});
		p.fillRect(r, QBrush(std::move(gradient)));
		if (over) {
			p.fillRect(
				r,
				anim::with_alpha(st::universalRippleAnimation.color->c, .3));
		}

		if (!isDisabled()) {
			paintRipple(p, 0, 0, &st::universalRippleAnimation.color->c);
		}
	} else {
		p.fillRect(r, over ? _st.overBgColor : _st.bgColor);
		if (!isDisabled()) {
			paintRipple(p, 0, 0);
		}
	}

	const auto &font = over ? _st.overFont : _st.font;
	p.setFont(font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(over ? _st.overColor : _st.color);

	if (width() >= st::columnMinimalWidthLeft) {
		r.setTop(_st.textTop);
		if (_hasTextIcon) {
			const auto &icon = _textIcon;
			const auto iconSize = icon.size() / style::DevicePixelRatio();
			const auto skip = st::dialogsInstallUpdateIconSkip;
			const auto textWidth = font->width(_text);
			const auto rect = QRect(
				(width() - (iconSize.width() + textWidth + skip)) / 2,
				r.y(),
				textWidth,
				r.height());
			if (_quiet) {
				const auto left = rect.x() + iconSize.width() + skip;
				auto gradient = QLinearGradient(left, 0, left + textWidth, 0);
				gradient.setStops({
					{ 0., st::groupCallLive1->c },
					{ 1., st::groupCallLive2->c },
				});
				p.setPen(QPen(QBrush(std::move(gradient)), 1.));
			}
			p.drawText(
				rect.translated(iconSize.width() + skip, 0),
				_text,
				style::al_top);
			p.drawImage(rect.x(), (height() - iconSize.height()) / 2, icon);
		} else {
			p.drawText(r, _text, style::al_top);
		}
	} else if (isDisabled() && _loading) {
		_loading->draw(
			p,
			QPoint(
				(width() - st::dialogsLoadMoreLoading.size.width()) / 2,
				(height() - st::dialogsLoadMoreLoading.size.height()) / 2),
			width());
	} else {
		if (_hasTextIcon) {
			const auto size = _textIcon.size() / style::DevicePixelRatio();
			p.drawImage(
				(width() - size.width()) / 2,
				(height() - size.height()) / 2,
				_textIcon);
		} else {
			(over ? _iconOver : _icon).paintInCenter(p, r);
		}
	}
}

} // namespace Dialogs
