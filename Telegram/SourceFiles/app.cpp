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
#include "ui/cached_round_corners.h"
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

	void initMedia() {
		Ui::StartCachedCorners();

		using Update = Window::Theme::BackgroundUpdate;
		static auto subscription = Window::Theme::Background()->add_subscription([](const Update &update) {
			if (update.paletteChanged()) {
				if (const auto m = App::main()) { // multi good
					m->updateScrollColors();
				}
				HistoryView::serviceColorsUpdated();
			}
		});
	}

	void deinitMedia() {
		Ui::FinishCachedCorners();
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

}
