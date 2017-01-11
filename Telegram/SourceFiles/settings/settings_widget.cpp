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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_widget.h"

#include "settings/settings_inner_widget.h"
#include "settings/settings_fixed_bar.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "ui/effects/widget_fade_wrap.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "localstorage.h"
#include "boxes/confirmbox.h"
#include "application.h"

namespace Settings {
namespace {

QString SecretText;
QMap<QString, base::lambda_copy<void()>> Codes;

void fillCodes() {
	Codes.insert(qsl("debugmode"), []() {
		QString text = cDebug() ? qsl("Do you want to disable DEBUG logs?") : qsl("Do you want to enable DEBUG logs?\n\nAll network events will be logged.");
		Ui::show(Box<ConfirmBox>(text, [] {
			App::app()->onSwitchDebugMode();
		}));
	});
	Codes.insert(qsl("testmode"), []() {
		auto text = cTestMode() ? qsl("Do you want to disable TEST mode?") : qsl("Do you want to enable TEST mode?\n\nYou will be switched to test cloud.");
		Ui::show(Box<ConfirmBox>(text, [] {
			App::app()->onSwitchTestMode();
		}));
	});
	Codes.insert(qsl("loadlang"), []() {
		Global::RefChooseCustomLang().notify();
	});
	Codes.insert(qsl("debugfiles"), []() {
		if (!cDebug()) return;
		if (DebugLogging::FileLoader()) {
			Global::RefDebugLoggingFlags() &= ~DebugLogging::FileLoaderFlag;
		} else {
			Global::RefDebugLoggingFlags() |= DebugLogging::FileLoaderFlag;
		}
		Ui::show(Box<InformBox>(DebugLogging::FileLoader() ? qsl("Enabled file download logging") : qsl("Disabled file download logging")));
	});
	Codes.insert(qsl("crashplease"), []() {
		t_assert(!"Crashed in Settings!");
	});
	Codes.insert(qsl("workmode"), []() {
		auto text = Global::DialogsModeEnabled() ? qsl("Disable work mode?") : qsl("Enable work mode?");
		Ui::show(Box<ConfirmBox>(text, [] {
			App::app()->onSwitchWorkMode();
		}));
	});
	Codes.insert(qsl("moderate"), []() {
		auto text = Global::ModerateModeEnabled() ? qsl("Disable moderate mode?") : qsl("Enable moderate mode?");
		Ui::show(Box<ConfirmBox>(text, []() {
			Global::SetModerateModeEnabled(!Global::ModerateModeEnabled());
			Local::writeUserSettings();
			Ui::hideLayer();
		}));
	});
	Codes.insert(qsl("getdifference"), []() {
		if (auto main = App::main()) {
			main->getDifference();
		}
	});
}

void codesFeedString(const QString &text) {
	if (Codes.isEmpty()) fillCodes();

	SecretText += text.toLower();
	int size = SecretText.size(), from = 0;
	while (size > from) {
		auto piece = SecretText.midRef(from);
		auto found = false;
		for (auto i = Codes.cbegin(), e = Codes.cend(); i != e; ++i) {
			if (piece == i.key()) {
				(*i)();
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		for (auto i = Codes.cbegin(), e = Codes.cend(); i != e; ++i) {
			if (i.key().startsWith(piece)) {
				found = true;
				break;
			}
		}
		if (found) break;

		++from;
	}
	SecretText = (size > from) ? SecretText.mid(from) : QString();
}

} // namespace

Widget::Widget(QWidget *parent) : LayerWidget(parent)
, _scroll(this, st::settingsScroll)
, _fixedBar(this)
, _fixedBarClose(this, st::settingsFixedBarClose)
, _fixedBarShadow(this, object_ptr<BoxLayerTitleShadow>(this)) {
	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this));

	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBarClose->moveToRight(0, 0);
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, _fixedBar->y() + _fixedBar->height());
	_fixedBarShadow->hideFast();
	_scroll->moveToLeft(0, st::settingsFixedBarHeight);

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	_fixedBarClose->setClickedCallback([]() {
		Ui::hideSettingsAndLayer();
	});

	connect(_inner, SIGNAL(heightUpdated()), this, SLOT(onInnerHeightUpdated()));
}

void Widget::onScroll() {
	if (_scroll->scrollTop() > 0) {
		_fixedBarShadow->showAnimated();
	} else {
		_fixedBarShadow->hideAnimated();
	}
}

void Widget::parentResized() {
	auto parentSize = parentWidget()->size();
	int windowWidth = parentSize.width();
	int newWidth = st::settingsMaxWidth;
	int newContentLeft = st::settingsMaxPadding;
	if (windowWidth <= st::settingsMaxWidth) {
		newWidth = windowWidth;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	} else if (windowWidth < st::settingsMaxWidth + 2 * st::settingsMargin) {
		newWidth = windowWidth - 2 * st::settingsMargin;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	}

	// Widget height depends on InnerWidget height, so we
	// resize it here, not in the resizeEvent() handler.
	_inner->resizeToWidth(newWidth, newContentLeft);

	resizeUsingInnerHeight(newWidth, newContentLeft);
}

void Widget::onInnerHeightUpdated() {
	resizeUsingInnerHeight(width(), _contentLeft);
}

void Widget::resizeUsingInnerHeight(int newWidth, int newContentLeft) {
	if (!App::wnd()) return;

	auto parentSize = parentWidget()->size();
	int windowWidth = parentSize.width();
	int windowHeight = parentSize.height();
	int maxHeight = st::settingsFixedBarHeight + _inner->height();
	int newHeight = maxHeight + st::boxRadius;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}

	if (_contentLeft != newContentLeft) {
		_contentLeft = newContentLeft;
	}

	_roundedCorners = (newHeight < windowHeight);
	setAttribute(Qt::WA_OpaquePaintEvent, !_roundedCorners);

	setGeometry((windowWidth - newWidth) / 2, (windowHeight - newHeight) / 2, newWidth, newHeight);
	update();
}

void Widget::showFinished() {
	_inner->showFinished();
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	if (_roundedCorners) {
		auto paintTopRounded = clip.intersects(QRect(0, 0, width(), st::boxRadius));
		auto paintBottomRounded = clip.intersects(QRect(0, height() - st::boxRadius, width(), st::boxRadius));
		if (paintTopRounded || paintBottomRounded) {
			auto parts = qFlags(App::RectPart::None);
			if (paintTopRounded) parts |= App::RectPart::TopFull;
			if (paintBottomRounded) parts |= App::RectPart::BottomFull;
			App::roundRect(p, rect(), st::boxBg, BoxCorners, nullptr, parts);
		}
		auto other = clip.intersected(QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
		if (!other.isEmpty()) {
			p.fillRect(other, st::boxBg);
		}
	} else {
		p.fillRect(e->rect(), st::boxBg);
	}
}

void Widget::resizeEvent(QResizeEvent *e) {
	LayerWidget::resizeEvent(e);
	if (!width() || !height()) {
		return;
	}

	_fixedBar->resizeToWidth(width());
	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBarClose->moveToRight(0, 0);
	auto shadowTop = _fixedBar->y() + _fixedBar->height();
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, shadowTop);

	auto scrollSize = QSize(width(), height() - shadowTop - (_roundedCorners ? st::boxRadius : 0));
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}
	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

void Widget::keyPressEvent(QKeyEvent *e) {
	codesFeedString(e->text());
	return LayerWidget::keyPressEvent(e);
}

} // namespace Settings
