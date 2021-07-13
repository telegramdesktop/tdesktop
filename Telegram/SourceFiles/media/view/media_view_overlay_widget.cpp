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
#include "ui/gl/gl_surface.h"
#include "boxes/confirm_box.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_group_thumbs.h"
#include "media/view/media_view_pip.h"
#include "media/view/media_view_overlay_raster.h"
#include "media/view/media_view_overlay_opengl.h"
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
#include "data/data_document_resolver.h"
#include "data/data_file_click_handler.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "base/qt_signal_producer.h"
#include "base/event_filter.h"
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

[[nodiscard]] Images::Options VideoThumbOptions(DocumentData *document) {
	const auto result = Images::Option::Smooth | Images::Option::Blurred;
	return (document && document->isVideoMessage())
		? (result | Images::Option::Circled)
		: result;
}

[[nodiscard]] QImage PrepareStaticImage(QImage image) {
	if (image.width() > kMaxDisplayImageSize
		|| image.height() > kMaxDisplayImageSize) {
		image = image.scaled(
			kMaxDisplayImageSize,
			kMaxDisplayImageSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return image;
}

[[nodiscard]] QImage PrepareStaticImage(const QString &path) {
	return PrepareStaticImage(App::readImage(path, nullptr, false));
}

[[nodiscard]] QImage PrepareStaticImage(const QByteArray &bytes) {
	return PrepareStaticImage(App::readImage(bytes, nullptr, false));
}

[[nodiscard]] bool IsSemitransparent(const QImage &image) {
	if (image.isNull()) {
		return true;
	} else if (!image.hasAlphaChannel()) {
		return false;
	}
	Assert(image.format() == QImage::Format_ARGB32_Premultiplied);
	constexpr auto kAlphaMask = 0xFF000000;
	auto ints = reinterpret_cast<const uint32*>(image.bits());
	const auto add = (image.bytesPerLine() / 4) - image.width();
	for (auto y = 0; y != image.height(); ++y) {
		for (auto till = ints + image.width(); ints != till; ++ints) {
			if ((*ints & kAlphaMask) != kAlphaMask) {
				return true;
			}
		}
		ints += add;
	}
	return false;
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
		not_null<QWidget*> controlsParent,
		not_null<PlaybackControls::Delegate*> controlsDelegate,
		Fn<void()> waitingCallback);
	Streamed(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		not_null<QWidget*> controlsParent,
		not_null<PlaybackControls::Delegate*> controlsDelegate,
		Fn<void()> waitingCallback);

	Streaming::Instance instance;
	PlaybackControls controls;

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
	not_null<QWidget*> controlsParent,
	not_null<PlaybackControls::Delegate*> controlsDelegate,
	Fn<void()> waitingCallback)
: instance(document, origin, std::move(waitingCallback))
, controls(controlsParent, controlsDelegate) {
}

OverlayWidget::Streamed::Streamed(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	not_null<QWidget*> controlsParent,
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
: _surface(Ui::GL::CreateSurface(
	[=](Ui::GL::Capabilities capabilities) {
		return chooseRenderer(capabilities);
	}))
, _widget(_surface->rpWidget())
, _docDownload(_widget, tr::lng_media_download(tr::now), st::mediaviewFileLink)
, _docSaveAs(_widget, tr::lng_mediaview_save_as(tr::now), st::mediaviewFileLink)
, _docCancel(_widget, tr::lng_cancel(tr::now), st::mediaviewFileLink)
, _radial([=](crl::time now) { return radialAnimationCallback(now); })
, _lastAction(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction)
, _stateAnimation([=](crl::time now) { return stateAnimationCallback(now); })
, _dropdown(_widget, st::mediaviewDropdownMenu) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_lastPositiveVolume = (Core::App().settings().videoVolume() > 0.)
		? Core::App().settings().videoVolume()
		: Core::Settings::kDefaultVolume;

	_widget->setWindowTitle(qsl("Media viewer"));

	const auto text = tr::lng_mediaview_saved_to(
		tr::now,
		lt_downloads,
		Ui::Text::Link(
			tr::lng_mediaview_downloads(tr::now),
			"internal:show_saved_message"),
		Ui::Text::WithEntities);
	_saveMsgText.setMarkedText(st::mediaviewSaveMsgStyle, text, Ui::DialogTextOptions());
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::mediaviewSaveMsgPadding.left() + st::mediaviewSaveMsgPadding.right(), st::mediaviewSaveMsgStyle.font->height + st::mediaviewSaveMsgPadding.top() + st::mediaviewSaveMsgPadding.bottom());
	_saveMsgImage = QImage(
		_saveMsg.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);

	_docRectImage = QImage(
		st::mediaviewFileSize * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	_docRectImage.setDevicePixelRatio(cIntRetinaFactor());

	_surface->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		toggleApplicationEventFilter(shown);
		if (shown) {
			const auto screenList = QGuiApplication::screens();
			DEBUG_LOG(("Viewer Pos: Shown, screen number: %1")
				.arg(screenList.indexOf(window()->screen())));
			moveToScreen();
		} else {
			clearAfterHide();
		}
	}, lifetime());

	const auto mousePosition = [](not_null<QEvent*> e) {
		return static_cast<QMouseEvent*>(e.get())->pos();
	};
	const auto mouseButton = [](not_null<QEvent*> e) {
		return static_cast<QMouseEvent*>(e.get())->button();
	};
	base::install_event_filter(_widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Move) {
			const auto position = static_cast<QMoveEvent*>(e.get())->pos();
			DEBUG_LOG(("Viewer Pos: Moved to %1, %2")
				.arg(position.x())
				.arg(position.y()));
		} else if (type == QEvent::Resize) {
			const auto size = static_cast<QResizeEvent*>(e.get())->size();
			DEBUG_LOG(("Viewer Pos: Resized to %1, %2")
				.arg(size.width())
				.arg(size.height()));
			updateControlsGeometry();
		} else if (type == QEvent::MouseButtonPress) {
			handleMousePress(mousePosition(e), mouseButton(e));
		} else if (type == QEvent::MouseButtonRelease) {
			handleMouseRelease(mousePosition(e), mouseButton(e));
		} else if (type == QEvent::MouseMove) {
			handleMouseMove(mousePosition(e));
		} else if (type == QEvent::KeyPress) {
			handleKeyPress(static_cast<QKeyEvent*>(e.get()));
		} else if (type == QEvent::ContextMenu) {
			const auto event = static_cast<QContextMenuEvent*>(e.get());
			const auto mouse = (event->reason() == QContextMenuEvent::Mouse);
			const auto position = mouse
				? std::make_optional(event->pos())
				: std::nullopt;
			if (handleContextMenu(position)) {
				return base::EventFilterResult::Cancel;
			}
		} else if (type == QEvent::MouseButtonDblClick) {
			if (handleDoubleClick(mousePosition(e), mouseButton(e))) {
				return base::EventFilterResult::Cancel;
			} else {
				handleMousePress(mousePosition(e), mouseButton(e));
			}
		} else if (type == QEvent::TouchBegin
			|| type == QEvent::TouchUpdate
			|| type == QEvent::TouchEnd
			|| type == QEvent::TouchCancel) {
			if (handleTouchEvent(static_cast<QTouchEvent*>(e.get()))) {
				return base::EventFilterResult::Cancel;;
			}
		} else if (type == QEvent::Wheel) {
			handleWheelEvent(static_cast<QWheelEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});

	if (Platform::IsLinux()) {
		_widget->setWindowFlags(Qt::FramelessWindowHint
			| Qt::MaximizeUsingFullscreenGeometryHint);
	} else if (Platform::IsMac()) {
		// Without Qt::Tool starting with Qt 5.15.1 this widget
		// when being opened from a fullscreen main window was
		// opening not as overlay over the main window, but as
		// a separate fullscreen window with a separate space.
		_widget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
	} else {
		_widget->setWindowFlags(Qt::FramelessWindowHint);
	}
	_widget->setAttribute(Qt::WA_NoSystemBackground, true);
	_widget->setAttribute(Qt::WA_TranslucentBackground, true);
	_widget->setMouseTracking(true);

	hide();
	_widget->createWinId();
	if (Platform::IsLinux()) {
		window()->setTransientParent(App::wnd()->windowHandle());
		_widget->setWindowModality(Qt::WindowModal);
	}
	if (!Platform::IsMac()) {
		_widget->setWindowState(Qt::WindowFullScreen);
	}

	QObject::connect(
		window(),
		&QWindow::screenChanged,
		[=](QScreen *screen) { handleScreenChanged(screen); });
	subscribeToScreenGeometry();
	updateGeometry();
	updateControlsGeometry();

#if defined Q_OS_MAC && !defined OS_OSX
	TouchBar::SetupMediaViewTouchBar(
		_widget->winId(),
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

	_saveMsgUpdater.setCallback([=] { updateImage(); });

	_widget->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setCallback([=] { handleTouchTimer(); });

	_controlsHideTimer.setCallback([=] { hideControls(); });

	_docDownload->addClickHandler([=] { downloadMedia(); });
	_docSaveAs->addClickHandler([=] { saveAs(); });
	_docCancel->addClickHandler([=] { saveCancel(); });

	_dropdown->setHiddenCallback([this] { dropdownHidden(); });
	_dropdownShowTimer.setCallback([=] { showDropdown(); });
}

void OverlayWidget::refreshLang() {
	InvokeQueued(_widget, [=] { updateThemePreviewGeometry(); });
}

void OverlayWidget::moveToScreen() {
	const auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (auto handle = widget ? widget->windowHandle() : nullptr) {
			return handle->screen();
		}
		return nullptr;
	};
	const auto applicationWindow = Core::App().activeWindow()
		? Core::App().activeWindow()->widget().get()
		: nullptr;
	const auto activeWindowScreen = widgetScreen(applicationWindow);
	const auto myScreen = widgetScreen(_widget);
	if (activeWindowScreen && myScreen != activeWindowScreen) {
		const auto screenList = QGuiApplication::screens();
		DEBUG_LOG(("Viewer Pos: Currently on screen %1, moving to screen %2")
			.arg(screenList.indexOf(myScreen))
			.arg(screenList.indexOf(activeWindowScreen)));
		window()->setScreen(activeWindowScreen);
		DEBUG_LOG(("Viewer Pos: New actual screen: %1")
			.arg(screenList.indexOf(window()->screen())));
	}
	updateGeometry();
}

void OverlayWidget::updateGeometry() {
	if (Platform::IsWayland()) {
		return;
	}
	const auto screen = window()->screen()
		? window()->screen()
		: QApplication::primaryScreen();
	const auto available = screen->geometry();
	const auto openglWidget = _opengl
		? static_cast<QOpenGLWidget*>(_widget.get())
		: nullptr;
	const auto useSizeHack = Platform::IsWindows()
		&& openglWidget
		&& (openglWidget->format().renderableType()
			!= QSurfaceFormat::OpenGLES);
	const auto use = useSizeHack
		? available.marginsAdded({ 0, 0, 0, 1 })
		: available;
	const auto mask = useSizeHack
		? QRegion(QRect(QPoint(), available.size()))
		: QRegion();
	if ((_widget->geometry() == use)
		&& (!useSizeHack || window()->mask() == mask)) {
		return;
	}
	DEBUG_LOG(("Viewer Pos: Setting %1, %2, %3, %4")
		.arg(use.x())
		.arg(use.y())
		.arg(use.width())
		.arg(use.height()));
	_widget->setGeometry(use);
	if (useSizeHack) {
		window()->setMask(mask);
	}
}

void OverlayWidget::updateControlsGeometry() {
	auto navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
	_closeNav = QRect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, st::mediaviewControlMargin, st::mediaviewControlSize, st::mediaviewControlSize);
	_closeNavIcon = style::centerrect(_closeNav, st::mediaviewClose);
	_leftNav = QRect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_leftNavIcon = style::centerrect(_leftNav, st::mediaviewLeft);
	_rightNav = QRect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_rightNavIcon = style::centerrect(_rightNav, st::mediaviewRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
	_photoRadialRect = QRect(QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2), st::radialSize);

	updateControls();
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

Streaming::FrameWithInfo OverlayWidget::videoFrameWithInfo() const {
	Expects(videoShown());

	return _streamed->instance.player().ready()
		? _streamed->instance.frameWithInfo()
		: Streaming::FrameWithInfo{
			.original = _streamed->instance.info().video.cover,
			.format = Streaming::FrameFormat::ARGB32,
			.index = -2,
		};
}

QImage OverlayWidget::currentVideoFrameImage() const {
	return _streamed->instance.player().ready()
		? _streamed->instance.player().currentFrameImage()
		: _streamed->instance.info().video.cover;
}

int OverlayWidget::streamedIndex() const {
	return _streamedCreated;
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

void OverlayWidget::setStaticContent(QImage image) {
	constexpr auto kGood = QImage::Format_ARGB32_Premultiplied;
	if (!image.isNull()
		&& image.format() != kGood
		&& image.format() != QImage::Format_RGB32) {
		image = std::move(image).convertToFormat(kGood);
	}
	image.setDevicePixelRatio(cRetinaFactor());
	_staticContent = std::move(image);
	_staticContentTransparent = IsSemitransparent(_staticContent);
}

bool OverlayWidget::contentShown() const {
	return _photo || documentContentShown();
}

bool OverlayWidget::opaqueContentShown() const {
	return contentShown()
		&& (!_staticContentTransparent
			|| !_document
			|| (!_document->isVideoMessage() && !_document->sticker()));
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
				_widget->update(_docRect);
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
		downloadMedia();
	} else if (_savePhotoVideoWhenLoaded == SavePhotoVideo::SaveAs) {
		_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
		saveAs();
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
		return QRect(width() - st::mediaviewIconSize.width() * i,
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
		_nameNav = QRect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, qMin(_fromNameLabel.maxWidth(), width() / 3), st::mediaviewFont->height);
		_dateNav = QRect(st::mediaviewTextLeft + _nameNav.width() + st::mediaviewTextSkip, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = QRect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	}
	updateHeader();
	refreshNavVisibility();
	resizeCenteredControls();

	updateOver(_widget->mapFromGlobal(QCursor::pos()));
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

void OverlayWidget::fillContextMenuActions(const MenuCallback &addAction) {
	if (_document && _document->loading()) {
		addAction(tr::lng_cancel(tr::now), [=] { saveCancel(); });
	}
	if (IsServerMsgId(_msgid.msg)) {
		addAction(tr::lng_context_to_msg(tr::now), [=] { toMessage(); });
	}
	if (_document && !_document->filepath(true).isEmpty()) {
		const auto text =  Platform::IsMac()
			? tr::lng_context_show_in_finder(tr::now)
			: tr::lng_context_show_in_folder(tr::now);
		addAction(text, [=] { showInFolder(); });
	}
	if ((_document && documentContentShown()) || (_photo && _photoMedia->loaded())) {
		addAction(tr::lng_mediaview_copy(tr::now), [=] { copyMedia(); });
	}
	if ((_photo && _photo->hasAttachedStickers())
		|| (_document && _document->hasAttachedStickers())) {
		addAction(
			tr::lng_context_attached_stickers(tr::now),
			[=] { showAttachedStickers(); });
	}
	if (_canForwardItem) {
		addAction(tr::lng_mediaview_forward(tr::now), [=] { forwardMedia(); });
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
		addAction(tr::lng_mediaview_delete(tr::now), [=] { deleteMedia(); });
	}
	addAction(tr::lng_mediaview_save_as(tr::now), [=] { saveAs(); });

	if (const auto overviewType = computeOverviewType()) {
		const auto text = _document
			? tr::lng_mediaview_files_all(tr::now)
			: tr::lng_mediaview_photos_all(tr::now);
		addAction(text, [=] { showMediaOverview(); });
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

int OverlayWidget::finalContentRotation() const {
	return _streamed
		? ((_rotation + (_streamed
			? _streamed->instance.info().video.rotation
			: 0)) % 360)
		: _rotation;
}

QRect OverlayWidget::finalContentRect() const {
	return { _x, _y, _w, _h };
}

OverlayWidget::ContentGeometry OverlayWidget::contentGeometry() const {
	const auto toRotation = qreal(finalContentRotation());
	const auto toRectRotated = QRectF(finalContentRect());
	const auto toRectCenter = toRectRotated.center();
	const auto toRect = ((int(toRotation) % 180) == 90)
		? QRectF(
			toRectCenter.x() - toRectRotated.height() / 2.,
			toRectCenter.y() - toRectRotated.width() / 2.,
			toRectRotated.height(),
			toRectRotated.width())
		: toRectRotated;
	if (!_geometryAnimation.animating()) {
		return { toRect, toRotation };
	}
	const auto fromRect = _oldGeometry.rect;
	const auto fromRotation = _oldGeometry.rotation;
	const auto progress = _geometryAnimation.value(1.);
	const auto rotationDelta = (toRotation - fromRotation);
	const auto useRotationDelta = (rotationDelta > 180.)
		? (rotationDelta - 360.)
		: (rotationDelta <= -180.)
		? (rotationDelta + 360.)
		: rotationDelta;
	const auto rotation = fromRotation + useRotationDelta * progress;
	const auto useRotation = (rotation > 360.)
		? (rotation - 360.)
		: (rotation < 0.)
		? (rotation + 360.)
		: rotation;
	const auto useRect = QRectF(
		fromRect.x() + (toRect.x() - fromRect.x()) * progress,
		fromRect.y() + (toRect.y() - fromRect.y()) * progress,
		fromRect.width() + (toRect.width() - fromRect.width()) * progress,
		fromRect.height() + (toRect.height() - fromRect.height()) * progress
	);
	return { useRect, useRotation };
}

void OverlayWidget::updateContentRect() {
	if (_opengl) {
		update();
	} else {
		update(finalContentRect());
	}
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
	_geometryAnimation.stop();
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

rpl::lifetime &OverlayWidget::lifetime() {
	return _surface->lifetime();
}

void OverlayWidget::showSaveMsgFile() {
	File::ShowInFolder(_saveMsgFilename);
}

void OverlayWidget::close() {
	Core::App().hideMediaView();
}

void OverlayWidget::activateControls() {
	if (!_menu && !_mousePressed) {
		_controlsHideTimer.callOnce(st::mediaviewWaitHide);
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

void OverlayWidget::hideControls(bool force) {
	if (!force) {
		if (!_dropdown->isHidden()
			|| (_streamed && _streamed->controls.hasMenu())
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

	_lastMouseMovePos = _widget->mapFromGlobal(QCursor::pos());
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
	_lastMouseMovePos = _widget->mapFromGlobal(QCursor::pos());
	updateOver(_lastMouseMovePos);
	_ignoringDropdown = false;
	if (!_controlsHideTimer.isActive()) {
		hideControls(true);
	}
}

void OverlayWidget::handleScreenChanged(QScreen *screen) {
	subscribeToScreenGeometry();
	if (isHidden()) {
		return;
	}

	const auto screenList = QGuiApplication::screens();
	DEBUG_LOG(("Viewer Pos: Screen changed to: %1")
		.arg(screenList.indexOf(screen)));

	moveToScreen();
}

void OverlayWidget::subscribeToScreenGeometry() {
	_screenGeometryLifetime.destroy();
	const auto screen = window()->screen();
	if (!screen) {
		return;
	}
	base::qt_signal_producer(
		screen,
		&QScreen::geometryChanged
	) | rpl::start_with_next([=] {
		updateGeometry();
	}, _screenGeometryLifetime);
}

void OverlayWidget::toMessage() {
	if (!_session) {
		return;
	}

	if (const auto item = _session->data().message(_msgid)) {
		close();
		if (const auto window = findWindow()) {
			window->showPeerHistoryAtItem(item);
		}
	}
}

void OverlayWidget::notifyFileDialogShown(bool shown) {
	if (shown && isHidden()) {
		return;
	}
	if (shown) {
		Ui::Platform::BringToBack(_widget);
	} else {
		Ui::Platform::ShowOverAll(_widget);
	}
}

void OverlayWidget::saveAs() {
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
			const auto photo = _photo;
			auto filter = qsl("Video Files (*.mp4);;") + FileDialog::AllFilesFilter();
			FileDialog::GetWritePath(
				_widget.get(),
				tr::lng_save_video(tr::now),
				filter,
				filedialogDefaultName(
					qsl("photo"),
					qsl(".mp4"),
					QString(),
					false,
					_photo->date),
				crl::guard(_widget, [=](const QString &result) {
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
		const auto photo = _photo;
		auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
		FileDialog::GetWritePath(
			_widget.get(),
			tr::lng_save_photo(tr::now),
			filter,
			filedialogDefaultName(
				qsl("photo"),
				qsl(".jpg"),
				QString(),
				false,
				_photo->date),
			crl::guard(_widget, [=](const QString &result) {
				if (!result.isEmpty() && _photo == photo) {
					image.save(result, "JPG");
				}
			}));
	}
	activate();
}

void OverlayWidget::handleDocumentClick() {
	if (_document->loading()) {
		saveCancel();
	} else {
		Data::ResolveDocument(
			findWindow(),
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

void OverlayWidget::downloadMedia() {
	if (!_photo && !_document) {
		return;
	}
	if (Core::App().settings().askDownloadPath()) {
		return saveAs();
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

void OverlayWidget::saveCancel() {
	if (_document && _document->loading()) {
		_document->cancel();
		if (_documentMedia->canBePlayed()) {
			redisplayContent();
		}
	}
}

void OverlayWidget::showInFolder() {
	if (!_document) return;

	auto filepath = _document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
		close();
	}
}

void OverlayWidget::forwardMedia() {
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

void OverlayWidget::deleteMedia() {
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

void OverlayWidget::showMediaOverview() {
	if (_menu) {
		_menu->hideMenu(true);
	}
	update();
	if (const auto overviewType = computeOverviewType()) {
		close();
		SharedMediaShowOverview(*overviewType, _history);
	}
}

void OverlayWidget::copyMedia() {
	_dropdown->hideAnimated(Ui::DropdownMenu::HideOption::IgnoreShow);
	if (_document) {
		QGuiApplication::clipboard()->setImage(transformedShownContent());
	} else if (_photo && _photoMedia->loaded()) {
		const auto image = _photoMedia->image(
			Data::PhotoSize::Large)->original();
		QGuiApplication::clipboard()->setImage(image);
	}
}

void OverlayWidget::showAttachedStickers() {
	if (!_session) {
		return;
	}
	const auto &active = _session->windows();
	if (active.empty()) {
		return;
	}
	const auto window = active.front();
	auto &attachedStickers = _session->api().attachedStickers();
	if (_photo) {
		attachedStickers.requestAttachedStickerSets(window, _photo);
	} else if (_document) {
		attachedStickers.requestAttachedStickerSets(window, _document);
	} else {
		return;
	}
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
	const auto isServerMsgId = IsServerMsgId(_msgid.msg);
	const auto isScheduled = [&] {
		if (isServerMsgId) {
			return false;
		}
		if (const auto item = _session->data().message(_msgid)) {
			return item->isScheduled();
		}
		return false;
	}();
	const auto keyForType = [&](SharedMediaType type) -> SharedMediaKey {
		return {
			_history->peer->id,
			_migrated ? _migrated->peer->id : 0,
			type,
			(_msgid.channel == _history->channelId())
				? _msgid.msg
				: (_msgid.msg - ServerMaxMsgId),
			isScheduled
		};
	};
	if (!isServerMsgId && !isScheduled) {
		return std::nullopt;
	}
	return sharedMediaType() | keyForType;
}

Data::FileOrigin OverlayWidget::fileOrigin() const {
	if (_msgid) {
		return _msgid;
	} else if (_photo && _user) {
		return Data::FileOriginUserPhoto(peerToUser(_user->id), _photo->id);
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
		return Data::FileOriginUserPhoto(peerToUser(_user->id), photo->id);
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
				&& (a.migratedPeerId == b.migratedPeerId)
				&& (a.scheduled == b.scheduled);
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
		return UserPhotosKey{ peerToUser(_user->id), _photo->id };
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

not_null<QWindow*> OverlayWidget::window() const {
	return _widget->windowHandle();
}

int OverlayWidget::width() const {
	return _widget->width();
}

int OverlayWidget::height() const {
	return _widget->height();
}

void OverlayWidget::update() {
	_widget->update();
}

void OverlayWidget::update(const QRegion &region) {
	_widget->update(region);
}

bool OverlayWidget::isHidden() const {
	return _widget->isHidden();
}

not_null<QWidget*> OverlayWidget::widget() const {
	return _widget;
}

void OverlayWidget::hide() {
	clearBeforeHide();
	applyHideWindowWorkaround();
	_widget->hide();
}

void OverlayWidget::setCursor(style::cursor cursor) {
	_widget->setCursor(cursor);
}

void OverlayWidget::setFocus() {
	_widget->setFocus();
}

void OverlayWidget::activate() {
	_widget->raise();
	_widget->activateWindow();
	QApplication::setActiveWindow(_widget);
	setFocus();
}

void OverlayWidget::show(OpenRequest request) {
	const auto document = request.document();
	const auto photo = request.photo();
	const auto contextItem = request.item();
	const auto contextPeer = request.peer();
	if (photo) {
		if (contextItem && contextPeer) {
			return;
		}
		setSession(&photo->session());

		if (contextPeer) {
			setContext(contextPeer);
		} else if (contextItem) {
			setContext(contextItem);
		} else {
			setContext(v::null);
		}

		clearControlsState();
		_firstOpenedPeerPhoto = (contextPeer != nullptr);
		assignMediaPointer(photo);

		displayPhoto(photo, contextPeer ? nullptr : contextItem);
		preloadData(0);
		activateControls();
	} else if (document) {
		setSession(&document->session());

		if (contextItem) {
			setContext(contextItem);
		} else {
			setContext(v::null);
		}

		clearControlsState();

		_streamingStartPaused = false;
		displayDocument(
			document,
			contextItem,
			request.cloudTheme()
				? *request.cloudTheme()
				: Data::CloudTheme(),
			request.continueStreaming());
		if (!isHidden()) {
			preloadData(0);
			activateControls();
		}
	}
	if (const auto controller = request.controller()) {
		_window = base::make_weak(&controller->window());
	}
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

	_staticContent = QImage();
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
	_staticContent = QImage();
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
				setStaticContent(image->original());
			} else if (const auto thumbnail = _documentMedia->thumbnail()) {
				setStaticContent(thumbnail->pixBlurred(
					_document->dimensions.width(),
					_document->dimensions.height()
				).toImage());
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
						setStaticContent(PrepareStaticImage(path));
						_touchbarDisplay.fire(TouchBarItemType::Photo);
					}
				} else if (!_documentMedia->bytes().isEmpty()) {
					setStaticContent(
						PrepareStaticImage(_documentMedia->bytes()));
					if (!_staticContent.isNull()) {
						_touchbarDisplay.fire(TouchBarItemType::Photo);
					}
				}
				location.accessDisable();
			}
		}
	}
	refreshCaption(item);

	_docIconRect = QRect((width() - st::mediaviewFileIconSize) / 2, (height() - st::mediaviewFileIconSize) / 2, st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	int32 colorIndex = documentColorIndex(_document, _docExt);
	_docIconColor = documentColor(colorIndex);
	const style::icon *thumbs[] = { &st::mediaviewFileBlue, &st::mediaviewFileGreen, &st::mediaviewFileRed, &st::mediaviewFileYellow };
	_docIcon = thumbs[colorIndex];

	int32 extmaxw = (st::mediaviewFileIconSize - st::mediaviewFileExtPadding * 2);
	_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
	if (_docExtWidth > extmaxw) {
		_docExt = st::mediaviewFileExtFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
		_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
	}
	if (documentBubbleShown()) {
		if (_document && _document->hasThumbnail()) {
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
		_docIconRect = QRect(_docRect.x() + st::mediaviewFilePadding, _docRect.y() + st::mediaviewFilePadding, st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	} else if (_themePreviewShown) {
		updateThemePreviewGeometry();
	} else if (!_staticContent.isNull()) {
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
	if (videoShown()) {
		applyVideoSize();
	}
	refreshFromLabel(item);
	_blurred = false;
	if (_showAsPip && _streamed && !videoIsGifOrUserpic()) {
		switchToPip();
	} else {
		displayFinished();
	}
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
		moveToScreen();
		//setAttribute(Qt::WA_DontShowOnScreen);
		//OverlayParent::setVisibleHook(true);
		//OverlayParent::setVisibleHook(false);
		//setAttribute(Qt::WA_DontShowOnScreen, false);
		Ui::Platform::UpdateOverlayed(_widget);
		if (Platform::IsLinux()) {
			_widget->showFullScreen();
		} else {
			_widget->show();
		}
		Ui::Platform::ShowOverAll(_widget);
		activate();
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

	const auto &player = _streamed->instance.player();
	if (player.playing()) {
		if (!_streamed->withSound) {
			return;
		}
		_pip = nullptr;
	} else if (!player.paused() && !player.finished() && !player.failed()) {
		_pip = nullptr;
	} else if (_pip && _streamed->withSound) {
		return;
	}

	const auto position = _document
		? _document->session().settings().mediaLastPlaybackPosition(
			_document->id)
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
	setStaticContent((good
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
			h / cIntRetinaFactor()
		).toImage());
}

void OverlayWidget::streamingReady(Streaming::Information &&info) {
	if (videoShown()) {
		applyVideoSize();
	} else {
		updateContentRect();
	}
}

void OverlayWidget::applyVideoSize() {
	const auto contentSize = style::ConvertScale(videoSize());
	if (contentSize != QSize(_width, _height)) {
		updateContentRect();
		_w = contentSize.width();
		_h = contentSize.height();
		contentSizeChanged();
	}
	updateContentRect();
}

bool OverlayWidget::createStreamingObjects() {
	Expects(_photo || _document);

	if (_document) {
		_streamed = std::make_unique<Streamed>(
			_document,
			fileOrigin(),
			_widget,
			static_cast<PlaybackControls::Delegate*>(this),
			[=] { waitingAnimationCallback(); });
	} else {
		_streamed = std::make_unique<Streamed>(
			_photo,
			fileOrigin(),
			_widget,
			static_cast<PlaybackControls::Delegate*>(this),
			[=] { waitingAnimationCallback(); });
	}
	if (!_streamed->instance.valid()) {
		_streamed = nullptr;
		return false;
	}
	++_streamedCreated;
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

QImage OverlayWidget::transformedShownContent() const {
	return transformShownContent(
		videoShown() ? currentVideoFrameImage() : _staticContent,
		finalContentRotation());
}

QImage OverlayWidget::transformShownContent(
		QImage content,
		int rotation) const {
	if (rotation) {
		content = RotateFrameImage(std::move(content), rotation);
	}
	if (videoShown()) {
		const auto requiredSize = videoSize();
		if (content.size() != requiredSize) {
			content = content.scaled(
				requiredSize,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
	}
	return content;
}

void OverlayWidget::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
		updatePlaybackState();
	}, [&](const UpdateVideo &update) {
		updateContentRect();
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
	const auto id = _themePreviewId = openssl::RandomValue<uint64>();
	const auto weak = Ui::MakeWeak(_widget);
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
					_widget,
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
					_widget,
					tr::lng_cancel(),
					st::themePreviewCancelButton);
				_themeCancel->show();
				_themeCancel->setClickedCallback([this] { close(); });
				if (const auto slug = _themeCloudData.slug; !slug.isEmpty()) {
					_themeShare.create(
						_widget,
						tr::lng_theme_share(),
						st::themePreviewCancelButton);
					_themeShare->show();
					_themeShare->setClickedCallback([=] {
						QGuiApplication::clipboard()->setText(
							session->createInternalLinkFull("addtheme/" + slug));
						Ui::Toast::Show(
							_widget,
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
	_oldGeometry = contentGeometry();
	_geometryAnimation.stop();
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
		} else {
			redisplayContent();
		}
	}
	if (_opengl) {
		_geometryAnimation.start(
			[=] { update(); },
			0.,
			1.,
			st::widgetFadeDuration/*,
			st::easeOutCirc*/);
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
		setStaticContent(transformedShownContent());
		_rotation = saved;
		updateContentRect();
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
	if (_streamed) {
		Player::mixer()->setVideoVolume(volume);
	}
	Core::App().settings().setVideoVolume(volume);
	Core::App().saveSettingsDelayed();
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
		_showAsPip = false;
		show(OpenRequest(
			findWindow(),
			document,
			document->owner().message(msgId),
			true));
	};
	_showAsPip = true;
	_pip = std::make_unique<PipWrap>(
		_widget,
		document,
		msgId,
		_streamed->instance.shared(),
		closeAndContinue,
		[=] { _pip = nullptr; });
	if (isHidden()) {
		clearBeforeHide();
		clearAfterHide();
	} else {
		close();
		if (const auto window = Core::App().activeWindow()) {
			window->activate();
		}
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
	setStaticContent(image->pixNoCache(
		use.width(),
		use.height(),
		Images::Option::Smooth
		| (blurred ? Images::Option::Blurred : Images::Option(0))
	).toImage());
	_blurred = blurred;
}

void OverlayWidget::validatePhotoCurrentImage() {
	if (!_photo) {
		return;
	}
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

Ui::GL::ChosenRenderer OverlayWidget::chooseRenderer(
		Ui::GL::Capabilities capabilities) {
	const auto use = Platform::IsMac()
		? true
		: capabilities.transparency;
	LOG(("OpenGL: %1 (OverlayWidget)").arg(Logs::b(use)));
	if (use) {
		_opengl = true;
		return {
			.renderer = std::make_unique<RendererGL>(this),
			.backend = Ui::GL::Backend::OpenGL,
		};
	}
	return {
		.renderer = std::make_unique<RendererSW>(this),
		.backend = Ui::GL::Backend::Raster,
	};
}

void OverlayWidget::paint(not_null<Renderer*> renderer) {
	renderer->paintBackground();
	if (contentShown()) {
		if (videoShown()) {
			renderer->paintTransformedVideoFrame(contentGeometry());
			if (_streamed->instance.player().ready()) {
				_streamed->instance.markFrameShown();
			}
		} else {
			validatePhotoCurrentImage();
			const auto fillTransparentBackground = (!_document
				|| (!_document->sticker() && !_document->isVideoMessage()))
				&& _staticContentTransparent;
			renderer->paintTransformedStaticContent(
				_staticContent,
				contentGeometry(),
				_staticContentTransparent,
				fillTransparentBackground);
		}
		paintRadialLoading(renderer);
	} else {
		if (_themePreviewShown) {
			renderer->paintThemePreview(_themePreviewRect);
		} else if (documentBubbleShown() && !_docRect.isEmpty()) {
			renderer->paintDocumentBubble(_docRect, _docIconRect);
		}
	}
	updateSaveMsgState();
	if (_saveMsgStarted && _saveMsgOpacity.current() > 0.) {
		renderer->paintSaveMsg(_saveMsg);
	}

	const auto opacity = _fullScreenVideo ? 0. : _controlsOpacity.current();
	if (opacity > 0) {
		paintControls(renderer, opacity);
		renderer->paintFooter(footerGeometry(), opacity);
		if (!_caption.isEmpty()) {
			renderer->paintCaption(captionGeometry(), opacity);
		}
		if (_groupThumbs) {
			renderer->paintGroupThumbs(
				QRect(
					_groupThumbsLeft,
					_groupThumbsTop,
					width() - 2 * _groupThumbsLeft,
					_groupThumbs->height()),
				opacity);
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

void OverlayWidget::paintRadialLoading(not_null<Renderer*> renderer) {
	const auto radial = _radial.animating();
	if (_streamed) {
		if (!_streamed->instance.waitingShown()) {
			return;
		}
	} else if (!radial && (!_document || _documentMedia->loaded())) {
		return;
	}

	const auto radialOpacity = radial ? _radial.opacity() : 0.;
	const auto inner = radialRect();
	Assert(!inner.isEmpty());

	renderer->paintRadialLoading(inner, radial, radialOpacity);
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

void OverlayWidget::paintThemePreviewContent(
		Painter &p,
		QRect outer,
		QRect clip) {
	const auto fill = outer.intersected(clip);
	if (!fill.isEmpty()) {
		if (_themePreview) {
			p.drawImage(
				outer.topLeft(),
				_themePreview->preview);
		} else {
			p.fillRect(fill, st::themePreviewBg);
			p.setFont(st::themePreviewLoadingFont);
			p.setPen(st::themePreviewLoadingFg);
			p.drawText(
				outer,
				(_themePreviewId
					? tr::lng_theme_preview_generating(tr::now)
					: tr::lng_theme_preview_invalid(tr::now)),
				QTextOption(style::al_center));
		}
	}

	const auto fillOverlay = [&](QRect fill) {
		const auto clipped = fill.intersected(clip);
		if (!clipped.isEmpty()) {
			p.setOpacity(st::themePreviewOverlayOpacity);
			p.fillRect(clipped, st::themePreviewBg);
			p.setOpacity(1.);
		}
	};
	auto titleRect = QRect(
		outer.x(),
		outer.y(),
		outer.width(),
		st::themePreviewMargin.top());
	if (titleRect.x() < 0) {
		titleRect = QRect(
			0,
			outer.y(),
			width(),
			st::themePreviewMargin.top());
	}
	if (titleRect.y() < 0) {
		titleRect.moveTop(0);
		fillOverlay(titleRect);
	}
	titleRect = titleRect.marginsRemoved(QMargins(
		st::themePreviewMargin.left(),
		st::themePreviewTitleTop,
		st::themePreviewMargin.right(),
		(titleRect.height()
			- st::themePreviewTitleTop
			- st::themePreviewTitleFont->height)));
	if (titleRect.intersects(clip)) {
		p.setFont(st::themePreviewTitleFont);
		p.setPen(st::themePreviewTitleFg);
		const auto title = _themeCloudData.title.isEmpty()
			? tr::lng_theme_preview_title(tr::now)
			: _themeCloudData.title;
		const auto elided = st::themePreviewTitleFont->elided(
			title,
			titleRect.width());
		p.drawTextLeft(titleRect.x(), titleRect.y(), width(), elided);
	}

	auto buttonsRect = QRect(
		outer.x(),
		outer.y() + outer.height() - st::themePreviewMargin.bottom(),
		outer.width(),
		st::themePreviewMargin.bottom());
	if (buttonsRect.y() + buttonsRect.height() > height()) {
		buttonsRect.moveTop(height() - buttonsRect.height());
		fillOverlay(buttonsRect);
	}
	if (_themeShare && _themeCloudData.usersCount > 0) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		const auto left = outer.x()
			+ (_themeShare->x() - _themePreviewRect.x())
			+ _themeShare->width()
			- (st::themePreviewCancelButton.width / 2);
		const auto baseline = outer.y()
			+ (_themeShare->y() - _themePreviewRect.y())
			+ st::themePreviewCancelButton.padding.top()
			+ st::themePreviewCancelButton.textTop
			+ st::themePreviewCancelButton.font->ascent;
		p.drawText(
			left,
			baseline,
			tr::lng_theme_preview_users(
				tr::now,
				lt_count,
				_themeCloudData.usersCount));
	}
}

void OverlayWidget::paintDocumentBubbleContent(
		Painter &p,
		QRect outer,
		QRect icon,
		QRect clip) const {
	p.fillRect(outer, st::mediaviewFileBg);
	if (icon.intersects(clip)) {
		if (!_document || !_document->hasThumbnail()) {
			p.fillRect(icon, _docIconColor);
			const auto radial = _radial.animating();
			const auto radialOpacity = radial ? _radial.opacity() : 0.;
			if ((!_document || _documentMedia->loaded()) && (!radial || radialOpacity < 1) && _docIcon) {
				_docIcon->paint(p, icon.x() + (icon.width() - _docIcon->width()), icon.y(), width());
				p.setPen(st::mediaviewFileExtFg);
				p.setFont(st::mediaviewFileExtFont);
				if (!_docExt.isEmpty()) {
					p.drawText(icon.x() + (icon.width() - _docExtWidth) / 2, icon.y() + st::mediaviewFileExtTop + st::mediaviewFileExtFont->ascent, _docExt);
				}
			}
		} else if (const auto thumbnail = _documentMedia->thumbnail()) {
			int32 rf(cIntRetinaFactor());
			p.drawPixmap(icon.topLeft(), thumbnail->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mediaviewFileIconSize * rf, st::mediaviewFileIconSize * rf));
		}
	}
	if (!icon.contains(clip)) {
		p.setPen(st::mediaviewFileNameFg);
		p.setFont(st::mediaviewFileNameFont);
		p.drawTextLeft(outer.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, outer.y() + st::mediaviewFilePadding + st::mediaviewFileNameTop, width(), _docName, _docNameWidth);

		p.setPen(st::mediaviewFileSizeFg);
		p.setFont(st::mediaviewFont);
		p.drawTextLeft(outer.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, outer.y() + st::mediaviewFilePadding + st::mediaviewFileSizeTop, width(), _docSize, _docSizeWidth);
	}
}

void OverlayWidget::paintSaveMsgContent(
		Painter &p,
		QRect outer,
		QRect clip) {
	p.setOpacity(_saveMsgOpacity.current());
	Ui::FillRoundRect(p, outer, st::mediaviewSaveMsgBg, Ui::MediaviewSaveCorners);
	st::mediaviewSaveMsgCheck.paint(p, outer.topLeft() + st::mediaviewSaveMsgCheckPos, width());

	p.setPen(st::mediaviewSaveMsgFg);
	p.setTextPalette(st::mediaviewTextPalette);
	_saveMsgText.draw(p, outer.x() + st::mediaviewSaveMsgPadding.left(), outer.y() + st::mediaviewSaveMsgPadding.top(), outer.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
	p.restoreTextPalette();
	p.setOpacity(1);
}

void OverlayWidget::paintControls(
		not_null<Renderer*> renderer,
		float64 opacity) {
	struct Control {
		OverState state = OverNone;
		bool visible = false;
		const QRect &outer;
		const QRect &inner;
		const style::icon &icon;
	};
	const QRect kEmpty;
	// When adding / removing controls please update RendererGL.
	const Control controls[] = {
		{
			OverLeftNav,
			_leftNavVisible,
			_leftNav,
			_leftNavIcon,
			st::mediaviewLeft },
		{
			OverRightNav,
			_rightNavVisible,
			_rightNav,
			_rightNavIcon,
			st::mediaviewRight },
		{
			OverClose,
			true,
			_closeNav,
			_closeNavIcon,
			st::mediaviewClose },
		{
			OverSave,
			_saveVisible,
			kEmpty,
			_saveNavIcon,
			st::mediaviewSave },
		{
			OverRotate,
			_rotateVisible,
			kEmpty,
			_rotateNavIcon,
			st::mediaviewRotate },
		{
			OverMore,
			true,
			kEmpty,
			_moreNavIcon,
			st::mediaviewMore },
	};

	renderer->paintControlsStart();
	for (const auto &control : controls) {
		if (!control.visible) {
			continue;
		}
		const auto bg = overLevel(control.state);
		const auto icon = bg * st::mediaviewIconOverOpacity
			+ (1 - bg) * st::mediaviewIconOpacity;
		renderer->paintControl(
			control.state,
			control.outer,
			bg * opacity,
			control.inner,
			icon * opacity,
			control.icon);
	}
}

void OverlayWidget::paintFooterContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity) {
	p.setPen(st::mediaviewControlFg);
	p.setFont(st::mediaviewThickFont);

	// header
	const auto shift = outer.topLeft() - _headerNav.topLeft();
	const auto header = _headerNav.translated(shift);
	const auto name = _nameNav.translated(shift);
	const auto date = _dateNav.translated(shift);
	if (header.intersects(clip)) {
		auto o = _headerHasLink ? overLevel(OverHeader) : 0;
		p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * opacity);
		p.drawText(header.left(), header.top() + st::mediaviewThickFont->ascent, _headerText);

		if (o > 0) {
			p.setOpacity(o * opacity);
			p.drawLine(header.left(), header.top() + st::mediaviewThickFont->ascent + 1, header.right(), header.top() + st::mediaviewThickFont->ascent + 1);
		}
	}

	p.setFont(st::mediaviewFont);

	// name
	if (_nameNav.isValid() && name.intersects(clip)) {
		float64 o = _from ? overLevel(OverName) : 0.;
		p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * opacity);
		_fromNameLabel.drawElided(p, name.left(), name.top(), name.width());

		if (o > 0) {
			p.setOpacity(o * opacity);
			p.drawLine(name.left(), name.top() + st::mediaviewFont->ascent + 1, name.right(), name.top() + st::mediaviewFont->ascent + 1);
		}
	}

	// date
	if (date.intersects(clip)) {
		float64 o = overLevel(OverDate);
		p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * opacity);
		p.drawText(date.left(), date.top() + st::mediaviewFont->ascent, _dateText);

		if (o > 0) {
			p.setOpacity(o * opacity);
			p.drawLine(date.left(), date.top() + st::mediaviewFont->ascent + 1, date.right(), date.top() + st::mediaviewFont->ascent + 1);
		}
	}
}

QRect OverlayWidget::footerGeometry() const {
	return _headerNav.united(_nameNav).united(_dateNav);
}

void OverlayWidget::paintCaptionContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity) {
	const auto inner = outer.marginsRemoved(st::mediaviewCaptionPadding);
	p.setOpacity(opacity);
	p.setBrush(st::mediaviewCaptionBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(outer, st::mediaviewCaptionRadius, st::mediaviewCaptionRadius);
	if (inner.intersects(clip)) {
		p.setTextPalette(st::mediaviewTextPalette);
		p.setPen(st::mediaviewCaptionFg);
		_caption.drawElided(p, inner.x(), inner.y(), inner.width(), inner.height() / st::mediaviewCaptionStyle.font->height);
		p.restoreTextPalette();
	}
}

QRect OverlayWidget::captionGeometry() const {
	return _captionRect.marginsAdded(st::mediaviewCaptionPadding);
}

void OverlayWidget::paintGroupThumbsContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity) {
	p.setOpacity(opacity);
	_groupThumbs->paint(p, outer.x(), outer.y(), width());
	if (_groupThumbs->hidden()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
}

void OverlayWidget::updateSaveMsgState() {
	if (!_saveMsgStarted) {
		return;
	}
	float64 dt = float64(crl::now()) - _saveMsgStarted;
	float64 hidingDt = dt - st::mediaviewSaveMsgShowing - st::mediaviewSaveMsgShown;
	if (dt >= st::mediaviewSaveMsgShowing
		+ st::mediaviewSaveMsgShown
		+ st::mediaviewSaveMsgHiding) {
		_saveMsgStarted = 0;
		return;
	}
	if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5) {
		_saveMsgOpacity.start(0);
	}
	float64 progress = (hidingDt >= 0) ? (hidingDt / st::mediaviewSaveMsgHiding) : (dt / st::mediaviewSaveMsgShowing);
	_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
	if (!_blurred) {
		const auto nextFrame = (dt < st::mediaviewSaveMsgShowing || hidingDt >= 0)
			? int(AnimationTimerDelta)
			: (st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + 1 - dt);
		_saveMsgUpdater.callOnce(nextFrame);
	}
}

void OverlayWidget::handleKeyPress(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	const auto modifiers = e->modifiers();
	const auto ctrl = modifiers.testFlag(Qt::ControlModifier);
	if (_streamed) {
		// Ctrl + F for full screen toggle is in eventFilter().
		const auto toggleFull = (modifiers.testFlag(Qt::AltModifier) || ctrl)
			&& (key == Qt::Key_Enter || key == Qt::Key_Return);
		if (toggleFull) {
			playbackToggleFullScreen();
			return;
		} else if (key == Qt::Key_Space) {
			playbackPauseResume();
			return;
		} else if (_fullScreenVideo) {
			if (key == Qt::Key_Escape) {
				playbackToggleFullScreen();
			}
			return;
		}
	}
	if (!_menu && key == Qt::Key_Escape) {
		if (_document && _document->loading() && !_streamed) {
			handleDocumentClick();
		} else {
			close();
		}
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		saveAs();
	} else if (key == Qt::Key_Copy || (key == Qt::Key_C && ctrl)) {
		copyMedia();
	} else if (key == Qt::Key_Enter
		|| key == Qt::Key_Return
		|| key == Qt::Key_Space) {
		if (_streamed) {
			playbackPauseResume();
		} else if (_document
			&& !_document->loading()
			&& (documentBubbleShown() || !_documentMedia->loaded())) {
			handleDocumentClick();
		}
	} else if (key == Qt::Key_Left) {
		if (_controlsHideTimer.isActive()) {
			activateControls();
		}
		moveToNext(-1);
	} else if (key == Qt::Key_Right) {
		if (_controlsHideTimer.isActive()) {
			activateControls();
		}
		moveToNext(1);
	} else if (ctrl) {
		if (key == Qt::Key_Plus
			|| key == Qt::Key_Equal
			|| key == Qt::Key_Asterisk
			|| key == ']') {
			zoomIn();
		} else if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
			zoomOut();
		} else if (key == Qt::Key_0) {
			zoomReset();
		} else if (key == Qt::Key_I) {
			update();
		}
	}
}

void OverlayWidget::handleWheelEvent(not_null<QWheelEvent*> e) {
	constexpr auto step = int(QWheelEvent::DefaultDeltasPerStep);

	_verticalWheelDelta += e->angleDelta().y();
	while (qAbs(_verticalWheelDelta) >= step) {
		if (_verticalWheelDelta < 0) {
			_verticalWheelDelta += step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomOut();
			} else if (e->source() == Qt::MouseEventNotSynthesized) {
				moveToNext(1);
			}
		} else {
			_verticalWheelDelta -= step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomIn();
			} else if (e->source() == Qt::MouseEventNotSynthesized) {
				moveToNext(-1);
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
	_oldGeometry = contentGeometry();
	_geometryAnimation.stop();

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
	if (_opengl) {
		_geometryAnimation.start(
			[=] { update(); },
			0.,
			1.,
			st::widgetFadeDuration/*,
			anim::easeOutCirc*/);
	}
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
	_widget->setWindowIcon(Window::CreateIcon(session));

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
	auto from = *_index + (delta ? -delta : -1);
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

void OverlayWidget::handleMousePress(
		QPoint position,
		Qt::MouseButton button) {
	updateOver(position);
	if (_menu || !_receiveMouse) {
		return;
	}

	ClickHandler::pressed();

	if (button == Qt::LeftButton) {
		_down = OverNone;
		if (!ClickHandler::getPressed()) {
			if (_over == OverLeftNav && moveToNext(-1)) {
				_lastAction = position;
			} else if (_over == OverRightNav && moveToNext(1)) {
				_lastAction = position;
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
			} else if (!_saveMsg.contains(position) || !_saveMsgStarted) {
				_pressed = true;
				_dragging = 0;
				updateCursor();
				_mStart = position;
				_xStart = _x;
				_yStart = _y;
			}
		}
	} else if (button == Qt::MiddleButton) {
		zoomReset();
	}
	activateControls();
}

bool OverlayWidget::handleDoubleClick(
		QPoint position,
		Qt::MouseButton button) {
	updateOver(position);

	if (_over != OverVideo || !_streamed || button != Qt::LeftButton) {
		return false;
	}
	playbackToggleFullScreen();
	playbackPauseResume();
	return true;
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

void OverlayWidget::handleMouseMove(QPoint position) {
	updateOver(position);
	if (_lastAction.x() >= 0
		&& ((position - _lastAction).manhattanLength()
			>= st::mediaviewDeltaFromLastAction)) {
		_lastAction = QPoint(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction);
	}
	if (_pressed) {
		if (!_dragging
			&& ((position - _mStart).manhattanLength()
				>= QApplication::startDragDistance())) {
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
			_x = _xStart + (position - _mStart).x();
			_y = _yStart + (position - _mStart).y();
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
			_dropdownShowTimer.callOnce(0);
		} else {
			_dropdownShowTimer.cancel();
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
	} else if (documentContentShown() && finalContentRect().contains(pos)) {
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

void OverlayWidget::handleMouseRelease(
		QPoint position,
		Qt::MouseButton button) {
	updateOver(position);

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
		ActivateClickHandler(_widget, activated, button);
		return;
	}

	if (_over == OverName && _down == OverName) {
		if (_from) {
			close();
			Ui::showPeerProfile(_from);
		}
	} else if (_over == OverDate && _down == OverDate) {
		toMessage();
	} else if (_over == OverHeader && _down == OverHeader) {
		showMediaOverview();
	} else if (_over == OverSave && _down == OverSave) {
		downloadMedia();
	} else if (_over == OverRotate && _down == OverRotate) {
		playbackControlsRotate();
	} else if (_over == OverIcon && _down == OverIcon) {
		handleDocumentClick();
	} else if (_over == OverMore && _down == OverMore) {
		InvokeQueued(_widget, [=] { showDropdown(); });
	} else if (_over == OverClose && _down == OverClose) {
		close();
	} else if (_over == OverVideo && _down == OverVideo) {
		if (_streamed) {
			playbackPauseResume();
		}
	} else if (_pressed) {
		if (_dragging) {
			if (_dragging > 0) {
				_x = _xStart + (position - _mStart).x();
				_y = _yStart + (position - _mStart).y();
				snapXY();
				update();
			}
			_dragging = 0;
			setCursor(style::cur_default);
		} else if ((position - _lastAction).manhattanLength()
			>= st::mediaviewDeltaFromLastAction) {
			if (_themePreviewShown) {
				if (!_themePreviewRect.contains(position)) {
					close();
				}
			} else if (!_document
				|| documentContentShown()
				|| !documentBubbleShown()
				|| !_docRect.contains(position)) {
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

bool OverlayWidget::handleContextMenu(std::optional<QPoint> position) {
	if (position && !QRect(_x, _y, _w, _h).contains(*position)) {
		return false;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_widget,
		st::mediaviewPopupMenu);
	fillContextMenuActions([&] (const QString &text, Fn<void()> handler) {
		_menu->addAction(text, std::move(handler));
	});
	_menu->setDestroyedCallback(crl::guard(_widget, [=] {
		activateControls();
		_receiveMouse = false;
		InvokeQueued(_widget, [=] { receiveMouse(); });
	}));
	_menu->popup(QCursor::pos());
	activateControls();
	return true;
}

bool OverlayWidget::handleTouchEvent(not_null<QTouchEvent*> e) {
	if (e->device()->type() != QTouchDevice::TouchScreen) {
		return false;
	} else if (e->type() == QEvent::TouchBegin
		&& !e->touchPoints().isEmpty()
		&& _widget->childAt(
			_widget->mapFromGlobal(
				e->touchPoints().cbegin()->screenPos().toPoint()))) {
		return false;
	}
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) {
			break;
		}
		_touchTimer.callOnce(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress || e->touchPoints().isEmpty()) {
			break;
		}
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) {
			break;
		}
		auto weak = Ui::MakeWeak(_widget);
		if (!_touchMove) {
			const auto button = _touchRightButton
				? Qt::RightButton
				: Qt::LeftButton;
			const auto position = _widget->mapFromGlobal(_touchStart);

			if (weak) handleMousePress(position, button);
			if (weak) handleMouseRelease(position, button);
			if (weak && _touchRightButton) {
				handleContextMenu(position);
			}
		} else if (_touchMove) {
			if ((!_leftNavVisible || !_leftNav.contains(_widget->mapFromGlobal(_touchStart))) && (!_rightNavVisible || !_rightNav.contains(_widget->mapFromGlobal(_touchStart)))) {
				QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::mediaviewSwipeDistance || d.x() < -st::mediaviewSwipeDistance)) {
					moveToNext(d.x() > 0 ? -1 : 1);
				}
			}
		}
		if (weak) {
			_touchTimer.cancel();
			_touchPress = _touchMove = _touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.cancel();
	} break;
	}
	return true;
}

void OverlayWidget::toggleApplicationEventFilter(bool install) {
	if (!install) {
		_applicationEventFilter = nullptr;
		return;
	} else if (_applicationEventFilter) {
		return;
	}
	class Filter final : public QObject {
	public:
		explicit Filter(not_null<OverlayWidget*> owner) : _owner(owner) {
		}

	private:
		bool eventFilter(QObject *obj, QEvent *e) override {
			return obj && e && _owner->filterApplicationEvent(obj, e);
		}

		const not_null<OverlayWidget*> _owner;

	};

	_applicationEventFilter = std::make_unique<Filter>(this);
	qApp->installEventFilter(_applicationEventFilter.get());
}

bool OverlayWidget::filterApplicationEvent(
		not_null<QObject*> object,
		not_null<QEvent*> e) {
	const auto type = e->type();
	if (type == QEvent::ShortcutOverride) {
		const auto keyEvent = static_cast<QKeyEvent*>(e.get());
		const auto ctrl = keyEvent->modifiers().testFlag(Qt::ControlModifier);
		if (keyEvent->key() == Qt::Key_F && ctrl && _streamed) {
			playbackToggleFullScreen();
		}
		return true;
	} else if (type == QEvent::MouseMove
		|| type == QEvent::MouseButtonPress
		|| type == QEvent::MouseButtonRelease) {
		if (object->isWidgetType()
			&& _widget->isAncestorOf(static_cast<QWidget*>(object.get()))) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			const auto mousePosition = _widget->mapFromGlobal(
				mouseEvent->globalPos());
			const auto delta = (mousePosition - _lastMouseMovePos);
			auto activate = delta.manhattanLength()
				>= st::mediaviewDeltaFromLastAction;
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
	return false;
}

void OverlayWidget::applyHideWindowWorkaround() {
	// QOpenGLWidget can't properly destroy a child widget if it is hidden
	// exactly after that, the child is cached in the backing store.
	// So on next paint we force full backing store repaint.
	if (_opengl && !isHidden() && !_hideWorkaround) {
		_hideWorkaround = std::make_unique<Ui::RpWidget>(_widget);
		_hideWorkaround->setGeometry(_widget->rect());
		_hideWorkaround->show();
		_hideWorkaround->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter(_hideWorkaround.get()).fillRect(_hideWorkaround->rect(), QColor(0, 1, 0, 1));
			crl::on_main(_hideWorkaround.get(), [=] {
				_hideWorkaround.reset();
			});
		}, _hideWorkaround->lifetime());
		_hideWorkaround->update();

		if (Platform::IsWindows()) {
			Ui::Platform::UpdateOverlayed(_widget);
		}
	}
}

Window::SessionController *OverlayWidget::findWindow() const {
	if (!_session) {
		return nullptr;
	}

	const auto window = _window.get();
	if (window) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == _session) {
				return controller;
			}
		}
	}

	const auto &active = _session->windows();
	if (!active.empty()) {
		return active.front();
	} else if (window) {
		Window::SessionController *controllerPtr = nullptr;
		window->invokeForSessionController(
			&_session->account(),
			[&](not_null<Window::SessionController*> newController) {
				controllerPtr = newController;
			});
		return controllerPtr;
	}

	return nullptr;
}

// #TODO unite and check
void OverlayWidget::clearBeforeHide() {
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
	if (_menu) {
		_menu->hideMenu(true);
	}
	_controlsHideTimer.cancel();
	_controlsState = ControlsShown;
	_controlsOpacity = anim::value(1, 1);
	_groupThumbs = nullptr;
	_groupThumbsRect = QRect();
	for (const auto child : _widget->children()) {
		if (child->isWidgetType()) {
			static_cast<QWidget*>(child)->hide();
		}
	}
}

void OverlayWidget::clearAfterHide() {
	clearStreaming();
	destroyThemePreview();
	_radial.stop();
	_staticContent = QImage();
	_themePreview = nullptr;
	_themeApply.destroyDelayed();
	_themeCancel.destroyDelayed();
	_themeShare.destroyDelayed();
}

void OverlayWidget::receiveMouse() {
	_receiveMouse = true;
}

void OverlayWidget::showDropdown() {
	_dropdown->clearActions();
	fillContextMenuActions([&] (const QString &text, Fn<void()> handler) {
		_dropdown->addAction(text, std::move(handler));
	});
	_dropdown->moveToRight(0, height() - _dropdown->height());
	_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	_dropdown->setFocus();
}

void OverlayWidget::handleTouchTimer() {
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
	_headerNav = QRect(st::mediaviewTextLeft, height() - st::mediaviewHeaderTop, hwidth, st::mediaviewThickFont->height);
}

float64 OverlayWidget::overLevel(OverState control) const {
	auto i = _animationOpacities.find(control);
	return (i == end(_animationOpacities))
		? (_over == control ? 1. : 0.)
		: i->second.current();
}

} // namespace View
} // namespace Media
