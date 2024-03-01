/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_widget.h"

#include "apiwrap.h"
#include "api/api_attached_stickers.h"
#include "api/api_peer_photo.h"
#include "lang/lang_keys.h"
#include "boxes/premium_preview_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "core/ui_integration.h"
#include "core/crash_reports.h"
#include "core/sandbox.h"
#include "core/shortcuts.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/layers/layer_manager.h"
#include "ui/text/text_utilities.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/item_text_options.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/cached_round_corners.h"
#include "ui/gl/gl_window.h"
#include "ui/boxes/confirm_box.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/statistics/info_statistics_widget.h"
#include "boxes/delete_messages_box.h"
#include "boxes/report_messages_box.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_group_thumbs.h"
#include "media/view/media_view_pip.h"
#include "media/view/media_view_overlay_raster.h"
#include "media/view/media_view_overlay_opengl.h"
#include "media/stories/media_stories_view.h"
#include "media/streaming/media_streaming_player.h"
#include "media/player/media_player_instance.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "history/view/media/history_view_media.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_media_rotation.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_file_click_handler.h"
#include "data/data_download_manager.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/power_save_blocker.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "base/qt_signal_producer.h"
#include "base/event_filter.h"
#include "main/main_account.h"
#include "main/main_domain.h" // Domain::activeSessionValue.
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "layout/layout_document_generic_preview.h"
#include "platform/platform_overlay_widget.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"
#include "calls/calls_instance.h"
#include "styles/style_media_view.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#ifdef Q_OS_MAC
#include "platform/mac/touchbar/mac_touchbar_media_view.h"
#endif // Q_OS_MAC

#include <QtWidgets/QApplication>
#include <QtCore/QBuffer>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

#include <kurlmimedata.h>

namespace Media {
namespace View {
namespace {

constexpr auto kPreloadCount = 3;
constexpr auto kMaxZoomLevel = 7; // x8
constexpr auto kZoomToScreenLevel = 1024;
constexpr auto kOverlayLoaderPriority = 2;
constexpr auto kSeekTimeMs = 5 * crl::time(1000);

// macOS OpenGL renderer fails to render larger texture
// even though it reports that max texture size is 16384.
constexpr auto kMaxDisplayImageSize = 4096;

// Preload X message ids before and after current.
constexpr auto kIdsLimit = 48;

// Preload next messages if we went further from current than that.
constexpr auto kIdsPreloadAfter = 28;

constexpr auto kLeftSiblingTextureIndex = 1;
constexpr auto kRightSiblingTextureIndex = 2;
constexpr auto kStoriesControlsOpacity = 1.;
constexpr auto kStorySavePromoDuration = 3 * crl::time(1000);

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

[[nodiscard]] Core::WindowPosition DefaultPosition() {
	const auto moncrc = [&] {
		if (const auto active = Core::App().activeWindow()) {
			const auto widget = active->widget();
			if (const auto screen = widget->screen()) {
				return Platform::ScreenNameChecksum(screen->name());
			}
		}
		return Core::App().settings().windowPosition().moncrc;
	}();
	return {
		.moncrc = moncrc,
		.scale = cScale(),
		.x = st::mediaviewDefaultLeft,
		.y = st::mediaviewDefaultTop,
		.w = st::mediaviewDefaultWidth,
		.h = st::mediaviewDefaultHeight,
	};
}

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
	const auto result = Images::Option::Blur;
	return (document && document->isVideoMessage())
		? (result | Images::Option::RoundCircle)
		: result;
}

[[nodiscard]] QImage PrepareStaticImage(Images::ReadArgs &&args) {
	auto read = Images::Read(std::move(args));
	return (read.image.width() > kMaxDisplayImageSize
		|| read.image.height() > kMaxDisplayImageSize)
		? read.image.scaled(
			kMaxDisplayImageSize,
			kMaxDisplayImageSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: read.image;
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
		Fn<void()> waitingCallback);
	Streamed(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> waitingCallback);

	Streaming::Instance instance;
	std::unique_ptr<PlaybackControls> controls;
	std::unique_ptr<base::PowerSaveBlocker> powerSaveBlocker;

	bool withSound = false;
	bool pausedBySeek = false;
	bool resumeOnCallEnd = false;
};

struct OverlayWidget::PipWrap {
	PipWrap(
		QWidget *parent,
		not_null<DocumentData*> document,
		std::shared_ptr<Streaming::Document> shared,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);

	PipWrap(const PipWrap &other) = delete;
	PipWrap &operator=(const PipWrap &other) = delete;

	PipDelegate delegate;
	Pip wrapped;
	rpl::lifetime lifetime;
};

struct OverlayWidget::ItemContext {
	not_null<HistoryItem*> item;
	MsgId topicRootId = 0;
};

struct OverlayWidget::StoriesContext {
	not_null<PeerData*> peer;
	StoryId id = 0;
	Data::StoriesContext within;
};

class OverlayWidget::Show final : public ChatHelpers::Show {
public:
	explicit Show(not_null<OverlayWidget*> widget) : _widget(widget) {
	}

	void activate() override {
		if (!_widget->isHidden()) {
			_widget->activate();
		}
	}

	void showOrHideBoxOrLayer(
			std::variant<
				v::null_t,
				object_ptr<Ui::BoxContent>,
				std::unique_ptr<Ui::LayerWidget>> &&layer,
			Ui::LayerOptions options,
			anim::type animated) const override {
		_widget->_layerBg->uiShow()->showOrHideBoxOrLayer(
			std::move(layer),
			options,
			anim::type::normal);
	}
	not_null<QWidget*> toastParent() const override {
		return _widget->_body;
	}
	bool valid() const override {
		return _widget->_session || _widget->_storiesSession;
	}
	operator bool() const override {
		return valid();
	}

	Main::Session &session() const override {
		Expects(_widget->_session || _widget->_storiesSession);

		return _widget->_session
			? *_widget->_session
			: *_widget->_storiesSession;
	}
	bool paused(ChatHelpers::PauseReason reason) const override {
		if (_widget->isHidden()
			|| (!_widget->_fullscreen
				&& !_widget->_window->isActiveWindow())) {
			return true;
		} else if (reason < ChatHelpers::PauseReason::Layer
			&& _widget->_layerBg->topShownLayer() != nullptr) {
			return true;
		}
		return false;
	}
	rpl::producer<> pauseChanged() const override {
		return rpl::never<>();
	}

	rpl::producer<bool> adjustShadowLeft() const override {
		return rpl::single(false);
	}
	SendMenu::Type sendMenuType() const override {
		return SendMenu::Type::SilentOnly;
	}

	bool showMediaPreview(
			Data::FileOrigin origin,
			not_null<DocumentData*> document) const override {
		return false; // #TODO stories
	}
	bool showMediaPreview(
			Data::FileOrigin origin,
			not_null<PhotoData*> photo) const override {
		return false; // #TODO stories
	}

	void processChosenSticker(
			ChatHelpers::FileChosen &&chosen) const override {
		_widget->_storiesStickerOrEmojiChosen.fire(std::move(chosen));
	}

private:
	not_null<OverlayWidget*> _widget;

};

OverlayWidget::Streamed::Streamed(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	Fn<void()> waitingCallback)
: instance(document, origin, std::move(waitingCallback)) {
}

OverlayWidget::Streamed::Streamed(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	Fn<void()> waitingCallback)
: instance(photo, origin, std::move(waitingCallback)) {
}

OverlayWidget::PipWrap::PipWrap(
	QWidget *parent,
	not_null<DocumentData*> document,
	std::shared_ptr<Streaming::Document> shared,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: delegate(parent, &document->session())
, wrapped(
	&delegate,
	document,
	std::move(shared),
	std::move(closeAndContinue),
	std::move(destroy)) {
}

OverlayWidget::OverlayWidget()
: _wrap(std::make_unique<Ui::GL::Window>())
, _window(_wrap->window())
, _helper(Platform::CreateOverlayWidgetHelper(_window.get(), [=](bool maximized) {
	toggleFullScreen(maximized);
}))
, _body(_wrap->widget())
, _titleBugWorkaround(std::make_unique<Ui::RpWidget>(_body))
, _surface(
	Ui::GL::CreateSurface(_body, chooseRenderer(_wrap->backend())))
, _widget(_surface->rpWidget())
, _fullscreen(Core::App().settings().mediaViewPosition().maximized == 2)
, _windowed(Core::App().settings().mediaViewPosition().maximized == 0)
, _cachedReactionIconFactory(std::make_unique<ReactionIconFactory>())
, _layerBg(std::make_unique<Ui::LayerManager>(_body))
, _docDownload(_body, tr::lng_media_download(tr::now), st::mediaviewFileLink)
, _docSaveAs(_body, tr::lng_mediaview_save_as(tr::now), st::mediaviewFileLink)
, _docCancel(_body, tr::lng_cancel(tr::now), st::mediaviewFileLink)
, _radial([=](crl::time now) { return radialAnimationCallback(now); })
, _lastAction(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction)
, _stateAnimation([=](crl::time now) { return stateAnimationCallback(now); })
, _dropdown(_body, st::mediaviewDropdownMenu) {
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_layerBg->setHideByBackgroundClick(true);

	CrashReports::SetAnnotation("OpenGL Renderer", "[not-initialized]");

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_lastPositiveVolume = (Core::App().settings().videoVolume() > 0.)
		? Core::App().settings().videoVolume()
		: Core::Settings::kDefaultVolume;

	_saveMsgTimer.setCallback([=, delay = st::mediaviewSaveMsgHiding] {
		_saveMsgAnimation.start([=] { updateSaveMsg(); }, 1., 0., delay);
	});

	_docRectImage = QImage(
		st::mediaviewFileSize * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	_docRectImage.setDevicePixelRatio(cIntRetinaFactor());

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		request->check(
			Shortcuts::Command::MediaViewerFullscreen
		) && request->handle([=] {
			if (_streamed) {
				playbackToggleFullScreen();
				return true;
			}
			return false;
		});
	}, lifetime());

	setupWindow();

	const auto mousePosition = [](not_null<QEvent*> e) {
		return static_cast<QMouseEvent*>(e.get())->pos();
	};
	const auto mouseButton = [](not_null<QEvent*> e) {
		return static_cast<QMouseEvent*>(e.get())->button();
	};
	base::install_event_filter(_window, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Move) {
			const auto position = static_cast<QMoveEvent*>(e.get())->pos();
			DEBUG_LOG(("Viewer Pos: Moved to %1, %2")
				.arg(position.x())
				.arg(position.y()));
			if (_windowed) {
				savePosition();
			} else {
				moveToScreen(true);
			}
		} else if (type == QEvent::Resize) {
			if (_windowed) {
				savePosition();
			}
		} else if (type == QEvent::Close
			&& !Core::Sandbox::Instance().isSavingSession()
			&& !Core::Quitting()) {
			e->ignore();
			close();
			return base::EventFilterResult::Cancel;
		} else if (type == QEvent::ThemeChange && Platform::IsLinux()) {
			_window->setWindowIcon(Window::CreateIcon(_session));
		} else if (type == QEvent::ContextMenu) {
			const auto event = static_cast<QContextMenuEvent*>(e.get());
			const auto mouse = (event->reason() == QContextMenuEvent::Mouse);
			const auto position = mouse
				? std::make_optional(event->pos())
				: std::nullopt;
			if (handleContextMenu(position)) {
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});
	base::install_event_filter(_body, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Resize) {
			const auto size = static_cast<QResizeEvent*>(e.get())->size();
			DEBUG_LOG(("Viewer Pos: Resized to %1, %2")
				.arg(size.width())
				.arg(size.height()));

			// Somehow Windows 11 knows the geometry of first widget below
			// the semi-native title control widgets and it uses
			// it's geometry to show the snap grid popup around it when
			// you put the mouse over the Maximize button. In the 4.6.4 beta
			// the first widget was `_widget`, so the popup was shown
			// either above the window or, if not enough space above, below
			// the whole window, you couldn't even put the mouse on it.
			//
			// So now here is this weird workaround that places our
			// `_titleBugWorkaround` widget as the first one under the title
			// controls and the system shows the popup around its geometry,
			// so we set it's height to the title controls height
			// and everything works as expected.
			//
			// This doesn't make sense. But it works. :shrug:
			_titleBugWorkaround->setGeometry(
				{ 0, 0, size.width(), st::mediaviewTitleButton.height });

			_widget->setGeometry({ QPoint(), size });
			updateControlsGeometry();
		} else if (type == QEvent::KeyPress) {
			handleKeyPress(static_cast<QKeyEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});
	base::install_event_filter(_widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Leave) {
			if (_over != Over::None) {
				updateOverState(Over::None);
			}
		} else if (type == QEvent::MouseButtonPress) {
			handleMousePress(mousePosition(e), mouseButton(e));
		} else if (type == QEvent::MouseButtonRelease) {
			handleMouseRelease(mousePosition(e), mouseButton(e));
		} else if (type == QEvent::MouseMove) {
			handleMouseMove(mousePosition(e));
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
				return base::EventFilterResult::Cancel;
			}
		} else if (type == QEvent::Wheel) {
			handleWheelEvent(static_cast<QWheelEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});
	_helper->mouseEvents(
	) | rpl::start_with_next([=](not_null<QMouseEvent*> e) {
		const auto type = e->type();
		const auto position = e->pos();
		if (_helper->skipTitleHitTest(position)) {
			return;
		}
		if (type == QEvent::MouseButtonPress) {
			handleMousePress(position, e->button());
		} else if (type == QEvent::MouseButtonRelease) {
			handleMouseRelease(position, e->button());
		} else if (type == QEvent::MouseMove) {
			handleMouseMove(position);
		} else if (type == QEvent::MouseButtonDblClick) {
			if (!handleDoubleClick(position, e->button())) {
				handleMousePress(position, e->button());
			}
		}
	}, lifetime());
	_topShadowRight = _helper->controlsSideRightValue();
	_topShadowRight.changes(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
		update();
	}, lifetime());

	_helper->topNotchSkipValue(
	) | rpl::start_with_next([=](int notch) {
		if (_topNotchSize != notch) {
			_topNotchSize = notch;
			if (_fullscreen) {
				updateControlsGeometry();
			}
		}
	}, lifetime());

	_window->setTitle(tr::lng_mediaview_title(tr::now));
	_window->setTitleStyle(st::mediaviewTitle);

	if constexpr (Platform::IsMac()) {
		// Without Qt::Tool starting with Qt 5.15.1 this widget
		// when being opened from a fullscreen main window was
		// opening not as overlay over the main window, but as
		// a separate fullscreen window with a separate space.
		_window->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
	}
	_widget->setMouseTracking(true);

	QObject::connect(
		window(),
		&QWindow::screenChanged,
		[=](QScreen *screen) { handleScreenChanged(screen); });
	subscribeToScreenGeometry();
	updateGeometry();
	updateControlsGeometry();

#ifdef Q_OS_MAC
	TouchBar::SetupMediaViewTouchBar(
		_window->winId(),
		static_cast<PlaybackControls::Delegate*>(this),
		_touchbarTrackState.events(),
		_touchbarDisplay.events(),
		_touchbarFullscreenToggled.events());
#endif // Q_OS_MAC

	using namespace rpl::mappers;
	rpl::combine(
		Core::App().calls().currentCallValue(),
		Core::App().calls().currentGroupCallValue(),
		_1 || _2
	) | rpl::start_with_next([=](bool call) {
		if (!_streamed
			|| !_document
			|| (_document->isAnimation() && !_document->isVideoMessage())) {
			return;
		} else if (call) {
			playbackPauseOnCall();
		} else {
			playbackResumeOnCall();
		}
	}, lifetime());

	_widget->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setCallback([=] { handleTouchTimer(); });

	_controlsHideTimer.setCallback([=] { hideControls(); });
	_helper->controlsActivations(
	) | rpl::start_with_next([=] {
		activateControls();
	}, lifetime());

	_docDownload->addClickHandler([=] { downloadMedia(); });
	_docSaveAs->addClickHandler([=] { saveAs(); });
	_docCancel->addClickHandler([=] { saveCancel(); });

	_dropdown->setHiddenCallback([this] { dropdownHidden(); });
	_dropdownShowTimer.setCallback([=] { showDropdown(); });

	orderWidgets();
}

void OverlayWidget::showSaveMsgToast(const QString &path, auto phrase) {
	showSaveMsgToastWith(path, phrase(
		tr::now,
		lt_downloads,
		Ui::Text::Link(
			tr::lng_mediaview_downloads(tr::now),
			"internal:show_saved_message"),
		Ui::Text::WithEntities));
}

void OverlayWidget::showSaveMsgToastWith(
		const QString &path,
		const TextWithEntities &text) {
	_saveMsgFilename = path;
	_saveMsgText.setMarkedText(st::mediaviewSaveMsgStyle, text);
	const auto w = _saveMsgText.maxWidth()
		+ st::mediaviewSaveMsgPadding.left()
		+ st::mediaviewSaveMsgPadding.right();
	const auto h = st::mediaviewSaveMsgStyle.font->height
		+ st::mediaviewSaveMsgPadding.top()
		+ st::mediaviewSaveMsgPadding.bottom();
	_saveMsg = QRect(
		(width() - w) / 2,
		_minUsedTop + (_maxUsedHeight - h) / 2,
		w,
		h);
	const auto callback = [=](float64 value) {
		updateSaveMsg();
		if (!_saveMsgAnimation.animating()) {
			_saveMsgTimer.callOnce(st::mediaviewSaveMsgShown);
		}
	};
	const auto duration = st::mediaviewSaveMsgShowing;
	_saveMsgAnimation.start(callback, 0., 1., duration);
	updateSaveMsg();
}

void OverlayWidget::orderWidgets() {
	_helper->orderWidgets();
}

void OverlayWidget::setupWindow() {
	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!_windowed
			|| !_widget->rect().contains(widgetPoint)
			|| _helper->skipTitleHitTest(widgetPoint)) {
			return Flag::None | Flag(0);
		}
		const auto inControls = (_over != Over::None) && (_over != Over::Video);
		if (inControls
			|| (_streamed
				&& _streamed->controls
				&& _streamed->controls->dragging())) {
			return Flag::None | Flag(0);
		} else if ((_w > _widget->width() || _h > _maxUsedHeight)
				&& (widgetPoint.y() > st::mediaviewHeaderTop)
				&& QRect(_x, _y, _w, _h).contains(widgetPoint)) {
			return Flag::None | Flag(0);
		} else if (_stories && _stories->ignoreWindowMove(widgetPoint)) {
			return Flag::None | Flag(0);
		}
		return Flag::Move | Flag(0);
	});

	const auto callback = [=](Qt::WindowState state) {
		if (state == Qt::WindowMinimized || Platform::IsMac()) {
			return;
		} else if (state == Qt::WindowMaximized) {
			if (_fullscreen || _windowed) {
				_fullscreen = _windowed = false;
				savePosition();
			}
		} else if (_fullscreen || _windowed) {
			return;
		} else if (state == Qt::WindowFullScreen) {
			_fullscreen = true;
			savePosition();
		} else {
			_windowed = true;
			savePosition();
		}
	};
	QObject::connect(
		_window->windowHandle(),
		&QWindow::windowStateChanged,
		callback);

	_window->setAttribute(Qt::WA_NoSystemBackground, true);
	_window->setAttribute(Qt::WA_TranslucentBackground, true);

	_window->setMinimumSize(
		{ st::mediaviewMinWidth, st::mediaviewMinHeight });

	_window->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		toggleApplicationEventFilter(shown);
		if (!shown) {
			clearAfterHide();
		} else {
			const auto geometry = _window->geometry();
			const auto screenList = QGuiApplication::screens();
			DEBUG_LOG(("Viewer Pos: Shown, geometry: %1, %2, %3, %4, screen number: %5")
				.arg(geometry.x())
				.arg(geometry.y())
				.arg(geometry.width())
				.arg(geometry.height())
				.arg(screenList.indexOf(_window->screen())));
			moveToScreen();
		}
	}, lifetime());
}

void OverlayWidget::refreshLang() {
	InvokeQueued(_widget, [=] { updateThemePreviewGeometry(); });
}

void OverlayWidget::moveToScreen(bool inMove) {
	if (!_fullscreen || _wasWindowedMode) {
		return;
	}
	const auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (!widget) {
			return nullptr;
		}
		if (!Platform::IsWayland()) {
			if (const auto screen = QGuiApplication::screenAt(
				widget->geometry().center())) {
				return screen;
			}
		}
		return widget->screen();
	};
	const auto applicationWindow = Core::App().activeWindow()
		? Core::App().activeWindow()->widget().get()
		: nullptr;
	const auto activeWindowScreen = widgetScreen(applicationWindow);
	const auto myScreen = _window->screen();
	if (activeWindowScreen && myScreen != activeWindowScreen) {
		const auto screenList = QGuiApplication::screens();
		DEBUG_LOG(("Viewer Pos: Currently on screen %1, moving to screen %2")
			.arg(screenList.indexOf(myScreen))
			.arg(screenList.indexOf(activeWindowScreen)));
		window()->setScreen(activeWindowScreen);
		DEBUG_LOG(("Viewer Pos: New actual screen: %1")
			.arg(screenList.indexOf(_window->screen())));
	}
	updateGeometry(inMove);
}

void OverlayWidget::initFullScreen() {
	if (_fullscreenInited) {
		return;
	}
	_fullscreenInited = true;
	switch (Core::App().settings().mediaViewPosition().maximized) {
	case 2:
		_fullscreen = true;
		_windowed = false;
		break;
	case 1:
		_fullscreen = Platform::IsMac();
		_windowed = false;
		break;
	}
}

void OverlayWidget::initNormalGeometry() {
	if (_normalGeometryInited) {
		return;
	}
	_normalGeometryInited = true;
	const auto saved = Core::App().settings().mediaViewPosition();
	const auto adjusted = Core::AdjustToScale(saved, u"Viewer"_q);
	const auto initial = DefaultPosition();
	_normalGeometry = initial.rect();
	if (const auto active = Core::App().activeWindow()) {
		_normalGeometry = active->widget()->countInitialGeometry(
			adjusted,
			initial,
			{ st::mediaviewMinWidth, st::mediaviewMinHeight });
	}
}

void OverlayWidget::savePosition() {
	if (isHidden() || isMinimized() || !_normalGeometryInited) {
		return;
	}
	const auto &savedPosition = Core::App().settings().mediaViewPosition();
	auto realPosition = savedPosition;
	if (_fullscreen) {
		realPosition.maximized = 2;
		realPosition.moncrc = 0;
		DEBUG_LOG(("Viewer Pos: Saving fullscreen position."));
	} else if (!_windowed) {
		realPosition.maximized = 1;
		realPosition.moncrc = 0;
		DEBUG_LOG(("Viewer Pos: Saving maximized position."));
	} else if (!_wasWindowedMode && !Platform::IsMac()) {
		return;
	} else {
		auto r = _normalGeometry = _window->geometry();
		realPosition.x = r.x();
		realPosition.y = r.y();
		realPosition.w = r.width();
		realPosition.h = r.height();
		realPosition.scale = cScale();
		realPosition.maximized = 0;
		realPosition.moncrc = 0;
		DEBUG_LOG(("Viewer Pos: "
			"Saving non-maximized position: %1, %2, %3, %4"
			).arg(realPosition.x
			).arg(realPosition.y
			).arg(realPosition.w
			).arg(realPosition.h));
	}
	realPosition = Window::PositionWithScreen(
		realPosition,
		_window,
		{ st::mediaviewMinWidth, st::mediaviewMinHeight });
	if (realPosition.w >= st::mediaviewMinWidth
		&& realPosition.h >= st::mediaviewMinHeight
		&& realPosition != savedPosition) {
		DEBUG_LOG(("Viewer Pos: "
			"Writing: %1, %2, %3, %4 (scale %5%, maximized %6)")
			.arg(realPosition.x)
			.arg(realPosition.y)
			.arg(realPosition.w)
			.arg(realPosition.h)
			.arg(realPosition.scale)
			.arg(Logs::b(realPosition.maximized)));
		Core::App().settings().setMediaViewPosition(realPosition);
		Core::App().saveSettingsDelayed();
	}
}

void OverlayWidget::updateGeometry(bool inMove) {
	initFullScreen();
	if (_fullscreen && (!Platform::IsWindows11OrGreater() || !isHidden())) {
		updateGeometryToScreen(inMove);
	} else if (_windowed && _normalGeometryInited) {
		_window->setGeometry(_normalGeometry);
	}
	if constexpr (!Platform::IsMac()) {
		if (_fullscreen) {
			if (!isHidden() && !isMinimized()) {
				_window->showFullScreen();
			}
		} else if (!_windowed) {
			if (!isHidden() && !isMinimized()) {
				_window->showMaximized();
			}
		}
	}
}

void OverlayWidget::updateGeometryToScreen(bool inMove) {
	const auto available = _window->screen()->geometry();
	const auto openglWidget = _opengl
		? static_cast<QOpenGLWidget*>(_widget.get())
		: nullptr;
	const auto possibleSizeHack = Platform::IsWindows() && openglWidget;
	const auto useSizeHack = possibleSizeHack
		&& (openglWidget->format().renderableType()
			!= QSurfaceFormat::OpenGLES);
	const auto use = useSizeHack
		? available.marginsAdded({ 0, 0, 0, 1 })
		: available;
	const auto mask = useSizeHack
		? QRegion(QRect(QPoint(), available.size()))
		: QRegion();
	if (inMove && use.contains(_window->geometry())) {
		return;
	}
	if ((_window->geometry() == use)
		&& (!possibleSizeHack || _window->mask() == mask)) {
		return;
	}
	DEBUG_LOG(("Viewer Pos: Setting %1, %2, %3, %4")
		.arg(use.x())
		.arg(use.y())
		.arg(use.width())
		.arg(use.height()));
	_window->setGeometry(use);
	if (possibleSizeHack) {
		_window->setMask(mask);
	}
}

void OverlayWidget::updateControlsGeometry() {
	updateNavigationControlsGeometry();

	_saveMsg.moveTo(
		(width() - _saveMsg.width()) / 2,
		_minUsedTop + (_maxUsedHeight - _saveMsg.height()) / 2);
	_photoRadialRect = QRect(
		QPoint(
			(width() - st::radialSize.width()) / 2,
			_minUsedTop + (_maxUsedHeight - st::radialSize.height()) / 2),
		st::radialSize);

	const auto bottom = st::mediaviewShadowBottom.height();
	const auto top = st::mediaviewShadowTop.size();
	_bottomShadowRect = QRect(0, height() - bottom, width(), bottom);
	_topShadowRect = QRect(
		QPoint(topShadowOnTheRight() ? (width() - top.width()) : 0, 0),
		top);

	if (_dropdown && !_dropdown->isHidden()) {
		_dropdown->moveToRight(0, height() - _dropdown->height());
	}

	updateControls();
	resizeContentByScreenSize();
	update();
}

void OverlayWidget::updateNavigationControlsGeometry() {
	_minUsedTop = topNotchSkip();
	_maxUsedHeight = height() - _minUsedTop;

	const auto overRect = QRect(
		QPoint(),
		QSize(st::mediaviewIconOver, st::mediaviewIconOver));
	const auto navSize = _stories
		? st::storiesControlSize
		: st::mediaviewControlSize;
	const auto navSkip = st::mediaviewHeaderTop;
	const auto xLeft = _stories ? (_x - navSize) : 0;
	const auto xRight = _stories ? (_x + _w) : (width() - navSize);
	_leftNav = QRect(
		xLeft,
		_minUsedTop + navSkip,
		navSize,
		_maxUsedHeight - 2 * navSkip);
	_leftNavOver = _stories
		? QRect()
		: style::centerrect(_leftNav, overRect);
	_leftNavIcon = style::centerrect(
		_leftNav,
		_stories ? st::storiesLeft : st::mediaviewLeft);
	_rightNav = QRect(
		xRight,
		_minUsedTop + navSkip,
		navSize,
		_maxUsedHeight - 2 * navSkip);
	_rightNavOver = _stories
		? QRect()
		: style::centerrect(_rightNav, overRect);
	_rightNavIcon = style::centerrect(
		_rightNav,
		_stories ? st::storiesRight : st::mediaviewRight);
}

bool OverlayWidget::topShadowOnTheRight() const {
	return _topShadowRight.current();
}

QSize OverlayWidget::flipSizeByRotation(QSize size) const {
	return FlipSizeByRotation(size, _rotation);
}

bool OverlayWidget::hasCopyMediaRestriction(bool skipPremiumCheck) const {
	if (const auto story = _stories ? _stories->story() : nullptr) {
		return skipPremiumCheck
			? !story->canDownloadIfPremium()
			: !story->canDownloadChecked();
	}
	return (_history && !_history->peer->allowsForwarding())
		|| (_message && _message->forbidsSaving());
}

bool OverlayWidget::showCopyMediaRestriction(bool skipPRemiumCheck) {
	if (!hasCopyMediaRestriction(skipPRemiumCheck)) {
		return false;
	} else if (_stories) {
		uiShow()->showToast(tr::lng_error_nocopy_story(tr::now));
	} else if (_history) {
		uiShow()->showToast(_history->peer->isBroadcast()
			? tr::lng_error_nocopy_channel(tr::now)
			: tr::lng_error_nocopy_group(tr::now));
	}
	return true;
}

bool OverlayWidget::videoShown() const {
	return _streamed && !_streamed->instance.info().video.cover.isNull();
}

QSize OverlayWidget::videoSize() const {
	Expects(videoShown());

	return flipSizeByRotation(_streamed->instance.info().video.size);
}

bool OverlayWidget::streamingRequiresControls() const {
	return !_stories
		&& _document
		&& (!_document->isAnimation() || _document->isVideoMessage());
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
			.image = _streamed->instance.info().video.cover,
			.format = Streaming::FrameFormat::ARGB32,
			.index = -2,
			.alpha = _streamed->instance.info().video.alpha,
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
			|| (!_document->isVideoMessage()
				&& !_document->sticker()
				&& (!_streamed || !_streamed->instance.info().video.alpha)));
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

void OverlayWidget::documentUpdated(not_null<DocumentData*> document) {
	if (_document != document) {
		return;
	} else if (documentBubbleShown()) {
		if ((_document->loading() && _docCancel->isHidden()) || (!_document->loading() && !_docCancel->isHidden())) {
			updateControls();
		} else if (_document->loading()) {
			updateDocSize();
			_widget->update(_docRect);
		}
	} else if (_streamed && _streamed->controls) {
		const auto ready = _documentMedia->loaded()
			? _document->size
			: _document->loading()
			? std::clamp(_document->loadOffset(), int64(), _document->size)
			: 0;
		_streamed->controls->setLoadingProgress(ready, _document->size);
	}
	if (_stories
		&& !_documentLoadingTo.isEmpty()
		&& _document->location(true).isEmpty()) {
		showSaveMsgToast(
			base::take(_documentLoadingTo),
			tr::lng_mediaview_video_saved_to);
	}
}

void OverlayWidget::changingMsgId(FullMsgId newId, MsgId oldId) {
	if (_message && _message->fullId() == newId) {
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
	if (_stories) {
		_leftNavVisible = _stories->subjumpAvailable(-1);
		_rightNavVisible = _stories->subjumpAvailable(1);
	} else if (_sharedMediaData) {
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

bool OverlayWidget::computeSaveButtonVisible() const {
	if (hasCopyMediaRestriction(true)) {
		return false;
	} else if (_photo) {
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
		|| _photoMedia->videoContent(Data::PhotoSize::Large).isEmpty()) {
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

void OverlayWidget::showPremiumDownloadPromo() {
	const auto filter = [=](const auto &...) {
		const auto usage = ChatHelpers::WindowUsage::PremiumPromo;
		if (const auto window = uiShow()->resolveWindow(usage)) {
			ShowPremiumPreviewBox(window, PremiumFeature::Stories);
			window->window().activate();
		}
		return false;
	};
	uiShow()->showToast({
		.text = tr::lng_stories_save_promo(
			tr::now,
			lt_link,
			Ui::Text::Link(
				Ui::Text::Bold(
					tr::lng_send_as_premium_required_link(tr::now))),
			Ui::Text::WithEntities),
		.duration = kStorySavePromoDuration,
		.adaptive = true,
		.filter = filter,
	});
}

void OverlayWidget::updateControls() {
	if (_document && documentBubbleShown()) {
		_docRect = QRect(
			(width() - st::mediaviewFileSize.width()) / 2,
			_minUsedTop + (_maxUsedHeight - st::mediaviewFileSize.height()) / 2,
			st::mediaviewFileSize.width(),
			st::mediaviewFileSize.height());
		_docIconRect = QRect(
			_docRect.x() + st::mediaviewFilePadding,
			_docRect.y() + st::mediaviewFilePadding,
			st::mediaviewFileIconSize,
			st::mediaviewFileIconSize);
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
		_docIconRect = QRect(
			(width() - st::mediaviewFileIconSize) / 2,
			_minUsedTop + (_maxUsedHeight - st::mediaviewFileIconSize) / 2,
			st::mediaviewFileIconSize,
			st::mediaviewFileIconSize);
		_docDownload->hide();
		_docSaveAs->hide();
		_docCancel->hide();
	}
	radialStart();

	updateThemePreviewGeometry();

	const auto story = _stories ? _stories->story() : nullptr;
	const auto overRect = QRect(
		QPoint(),
		QSize(st::mediaviewIconOver, st::mediaviewIconOver));
	_saveVisible = computeSaveButtonVisible();
	_shareVisible = story && story->canShare();
	_rotateVisible = !_themePreviewShown && !story;
	const auto navRect = [&](int i) {
		return QRect(
			width() - st::mediaviewIconSize.width() * i,
			height() - st::mediaviewIconSize.height(),
			st::mediaviewIconSize.width(),
			st::mediaviewIconSize.height());
	};
	auto index = 1;
	_moreNav = navRect(index);
	_moreNavOver = style::centerrect(_moreNav, overRect);
	_moreNavIcon = style::centerrect(_moreNav, st::mediaviewMore);
	++index;
	_rotateNav = navRect(index);
	_rotateNavOver = style::centerrect(_rotateNav, overRect);
	_rotateNavIcon = style::centerrect(_rotateNav, st::mediaviewRotate);
	if (_rotateVisible) {
		++index;
	}
	_shareNav = navRect(index);
	_shareNavOver = style::centerrect(_shareNav, overRect);
	_shareNavIcon = style::centerrect(_shareNav, st::mediaviewShare);
	if (_shareVisible) {
		++index;
	}
	_saveNav = navRect(index);
	_saveNavOver = style::centerrect(_saveNav, overRect);
	_saveNavIcon = style::centerrect(_saveNav, st::mediaviewSave);
	Assert(st::mediaviewSave.size() == st::mediaviewSaveLocked.size());

	const auto dNow = QDateTime::currentDateTime();
	const auto d = [&] {
		if (_message) {
			return ItemDateTime(_message);
		} else if (_photo) {
			return base::unixtime::parse(_photo->date);
		} else if (_document) {
			return base::unixtime::parse(_document->date);
		}
		return dNow;
	}();
	_dateText = d.isValid() ? Ui::FormatDateTime(d) : QString();
	if (!_fromName.isEmpty()) {
		_fromNameLabel.setText(
			st::mediaviewTextStyle,
			_fromName,
			Ui::NameTextOptions());
		_nameNav = QRect(
			st::mediaviewTextLeft,
			height() - st::mediaviewTextTop,
			qMin(_fromNameLabel.maxWidth(), width() / 3),
			st::mediaviewFont->height);
		_dateNav = QRect(
			st::mediaviewTextLeft + _nameNav.width() + st::mediaviewTextSkip,
			height() - st::mediaviewTextTop,
			st::mediaviewFont->width(_dateText),
			st::mediaviewFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = QRect(
			st::mediaviewTextLeft,
			height() - st::mediaviewTextTop,
			st::mediaviewFont->width(_dateText),
			st::mediaviewFont->height);
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
	_caption.updateSkipBlock(0, 0);
	_captionShowMoreWidth = 0;
	_captionSkipBlockWidth = 0;

	const auto storiesCaptionWidth = _w
		- st::mediaviewCaptionPadding.left()
		- st::mediaviewCaptionPadding.right();
	if (_caption.isEmpty() && (!_stories || !_stories->repost())) {
		_captionRect = QRect();
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto captionBottom = _stories
		? (_y + _h)
		: (_streamed && _streamed->controls)
		? (_streamed->controls->y() - st::mediaviewCaptionMargin.height())
		: _groupThumbs
		? _groupThumbsTop
		: height() - st::mediaviewCaptionMargin.height();
	const auto captionWidth = _stories
		? storiesCaptionWidth
		: std::min(
			(_groupThumbsAvailableWidth
				- st::mediaviewCaptionPadding.left()
				- st::mediaviewCaptionPadding.right()),
			_caption.maxWidth());
	const auto lineHeight = st::mediaviewCaptionStyle.font->height;
	const auto wantedHeight = _caption.countHeight(captionWidth);
	const auto maxHeight = !_stories
		? (_maxUsedHeight / 4)
		: (wantedHeight > lineHeight * Stories::kMaxShownCaptionLines)
		? (lineHeight * Stories::kCollapsedCaptionLines)
		: wantedHeight;
	const auto captionHeight = std::min(
		wantedHeight,
		(maxHeight / lineHeight) * lineHeight);
	if (_stories && captionHeight < wantedHeight) {
		const auto padding = st::storiesShowMorePadding;
		_captionShowMoreWidth = st::storiesShowMoreFont->width(
			tr::lng_stories_show_more(tr::now));
		_captionSkipBlockWidth = _captionShowMoreWidth
			+ padding.left()
			+ padding.right()
			- st::mediaviewCaptionPadding.right();
		const auto skiph = st::storiesShowMoreFont->height
			+ padding.bottom()
			- st::mediaviewCaptionPadding.bottom();
		_caption.updateSkipBlock(_captionSkipBlockWidth, skiph);
	}
	_captionRect = QRect(
		(width() - captionWidth) / 2,
		(captionBottom
			- captionHeight
			- st::mediaviewCaptionPadding.bottom()),
		captionWidth,
		captionHeight);
}

void OverlayWidget::fillContextMenuActions(const MenuCallback &addAction) {
	const auto story = _stories ? _stories->story() : nullptr;
	if (!story && _document && _document->loading()) {
		addAction(
			tr::lng_cancel(tr::now),
			[=] { saveCancel(); },
			&st::mediaMenuIconCancel);
	}
	if (_message && _message->isRegular()) {
		addAction(
			tr::lng_context_to_msg(tr::now),
			[=] { toMessage(); },
			&st::mediaMenuIconShowInChat);
	}
	if (story && story->peer()->isSelf()) {
		const auto pinned = story->pinned();
		const auto text = pinned
			? tr::lng_mediaview_archive_story(tr::now)
			: tr::lng_mediaview_save_to_profile(tr::now);
		addAction(text, [=] {
			if (_stories) {
				_stories->togglePinnedRequested(!pinned);
			}
		}, pinned
			? &st::mediaMenuIconArchiveStory
			: &st::mediaMenuIconSaveStory);
	}
	if ((!story || story->canDownloadChecked())
		&& _document
		&& !_document->filepath(true).isEmpty()) {
		const auto text = Platform::IsMac()
			? tr::lng_context_show_in_finder(tr::now)
			: tr::lng_context_show_in_folder(tr::now);
		addAction(
			text,
			[=] { showInFolder(); },
			&st::mediaMenuIconShowInFolder);
	}
	if (!hasCopyMediaRestriction()) {
		if ((_document && documentContentShown()) || (_photo && _photoMedia->loaded())) {
			addAction(
				tr::lng_mediaview_copy(tr::now),
				[=] { copyMedia(); },
				&st::mediaMenuIconCopy);
		}
	}
	if ((_photo && _photo->hasAttachedStickers())
		|| (_document && _document->hasAttachedStickers())) {
		addAction(
			tr::lng_context_attached_stickers(tr::now),
			[=] { showAttachedStickers(); },
			&st::mediaMenuIconStickers);
	}
	if (_message && _message->allowsForward()) {
		addAction(
			tr::lng_mediaview_forward(tr::now),
			[=] { forwardMedia(); },
			&st::mediaMenuIconForward);
	}
	if (story && story->canShare()) {
		addAction(tr::lng_mediaview_forward(tr::now), [=] {
			_stories->shareRequested();
		}, &st::mediaMenuIconForward);
	}
	const auto canDelete = [&] {
		if (story && story->canDelete()) {
			return true;
		} else if (_message && _message->canDelete()) {
			return true;
		} else if (!_message
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
		addAction(
			tr::lng_mediaview_delete(tr::now),
			[=] { deleteMedia(); },
			&st::mediaMenuIconDelete);
	}
	if (!hasCopyMediaRestriction(true)) {
		addAction(
			tr::lng_mediaview_save_as(tr::now),
			[=] { saveAs(); },
			(saveControlLocked()
				? &st::mediaMenuIconDownloadLocked
				: &st::mediaMenuIconDownload));
	}

	if (const auto overviewType = computeOverviewType()) {
		const auto text = _document
			? tr::lng_mediaview_files_all(tr::now)
			: tr::lng_mediaview_photos_all(tr::now);
		addAction(
			text,
			[=] { showMediaOverview(); },
			&st::mediaMenuIconShowAll);
	}
	[&] { // Set userpic.
		if (!_peer || !_photo || (_peer->userpicPhotoId() == _photo->id)) {
			return;
		}
		using Type = SharedMediaType;
		if (sharedMediaType().value_or(Type::File) == Type::ChatPhoto) {
			if (const auto chat = _peer->asChat()) {
				if (!chat->canEditInformation()) {
					return;
				}
			} else if (const auto channel = _peer->asChannel()) {
				if (!channel->canEditInformation()) {
					return;
				}
			} else {
				return;
			}
		} else if (userPhotosKey()) {
			if (_user != _user->session().user()) {
				return;
			}
		} else {
			return;
		}
		const auto photo = _photo;
		const auto peer = _peer;
		addAction(tr::lng_mediaview_set_userpic(tr::now), [=] {
			auto lifetime = std::make_shared<rpl::lifetime>();
			peer->session().changes().peerFlagsValue(
				peer,
				Data::PeerUpdate::Flag::Photo
			) | rpl::start_with_next([=]() mutable {
				if (lifetime) {
					base::take(lifetime)->destroy();
				}
				close();
			}, *lifetime);

			peer->session().api().peerPhoto().set(peer, photo);
		}, &st::mediaMenuIconProfile);
	}();
	[&] { // Report userpic.
		if (!_peer || !_photo) {
			return;
		}
		using Type = SharedMediaType;
		if (userPhotosKey()) {
			if (_peer->isSelf() || _peer->isNotificationsUser()) {
				return;
			} else if (const auto user = _peer->asUser()) {
				if (user->hasPersonalPhoto()
					&& user->userpicPhotoId() == _photo->id) {
					return;
				}
			}
		} else if ((sharedMediaType().value_or(Type::File) == Type::ChatPhoto)
			|| (_peer->userpicPhotoId() == _photo->id)) {
			if (const auto chat = _peer->asChat()) {
				if (chat->canEditInformation()) {
					return;
				}
			} else if (const auto channel = _peer->asChannel()) {
				if (channel->canEditInformation()) {
					return;
				}
			} else {
				return;
			}
		} else {
			return;
		}
		const auto photo = _photo;
		const auto peer = _peer;
		addAction(tr::lng_mediaview_report_profile_photo(tr::now), [=] {
			if (const auto window = findWindow()) {
				close();
				window->show(
					ReportProfilePhotoBox(peer, photo),
					Ui::LayerOption::CloseOther);
			}
		}, &st::mediaMenuIconReport);
	}();
	{
		const auto channel = story ? story->peer()->asChannel() : nullptr;
		using Flag = ChannelDataFlag;
		if (channel && (channel->flags() & Flag::CanGetStatistics)) {
			const auto peer = channel;
			const auto fullId = story->fullId();
			addAction(tr::lng_stats_title(tr::now), [=] {
				if (const auto window = findWindow()) {
					close();
					using namespace Info;
					window->showSection(Statistics::Make(peer, {}, fullId));
				}
			}, &st::mediaMenuIconStats);
		}
	}
	if (_stories && _stories->allowStealthMode()) {
		const auto now = base::unixtime::now();
		const auto stealth = _session->data().stories().stealthMode();
		addAction(tr::lng_stealth_mode_menu_item(tr::now), [=] {
			_stories->setupStealthMode();
		}, ((_session->premium() || (stealth.enabledTill > now))
			? &st::mediaMenuIconStealth
			: &st::mediaMenuIconStealthLocked));
	}
	if (story && story->canReport()) {
		addAction(tr::lng_profile_report(tr::now), [=] {
			_stories->reportRequested();
		}, &st::mediaMenuIconReport);
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
		const auto &[state, started] = *i;
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
	_helper->setControlsOpacity(_controlsOpacity.current());
	const auto content = finalContentRect();
	const auto siblingType = (_over == Over::LeftStories)
		? Stories::SiblingType::Left
		: Stories::SiblingType::Right;
	const auto toUpdate = QRegion()
		+ (_over == Over::Left ? _leftNavOver : _leftNavIcon)
		+ (_over == Over::Right ? _rightNavOver : _rightNavIcon)
		+ (_over == Over::Save ? _saveNavOver : _saveNavIcon)
		+ (_over == Over::Share ? _shareNavOver : _shareNavIcon)
		+ (_over == Over::Rotate ? _rotateNavOver : _rotateNavIcon)
		+ (_over == Over::More ? _moreNavOver : _moreNavIcon)
		+ ((_stories
			&& (_over == Over::LeftStories || _over == Over::RightStories))
			? _stories->sibling(siblingType).layout.geometry
			: QRect())
		+ _headerNav
		+ _nameNav
		+ _dateNav
		+ _captionRect.marginsAdded(st::mediaviewCaptionPadding)
		+ _groupThumbsRect
		+ content.intersected(_bottomShadowRect)
		+ content.intersected(_topShadowRect);
	update(toUpdate);
	return (dt < 1);
}

void OverlayWidget::waitingAnimationCallback() {
	if (!anim::Disabled()) {
		update(radialRect());
	}
}

void OverlayWidget::updateCursor() {
	setCursor((_controlsState == ControlsHidden)
		? Qt::BlankCursor
		: (_over == Over::None || (_over == Over::Video && _stories))
		? style::cur_default
		: style::cur_pointer);
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
	if (_stories) {
		auto result = storiesContentGeometry(_stories->contentLayout());
		if (!_caption.isEmpty()) {
			result.bottomShadowSkip = _widget->height()
				- _captionRect.y()
				+ st::mediaviewCaptionStyle.font->height
				- st::storiesShadowBottom.height();
		}
		return result;
	}
	const auto controlsOpacity = _controlsOpacity.current();
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
		return { toRect, toRotation, controlsOpacity };
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
	return { useRect, useRotation, controlsOpacity };
}

OverlayWidget::ContentGeometry OverlayWidget::storiesContentGeometry(
		const Stories::ContentLayout &layout,
		float64 scale) const {
	return {
		.rect = QRectF(layout.geometry),
		.controlsOpacity = kStoriesControlsOpacity,
		.fade = layout.fade,
		.scale = scale,
		.roundRadius = layout.radius,
		.topShadowShown = !layout.headerOutside,
	};
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

void OverlayWidget::recountSkipTop() {
	const auto bottom = (!_streamed || !_streamed->controls)
		? height()
		: (_streamed->controls->y() - st::mediaviewCaptionPadding.bottom());
	const auto skipHeightBottom = (height() - bottom);
	_skipTop = _minUsedTop + std::min(
		std::max(
			st::mediaviewCaptionMargin.height(),
			height() - _height - skipHeightBottom),
		skipHeightBottom);
	_availableHeight = height() - skipHeightBottom - _skipTop;
	if (_fullScreenVideo && skipHeightBottom > 0 && _width > 0) {
		const auto h = width() * _height / _width;
		const auto topAllFit = _maxUsedHeight - skipHeightBottom - h;
		if (_skipTop > topAllFit) {
			_skipTop = std::max(topAllFit, 0);
		}
	}
}

void OverlayWidget::resizeContentByScreenSize() {
	if (_stories) {
		const auto content = _stories->finalShownGeometry();
		_x = content.x();
		_y = content.y();
		_w = content.width();
		_h = content.height();
		_zoom = 0;
		updateNavigationControlsGeometry();
		return;
	}
	recountSkipTop();
	const auto availableWidth = width();
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
		_zoomToDefault = countZoomFor(availableWidth, _availableHeight);
		_zoomToScreen = countZoomFor(width(), _maxUsedHeight);
	} else {
		_zoomToDefault = _zoomToScreen = 0;
	}
	const auto usew = _fullScreenVideo ? width() : availableWidth;
	const auto useh = _fullScreenVideo ? _maxUsedHeight : _availableHeight;
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
	_y = _skipTop + (_availableHeight - _h) / 2;
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
	const auto streamVideo = ready && _documentMedia->canBePlayed(_message);
	const auto tryOpenImage = ready
		&& (_document->size < Images::kReadBytesLimit);
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
	if (_stories || _fullScreenVideo) {
		return;
	}
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
	_y = _skipTop - (_height / 2);
	float64 z = (_zoom == kZoomToScreenLevel) ? full : _zoom;
	if (z >= 0) {
		_x = qRound(_x * (z + 1));
		_y = qRound(_y * (z + 1));
	} else {
		_x = qRound(_x / (-z + 1));
		_y = qRound(_y / (-z + 1));
	}
	_x += width() / 2;
	_y += _availableHeight / 2;
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

	// Otherwise dropdownHidden() may be called from the destructor.
	_dropdown.destroy();
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
		_documentLoadingTo = QString();
	}
}

void OverlayWidget::assignMediaPointer(not_null<PhotoData*> photo) {
	_savePhotoVideoWhenLoaded = SavePhotoVideo::None;
	_document = nullptr;
	_documentMedia = nullptr;
	_documentLoadingTo = QString();
	if (_photo != photo) {
		_photo = photo;
		_photoMedia = _photo->createMediaView();
		_photoMedia->wanted(Data::PhotoSize::Small, fileOrigin());
		if (!_photo->hasVideo() || _photo->videoPlaybackFailed()) {
			_photo->load(fileOrigin(), LoadFromCloudOrLocal, true);
		}
	}
}

void OverlayWidget::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	setCursor((active || ClickHandler::getPressed())
		? style::cur_pointer
		: style::cur_default);
	update(QRegion(_saveMsg) + captionGeometry());
}

void OverlayWidget::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	setCursor((pressed || ClickHandler::getActive())
		? style::cur_pointer
		: style::cur_default);
	update(QRegion(_saveMsg) + captionGeometry());
}

rpl::lifetime &OverlayWidget::lifetime() {
	return _surface->lifetime();
}

void OverlayWidget::showSaveMsgFile() {
	File::ShowInFolder(_saveMsgFilename);
}

void OverlayWidget::close() {
	if (isHidden()) {
		return;
	}
	hide();
	if (const auto window = Core::App().activeWindow()) {
		window->reActivate();
	}
	_helper->clearState();
}

void OverlayWidget::minimize() {
	if (isHidden()) {
		return;
	}
	_helper->minimize(_window);
}

void OverlayWidget::toggleFullScreen() {
	toggleFullScreen(!_fullscreen);
}

void OverlayWidget::toggleFullScreen(bool fullscreen) {
	_helper->clearState();
	_fullscreen = fullscreen;
	_windowed = !fullscreen;
	initNormalGeometry();
	if constexpr (Platform::IsMac()) {
		_helper->beforeShow(_fullscreen);
		updateGeometry();
		_helper->afterShow(_fullscreen);
	} else if (_fullscreen) {
		updateGeometry();
		_window->showFullScreen();
	} else {
		_wasWindowedMode = false;
		_window->showNormal();
		updateGeometry();
		_wasWindowedMode = true;
	}
	savePosition();
	_helper->clearState();
}

void OverlayWidget::activateControls() {
	if (!_menu && !_mousePressed && !_stories) {
		_controlsHideTimer.callOnce(st::mediaviewWaitHide);
	}
	if (_fullScreenVideo) {
		if (_streamed && _streamed->controls) {
			_streamed->controls->showAnimated();
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
	if (_stories) {
		_controlsState = ControlsShown;
		_controlsOpacity = anim::value(1);
		_helper->setControlsOpacity(1.);
		return;
	} else if (!force) {
		if (!_dropdown->isHidden()
			|| (_streamed
				&& _streamed->controls
				&& _streamed->controls->hasMenu())
			|| _menu
			|| _mousePressed) {
			return;
		}
	}
	if (_fullScreenVideo && _streamed && _streamed->controls) {
		_streamed->controls->hideAnimated();
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
	if (_stories) {
		_stories->menuShown(false);
	}
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
	const auto screen = _window->screen();
	if (!screen) {
		return;
	}
	base::qt_signal_producer(
		screen,
		&QScreen::geometryChanged
	) | rpl::filter([=] {
		return !isHidden() && !isMinimized() && _fullscreen;
	}) | rpl::start_with_next([=] {
		updateGeometry();
	}, _screenGeometryLifetime);
}

void OverlayWidget::toMessage() {
	if (const auto item = _message) {
		close();
		if (const auto window = findWindow()) {
			window->showMessage(item);
		}
	}
}

void OverlayWidget::notifyFileDialogShown(bool shown) {
	_helper->notifyFileDialogShown(shown);
}

void OverlayWidget::saveAs() {
	if (showCopyMediaRestriction(true)) {
		return;
	} else if (hasCopyMediaRestriction()) {
		Assert(_stories != nullptr);
		showPremiumDownloadPromo();
		return;
	}
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
				name = pattern.isEmpty() ? u".unknown"_q : pattern.replace('*', QString());
			}

			if (pattern.isEmpty()) {
				filter = QString();
			} else {
				filter = mimeType.filterString() + u";;"_q + FileDialog::AllFilesFilter();
			}

			file = FileNameForSave(
				_session,
				tr::lng_save_file(tr::now),
				filter,
				u"doc"_q,
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
				if (_message) {
					auto &manager = Core::App().downloadManager();
					manager.addLoaded({
						.item = _message,
						.document = _document,
					}, file, manager.computeNextStartDate());
				}
			}

			if (bytes.isEmpty()) {
				location.accessDisable();
			}
		} else {
			DocumentSaveClickHandler::SaveAndTrack(
				_message ? _message->fullId() : FullMsgId(),
				_document,
				DocumentSaveClickHandler::Mode::ToNewFile);
			updateControls();
			updateOver(_lastMouseMovePos);
		}
	} else if (_photo && _photo->hasVideo()) {
		constexpr auto large = Data::PhotoSize::Large;
		if (const auto bytes = _photoMedia->videoContent(large); !bytes.isEmpty()) {
			const auto photo = _photo;
			auto filter = u"Video Files (*.mp4);;"_q + FileDialog::AllFilesFilter();
			FileDialog::GetWritePath(
				_window.get(),
				tr::lng_save_video(tr::now),
				filter,
				filedialogDefaultName(
					u"photo"_q,
					u".mp4"_q,
					QString(),
					false,
					_photo->date),
				crl::guard(_window, [=](const QString &result) {
					QFile f(result);
					if (!result.isEmpty()
						&& _photo == photo
						&& f.open(QIODevice::WriteOnly)) {
						f.write(bytes);
					}
				}));
		} else {
			_photo->loadVideo(large, fileOrigin());
			_savePhotoVideoWhenLoaded = SavePhotoVideo::SaveAs;
		}
	} else {
		if (!_photo || !_photoMedia->loaded()) {
			return;
		}

		const auto media = _photoMedia;
		const auto photo = _photo;
		const auto filter = u"JPEG Image (*.jpg);;"_q
			+ FileDialog::AllFilesFilter();
		FileDialog::GetWritePath(
			_window.get(),
			tr::lng_save_photo(tr::now),
			filter,
			filedialogDefaultName(
				u"photo"_q,
				u".jpg"_q,
				QString(),
				false,
				_photo->date),
			crl::guard(_window, [=](const QString &result) {
				if (!result.isEmpty() && _photo == photo) {
					media->saveToFile(result);
				}
			}));
	}
	activate();
}

void OverlayWidget::handleDocumentClick() {
	if (_document->loading()) {
		saveCancel();
	} else {
		_reShow = true;
		Data::ResolveDocument(
			findWindow(),
			_document,
			_message,
			_topicRootId);
		if (_document && _document->loading() && !_radial.animating()) {
			_radial.start(_documentMedia->progress());
		}
		_reShow = false;
	}
}

void OverlayWidget::downloadMedia() {
	if (!_photo && !_document) {
		return;
	} else if (Core::App().settings().askDownloadPath()) {
		return saveAs();
	} else if (hasCopyMediaRestriction()) {
		if (_stories && !hasCopyMediaRestriction(true)) {
			showPremiumDownloadPromo();
		}
		return;
	}

	QString path;
	const auto session = _photo ? &_photo->session() : &_document->session();
	if (Core::App().settings().downloadPath().isEmpty()) {
		path = File::DefaultDownloadPath(session);
	} else if (Core::App().settings().downloadPath() == FileDialog::Tmp()) {
		path = session->local().tempDirectory();
	} else {
		path = Core::App().settings().downloadPath();
	}
	if (path.isEmpty()) return;
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
				} else if (_message) {
					auto &manager = Core::App().downloadManager();
					manager.addLoaded({
						.item = _message,
						.document = _document,
					}, toName, manager.computeNextStartDate());
				}
			}
			if (_stories && !toName.isEmpty()) {
				showSaveMsgToast(toName, tr::lng_mediaview_video_saved_to);
			}
			location.accessDisable();
		} else {
			if (_document->filepath(true).isEmpty()
				&& !_document->loading()) {
				const auto document = _document;
				const auto checkSaveStarted = [=] {
					if (isHidden() || _document != document) {
						return;
					}
					_documentLoadingTo = _document->loadingFilePath();
					if (_stories && _documentLoadingTo.isEmpty()) {
						const auto toName = _document->filepath(true);
						if (!toName.isEmpty()) {
							showSaveMsgToast(
								toName,
								tr::lng_mediaview_video_saved_to);
						}
					}
				};
				DocumentSaveClickHandler::SaveAndTrack(
					_message ? _message->fullId() : FullMsgId(),
					_document,
					DocumentSaveClickHandler::Mode::ToFile,
					crl::guard(_widget, checkSaveStarted));
			} else {
				_saveVisible = computeSaveButtonVisible();
				update(_saveNavOver);
			}
			updateOver(_lastMouseMovePos);
		}
	} else if (_photo && _photo->hasVideo()) {
		if (!_photoMedia->videoContent(Data::PhotoSize::Large).isEmpty()) {
			if (!QDir().exists(path)) {
				QDir().mkpath(path);
			}
			toName = filedialogDefaultName(u"photo"_q, u".mp4"_q, path);
			if (!_photoMedia->saveToFile(toName)) {
				toName = QString();
			}
		} else {
			_photo->loadVideo(Data::PhotoSize::Large, fileOrigin());
			_savePhotoVideoWhenLoaded = SavePhotoVideo::QuickSave;
		}
	} else {
		if (!_photo || !_photoMedia->loaded()) {
			_saveVisible = computeSaveButtonVisible();
			update(_saveNavOver);
		} else {
			if (!QDir().exists(path)) {
				QDir().mkpath(path);
			}
			toName = filedialogDefaultName(u"photo"_q, u".jpg"_q, path);
			const auto saved = _photoMedia->saveToFile(toName);
			if (!saved) {
				toName = QString();
			}
		}
	}
	if (!toName.isEmpty()) {
		showSaveMsgToast(toName, (_stories && _document)
			? tr::lng_mediaview_video_saved_to
			: tr::lng_mediaview_saved_to);
	}
}

void OverlayWidget::saveCancel() {
	if (_document && _document->loading()) {
		_document->cancel();
		if (_documentMedia->canBePlayed(_message)) {
			redisplayContent();
		}
	}
}

void OverlayWidget::showInFolder() {
	if (!_document) return;

	auto filepath = _document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
		if (!_windowed) {
			close();
		}
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
	const auto id = (_message && _message->allowsForward())
		? _message->fullId()
		: FullMsgId();
	if (id) {
		if (!_windowed) {
			close();
		}
		Window::ShowForwardMessagesBox(active.front(), { 1, id });
	}
}

void OverlayWidget::deleteMedia() {
	if (_stories) {
		_stories->deleteRequested();
		return;
	} else if (!_session) {
		return;
	}

	const auto session = _session;
	const auto photo = _photo;
	const auto message = _message;
	const auto deletingPeerPhoto = [&] {
		if (!_message) {
			return true;
		} else if (_photo && _history) {
			if (_history->peer->userpicPhotoId() == _photo->id) {
				return _firstOpenedPeerPhoto;
			}
		}
		return false;
	}();
	close();

	if (const auto window = findWindow()) {
		if (deletingPeerPhoto) {
			if (photo) {
				window->show(
					Ui::MakeConfirmBox({
						.text = tr::lng_delete_photo_sure(),
						.confirmed = crl::guard(_widget, [=] {
							session->api().peerPhoto().clear(photo);
							window->hideLayer();
						}),
						.confirmText = tr::lng_box_delete(),
					}),
					Ui::LayerOption::CloseOther);
			}
		} else if (message) {
			const auto suggestModerateActions = true;
			window->show(
				Box<DeleteMessagesBox>(message, suggestModerateActions),
				Ui::LayerOption::CloseOther);
		}
	}
}

void OverlayWidget::showMediaOverview() {
	if (_menu) {
		_menu->hideMenu(true);
	}
	update();
	if (const auto overviewType = computeOverviewType()) {
		if (!_windowed) {
			close();
		}
		if (SharedMediaOverviewType(*overviewType)) {
			if (const auto window = findWindow()) {
				const auto topic = _topicRootId
					? _history->peer->forumTopicFor(_topicRootId)
					: nullptr;
				if (_topicRootId && !topic) {
					return;
				}
				window->showSection(_topicRootId
					? std::make_shared<Info::Memento>(
						topic,
						Info::Section(*overviewType))
					: std::make_shared<Info::Memento>(
						_history->peer,
						Info::Section(*overviewType)));
			}
		}
	}
}

void OverlayWidget::copyMedia() {
	if (showCopyMediaRestriction()) {
		return;
	}
	_dropdown->hideAnimated(Ui::DropdownMenu::HideOption::IgnoreShow);
	if (_document) {
		const auto filepath = _document->filepath(true);
		auto image = transformedShownContent();
		if (!image.isNull() || !filepath.isEmpty()) {
			auto mime = std::make_unique<QMimeData>();
			if (!image.isNull()) {
				mime->setImageData(std::move(image));
			}
			if (!filepath.isEmpty() && !videoShown()) {
				mime->setUrls({ QUrl::fromLocalFile(filepath) });
				KUrlMimeData::exportUrlsToPortal(mime.get());
			}
			QGuiApplication::clipboard()->setMimeData(mime.release());
		}
	} else if (_photo && _photoMedia->loaded()) {
		_photoMedia->setToClipboard();
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
	if (!_windowed) {
		close();
	}
}

auto OverlayWidget::sharedMediaType() const
-> std::optional<SharedMediaType> {
	using Type = SharedMediaType;
	if (_message) {
		if (const auto media = _message->media()) {
			if (media->webpage()) {
				return std::nullopt;
			}
		}
		if (_photo) {
			if (_message->isService()) {
				return Type::ChatPhoto;
			}
			return Type::PhotoVideo;
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
	if (!_message
		&& _peer
		&& !_user
		&& _photo
		&& _peer->userpicPhotoId() == _photo->id) {
		return SharedMediaKey{
			_history->peer->id,
			MsgId(0), // topicRootId
			_migrated ? _migrated->peer->id : 0,
			SharedMediaType::ChatPhoto,
			_photo
		};
	}
	if (!_message) {
		return std::nullopt;
	}
	const auto isScheduled = _message->isScheduled();
	const auto keyForType = [&](SharedMediaType type) -> SharedMediaKey {
		return {
			_history->peer->id,
			(isScheduled
				? SparseIdsMergedSlice::kScheduledTopicId
				: _topicRootId),
			_migrated ? _migrated->peer->id : 0,
			type,
			(_message->history() == _history
				? _message->id
				: (_message->id - ServerMaxMsgId))
		};
	};
	if (!_message->isRegular() && !isScheduled) {
		return std::nullopt;
	}
	return sharedMediaType() | keyForType;
}

Data::FileOrigin OverlayWidget::fileOrigin() const {
	if (_stories) {
		return _stories->fileOrigin();
	} else if (_message) {
		return _message->fullId();
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
				&& (a.topicRootId == b.topicRootId)
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
	if (!_message && _user && _photo) {
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
	if (_message) {
		if (const auto media = _message->media()) {
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
		if (_message) {
			if (const auto media = _message->media()) {
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

void OverlayWidget::refreshFromLabel() {
	if (_message) {
		_from = _message->originalSender();
		if (const auto info = _message->originalHiddenSenderInfo()) {
			_fromName = info->name;
		} else {
			Assert(_from != nullptr);
			const auto from = _from->migrateTo()
				? _from->migrateTo()
				: _from;
			_fromName = from->name();
		}
	} else {
		_from = _user;
		_fromName = _user ? _user->name() : QString();
	}
}

void OverlayWidget::refreshCaption() {
	_caption = Ui::Text::String();
	const auto caption = [&] {
		if (_stories) {
			return _stories->captionText();
		} else if (_message) {
			if (const auto media = _message->media()) {
				if (media->webpage()) {
					return TextWithEntities();
				}
			}
			return _message->translatedText();
		}
		return TextWithEntities();
	}();
	if (caption.text.isEmpty()) {
		return;
	}

	using namespace HistoryView;
	_caption = Ui::Text::String(st::msgMinWidth);
	const auto duration = (_streamed && _document && _message)
		? DurationForTimestampLinks(_document)
		: 0;
	const auto base = duration
		? TimestampLinkBase(_document, _message->fullId())
		: QString();
	const auto captionRepaint = [=] {
		if (_fullScreenVideo || !_controlsOpacity.current()) {
			return;
		}
		update(captionGeometry());
	};
	const auto context = Core::MarkedTextContext{
		.session = (_stories
			? _storiesSession
			: &_message->history()->session()),
		.customEmojiRepaint = captionRepaint,
	};
	_caption.setMarkedText(
		st::mediaviewCaptionStyle,
		(base.isEmpty()
			? caption
			: AddTimestampLinks(caption, duration, base)),
		(_message
			? Ui::ItemTextOptions(_message)
			: Ui::ItemTextDefaultOptions()),
		context);
	if (_caption.hasSpoilers()) {
		const auto weak = Ui::MakeWeak(widget());
		_caption.setSpoilerLinkFilter([=](const ClickContext &context) {
			return (weak != nullptr);
		});
	}
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
		const auto messageId = _message ? _message->fullId() : FullMsgId();
		View::GroupThumbs::Refresh(
			_session,
			_groupThumbs,
			{ messageId, &*_collageData },
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
	_saveMsgAnimation.stop();
	_saveMsgTimer.cancel();
	_loadRequest = 0;
	_over = _down = Over::None;
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
	return _window->windowHandle();
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

bool OverlayWidget::isActive() const {
	return !isHidden() && !isMinimized() && _window->isActiveWindow();
}

bool OverlayWidget::isHidden() const {
	return _window->isHidden();
}

bool OverlayWidget::isMinimized() const {
	return _window->windowHandle()->windowState() == Qt::WindowMinimized;
}

bool OverlayWidget::isFullScreen() const {
	return _fullscreen;
}

not_null<QWidget*> OverlayWidget::widget() const {
	return _widget;
}

void OverlayWidget::hide() {
	clearBeforeHide();
	applyHideWindowWorkaround();
	_window->hide();
}

void OverlayWidget::setCursor(style::cursor cursor) {
	_widget->setCursor(cursor);
}

void OverlayWidget::setFocus() {
	_body->setFocus();
}

bool OverlayWidget::takeFocusFrom(not_null<QWidget*> window) const {
	return _fullscreen
		&& !isHidden()
		&& !isMinimized()
		&& (_window->screen() == window->screen());
}

void OverlayWidget::activate() {
	_window->raise();
	_window->activateWindow();
	setFocus();
	QApplication::setActiveWindow(_window);
	setFocus();
}

void OverlayWidget::show(OpenRequest request) {
	const auto story = request.story();
	const auto document = story ? story->document() : request.document();
	const auto photo = story ? story->photo() : request.photo();
	const auto contextItem = request.item();
	const auto contextPeer = request.peer();
	const auto contextTopicRootId = request.topicRootId();
	if (!request.continueStreaming() && !request.startTime() && !_reShow) {
		if (_message && (_message == contextItem)) {
			return close();
		} else if (_user && (_user == contextPeer)) {
			if ((_photo && (_photo == photo))
				|| (_document && (_document == document))) {
				return close();
			}
		}
	}
	if (isHidden() || isMinimized()) {
		// Count top notch on macOS before counting geometry.
		_helper->beforeShow(_fullscreen);
	}
	if (_cachedShow) {
		_cachedShow->showOrHideBoxOrLayer(
			v::null,
			Ui::LayerOption::CloseOther,
			anim::type::instant);
	}
	if (photo) {
		if (contextItem && contextPeer) {
			return;
		}
		setSession(&photo->session());

		if (story) {
			setContext(StoriesContext{
				story->peer(),
				story->id(),
				request.storiesContext(),
			});
		} else if (contextPeer) {
			setContext(contextPeer);
		} else if (contextItem) {
			setContext(ItemContext{ contextItem, contextTopicRootId });
		} else {
			setContext(v::null);
		}

		clearControlsState();
		_firstOpenedPeerPhoto = (contextPeer != nullptr);
		assignMediaPointer(photo);

		displayPhoto(photo);
		preloadData(0);
		activateControls();
	} else if (story || document) {
		setSession(document ? &document->session() : &story->session());

		if (story) {
			setContext(StoriesContext{
				story->peer(),
				story->id(),
				request.storiesContext(),
			});
		} else if (contextItem) {
			setContext(ItemContext{ contextItem, contextTopicRootId });
		} else {
			setContext(v::null);
		}

		clearControlsState();

		_streamingStartPaused = false;
		displayDocument(
			document,
			anim::activation::normal,
			request.cloudTheme()
				? *request.cloudTheme()
				: Data::CloudTheme(),
			{ request.continueStreaming(), request.startTime() });
		if (!isHidden()) {
			preloadData(0);
			activateControls();
		}
	}
	if (const auto controller = request.controller()) {
		_openedFrom = base::make_weak(&controller->window());
	}
}

void OverlayWidget::displayPhoto(
		not_null<PhotoData*> photo,
		anim::activation activation) {
	if (photo->isNull()) {
		displayDocument(nullptr, activation);
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
	if (!_stories && _photo->videoCanBePlayed()) {
		initStreaming();
	}

	refreshCaption();

	_blurred = true;
	_down = Over::None;
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
	refreshFromLabel();
	displayFinished(activation);
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
	} else if (_photo) {
		displayPhoto(_photo, anim::activation::background);
	} else {
		displayDocument(_document, anim::activation::background);
	}
}

// Empty messages shown as docs: doc can be nullptr.
void OverlayWidget::displayDocument(
		DocumentData *doc,
		anim::activation activation,
		const Data::CloudTheme &cloud,
		const StartStreaming &startStreaming) {
	_fullScreenVideo = false;
	_staticContent = QImage();
	clearStreaming(_document != doc);
	destroyThemePreview();
	assignMediaPointer(doc);

	_rotation = _document
		? _document->owner().mediaRotation().get(_document)
		: 0;
	_themeCloudData = cloud;
	_radial.stop();

	_touchbarDisplay.fire(TouchBarItemType::None);

	refreshMediaViewer();
	if (_document) {
		if (_document->sticker()) {
			if (const auto image = _documentMedia->getStickerLarge()) {
				setStaticContent(image->original());
			} else if (const auto thumbnail = _documentMedia->thumbnail()) {
				setStaticContent(thumbnail->pix(
					_document->dimensions,
					{ .options = Images::Option::Blur }
				).toImage());
			}
		} else {
			if (_documentMedia->canBePlayed(_message)
				&& initStreaming(startStreaming)) {
			} else if (_document->isVideoFile()) {
				_documentMedia->automaticLoad(fileOrigin(), _message);
				initStreamingThumbnail();
			} else if (_document->isTheme()) {
				_documentMedia->automaticLoad(fileOrigin(), _message);
				initThemePreview();
			} else {
				_documentMedia->automaticLoad(fileOrigin(), _message);
				_document->saveFromDataSilent();
				auto &location = _document->location(true);
				if (location.accessEnable()) {
					setStaticContent(PrepareStaticImage({
						.path = location.name(),
					}));
					if (!_staticContent.isNull()) {
						_touchbarDisplay.fire(TouchBarItemType::Photo);
					}
				} else {
					setStaticContent(PrepareStaticImage({
						.content = _documentMedia->bytes(),
					}));
					if (!_staticContent.isNull()) {
						_touchbarDisplay.fire(TouchBarItemType::Photo);
					}
				}
				location.accessDisable();
			}
		}
	}
	refreshCaption();

	const auto docGeneric = Layout::DocumentGenericPreview::Create(_document);
	_docExt = docGeneric.ext;
	_docIconColor = docGeneric.color;
	_docIcon = docGeneric.icon();

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
					? u"GIF"_q
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
	refreshFromLabel();
	_blurred = false;
	if (_showAsPip && _streamed && _streamed->controls) {
		switchToPip();
	} else {
		displayFinished(activation);
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

void OverlayWidget::displayFinished(anim::activation activation) {
	updateControls();
	if (isHidden()) {
		_helper->beforeShow(_fullscreen);
		moveToScreen();
		showAndActivate();
	} else if (activation == anim::activation::background) {
		return;
	} else if (isMinimized()) {
		_helper->beforeShow(_fullscreen);
		showAndActivate();
	} else {
		activate();
	}
}

void OverlayWidget::showAndActivate() {
	_body->show();
	initNormalGeometry();
	if (_windowed || Platform::IsMac()) {
		_wasWindowedMode = false;
	}
	updateGeometry();
	if (_windowed || Platform::IsMac()) {
		_window->showNormal();
		_wasWindowedMode = true;
	} else if (_fullscreen) {
		_window->showFullScreen();
		if (Platform::IsWindows11OrGreater()) {
			updateGeometry();
		}
	} else {
		_window->showMaximized();
	}
	_helper->afterShow(_fullscreen);
	_widget->update();
	activate();
}

bool OverlayWidget::canInitStreaming() const {
	return (_document && _documentMedia->canBePlayed(_message))
		|| (_photo && _photo->videoCanBePlayed());
}

bool OverlayWidget::initStreaming(const StartStreaming &startStreaming) {
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

	if (startStreaming.continueStreaming) {
		_pip = nullptr;
	}
	if (!startStreaming.continueStreaming
		|| (!_streamed->instance.player().active()
			&& !_streamed->instance.player().finished())) {
		startStreamingPlayer(startStreaming);
	} else {
		updatePlaybackState();
	}
	return true;
}

void OverlayWidget::startStreamingPlayer(
		const StartStreaming &startStreaming) {
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
		? startStreaming.startTime
		: _photo
		? _photo->videoStartPosition()
		: 0;
	restartAtSeekPosition(position);
}

void OverlayWidget::initStreamingThumbnail() {
	Expects(_photo || _document);

	_touchbarDisplay.fire(TouchBarItemType::Video);

	auto userpicImage = std::optional<Image>();
	const auto computePhotoThumbnail = [&] {
		const auto thumbnail = _photoMedia->image(Data::PhotoSize::Thumbnail);
		if (thumbnail) {
			return thumbnail;
		} else if (_peer && _peer->userpicPhotoId() == _photo->id) {
			if (const auto view = _peer->activeUserpicView(); view.cloud) {
				if (!view.cloud->isNull()) {
					userpicImage.emplace(base::duplicate(*view.cloud));
					return &*userpicImage;
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
			_photo->videoLocation(Data::PhotoSize::Large).width(),
			_photo->videoLocation(Data::PhotoSize::Large).height())
		: good
		? good->size()
		: _document->dimensions;
	if (!good && !thumbnail && !blurred) {
		return;
	} else if (size.isEmpty()) {
		return;
	}
	const auto options = VideoThumbOptions(_document);
	const auto goodOptions = (options & ~Images::Option::Blur);
	setStaticContent((good
		? good
		: thumbnail
		? thumbnail
		: blurred
		? blurred
		: Image::BlankMedia().get())->pixNoCache(
			size,
			{
				.options = good ? goodOptions : options,
				.outer = size / style::DevicePixelRatio(),
			}
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

	const auto origin = fileOrigin();
	const auto callback = [=] { waitingAnimationCallback(); };
	if (_document) {
		_streamed = std::make_unique<Streamed>(_document, origin, callback);
	} else {
		_streamed = std::make_unique<Streamed>(_photo, origin, callback);
	}
	if (!_streamed->instance.valid()) {
		_streamed = nullptr;
		return false;
	}
	++_streamedCreated;
	_streamed->instance.setPriority(kOverlayLoaderPriority);
	_streamed->instance.lockPlayer();
	_streamed->withSound = _document
		&& !_document->isSilentVideo()
		&& (_document->isAudioFile()
			|| _document->isVideoFile()
			|| _document->isVoiceMessage()
			|| _document->isVideoMessage());
	if (streamingRequiresControls()) {
		_streamed->controls = std::make_unique<PlaybackControls>(
			_body,
			static_cast<PlaybackControls::Delegate*>(this));
		_streamed->controls->show();
		refreshClipControllerGeometry();
	}
	return true;
}

void OverlayWidget::updatePowerSaveBlocker(
		const Player::TrackState &state) {
	Expects(_streamed != nullptr);

	const auto block = (_document != nullptr)
		&& _document->isVideoFile()
		&& !IsPausedOrPausing(state.state)
		&& !IsStoppedOrStopping(state.state);
	base::UpdatePowerSaveBlocker(
		_streamed->powerSaveBlocker,
		block,
		base::PowerSaveBlockType::PreventDisplaySleep,
		[] { return u"Video playback is active"_q; },
		[=] { return window(); });
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
	const auto id = _themePreviewId = base::RandomValue<uint64>();
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
				using TextTransform = Ui::RoundButton::TextTransform;
				_themeApply.create(
					_body,
					tr::lng_theme_preview_apply(),
					st::themePreviewApplyButton);
				_themeApply->setTextTransform(TextTransform::NoTransform);
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
					_body,
					tr::lng_cancel(),
					st::themePreviewCancelButton);
				_themeCancel->setTextTransform(TextTransform::NoTransform);
				_themeCancel->show();
				_themeCancel->setClickedCallback([this] { close(); });
				if (const auto slug = _themeCloudData.slug; !slug.isEmpty()) {
					_themeShare.create(
						_body,
						tr::lng_theme_share(),
						st::themePreviewCancelButton);
					_themeShare->setTextTransform(TextTransform::NoTransform);
					_themeShare->show();
					_themeShare->setClickedCallback([=] {
						QGuiApplication::clipboard()->setText(
							session->createInternalLinkFull("addtheme/" + slug));
						uiShow()->showToast(
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
	if (!_streamed || !_streamed->controls) {
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto controllerBottom = (_groupThumbs && !_fullScreenVideo)
		? _groupThumbsTop
		: height();
	const auto skip = st::mediaviewCaptionPadding.bottom();
	const auto controllerWidth = std::min(
		st::mediaviewControllerSize.width(),
		width() - 2 * skip);
	_streamed->controls->resize(
		controllerWidth,
		st::mediaviewControllerSize.height());
	_streamed->controls->move(
		(width() - controllerWidth) / 2,
		(controllerBottom
			- _streamed->controls->height()
			- st::mediaviewCaptionPadding.bottom()));
	Ui::SendPendingMoveResizeEvents(_streamed->controls.get());
}

void OverlayWidget::playbackControlsPlay() {
	playbackPauseResume();
	activateControls();
}

void OverlayWidget::playbackControlsPause() {
	playbackPauseResume();
	activateControls();
}

void OverlayWidget::playbackControlsToFullScreen() {
	playbackToggleFullScreen();
	activateControls();
}

void OverlayWidget::playbackControlsFromFullScreen() {
	playbackToggleFullScreen();
	activateControls();
}

void OverlayWidget::playbackControlsToPictureInPicture() {
	if (_streamed && _streamed->controls) {
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

void OverlayWidget::seekRelativeTime(crl::time time) {
	Expects(_streamed != nullptr);

	const auto newTime = std::clamp(
		_streamed->instance.info().video.state.position + time,
		crl::time(0),
		_streamed->instance.info().video.state.duration);
	restartAtSeekPosition(newTime);
}

void OverlayWidget::restartAtProgress(float64 progress) {
	Expects(_streamed != nullptr);

	restartAtSeekPosition(_streamed->instance.info().video.state.duration
		* std::clamp(progress, 0., 1.));
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
	auto options = Streaming::PlaybackOptions{
		.position = position,
		.durationOverride = ((_stories
			&& _document
			&& _document->hasDuration())
			? _document->duration()
			: crl::time(0)),
		.hwAllowed = Core::App().settings().hardwareAcceleratedVideo(),
		.seekable = !_stories,
	};
	if (!_streamed->withSound) {
		options.mode = Streaming::Mode::Video;
		options.loop = !_stories;
	} else {
		Assert(_document != nullptr);
		const auto messageId = _message ? _message->fullId() : FullMsgId();
		options.audioId = AudioMsgId(_document, messageId);
		options.speed = _stories
			? 1.
			: Core::App().settings().videoPlaybackSpeed();
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
		playbackPauseResume();
	}
}

void OverlayWidget::playbackControlsSeekFinished(crl::time position) {
	Expects(_streamed != nullptr);

	_streamingStartPaused = !_streamed->pausedBySeek
		&& !_streamed->instance.player().finished();
	restartAtSeekPosition(position);
	activateControls();
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
	activateControls();
}

void OverlayWidget::playbackControlsVolumeChangeFinished() {
	const auto volume = Core::App().settings().videoVolume();
	if (volume > 0.) {
		_lastPositiveVolume = volume;
	}
	activateControls();
}

void OverlayWidget::playbackControlsSpeedChanged(float64 speed) {
	DEBUG_LOG(("Media playback speed: change to %1.").arg(speed));
	if (_document) {
		DEBUG_LOG(("Media playback speed: %1 to settings.").arg(speed));
		Core::App().settings().setVideoPlaybackSpeed(speed);
		Core::App().saveSettingsDelayed();
	}
	if (_streamed && _streamed->controls && !_stories) {
		DEBUG_LOG(("Media playback speed: %1 to _streamed.").arg(speed));
		_streamed->instance.setSpeed(speed);
	}
}

float64 OverlayWidget::playbackControlsCurrentSpeed(bool lastNonDefault) {
	return Core::App().settings().videoPlaybackSpeed(lastNonDefault);
}

void OverlayWidget::switchToPip() {
	Expects(_streamed != nullptr);
	Expects(_document != nullptr);

	const auto document = _document;
	const auto messageId = _message ? _message->fullId() : FullMsgId();
	const auto topicRootId = _topicRootId;
	const auto closeAndContinue = [=] {
		_showAsPip = false;
		show(OpenRequest(
			findWindow(false),
			document,
			document->owner().message(messageId),
			topicRootId,
			true));
	};
	_showAsPip = true;
	_pip = std::make_unique<PipWrap>(
		_window,
		document,
		_streamed->instance.shared(),
		closeAndContinue,
		[=] { _pip = nullptr; });

	if (const auto raw = _message) {
		raw->history()->owner().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return (raw == item);
		}) | rpl::start_with_next([=] {
			_pip = nullptr;
		}, _pip->lifetime);

		Core::App().passcodeLockChanges(
		) | rpl::filter(
			rpl::mappers::_1
		) | rpl::start_with_next([=] {
			_pip = nullptr;
		}, _pip->lifetime);
	}

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

not_null<Ui::RpWidget*> OverlayWidget::storiesWrap() {
	return _body;
}

std::shared_ptr<ChatHelpers::Show> OverlayWidget::storiesShow() {
	return uiShow();
}

std::shared_ptr<ChatHelpers::Show> OverlayWidget::uiShow() {
	if (!_cachedShow) {
		_cachedShow = std::make_shared<Show>(this);
	}
	return _cachedShow;
}

auto OverlayWidget::storiesStickerOrEmojiChosen()
-> rpl::producer<ChatHelpers::FileChosen> {
	return _storiesStickerOrEmojiChosen.events();
}

auto OverlayWidget::storiesCachedReactionIconFactory()
-> HistoryView::Reactions::CachedIconFactory & {
	return *_cachedReactionIconFactory;
}

void OverlayWidget::storiesJumpTo(
		not_null<Main::Session*> session,
		FullStoryId id,
		Data::StoriesContext context) {
	Expects(_stories != nullptr);
	Expects(id.valid());

	const auto maybeStory = session->data().stories().lookup(id);
	if (!maybeStory) {
		close();
		return;
	}
	const auto story = *maybeStory;
	setContext(StoriesContext{
		story->peer(),
		story->id(),
		context,
	});
	clearStreaming();
	_streamingStartPaused = false;
	v::match(story->media().data, [&](not_null<PhotoData*> photo) {
		displayPhoto(photo, anim::activation::background);
	}, [&](not_null<DocumentData*> document) {
		displayDocument(document, anim::activation::background);
	}, [&](v::null_t) {
		displayDocument(nullptr, anim::activation::background);
	});
}

void OverlayWidget::storiesRedisplay(not_null<Data::Story*> story) {
	Expects(_stories != nullptr);

	clearStreaming();
	_streamingStartPaused = false;
	v::match(story->media().data, [&](not_null<PhotoData*> photo) {
		displayPhoto(photo, anim::activation::background);
	}, [&](not_null<DocumentData*> document) {
		displayDocument(document, anim::activation::background);
	}, [&](v::null_t) {
		displayDocument(nullptr, anim::activation::background);
	});
}

void OverlayWidget::storiesClose() {
	close();
}

bool OverlayWidget::storiesPaused() {
	return _streamed
		&& !_streamed->instance.player().failed()
		&& !_streamed->instance.player().finished()
		&& _streamed->instance.player().active()
		&& _streamed->instance.player().paused();
}

rpl::producer<bool> OverlayWidget::storiesLayerShown() {
	return _layerBg->layerShownValue();
}

void OverlayWidget::storiesTogglePaused(bool paused) {
	if (!_streamed
		|| _streamed->instance.player().failed()
		|| _streamed->instance.player().finished()
		|| !_streamed->instance.player().active()) {
		return;
	} else if (_streamed->instance.player().paused()) {
		if (!paused) {
			_streamed->instance.resume();
			updatePlaybackState();
			playbackPauseMusic();
		}
	} else if (paused) {
		_streamed->instance.pause();
		updatePlaybackState();
	}
}

float64 OverlayWidget::storiesSiblingOver(Stories::SiblingType type) {
	return (type == Stories::SiblingType::Left)
		? overLevel(Over::LeftStories)
		: overLevel(Over::RightStories);
}

void OverlayWidget::storiesRepaint() {
	update();
}

void OverlayWidget::storiesVolumeToggle() {
	playbackControlsVolumeToggled();
}

void OverlayWidget::storiesVolumeChanged(float64 volume) {
	playbackControlsVolumeChanged(volume);
}

void OverlayWidget::storiesVolumeChangeFinished() {
	playbackControlsVolumeChangeFinished();
}

int OverlayWidget::topNotchSkip() const {
	return _fullscreen ? _topNotchSize : 0;
}

int OverlayWidget::storiesTopNotchSkip() {
	return topNotchSkip();
}

void OverlayWidget::playbackToggleFullScreen() {
	Expects(_streamed != nullptr);

	if (_stories
		|| !videoShown()
		|| (!_streamed->controls && !_fullScreenVideo)) {
		return;
	}
	_fullScreenVideo = !_fullScreenVideo;
	if (_fullScreenVideo) {
		_fullScreenZoomCache = _zoom;
	}
	resizeCenteredControls();
	recountSkipTop();
	setZoomLevel(
		_fullScreenVideo ? kZoomToScreenLevel : _fullScreenZoomCache,
		true);
	if (_streamed->controls) {
		if (!_fullScreenVideo) {
			_streamed->controls->showAnimated();
		}
		_streamed->controls->setInFullScreen(_fullScreenVideo);
	}
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

	if (!_streamed->controls && !_stories) {
		return;
	}
	const auto state = _streamed->instance.player().prepareLegacyState();
	if (state.position != kTimeUnknown && state.length != kTimeUnknown) {
		if (_streamed->controls) {
			_streamed->controls->updatePlayback(state);
			_touchbarTrackState.fire_copy(state);
			updatePowerSaveBlocker(state);
		}
		if (_stories) {
			_stories->updatePlayback(state);
		}
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
		use,
		{ .options = (blurred ? Images::Option::Blur : Images::Option()) }
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
		&& !_message
		&& _peer
		&& _peer->hasUserpic()) {
		if (const auto view = _peer->activeUserpicView(); view.cloud) {
			if (!view.cloud->isNull()) {
				auto image = Image(base::duplicate(*view.cloud));
				validatePhotoImage(&image, true);
			}
		}
	}
	if (_staticContent.isNull()) {
		_photoMedia->wanted(Data::PhotoSize::Small, fileOrigin());
	}
}

Ui::GL::ChosenRenderer OverlayWidget::chooseRenderer(
		Ui::GL::Backend backend) {
	_opengl = (backend == Ui::GL::Backend::OpenGL);
	return {
		.renderer = (_opengl
			? std::unique_ptr<Ui::GL::Renderer>(
				std::make_unique<RendererGL>(this))
			: std::make_unique<RendererSW>(this)),
		.backend = backend,
	};
}

void OverlayWidget::paint(not_null<Renderer*> renderer) {
	renderer->paintBackground();
	if (contentShown()) {
		if (videoShown()) {
			renderer->paintTransformedVideoFrame(contentGeometry());
			if (_streamed->instance.player().ready()) {
				_streamed->instance.markFrameShown();
				if (_stories) {
					_stories->ready();
				}
			}
		} else {
			validatePhotoCurrentImage();
			if (_stories && !_blurred) {
				_stories->ready();
			}
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
		if (_stories) {
			using namespace Stories;
			const auto paint = [&](const SiblingView &view, int index) {
				renderer->paintTransformedStaticContent(
					view.image,
					storiesContentGeometry(view.layout, view.scale),
					false, // semi-transparent
					false, // fill transparent background
					index);
				const auto base = (index - 1) * 2;
				const auto userpicSize = view.userpic.size()
					/ view.userpic.devicePixelRatio();
				renderer->paintStoriesSiblingPart(
					base,
					view.userpic,
					QRect(view.userpicPosition, userpicSize));
				const auto nameSize = view.name.size()
					/ view.name.devicePixelRatio();
				renderer->paintStoriesSiblingPart(
					base + 1,
					view.name,
					QRect(view.namePosition, nameSize),
					view.nameOpacity);
			};
			if (const auto left = _stories->sibling(SiblingType::Left)) {
				paint(left, kLeftSiblingTextureIndex);
			}
			if (const auto right = _stories->sibling(SiblingType::Right)) {
				paint(right, kRightSiblingTextureIndex);
			}
		}
	} else if (_stories) {
		// Unsupported story.
	} else if (_themePreviewShown) {
		renderer->paintThemePreview(_themePreviewRect);
	} else if (documentBubbleShown() && !_docRect.isEmpty()) {
		renderer->paintDocumentBubble(_docRect, _docIconRect);
	}
	if (isSaveMsgShown()) {
		renderer->paintSaveMsg(_saveMsg);
	}

	const auto opacity = _fullScreenVideo ? 0. : _controlsOpacity.current();
	if (opacity > 0) {
		paintControls(renderer, opacity);
		if (!_stories) {
			renderer->paintFooter(footerGeometry(), opacity);
		}
		if (!(_stories ? _stories->skipCaption() : _caption.isEmpty())) {
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
	if (const auto radius = _window->manualRoundingRadius()) {
		renderer->paintRoundedCorners(radius);
	}
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
		const auto o = overLevel(Over::Icon);
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
	p.setOpacity(_saveMsgAnimation.value(1.));
	Ui::FillRoundRect(p, outer, st::mediaviewSaveMsgBg, Ui::MediaviewSaveCorners);
	st::mediaviewSaveMsgCheck.paint(p, outer.topLeft() + st::mediaviewSaveMsgCheckPos, width());

	p.setPen(st::mediaviewSaveMsgFg);
	_saveMsgText.draw(p, {
		.position = QPoint(
			outer.x() + st::mediaviewSaveMsgPadding.left(),
			outer.y() + st::mediaviewSaveMsgPadding.top()),
		.availableWidth = outer.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right(),
		.palette = &st::mediaviewTextPalette,
	});
	p.setOpacity(1);
}

bool OverlayWidget::saveControlLocked() const {
	const auto story = _stories ? _stories->story() : nullptr;
	return story
		&& story->canDownloadIfPremium()
		&& !story->canDownloadChecked();
}

void OverlayWidget::paintControls(
		not_null<Renderer*> renderer,
		float64 opacity) {
	struct Control {
		Over state = Over::None;
		bool visible = false;
		const QRect &over;
		const QRect &inner;
		const style::icon &icon;
		bool nonbright = false;
	};
	// When adding / removing controls please update RendererGL.
	const Control controls[] = {
		{
			Over::Left,
			_leftNavVisible,
			_leftNavOver,
			_leftNavIcon,
			_stories ? st::storiesLeft : st::mediaviewLeft,
			true },
		{
			Over::Right,
			_rightNavVisible,
			_rightNavOver,
			_rightNavIcon,
			_stories ? st::storiesRight : st::mediaviewRight,
			true },
		{
			Over::Save,
			_saveVisible,
			_saveNavOver,
			_saveNavIcon,
			(saveControlLocked()
				? st::mediaviewSaveLocked
				: st::mediaviewSave) },
		{
			Over::Share,
			_shareVisible,
			_shareNavOver,
			_shareNavIcon,
			st::mediaviewShare },
		{
			Over::Rotate,
			_rotateVisible,
			_rotateNavOver,
			_rotateNavIcon,
			st::mediaviewRotate },
		{
			Over::More,
			true,
			_moreNavOver,
			_moreNavIcon,
			st::mediaviewMore },
	};

	renderer->paintControlsStart();
	for (const auto &control : controls) {
		if (!control.visible) {
			continue;
		}
		const auto progress = overLevel(control.state);
		const auto bg = progress;
		const auto icon = controlOpacity(progress, control.nonbright);
		renderer->paintControl(
			control.state,
			control.over,
			bg * opacity,
			control.inner,
			icon * opacity,
			control.icon);
	}
}

float64 OverlayWidget::controlOpacity(
		float64 progress,
		bool nonbright) const {
	if (nonbright && _stories) {
		return progress * kStoriesNavOverOpacity
			+ (1. - progress) * kStoriesNavOpacity;
	}
	const auto normal = _windowed
		? kNormalIconOpacity
		: kMaximizedIconOpacity;
	return progress + (1. - progress) * normal;
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
		auto o = _headerHasLink ? overLevel(Over::Header) : 0;
		p.setOpacity(controlOpacity(o) * opacity);
		p.drawText(header.left(), header.top() + st::mediaviewThickFont->ascent, _headerText);

		if (o > 0) {
			p.setOpacity(o * opacity);
			p.drawLine(header.left(), header.top() + st::mediaviewThickFont->ascent + 1, header.right(), header.top() + st::mediaviewThickFont->ascent + 1);
		}
	}

	p.setFont(st::mediaviewFont);

	// name
	if (_nameNav.isValid() && name.intersects(clip)) {
		float64 o = _from ? overLevel(Over::Name) : 0.;
		p.setOpacity(controlOpacity(o) * opacity);
		_fromNameLabel.drawElided(p, name.left(), name.top(), name.width());

		if (o > 0) {
			p.setOpacity(o * opacity);
			p.drawLine(name.left(), name.top() + st::mediaviewFont->ascent + 1, name.right(), name.top() + st::mediaviewFont->ascent + 1);
		}
	}

	// date
	if (date.intersects(clip)) {
		float64 o = overLevel(Over::Date);
		p.setOpacity(controlOpacity(o) * opacity);
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
	const auto full = outer.marginsRemoved(st::mediaviewCaptionPadding);
	const auto inner = full.marginsRemoved(
		_stories ? _stories->repostCaptionPadding() : QMargins());
	if (_stories) {
		p.setOpacity(1.);
		if (_stories->repost()) {
			_stories->drawRepostInfo(p, full.x(), full.y(), full.width());
		}
	} else {
		p.setOpacity(opacity);
		p.setBrush(st::mediaviewCaptionBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(
			outer,
			st::mediaviewCaptionRadius,
			st::mediaviewCaptionRadius);
	}
	if (inner.intersects(clip)) {
		p.setPen(st::mediaviewCaptionFg);
		_caption.draw(p, {
			.position = inner.topLeft(),
			.availableWidth = inner.width(),
			.palette = &st::mediaviewTextPalette,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.pausedEmoji = On(PowerSaving::kEmojiChat),
			.pausedSpoiler = On(PowerSaving::kChatSpoiler),
			.elisionHeight = inner.height(),
			.elisionRemoveFromEnd = _captionSkipBlockWidth,
		});

		if (_captionShowMoreWidth > 0) {
			const auto padding = st::storiesShowMorePadding;
			const auto showMoreLeft = outer.x()
				+ outer.width()
				- padding.right()
				- _captionShowMoreWidth;
			const auto showMoreTop = outer.y()
				+ outer.height()
				- padding.bottom()
				- st::storiesShowMoreFont->height;
			const auto underline = _captionExpandLink
				&& ClickHandler::showAsActive(_captionExpandLink);
			p.setFont(underline
				? st::storiesShowMoreFont->underline()
				: st::storiesShowMoreFont);
			p.drawTextLeft(
				showMoreLeft,
				showMoreTop,
				width(),
				tr::lng_stories_show_more(tr::now));
		}
	}
}

QRect OverlayWidget::captionGeometry() const {
	return _captionRect.marginsAdded(
		st::mediaviewCaptionPadding
	).marginsAdded(
		_stories ? _stories->repostCaptionPadding() : QMargins());
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

bool OverlayWidget::isSaveMsgShown() const {
	return _saveMsgAnimation.animating() || _saveMsgTimer.isActive();
}

void OverlayWidget::handleKeyPress(not_null<QKeyEvent*> e) {
	if (_processingKeyPress) {
		return;
	}
	_processingKeyPress = true;
	const auto guard = gsl::finally([&] { _processingKeyPress = false; });
	const auto key = e->key();
	const auto modifiers = e->modifiers();
	const auto ctrl = modifiers.testFlag(Qt::ControlModifier);
	if (_stories) {
		if (key == Qt::Key_Space && _down != Over::Video) {
			_stories->togglePaused(!_stories->paused());
			return;
		}
	} else if (_streamed) {
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
			} else if (ctrl) {
			} else if (key == Qt::Key_0) {
				activateControls();
				restartAtSeekPosition(0);
			} else if (key >= Qt::Key_1 && key <= Qt::Key_9) {
				activateControls();
				const auto index = int(key - Qt::Key_0);
				restartAtProgress(index / 10.0);
			} else if (key == Qt::Key_Left) {
				activateControls();
				seekRelativeTime(-kSeekTimeMs);
			} else if (key == Qt::Key_Right) {
				activateControls();
				seekRelativeTime(kSeekTimeMs);
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
		if (!moveToNext(1) && _stories) {
			storiesClose();
		}
	} else if (ctrl) {
		if (key == Qt::Key_Plus
			|| key == Qt::Key_Equal
			|| key == Qt::Key_Asterisk
			|| key == ']') {
			zoomIn();
		} else if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
			zoomOut();
		}
	} else if (_stories) {
		_stories->tryProcessKeyInput(e);
	}
}

void OverlayWidget::handleWheelEvent(not_null<QWheelEvent*> e) {
	constexpr auto step = int(QWheelEvent::DefaultDeltasPerStep);

	const auto acceptForJump = !_stories
		&& ((e->source() == Qt::MouseEventNotSynthesized)
			|| (e->source() == Qt::MouseEventSynthesizedBySystem));
	_verticalWheelDelta += e->angleDelta().y();
	while (qAbs(_verticalWheelDelta) >= step) {
		if (_verticalWheelDelta < 0) {
			_verticalWheelDelta += step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomOut();
			} else if (acceptForJump) {
				moveToNext(1);
			}
		} else {
			_verticalWheelDelta -= step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomIn();
			} else if (acceptForJump) {
				moveToNext(-1);
			}
		}
	}
}

void OverlayWidget::setZoomLevel(int newZoom, bool force) {
	if (_stories
		|| (!force && _zoom == newZoom)
		|| (_fullScreenVideo && newZoom != kZoomToScreenLevel)) {
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
		ny = (_y - _availableHeight / 2.) / (z + 1);
	} else {
		nx = (_x - width() / 2.) * (-z + 1);
		ny = (_y - _availableHeight / 2.) * (-z + 1);
	}
	_zoom = newZoom;
	z = (_zoom == kZoomToScreenLevel) ? full : _zoom;
	if (z > 0) {
		_w = qRound(_w * (z + 1));
		_h = qRound(_h * (z + 1));
		_x = qRound(nx * (z + 1) + width() / 2.);
		_y = qRound(ny * (z + 1) + _availableHeight / 2.);
	} else {
		_w = qRound(_w / (-z + 1));
		_h = qRound(_h / (-z + 1));
		_x = qRound(nx / (-z + 1) + width() / 2.);
		_y = qRound(ny / (-z + 1) + _availableHeight / 2.);
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

	const auto &items = _collageData->items;
	if (!_message || index < 0 || index >= items.size()) {
		return { v::null, nullptr };
	}
	if (const auto document = std::get_if<DocumentData*>(&items[index])) {
		return { *document, _message, _topicRootId };
	} else if (const auto photo = std::get_if<PhotoData*>(&items[index])) {
		return { *photo, _message, _topicRootId };
	}
	return { v::null, nullptr };
}

OverlayWidget::Entity OverlayWidget::entityForItemId(const FullMsgId &itemId) const {
	Expects(_session != nullptr);

	if (const auto item = _session->data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				return { photo, item, _topicRootId };
			} else if (const auto document = media->document()) {
				return { document, item, _topicRootId };
			}
		}
		return { v::null, item, _topicRootId };
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
		ItemContext,
		not_null<PeerData*>,
		StoriesContext> context) {
	if (const auto item = std::get_if<ItemContext>(&context)) {
		_message = item->item;
		_history = _message->history();
		_peer = _history->peer;
		_topicRootId = _peer->isForum() ? item->topicRootId : MsgId();
		setStoriesPeer(nullptr);
	} else if (const auto peer = std::get_if<not_null<PeerData*>>(&context)) {
		_peer = *peer;
		_history = _peer->owner().history(_peer);
		_message = nullptr;
		_topicRootId = MsgId();
		setStoriesPeer(nullptr);
	} else if (const auto story = std::get_if<StoriesContext>(&context)) {
		_message = nullptr;
		_topicRootId = MsgId();
		_history = nullptr;
		_peer = nullptr;
		setStoriesPeer(story->peer);
		auto &stories = story->peer->owner().stories();
		const auto maybeStory = stories.lookup(
			{ story->peer->id, story->id });
		if (maybeStory) {
			_stories->show(*maybeStory, story->within);
			_dropdown->raise();
		}
	} else {
		_message = nullptr;
		_topicRootId = MsgId();
		_history = nullptr;
		_peer = nullptr;
		setStoriesPeer(nullptr);
	}
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = _history->owner().history(
				_history->peer->migrateFrom());
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = _history->owner().history(_history->peer->migrateTo());
		}
	}
	_user = _peer ? _peer->asUser() : nullptr;
}

void OverlayWidget::setStoriesPeer(PeerData *peer) {
	const auto session = peer ? &peer->session() : nullptr;
	if (!session && !_storiesSession) {
		Assert(!_stories);
	} else if (!peer) {
		_stories = nullptr;
		_storiesSession = nullptr;
		_storiesChanged.fire({});
		updateNavigationControlsGeometry();
	} else if (_storiesSession != session) {
		_stories = nullptr;
		_storiesSession = session;
		const auto delegate = static_cast<Stories::Delegate*>(this);
		_stories = std::make_unique<Stories::View>(delegate);
		_stories->finalShownGeometryValue(
		) | rpl::skip(1) | rpl::start_with_next([=] {
			updateControlsGeometry();
		}, _stories->lifetime());
		_storiesChanged.fire({});
	}
}

void OverlayWidget::setSession(not_null<Main::Session*> session) {
	if (_session == session) {
		return;
	}

	clearSession();
	_session = session;
	_window->setWindowIcon(Window::CreateIcon(session));

	session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		if (!isHidden()) {
			updateControls();
			checkForSaveLoaded();
		}
	}, _sessionLifetime);

	session->data().documentLoadProgress(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		documentUpdated(document);
	}, _sessionLifetime);

	session->data().itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		changingMsgId(change.newId, change.oldId);
	}, _sessionLifetime);

	session->data().itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (_message == item);
	}) | rpl::start_with_next([=] {
		close();
		clearSession();
	}, _sessionLifetime);

	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		clearSession();
	}, _sessionLifetime);
}

bool OverlayWidget::moveToNext(int delta) {
	if (_stories) {
		return _stories->subjumpFor(delta);
	} else if (!_index) {
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
		setContext(ItemContext{ item, entity.topicRootId });
	} else if (_peer) {
		setContext(_peer);
	} else {
		setContext(v::null);
	}
	clearStreaming();
	_streamingStartPaused = false;
	if (auto photo = std::get_if<not_null<PhotoData*>>(&entity.data)) {
		displayPhoto(*photo);
	} else if (auto document = std::get_if<not_null<DocumentData*>>(&entity.data)) {
		displayDocument(*document);
	} else {
		displayDocument(nullptr);
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
			const auto &[i, ok] = photos.emplace((*photo)->createMediaView());
			(*i)->wanted(Data::PhotoSize::Small, fileOrigin(entity));
			(*photo)->load(fileOrigin(entity), LoadFromCloudOrLocal, true);
		} else if (auto document = std::get_if<not_null<DocumentData*>>(
				&entity.data)) {
			const auto &[i, ok] = documents.emplace(
				(*document)->createMediaView());
			(*i)->thumbnailWanted(fileOrigin(entity));
			if (!(*i)->canBePlayed(entity.item)) {
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
		_down = Over::None;
		if (!ClickHandler::getPressed()) {
			if ((_over == Over::Left && moveToNext(-1))
				|| (_over == Over::Right && moveToNext(1))
				|| (_stories
					&& _over == Over::LeftStories
					&& _stories->jumpFor(-1))
				|| (_stories
					&& _over == Over::RightStories
					&& _stories->jumpFor(1))) {
				_lastAction = position;
			} else if (_over == Over::Name
				|| _over == Over::Date
				|| _over == Over::Header
				|| _over == Over::Save
				|| _over == Over::Share
				|| _over == Over::Rotate
				|| _over == Over::Icon
				|| _over == Over::More
				|| _over == Over::Video) {
				_down = _over;
				if (_over == Over::Video && _stories) {
					_stories->contentPressed(true);
				}
			} else if (!_saveMsg.contains(position) || !isSaveMsgShown()) {
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

	if (_over != Over::Video || button != Qt::LeftButton) {
		return false;
	} else if (_stories) {
		if (ClickHandler::getActive()) {
			return false;
		}
		toggleFullScreen(_windowed);
	} else if (!_streamed) {
		return false;
	} else {
		playbackToggleFullScreen();
		playbackPauseResume();
	}
	return true;
}

void OverlayWidget::snapXY() {
	auto xmin = width() - _w, xmax = 0;
	auto ymin = height() - _h, ymax = _minUsedTop;
	accumulate_min(xmin, (width() - _w) / 2);
	accumulate_max(xmax, (width() - _w) / 2);
	accumulate_min(ymin, _skipTop + (_availableHeight - _h) / 2);
	accumulate_max(ymax, _skipTop + (_availableHeight - _h) / 2);
	accumulate_max(_x, xmin);
	accumulate_min(_x, xmax);
	accumulate_max(_y, ymin);
	accumulate_min(_y, ymax);
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
				if (_w > width() || _h > _maxUsedHeight) {
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

void OverlayWidget::updateOverRect(Over state) {
	using Type = Stories::SiblingType;
	switch (state) {
	case Over::Left:
		update(_stories ? _leftNavIcon : _leftNavOver);
		break;
	case Over::Right:
		update(_stories ? _rightNavIcon : _rightNavOver);
		break;
	case Over::LeftStories:
		update(_stories
			? _stories->sibling(Type::Left).layout.geometry :
			QRect());
		break;
	case Over::RightStories:
		update(_stories
			? _stories->sibling(Type::Right).layout.geometry
			: QRect());
		break;
	case Over::Name: update(_nameNav); break;
	case Over::Date: update(_dateNav); break;
	case Over::Save: update(_saveNavOver); break;
	case Over::Share: update(_shareNavOver); break;
	case Over::Rotate: update(_rotateNavOver); break;
	case Over::Icon: update(_docIconRect); break;
	case Over::Header: update(_headerNav); break;
	case Over::More: update(_moreNavOver); break;
	}
}

bool OverlayWidget::updateOverState(Over newState) {
	bool result = true;
	if (_over != newState) {
		if (!_stories && newState == Over::More && !_ignoringDropdown) {
			_dropdownShowTimer.callOnce(0);
		} else {
			_dropdownShowTimer.cancel();
		}
		updateOverRect(_over);
		updateOverRect(newState);
		if (_over != Over::None) {
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
		if (newState != Over::None) {
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
	if (isSaveMsgShown() && _saveMsg.contains(pos)) {
		auto textState = _saveMsgText.getState(pos - _saveMsg.topLeft() - QPoint(st::mediaviewSaveMsgPadding.left(), st::mediaviewSaveMsgPadding.top()), _saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
		lnk = textState.link;
		lnkhost = this;
	} else if (_captionRect.contains(pos)) {
		auto request = Ui::Text::StateRequestElided();
		const auto lineHeight = st::mediaviewCaptionStyle.font->height;
		request.lines = _captionRect.height() / lineHeight;
		request.removeFromEnd = _captionSkipBlockWidth;
		auto textState = _caption.getStateElided(pos - _captionRect.topLeft(), _captionRect.width(), request);
		lnk = textState.link;
		if (_stories && !lnk) {
			lnk = ensureCaptionExpandLink();
		}
		lnkhost = this;
	} else if (_stories && captionGeometry().contains(pos)) {
		const auto padding = st::mediaviewCaptionPadding;
		const auto handler = _stories->lookupRepostHandler(
			pos - captionGeometry().marginsRemoved(padding).topLeft());
		if (handler) {
			lnk = handler.link;
			lnkhost = handler.host;
			setCursor(style::cur_pointer);
			_cursorOverriden = true;
		}
	} else if (_groupThumbs && _groupThumbsRect.contains(pos)) {
		const auto point = pos - QPoint(_groupThumbsLeft, _groupThumbsTop);
		lnk = _groupThumbs->getState(point);
		lnkhost = this;
	} else if (_stories) {
		lnk = _stories->lookupAreaHandler(pos);
		lnkhost = this;
	}

	// retina
	if (pos.x() == width()) {
		pos.setX(pos.x() - 1);
	}
	if (pos.y() == height()) {
		pos.setY(pos.y() - 1);
	}

	if (_cursorOverriden && (!lnkhost || lnkhost == this)) {
		_cursorOverriden = false;
		setCursor(style::cur_default);
	}
	ClickHandler::setActive(lnk, lnkhost);

	if (_pressed || _dragging) return;

	using SiblingType = Stories::SiblingType;
	if (_fullScreenVideo) {
		updateOverState(Over::Video);
	} else if (_leftNavVisible && _leftNav.contains(pos)) {
		updateOverState(Over::Left);
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		updateOverState(Over::Right);
	} else if (_stories
		&& _stories->sibling(
			SiblingType::Left).layout.geometry.contains(pos)) {
		updateOverState(Over::LeftStories);
	} else if (_stories
		&& _stories->sibling(
			SiblingType::Right).layout.geometry.contains(pos)) {
		updateOverState(Over::RightStories);
	} else if (!_stories && _from && _nameNav.contains(pos)) {
		updateOverState(Over::Name);
	} else if (!_stories
		&& _message
		&& _message->isRegular()
		&& _dateNav.contains(pos)) {
		updateOverState(Over::Date);
	} else if (!_stories && _headerHasLink && _headerNav.contains(pos)) {
		updateOverState(Over::Header);
	} else if (_saveVisible && _saveNav.contains(pos)) {
		updateOverState(Over::Save);
	} else if (_shareVisible && _shareNav.contains(pos)) {
		updateOverState(Over::Share);
	} else if (_rotateVisible && _rotateNav.contains(pos)) {
		updateOverState(Over::Rotate);
	} else if (_document
		&& documentBubbleShown()
		&& _docIconRect.contains(pos)) {
		updateOverState(Over::Icon);
	} else if (_moreNav.contains(pos)) {
		updateOverState(Over::More);
	} else if (contentShown() && finalContentRect().contains(pos)) {
		if (_stories) {
			updateOverState(Over::Video);
		} else if (_streamed
			&& _document
			&& (_document->isVideoFile() || _document->isVideoMessage())) {
			updateOverState(Over::Video);
		} else if (!_streamed && _document && !_documentMedia->loaded()) {
			updateOverState(Over::Icon);
		} else if (_over != Over::None) {
			updateOverState(Over::None);
		}
	} else if (_over != Over::None) {
		updateOverState(Over::None);
	}
}

ClickHandlerPtr OverlayWidget::ensureCaptionExpandLink() {
	if (!_captionExpandLink) {
		const auto toggle = crl::guard(_widget, [=] {
			if (_stories) {
				_stories->showFullCaption();
			}
		});
		_captionExpandLink = std::make_shared<LambdaClickHandler>(toggle);
	}
	return _captionExpandLink;
}

void OverlayWidget::handleMouseRelease(
		QPoint position,
		Qt::MouseButton button) {
	updateOver(position);

	if (const auto activated = ClickHandler::unpressed()) {
		if (activated->url() == u"internal:show_saved_message"_q) {
			showSaveMsgFile();
			return;
		}
		// There may be a mention / hashtag / bot command link.
		// For now activate account for all activated links.
		// findWindow() will activate account.
		ActivateClickHandler(_widget, activated, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.itemId = _message ? _message->fullId() : FullMsgId(),
				.sessionWindow = base::make_weak(findWindow()),
			})
		});
		return;
	}

	if (_over == Over::Name && _down == Over::Name) {
		if (_from) {
			if (!_windowed) {
				close();
			}
			if (const auto window = findWindow(true)) {
				window->showPeerInfo(_from);
				window->window().activate();
			}
		}
	} else if (_over == Over::Date && _down == Over::Date) {
		toMessage();
	} else if (_over == Over::Header && _down == Over::Header) {
		showMediaOverview();
	} else if (_over == Over::Save && _down == Over::Save) {
		downloadMedia();
	} else if (_over == Over::Share && _down == Over::Share && _stories) {
		_stories->shareRequested();
	} else if (_over == Over::Rotate && _down == Over::Rotate) {
		playbackControlsRotate();
	} else if (_over == Over::Icon && _down == Over::Icon) {
		handleDocumentClick();
	} else if (_over == Over::More && _down == Over::More) {
		InvokeQueued(_widget, [=] { showDropdown(); });
	} else if (_over == Over::Video && _down == Over::Video) {
		if (_stories) {
			_stories->contentPressed(false);
		} else if (_streamed) {
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
		} else if (!_windowed
			&& (position - _lastAction).manhattanLength()
				>= st::mediaviewDeltaFromLastAction) {
			if (_themePreviewShown) {
				if (!_themePreviewRect.contains(position)) {
					close();
				}
			} else if (!_document
				|| documentContentShown()
				|| !documentBubbleShown()
				|| !_docRect.contains(position)) {
				if (!_stories || _stories->closeByClickAt(position)) {
					close();
				}
			}
		}
		_pressed = false;
	}
	_down = Over::None;
	if (!isHidden()) {
		activateControls();
	}
}

bool OverlayWidget::handleContextMenu(std::optional<QPoint> position) {
	if (position && !QRect(_x, _y, _w, _h).contains(*position)) {
		return false;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_window,
		st::mediaviewPopupMenu);
	fillContextMenuActions([&](
			const QString &text,
			Fn<void()> handler,
			const style::icon *icon) {
		_menu->addAction(text, std::move(handler), icon);
	});

	if (_menu->empty()) {
		_menu = nullptr;
		return true;
	}
	if (_stories) {
		_stories->menuShown(true);
	}
	_menu->setDestroyedCallback(crl::guard(_widget, [=] {
		if (_stories) {
			_stories->menuShown(false);
		}
		activateControls();
		_receiveMouse = false;
		InvokeQueued(_widget, [=] { receiveMouse(); });
	}));

	using HistoryView::Reactions::AttachSelectorResult;
	const auto attached = _stories
		? _stories->attachReactionsToMenu(_menu.get(), QCursor::pos())
		: AttachSelectorResult::Skipped;
	if (attached == AttachSelectorResult::Failed) {
		_menu = nullptr;
		return true;
	} else if (attached == AttachSelectorResult::Attached) {
		_menu->popupPrepared();
	} else {
		_menu->popup(QCursor::pos());
	}
	activateControls();
	return true;
}

bool OverlayWidget::handleTouchEvent(not_null<QTouchEvent*> e) {
	if (e->device()->type() != base::TouchDevice::TouchScreen) {
		return false;
	} else if (e->type() == QEvent::TouchBegin
		&& !e->touchPoints().isEmpty()
		&& _body->childAt(
			_body->mapFromGlobal(
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
		const auto event = static_cast<QKeyEvent*>(e.get());
		const auto key = event->key();
		const auto ctrl = event->modifiers().testFlag(Qt::ControlModifier);
		if (key == Qt::Key_F && ctrl && _streamed) {
			playbackToggleFullScreen();
			return true;
		} else if (key == Qt::Key_0 && ctrl) {
			zoomReset();
			return true;
		}
		return false;
	} else if (type == QEvent::MouseMove
		|| type == QEvent::MouseButtonPress
		|| type == QEvent::MouseButtonRelease) {
		if (object->isWidgetType()
			&& static_cast<QWidget*>(object.get())->window() == _window) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			const auto mousePosition = _body->mapFromGlobal(
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
	if (!isHidden() && !_hideWorkaround) {
		_hideWorkaround = std::make_unique<Ui::RpWidget>(_window);
		const auto raw = _hideWorkaround.get();
		raw->setGeometry(_window->rect());
		raw->show();
		raw->paintRequest(
		) | rpl::start_with_next([=] {
			if (_hideWorkaround.get() == raw) {
				_hideWorkaround.release();
			}
			QPainter(raw).fillRect(raw->rect(), QColor(0, 1, 0, 1));
			crl::on_main(raw, [=] {
				delete raw;
			});
		}, raw->lifetime());
		raw->update();
		_widget->update();

		if (!Platform::IsMac()) {
			Ui::ForceFullRepaintSync(_window);
		}
		_hideWorkaround = nullptr;
	}
}

Window::SessionController *OverlayWidget::findWindow(bool switchTo) const {
	if (!_session) {
		return nullptr;
	}

	const auto window = _openedFrom.get();
	if (window) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == _session) {
				return controller;
			}
		}
	}

	if (switchTo) {
		auto controllerPtr = (Window::SessionController*)nullptr;
		const auto account = &_session->account();
		const auto sessionWindow = Core::App().windowFor(account);
		const auto anyWindow = (sessionWindow
			&& &sessionWindow->account() == account)
			? sessionWindow
			: window
			? window
			: sessionWindow;
		if (anyWindow) {
			anyWindow->invokeForSessionController(
				&_session->account(),
				_history ? _history->peer.get() : nullptr,
				[&](not_null<Window::SessionController*> newController) {
					controllerPtr = newController;
				});
		}
		return controllerPtr;
	}

	return nullptr;
}

// #TODO unite and check
void OverlayWidget::clearBeforeHide() {
	_message = nullptr;
	_sharedMedia = nullptr;
	_sharedMediaData = std::nullopt;
	_sharedMediaDataKey = std::nullopt;
	_userPhotos = nullptr;
	_userPhotosData = std::nullopt;
	_collage = nullptr;
	_collageData = std::nullopt;
	clearStreaming();
	setStoriesPeer(nullptr);
	_layerBg->hideAll(anim::type::instant);
	assignMediaPointer(nullptr);
	_preloadPhotos.clear();
	_preloadDocuments.clear();
	if (_menu) {
		_menu->hideMenu(true);
	}
	_controlsHideTimer.cancel();
	_controlsState = ControlsShown;
	_controlsOpacity = anim::value(1);
	_helper->setControlsOpacity(1.);
	_groupThumbs = nullptr;
	_groupThumbsRect = QRect();
}

void OverlayWidget::clearAfterHide() {
	_body->hide();
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
	fillContextMenuActions([&](
			const QString &text,
			Fn<void()> handler,
			const style::icon *icon) {
		_dropdown->addAction(text, std::move(handler), icon);
	});
	_dropdown->moveToRight(0, height() - _dropdown->height());
	_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	_dropdown->setFocus();
	if (_stories) {
		_stories->menuShown(true);
	}
}

void OverlayWidget::handleTouchTimer() {
	_touchRightButton = true;
}

void OverlayWidget::updateSaveMsg() {
	update(_saveMsg);
}

void OverlayWidget::findCurrent() {
	using namespace rpl::mappers;
	if (_sharedMediaData) {
		_index = _message
			? _sharedMediaData->indexOf(_message->fullId())
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
			if (_user
				&& (index == count - 1)
				&& SyncUserFallbackPhotoViewer(_user)) {
				_headerText = tr::lng_mediaview_profile_public_photo(tr::now);
			} else if (_user
				&& _user->hasPersonalPhoto()
				&& _photo
				&& (_photo->id == _user->userpicPhotoId())) {
				_headerText = tr::lng_mediaview_profile_photo_by_you(tr::now);
			} else {
				_headerText = tr::lng_mediaview_n_of_amount(
					tr::now,
					lt_n,
					QString::number(index + 1),
					lt_amount,
					QString::number(count));
			}
		}
	} else {
		if (_document) {
			_headerText = _document->filename().isEmpty()
				? tr::lng_mediaview_doc_image(tr::now)
				: _document->filename();
		} else if (_message) {
			_headerText = tr::lng_mediaview_single_photo(tr::now);
		} else if (_user) {
			_headerText = tr::lng_mediaview_profile_photo(tr::now);
		} else if ((_history && _history->peer->isBroadcast())
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

float64 OverlayWidget::overLevel(Over control) const {
	auto i = _animationOpacities.find(control);
	return (i == end(_animationOpacities))
		? (_over == control ? 1. : 0.)
		: i->second.current();
}

} // namespace View
} // namespace Media
