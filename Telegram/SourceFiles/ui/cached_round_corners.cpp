/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/cached_round_corners.h"

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

std::vector<CornersPixmaps> Corners;
base::flat_map<uint32, CornersPixmaps> CornersMap;
QImage CornersMaskLarge[4], CornersMaskSmall[4];
rpl::lifetime PaletteChangedLifetime;

[[nodiscard]] std::array<QImage, 4> PrepareCorners(int32 radius, const QBrush &brush, const style::color *shadow = nullptr) {
	int32 r = radius * style::DevicePixelRatio(), s = st::msgShadow * style::DevicePixelRatio();
	QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied);
	{
		auto p = QPainter(&rect);
		PainterHighQualityEnabler hq(p);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(QRect(0, 0, rect.width(), rect.height()), Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
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
	mask = PrepareCorners(st::historyMessageRadius, QColor(255, 255, 255), nullptr);
	for (int i = 0; i < 4; ++i) {
		CornersMaskLarge[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
		CornersMaskLarge[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CreatePaletteCorners() {
	PrepareCorners(MenuCorners, st::roundRadiusSmall, st::menuBg);
	PrepareCorners(BoxCorners, st::boxRadius, st::boxBg);
	PrepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
	PrepareCorners(OverviewVideoCorners, st::overviewVideoStatusRadius, st::msgDateImgBg);
	PrepareCorners(OverviewVideoSelectedCorners, st::overviewVideoStatusRadius, st::msgDateImgBgSelected);
	PrepareCorners(ForwardCorners, st::historyMessageRadius, st::historyForwardChooseBg);
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
	) | rpl::start_with_next([=] {
		CreatePaletteCorners();
	}, PaletteChangedLifetime);
}

void FinishCachedCorners() {
	Corners.clear();
	CornersMap.clear();
	PaletteChangedLifetime.destroy();
}

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corner, const style::color *shadow, RectParts parts) {
	auto cornerWidth = corner.p[0].width() / style::DevicePixelRatio();
	auto cornerHeight = corner.p[0].height() / style::DevicePixelRatio();
	if (w < 2 * cornerWidth || h < 2 * cornerHeight) return;
	if (w > 2 * cornerWidth) {
		if (parts & RectPart::Top) {
			p.fillRect(x + cornerWidth, y, w - 2 * cornerWidth, cornerHeight, bg);
		}
		if (parts & RectPart::Bottom) {
			p.fillRect(x + cornerWidth, y + h - cornerHeight, w - 2 * cornerWidth, cornerHeight, bg);
			if (shadow) {
				p.fillRect(x + cornerWidth, y + h, w - 2 * cornerWidth, st::msgShadow, *shadow);
			}
		}
	}
	if (h > 2 * cornerHeight) {
		if ((parts & RectPart::NoTopBottom) == RectPart::NoTopBottom) {
			p.fillRect(x, y + cornerHeight, w, h - 2 * cornerHeight, bg);
		} else {
			if (parts & RectPart::Left) {
				p.fillRect(x, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, bg);
			}
			if ((parts & RectPart::Center) && w > 2 * cornerWidth) {
				p.fillRect(x + cornerWidth, y + cornerHeight, w - 2 * cornerWidth, h - 2 * cornerHeight, bg);
			}
			if (parts & RectPart::Right) {
				p.fillRect(x + w - cornerWidth, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, bg);
			}
		}
	}
	if (parts & RectPart::TopLeft) {
		p.drawPixmap(x, y, corner.p[0]);
	}
	if (parts & RectPart::TopRight) {
		p.drawPixmap(x + w - cornerWidth, y, corner.p[1]);
	}
	if (parts & RectPart::BottomLeft) {
		p.drawPixmap(x, y + h - cornerHeight, corner.p[2]);
	}
	if (parts & RectPart::BottomRight) {
		p.drawPixmap(x + w - cornerWidth, y + h - cornerHeight, corner.p[3]);
	}
}

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index, const style::color *shadow, RectParts parts) {
	FillRoundRect(p, x, y, w, h, bg, Corners[index], shadow, parts);
}

void FillRoundShadow(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, CachedRoundCorners index, RectParts parts) {
	FillRoundShadow(p, x, y, w, h, shadow, Corners[index], parts);
}

void FillRoundShadow(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, const CornersPixmaps &corner, RectParts parts) {
	auto cornerWidth = corner.p[0].width() / style::DevicePixelRatio();
	auto cornerHeight = corner.p[0].height() / style::DevicePixelRatio();
	if (parts & RectPart::Bottom) {
		p.fillRect(x + cornerWidth, y + h, w - 2 * cornerWidth, st::msgShadow, shadow);
	}
	if (parts & RectPart::BottomLeft) {
		p.fillRect(x, y + h - cornerHeight, cornerWidth, st::msgShadow, shadow);
		p.drawPixmap(x, y + h - cornerHeight + st::msgShadow, corner.p[2]);
	}
	if (parts & RectPart::BottomRight) {
		p.fillRect(x + w - cornerWidth, y + h - cornerHeight, cornerWidth, st::msgShadow, shadow);
		p.drawPixmap(x + w - cornerWidth, y + h - cornerHeight + st::msgShadow, corner.p[3]);
	}
}

CornersPixmaps PrepareCornerPixmaps(int32 radius, style::color bg, const style::color *sh) {
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
		return PrepareCornerPixmaps(st::historyMessageRadius, bg, sh);
	}
	Unexpected("Image round radius in PrepareCornerPixmaps.");
}

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts) {
	if (radius == ImageRoundRadius::None) {
		p.fillRect(x, y, w, h, bg);
		return;
	}
	const auto colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24)
		| ((uint32(bg->c.red()) & 0xFF) << 16)
		| ((uint32(bg->c.green()) & 0xFF) << 8)
		| ((uint32(bg->c.blue()) & 0xFF));
	auto i = CornersMap.find(colorKey);
	if (i == end(CornersMap)) {
		i = CornersMap.emplace(
			colorKey,
			PrepareCornerPixmaps(radius, bg, nullptr)).first;
	}
	FillRoundRect(p, x, y, w, h, bg, i->second, nullptr, parts);
}

} // namespace Ui
