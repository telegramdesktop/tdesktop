/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_widget.h"

#include "apiwrap.h"
#include "api/api_attached_stickers.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "core/ui_integration.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/item_text_options.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "boxes/confirm_box.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_group_thumbs.h"
#include "media/view/media_view_pip.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/player/media_player_instance.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/media/history_view_media.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_media_rotation.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "main/main_account.h"
#include "main/main_domain.h" // Domain::activeSessionValue.
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "layout.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"
#include "calls/calls_instance.h"
#include "facades.h"
#include "app.h"
#include "styles/style_media_view.h"
#include "styles/style_chat.h"

#ifdef Q_OS_MAC
#include "platform/mac/touchbar/mac_touchbar_media_view.h"
#endif // Q_OS_MAC

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QBuffer>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Media {
namespace View {
namespace {

constexpr auto kPreloadCount = 3;
constexpr auto kMaxZoomLevel = 7; // x8
constexpr auto kZoomToScreenLevel = 1024;
constexpr auto kOverlayLoaderPriority = 2;

// macOS OpenGL renderer fails to render larger texture
// even though it reports that max texture size is 16384.
constexpr auto kMaxDisplayImageSize = 4096;

// Preload X message ids before and after current.
constexpr auto kIdsLimit = 48;

// Preload next messages if we went further from current than that.
constexpr auto kIdsPreloadAfter = 28;

class PipDelegate final : public Pip::Delegate {
public:
	PipDelegate(QWidget *parent, not_null<Main::Session*> session);

	void pipSaveGeometry(QByteArray geometry) override;
	QByteArray pipLoadGeometry() override;
	float64 pipPlaybackSpeed() override;
	QWidget *pipParentWidget() override;

private:
	QWidget *_parent = nullptr;
	not_null<Main::Session*> _session;

};

PipDelegate::PipDelegate(QWidget *parent, not_null<Main::Session*> session)
: _parent(parent)
, _session(session) {
}

void PipDelegate::pipSaveGeometry(QByteArray geometry) {
	Core::App().settings().setVideoPipGeometry(geometry);
	Core::App().saveSettingsDelayed();
}

QByteArray PipDelegate::pipLoadGeometry() {
	return Core::App().settings().videoPipGeometry();
}

float64 PipDelegate::pipPlaybackSpeed() {
	return Core::App().settings().videoPlaybackSpeed();
}

QWidget *PipDelegate::pipParentWidget() {
	return _parent;
}

Images::Options VideoThumbOptions(DocumentData *document) {
	const auto result = Images::Option::Smooth | Images::Option::Blurred;
	return (document && document->isVideoMessage())
		? (result | Images::Option::Circled)
		: result;
}

void PaintImageProfile(QPainter &p, const QImage &image, QRect rect, QRect fill) {
	const auto argb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto rgb = image.convertToFormat(QImage::Format_RGB32);
	const auto argbp = QPixmap::fromImage(argb);
	const auto rgbp = QPixmap::fromImage(rgb);
	const auto width = image.width();
	const auto height = image.height();
	const auto xcopies = (fill.width() + width - 1) / width;
	const auto ycopies = (fill.height() + height - 1) / height;
	const auto copies = xcopies * ycopies;
	auto times = QStringList();
	const auto bench = [&](QString label, auto &&paint) {
		const auto single = [&](QString label) {
			auto now = crl::now();
			const auto push = [&] {
				times.push_back(QString("%1").arg(crl::now() - now, 4, 10, QChar(' ')));
				now = crl::now();
			};
			paint(rect);
			push();
			{
				PainterHighQualityEnabler hq(p);
				paint(rect);
			}
			push();
			for (auto i = 0; i < xcopies; ++i) {
				for (auto j = 0; j < ycopies; ++j) {
					paint(QRect(
						fill.topLeft() + QPoint(i * width, j * height),
						QSize(width, height)));
				}
			}
			push();
			LOG(("FRAME (%1): %2 (copies: %3)").arg(label).arg(times.join(' ')).arg(copies));
			times = QStringList();
			now = crl::now();
		};
		p.setCompositionMode(QPainter::CompositionMode_Source);
		single(label + " S");
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		single(label + " O");
	};
	bench("ARGB I", [&](QRect rect) {
		p.drawImage(rect, argb);
	});
	bench("RGB  I", [&](QRect rect) {
		p.drawImage(rect, rgb);
	});
	bench("ARGB P", [&](QRect rect) {
		p.drawPixmap(rect, argbp);
	});
	bench("RGB  P", [&](QRect rect) {
		p.drawPixmap(rect, rgbp);
	});
}

QPixmap PrepareStaticImage(const QString &path) {
	auto image = App::readImage(path, nullptr, false);
#if defined Q_OS_MAC && !defined OS_MAC_OLD
	if (image.width() > kMaxDisplayImageSize
		|| image.height() > kMaxDisplayImageSize) {
		image = image.scaled(
			kMaxDisplayImageSize,
			kMaxDisplayImageSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
#endif // Q_OS_MAC && !OS_MAC_OLD
	return App::pixmapFromImageInPlace(std::move(image));
}

} // namespace

struct OverlayWidget::SharedMedia {
	SharedMedia(SharedMediaKey key) : key(key) {
	}

	SharedMediaKey key;
	rpl::lifetime lifetime;
};

struct OverlayWidget::UserPhotos {
	UserPhotos(UserPhotosKey key) : key(key) {
	}

	UserPhotosKey key;
	rpl::lifetime lifetime;
};

struct OverlayWidget::Collage {
	Collage(CollageKey key) : key(key) {
	}

	CollageKey key;
};

struct OverlayWidget::Streamed {
	Streamed(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		QWidget *controlsParent,
		not_null<PlaybackControls::Delegate*> controlsDelegate,
		Fn<void()> waitingCallback);
	Streamed(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		QWidget *controlsParent,
		not_null<PlaybackControls::Delegate*> controlsDelegate,
		Fn<void()> waitingCallback);

	Streaming::Instance instance;
	PlaybackControls controls;

	QImage frameForDirectPaint;

	bool withSound = false;
	bool pausedBySeek = false;
	bool resumeOnCallEnd = false;
};

struct OverlayWidget::PipWrap {
	PipWrap(
		QWidget *parent,
		not_null<DocumentData*> document,
		FullMsgId contextId,
		std::shared_ptr<Streaming::Document> shared,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);

	PipWrap(const PipWrap &other) = delete;
	PipWrap &operator=(const PipWrap &other) = delete;

	PipDelegate delegate;
	Pip wrapped;
};

OverlayWidget::Streamed::Streamed(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	QWidget *controlsParent,
	not_null<PlaybackControls::Delegate*> controlsDelegate,
	Fn<void()> waitingCallback)
: instance(document, origin, std::move(waitingCallback))
, controls(controlsParent, controlsDelegate) {
}

OverlayWidget::Streamed::Streamed(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	QWidget *controlsParent,
	not_null<PlaybackControls::Delegate*> controlsDelegate,
	Fn<void()> waitingCallback)
: instance(photo, origin, std::move(waitingCallback))
, controls(controlsParent, controlsDelegate) {
}

OverlayWidget::PipWrap::PipWrap(
	QWidget *parent,
	not_null<DocumentData*> document,
	FullMsgId contextId,
	std::shared_ptr<Streaming::Document> shared,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: delegate(parent, &document->session())
, wrapped(
	&delegate,
	document,
	contextId,
	std::move(shared),
	std::move(closeAndContinue),
	std::move(destroy)) {
}

OverlayWidget::OverlayWidget()
: OverlayParent(nullptr)
, _transparentBrush(style::transparentPlaceholderBrush())
, _docDownload(this, tr::lng_media_download(tr::now), st::mediaviewFileLink)
, _docSaveAs(this, tr::lng_mediaview_save_as(tr::now), st::mediaviewFileLink)
, _docCancel(this, tr::lng_cancel(tr::now), st::mediaviewFileLink)
, _radial([=](crl::time now) { return radialAnimationCallback(now); })
, _lastAction(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction)
, _stateAnimation([=](crl::time now) { return stateAnimationCallback(now); })
, _dropdown(this, st::mediaviewDropdownMenu)
, _dropdownShowTimer(this) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_lastPositiveVolume = (Core::App().settings().videoVolume() > 0.)
		? Core::App().settings().videoVolume()
		: Core::Settings::kDefaultVolume;

	setWindowTitle(qsl("Media viewer"));

	const auto text = tr::lng_mediaview_saved_to(
		tr::now,
		lt_downloads,
		Ui::Text::Link(
			tr::lng_mediaview_downloads(tr::now),
			"internal:show_saved_message"),
		Ui::Text::WithEntities);
	_saveMsgText.setMarkedText(st::mediaviewSaveMsgStyle, text, Ui::DialogTextOptions());
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::mediaviewSaveMsgPadding.left() + st::mediaviewSaveMsgPadding.right(), st::mediaviewSaveMsgStyle.font->height + st::mediaviewSaveMsgPadding.top() + st::mediaviewSaveMsgPadding.bottom());

	connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(onScreenResized(int)));

	if (Platform::IsLinux()) {
		setWindowFlags(Qt::FramelessWindowHint
			| Qt::MaximizeUsingFullscreenGeometryHint);
	} else if (Platform::IsMac()) {
		// Without Qt::Tool starting with Qt 5.15.1 this widget
		// when being opened from a fullscreen main window was
		// opening not as overlay over the main window, but as
		// a separate fullscreen window with a separate space.
		setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
	} else {
		setWindowFlags(Qt::FramelessWindowHint);
	}
	updateGeometry();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);

	hide();
	createWinId();
	if (Platform::IsLinux()) {
		windowHandle()->setTransientParent(App::wnd()->windowHandle());
		setWindowModality(Qt::WindowModal);
	}
	if (!Platform::IsMac()) {
		setWindowState(Qt::WindowFullScreen);
	}

	connect(
		windowHandle(),
		&QWindow::visibleChanged,
		this,
		[=](bool visible) { handleVisibleChanged(visible); });
	connect(
		windowHandle(),
		&QWindow::screenChanged,
		this,
		[=](QScreen *screen) { handleScreenChanged(screen); });

#if defined Q_OS_MAC && !defined OS_OSX
	TouchBar::SetupMediaViewTouchBar(
		winId(),
		static_cast<PlaybackControls::Delegate*>(this),
		_touchbarTrackState.events(),
		_touchbarDisplay.events(),
		_touchbarFullscreenToggled.events());
#endif // Q_OS_MAC && !OS_OSX

	using namespace rpl::mappers;
	rpl::combine(
		Core::App().calls().currentCallValue(),
		Core::App().calls().currentGroupCallValue(),
		_1 || _2
	) | rpl::start_with_next([=](bool call) {
		if (!_streamed || videoIsGifOrUserpic()) {
			return;
		} else if (call) {
			playbackPauseOnCall();
		} else {
			playbackResumeOnCall();
		}
	}, lifetime());

	_saveMsgUpdater.setSingleShot(true);
	connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	_controlsHideTimer.setSingleShot(true);
	connect(&_controlsHideTimer, SIGNAL(timeout()), this, SLOT(onHideControls()));

	_docDownload->addClickHandler([=] { onDownload(); });
	_docSaveAs->addClickHandler([=] { onSaveAs(); });
	_docCancel->addClickHandler([=] { onSaveCancel(); });

	_dropdown->setHiddenCallback([this] { dropdownHidden(); });
	_dropdownShowTimer->setSingleShot(true);
	connect(_dropdownShowTimer, SIGNAL(timeout()), this, SLOT(onDropdown()));
}

void OverlayWidget::refreshLang() {
	InvokeQueued(this, [this] { updateThemePreviewGeometry(); });
}

void OverlayWidget::moveToScreen() {
	Expects(windowHandle());

	const auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (auto handle = widget ? widget->windowHandle() : nullptr) {
			return handle->screen();
		}
		return nullptr;
	};
	const auto window = Core::App().activeWindow()
		? Core::App().activeWindow()->widget().get()
		: nullptr;
	const auto activeWindowScreen = widgetScreen(window);
	const auto myScreen = widgetScreen(this);
	// Wayland doesn't support positioning, but Qt emits screenChanged anyway
	// and geometry of the widget become broken
	if (activeWindowScreen
		&& myScreen != activeWindowScreen
		&& !Platform::IsWayland()) {
		windowHandle()->setScreen(activeWindowScreen);
	}
	updateGeometry();
}

void OverlayWidget::updateGeometry() {
	const auto screen = windowHandle() && windowHandle()->screen()
		? windowHandle()->screen()
		: QApplication::primaryScreen();
	const auto available = screen->geometry();
	if (geometry() == available) {
		return;
	}
	setGeometry(available);

	auto navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
	_closeNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, st::mediaviewControlMargin, st::mediaviewControlSize, st::mediaviewControlSize);
	_closeNavIcon = style::centerrect(_closeNav, st::mediaviewClose);
	_leftNav = myrtlrect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_leftNavIcon = style::centerrect(_leftNav, st::mediaviewLeft);
	_rightNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_rightNavIcon = style::centerrect(_rightNav, st::mediaviewRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
	_photoRadialRect = QRect(QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2), st::radialSize);

	resizeContentByScreenSize();
	update();
}

QSize OverlayWidget::flipSizeByRotation(QSize size) const {
	return FlipSizeByRotation(size, _rotation);
}

bool OverlayWidget::videoShown() const {
	return _streamed && !_streamed->instance.info().video.cover.isNull();
}

QSize OverlayWidget::videoSize() const {
	Expects(videoShown());

	return flipSizeByRotation(_streamed->instance.info().video.size);
}

bool OverlayWidget::videoIsGifOrUserpic() const {
	return _streamed
		&& (!_document
			|| (_document->isAnimation() && !_document->isVideoMessage()));
}

QImage OverlayWidget::videoFrame() const {
	Expects(videoShown());

	auto request = Streaming::FrameRequest();
	//request.radius = (_document && _document->isVideoMessage())
	//	? ImageRoundRadius::Ellipse
	//	: ImageRoundRadius::None;
	return _streamed->instance.player().ready()
		? _streamed->instance.frame(request)
		: _streamed->instance.info().video.cover;
}

QImage OverlayWidget::videoFrameForDirectPaint() const {
	Expects(_streamed != nullptr);

	const auto result = videoFrame();

#ifdef USE_OPENGL_OVERLAY_WIDGET
	const auto bytesPerLine = result.bytesPerLine();
	if (bytesPerLine == result.width() * 4) {
		return result;
	}

	// On macOS 10.8+ we use QOpenGLWidget as OverlayWidget base class.
	// The OpenGL painter can't paint textures where byte data is with strides.
	// So in that case we prepare a compact copy of the frame to render.
	//
	// See Qt commit ed557c037847e343caa010562952b398f806adcd
	//
	auto &cache = _streamed->frameForDirectPaint;
	if (cache.size() != result.size()) {
		cache = QImage(result.size(), result.format());
	}
	const auto height = result.height();
	const auto line = cache.bytesPerLine();
	Assert(line == result.width() * 4);
	Assert(line < bytesPerLine);

	auto from = result.bits();
	auto to = cache.bits();
	for (auto y = 0; y != height; ++y) {
		memcpy(to, from, line);
		to += line;
		from += bytesPerLine;
	}
	return cache;
#endif // USE_OPENGL_OVERLAY_WIDGET

	return result;
}

bool OverlayWidget::documentContentShown() const {
	return _document && (!_staticContent.isNull() || videoShown());
}

bool OverlayWidget::documentBubbleShown() const {
	return (!_photo && !_document)
		|| (_document
			&& !_themePreviewShown
			&& !_streamed
			&& _staticContent.isNull());
}

void OverlayWidget::clearStreaming(bool savePosition) {
	if (_streamed && _document && savePosition) {
		Media::Player::SaveLastPlaybackPosition(
			_document,
			_streamed->instance.player().prepareLegacyState());
	}
	_fullScreenVideo = false;
	_streamed = nullptr;
}

void OverlayWidget::documentUpdated(DocumentData *doc) {
	if (_document && _document == doc) {
		if (documentBubbleShown()) {
			if ((_document->loading() && _docCancel->isHidden()) || (!_document->loading() && !_docCancel->isHidden())) {
				updateControls();
			} else if (_document->loading()) {
				updateDocSize();
				update(_docRect);
			}
		} else if (_streamed) {
			const auto ready = _documentMedia->loaded()
				? _document->size
				: _document->loading()
				? std::clamp(_document->loadOffset(), 0, _document->size)
				: 0;
			_streamed->controls.setLoadingProgress(ready, _document->size);
		}
	}
}

void OverlayWidget::changingMsgId(not_null<HistoryItem*> row, MsgId oldId) {
	if (FullMsgId(row->channelId(), oldId) == _msgid) {
		_msgid = row->fullId();
		refreshMediaViewer();
	}
}

void OverlayWidget::updateDocSize() {
	if (!_document || !documentBubbleShown()) {
		return;
	}

	const auto size = _document->size;
	_docSize = _document->loading()
		? Ui::FormatProgressText(_document->loadOffset(), size)
		: Ui::FormatSizeText(size);
	_docSizeWidth = st::mediaviewFont->width(_docSize);
	int32 maxw = st::mediaviewFileSize.width() - st::mediaviewFileIconSize - st::mediaviewFilePadding * 3;
	if (_docSizeWidth > maxw) {
		_docSize = st::mediaviewFont->elided(_docSize, maxw);
		_docSizeWidth = st::mediaviewFont->width(_docSize);
	}
}

void OverlayWidget::refreshNavVisibility() {
	if (_sharedMediaData) {
		_leftNavVisible = _index && (*_index > 0);
		_rightNavVisible = _index && (*_index + 1 < _sharedMediaData->size());
	} else if (_userPhotosData) {
		_leftNavVisible = _index && (*_index > 0);
		_rightNavVisible = _index && (*_index + 1 < _userPhotosData->size());
	} else if (_collageData) {
		_leftNavVisible = _index && (*_index > 0);
		_rightNavVisible = _index && (*_index + 1 < _collageData->items.size());
	} else {
		_leftNavVisible = false;
		_rightNavVisible = false;
	}
}

bool OverlayWidget::contentCanBeSaved() const {
	if (_photo) {
		return _photo->hasVideo() || _photoMedia->loaded();
	} else if (_document) {
		return _document->filepath(true).isEmpty() && !_document->loading();
	} else {
		return false;
	}
}

void OverlayWidget::checkForSaveLoaded() {
	if (_savePhotoVideoWhenLoaded == SavePhotoVideo::None) {
		return;
	} else if (!_photo
		|| !_photo->hasVideo()
		|| _photoMedia->videoContent().isEmpty()) {
		return;
	} else if (_savePhotoVideoWhenLoaded == SavePhotoVideo::QuickSave) {
		_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
		onDownload();
	} else if (_savePhotoVideoWhenLoaded == SavePhotoVideo::SaveAs) {
		_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
		onSaveAs();
	} else {
		Unexpected("SavePhotoVideo in OverlayWidget::checkForSaveLoaded.");
	}
}

void OverlayWidget::updateControls() {
	if (_document && documentBubbleShown()) {
		if (_document->loading()) {
			_docDownload->hide();
			_docSaveAs->hide();
			_docCancel->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
			_docCancel->show();
		} else {
			if (_documentMedia->loaded(true)) {
				_docDownload->hide();
				_docSaveAs->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
				_docSaveAs->show();
				_docCancel->hide();
			} else {
				_docDownload->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
				_docDownload->show();
				_docSaveAs->moveToLeft(_docRect.x() + 2.5 * st::mediaviewFilePadding + st::mediaviewFileIconSize + _docDownload->width(), _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
				_docSaveAs->show();
				_docCancel->hide();
			}
		}
		updateDocSize();
	} else {
		_docDownload->hide();
		_docSaveAs->hide();
		_docCancel->hide();
	}
	radialStart();

	updateThemePreviewGeometry();

	_saveVisible = contentCanBeSaved();
	_rotateVisible = !_themePreviewShown;
	const auto navRect = [&](int i) {
		return myrtlrect(width() - st::mediaviewIconSize.width() * i,
			height() - st::mediaviewIconSize.height(),
			st::mediaviewIconSize.width(),
			st::mediaviewIconSize.height());
	};
	_saveNav = navRect(_rotateVisible ? 3 : 2);
	_saveNavIcon = style::centerrect(_saveNav, st::mediaviewSave);
	_rotateNav = navRect(2);
	_rotateNavIcon = style::centerrect(_rotateNav, st::mediaviewRotate);
	_moreNav = navRect(1);
	_moreNavIcon = style::centerrect(_moreNav, st::mediaviewMore);

	const auto dNow = QDateTime::currentDateTime();
	const auto d = [&] {
		if (!_session) {
			return dNow;
		} else if (const auto item = _session->data().message(_msgid)) {
			return ItemDateTime(item);
		} else if (_photo) {
			return base::unixtime::parse(_photo->date);
		} else if (_document) {
			return base::unixtime::parse(_document->date);
		}
		return dNow;
	}();
	_dateText = Ui::FormatDateTime(d, cTimeFormat());
	if (!_fromName.isEmpty()) {
		_fromNameLabel.setText(st::mediaviewTextStyle, _fromName, Ui::NameTextOptions());
		_nameNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, qMin(_fromNameLabel.maxWidth(), width() / 3), st::mediaviewFont->height);
		_dateNav = myrtlrect(st::mediaviewTextLeft + _nameNav.width() + st::mediaviewTextSkip, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	}
	updateHeader();
	refreshNavVisibility();
	resizeCenteredControls();

	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

void OverlayWidget::resizeCenteredControls() {
	const auto bottomSkip = std::max(
		_dateNav.left() + _dateNav.width(),
		_headerNav.left() + _headerNav.width())
		+ st::mediaviewCaptionMargin.width();
	_groupThumbsAvailableWidth = std::max(
		width() - 2 * bottomSkip,
		st::msgMinWidth
		+ st::mediaviewCaptionPadding.left()
		+ st::mediaviewCaptionPadding.right());
	_groupThumbsLeft = (width() - _groupThumbsAvailableWidth) / 2;
	refreshGroupThumbs();
	_groupThumbsTop = _groupThumbs ? (height() - _groupThumbs->height()) : 0;

	refreshClipControllerGeometry();
	refreshCaptionGeometry();
}

void OverlayWidget::refreshCaptionGeometry() {
	if (_caption.isEmpty()) {
		_captionRect = QRect();
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto captionBottom = (_streamed && !videoIsGifOrUserpic())
		? (_streamed->controls.y() - st::mediaviewCaptionMargin.height())
		: _groupThumbs
		? _groupThumbsTop
		: height() - st::mediaviewCaptionMargin.height();
	const auto captionWidth = std::min(
		_groupThumbsAvailableWidth
		- st::mediaviewCaptionPadding.left()
		- st::mediaviewCaptionPadding.right(),
		_caption.maxWidth());
	const auto captionHeight = std::min(
		_caption.countHeight(captionWidth),
		height() / 4
		- st::mediaviewCaptionPadding.top()
		- st::mediaviewCaptionPadding.bottom()
		- 2 * st::mediaviewCaptionMargin.height());
	_captionRect = QRect(
		(width() - captionWidth) / 2,
		captionBottom
		- captionHeight
		- st::mediaviewCaptionPadding.bottom(),
		captionWidth,
		captionHeight);
}

void OverlayWidget::updateActions() {
	_actions.clear();

	if (_document && _document->loading()) {
		_actions.push_back({ tr::lng_cancel(tr::now), SLOT(onSaveCancel()) });
	}
	if (IsServerMsgId(_msgid.msg)) {
		_actions.push_back({ tr::lng_context_to_msg(tr::now), SLOT(onToMessage()) });
	}
	if (_document && !_document->filepath(true).isEmpty()) {
		_actions.push_back({ Platform::IsMac() ? tr::lng_context_show_in_finder(tr::now) : tr::lng_context_show_in_folder(tr::now), SLOT(onShowInFolder()) });
	}
	if ((_document && documentContentShown()) || (_photo && _photoMedia->loaded())) {
		_actions.push_back({ tr::lng_mediaview_copy(tr::now), SLOT(onCopy()) });
	}
	if ((_photo && _photo->hasAttachedStickers())
		|| (_document && _document->hasAttachedStickers())) {
		auto member = _photo
			? SLOT(onPhotoAttachedStickers())
			: SLOT(onDocumentAttachedStickers());
		_actions.push_back({
			tr::lng_context_attached_stickers(tr::now),
			std::move(member)
		});
	}
	if (_canForwardItem) {
		_actions.push_back({ tr::lng_mediaview_forward(tr::now), SLOT(onForward()) });
	}
	const auto canDelete = [&] {
		if (_canDeleteItem) {
			return true;
		} else if (!_msgid
			&& _photo
			&& _user
			&& _user == _user->session().user()) {
			return _userPhotosData && _fullIndex && _fullCount;
		} else if (_photo && _photo->peer && _photo->peer->userpicPhotoId() == _photo->id) {
			if (auto chat = _photo->peer->asChat()) {
				return chat->canEditInformation();
			} else if (auto channel = _photo->peer->asChannel()) {
				return channel->canEditInformation();
			}
		}
		return false;
	}();
	if (canDelete) {
		_actions.push_back({ tr::lng_mediaview_delete(tr::now), SLOT(onDelete()) });
	}
	_actions.push_back({ tr::lng_mediaview_save_as(tr::now), SLOT(onSaveAs()) });

	if (const auto overviewType = computeOverviewType()) {
		_actions.push_back({ _document ? tr::lng_mediaview_files_all(tr::now) : tr::lng_mediaview_photos_all(tr::now), SLOT(onOverview()) });
	}
}

auto OverlayWidget::computeOverviewType() const
-> std::optional<SharedMediaType> {
	if (const auto mediaType = sharedMediaType()) {
		if (const auto overviewType = SharedMediaOverviewType(*mediaType)) {
			return overviewType;
		} else if (mediaType == SharedMediaType::PhotoVideo) {
			if (_photo) {
				return SharedMediaOverviewType(SharedMediaType::Photo);
			} else if (_document) {
				return SharedMediaOverviewType(SharedMediaType::Video);
			}
		}
	}
	return std::nullopt;
}

bool OverlayWidget::stateAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::mediaviewShowDuration + st::mediaviewHideDuration;
	}
	for (auto i = begin(_animations); i != end(_animations);) {
		const auto [state, started] = *i;
		updateOverRect(state);
		const auto dt = float64(now - started) / st::mediaviewFadeDuration;
		if (dt >= 1) {
			_animationOpacities.erase(state);
			i = _animations.erase(i);
		} else {
			_animationOpacities[state].update(dt, anim::linear);
			++i;
		}
	}
	return !_animations.empty() || updateControlsAnimation(now);
}

bool OverlayWidget::updateControlsAnimation(crl::time now) {
	if (_controlsState != ControlsShowing
		&& _controlsState != ControlsHiding) {
		return false;
	}
	const auto duration = (_controlsState == ControlsShowing)
		? st::mediaviewShowDuration
		: st::mediaviewHideDuration;
	const auto dt = float64(now - _controlsAnimStarted)
		/ duration;
	if (dt >= 1) {
		_controlsOpacity.finish();
		_controlsState = (_controlsState == ControlsShowing)
			? ControlsShown
			: ControlsHidden;
		updateCursor();
	} else {
		_controlsOpacity.update(dt, anim::linear);
	}
	const auto toUpdate = QRegion()
		+ (_over == OverLeftNav ? _leftNav : _leftNavIcon)
		+ (_over == OverRightNav ? _rightNav : _rightNavIcon)
		+ (_over == OverClose ? _closeNav : _closeNavIcon)
		+ _saveNavIcon
		+ _rotateNavIcon
		+ _moreNavIcon
		+ _headerNav
		+ _nameNav
		+ _dateNav
		+ _captionRect.marginsAdded(st::mediaviewCaptionPadding)
		+ _groupThumbsRect;
	update(toUpdate);
	return (dt < 1);
}

void OverlayWidget::waitingAnimationCallback() {
	if (!anim::Disabled()) {
		update(radialRect());
	}
}

void OverlayWidget::updateCursor() {
	setCursor(_controlsState == ControlsHidden
		? Qt::BlankCursor
		: (_over == OverNone ? style::cur_default : style::cur_pointer));
}

int OverlayWidget::contentRotation() const {
	if (!_streamed) {
		return _rotation;
	}
	return (_rotation + (_streamed
		? _streamed->instance.info().video.rotation
		: 0)) % 360;
}

QRect OverlayWidget::contentRect() const {
	return { _x, _y, _w, _h };
}

void OverlayWidget::contentSizeChanged() {
	_width = _w;
	_height = _h;
	resizeContentByScreenSize();
}

void OverlayWidget::resizeContentByScreenSize() {
	const auto bottom = (!_streamed || videoIsGifOrUserpic())
		? height()
		: (_streamed->controls.y()
			- st::mediaviewCaptionPadding.bottom()
			- st::mediaviewCaptionMargin.height());
	const auto skipWidth = 0;
	const auto skipHeight = (height() - bottom);
	const auto availableWidth = width();
	const auto availableHeight = height() - 2 * skipHeight;
	const auto countZoomFor = [&](int outerw, int outerh) {
		auto result = float64(outerw) / _width;
		if (_height * result > outerh) {
			result = float64(outerh) / _height;
		}
		if (result >= 1.) {
			result -= 1.;
		} else {
			result = 1. - (1. / result);
		}
		return result;
	};
	if (_width > 0 && _height > 0) {
		_zoomToDefault = countZoomFor(availableWidth, availableHeight);
		_zoomToScreen = countZoomFor(width(), height());
	} else {
		_zoomToDefault = _zoomToScreen = 0;
	}
	const auto usew = _fullScreenVideo ? width() : availableWidth;
	const auto useh = _fullScreenVideo ? height() : availableHeight;
	if ((_width > usew) || (_height > useh) || _fullScreenVideo) {
		const auto use = _fullScreenVideo ? _zoomToScreen : _zoomToDefault;
		_zoom = kZoomToScreenLevel;
		if (use >= 0) {
			_w = qRound(_width * (use + 1));
			_h = qRound(_height * (use + 1));
		} else {
			_w = qRound(_width / (-use + 1));
			_h = qRound(_height / (-use + 1));
		}
	} else {
		_zoom = 0;
		_w = _width;
		_h = _height;
	}
	_x = (width() - _w) / 2;
	_y = (height() - _h) / 2;
}

float64 OverlayWidget::radialProgress() const {
	if (_document) {
		return _documentMedia->progress();
	} else if (_photo) {
		return _photoMedia->progress();
	}
	return 1.;
}

bool OverlayWidget::radialLoading() const {
	if (_streamed) {
		return false;
	} else if (_document) {
		return _document->loading();
	} else if (_photo) {
		return _photo->displayLoading();
	}
	return false;
}

QRect OverlayWidget::radialRect() const {
	if (_photo) {
		return _photoRadialRect;
	} else if (_document) {
		return QRect(
			QPoint(
				_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2),
				_docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)),
			st::radialSize);
	}
	return QRect();
}

void OverlayWidget::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (auto shift = radialTimeShift()) {
			_radial.update(radialProgress(), !radialLoading(), crl::now() + shift);
		}
	}
}

crl::time OverlayWidget::radialTimeShift() const {
	return _photo ? st::radialDuration : 0;
}

bool OverlayWidget::radialAnimationCallback(crl::time now) {
	if ((!_document && !_photo) || _streamed) {
		return false;
	}
	const auto wasAnimating = _radial.animating();
	const auto updated = _radial.update(
		radialProgress(),
		!radialLoading(),
		now + radialTimeShift());
	if ((wasAnimating || _radial.animating())
		&& (!anim::Disabled() || updated)) {
		update(radialRect());
	}
	const auto ready = _document && _documentMedia->loaded();
	const auto streamVideo = ready && _documentMedia->canBePlayed();
	const auto tryOpenImage = ready && (_document->size < App::kImageSizeLimit);
	if (ready && ((tryOpenImage && !_radial.animating()) || streamVideo)) {
		_streamingStartPaused = false;
		if (streamVideo) {
			redisplayContent();
		} else {
			auto &location = _document->location(true);
			if (location.accessEnable()) {
				if (_document->isTheme()
					|| QImageReader(location.name()).canRead()) {
					redisplayContent();
				}
				location.accessDisable();
			}
		}
	}
	return true;
}

void OverlayWidget::zoomIn() {
	auto newZoom = _zoom;
	const auto full = _fullScreenVideo ? _zoomToScreen : _zoomToDefault;
	if (newZoom == kZoomToScreenLevel) {
		if (qCeil(full) <= kMaxZoomLevel) {
			newZoom = qCeil(full);
		}
	} else {
		if (newZoom < full && (newZoom + 1 > full || (full > kMaxZoomLevel && newZoom == kMaxZoomLevel))) {
			newZoom = kZoomToScreenLevel;
		} else if (newZoom < kMaxZoomLevel) {
			++newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void OverlayWidget::zoomOut() {
	auto newZoom = _zoom;
	const auto full = _fullScreenVideo ? _zoomToScreen : _zoomToDefault;
	if (newZoom == kZoomToScreenLevel) {
		if (qFloor(full) >= -kMaxZoomLevel) {
			newZoom = qFloor(full);
		}
	} else {
		if (newZoom > full && (newZoom - 1 < full || (full < -kMaxZoomLevel && newZoom == -kMaxZoomLevel))) {
			newZoom = kZoomToScreenLevel;
		} else if (newZoom > -kMaxZoomLevel) {
			--newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void OverlayWidget::zoomReset() {
	auto newZoom = _zoom;
	const auto full = _fullScreenVideo ? _zoomToScreen : _zoomToDefault;
	if (_zoom == 0) {
		if (qFloor(full) == qCeil(full) && qRound(full) >= -kMaxZoomLevel && qRound(full) <= kMaxZoomLevel) {
			newZoom = qRound(full);
		} else {
			newZoom = kZoomToScreenLevel;
		}
	} else {
		newZoom = 0;
	}
	_x = -_width / 2;
	_y = -_height / 2;
	float64 z = (_zoom == kZoomToScreenLevel) ? full : _zoom;
	if (z >= 0) {
		_x = qRound(_x * (z + 1));
		_y = qRound(_y * (z + 1));
	} else {
		_x = qRound(_x / (-z + 1));
		_y = qRound(_y / (-z + 1));
	}
	_x += width() / 2;
	_y += height() / 2;
	update();
	zoomUpdate(newZoom);
}

void OverlayWidget::zoomUpdate(int32 &newZoom) {
	if (newZoom != kZoomToScreenLevel) {
		while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h) {
			++newZoom;
		}
	}
	setZoomLevel(newZoom);
}

void OverlayWidget::clearSession() {
	if (!isHidden()) {
		hide();
	}
	_sessionLifetime.destroy();
	if (!_animations.empty()) {
		_animations.clear();
		_stateAnimation.stop();
	}
	if (!_animationOpacities.empty()) {
		_animationOpacities.clear();
	}
	clearStreaming();
	delete _menu;
	_menu = nullptr;
	setContext(v::null);
	_from = nullptr;
	_fromName = QString();
	assignMediaPointer(nullptr);
	_fullScreenVideo = false;
	_caption.clear();
	_sharedMedia = nullptr;
	_userPhotos = nullptr;
	_collage = nullptr;
	_session = nullptr;
}

OverlayWidget::~OverlayWidget() {
	clearSession();
}

void OverlayWidget::assignMediaPointer(DocumentData *document) {
	_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
	_photo = nullptr;
	_photoMedia = nullptr;
	if (_document != document) {
		if ((_document = document)) {
			_documentMedia = _document->createMediaView();
			_documentMedia->goodThumbnailWanted();
			_documentMedia->thumbnailWanted(fileOrigin());
		} else {
			_documentMedia = nullptr;
		}
	}
}

void OverlayWidget::assignMediaPointer(not_null<PhotoData*> photo) {
	_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
	_document = nullptr;
	_documentMedia = nullptr;
	if (_photo != photo) {
		_photo = photo;
		_photoMedia = _photo->createMediaView();
		_photoMedia->wanted(Data::PhotoSize::Small, fileOrigin());
		if (!_photo->hasVideo() || _photo->videoPlaybackFailed()) {
			_photo->load(fileOrigin(), LoadFromCloudOrLocal, true);
		}
	}
}

void OverlayWidget::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	setCursor((active || ClickHandler::getPressed()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void OverlayWidget::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	setCursor((pressed || ClickHandler::getActive()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void OverlayWidget::showSaveMsgFile() {
	File::ShowInFolder(_saveMsgFilename);
}

void OverlayWidget::updateMixerVideoVolume() const {
	if (_streamed) {
		Player::mixer()->setVideoVolume(Core::App().settings().videoVolume());
	}
}

void OverlayWidget::close() {
	Core::App().hideMediaView();
}

void OverlayWidget::activateControls() {
	if (!_menu && !_mousePressed) {
		_controlsHideTimer.start(int(st::mediaviewWaitHide));
	}
	if (_fullScreenVideo) {
		if (_streamed) {
			_streamed->controls.showAnimated();
		}
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) {
		_controlsState = ControlsShowing;
		_controlsAnimStarted = crl::now();
		_controlsOpacity.start(1);
		if (!_stateAnimation.animating()) {
			_stateAnimation.start();
		}
	}
}

void OverlayWidget::onHideControls(bool force) {
	if (!force) {
		if (!_dropdown->isHidden()
			|| _menu
			|| _mousePressed
			|| (_fullScreenVideo
				&& !videoIsGifOrUserpic()
				&& _streamed->controls.geometry().contains(_lastMouseMovePos))) {
			return;
		}
	}
	if (_fullScreenVideo) {
		_streamed->controls.hideAnimated();
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;

	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	_controlsState = ControlsHiding;
	_controlsAnimStarted = crl::now();
	_controlsOpacity.start(0);
	if (!_stateAnimation.animating()) {
		_stateAnimation.start();
	}
}

void OverlayWidget::dropdownHidden() {
	setFocus();
	_ignoringDropdown = true;
	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	updateOver(_lastMouseMovePos);
	_ignoringDropdown = false;
	if (!_controlsHideTimer.isActive()) {
		onHideControls(true);
	}
}

void OverlayWidget::onScreenResized(int screen) {
	if (isHidden()) {
		return;
	}

	const auto screens = QApplication::screens();
	const auto changed = (screen >= 0 && screen < screens.size())
		? screens[screen]
		: nullptr;
	if (windowHandle()
		&& windowHandle()->screen()
		&& changed
		&& windowHandle()->screen() == changed) {
		updateGeometry();
	}
}

void OverlayWidget::handleVisibleChanged(bool visible) {
	if (visible) {
		moveToScreen();
	}
}

void OverlayWidget::handleScreenChanged(QScreen *screen) {
	if (isVisible()) {
		moveToScreen();
	}
}

void OverlayWidget::onToMessage() {
	if (!_session) {
		return;
	}
	if (const auto item = _session->data().message(_msgid)) {
		close();
		Ui::showPeerHistoryAtItem(item);
	}
}

void OverlayWidget::notifyFileDialogShown(bool shown) {
	if (shown && isHidden()) {
		return;
	}
	if (shown) {
		Ui::Platform::BringToBack(this);
	} else {
		Ui::Platform::ShowOverAll(this);
	}
}

void OverlayWidget::onSaveAs() {
	QString file;
	if (_document) {
		const auto &location = _document->location(true);
		const auto bytes = _documentMedia->bytes();
		if (!bytes.isEmpty() || location.accessEnable()) {
			QFileInfo alreadyInfo(location.name());
			QDir alreadyDir(alreadyInfo.dir());
			QString name = alreadyInfo.fileName(), filter;
			const auto mimeType = Core::MimeTypeForName(_document->mimeString());
			QStringList p = mimeType.globPatterns();
			QString pattern = p.isEmpty() ? QString() : p.front();
			if (name.isEmpty()) {
				name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
			}

			if (pattern.isEmpty()) {
				filter = QString();
			} else {
				filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
			}

			file = FileNameForSave(
				_session,
				tr::lng_save_file(tr::now),
				filter,
				qsl("doc"),
				name,
				true,
				alreadyDir);
			if (!file.isEmpty() && file != location.name()) {
				if (bytes.isEmpty()) {
					QFile(file).remove();
					QFile(location.name()).copy(file);
				} else {
					QFile f(file);
					f.open(QIODevice::WriteOnly);
					f.write(bytes);
				}
			}

			if (bytes.isEmpty()) {
				location.accessDisable();
			}
		} else {
			DocumentSaveClickHandler::Save(
				fileOrigin(),
				_document,
				DocumentSaveClickHandler::Mode::ToNewFile);
			updateControls();
			updateOver(_lastMouseMovePos);
		}
	} else if (_photo && _photo->hasVideo()) {
		if (const auto bytes = _photoMedia->videoContent(); !bytes.isEmpty()) {
			auto filter = qsl("Video Files (*.mp4);;") + FileDialog::AllFilesFilter();
			FileDialog::GetWritePath(
				this,
				tr::lng_save_video(tr::now),
				filter,
				filedialogDefaultName(
					qsl("photo"),
					qsl(".mp4"),
					QString(),
					false,
					_photo->date),
				crl::guard(this, [=, photo = _photo](const QString &result) {
					QFile f(result);
					if (!result.isEmpty()
						&& _photo == photo
						&& f.open(QIODevice::WriteOnly)) {
						f.write(bytes);
					}
				}));
		} else {
			_photo->loadVideo(fileOrigin());
			_savePhotoVideoWhenLoaded = SavePhotoVideo::SaveAs;
		}
	} else {
		if (!_photo || !_photoMedia->loaded()) {
			return;
		}

		const auto image = _photoMedia->image(Data::PhotoSize::Large)->original();
		auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
		FileDialog::GetWritePath(
			this,
			tr::lng_save_photo(tr::now),
			filter,
			filedialogDefaultName(
				qsl("photo"),
				qsl(".jpg"),
				QString(),
				false,
				_photo->date),
			crl::guard(this, [=, photo = _photo](const QString &result) {
				if (!result.isEmpty() && _photo == photo) {
					image.save(result, "JPG");
				}
			}));
	}
	activateWindow();
	QApplication::setActiveWindow(this);
	setFocus();
}

void OverlayWidget::onDocClick() {
	if (_document->loading()) {
		onSaveCancel();
	} else {
		DocumentOpenClickHandler::Open(
			fileOrigin(),
			_document,
			_document->owner().message(_msgid));
		if (_document->loading() && !_radial.animating()) {
			_radial.start(_documentMedia->progress());
		}
	}
}

PeerData *OverlayWidget::ui_getPeerForMouseAction() {
	return _history ? _history->peer.get() : nullptr;
}

void OverlayWidget::onDownload() {
	if (!_photo && !_document) {
		return;
	}
	if (Core::App().settings().askDownloadPath()) {
		return onSaveAs();
	}

	QString path;
	const auto session = _photo ? &_photo->session() : &_document->session();
	if (Core::App().settings().downloadPath().isEmpty()) {
		path = File::DefaultDownloadPath(session);
	} else if (Core::App().settings().downloadPath() == qsl("tmp")) {
		path = session->local().tempDirectory();
	} else {
		path = Core::App().settings().downloadPath();
	}
	QString toName;
	if (_document) {
		const auto &location = _document->location(true);
		if (location.accessEnable()) {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogNextFilename(
				_document->filename(),
				location.name(),
				path);
			if (!toName.isEmpty() && toName != location.name()) {
				QFile(toName).remove();
				if (!QFile(location.name()).copy(toName)) {
					toName = QString();
				}
			}
			location.accessDisable();
		} else {
			if (_document->filepath(true).isEmpty()
				&& !_document->loading()) {
				DocumentSaveClickHandler::Save(
					fileOrigin(),
					_document,
					DocumentSaveClickHandler::Mode::ToFile);
				updateControls();
			} else {
				_saveVisible = contentCanBeSaved();
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		}
	} else if (_photo && _photo->hasVideo()) {
		if (const auto bytes = _photoMedia->videoContent(); !bytes.isEmpty()) {
			if (!QDir().exists(path)) {
				QDir().mkpath(path);
			}
			toName = filedialogDefaultName(qsl("photo"), qsl(".mp4"), path);
			QFile f(toName);
			if (!f.open(QIODevice::WriteOnly)
				|| f.write(bytes) != bytes.size()) {
				toName = QString();
			}
		} else {
			_photo->loadVideo(fileOrigin());
			_savePhotoVideoWhenLoaded = SavePhotoVideo::QuickSave;
		}
	} else {
		if (!_photo || !_photoMedia->loaded()) {
			_saveVisible = contentCanBeSaved();
			update(_saveNav);
		} else {
			const auto image = _photoMedia->image(
				Data::PhotoSize::Large)->original();

			if (!QDir().exists(path)) {
				QDir().mkpath(path);
			}
			toName = filedialogDefaultName(qsl("photo"), qsl(".jpg"), path);
			if (!image.save(toName, "JPG")) {
				toName = QString();
			}
		}
	}
	if (!toName.isEmpty()) {
		_saveMsgFilename = toName;
		_saveMsgStarted = crl::now();
		_saveMsgOpacity.start(1);
		updateImage();
	}
}

void OverlayWidget::onSaveCancel() {
	if (_document && _document->loading()) {
		_document->cancel();
		if (_documentMedia->canBePlayed()) {
			redisplayContent();
		}
	}
}

void OverlayWidget::onShowInFolder() {
	if (!_document) return;

	auto filepath = _document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
		close();
	}
}

void OverlayWidget::onForward() {
	if (!_session) {
		return;
	}
	const auto &active = _session->windows();
	if (active.empty()) {
		return;
	}
	const auto item = _session->data().message(_msgid);
	if (!item || !IsServerMsgId(item->id) || item->serviceMsg()) {
		return;
	}

	close();
	Window::ShowForwardMessagesBox(
		active.front(),
		{ 1, item->fullId() });
}

void OverlayWidget::onDelete() {
	if (!_session) {
		return;
	}

	const auto session = _session;
	const auto photo = _photo;
	const auto msgid = _msgid;
	const auto deletingPeerPhoto = [&] {
		if (!_msgid) {
			return true;
		} else if (_photo && _history) {
			if (_history->peer->userpicPhotoId() == _photo->id) {
				return _firstOpenedPeerPhoto;
			}
		}
		return false;
	}();
	close();

	Core::App().domain().activate(&session->account());
	const auto &active = session->windows();
	if (active.empty()) {
		return;
	}
	if (deletingPeerPhoto) {
		active.front()->content()->deletePhotoLayer(photo);
	} else if (const auto item = session->data().message(msgid)) {
		const auto suggestModerateActions = true;
		Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
	}
}

void OverlayWidget::onOverview() {
	if (_menu) _menu->hideMenu(true);
	update();
	if (const auto overviewType = computeOverviewType()) {
		close();
		SharedMediaShowOverview(*overviewType, _history);
	}
}

void OverlayWidget::onCopy() {
	_dropdown->hideAnimated(Ui::DropdownMenu::HideOption::IgnoreShow);
	if (_document) {
		QGuiApplication::clipboard()->setImage(videoShown()
			? transformVideoFrame(videoFrame())
			: transformStaticContent(_staticContent));
	} else if (_photo && _photoMedia->loaded()) {
		const auto image = _photoMedia->image(
			Data::PhotoSize::Large)->original();
		QGuiApplication::clipboard()->setImage(image);
	}
}

void OverlayWidget::onPhotoAttachedStickers() {
	if (!_session || !_photo) {
		return;
	}
	const auto &active = _session->windows();
	if (active.empty()) {
		return;
	}
	_session->api().attachedStickers().requestAttachedStickerSets(
		active.front(),
		_photo);
	close();
}

void OverlayWidget::onDocumentAttachedStickers() {
	if (!_session || !_document) {
		return;
	}
	const auto &active = _session->windows();
	if (active.empty()) {
		return;
	}
	_session->api().attachedStickers().requestAttachedStickerSets(
		active.front(),
		_document);
	close();
}

auto OverlayWidget::sharedMediaType() const
-> std::optional<SharedMediaType> {
	using Type = SharedMediaType;
	if (!_session) {
		return std::nullopt;
	} else if (const auto item = _session->data().message(_msgid)) {
		if (const auto media = item->media()) {
			if (media->webpage()) {
				return std::nullopt;
			}
		}
		if (_photo) {
			if (item->toHistoryMessage()) {
				return Type::PhotoVideo;
			}
			return Type::ChatPhoto;
		} else if (_document) {
			if (_document->isGifv()) {
				return Type::GIF;
			} else if (_document->isVideoFile()) {
				return Type::PhotoVideo;
			}
			return Type::File;
		}
	}
	return std::nullopt;
}

auto OverlayWidget::sharedMediaKey() const -> std::optional<SharedMediaKey> {
	if (!_msgid
		&& _peer
		&& !_user
		&& _photo
		&& _peer->userpicPhotoId() == _photo->id) {
		return SharedMediaKey {
			_history->peer->id,
			_migrated ? _migrated->peer->id : 0,
			SharedMediaType::ChatPhoto,
			_photo
		};
	}
	if (!IsServerMsgId(_msgid.msg)) {
		return std::nullopt;
	}
	auto keyForType = [this](SharedMediaType type) -> SharedMediaKey {
		return {
			_history->peer->id,
			_migrated ? _migrated->peer->id : 0,
			type,
			(_msgid.channel == _history->channelId()) ? _msgid.msg : (_msgid.msg - ServerMaxMsgId) };
	};
	return
		sharedMediaType()
		| keyForType;
}

Data::FileOrigin OverlayWidget::fileOrigin() const {
	if (_msgid) {
		return _msgid;
	} else if (_photo && _user) {
		return Data::FileOriginUserPhoto(_user->bareId(), _photo->id);
	} else if (_photo && _peer && _peer->userpicPhotoId() == _photo->id) {
		return Data::FileOriginPeerPhoto(_peer->id);
	}
	return Data::FileOrigin();
}

Data::FileOrigin OverlayWidget::fileOrigin(const Entity &entity) const {
	if (const auto item = entity.item) {
		return item->fullId();
	} else if (!v::is<not_null<PhotoData*>>(entity.data)) {
		return Data::FileOrigin();
	}
	const auto photo = v::get<not_null<PhotoData*>>(entity.data);
	if (_user) {
		return Data::FileOriginUserPhoto(_user->bareId(), photo->id);
	} else if (_peer && _peer->userpicPhotoId() == photo->id) {
		return Data::FileOriginPeerPhoto(_peer->id);
	}
	return Data::FileOrigin();
}

bool OverlayWidget::validSharedMedia() const {
	if (auto key = sharedMediaKey()) {
		if (!_sharedMedia) {
			return false;
		}
		using Key = SharedMediaWithLastSlice::Key;
		auto inSameDomain = [](const Key &a, const Key &b) {
			return (a.type == b.type)
				&& (a.peerId == b.peerId)
				&& (a.migratedPeerId == b.migratedPeerId);
		};
		auto countDistanceInData = [&](const Key &a, const Key &b) {
			return [&](const SharedMediaWithLastSlice &data) {
				return inSameDomain(a, b)
					? data.distance(a, b)
					: std::optional<int>();
			};
		};

		if (key == _sharedMedia->key) {
			return true;
		} else if (!_sharedMediaDataKey
			|| _sharedMedia->key != *_sharedMediaDataKey) {
			return false;
		}
		auto distance = _sharedMediaData
			| countDistanceInData(*key, _sharedMedia->key)
			| func::abs;
		if (distance) {
			return (*distance < kIdsPreloadAfter);
		}
	}
	return (_sharedMedia == nullptr);
}

void OverlayWidget::validateSharedMedia() {
	if (const auto key = sharedMediaKey()) {
		Assert(_history != nullptr);

		_sharedMedia = std::make_unique<SharedMedia>(*key);
		auto viewer = (key->type == SharedMediaType::ChatPhoto)
			? SharedMediaWithLastReversedViewer
			: SharedMediaWithLastViewer;
		viewer(
			&_history->session(),
			*key,
			kIdsLimit,
			kIdsLimit
		) | rpl::start_with_next([this](
				SharedMediaWithLastSlice &&update) {
			handleSharedMediaUpdate(std::move(update));
		}, _sharedMedia->lifetime);
	} else {
		_sharedMedia = nullptr;
		_sharedMediaData = std::nullopt;
		_sharedMediaDataKey = std::nullopt;
	}
}

void OverlayWidget::handleSharedMediaUpdate(SharedMediaWithLastSlice &&update) {
	if ((!_photo && !_document) || !_sharedMedia) {
		_sharedMediaData = std::nullopt;
		_sharedMediaDataKey = std::nullopt;
	} else {
		_sharedMediaData = std::move(update);
		_sharedMediaDataKey = _sharedMedia->key;
	}
	findCurrent();
	updateControls();
	preloadData(0);
}

std::optional<OverlayWidget::UserPhotosKey> OverlayWidget::userPhotosKey() const {
	if (!_msgid && _user && _photo) {
		return UserPhotosKey {
			_user->bareId(),
			_photo->id
		};
	}
	return std::nullopt;
}

bool OverlayWidget::validUserPhotos() const {
	if (const auto key = userPhotosKey()) {
		if (!_userPhotos) {
			return false;
		}
		const auto countDistanceInData = [](const auto &a, const auto &b) {
			return [&](const UserPhotosSlice &data) {
				return data.distance(a, b);
			};
		};

		const auto distance = (key == _userPhotos->key) ? 0 :
			_userPhotosData
			| countDistanceInData(*key, _userPhotos->key)
			| func::abs;
		if (distance) {
			return (*distance < kIdsPreloadAfter);
		}
	}
	return (_userPhotos == nullptr);
}

void OverlayWidget::validateUserPhotos() {
	if (const auto key = userPhotosKey()) {
		Assert(_user != nullptr);

		_userPhotos = std::make_unique<UserPhotos>(*key);
		UserPhotosReversedViewer(
			&_user->session(),
			*key,
			kIdsLimit,
			kIdsLimit
		) | rpl::start_with_next([this](
				UserPhotosSlice &&update) {
			handleUserPhotosUpdate(std::move(update));
		}, _userPhotos->lifetime);
	} else {
		_userPhotos = nullptr;
		_userPhotosData = std::nullopt;
	}
}

void OverlayWidget::handleUserPhotosUpdate(UserPhotosSlice &&update) {
	if (!_photo || !_userPhotos) {
		_userPhotosData = std::nullopt;
	} else {
		_userPhotosData = std::move(update);
	}
	findCurrent();
	updateControls();
	preloadData(0);
}

std::optional<OverlayWidget::CollageKey> OverlayWidget::collageKey() const {
	if (!_session) {
		return std::nullopt;
	} else if (const auto item = _session->data().message(_msgid)) {
		if (const auto media = item->media()) {
			if (const auto page = media->webpage()) {
				for (const auto &item : page->collage.items) {
					if (item == _photo || item == _document) {
						return item;
					}
				}
			}
		}
	}
	return std::nullopt;
}

bool OverlayWidget::validCollage() const {
	if (const auto key = collageKey()) {
		if (!_collage) {
			return false;
		}
		const auto countDistanceInData = [](const auto &a, const auto &b) {
			return [&](const WebPageCollage &data) {
				const auto i = ranges::find(data.items, a);
				const auto j = ranges::find(data.items, b);
				return (i != end(data.items) && j != end(data.items))
					? std::make_optional(i - j)
					: std::nullopt;
			};
		};

		if (key == _collage->key) {
			return true;
		} else if (_collageData) {
			const auto &items = _collageData->items;
			if (ranges::find(items, *key) != end(items)
				&& ranges::find(items, _collage->key) != end(items)) {
				return true;
			}
		}
	}
	return (_collage == nullptr);
}

void OverlayWidget::validateCollage() {
	if (const auto key = collageKey()) {
		_collage = std::make_unique<Collage>(*key);
		_collageData = WebPageCollage();
		if (const auto item = _session->data().message(_msgid)) {
			if (const auto media = item->media()) {
				if (const auto page = media->webpage()) {
					_collageData = page->collage;
				}
			}
		}
	} else {
		_collage = nullptr;
		_collageData = std::nullopt;
	}
}

void OverlayWidget::refreshMediaViewer() {
	if (!validSharedMedia()) {
		validateSharedMedia();
	}
	if (!validUserPhotos()) {
		validateUserPhotos();
	}
	if (!validCollage()) {
		validateCollage();
	}
	findCurrent();
	updateControls();
}

void OverlayWidget::refreshFromLabel(HistoryItem *item) {
	if (_msgid && item) {
		_from = item->senderOriginal();
		if (const auto info = item->hiddenForwardedInfo()) {
			_fromName = info->name;
		} else {
			Assert(_from != nullptr);
			const auto from = _from->migrateTo() ? _from->migrateTo() : _from;
			_fromName = from->name;
		}
	} else {
		_from = _user;
		_fromName = _user ? _user->name : QString();
	}
}

void OverlayWidget::refreshCaption(HistoryItem *item) {
	_caption = Ui::Text::String();
	if (!item) {
		return;
	} else if (const auto media = item->media()) {
		if (media->webpage()) {
			return;
		}
	}
	const auto caption = item->originalText();
	if (caption.text.isEmpty()) {
		return;
	}
	const auto asBot = [&] {
		if (const auto author = item->author()->asUser()) {
			return author->isBot();
		}
		return false;
	}();

	using namespace HistoryView;
	_caption = Ui::Text::String(st::msgMinWidth);
	const auto duration = (_streamed && _document && !videoIsGifOrUserpic())
		? _document->getDuration()
		: 0;
	const auto base = duration
		? DocumentTimestampLinkBase(_document, item->fullId())
		: QString();
	const auto context = Core::MarkedTextContext{
		.session = &item->history()->session()
	};
	_caption.setMarkedText(
		st::mediaviewCaptionStyle,
		AddTimestampLinks(caption, duration, base),
		Ui::ItemTextOptions(item),
		context);
}

void OverlayWidget::refreshGroupThumbs() {
	const auto existed = (_groupThumbs != nullptr);
	if (_index && _sharedMediaData) {
		View::GroupThumbs::Refresh(
			_session,
			_groupThumbs,
			*_sharedMediaData,
			*_index,
			_groupThumbsAvailableWidth);
	} else if (_index && _userPhotosData) {
		View::GroupThumbs::Refresh(
			_session,
			_groupThumbs,
			*_userPhotosData,
			*_index,
			_groupThumbsAvailableWidth);
	} else if (_index && _collageData) {
		View::GroupThumbs::Refresh(
			_session,
			_groupThumbs,
			{ _msgid, &*_collageData },
			*_index,
			_groupThumbsAvailableWidth);
	} else if (_groupThumbs) {
		_groupThumbs->clear();
		_groupThumbs->resizeToWidth(_groupThumbsAvailableWidth);
	}
	if (_groupThumbs && !existed) {
		initGroupThumbs();
	}
}

void OverlayWidget::initGroupThumbs() {
	Expects(_groupThumbs != nullptr);

	_groupThumbs->updateRequests(
	) | rpl::start_with_next([this](QRect rect) {
		const auto shift = (width() / 2);
		_groupThumbsRect = QRect(
			shift + rect.x(),
			_groupThumbsTop,
			rect.width(),
			_groupThumbs->height());
		update(_groupThumbsRect);
	}, _groupThumbs->lifetime());

	_groupThumbs->activateRequests(
	) | rpl::start_with_next([this](View::GroupThumbs::Key key) {
		using CollageKey = View::GroupThumbs::CollageKey;
		if (const auto photoId = std::get_if<PhotoId>(&key)) {
			const auto photo = _session->data().photo(*photoId);
			moveToEntity({ photo, nullptr });
		} else if (const auto itemId = std::get_if<FullMsgId>(&key)) {
			moveToEntity(entityForItemId(*itemId));
		} else if (const auto collageKey = std::get_if<CollageKey>(&key)) {
			if (_collageData) {
				moveToEntity(entityForCollage(collageKey->index));
			}
		}
	}, _groupThumbs->lifetime());

	_groupThumbsRect = QRect(
		_groupThumbsLeft,
		_groupThumbsTop,
		width() - 2 * _groupThumbsLeft,
		height() - _groupThumbsTop);
}

void OverlayWidget::clearControlsState() {
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = _down = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.empty()) {
		_animations.clear();
		_stateAnimation.stop();
	}
	if (!_animationOpacities.empty()) {
		_animationOpacities.clear();
	}
}

void OverlayWidget::showPhoto(
		not_null<PhotoData*> photo,
		HistoryItem *context) {
	setSession(&photo->session());

	if (context) {
		setContext(context);
	} else {
		setContext(v::null);
	}

	clearControlsState();
	_firstOpenedPeerPhoto = false;
	assignMediaPointer(photo);

	displayPhoto(photo, context);
	preloadData(0);
	activateControls();
}

void OverlayWidget::showPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> context) {
	setSession(&photo->session());
	setContext(context);

	clearControlsState();
	_firstOpenedPeerPhoto = true;
	assignMediaPointer(photo);

	displayPhoto(photo, nullptr);
	preloadData(0);
	activateControls();
}

void OverlayWidget::showDocument(
		not_null<DocumentData*> document,
		HistoryItem *context) {
	showDocument(document, context, Data::CloudTheme(), false);
}

void OverlayWidget::showTheme(
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloud) {
	showDocument(document, nullptr, cloud, false);
}

void OverlayWidget::showDocument(
		not_null<DocumentData*> document,
		HistoryItem *context,
		const Data::CloudTheme &cloud,
		bool continueStreaming) {
	setSession(&document->session());

	if (context) {
		setContext(context);
	} else {
		setContext(v::null);
	}

	clearControlsState();

	_streamingStartPaused = false;
	displayDocument(document, context, cloud, continueStreaming);
	preloadData(0);
	activateControls();
}

void OverlayWidget::displayPhoto(not_null<PhotoData*> photo, HistoryItem *item) {
	if (photo->isNull()) {
		displayDocument(nullptr, item);
		return;
	}
	_touchbarDisplay.fire(TouchBarItemType::Photo);

	clearStreaming();
	destroyThemePreview();

	_fullScreenVideo = false;
	assignMediaPointer(photo);
	_rotation = _photo->owner().mediaRotation().get(_photo);
	_radial.stop();

	refreshMediaViewer();

	_staticContent = QPixmap();
	if (_photo->videoCanBePlayed()) {
		initStreaming();
	}

	refreshCaption(item);

	_blurred = true;
	_down = OverNone;
	if (!_staticContent.isNull()) {
		// Video thumbnail.
		const auto size = style::ConvertScale(
			flipSizeByRotation(_staticContent.size()));
		_w = size.width();
		_h = size.height();
	} else {
		const auto size = style::ConvertScale(flipSizeByRotation(QSize(
			photo->width(),
			photo->height())));
		_w = size.width();
		_h = size.height();
	}
	contentSizeChanged();
	refreshFromLabel(item);
	displayFinished();
}

void OverlayWidget::destroyThemePreview() {
	_themePreviewId = 0;
	_themePreviewShown = false;
	_themePreview.reset();
	_themeApply.destroy();
	_themeCancel.destroy();
	_themeShare.destroy();
}

void OverlayWidget::redisplayContent() {
	if (isHidden() || !_session) {
		return;
	}
	const auto item = _session->data().message(_msgid);
	if (_photo) {
		displayPhoto(_photo, item);
	} else {
		displayDocument(_document, item);
	}
}

// Empty messages shown as docs: doc can be nullptr.
void OverlayWidget::displayDocument(
		DocumentData *doc,
		HistoryItem *item,
		const Data::CloudTheme &cloud,
		bool continueStreaming) {
	_fullScreenVideo = false;
	_staticContent = QPixmap();
	clearStreaming(_document != doc);
	destroyThemePreview();
	assignMediaPointer(doc);

	_rotation = _document ? _document->owner().mediaRotation().get(_document) : 0;
	_themeCloudData = cloud;
	_radial.stop();

	_touchbarDisplay.fire(TouchBarItemType::None);

	refreshMediaViewer();
	if (_document) {
		if (_document->sticker()) {
			if (const auto image = _documentMedia->getStickerLarge()) {
				_staticContent = image->pix();
			} else if (const auto thumbnail = _documentMedia->thumbnail()) {
				_staticContent = thumbnail->pixBlurred(
					_document->dimensions.width(),
					_document->dimensions.height());
			}
		} else {
			if (_documentMedia->canBePlayed()
				&& initStreaming(continueStreaming)) {
			} else if (_document->isVideoFile()) {
				_documentMedia->automaticLoad(fileOrigin(), item);
				initStreamingThumbnail();
			} else if (_document->isTheme()) {
				_documentMedia->automaticLoad(fileOrigin(), item);
				initThemePreview();
			} else {
				_documentMedia->automaticLoad(fileOrigin(), item);
				_document->saveFromDataSilent();
				auto &location = _document->location(true);
				if (location.accessEnable()) {
					const auto &path = location.name();
					if (QImageReader(path).canRead()) {
						_staticContent = PrepareStaticImage(path);
						_touchbarDisplay.fire(TouchBarItemType::Photo);
					}
				}
				location.accessDisable();
			}
		}
	}
	refreshCaption(item);

	_docIconRect = QRect((width() - st::mediaviewFileIconSize) / 2, (height() - st::mediaviewFileIconSize) / 2, st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	if (documentBubbleShown()) {
		if (!_document || !_document->hasThumbnail()) {
			int32 colorIndex = documentColorIndex(_document, _docExt);
			_docIconColor = documentColor(colorIndex);
			const style::icon *(thumbs[]) = { &st::mediaviewFileBlue, &st::mediaviewFileGreen, &st::mediaviewFileRed, &st::mediaviewFileYellow };
			_docIcon = thumbs[colorIndex];

			int32 extmaxw = (st::mediaviewFileIconSize - st::mediaviewFileExtPadding * 2);
			_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
			if (_docExtWidth > extmaxw) {
				_docExt = st::mediaviewFileExtFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
				_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
			}
		} else {
			_document->loadThumbnail(fileOrigin());
			const auto tw = _documentMedia->thumbnailSize().width();
			const auto th = _documentMedia->thumbnailSize().height();
			if (!tw || !th) {
				_docThumbx = _docThumby = _docThumbw = 0;
			} else if (tw > th) {
				_docThumbw = (tw * st::mediaviewFileIconSize) / th;
				_docThumbx = (_docThumbw - st::mediaviewFileIconSize) / 2;
				_docThumby = 0;
			} else {
				_docThumbw = st::mediaviewFileIconSize;
				_docThumbx = 0;
				_docThumby = ((th * _docThumbw) / tw - st::mediaviewFileIconSize) / 2;
			}
		}

		int32 maxw = st::mediaviewFileSize.width() - st::mediaviewFileIconSize - st::mediaviewFilePadding * 3;

		if (_document) {
			_docName = (_document->type == StickerDocument)
				? tr::lng_in_dlg_sticker(tr::now)
				: (_document->type == AnimatedDocument
					? qsl("GIF")
					: (_document->filename().isEmpty()
						? tr::lng_mediaview_doc_image(tr::now)
						: _document->filename()));
		} else {
			_docName = tr::lng_message_empty(tr::now);
		}
		_docNameWidth = st::mediaviewFileNameFont->width(_docName);
		if (_docNameWidth > maxw) {
			_docName = st::mediaviewFileNameFont->elided(_docName, maxw, Qt::ElideMiddle);
			_docNameWidth = st::mediaviewFileNameFont->width(_docName);
		}

		// _docSize is updated in updateControls()

		_docRect = QRect((width() - st::mediaviewFileSize.width()) / 2, (height() - st::mediaviewFileSize.height()) / 2, st::mediaviewFileSize.width(), st::mediaviewFileSize.height());
		_docIconRect = myrtlrect(_docRect.x() + st::mediaviewFilePadding, _docRect.y() + st::mediaviewFilePadding, st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	} else if (_themePreviewShown) {
		updateThemePreviewGeometry();
	} else if (!_staticContent.isNull()) {
		_staticContent.setDevicePixelRatio(cRetinaFactor());
		const auto size = style::ConvertScale(
			flipSizeByRotation(_staticContent.size()));
		_w = size.width();
		_h = size.height();
	} else if (videoShown()) {
		const auto contentSize = style::ConvertScale(videoSize());
		_w = contentSize.width();
		_h = contentSize.height();
	}
	contentSizeChanged();
	refreshFromLabel(item);
	_blurred = false;
	displayFinished();
}

void OverlayWidget::updateThemePreviewGeometry() {
	if (_themePreviewShown) {
		auto previewRect = QRect((width() - st::themePreviewSize.width()) / 2, (height() - st::themePreviewSize.height()) / 2, st::themePreviewSize.width(), st::themePreviewSize.height());
		_themePreviewRect = previewRect.marginsAdded(st::themePreviewMargin);
		if (_themeApply) {
			auto right = qMax(width() - _themePreviewRect.x() - _themePreviewRect.width(), 0) + st::themePreviewMargin.right();
			auto bottom = qMin(height(), _themePreviewRect.y() + _themePreviewRect.height());
			_themeApply->moveToRight(right, bottom - st::themePreviewMargin.bottom() + (st::themePreviewMargin.bottom() - _themeApply->height()) / 2);
			right += _themeApply->width() + st::themePreviewButtonsSkip;
			_themeCancel->moveToRight(right, _themeApply->y());
			if (_themeShare) {
				_themeShare->moveToLeft(previewRect.x(), _themeApply->y());
			}
		}

		// For context menu event.
		_x = _themePreviewRect.x();
		_y = _themePreviewRect.y();
		_w = _themePreviewRect.width();
		_h = _themePreviewRect.height();
	}
}

void OverlayWidget::displayFinished() {
	updateControls();
	if (isHidden()) {
		Ui::Platform::UpdateOverlayed(this);
		if (Platform::IsLinux()) {
			showFullScreen();
		} else {
			show();
		}
		Ui::Platform::ShowOverAll(this);
		activateWindow();
		QApplication::setActiveWindow(this);
		setFocus();
	}
}

bool OverlayWidget::canInitStreaming() const {
	return (_document && _documentMedia->canBePlayed())
		|| (_photo && _photo->videoCanBePlayed());
}

bool OverlayWidget::initStreaming(bool continueStreaming) {
	Expects(canInitStreaming());

	if (_streamed) {
		return true;
	}
	initStreamingThumbnail();
	if (!createStreamingObjects()) {
		if (_document) {
			_document->setInappPlaybackFailed();
		} else {
			_photo->setVideoPlaybackFailed();
		}
		return false;
	}

	Core::App().updateNonIdle();

	_streamed->instance.player().updates(
	) | rpl::start_with_next_error([=](Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Streaming::Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->instance.lifetime());

	if (continueStreaming) {
		_pip = nullptr;
	}
	if (!continueStreaming
		|| (!_streamed->instance.player().active()
			&& !_streamed->instance.player().finished())) {
		startStreamingPlayer();
	} else {
		updatePlaybackState();
	}
	return true;
}

void OverlayWidget::startStreamingPlayer() {
	Expects(_streamed != nullptr);

	if (!_streamed->instance.player().paused()
		&& !_streamed->instance.player().finished()
		&& !_streamed->instance.player().failed()) {
		if (!_streamed->withSound) {
			return;
		}
		_pip = nullptr;
	} else if (_pip && _streamed->withSound) {
		return;
	}

	const auto position = _document
		? _document->session().settings().mediaLastPlaybackPosition(_document->id)
		: _photo
		? _photo->videoStartPosition()
		: 0;
	restartAtSeekPosition(position);
}

void OverlayWidget::initStreamingThumbnail() {
	Expects(_photo || _document);

	_touchbarDisplay.fire(TouchBarItemType::Video);

	const auto computePhotoThumbnail = [&] {
		const auto thumbnail = _photoMedia->image(Data::PhotoSize::Thumbnail);
		if (thumbnail) {
			return thumbnail;
		} else if (_peer && _peer->userpicPhotoId() == _photo->id) {
			if (const auto view = _peer->activeUserpicView()) {
				if (const auto image = view->image()) {
					return image;
				}
			}
		}
		return thumbnail;
	};
	const auto good = _document
		? _documentMedia->goodThumbnail()
		: _photoMedia->image(Data::PhotoSize::Large);
	const auto thumbnail = _document
		? _documentMedia->thumbnail()
		: computePhotoThumbnail();
	const auto blurred = _document
		? _documentMedia->thumbnailInline()
		: _photoMedia->thumbnailInline();
	const auto size = _photo
		? QSize(
			_photo->videoLocation().width(),
			_photo->videoLocation().height())
		: good
		? good->size()
		: _document->dimensions;
	if (!good && !thumbnail && !blurred) {
		return;
	} else if (size.isEmpty()) {
		return;
	}
	const auto w = size.width();
	const auto h = size.height();
	const auto options = VideoThumbOptions(_document);
	const auto goodOptions = (options & ~Images::Option::Blurred);
	_staticContent = (good
		? good
		: thumbnail
		? thumbnail
		: blurred
		? blurred
		: Image::BlankMedia().get())->pixNoCache(
			w,
			h,
			good ? goodOptions : options,
			w / cIntRetinaFactor(),
			h / cIntRetinaFactor());
	_staticContent.setDevicePixelRatio(cRetinaFactor());
}

void OverlayWidget::streamingReady(Streaming::Information &&info) {
	if (videoShown()) {
		applyVideoSize();
	}
	update(contentRect());
}

void OverlayWidget::applyVideoSize() {
	const auto contentSize = style::ConvertScale(videoSize());
	if (contentSize != QSize(_width, _height)) {
		update(contentRect());
		_w = contentSize.width();
		_h = contentSize.height();
		contentSizeChanged();
	}
}

bool OverlayWidget::createStreamingObjects() {
	Expects(_photo || _document);

	if (_document) {
		_streamed = std::make_unique<Streamed>(
			_document,
			fileOrigin(),
			this,
			static_cast<PlaybackControls::Delegate*>(this),
			[=] { waitingAnimationCallback(); });
	} else {
		_streamed = std::make_unique<Streamed>(
			_photo,
			fileOrigin(),
			this,
			static_cast<PlaybackControls::Delegate*>(this),
			[=] { waitingAnimationCallback(); });
	}
	if (!_streamed->instance.valid()) {
		_streamed = nullptr;
		return false;
	}
	_streamed->instance.setPriority(kOverlayLoaderPriority);
	_streamed->instance.lockPlayer();
	_streamed->withSound = _document
		&& (_document->isAudioFile()
			|| _document->isVideoFile()
			|| _document->isVoiceMessage()
			|| _document->isVideoMessage());

	if (videoIsGifOrUserpic()) {
		_streamed->controls.hide();
	} else {
		refreshClipControllerGeometry();
		_streamed->controls.show();
	}
	return true;
}

QImage OverlayWidget::transformVideoFrame(QImage frame) const {
	Expects(videoShown());

	const auto rotation = contentRotation();
	if (rotation != 0) {
		frame = RotateFrameImage(std::move(frame), rotation);
	}
	const auto requiredSize = videoSize();
	if (frame.size() != requiredSize) {
		frame = frame.scaled(
			requiredSize,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	return frame;
}

QImage OverlayWidget::transformStaticContent(QPixmap content) const {
	return _rotation
		? RotateFrameImage(content.toImage(), _rotation)
		: content.toImage();
}

void OverlayWidget::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
		updatePlaybackState();
	}, [&](const UpdateVideo &update) {
		this->update(contentRect());
		Core::App().updateNonIdle();
		updatePlaybackState();
	}, [&](const PreloadedAudio &update) {
		updatePlaybackState();
	}, [&](const UpdateAudio &update) {
		updatePlaybackState();
	}, [&](WaitingForData) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		updatePlaybackState();
	});
}

void OverlayWidget::handleStreamingError(Streaming::Error &&error) {
	Expects(_document || _photo);

	if (error == Streaming::Error::NotStreamable) {
		if (_document) {
			_document->setNotSupportsStreaming();
		} else {
			_photo->setVideoPlaybackFailed();
		}
	} else if (error == Streaming::Error::OpenFailed) {
		if (_document) {
			_document->setInappPlaybackFailed();
		} else {
			_photo->setVideoPlaybackFailed();
		}
	}
	if (canInitStreaming()) {
		updatePlaybackState();
	} else {
		redisplayContent();
	}
}

void OverlayWidget::initThemePreview() {
	using namespace Window::Theme;

	Assert(_document && _document->isTheme());

	const auto bytes = _documentMedia->bytes();
	auto &location = _document->location();
	if (bytes.isEmpty()
		&& (location.isEmpty() || !location.accessEnable())) {
		return;
	}
	_themePreviewShown = true;

	auto current = CurrentData();
	current.backgroundId = Background()->id();
	current.backgroundImage = Background()->createCurrentImage();
	current.backgroundTiled = Background()->tile();

	const auto &cloudList = _document->session().data().cloudThemes().list();
	const auto i = ranges::find(
		cloudList,
		_document->id,
		&Data::CloudTheme::documentId);
	const auto cloud = (i != end(cloudList)) ? *i : Data::CloudTheme();
	const auto isTrusted = (cloud.documentId != 0);
	const auto fields = [&] {
		auto result = _themeCloudData.id ? _themeCloudData : cloud;
		if (!result.documentId) {
			result.documentId = _document->id;
		}
		return result;
	}();

	const auto weakSession = base::make_weak(&_document->session());
	const auto path = _document->location().name();
	const auto id = _themePreviewId = rand_value<uint64>();
	const auto weak = Ui::MakeWeak(this);
	crl::async([=, data = std::move(current)]() mutable {
		auto preview = GeneratePreview(
			bytes,
			path,
			fields,
			std::move(data),
			Window::Theme::PreviewType::Extended);
		crl::on_main(weak, [=, result = std::move(preview)]() mutable {
			const auto session = weakSession.get();
			if (id != _themePreviewId || !session) {
				return;
			}
			_themePreviewId = 0;
			_themePreview = std::move(result);
			if (_themePreview) {
				_themeApply.create(
					this,
					tr::lng_theme_preview_apply(),
					st::themePreviewApplyButton);
				_themeApply->show();
				_themeApply->setClickedCallback([=] {
					const auto &object = Background()->themeObject();
					const auto currentlyIsCustom = !object.cloud.id
						&& !IsEmbeddedTheme(object.pathAbsolute);
					auto preview = std::move(_themePreview);
					close();
					Apply(std::move(preview));
					if (isTrusted && !currentlyIsCustom) {
						KeepApplied();
					}
				});
				_themeCancel.create(
					this,
					tr::lng_cancel(),
					st::themePreviewCancelButton);
				_themeCancel->show();
				_themeCancel->setClickedCallback([this] { close(); });
				if (const auto slug = _themeCloudData.slug; !slug.isEmpty()) {
					_themeShare.create(
						this,
						tr::lng_theme_share(),
						st::themePreviewCancelButton);
					_themeShare->show();
					_themeShare->setClickedCallback([=] {
						QGuiApplication::clipboard()->setText(
							session->createInternalLinkFull("addtheme/" + slug));
						Ui::Toast::Show(
							this,
							tr::lng_background_link_copied(tr::now));
					});
				} else {
					_themeShare.destroy();
				}
				updateControls();
			}
			update();
		});
	});
	location.accessDisable();
}

void OverlayWidget::refreshClipControllerGeometry() {
	if (!_streamed || videoIsGifOrUserpic()) {
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto controllerBottom = _groupThumbs
		? _groupThumbsTop
		: height();
	_streamed->controls.resize(st::mediaviewControllerSize);
	_streamed->controls.move(
		(width() - _streamed->controls.width()) / 2,
		controllerBottom - _streamed->controls.height() - st::mediaviewCaptionPadding.bottom() - st::mediaviewCaptionMargin.height());
	Ui::SendPendingMoveResizeEvents(&_streamed->controls);
}

void OverlayWidget::playbackControlsPlay() {
	playbackPauseResume();
}

void OverlayWidget::playbackControlsPause() {
	playbackPauseResume();
}

void OverlayWidget::playbackControlsToFullScreen() {
	playbackToggleFullScreen();
}

void OverlayWidget::playbackControlsFromFullScreen() {
	playbackToggleFullScreen();
}

void OverlayWidget::playbackControlsToPictureInPicture() {
	if (!videoIsGifOrUserpic()) {
		switchToPip();
	}
}

void OverlayWidget::playbackControlsRotate() {
	if (_photo) {
		auto &storage = _photo->owner().mediaRotation();
		storage.set(_photo, storage.get(_photo) - 90);
		_rotation = storage.get(_photo);
		redisplayContent();
	} else if (_document) {
		auto &storage = _document->owner().mediaRotation();
		storage.set(_document, storage.get(_document) - 90);
		_rotation = storage.get(_document);
		if (videoShown()) {
			applyVideoSize();
			update(contentRect());
		} else {
			redisplayContent();
		}
	}
}

void OverlayWidget::playbackPauseResume() {
	Expects(_streamed != nullptr);

	_streamed->resumeOnCallEnd = false;
	if (_streamed->instance.player().failed()) {
		clearStreaming();
		if (!canInitStreaming() || !initStreaming()) {
			redisplayContent();
		}
	} else if (_streamed->instance.player().finished()
		|| !_streamed->instance.player().active()) {
		_streamingStartPaused = false;
		restartAtSeekPosition(0);
	} else if (_streamed->instance.player().paused()) {
		_streamed->instance.resume();
		updatePlaybackState();
		playbackPauseMusic();
	} else {
		_streamed->instance.pause();
		updatePlaybackState();
	}
}

void OverlayWidget::restartAtSeekPosition(crl::time position) {
	Expects(_streamed != nullptr);

	if (videoShown()) {
		_streamed->instance.saveFrameToCover();
		const auto saved = base::take(_rotation);
		_staticContent = Images::PixmapFast(transformVideoFrame(videoFrame()));
		_rotation = saved;
		update(contentRect());
	}
	auto options = Streaming::PlaybackOptions();
	options.position = position;
	if (!_streamed->withSound) {
		options.mode = Streaming::Mode::Video;
		options.loop = true;
	} else {
		Assert(_document != nullptr);
		options.audioId = AudioMsgId(_document, _msgid);
		options.speed = Core::App().settings().videoPlaybackSpeed();
		if (_pip) {
			_pip = nullptr;
		}
	}
	_streamed->instance.play(options);
	if (_streamingStartPaused) {
		_streamed->instance.pause();
	} else {
		playbackPauseMusic();
	}
	_streamed->pausedBySeek = false;

	updatePlaybackState();
}

void OverlayWidget::playbackControlsSeekProgress(crl::time position) {
	Expects(_streamed != nullptr);

	if (!_streamed->instance.player().paused()
		&& !_streamed->instance.player().finished()) {
		_streamed->pausedBySeek = true;
		playbackControlsPause();
	}
}

void OverlayWidget::playbackControlsSeekFinished(crl::time position) {
	Expects(_streamed != nullptr);

	_streamingStartPaused = !_streamed->pausedBySeek
		&& !_streamed->instance.player().finished();
	restartAtSeekPosition(position);
}

void OverlayWidget::playbackControlsVolumeChanged(float64 volume) {
	Core::App().settings().setVideoVolume(volume);
	Core::App().saveSettingsDelayed();
	updateMixerVideoVolume();
}

float64 OverlayWidget::playbackControlsCurrentVolume() {
	return Core::App().settings().videoVolume();
}

void OverlayWidget::playbackControlsVolumeToggled() {
	const auto volume = Core::App().settings().videoVolume();
	playbackControlsVolumeChanged(volume ? 0. : _lastPositiveVolume);
}

void OverlayWidget::playbackControlsVolumeChangeFinished() {
	const auto volume = Core::App().settings().videoVolume();
	if (volume > 0.) {
		_lastPositiveVolume = volume;
	}
}

void OverlayWidget::playbackControlsSpeedChanged(float64 speed) {
	DEBUG_LOG(("Media playback speed: change to %1.").arg(speed));
	if (_document) {
		DEBUG_LOG(("Media playback speed: %1 to settings.").arg(speed));
		Core::App().settings().setVideoPlaybackSpeed(speed);
		Core::App().saveSettingsDelayed();
	}
	if (_streamed && !videoIsGifOrUserpic()) {
		DEBUG_LOG(("Media playback speed: %1 to _streamed.").arg(speed));
		_streamed->instance.setSpeed(speed);
	}
}

float64 OverlayWidget::playbackControlsCurrentSpeed() {
	const auto result = Core::App().settings().videoPlaybackSpeed();
	DEBUG_LOG(("Media playback speed: now %1.").arg(result));
	return result;
}

void OverlayWidget::switchToPip() {
	Expects(_streamed != nullptr);
	Expects(_document != nullptr);

	const auto document = _document;
	const auto msgId = _msgid;
	const auto closeAndContinue = [=] {
		showDocument(document, document->owner().message(msgId), {}, true);
	};
	_pip = std::make_unique<PipWrap>(
		this,
		document,
		msgId,
		_streamed->instance.shared(),
		closeAndContinue,
		[=] { _pip = nullptr; });
	close();
	if (const auto window = Core::App().activeWindow()) {
		window->activate();
	}
}

void OverlayWidget::playbackToggleFullScreen() {
	Expects(_streamed != nullptr);

	if (!videoShown() || (videoIsGifOrUserpic() && !_fullScreenVideo)) {
		return;
	}
	_fullScreenVideo = !_fullScreenVideo;
	if (_fullScreenVideo) {
		_fullScreenZoomCache = _zoom;
		setZoomLevel(kZoomToScreenLevel, true);
	} else {
		setZoomLevel(_fullScreenZoomCache, true);
		_streamed->controls.showAnimated();
	}

	_streamed->controls.setInFullScreen(_fullScreenVideo);
	_touchbarFullscreenToggled.fire_copy(_fullScreenVideo);
	updateControls();
	update();
}

void OverlayWidget::playbackPauseOnCall() {
	Expects(_streamed != nullptr);

	if (_streamed->instance.player().finished()
		|| _streamed->instance.player().paused()) {
		return;
	}
	_streamed->resumeOnCallEnd = true;
	_streamed->instance.pause();
	updatePlaybackState();
}

void OverlayWidget::playbackResumeOnCall() {
	Expects(_streamed != nullptr);

	if (_streamed->resumeOnCallEnd) {
		_streamed->resumeOnCallEnd = false;
		_streamed->instance.resume();
		updatePlaybackState();
		playbackPauseMusic();
	}
}

void OverlayWidget::playbackPauseMusic() {
	Expects(_streamed != nullptr);

	if (!_streamed->withSound) {
		return;
	}
	Player::instance()->pause(AudioMsgId::Type::Voice);
	Player::instance()->pause(AudioMsgId::Type::Song);
}

void OverlayWidget::updatePlaybackState() {
	Expects(_streamed != nullptr);

	if (videoIsGifOrUserpic()) {
		return;
	}
	const auto state = _streamed->instance.player().prepareLegacyState();
	if (state.position != kTimeUnknown && state.length != kTimeUnknown) {
		_streamed->controls.updatePlayback(state);
		_touchbarTrackState.fire_copy(state);
	}
}

void OverlayWidget::validatePhotoImage(Image *image, bool blurred) {
	if (!image) {
		return;
	} else if (!_staticContent.isNull() && (blurred || !_blurred)) {
		return;
	}
	const auto use = flipSizeByRotation({ _width, _height })
		* cIntRetinaFactor();
	_staticContent = image->pixNoCache(
		use.width(),
		use.height(),
		Images::Option::Smooth
		| (blurred ? Images::Option::Blurred : Images::Option(0)));
	_staticContent.setDevicePixelRatio(cRetinaFactor());
	_blurred = blurred;
}

void OverlayWidget::validatePhotoCurrentImage() {
	validatePhotoImage(_photoMedia->image(Data::PhotoSize::Large), false);
	validatePhotoImage(_photoMedia->image(Data::PhotoSize::Thumbnail), true);
	validatePhotoImage(_photoMedia->image(Data::PhotoSize::Small), true);
	validatePhotoImage(_photoMedia->thumbnailInline(), true);
	if (_staticContent.isNull()
		&& !_msgid
		&& _peer
		&& _peer->hasUserpic()) {
		if (const auto view = _peer->activeUserpicView()) {
			validatePhotoImage(view->image(), true);
		}
	}
	if (_staticContent.isNull()) {
		_photoMedia->wanted(Data::PhotoSize::Small, fileOrigin());
	}
}

void OverlayWidget::paintEvent(QPaintEvent *e) {
	const auto r = e->rect();
	const auto region = e->region();
	const auto contentShown = _photo || documentContentShown();
	const auto bgRegion = contentShown
		? (region - contentRect())
		: region;

	auto ms = crl::now();

	Painter p(this);

	bool name = false;

	p.setClipRegion(region);

	// main bg
	const auto m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	const auto bgColor = _fullScreenVideo ? st::mediaviewVideoBg : st::mediaviewBg;
	for (const auto rect : bgRegion) {
		p.fillRect(rect, bgColor);
	}
	p.setCompositionMode(m);

	// photo
	if (_photo) {
		validatePhotoCurrentImage();
	}
	p.setOpacity(1);
	if (contentShown) {
		const auto rect = contentRect();
		if (rect.intersects(r)) {
			if (videoShown()) {
				paintTransformedVideoFrame(p);
			} else {
				paintTransformedStaticContent(p);
			}

			const auto radial = _radial.animating();
			const auto radialOpacity = radial ? _radial.opacity() : 0.;
			paintRadialLoading(p, radial, radialOpacity);
		}
		if (_saveMsgStarted && _saveMsg.intersects(r)) {
			float64 dt = float64(ms) - _saveMsgStarted, hidingDt = dt - st::mediaviewSaveMsgShowing - st::mediaviewSaveMsgShown;
			if (dt < st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + st::mediaviewSaveMsgHiding) {
				if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5) {
					_saveMsgOpacity.start(0);
				}
				float64 progress = (hidingDt >= 0) ? (hidingDt / st::mediaviewSaveMsgHiding) : (dt / st::mediaviewSaveMsgShowing);
				_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
				if (_saveMsgOpacity.current() > 0) {
					p.setOpacity(_saveMsgOpacity.current());
					Ui::FillRoundRect(p, _saveMsg, st::mediaviewSaveMsgBg, Ui::MediaviewSaveCorners);
					st::mediaviewSaveMsgCheck.paint(p, _saveMsg.topLeft() + st::mediaviewSaveMsgCheckPos, width());

					p.setPen(st::mediaviewSaveMsgFg);
					p.setTextPalette(st::mediaviewTextPalette);
					_saveMsgText.draw(p, _saveMsg.x() + st::mediaviewSaveMsgPadding.left(), _saveMsg.y() + st::mediaviewSaveMsgPadding.top(), _saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
					p.restoreTextPalette();
					p.setOpacity(1);
				}
				if (!_blurred) {
					auto nextFrame = (dt < st::mediaviewSaveMsgShowing || hidingDt >= 0) ? int(AnimationTimerDelta) : (st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + 1 - dt);
					_saveMsgUpdater.start(nextFrame);
				}
			} else {
				_saveMsgStarted = 0;
			}
		}
	} else if (_themePreviewShown) {
		paintThemePreview(p, r);
	} else if (documentBubbleShown()) {
		if (_docRect.intersects(r)) {
			p.fillRect(_docRect, st::mediaviewFileBg);
			if (_docIconRect.intersects(r)) {
				const auto radial = _radial.animating();
				const auto radialOpacity = radial ? _radial.opacity() : 0.;
				if (!_document || !_document->hasThumbnail()) {
					p.fillRect(_docIconRect, _docIconColor);
					if ((!_document || _documentMedia->loaded()) && (!radial || radialOpacity < 1) && _docIcon) {
						_docIcon->paint(p, _docIconRect.x() + (_docIconRect.width() - _docIcon->width()), _docIconRect.y(), width());
						p.setPen(st::mediaviewFileExtFg);
						p.setFont(st::mediaviewFileExtFont);
						if (!_docExt.isEmpty()) {
							p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2, _docIconRect.y() + st::mediaviewFileExtTop + st::mediaviewFileExtFont->ascent, _docExt);
						}
					}
				} else if (const auto thumbnail = _documentMedia->thumbnail()) {
					int32 rf(cIntRetinaFactor());
					p.drawPixmap(_docIconRect.topLeft(), thumbnail->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mediaviewFileIconSize * rf, st::mediaviewFileIconSize * rf));
				}

				paintRadialLoading(p, radial, radialOpacity);
			}

			if (!_docIconRect.contains(r)) {
				name = true;
				p.setPen(st::mediaviewFileNameFg);
				p.setFont(st::mediaviewFileNameFont);
				p.drawTextLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileNameTop, width(), _docName, _docNameWidth);

				p.setPen(st::mediaviewFileSizeFg);
				p.setFont(st::mediaviewFont);
				p.drawTextLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileSizeTop, width(), _docSize, _docSizeWidth);
			}
		}
	}

	float64 co = _fullScreenVideo ? 0. : _controlsOpacity.current();
	if (co > 0) {
		// left nav bar
		if (_leftNav.intersects(r) && _leftNavVisible) {
			auto o = overLevel(OverLeftNav);
			if (o > 0) {
				p.setOpacity(o * co);
				for (const auto &rect : region) {
					const auto fill = _leftNav.intersected(rect);
					if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
				}
			}
			if (_leftNavIcon.intersects(r)) {
				p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
				st::mediaviewLeft.paintInCenter(p, _leftNavIcon);
			}
		}

		// right nav bar
		if (_rightNav.intersects(r) && _rightNavVisible) {
			auto o = overLevel(OverRightNav);
			if (o > 0) {
				p.setOpacity(o * co);
				for (const auto &rect : region) {
					const auto fill = _rightNav.intersected(rect);
					if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
				}
			}
			if (_rightNavIcon.intersects(r)) {
				p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
				st::mediaviewRight.paintInCenter(p, _rightNavIcon);
			}
		}

		// close button
		if (_closeNav.intersects(r)) {
			auto o = overLevel(OverClose);
			if (o > 0) {
				p.setOpacity(o * co);
				for (const auto &rect : region) {
					const auto fill = _closeNav.intersected(rect);
					if (!fill.isEmpty()) p.fillRect(fill, st::mediaviewControlBg);
				}
			}
			if (_closeNavIcon.intersects(r)) {
				p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
				st::mediaviewClose.paintInCenter(p, _closeNavIcon);
			}
		}

		// save button
		if (_saveVisible && _saveNavIcon.intersects(r)) {
			auto o = overLevel(OverSave);
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			st::mediaviewSave.paintInCenter(p, _saveNavIcon);
		}

		// rotate button
		if (_rotateVisible && _rotateNavIcon.intersects(r)) {
			auto o = overLevel(OverRotate);
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			st::mediaviewRotate.paintInCenter(p, _rotateNavIcon);
		}

		// more area
		if (_moreNavIcon.intersects(r)) {
			auto o = overLevel(OverMore);
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			st::mediaviewMore.paintInCenter(p, _moreNavIcon);
		}

		p.setPen(st::mediaviewControlFg);
		p.setFont(st::mediaviewThickFont);

		// header
		if (_headerNav.intersects(r)) {
			auto o = _headerHasLink ? overLevel(OverHeader) : 0;
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			p.drawText(_headerNav.left(), _headerNav.top() + st::mediaviewThickFont->ascent, _headerText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_headerNav.left(), _headerNav.top() + st::mediaviewThickFont->ascent + 1, _headerNav.right(), _headerNav.top() + st::mediaviewThickFont->ascent + 1);
			}
		}

		p.setFont(st::mediaviewFont);

		// name
		if (_nameNav.isValid() && _nameNav.intersects(r)) {
			float64 o = _from ? overLevel(OverName) : 0.;
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			_fromNameLabel.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_nameNav.left(), _nameNav.top() + st::mediaviewFont->ascent + 1, _nameNav.right(), _nameNav.top() + st::mediaviewFont->ascent + 1);
			}
		}

		// date
		if (_dateNav.intersects(r)) {
			float64 o = overLevel(OverDate);
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			p.drawText(_dateNav.left(), _dateNav.top() + st::mediaviewFont->ascent, _dateText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_dateNav.left(), _dateNav.top() + st::mediaviewFont->ascent + 1, _dateNav.right(), _dateNav.top() + st::mediaviewFont->ascent + 1);
			}
		}

		// caption
		if (!_caption.isEmpty()) {
			QRect outer(_captionRect.marginsAdded(st::mediaviewCaptionPadding));
			if (outer.intersects(r)) {
				p.setOpacity(co);
				p.setBrush(st::mediaviewCaptionBg);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(outer, st::mediaviewCaptionRadius, st::mediaviewCaptionRadius);
				if (_captionRect.intersects(r)) {
					p.setTextPalette(st::mediaviewTextPalette);
					p.setPen(st::mediaviewCaptionFg);
					_caption.drawElided(p, _captionRect.x(), _captionRect.y(), _captionRect.width(), _captionRect.height() / st::mediaviewCaptionStyle.font->height);
					p.restoreTextPalette();
				}
			}
		}

		if (_groupThumbs && _groupThumbsRect.intersects(r)) {
			p.setOpacity(co);
			_groupThumbs->paint(
				p,
				_groupThumbsLeft,
				_groupThumbsTop,
				width());
			if (_groupThumbs->hidden()) {
				_groupThumbs = nullptr;
				_groupThumbsRect = QRect();
			}
		}
	}
	checkGroupThumbsAnimation();
}

void OverlayWidget::checkGroupThumbsAnimation() {
	if (_groupThumbs
		&& (!_streamed || _streamed->instance.player().ready())) {
		_groupThumbs->checkForAnimationStart();
	}
}

void OverlayWidget::paintTransformedVideoFrame(Painter &p) {
	Expects(_streamed != nullptr);

	const auto rect = contentRect();
	const auto image = videoFrameForDirectPaint();

	PainterHighQualityEnabler hq(p);

	const auto rotation = contentRotation();
	if (UsePainterRotation(rotation)) {
		if (rotation) {
			p.save();
			p.rotate(rotation);
		}
		p.drawImage(RotatedRect(rect, rotation), image);
		if (rotation) {
			p.restore();
		}
	} else {
		p.drawImage(rect, transformVideoFrame(image));
	}
	if (_streamed->instance.player().ready()) {
		_streamed->instance.markFrameShown();
	}
}

void OverlayWidget::paintTransformedStaticContent(Painter &p) {
	const auto rect = contentRect();

	PainterHighQualityEnabler hq(p);
	if ((!_document || !_documentMedia->getStickerLarge())
		&& (_staticContent.isNull()
			|| _staticContent.hasAlpha())) {
		p.fillRect(rect, _transparentBrush);
	}
	if (_staticContent.isNull()) {
		return;
	}
	const auto rotation = contentRotation();
	if (UsePainterRotation(rotation)) {
		if (rotation) {
			p.save();
			p.rotate(rotation);
		}
		p.drawPixmap(RotatedRect(rect, rotation), _staticContent);
		if (rotation) {
			p.restore();
		}
	} else {
		p.drawImage(rect, transformStaticContent(_staticContent));
	}
}

void OverlayWidget::paintRadialLoading(
		Painter &p,
		bool radial,
		float64 radialOpacity) {
	if (_streamed) {
		if (!_streamed->instance.waitingShown()) {
			return;
		}
	} else if (!radial && (!_document || _documentMedia->loaded())) {
		return;
	}

	const auto inner = radialRect();
	Assert(!inner.isEmpty());

#ifdef USE_OPENGL_OVERLAY_WIDGET
	{
		if (_radialCache.size() != inner.size() * cIntRetinaFactor()) {
			_radialCache = QImage(
				inner.size() * cIntRetinaFactor(),
				QImage::Format_ARGB32_Premultiplied);
			_radialCache.setDevicePixelRatio(cRetinaFactor());
		}
		_radialCache.fill(Qt::transparent);

		Painter q(&_radialCache);
		const auto moved = inner.translated(-inner.topLeft());
		paintRadialLoadingContent(q, moved, radial, radialOpacity);
	}
	p.drawImage(inner.topLeft(), _radialCache);
#else // USE_OPENGL_OVERLAY_WIDGET
	paintRadialLoadingContent(p, inner, radial, radialOpacity);
#endif // USE_OPENGL_OVERLAY_WIDGET
}

void OverlayWidget::paintRadialLoadingContent(
		Painter &p,
		QRect inner,
		bool radial,
		float64 radialOpacity) const {
	const auto arc = inner.marginsRemoved(QMargins(
		st::radialLine,
		st::radialLine,
		st::radialLine,
		st::radialLine));
	const auto paintBg = [&](float64 opacity, QBrush brush) {
		p.setOpacity(opacity);
		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}
		p.setOpacity(1.);
	};

	if (_streamed) {
		paintBg(
			_streamed->instance.waitingOpacity(),
			st::radialBg);
		Ui::InfiniteRadialAnimation::Draw(
			p,
			_streamed->instance.waitingState(),
			arc.topLeft(),
			arc.size(),
			width(),
			st::radialFg,
			st::radialLine);
		return;
	}
	if (_photo) {
		paintBg(radialOpacity, st::radialBg);
	} else {
		const auto o = overLevel(OverIcon);
		paintBg(
			_documentMedia->loaded() ? radialOpacity : 1.,
			anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, o));

		const auto icon = [&]() -> const style::icon * {
			if (radial || _document->loading()) {
				return &st::historyFileThumbCancel;
			}
			return &st::historyFileThumbDownload;
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
	}
	if (radial) {
		p.setOpacity(1);
		_radial.draw(p, arc, st::radialLine, st::radialFg);
	}
}

void OverlayWidget::paintThemePreview(Painter &p, QRect clip) {
	auto fill = _themePreviewRect.intersected(clip);
	if (!fill.isEmpty()) {
		if (_themePreview) {
			p.drawImage(
				myrtlrect(_themePreviewRect).topLeft(),
				_themePreview->preview);
		} else {
			p.fillRect(fill, st::themePreviewBg);
			p.setFont(st::themePreviewLoadingFont);
			p.setPen(st::themePreviewLoadingFg);
			p.drawText(
				_themePreviewRect,
				(_themePreviewId
					? tr::lng_theme_preview_generating(tr::now)
					: tr::lng_theme_preview_invalid(tr::now)),
				QTextOption(style::al_center));
		}
	}

	auto fillOverlay = [&](QRect fill) {
		auto clipped = fill.intersected(clip);
		if (!clipped.isEmpty()) {
			p.setOpacity(st::themePreviewOverlayOpacity);
			p.fillRect(clipped, st::themePreviewBg);
			p.setOpacity(1.);
		}
	};
	auto titleRect = QRect(_themePreviewRect.x(), _themePreviewRect.y(), _themePreviewRect.width(), st::themePreviewMargin.top());
	if (titleRect.x() < 0) {
		titleRect = QRect(0, _themePreviewRect.y(), width(), st::themePreviewMargin.top());
	}
	if (auto fillTitleRect = (titleRect.y() < 0)) {
		titleRect.moveTop(0);
		fillOverlay(titleRect);
	}
	titleRect = titleRect.marginsRemoved(QMargins(st::themePreviewMargin.left(), st::themePreviewTitleTop, st::themePreviewMargin.right(), titleRect.height() - st::themePreviewTitleTop - st::themePreviewTitleFont->height));
	if (titleRect.intersects(clip)) {
		p.setFont(st::themePreviewTitleFont);
		p.setPen(st::themePreviewTitleFg);
		const auto title = _themeCloudData.title.isEmpty()
			? tr::lng_theme_preview_title(tr::now)
			: _themeCloudData.title;
		const auto elided = st::themePreviewTitleFont->elided(title, titleRect.width());
		p.drawTextLeft(titleRect.x(), titleRect.y(), width(), elided);
	}

	auto buttonsRect = QRect(_themePreviewRect.x(), _themePreviewRect.y() + _themePreviewRect.height() - st::themePreviewMargin.bottom(), _themePreviewRect.width(), st::themePreviewMargin.bottom());
	if (auto fillButtonsRect = (buttonsRect.y() + buttonsRect.height() > height())) {
		buttonsRect.moveTop(height() - buttonsRect.height());
		fillOverlay(buttonsRect);
	}
	if (_themeShare && _themeCloudData.usersCount > 0) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		const auto left = _themeShare->x() + _themeShare->width() - (st::themePreviewCancelButton.width / 2);
		const auto baseline = _themeShare->y() + st::themePreviewCancelButton.padding.top() + +st::themePreviewCancelButton.textTop + st::themePreviewCancelButton.font->ascent;
		p.drawText(left, baseline, tr::lng_theme_preview_users(tr::now, lt_count, _themeCloudData.usersCount));
	}
}

void OverlayWidget::keyPressEvent(QKeyEvent *e) {
	const auto ctrl = e->modifiers().testFlag(Qt::ControlModifier);
	if (_streamed) {
		// Ctrl + F for full screen toggle is in eventFilter().
		const auto toggleFull = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return)
			&& (e->modifiers().testFlag(Qt::AltModifier) || ctrl);
		if (toggleFull) {
			playbackToggleFullScreen();
			return;
		} else if (e->key() == Qt::Key_Space) {
			playbackPauseResume();
			return;
		} else if (_fullScreenVideo) {
			if (e->key() == Qt::Key_Escape) {
				playbackToggleFullScreen();
			}
			return;
		}
	}
	if (!_menu && e->key() == Qt::Key_Escape) {
		if (_document && _document->loading() && !_streamed) {
			onDocClick();
		} else {
			close();
		}
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSaveAs();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && ctrl)) {
		onCopy();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		if (_streamed) {
			playbackPauseResume();
		} else if (_document && !_document->loading() && (documentBubbleShown() || !_documentMedia->loaded())) {
			onDocClick();
		}
	} else if (e->key() == Qt::Key_Left) {
		if (_controlsHideTimer.isActive()) {
			activateControls();
		}
		moveToNext(-1);
	} else if (e->key() == Qt::Key_Right) {
		if (_controlsHideTimer.isActive()) {
			activateControls();
		}
		moveToNext(1);
	} else if (ctrl) {
		if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Asterisk || e->key() == ']') {
			zoomIn();
		} else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore) {
			zoomOut();
		} else if (e->key() == Qt::Key_0) {
			zoomReset();
		} else if (e->key() == Qt::Key_I) {
			update();
		}
	}
}

void OverlayWidget::wheelEvent(QWheelEvent *e) {
#ifdef OS_MAC_OLD
	constexpr auto step = 120;
#else // OS_MAC_OLD
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
#endif // OS_MAC_OLD

	_verticalWheelDelta += e->angleDelta().y();
	while (qAbs(_verticalWheelDelta) >= step) {
		if (_verticalWheelDelta < 0) {
			_verticalWheelDelta += step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomOut();
			} else {
#ifndef OS_MAC_OLD
				if (e->source() == Qt::MouseEventNotSynthesized) {
					moveToNext(1);
				}
#endif // OS_MAC_OLD
			}
		} else {
			_verticalWheelDelta -= step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomIn();
			} else {
#ifndef OS_MAC_OLD
				if (e->source() == Qt::MouseEventNotSynthesized) {
					moveToNext(-1);
				}
#endif // OS_MAC_OLD
			}
		}
	}
}

void OverlayWidget::setZoomLevel(int newZoom, bool force) {
	if (!force && _zoom == newZoom) {
		return;
	}

	const auto full = _fullScreenVideo ? _zoomToScreen : _zoomToDefault;
	float64 nx, ny, z = (_zoom == kZoomToScreenLevel) ? full : _zoom;
	const auto contentSize = videoShown()
		? style::ConvertScale(videoSize())
		: QSize(_width, _height);
	_w = contentSize.width();
	_h = contentSize.height();
	if (z >= 0) {
		nx = (_x - width() / 2.) / (z + 1);
		ny = (_y - height() / 2.) / (z + 1);
	} else {
		nx = (_x - width() / 2.) * (-z + 1);
		ny = (_y - height() / 2.) * (-z + 1);
	}
	_zoom = newZoom;
	z = (_zoom == kZoomToScreenLevel) ? full : _zoom;
	if (z > 0) {
		_w = qRound(_w * (z + 1));
		_h = qRound(_h * (z + 1));
		_x = qRound(nx * (z + 1) + width() / 2.);
		_y = qRound(ny * (z + 1) + height() / 2.);
	} else {
		_w = qRound(_w / (-z + 1));
		_h = qRound(_h / (-z + 1));
		_x = qRound(nx / (-z + 1) + width() / 2.);
		_y = qRound(ny / (-z + 1) + height() / 2.);
	}
	snapXY();
	update();
}

OverlayWidget::Entity OverlayWidget::entityForUserPhotos(int index) const {
	Expects(_userPhotosData.has_value());
	Expects(_session != nullptr);

	if (index < 0 || index >= _userPhotosData->size()) {
		return { v::null, nullptr };
	}
	const auto id = (*_userPhotosData)[index];
	if (const auto photo = _session->data().photo(id)) {
		return { photo, nullptr };
	}
	return { v::null, nullptr };
}

OverlayWidget::Entity OverlayWidget::entityForSharedMedia(int index) const {
	Expects(_sharedMediaData.has_value());

	if (index < 0 || index >= _sharedMediaData->size()) {
		return { v::null, nullptr };
	}
	auto value = (*_sharedMediaData)[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		// Last peer photo.
		return { *photo, nullptr };
	} else if (const auto itemId = std::get_if<FullMsgId>(&value)) {
		return entityForItemId(*itemId);
	}
	return { v::null, nullptr };
}

OverlayWidget::Entity OverlayWidget::entityForCollage(int index) const {
	Expects(_collageData.has_value());
	Expects(_session != nullptr);

	const auto item = _session->data().message(_msgid);
	const auto &items = _collageData->items;
	if (!item || index < 0 || index >= items.size()) {
		return { v::null, nullptr };
	}
	if (const auto document = std::get_if<DocumentData*>(&items[index])) {
		return { *document, item };
	} else if (const auto photo = std::get_if<PhotoData*>(&items[index])) {
		return { *photo, item };
	}
	return { v::null, nullptr };
}

OverlayWidget::Entity OverlayWidget::entityForItemId(const FullMsgId &itemId) const {
	Expects(_session != nullptr);

	if (const auto item = _session->data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				return { photo, item };
			} else if (const auto document = media->document()) {
				return { document, item };
			}
		}
		return { v::null, item };
	}
	return { v::null, nullptr };
}

OverlayWidget::Entity OverlayWidget::entityByIndex(int index) const {
	if (_sharedMediaData) {
		return entityForSharedMedia(index);
	} else if (_userPhotosData) {
		return entityForUserPhotos(index);
	} else if (_collageData) {
		return entityForCollage(index);
	}
	return { v::null, nullptr };
}

void OverlayWidget::setContext(
	std::variant<
		v::null_t,
		not_null<HistoryItem*>,
		not_null<PeerData*>> context) {
	if (const auto item = std::get_if<not_null<HistoryItem*>>(&context)) {
		_msgid = (*item)->fullId();
		_canForwardItem = (*item)->allowsForward();
		_canDeleteItem = (*item)->canDelete();
		_history = (*item)->history();
		_peer = _history->peer;
	} else if (const auto peer = std::get_if<not_null<PeerData*>>(&context)) {
		_msgid = FullMsgId();
		_canForwardItem = _canDeleteItem = false;
		_history = (*peer)->owner().history(*peer);
		_peer = *peer;
	} else {
		_msgid = FullMsgId();
		_canForwardItem = _canDeleteItem = false;
		_history = nullptr;
		_peer = nullptr;
	}
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = _history->owner().history(_history->peer->migrateFrom());
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = _history->owner().history(_history->peer->migrateTo());
		}
	}
	_user = _peer ? _peer->asUser() : nullptr;
}

void OverlayWidget::setSession(not_null<Main::Session*> session) {
	if (_session == session) {
		return;
	}

	clearSession();
	_session = session;
	setWindowIcon(Window::CreateIcon(session));

	session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		if (!isHidden()) {
			updateControls();
			checkForSaveLoaded();
		}
	}, _sessionLifetime);

	base::ObservableViewer(
		session->documentUpdated
	) | rpl::start_with_next([=](DocumentData *document) {
		if (!isHidden()) {
			documentUpdated(document);
		}
	}, _sessionLifetime);

	session->data().itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		changingMsgId(change.item, change.oldId);
	}, _sessionLifetime);

	session->data().itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (_document != nullptr || _photo != nullptr)
			&& (item->fullId() == _msgid);
	}) | rpl::start_with_next([=] {
		close();
	}, _sessionLifetime);

	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		clearSession();
	}, _sessionLifetime);
}

bool OverlayWidget::moveToNext(int delta) {
	if (!_index) {
		return false;
	}
	auto newIndex = *_index + delta;
	return moveToEntity(entityByIndex(newIndex), delta);
}

bool OverlayWidget::moveToEntity(const Entity &entity, int preloadDelta) {
	if (v::is_null(entity.data) && !entity.item) {
		return false;
	}
	if (const auto item = entity.item) {
		setContext(item);
	} else if (_peer) {
		setContext(_peer);
	} else {
		setContext(v::null);
	}
	clearStreaming();
	_streamingStartPaused = false;
	if (auto photo = std::get_if<not_null<PhotoData*>>(&entity.data)) {
		displayPhoto(*photo, entity.item);
	} else if (auto document = std::get_if<not_null<DocumentData*>>(&entity.data)) {
		displayDocument(*document, entity.item);
	} else {
		displayDocument(nullptr, entity.item);
	}
	preloadData(preloadDelta);
	return true;
}

void OverlayWidget::preloadData(int delta) {
	if (!_index) {
		return;
	}
	auto from = *_index + (delta ? delta : -1);
	auto till = *_index + (delta ? delta * kPreloadCount : 1);
	if (from > till) std::swap(from, till);

	auto photos = base::flat_set<std::shared_ptr<Data::PhotoMedia>>();
	auto documents = base::flat_set<std::shared_ptr<Data::DocumentMedia>>();
	for (auto index = from; index != till + 1; ++index) {
		auto entity = entityByIndex(index);
		if (auto photo = std::get_if<not_null<PhotoData*>>(&entity.data)) {
			const auto [i, ok] = photos.emplace((*photo)->createMediaView());
			(*i)->wanted(Data::PhotoSize::Small, fileOrigin(entity));
			(*photo)->load(fileOrigin(entity), LoadFromCloudOrLocal, true);
		} else if (auto document = std::get_if<not_null<DocumentData*>>(
				&entity.data)) {
			const auto [i, ok] = documents.emplace(
				(*document)->createMediaView());
			(*i)->thumbnailWanted(fileOrigin(entity));
			if (!(*i)->canBePlayed()) {
				(*i)->automaticLoad(fileOrigin(entity), entity.item);
			}
		}
	}
	_preloadPhotos = std::move(photos);
	_preloadDocuments = std::move(documents);
}

void OverlayWidget::mousePressEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_menu || !_receiveMouse) return;

	ClickHandler::pressed();

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (!ClickHandler::getPressed()) {
			if (_over == OverLeftNav && moveToNext(-1)) {
				_lastAction = e->pos();
			} else if (_over == OverRightNav && moveToNext(1)) {
				_lastAction = e->pos();
			} else if (_over == OverName
				|| _over == OverDate
				|| _over == OverHeader
				|| _over == OverSave
				|| _over == OverRotate
				|| _over == OverIcon
				|| _over == OverMore
				|| _over == OverClose
				|| _over == OverVideo) {
				_down = _over;
			} else if (!_saveMsg.contains(e->pos()) || !_saveMsgStarted) {
				_pressed = true;
				_dragging = 0;
				updateCursor();
				_mStart = e->pos();
				_xStart = _x;
				_yStart = _y;
			}
		}
	} else if (e->button() == Qt::MiddleButton) {
		zoomReset();
	}
	activateControls();
}

void OverlayWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (_over == OverVideo && _streamed) {
		playbackToggleFullScreen();
		playbackPauseResume();
	} else {
		e->ignore();
		return OverlayParent::mouseDoubleClickEvent(e);
	}
}

void OverlayWidget::snapXY() {
	int32 xmin = width() - _w, xmax = 0;
	int32 ymin = height() - _h, ymax = 0;
	if (xmin > (width() - _w) / 2) xmin = (width() - _w) / 2;
	if (xmax < (width() - _w) / 2) xmax = (width() - _w) / 2;
	if (ymin > (height() - _h) / 2) ymin = (height() - _h) / 2;
	if (ymax < (height() - _h) / 2) ymax = (height() - _h) / 2;
	if (_x < xmin) _x = xmin;
	if (_x > xmax) _x = xmax;
	if (_y < ymin) _y = ymin;
	if (_y > ymax) _y = ymax;
}

void OverlayWidget::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::mediaviewDeltaFromLastAction) {
		_lastAction = QPoint(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction);
	}
	if (_pressed) {
		if (!_dragging && (e->pos() - _mStart).manhattanLength() >= QApplication::startDragDistance()) {
			_dragging = QRect(_x, _y, _w, _h).contains(_mStart) ? 1 : -1;
			if (_dragging > 0) {
				if (_w > width() || _h > height()) {
					setCursor(style::cur_sizeall);
				} else {
					setCursor(style::cur_default);
				}
			}
		}
		if (_dragging > 0) {
			_x = _xStart + (e->pos() - _mStart).x();
			_y = _yStart + (e->pos() - _mStart).y();
			snapXY();
			update();
		}
	}
}

void OverlayWidget::updateOverRect(OverState state) {
	switch (state) {
	case OverLeftNav: update(_leftNav); break;
	case OverRightNav: update(_rightNav); break;
	case OverName: update(_nameNav); break;
	case OverDate: update(_dateNav); break;
	case OverSave: update(_saveNavIcon); break;
	case OverRotate: update(_rotateNavIcon); break;
	case OverIcon: update(_docIconRect); break;
	case OverHeader: update(_headerNav); break;
	case OverClose: update(_closeNav); break;
	case OverMore: update(_moreNavIcon); break;
	}
}

bool OverlayWidget::updateOverState(OverState newState) {
	bool result = true;
	if (_over != newState) {
		if (newState == OverMore && !_ignoringDropdown) {
			_dropdownShowTimer->start(0);
		} else {
			_dropdownShowTimer->stop();
		}
		updateOverRect(_over);
		updateOverRect(newState);
		if (_over != OverNone) {
			_animations[_over] = crl::now();
			const auto i = _animationOpacities.find(_over);
			if (i != end(_animationOpacities)) {
				i->second.start(0);
			} else {
				_animationOpacities.emplace(_over, anim::value(1, 0));
			}
			if (!_stateAnimation.animating()) {
				_stateAnimation.start();
			}
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_animations[_over] = crl::now();
			const auto i = _animationOpacities.find(_over);
			if (i != end(_animationOpacities)) {
				i->second.start(1);
			} else {
				_animationOpacities.emplace(_over, anim::value(0, 1));
			}
			if (!_stateAnimation.animating()) {
				_stateAnimation.start();
			}
		}
		updateCursor();
	}
	return result;
}

void OverlayWidget::updateOver(QPoint pos) {
	ClickHandlerPtr lnk;
	ClickHandlerHost *lnkhost = nullptr;
	if (_saveMsgStarted && _saveMsg.contains(pos)) {
		auto textState = _saveMsgText.getState(pos - _saveMsg.topLeft() - QPoint(st::mediaviewSaveMsgPadding.left(), st::mediaviewSaveMsgPadding.top()), _saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
		lnk = textState.link;
		lnkhost = this;
	} else if (_captionRect.contains(pos)) {
		auto textState = _caption.getState(pos - _captionRect.topLeft(), _captionRect.width());
		lnk = textState.link;
		lnkhost = this;
	} else if (_groupThumbs && _groupThumbsRect.contains(pos)) {
		const auto point = pos - QPoint(_groupThumbsLeft, _groupThumbsTop);
		lnk = _groupThumbs->getState(point);
		lnkhost = this;
	}


	// retina
	if (pos.x() == width()) {
		pos.setX(pos.x() - 1);
	}
	if (pos.y() == height()) {
		pos.setY(pos.y() - 1);
	}

	ClickHandler::setActive(lnk, lnkhost);

	if (_pressed || _dragging) return;

	if (_fullScreenVideo) {
		updateOverState(OverVideo);
	} else if (_leftNavVisible && _leftNav.contains(pos)) {
		updateOverState(OverLeftNav);
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		updateOverState(OverRightNav);
	} else if (_from && _nameNav.contains(pos)) {
		updateOverState(OverName);
	} else if (IsServerMsgId(_msgid.msg) && _dateNav.contains(pos)) {
		updateOverState(OverDate);
	} else if (_headerHasLink && _headerNav.contains(pos)) {
		updateOverState(OverHeader);
	} else if (_saveVisible && _saveNav.contains(pos)) {
		updateOverState(OverSave);
	} else if (_rotateVisible && _rotateNav.contains(pos)) {
		updateOverState(OverRotate);
	} else if (_document && documentBubbleShown() && _docIconRect.contains(pos)) {
		updateOverState(OverIcon);
	} else if (_moreNav.contains(pos)) {
		updateOverState(OverMore);
	} else if (_closeNav.contains(pos)) {
		updateOverState(OverClose);
	} else if (documentContentShown() && contentRect().contains(pos)) {
		if ((_document->isVideoFile() || _document->isVideoMessage()) && _streamed) {
			updateOverState(OverVideo);
		} else if (!_streamed && !_documentMedia->loaded()) {
			updateOverState(OverIcon);
		} else if (_over != OverNone) {
			updateOverState(OverNone);
		}
	} else if (_over != OverNone) {
		updateOverState(OverNone);
	}
}

void OverlayWidget::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (const auto activated = ClickHandler::unpressed()) {
		if (activated->dragText() == qstr("internal:show_saved_message")) {
			showSaveMsgFile();
			return;
		}
		// There may be a mention / hashtag / bot command link.
		// For now activate account for all activated links.
		if (_session) {
			Core::App().domain().activate(&_session->account());
		}
		ActivateClickHandler(this, activated, e->button());
		return;
	}

	if (_over == OverName && _down == OverName) {
		if (_from) {
			close();
			Ui::showPeerProfile(_from);
		}
	} else if (_over == OverDate && _down == OverDate) {
		onToMessage();
	} else if (_over == OverHeader && _down == OverHeader) {
		onOverview();
	} else if (_over == OverSave && _down == OverSave) {
		onDownload();
	} else if (_over == OverRotate && _down == OverRotate) {
		playbackControlsRotate();
	} else if (_over == OverIcon && _down == OverIcon) {
		onDocClick();
	} else if (_over == OverMore && _down == OverMore) {
		QTimer::singleShot(0, this, SLOT(onDropdown()));
	} else if (_over == OverClose && _down == OverClose) {
		close();
	} else if (_over == OverVideo && _down == OverVideo) {
		if (_streamed) {
			playbackPauseResume();
		}
	} else if (_pressed) {
		if (_dragging) {
			if (_dragging > 0) {
				_x = _xStart + (e->pos() - _mStart).x();
				_y = _yStart + (e->pos() - _mStart).y();
				snapXY();
				update();
			}
			_dragging = 0;
			setCursor(style::cur_default);
		} else if ((e->pos() - _lastAction).manhattanLength() >= st::mediaviewDeltaFromLastAction) {
			if (_themePreviewShown) {
				if (!_themePreviewRect.contains(e->pos())) {
					close();
				}
			} else if (!_document
				|| documentContentShown()
				|| !documentBubbleShown()
				|| !_docRect.contains(e->pos())) {
				close();
			}
		}
		_pressed = false;
	}
	_down = OverNone;
	if (!isHidden()) {
		activateControls();
	}
}

void OverlayWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos())) {
		if (_menu) {
			_menu->deleteLater();
			_menu = nullptr;
		}
		_menu = new Ui::PopupMenu(this, st::mediaviewPopupMenu);
		updateActions();
		for_const (auto &action, _actions) {
			_menu->addAction(action.text, this, action.member);
		}
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
		activateControls();
	}
}

void OverlayWidget::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) return;
		auto weak = Ui::MakeWeak(this);
		if (!_touchMove) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			auto mapped = mapFromGlobal(_touchStart);

			QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, mapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			pressEvent.accept();
			if (weak) mousePressEvent(&pressEvent);

			QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, mapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			if (weak) mouseReleaseEvent(&releaseEvent);

			if (weak && _touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		} else if (_touchMove) {
			if ((!_leftNavVisible || !_leftNav.contains(mapFromGlobal(_touchStart))) && (!_rightNavVisible || !_rightNav.contains(mapFromGlobal(_touchStart)))) {
				QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::mediaviewSwipeDistance || d.x() < -st::mediaviewSwipeDistance)) {
					moveToNext(d.x() > 0 ? -1 : 1);
				}
			}
		}
		if (weak) {
			_touchTimer.stop();
			_touchPress = _touchMove = _touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

bool OverlayWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::UpdateRequest) {
		_wasRepainted = true;
	} else if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			if (ev->type() != QEvent::TouchBegin || ev->touchPoints().isEmpty() || !childAt(mapFromGlobal(ev->touchPoints().cbegin()->screenPos().toPoint()))) {
				touchEvent(ev);
				return true;
			}
		}
	} else if (e->type() == QEvent::Wheel) {
		QWheelEvent *ev = static_cast<QWheelEvent*>(e);
		if (ev->phase() == Qt::ScrollBegin) {
			_accumScroll = ev->angleDelta();
		} else {
			_accumScroll += ev->angleDelta();
			if (ev->phase() == Qt::ScrollEnd) {
				if (ev->angleDelta().x() != 0) {
					if (_accumScroll.x() * _accumScroll.x() > _accumScroll.y() * _accumScroll.y() && _accumScroll.x() != 0) {
						moveToNext(_accumScroll.x() > 0 ? -1 : 1);
					}
					_accumScroll = QPoint();
				}
			}
		}
	}
	return OverlayParent::eventHook(e);
}

bool OverlayWidget::eventFilter(QObject *obj, QEvent *e) {
	auto type = e->type();
	if (type == QEvent::ShortcutOverride) {
		const auto keyEvent = static_cast<QKeyEvent*>(e);
		const auto ctrl = keyEvent->modifiers().testFlag(Qt::ControlModifier);
		if (keyEvent->key() == Qt::Key_F && ctrl && _streamed) {
			playbackToggleFullScreen();
		}
		return true;
	}
	if ((type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) && obj->isWidgetType()) {
		if (isAncestorOf(static_cast<QWidget*>(obj))) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e);
			const auto mousePosition = mapFromGlobal(mouseEvent->globalPos());
			const auto delta = (mousePosition - _lastMouseMovePos);
			auto activate = delta.manhattanLength() >= st::mediaviewDeltaFromLastAction;
			if (activate) {
				_lastMouseMovePos = mousePosition;
			}
			if (type == QEvent::MouseButtonPress) {
				_mousePressed = true;
				activate = true;
			} else if (type == QEvent::MouseButtonRelease) {
				_mousePressed = false;
				activate = true;
			}
			if (activate) {
				activateControls();
			}
		}
	}
	return OverlayParent::eventFilter(obj, e);
}

void OverlayWidget::applyHideWindowWorkaround() {
#ifdef USE_OPENGL_OVERLAY_WIDGET
	// QOpenGLWidget can't properly destroy a child widget if
	// it is hidden exactly after that, so it must be repainted
	// before it is hidden without the child widget.
	if (!isHidden()) {
		_dropdown->hideFast();
		hideChildren();
		_wasRepainted = false;
		repaint();
		if (!_wasRepainted) {
			// Qt has some optimization to prevent too frequent repaints.
			// If the previous repaint was less than 1/60 second it silently
			// converts repaint() call to an update() call. But we have to
			// repaint right now, before hide(), with _streamingControls destroyed.
			auto event = QEvent(QEvent::UpdateRequest);
			QApplication::sendEvent(this, &event);
		}
	}
#endif // USE_OPENGL_OVERLAY_WIDGET
}

void OverlayWidget::setVisibleHook(bool visible) {
	if (!visible) {
		applyHideWindowWorkaround();
		_sharedMedia = nullptr;
		_sharedMediaData = std::nullopt;
		_sharedMediaDataKey = std::nullopt;
		_userPhotos = nullptr;
		_userPhotosData = std::nullopt;
		_collage = nullptr;
		_collageData = std::nullopt;
		assignMediaPointer(nullptr);
		_preloadPhotos.clear();
		_preloadDocuments.clear();
		if (_menu) _menu->hideMenu(true);
		_controlsHideTimer.stop();
		_controlsState = ControlsShown;
		_controlsOpacity = anim::value(1, 1);
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	OverlayParent::setVisibleHook(visible);
	if (visible) {
		QCoreApplication::instance()->installEventFilter(this);
	} else {
		QCoreApplication::instance()->removeEventFilter(this);

		clearStreaming();
		destroyThemePreview();
		_radial.stop();
		_staticContent = QPixmap();
		_themePreview = nullptr;
		_themeApply.destroyDelayed();
		_themeCancel.destroyDelayed();
		_themeShare.destroyDelayed();
	}
}

void OverlayWidget::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = nullptr;
		activateControls();
	}
	_receiveMouse = false;
	QTimer::singleShot(0, this, SLOT(receiveMouse()));
}

void OverlayWidget::receiveMouse() {
	_receiveMouse = true;
}

void OverlayWidget::onDropdown() {
	updateActions();
	_dropdown->clearActions();
	for_const (auto &action, _actions) {
		_dropdown->addAction(action.text, this, action.member);
	}
	_dropdown->moveToRight(0, height() - _dropdown->height());
	_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	_dropdown->setFocus();
}

void OverlayWidget::onTouchTimer() {
	_touchRightButton = true;
}

void OverlayWidget::updateImage() {
	update(_saveMsg);
}

void OverlayWidget::findCurrent() {
	using namespace rpl::mappers;
	if (_sharedMediaData) {
		_index = _msgid
			? _sharedMediaData->indexOf(_msgid)
			: _photo ? _sharedMediaData->indexOf(_photo) : std::nullopt;
		_fullIndex = _sharedMediaData->skippedBefore()
			? (_index | func::add(*_sharedMediaData->skippedBefore()))
			: std::nullopt;
		_fullCount = _sharedMediaData->fullCount();
	} else if (_userPhotosData) {
		_index = _photo ? _userPhotosData->indexOf(_photo->id) : std::nullopt;
		_fullIndex = _userPhotosData->skippedBefore()
			? (_index | func::add(*_userPhotosData->skippedBefore()))
			: std::nullopt;
		_fullCount = _userPhotosData->fullCount();
	} else if (_collageData) {
		const auto item = _photo ? WebPageCollage::Item(_photo) : _document;
		const auto &items = _collageData->items;
		const auto i = ranges::find(items, item);
		_index = (i != end(items))
			? std::make_optional(int(i - begin(items)))
			: std::nullopt;
		_fullIndex = _index;
		_fullCount = items.size();
	} else {
		_index = _fullIndex = _fullCount = std::nullopt;
	}
}

void OverlayWidget::updateHeader() {
	auto index = _fullIndex ? *_fullIndex : -1;
	auto count = _fullCount ? *_fullCount : -1;
	if (index >= 0 && index < count && count > 1) {
		if (_document) {
			_headerText = tr::lng_mediaview_file_n_of_amount(
				tr::now,
				lt_file,
				(_document->filename().isEmpty()
					? tr::lng_mediaview_doc_image(tr::now)
					: _document->filename()),
				lt_n,
				QString::number(index + 1),
				lt_amount,
				QString::number(count));
		} else {
			_headerText = tr::lng_mediaview_n_of_amount(
				tr::now,
				lt_n,
				QString::number(index + 1),
				lt_amount,
				QString::number(count));
		}
	} else {
		if (_document) {
			_headerText = _document->filename().isEmpty() ? tr::lng_mediaview_doc_image(tr::now) : _document->filename();
		} else if (_msgid) {
			_headerText = tr::lng_mediaview_single_photo(tr::now);
		} else if (_user) {
			_headerText = tr::lng_mediaview_profile_photo(tr::now);
		} else if ((_history && _history->channelId() && !_history->isMegagroup())
			|| (_peer && _peer->isChannel() && !_peer->isMegagroup())) {
			_headerText = tr::lng_mediaview_channel_photo(tr::now);
		} else if (_peer) {
			_headerText = tr::lng_mediaview_group_photo(tr::now);
		} else {
			_headerText = tr::lng_mediaview_single_photo(tr::now);
		}
	}
	_headerHasLink = computeOverviewType() != std::nullopt;
	auto hwidth = st::mediaviewThickFont->width(_headerText);
	if (hwidth > width() / 3) {
		hwidth = width() / 3;
		_headerText = st::mediaviewThickFont->elided(_headerText, hwidth, Qt::ElideMiddle);
	}
	_headerNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewHeaderTop, hwidth, st::mediaviewThickFont->height);
}

float64 OverlayWidget::overLevel(OverState control) const {
	auto i = _animationOpacities.find(control);
	return (i == end(_animationOpacities))
		? (_over == control ? 1. : 0.)
		: i->second.current();
}

} // namespace View
} // namespace Media
