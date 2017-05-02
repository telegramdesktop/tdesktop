/*
 This file is part of Telegram Desktop,
 the official desktop version of Telegram messaging app, see https://telegram.org

 Telegram Desktop is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 It is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
 Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
 */
#include "ui/widgets/tooltip.h"

#include "mainwindow.h"
#include "styles/style_widgets.h"
#include "platform/platform_specific.h"

namespace Ui {

Tooltip *TooltipInstance = nullptr;

bool AbstractTooltipShower::tooltipWindowActive() const {
	if (auto window = App::wnd()) {
		window->updateIsActive(0);
		return window->isActive();
	}
	return false;
}

const style::Tooltip *AbstractTooltipShower::tooltipSt() const {
	return &st::defaultTooltip;
}

AbstractTooltipShower::~AbstractTooltipShower() {
	if (TooltipInstance && TooltipInstance->_shower == this) {
		TooltipInstance->_shower = 0;
	}
}

Tooltip::Tooltip() : TWidget(nullptr) {
	TooltipInstance = this;

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::ToolTip);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	_showTimer.setCallback([this] { performShow(); });
	_hideByLeaveTimer.setCallback([this] { Hide(); });

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
}

void Tooltip::performShow() {
	if (_shower) {
		auto text = _shower->tooltipWindowActive() ? _shower->tooltipText() : QString();
		if (text.isEmpty()) {
			Hide();
		} else {
			TooltipInstance->popup(_shower->tooltipPos(), text, _shower->tooltipSt());
		}
	}
}

void Tooltip::onWndActiveChanged() {
	if (!App::wnd() || !App::wnd()->windowHandle() || !App::wnd()->windowHandle()->isActive()) {
		Tooltip::Hide();
	}
}

bool Tooltip::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Leave) {
		_hideByLeaveTimer.callOnce(10);
	} else if (e->type() == QEvent::Enter) {
		_hideByLeaveTimer.cancel();
	} else if (e->type() == QEvent::MouseMove) {
		if ((QCursor::pos() - _point).manhattanLength() > QApplication::startDragDistance()) {
			Hide();
		}
	}
	return TWidget::eventFilter(o, e);
}

Tooltip::~Tooltip() {
	if (TooltipInstance == this) {
		TooltipInstance = 0;
	}
}

void Tooltip::popup(const QPoint &m, const QString &text, const style::Tooltip *st) {
	if (!_isEventFilter) {
		QCoreApplication::instance()->installEventFilter(this);
	}

	_point = m;
	_st = st;
	_text = Text(_st->textStyle, text, _textPlainOptions, _st->widthMax, true);

	_useTransparency = Platform::TranslucentWindowsSupported(_point);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);

	int32 addw = 2 * st::lineWidth + _st->textPadding.left() + _st->textPadding.right();
	int32 addh = 2 * st::lineWidth + _st->textPadding.top() + _st->textPadding.bottom();

	// count tooltip size
	QSize s(addw + _text.maxWidth(), addh + _text.minHeight());
	if (s.width() > _st->widthMax) {
		s.setWidth(addw + _text.countWidth(_st->widthMax - addw));
		s.setHeight(addh + _text.countHeight(s.width() - addw));
	}
	int32 maxh = addh + (_st->linesMax * _st->textStyle.font->height);
	if (s.height() > maxh) {
		s.setHeight(maxh);
	}

	// count tooltip position
	QPoint p(m + _st->shift);
	if (rtl()) {
		p.setX(m.x() - s.width() - _st->shift.x());
	}
	if (s.width() < 2 * _st->shift.x()) {
		p.setX(m.x() - (s.width() / 2));
	}

	// adjust tooltip position
	QRect r(QApplication::desktop()->screenGeometry(m));
	if (r.x() + r.width() - _st->skip < p.x() + s.width() && p.x() + s.width() > m.x()) {
		p.setX(qMax(r.x() + r.width() - int32(_st->skip) - s.width(), m.x() - s.width()));
	}
	if (r.x() + _st->skip > p.x() && p.x() < m.x()) {
		p.setX(qMin(m.x(), r.x() + int32(_st->skip)));
	}
	if (r.y() + r.height() - _st->skip < p.y() + s.height()) {
		p.setY(m.y() - s.height() - _st->skip);
	}
	if (r.y() > p.x()) {
		p.setY(qMin(m.y() + _st->shift.y(), r.y() + r.height() - s.height()));
	}

	setGeometry(QRect(p, s));

	_hideByLeaveTimer.cancel();
	show();
}

void Tooltip::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_useTransparency) {
		Platform::StartTranslucentPaint(p, e);
	}

	if (_useTransparency) {
		p.setPen(_st->textBorder);
		p.setBrush(_st->textBg);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1., height() - 1.), st::buttonRadius, st::buttonRadius);
	} else {
		p.fillRect(rect(), _st->textBg);

		p.fillRect(QRect(0, 0, width(), st::lineWidth), _st->textBorder);
		p.fillRect(QRect(0, height() - st::lineWidth, width(), st::lineWidth), _st->textBorder);
		p.fillRect(QRect(0, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);
		p.fillRect(QRect(width() - st::lineWidth, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);
	}
	int32 lines = qFloor((height() - 2 * st::lineWidth - _st->textPadding.top() - _st->textPadding.bottom()) / _st->textStyle.font->height);

	p.setPen(_st->textFg);
	_text.drawElided(p, st::lineWidth + _st->textPadding.left(), st::lineWidth + _st->textPadding.top(), width() - 2 * st::lineWidth - _st->textPadding.left() - _st->textPadding.right(), lines);
}

void Tooltip::hideEvent(QHideEvent *e) {
	if (TooltipInstance == this) {
		Hide();
	}
}

void Tooltip::Show(int32 delay, const AbstractTooltipShower *shower) {
	if (!TooltipInstance) {
		new Tooltip();
	}
	TooltipInstance->_shower = shower;
	if (delay >= 0) {
		TooltipInstance->_showTimer.callOnce(delay);
	} else {
		TooltipInstance->performShow();
	}
}

void Tooltip::Hide() {
	if (auto instance = TooltipInstance) {
		TooltipInstance = nullptr;
		instance->_showTimer.cancel();
		instance->_hideByLeaveTimer.cancel();
		instance->hide();
		instance->deleteLater();
	}
}

} // namespace Ui
