/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/cached_round_corners.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/image/image_prepare.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_overview.h"
#include "styles/style_media_view.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

constexpr auto kCachedCornerRadiusCount = int(CachedCornerRadius::kCount);

std::vector<CornersPixmaps> Corners;
QImage CornersMaskLarge[4], CornersMaskSmall[4];
rpl::lifetime PaletteChangedLifetime;

std::array<std::array<QImage, 4>, kCachedCornerRadiusCount> CachedMasks;

[[nodiscard]] std::array<QImage, 4> PrepareCorners(int32 radius, const QBrush &brush, const style::color *shadow = nullptr) {
	const auto r = radius * style::DevicePixelRatio();
	const auto s = st::msgShadow * style::DevicePixelRatio();
	QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied);
	rect.fill(Qt::transparent);
	{
		auto p = QPainter(&rect);
		PainterHighQualityEnabler hq(p);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		if (shadow) {
			p.setBrush((*shadow)->b);
			p.drawRoundedRect(0, s, r * 3, r * 3, r, r);
		}
		p.setBrush(brush);
		p.drawRoundedRect(0, 0, r * 3, r * 3, r, r);
	}
	auto result = std::array<QImage, 4>();
	result[0] = rect.copy(0, 0, r, r);
	result[1] = rect.copy(r * 2, 0, r, r);
	result[2] = rect.copy(0, r * 2, r, r + (shadow ? s : 0));
	result[3] = rect.copy(r * 2, r * 2, r, r + (shadow ? s : 0));
	return result;
}

void PrepareCorners(CachedRoundCorners index, int32 radius, const QBrush &brush, const style::color *shadow = nullptr) {
	Expects(index < Corners.size());

	auto images = PrepareCorners(radius, brush, shadow);
	for (int i = 0; i < 4; ++i) {
		Corners[index].p[i] = PixmapFromImage(std::move(images[i]));
		Corners[index].p[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CreateMaskCorners() {
	auto mask = PrepareCorners(st::roundRadiusSmall, QColor(255, 255, 255), nullptr);
	for (int i = 0; i < 4; ++i) {
		CornersMaskSmall[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
		CornersMaskSmall[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
	mask = PrepareCorners(st::roundRadiusLarge, QColor(255, 255, 255), nullptr);
	for (int i = 0; i < 4; ++i) {
		CornersMaskLarge[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
		CornersMaskLarge[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CreatePaletteCorners() {
	PrepareCorners(MenuCorners, st::innerDropdownRadius, st::menuBg);
	PrepareCorners(BoxCorners, st::boxRadius, st::boxBg);
	PrepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
	PrepareCorners(OverviewVideoCorners, st::overviewVideoStatusRadius, st::msgDateImgBg);
	PrepareCorners(OverviewVideoSelectedCorners, st::overviewVideoStatusRadius, st::msgDateImgBgSelected);
	PrepareCorners(ForwardCorners, st::roundRadiusLarge, st::historyForwardChooseBg);
	PrepareCorners(MediaviewSaveCorners, st::mediaviewControllerRadius, st::mediaviewSaveMsgBg);
	PrepareCorners(StickerHoverCorners, st::roundRadiusSmall, st::emojiPanHover);
	PrepareCorners(BotKeyboardCorners, st::roundRadiusSmall, st::botKbBg);

	PrepareCorners(Doc1Corners, st::roundRadiusSmall, st::msgFile1Bg);
	PrepareCorners(Doc2Corners, st::roundRadiusSmall, st::msgFile2Bg);
	PrepareCorners(Doc3Corners, st::roundRadiusSmall, st::msgFile3Bg);
	PrepareCorners(Doc4Corners, st::roundRadiusSmall, st::msgFile4Bg);
}

} // namespace

void StartCachedCorners() {
	Corners.resize(RoundCornersCount);
	CreateMaskCorners();
	CreatePaletteCorners();

	style::PaletteChanged(
	) | rpl::on_next([=] {
		CreatePaletteCorners();
	}, PaletteChangedLifetime);
}

void FinishCachedCorners() {
	Corners.clear();
	PaletteChangedLifetime.destroy();
}

void RefreshCachedCorners() {
	FinishCachedCorners();
	StartCachedCorners();
}

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corners) {
	using namespace Images;

	if (corners.p[kTopLeft].isNull()
		&& corners.p[kTopRight].isNull()
		&& corners.p[kBottomLeft].isNull()
		&& corners.p[kBottomRight].isNull()) {
		p.fillRect(x, y, w, h, bg);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto dx = qRound(x * ratio);
	const auto dy = qRound(y * ratio);
	const auto dw = qRound(w * ratio);
	const auto dh = qRound(h * ratio);

	const auto devSize = [&](int index) {
		return corners.p[index].isNull() ? 0 : corners.p[index].width();
	};
	const auto topLeft = devSize(kTopLeft);
	const auto topRight = devSize(kTopRight);
	const auto bottomLeft = devSize(kBottomLeft);
	const auto bottomRight = devSize(kBottomRight);
	const auto topDev = std::max(topLeft, topRight);
	const auto bottomDev = std::max(bottomLeft, bottomRight);

	const auto fillBg = [&](int fdx, int fdy, int fdw, int fdh) {
		if (fdw <= 0 || fdh <= 0) {
			return;
		}
		p.fillRect(QRectF(fdx / ratio, fdy / ratio, fdw / ratio, fdh / ratio), bg);
	};
	const auto fillCorner = [&](int cdx, int cdy, int index) {
		if (const auto &pix = corners.p[index]; !pix.isNull()) {
			p.drawPixmap(QPointF(cdx / ratio, cdy / ratio), pix);
		}
	};

	if (topDev > 0) {
		if (topLeft > 0) {
			fillCorner(dx, dy, kTopLeft);
			if (const auto add = topDev - topLeft; add > 0) {
				fillBg(dx, dy + topLeft, topLeft, add);
			}
		}
		if (const auto fill = dw - topLeft - topRight; fill > 0) {
			fillBg(dx + topLeft, dy, fill, topDev);
		}
		if (topRight > 0) {
			fillCorner(dx + dw - topRight, dy, kTopRight);
			if (const auto add = topDev - topRight; add > 0) {
				fillBg(dx + dw - topRight, dy + topRight, topRight, add);
			}
		}
	}
	if (const auto fill = dh - topDev - bottomDev; fill > 0) {
		fillBg(dx, dy + topDev, dw, fill);
	}
	if (bottomDev > 0) {
		const auto byDev = dy + dh - bottomDev;
		if (bottomLeft > 0) {
			fillCorner(dx, byDev + (bottomDev - bottomLeft), kBottomLeft);
			if (const auto add = bottomDev - bottomLeft; add > 0) {
				fillBg(dx, byDev, bottomLeft, add);
			}
		}
		if (const auto fill = dw - bottomLeft - bottomRight; fill > 0) {
			fillBg(dx + bottomLeft, byDev, fill, bottomDev);
		}
		if (bottomRight > 0) {
			fillCorner(dx + dw - bottomRight, byDev + (bottomDev - bottomRight), kBottomRight);
			if (const auto add = bottomDev - bottomRight; add > 0) {
				fillBg(dx + dw - bottomRight, byDev, bottomRight, add);
			}
		}
	}
}

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index) {
	FillRoundRect(p, x, y, w, h, bg, CachedCornerPixmaps(index));
}

void FillRoundShadow(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, const CornersPixmaps &corners) {
	constexpr auto kLeft = 2;
	constexpr auto kRight = 3;

	const auto ratio = style::DevicePixelRatio();
	const auto size = [&](int index) {
		const auto &pix = corners.p[index];
		return pix.isNull() ? 0 : (pix.width() / ratio);
	};
	const auto fillCorner = [&](int left, int bottom, int index) {
		const auto &pix = corners.p[index];
		if (pix.isNull()) {
			return;
		}
		const auto size = pix.width() / ratio;
		p.drawPixmap(left, bottom - size, pix);
	};
	const auto left = size(kLeft);
	const auto right = size(kRight);
	const auto from = x + left;
	fillCorner(x, y + h + st::msgShadow, kLeft);
	if (const auto width = w - left - right; width > 0) {
		p.fillRect(from, y + h, width, st::msgShadow, shadow);
	}
	fillCorner(x + w - right, y + h + st::msgShadow, kRight);
}

const CornersPixmaps &CachedCornerPixmaps(CachedRoundCorners index) {
	Expects(index >= 0 && index < RoundCornersCount);

	return Corners[index];
}

CornersPixmaps PrepareCornerPixmaps(int radius, style::color bg, const style::color *sh) {
	auto images = PrepareCorners(radius, bg, sh);
	auto result = CornersPixmaps();
	for (int j = 0; j < 4; ++j) {
		result.p[j] = PixmapFromImage(std::move(images[j]));
		result.p[j].setDevicePixelRatio(style::DevicePixelRatio());
	}
	return result;
}

CornersPixmaps PrepareCornerPixmaps(ImageRoundRadius radius, style::color bg, const style::color *sh) {
	switch (radius) {
	case ImageRoundRadius::Small:
		return PrepareCornerPixmaps(st::roundRadiusSmall, bg, sh);
	case ImageRoundRadius::Large:
		return PrepareCornerPixmaps(st::roundRadiusLarge, bg, sh);
	}
	Unexpected("Image round radius in PrepareCornerPixmaps.");
}

CornersPixmaps PrepareInvertedCornerPixmaps(int radius, style::color bg) {
	const auto size = style::DevicePixels(radius );
	auto circle = style::colorizeImage(
		style::createInvertedCircleMask(radius * 2),
		bg);
	circle.setDevicePixelRatio(style::DevicePixelRatio());
	auto result = CornersPixmaps();
	const auto fill = [&](int index, int xoffset, int yoffset) {
		result.p[index] = PixmapFromImage(
			circle.copy(QRect(xoffset, yoffset, size, size)));
	};
	fill(0, 0, 0);
	fill(1, size, 0);
	fill(2, size, size);
	fill(3, 0, size);
	return result;
}

[[nodiscard]] int CachedCornerRadiusValue(CachedCornerRadius tag) {
	using Radius = CachedCornerRadius;
	switch (tag) {
	case Radius::Small: return st::roundRadiusSmall;
	case Radius::ThumbSmall: return MsgFileThumbRadiusSmall();
	case Radius::ThumbLarge: return MsgFileThumbRadiusLarge();
	case Radius::BubbleSmall: return BubbleRadiusSmall();
	case Radius::BubbleLarge: return BubbleRadiusLarge();
	}
	Unexpected("Radius tag in CachedCornerRadiusValue.");
}

[[nodiscard]] const std::array<QImage, 4> &CachedCornersMasks(
		CachedCornerRadius radius) {
	const auto index = static_cast<int>(radius);
	Assert(index >= 0 && index < kCachedCornerRadiusCount);

	if (CachedMasks[index][0].isNull()) {
		CachedMasks[index] = Images::CornersMask(
			CachedCornerRadiusValue(CachedCornerRadius(index)));
	}
	return CachedMasks[index];
}

} // namespace Ui
