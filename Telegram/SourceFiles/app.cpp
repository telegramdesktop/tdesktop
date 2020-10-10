/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "app.h"

#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_abstract_structure.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "history/history.h"
#include "history/history_location_manager.h"
#include "history/history_item_components.h"
#include "history/view/history_view_service_message.h"
#include "media/audio/media_audio.h"
#include "ui/image/image.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "platform/platform_notifications_manager.h"
#include "storage/file_upload.h"
#include "storage/localstorage.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "numbers.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_overview.h"
#include "styles/style_media_view.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

#include <QtCore/QBuffer>
#include <QtGui/QFontDatabase>

#ifdef OS_MAC_OLD
#include <libexif/exif-data.h>
#endif // OS_MAC_OLD

namespace {

constexpr auto kImageAreaLimit = 12'032 * 9'024;

App::LaunchState _launchState = App::Launched;

HistoryView::Element *hoveredItem = nullptr,
	*pressedItem = nullptr,
	*hoveredLinkItem = nullptr,
	*pressedLinkItem = nullptr,
	*mousedItem = nullptr;

struct CornersPixmaps {
	QPixmap p[4];
};
QVector<CornersPixmaps> corners;
using CornersMap = QMap<uint32, CornersPixmaps>;
CornersMap cornersMap;
QImage cornersMaskLarge[4], cornersMaskSmall[4];

} // namespace

namespace App {

	QString formatPhone(QString phone) {
		if (phone.isEmpty()) return QString();
		if (phone.at(0) == '0') return phone;

		QString number = phone;
		for (const QChar *ch = phone.constData(), *e = ch + phone.size(); ch != e; ++ch) {
			if (ch->unicode() < '0' || ch->unicode() > '9') {
				number = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
			}
		}
		QVector<int> groups = phoneNumberParse(number);
		if (groups.isEmpty()) return '+' + number;

		QString result;
		result.reserve(number.size() + groups.size() + 1);
		result.append('+');
		int32 sum = 0;
		for (int32 i = 0, l = groups.size(); i < l; ++i) {
			result.append(number.midRef(sum, groups.at(i)));
			sum += groups.at(i);
			if (sum < number.size()) result.append(' ');
		}
		if (sum < number.size()) result.append(number.midRef(sum));
		return result;
	}

	void prepareCorners(RoundCorners index, int32 radius, const QBrush &brush, const style::color *shadow = nullptr, QImage *cors = nullptr) {
		Expects(::corners.size() > index);

		int32 r = radius * cIntRetinaFactor(), s = st::msgShadow * cIntRetinaFactor();
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
				::corners[index].p[i] = pixmapFromImageInPlace(std::move(cors[i]));
				::corners[index].p[i].setDevicePixelRatio(cRetinaFactor());
			}
		}
	}

	void createMaskCorners() {
		QImage mask[4];
		prepareCorners(SmallMaskCorners, st::buttonRadius, QColor(255, 255, 255), nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskSmall[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
			::cornersMaskSmall[i].setDevicePixelRatio(cRetinaFactor());
		}
		prepareCorners(LargeMaskCorners, st::historyMessageRadius, QColor(255, 255, 255), nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskLarge[i] = mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
			::cornersMaskLarge[i].setDevicePixelRatio(cRetinaFactor());
		}
	}

	void createPaletteCorners() {
		prepareCorners(MenuCorners, st::buttonRadius, st::menuBg);
		prepareCorners(BoxCorners, st::boxRadius, st::boxBg);
		prepareCorners(BotKbOverCorners, st::dateRadius, st::msgBotKbOverBgAdd);
		prepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
		prepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceBgSelected);
		prepareCorners(SelectedOverlaySmallCorners, st::buttonRadius, st::msgSelectOverlay);
		prepareCorners(SelectedOverlayLargeCorners, st::historyMessageRadius, st::msgSelectOverlay);
		prepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
		prepareCorners(DateSelectedCorners, st::dateRadius, st::msgDateImgBgSelected);
		prepareCorners(OverviewVideoCorners, st::overviewVideoStatusRadius, st::msgDateImgBg);
		prepareCorners(OverviewVideoSelectedCorners, st::overviewVideoStatusRadius, st::msgDateImgBgSelected);
		prepareCorners(InShadowCorners, st::historyMessageRadius, st::msgInShadow);
		prepareCorners(InSelectedShadowCorners, st::historyMessageRadius, st::msgInShadowSelected);
		prepareCorners(ForwardCorners, st::historyMessageRadius, st::historyForwardChooseBg);
		prepareCorners(MediaviewSaveCorners, st::mediaviewControllerRadius, st::mediaviewSaveMsgBg);
		prepareCorners(EmojiHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(StickerHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(BotKeyboardCorners, st::buttonRadius, st::botKbBg);
		prepareCorners(PhotoSelectOverlayCorners, st::buttonRadius, st::overviewPhotoSelectOverlay);

		prepareCorners(Doc1Corners, st::buttonRadius, st::msgFile1Bg);
		prepareCorners(Doc2Corners, st::buttonRadius, st::msgFile2Bg);
		prepareCorners(Doc3Corners, st::buttonRadius, st::msgFile3Bg);
		prepareCorners(Doc4Corners, st::buttonRadius, st::msgFile4Bg);

		prepareCorners(MessageInCorners, st::historyMessageRadius, st::msgInBg, &st::msgInShadow);
		prepareCorners(MessageInSelectedCorners, st::historyMessageRadius, st::msgInBgSelected, &st::msgInShadowSelected);
		prepareCorners(MessageOutCorners, st::historyMessageRadius, st::msgOutBg, &st::msgOutShadow);
		prepareCorners(MessageOutSelectedCorners, st::historyMessageRadius, st::msgOutBgSelected, &st::msgOutShadowSelected);

		prepareCorners(SendFilesBoxAlbumGroupCorners, st::sendBoxAlbumGroupRadius, st::callFingerprintBg);
	}

	void createCorners() {
		::corners.resize(RoundCornersCount);
		createMaskCorners();
		createPaletteCorners();
	}

	void clearCorners() {
		::corners.clear();
		::cornersMap.clear();
	}

	void initMedia() {
		createCorners();

		using Update = Window::Theme::BackgroundUpdate;
		static auto subscription = Window::Theme::Background()->add_subscription([](const Update &update) {
			if (update.paletteChanged()) {
				createPaletteCorners();

				if (const auto m = App::main()) { // multi good
					m->updateScrollColors();
				}
				HistoryView::serviceColorsUpdated();
			} else if (update.type == Update::Type::New) {
				prepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
				prepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceBgSelected);

				if (const auto m = App::main()) { // multi good
					m->updateScrollColors();
				}
				HistoryView::serviceColorsUpdated();
			}
		});
	}

	void deinitMedia() {
		clearCorners();

		Data::clearGlobalStructures();
	}

	void hoveredItem(HistoryView::Element *item) {
		::hoveredItem = item;
	}

	HistoryView::Element *hoveredItem() {
		return ::hoveredItem;
	}

	void pressedItem(HistoryView::Element *item) {
		::pressedItem = item;
	}

	HistoryView::Element *pressedItem() {
		return ::pressedItem;
	}

	void hoveredLinkItem(HistoryView::Element *item) {
		::hoveredLinkItem = item;
	}

	HistoryView::Element *hoveredLinkItem() {
		return ::hoveredLinkItem;
	}

	void pressedLinkItem(HistoryView::Element *item) {
		::pressedLinkItem = item;
	}

	HistoryView::Element *pressedLinkItem() {
		return ::pressedLinkItem;
	}

	void mousedItem(HistoryView::Element *item) {
		::mousedItem = item;
	}

	HistoryView::Element *mousedItem() {
		return ::mousedItem;
	}

	void clearMousedItems() {
		hoveredItem(nullptr);
		pressedItem(nullptr);
		hoveredLinkItem(nullptr);
		pressedLinkItem(nullptr);
		mousedItem(nullptr);
	}

	void quit() {
		if (quitting()) {
			return;
		} else if (Core::IsAppLaunched()
			&& Core::App().exportPreventsQuit()) {
			return;
		}
		setLaunchState(QuitRequested);

		if (auto window = App::wnd()) {
			if (!Core::Sandbox::Instance().isSavingSession()) {
				window->hide();
			}
		}
		Core::Application::QuitAttempt();
	}

	bool quitting() {
		return _launchState != Launched;
	}

	LaunchState launchState() {
		return _launchState;
	}

	void setLaunchState(LaunchState state) {
		_launchState = state;
	}

	void restart() {
		using namespace Core;
		const auto updateReady = !UpdaterDisabled()
			&& (UpdateChecker().state() == UpdateChecker::State::Ready);
		if (updateReady) {
			cSetRestartingUpdate(true);
		} else {
			cSetRestarting(true);
			cSetRestartingToSettings(true);
		}
		App::quit();
	}

	QImage readImage(QByteArray data, QByteArray *format, bool opaque, bool *animated) {
		if (data.isEmpty()) {
			return QImage();
		}
		QByteArray tmpFormat;
		QImage result;
		QBuffer buffer(&data);
		if (!format) {
			format = &tmpFormat;
		}
		{
			QImageReader reader(&buffer, *format);
#ifndef OS_MAC_OLD
			reader.setAutoTransform(true);
#endif // OS_MAC_OLD
			if (animated) *animated = reader.supportsAnimation() && reader.imageCount() > 1;
			if (!reader.canRead()) {
				return QImage();
			}
			const auto imageSize = reader.size();
			if (imageSize.width() * imageSize.height() > kImageAreaLimit) {
				return QImage();
			}
			QByteArray fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
			if (!reader.read(&result)) {
				return QImage();
			}
			fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
		}
		buffer.seek(0);
		auto fmt = QString::fromUtf8(*format).toLower();
		if (fmt == "jpg" || fmt == "jpeg") {
#ifdef OS_MAC_OLD
			if (auto exifData = exif_data_new_from_data((const uchar*)(data.constData()), data.size())) {
				auto byteOrder = exif_data_get_byte_order(exifData);
				if (auto exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION)) {
					auto orientationFix = [exifEntry, byteOrder] {
						auto orientation = exif_get_short(exifEntry->data, byteOrder);
						switch (orientation) {
						case 2: return QTransform(-1, 0, 0, 1, 0, 0);
						case 3: return QTransform(-1, 0, 0, -1, 0, 0);
						case 4: return QTransform(1, 0, 0, -1, 0, 0);
						case 5: return QTransform(0, -1, -1, 0, 0, 0);
						case 6: return QTransform(0, 1, -1, 0, 0, 0);
						case 7: return QTransform(0, 1, 1, 0, 0, 0);
						case 8: return QTransform(0, -1, 1, 0, 0, 0);
						}
						return QTransform();
					};
					result = result.transformed(orientationFix());
				}
				exif_data_free(exifData);
			}
#endif // OS_MAC_OLD
		} else if (opaque) {
			result = Images::prepareOpaque(std::move(result));
		}
		return result;
	}

	QImage readImage(const QString &file, QByteArray *format, bool opaque, bool *animated, QByteArray *content) {
		QFile f(file);
		if (f.size() > kImageSizeLimit || !f.open(QIODevice::ReadOnly)) {
			if (animated) *animated = false;
			return QImage();
		}
		auto imageBytes = f.readAll();
		auto result = readImage(imageBytes, format, opaque, animated);
		if (content && !result.isNull()) {
			*content = imageBytes;
		}
		return result;
	}

	QPixmap pixmapFromImageInPlace(QImage &&image) {
		return QPixmap::fromImage(std::move(image), Qt::ColorOnly);
	}

	void rectWithCorners(Painter &p, QRect rect, const style::color &bg, RoundCorners index, RectParts corners) {
		auto parts = RectPart::Top
			| RectPart::NoTopBottom
			| RectPart::Bottom
			| corners;
		roundRect(p, rect, bg, index, nullptr, parts);
		if ((corners & RectPart::AllCorners) != RectPart::AllCorners) {
			const auto size = ::corners[index].p[0].width() / cIntRetinaFactor();
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

	void complexOverlayRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
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
			rectWithCorners(p, rect, bg, overlayCorners, corners);
		}
	}

	void complexLocationRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners) {
		rectWithCorners(p, rect, st::msgInBg, MessageInCorners, corners);
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corner, const style::color *shadow, RectParts parts) {
		auto cornerWidth = corner.p[0].width() / cIntRetinaFactor();
		auto cornerHeight = corner.p[0].height() / cIntRetinaFactor();
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

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, RoundCorners index, const style::color *shadow, RectParts parts) {
		roundRect(p, x, y, w, h, bg, ::corners[index], shadow, parts);
	}

	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, RoundCorners index, RectParts parts) {
		auto &corner = ::corners[index];
		auto cornerWidth = corner.p[0].width() / cIntRetinaFactor();
		auto cornerHeight = corner.p[0].height() / cIntRetinaFactor();
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

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts) {
		auto colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24) | ((uint32(bg->c.red()) & 0xFF) << 16) | ((uint32(bg->c.green()) & 0xFF) << 8) | ((uint32(bg->c.blue()) & 0xFF) << 24);
		auto i = cornersMap.find(colorKey);
		if (i == cornersMap.cend()) {
			QImage images[4];
			switch (radius) {
			case ImageRoundRadius::Small: prepareCorners(SmallMaskCorners, st::buttonRadius, bg, nullptr, images); break;
			case ImageRoundRadius::Large: prepareCorners(LargeMaskCorners, st::historyMessageRadius, bg, nullptr, images); break;
			default: p.fillRect(x, y, w, h, bg); return;
			}

			CornersPixmaps pixmaps;
			for (int j = 0; j < 4; ++j) {
				pixmaps.p[j] = pixmapFromImageInPlace(std::move(images[j]));
				pixmaps.p[j].setDevicePixelRatio(cRetinaFactor());
			}
			i = cornersMap.insert(colorKey, pixmaps);
		}
		roundRect(p, x, y, w, h, bg, i.value(), nullptr, parts);
	}

}
