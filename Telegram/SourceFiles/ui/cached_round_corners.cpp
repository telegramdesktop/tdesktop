/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/cached_round_corners.h"

#include "ui/ui_utility.h"
#include "ui/image/image_prepare.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_overview.h"
#include "styles/style_media_view.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

struct CornersPixmaps {
	QPixmap p[4];
};
std::vector<CornersPixmaps> Corners;
base::flat_map<uint32, CornersPixmaps> CornersMap;
QImage CornersMaskLarge[4], CornersMaskSmall[4];
rpl::lifetime PaletteChangedLifetime;

void PrepareCorners(CachedRoundCorners index, int32 radius, const QBrush &brush, const style::color *shadow = nullptr, QImage *cors = nullptr) {
	Expects(Corners.size() > index);

	int32 r = radius * style::DevicePixelRatio(), s = st::msgShadow * style::DevicePixelRatio();
	QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied), localCors[4];
	{
		Painter p(&rect);
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
	if (!cors) cors = localCors;
	cors[0] = rect.copy(0, 0, r, r);
	cors[1] = rect.copy(r * 2, 0, r, r);
	cors[2] = rect.copy(0, r * 2, r, r + (shadow ? s : 0));
	cors[3] = rect.copy(r * 2, r * 2, r, r + (shadow ? s : 0));
	if (index != SmallMaskCorners && index != LargeMaskCorners) {
		for (int i = 0; i < 4; ++i) {
			Corners[index].p[i] = PixmapFromImage(std::move(cors[i]));
			Corners[index].p[i].setDevicePixelRatio(style::DevicePixelRatio());
		}
	}
}

void CreateMaskCorners() {
	QImage mask[4];
	PrepareCorners(SmallMaskCorners, st::buttonRadius, QColor(255, 255, 255), nullptr, mask);
	for (int i = 0; i < 4; ++i) {
		CornersMaskSmall[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
		CornersMaskSmall[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
	PrepareCorners(LargeMaskCorners, st::historyMessageRadius, QColor(255, 255, 255), nullptr, mask);
	for (int i = 0; i < 4; ++i) {
		CornersMaskLarge[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
		CornersMaskLarge[i].setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CreatePaletteCorners() {
	PrepareCorners(MenuCorners, st::buttonRadius, st::menuBg);
	PrepareCorners(BoxCorners, st::boxRadius, st::boxBg);
	PrepareCorners(BotKbOverCorners, st::dateRadius, st::msgBotKbOverBgAdd);
	PrepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
	PrepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceBgSelected);
	PrepareCorners(SelectedOverlaySmallCorners, st::buttonRadius, st::msgSelectOverlay);
	PrepareCorners(SelectedOverlayLargeCorners, st::historyMessageRadius, st::msgSelectOverlay);
	PrepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
	PrepareCorners(DateSelectedCorners, st::dateRadius, st::msgDateImgBgSelected);
	PrepareCorners(OverviewVideoCorners, st::overviewVideoStatusRadius, st::msgDateImgBg);
	PrepareCorners(OverviewVideoSelectedCorners, st::overviewVideoStatusRadius, st::msgDateImgBgSelected);
	PrepareCorners(InShadowCorners, st::historyMessageRadius, st::msgInShadow);
	PrepareCorners(InSelectedShadowCorners, st::historyMessageRadius, st::msgInShadowSelected);
	PrepareCorners(ForwardCorners, st::historyMessageRadius, st::historyForwardChooseBg);
	PrepareCorners(MediaviewSaveCorners, st::mediaviewControllerRadius, st::mediaviewSaveMsgBg);
	PrepareCorners(EmojiHoverCorners, st::buttonRadius, st::emojiPanHover);
	PrepareCorners(StickerHoverCorners, st::buttonRadius, st::emojiPanHover);
	PrepareCorners(BotKeyboardCorners, st::buttonRadius, st::botKbBg);
	PrepareCorners(PhotoSelectOverlayCorners, st::buttonRadius, st::overviewPhotoSelectOverlay);

	PrepareCorners(Doc1Corners, st::buttonRadius, st::msgFile1Bg);
	PrepareCorners(Doc2Corners, st::buttonRadius, st::msgFile2Bg);
	PrepareCorners(Doc3Corners, st::buttonRadius, st::msgFile3Bg);
	PrepareCorners(Doc4Corners, st::buttonRadius, st::msgFile4Bg);

	PrepareCorners(MessageInCorners, st::historyMessageRadius, st::msgInBg, &st::msgInShadow);
	PrepareCorners(MessageInSelectedCorners, st::historyMessageRadius, st::msgInBgSelected, &st::msgInShadowSelected);
	PrepareCorners(MessageOutCorners, st::historyMessageRadius, st::msgOutBg, &st::msgOutShadow);
	PrepareCorners(MessageOutSelectedCorners, st::historyMessageRadius, st::msgOutBgSelected, &st::msgOutShadowSelected);
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

void RectWithCorners(Painter &p, QRect rect, const style::color &bg, CachedRoundCorners index, RectParts corners) {
	auto parts = RectPart::Top
		| RectPart::NoTopBottom
		| RectPart::Bottom
		| corners;
	FillRoundRect(p, rect, bg, index, nullptr, parts);
	if ((corners & RectPart::AllCorners) != RectPart::AllCorners) {
		const auto size = Corners[index].p[0].width() / style::DevicePixelRatio();
		if (!(corners & RectPart::TopLeft)) {
			p.fillRect(rect.x(), rect.y(), size, size, bg);
		}
		if (!(corners & RectPart::TopRight)) {
			p.fillRect(rect.x() + rect.width() - size, rect.y(), size, size, bg);
		}
		if (!(corners & RectPart::BottomLeft)) {
			p.fillRect(rect.x(), rect.y() + rect.height() - size, size, size, bg);
		}
		if (!(corners & RectPart::BottomRight)) {
			p.fillRect(rect.x() + rect.width() - size, rect.y() + rect.height() - size, size, size, bg);
		}
	}
}

void FillComplexOverlayRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
	if (radius == ImageRoundRadius::Ellipse) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(p.textPalette().selectOverlay);
		p.drawEllipse(rect);
	} else {
		auto overlayCorners = (radius == ImageRoundRadius::Small)
			? SelectedOverlaySmallCorners
			: SelectedOverlayLargeCorners;
		const auto bg = p.textPalette().selectOverlay;
		RectWithCorners(p, rect, bg, overlayCorners, corners);
	}
}

void FillComplexLocationRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
	RectWithCorners(p, rect, st::msgInBg, MessageInCorners, corners);
}

void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corner, const style::color *shadow, RectParts parts) {
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

void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index, const style::color *shadow, RectParts parts) {
	FillRoundRect(p, x, y, w, h, bg, Corners[index], shadow, parts);
}

void FillRoundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, CachedRoundCorners index, RectParts parts) {
	auto &corner = Corners[index];
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

void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts) {
	auto colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24) | ((uint32(bg->c.red()) & 0xFF) << 16) | ((uint32(bg->c.green()) & 0xFF) << 8) | ((uint32(bg->c.blue()) & 0xFF) << 24);
	auto i = CornersMap.find(colorKey);
	if (i == CornersMap.cend()) {
		QImage images[4];
		switch (radius) {
		case ImageRoundRadius::Small: PrepareCorners(SmallMaskCorners, st::buttonRadius, bg, nullptr, images); break;
		case ImageRoundRadius::Large: PrepareCorners(LargeMaskCorners, st::historyMessageRadius, bg, nullptr, images); break;
		default: p.fillRect(x, y, w, h, bg); return;
		}

		CornersPixmaps pixmaps;
		for (int j = 0; j < 4; ++j) {
			pixmaps.p[j] = PixmapFromImage(std::move(images[j]));
			pixmaps.p[j].setDevicePixelRatio(style::DevicePixelRatio());
		}
		i = CornersMap.emplace(colorKey, pixmaps).first;
	}
	FillRoundRect(p, x, y, w, h, bg, i->second, nullptr, parts);
}

} // namespace Ui
