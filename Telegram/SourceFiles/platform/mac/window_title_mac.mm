/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/window_title_mac.h"

#include "mainwindow.h"
#include "ui/widgets/shadow.h"
#include "ui/image/image_prepare.h"
#include "core/application.h"
#include "styles/style_window.h"
#include "styles/style_media_view.h"
#include "platform/platform_main_window.h"
#include "window/window_controller.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>

namespace Platform {

TitleWidget::TitleWidget(MainWindow *parent, int height)
: Window::TitleWidget(parent)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), height);

#ifndef OS_MAC_OLD
	QStringList families = { qsl(".SF NS Text"), qsl("Helvetica Neue") };
	for (auto family : families) {
		_font.setFamily(family);
		if (QFontInfo(_font).family() == _font.family()) {
			break;
		}
	}
#endif // OS_MAC_OLD

	if (QFontInfo(_font).family() == _font.family()) {
		_font.setPixelSize((height * 15) / 24);
	} else {
		_font = st::normalFont;
	}

	Core::App().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto active = isActiveWindow();
	p.fillRect(rect(), active ? st::titleBgActive : st::titleBg);

	p.setFont(_font);
	p.setPen(active ? st::titleFgActive : st::titleFg);
	p.drawText(rect(), static_cast<MainWindow*>(parentWidget())->titleText(), style::al_center);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	auto window = parentWidget();
	if (window->windowState() == Qt::WindowMaximized) {
		window->setWindowState(Qt::WindowNoState);
	} else {
		window->setWindowState(Qt::WindowMaximized);
	}
}

object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	if (auto window = qobject_cast<Platform::MainWindow*>(parent)) {
		if (auto height = window->getCustomTitleHeight()) {
			return object_ptr<TitleWidget>(window, height);
		}
	}
	return { nullptr };
}

// All the window decorations preview is done without taking cScale() into
// account, with 100% scale and without "px" dimensions, because thats
// how it will look in real launched macOS app.
int PreviewTitleHeight() {
	if (auto window = Core::App().activeWindow()) {
		if (auto height = window->widget()->getCustomTitleHeight()) {
			return height;
		}
	}
	return 22;
}

QImage PreviewWindowSystemButton(QColor inner, QColor border) {
	auto buttonSize = 12;
	auto fullSize = buttonSize * cIntRetinaFactor();
	auto result = QImage(fullSize, fullSize, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(border);
		p.setBrush(inner);
		p.drawEllipse(QRectF(0.5, 0.5, fullSize - 1., fullSize - 1.));
	}
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

void PreviewWindowTitle(Painter &p, const style::palette &palette, QRect body, int titleHeight, int outerWidth) {
	auto titleRect = QRect(body.x(), body.y() - titleHeight, body.width(), titleHeight);
	p.fillRect(titleRect, QColor(0, 0, 0));
	p.fillRect(titleRect, st::titleBgActive[palette]);
	p.fillRect(titleRect.x(), titleRect.y() + titleRect.height() - st::lineWidth, titleRect.width(), st::lineWidth, st::titleShadow[palette]);

	auto useSystemFont = false;
	QFont font;
#ifndef OS_MAC_OLD
	QStringList families = { qsl(".SF NS Text"), qsl("Helvetica Neue") };
	for (auto family : families) {
		font.setFamily(family);
		if (QFontInfo(font).family() == font.family()) {
			useSystemFont = true;
			break;
		}
	}
#endif // OS_MAC_OLD

	if (useSystemFont) {
		font.setPixelSize((titleHeight * 15) / 24);
	} else {
		font = st::normalFont;
	}

	p.setPen(st::titleFgActive[palette]);
	p.setFont(font);

	p.drawText(titleRect, qsl("Telegram"), style::al_center);

	auto isGraphite = ([NSColor currentControlTint] == NSGraphiteControlTint);
	auto buttonSkip = 8;
	auto graphiteInner = QColor(141, 141, 146);
	auto graphiteBorder = QColor(104, 104, 109);
	auto closeInner = isGraphite ? graphiteInner : QColor(252, 96, 92);
	auto closeBorder = isGraphite ? graphiteBorder : QColor(222, 64, 59);
	auto minimizeInner = isGraphite ? graphiteInner : QColor(254, 192, 65);
	auto minimizeBorder = isGraphite ? graphiteBorder : QColor(221, 152, 25);
	auto maximizeInner = isGraphite ? graphiteInner : QColor(52, 200, 74);
	auto maximizeBorder = isGraphite ? graphiteBorder : QColor(21, 164, 41);
	auto close = PreviewWindowSystemButton(closeInner, closeBorder);
	auto left = buttonSkip;
	p.drawImage(titleRect.x() + left, titleRect.y() + (titleRect.height() - (close.height() / cIntRetinaFactor())) / 2, close);
	left += (close.width() / cIntRetinaFactor()) + buttonSkip;
	auto minimize = PreviewWindowSystemButton(minimizeInner, minimizeBorder);
	p.drawImage(titleRect.x() + left, titleRect.y() + (titleRect.height() - (minimize.height() / cIntRetinaFactor())) / 2, minimize);
	left += (minimize.width() / cIntRetinaFactor()) + buttonSkip;
	auto maximize = PreviewWindowSystemButton(maximizeInner, maximizeBorder);
	p.drawImage(titleRect.x() + left, titleRect.y() + (titleRect.height() - (maximize.height() / cIntRetinaFactor())) / 2, maximize);
}

void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	auto retina = cIntRetinaFactor();
	auto titleHeight = PreviewTitleHeight();
	{
		Painter p(&preview);
		PreviewWindowTitle(p, palette, body, titleHeight, outerWidth);
	}
	auto inner = QRect(body.x(), body.y() - titleHeight, body.width(), body.height() + titleHeight);

	auto retinaRadius = st::macWindowRoundRadius * retina;
	auto roundMask = QImage(2 * retinaRadius, 2 * retinaRadius, QImage::Format_ARGB32_Premultiplied);
	roundMask.setDevicePixelRatio(cRetinaFactor());
	roundMask.fill(Qt::transparent);
	{
		Painter p(&roundMask);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255));
		p.drawRoundedRect(0, 0, 2 * st::macWindowRoundRadius, 2 * st::macWindowRoundRadius, st::macWindowRoundRadius, st::macWindowRoundRadius);
	}
	QImage corners[4];
	corners[0] = roundMask.copy(0, 0, retinaRadius, retinaRadius);
	corners[1] = roundMask.copy(retinaRadius, 0, retinaRadius, retinaRadius);
	corners[2] = roundMask.copy(0, retinaRadius, retinaRadius, retinaRadius);
	corners[3] = roundMask.copy(retinaRadius, retinaRadius, retinaRadius, retinaRadius);
	auto rounded = preview.copy(inner.x() * retina, inner.y() * retina, inner.width() * retina, inner.height() * retina);
	Images::prepareRound(rounded, corners);
	rounded.setDevicePixelRatio(cRetinaFactor());
	preview.fill(st::themePreviewBg->c);

	auto topLeft = st::macWindowShadowTopLeft.instance(QColor(0, 0, 0), 100);
	auto topRight = topLeft.mirrored(true, false);
	auto bottomLeft = topLeft.mirrored(false, true);
	auto bottomRight = bottomLeft.mirrored(true, false);
	auto extend = QMargins(37, 28, 37, 28);
	auto left = topLeft.copy(0, topLeft.height() - retina, extend.left() * retina, retina);
	auto top = topLeft.copy(topLeft.width() - retina, 0, retina, extend.top() * retina);
	auto right = topRight.copy(topRight.width() - (extend.right() * retina), topRight.height() - retina, extend.right() * retina, retina);
	auto bottom = bottomRight.copy(0, bottomRight.height() - (extend.bottom() * retina), retina, extend.bottom() * retina);
	{
		Painter p(&preview);
		p.drawImage(inner.x() - extend.left(), inner.y() - extend.top(), topLeft);
		p.drawImage(inner.x() + inner.width() + extend.right() - (topRight.width() / retina), inner.y() - extend.top(), topRight);
		p.drawImage(inner.x() - extend.left(), inner.y() + inner.height() + extend.bottom() - (bottomLeft.height() / retina), bottomLeft);
		p.drawImage(inner.x() + inner.width() + extend.right() - (bottomRight.width() / retina), inner.y() + inner.height() + extend.bottom() - (bottomRight.height() / retina), bottomRight);
		p.drawImage(QRect(inner.x() - extend.left(), inner.y() - extend.top() + (topLeft.height() / retina), extend.left(), extend.top() + inner.height() + extend.bottom() - (topLeft.height() / retina) - (bottomLeft.height() / retina)), left);
		p.drawImage(QRect(inner.x() - extend.left() + (topLeft.width() / retina), inner.y() - extend.top(), extend.left() + inner.width() + extend.right() - (topLeft.width() / retina) - (topRight.width() / retina), extend.top()), top);
		p.drawImage(QRect(inner.x() + inner.width(), inner.y() - extend.top() + (topRight.height() / retina), extend.right(), extend.top() + inner.height() + extend.bottom() - (topRight.height() / retina) - (bottomRight.height() / retina)), right);
		p.drawImage(QRect(inner.x() - extend.left() + (bottomLeft.width() / retina), inner.y() + inner.height(), extend.left() + inner.width() + extend.right() - (bottomLeft.width() / retina) - (bottomRight.width() / retina), extend.bottom()), bottom);
		p.drawImage(inner.x(), inner.y(), rounded);
	}
}

} // namespace Platform
