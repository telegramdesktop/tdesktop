/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_window_title.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "core/application.h"
#include "styles/style_window.h"
#include "styles/style_media_view.h"
#include "window/window_controller.h"

#include <Cocoa/Cocoa.h>

namespace Platform {

// All the window decorations preview is done without taking cScale() into
// account, with 100% scale and without "px" dimensions, because thats
// how it will look in real launched macOS app.
int PreviewTitleHeight() {
	if (const auto window = Core::App().activePrimaryWindow()) {
		if (const auto height = window->widget()->getCustomTitleHeight()) {
			return height;
		}
	}
	return 22;
}

QImage PreviewWindowSystemButton(QColor inner, QColor border) {
	auto buttonSize = 12;
	auto fullSize = buttonSize * style::DevicePixelRatio();
	auto result = QImage(fullSize, fullSize, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(border);
		p.setBrush(inner);
		p.drawEllipse(QRectF(0.5, 0.5, fullSize - 1., fullSize - 1.));
	}
	result.setDevicePixelRatio(style::DevicePixelRatio());
	return result;
}

void PreviewWindowTitle(Painter &p, const style::palette &palette, QRect body, int titleHeight, int outerWidth) {
	auto titleRect = QRect(body.x(), body.y() - titleHeight, body.width(), titleHeight);
	p.fillRect(titleRect, QColor(0, 0, 0));
	p.fillRect(titleRect, st::titleBgActive[palette]);
	p.fillRect(titleRect.x(), titleRect.y() + titleRect.height() - st::lineWidth, titleRect.width(), st::lineWidth, st::titleShadow[palette]);

	QFont font;
	const auto families = QStringList{
		u".AppleSystemUIFont"_q,
		u".SF NS Text"_q,
		u"Helvetica Neue"_q,
	};
	for (auto family : families) {
		font.setFamily(family);
		if (QFontInfo(font).family() == font.family()) {
			break;
		}
	}

	if (QFontInfo(font).family() != font.family()) {
		font = st::semiboldFont;
		font.setPixelSize(13);
	} else if (font.family() == u".AppleSystemUIFont"_q) {
		font.setBold(true);
		font.setPixelSize(13);
	} else {
		font.setPixelSize((titleHeight * 15) / 24);
	}

	p.setPen(st::titleFgActive[palette]);
	p.setFont(font);

	p.drawText(titleRect, u"Telegram"_q, style::al_center);

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
	p.drawImage(
		titleRect.x() + left,
		titleRect.y()
			+ (titleRect.height()
				- (close.height() / style::DevicePixelRatio())) / 2,
		close);
	left += (close.width() / style::DevicePixelRatio()) + buttonSkip;
	auto minimize = PreviewWindowSystemButton(minimizeInner, minimizeBorder);
	p.drawImage(
		titleRect.x() + left,
		titleRect.y()
			+ (titleRect.height()
				- (minimize.height() / style::DevicePixelRatio())) / 2,
		minimize);
	left += (minimize.width() / style::DevicePixelRatio()) + buttonSkip;
	auto maximize = PreviewWindowSystemButton(maximizeInner, maximizeBorder);
	p.drawImage(
		titleRect.x() + left,
		titleRect.y()
			+ (titleRect.height()
				- (maximize.height() / style::DevicePixelRatio())) / 2,
		maximize);
}

void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	auto retina = style::DevicePixelRatio();
	auto titleHeight = PreviewTitleHeight();
	{
		Painter p(&preview);
		PreviewWindowTitle(p, palette, body, titleHeight, outerWidth);
	}
	auto inner = QRect(body.x(), body.y() - titleHeight, body.width(), body.height() + titleHeight);

	auto retinaRadius = st::macWindowRoundRadius * retina;
	auto roundMask = QImage(2 * retinaRadius, 2 * retinaRadius, QImage::Format_ARGB32_Premultiplied);
	roundMask.setDevicePixelRatio(style::DevicePixelRatio());
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
	auto rounded = Images::Round(
		preview.copy(
			inner.x() * retina,
			inner.y() * retina,
			inner.width() * retina,
			inner.height() * retina),
			corners);
	rounded.setDevicePixelRatio(style::DevicePixelRatio());
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
