/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/gl/gl_surface.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_web_page.h"
#include "data/data_cloud_themes.h" // Data::CloudTheme.
#include "media/stories/media_stories_delegate.h"
#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_open_common.h"
#include "media/media_common.h"

class History;

namespace anim {
enum class activation : uchar;
} // namespace anim

namespace Data {
class PhotoMedia;
class DocumentMedia;
struct StoriesContext;
} // namespace Data

namespace Ui {
class DropdownMenu;
class PopupMenu;
class LinkButton;
class RoundButton;
class RpWindow;
class LayerManager;
} // namespace Ui

namespace Ui::GL {
class Window;
struct ChosenRenderer;
enum class Backend;
} // namespace Ui::GL

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Platform {
class OverlayWidgetHelper;
} // namespace Platform

namespace Window::Theme {
struct Preview;
} // namespace Window::Theme

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace Media::Streaming {
struct Information;
struct Update;
struct FrameWithInfo;
enum class Error;
} // namespace Media::Streaming

namespace Media::Stories {
class View;
struct ContentLayout;
} // namespace Media::Stories

namespace Media::View {

class GroupThumbs;
class Pip;

class OverlayWidget final
	: public ClickHandlerHost
	, private PlaybackControls::Delegate
	, private Stories::Delegate {
public:
	OverlayWidget();
	~OverlayWidget();

	enum class TouchBarItemType {
		Photo,
		Video,
		None,
	};

	[[nodiscard]] bool isActive() const;
	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] bool isMinimized() const;
	[[nodiscard]] bool isFullScreen() const;
	[[nodiscard]] not_null<QWidget*> widget() const;
	void hide();
	void setCursor(style::cursor cursor);
	void setFocus();
	[[nodiscard]] bool takeFocusFrom(not_null<QWidget*> window) const;
	void activate();

	void show(OpenRequest request);

	//void leaveToChildEvent(QEvent *e, QWidget *child) override {
	//	// e -- from enterEvent() of child TWidget
	//	updateOverState(Over::None);
	//}
	//void enterFromChildEvent(QEvent *e, QWidget *child) override {
	//	// e -- from leaveEvent() of child TWidget
	//	updateOver(mapFromGlobal(QCursor::pos()));
	//}

	void activateControls();
	void close();
	void minimize();
	void toggleFullScreen();
	void toggleFullScreen(bool fullscreen);

	void notifyFileDialogShown(bool shown);

	void clearSession();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	rpl::lifetime &lifetime();

private:
	class Show;
	struct Streamed;
	struct PipWrap;
	struct ItemContext;
	struct StoriesContext;
	class Renderer;
	class RendererSW;
	class RendererGL;
	class SponsoredButton;

	// If changing, see paintControls()!
	enum class Over {
		None,
		Left,
		Right,
		LeftStories,
		RightStories,
		SponsoredButton,
		Header,
		Name,
		Date,
		Save,
		Share,
		Rotate,
		More,
		Icon,
		Video,
		Caption,
	};
	struct Entity {
		std::variant<
			v::null_t,
			not_null<PhotoData*>,
			not_null<DocumentData*>> data;
		HistoryItem *item = nullptr;
		MsgId topicRootId = 0;
	};
	enum class SavePhotoVideo {
		None,
		QuickSave,
		SaveAs,
	};
	struct ContentGeometry {
		QRectF rect;
		qreal rotation = 0.;
		qreal controlsOpacity = 0.;

		// Stories.
		qreal fade = 0.;
		qreal scale = 1.;
		int bottomShadowSkip = 0;
		int roundRadius = 0;
		bool topShadowShown = false;
	};
	struct StartStreaming {
		StartStreaming() : continueStreaming(false), startTime(0) {
		}
		StartStreaming(bool continueStreaming, crl::time startTime)
		: continueStreaming(continueStreaming)
		, startTime(startTime) {
		}
		const bool continueStreaming = false;
		const crl::time startTime = 0;
	};

	[[nodiscard]] not_null<QWindow*> window() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	void update();
	void update(const QRegion &region);

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Backend backend);
	void paint(not_null<Renderer*> renderer);

	void setupWindow();
	void orderWidgets();
	void showAndActivate();
	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleMouseMove(QPoint position);
	bool handleContextMenu(std::optional<QPoint> position);
	bool handleDoubleClick(QPoint position, Qt::MouseButton button);
	bool handleTouchEvent(not_null<QTouchEvent*> e);
	void handleWheelEvent(not_null<QWheelEvent*> e);
	void handleKeyPress(not_null<QKeyEvent*> e);

	void toggleApplicationEventFilter(bool install);
	bool filterApplicationEvent(
		not_null<QObject*> object,
		not_null<QEvent*> e);
	void setSession(not_null<Main::Session*> session);

	void playbackControlsPlay() override;
	void playbackControlsPause() override;
	void playbackControlsSeekProgress(crl::time position) override;
	void playbackControlsSeekFinished(crl::time position) override;
	void playbackControlsVolumeChanged(float64 volume) override;
	float64 playbackControlsCurrentVolume() override;
	void playbackControlsVolumeToggled() override;
	void playbackControlsVolumeChangeFinished() override;
	void playbackControlsSpeedChanged(float64 speed) override;
	float64 playbackControlsCurrentSpeed(bool lastNonDefault) override;
	std::vector<int> playbackControlsQualities() override;
	VideoQuality playbackControlsCurrentQuality() override;
	void playbackControlsQualityChanged(int quality) override;
	void playbackControlsToFullScreen() override;
	void playbackControlsFromFullScreen() override;
	void playbackControlsToPictureInPicture() override;
	void playbackControlsRotate() override;
	void playbackPauseResume();
	void playbackToggleFullScreen();
	void playbackPauseOnCall();
	void playbackResumeOnCall();
	void playbackPauseMusic();
	void switchToPip();
	[[nodiscard]] int topNotchSkip() const;
	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow();

	not_null<Ui::RpWidget*> storiesWrap() override;
	std::shared_ptr<ChatHelpers::Show> storiesShow() override;
	auto storiesStickerOrEmojiChosen()
		-> rpl::producer<ChatHelpers::FileChosen> override;
	void storiesRedisplay(not_null<Data::Story*> story) override;
	void storiesJumpTo(
		not_null<Main::Session*> session,
		FullStoryId id,
		Data::StoriesContext context) override;
	void storiesClose() override;
	bool storiesPaused() override;
	rpl::producer<bool> storiesLayerShown() override;
	void storiesTogglePaused(bool paused) override;
	float64 storiesSiblingOver(Stories::SiblingType type) override;
	void storiesRepaint() override;
	void storiesVolumeToggle() override;
	void storiesVolumeChanged(float64 volume) override;
	void storiesVolumeChangeFinished() override;
	int storiesTopNotchSkip() override;

	void hideControls(bool force = false);
	void subscribeToScreenGeometry();

	void toMessage();
	void saveAs();
	void downloadMedia();
	void saveCancel();
	void showInFolder();
	void forwardMedia();
	void deleteMedia();
	void showMediaOverview();
	void copyMedia();
	void receiveMouse();
	void showAttachedStickers();
	void showDropdown();
	void handleTouchTimer();
	void handleDocumentClick();

	[[nodiscard]] bool canShareAtTime() const;
	[[nodiscard]] TimeId shareAtVideoTimestamp() const;
	void shareAtTime();

	void showSaveMsgToast(const QString &path, auto phrase);
	void showSaveMsgToastWith(
		const QString &path,
		const TextWithEntities &text);
	void updateSaveMsg();

	void clearBeforeHide();
	void clearAfterHide();

	void assignMediaPointer(DocumentData *document);
	void assignMediaPointer(not_null<PhotoData*> photo);

	void updateOver(QPoint mpos);
	void initFullScreen();
	void initNormalGeometry();
	void savePosition();
	void moveToScreen(bool inMove = false);
	void updateGeometry(bool inMove = false);
	void updateGeometryToScreen(bool inMove = false);
	bool moveToNext(int delta);
	void preloadData(int delta);

	void handleScreenChanged(not_null<QScreen*> screen);

	[[nodiscard]] bool computeSaveButtonVisible() const;
	void checkForSaveLoaded();
	void showPremiumDownloadPromo();

	[[nodiscard]] Entity entityForUserPhotos(int index) const;
	[[nodiscard]] Entity entityForSharedMedia(int index) const;
	[[nodiscard]] Entity entityForCollage(int index) const;
	[[nodiscard]] Entity entityByIndex(int index) const;
	[[nodiscard]] Entity entityForItemId(const FullMsgId &itemId) const;
	bool moveToEntity(const Entity &entity, int preloadDelta = 0);

	void setContext(std::variant<
		v::null_t,
		ItemContext,
		not_null<PeerData*>,
		StoriesContext> context);
	void setStoriesPeer(PeerData *peer);

	void refreshLang();
	void showSaveMsgFile();

	struct SharedMedia;
	using SharedMediaType = SharedMediaWithLastSlice::Type;
	using SharedMediaKey = SharedMediaWithLastSlice::Key;
	[[nodiscard]] std::optional<SharedMediaType> sharedMediaType() const;
	[[nodiscard]] std::optional<SharedMediaKey> sharedMediaKey() const;
	[[nodiscard]] std::optional<SharedMediaType> computeOverviewType() const;
	bool validSharedMedia() const;
	void validateSharedMedia();
	void handleSharedMediaUpdate(SharedMediaWithLastSlice &&update);

	struct UserPhotos;
	using UserPhotosKey = UserPhotosSlice::Key;
	[[nodiscard]] std::optional<UserPhotosKey> userPhotosKey() const;
	bool validUserPhotos() const;
	void validateUserPhotos();
	void handleUserPhotosUpdate(UserPhotosSlice &&update);

	struct Collage;
	using CollageKey = WebPageCollage::Item;
	[[nodiscard]] std::optional<CollageKey> collageKey() const;
	bool validCollage() const;
	void validateCollage();

	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] Data::FileOrigin fileOrigin(const Entity& entity) const;

	void refreshFromLabel();
	void refreshCaption();
	void refreshMediaViewer();
	void refreshNavVisibility();
	void refreshGroupThumbs();

	void dropdownHidden();
	void updateDocSize();
	void updateControls();
	void updateControlsGeometry();
	void updateNavigationControlsGeometry();

	void fillContextMenuActions(const Ui::Menu::MenuCallback &addAction);

	void resizeCenteredControls();
	void resizeContentByScreenSize();
	void recountSkipTop();

	void displayPhoto(
		not_null<PhotoData*> photo,
		anim::activation activation = anim::activation::normal);
	void displayDocument(
		DocumentData *document,
		anim::activation activation = anim::activation::normal,
		const Data::CloudTheme &cloud = Data::CloudTheme(),
		const StartStreaming &startStreaming = StartStreaming());
	void displayFinished(anim::activation activation);
	void redisplayContent();
	void findCurrent();

	void updateCursor();
	void setZoomLevel(int newZoom, bool force = false);

	void updatePlaybackState();
	void seekRelativeTime(crl::time time);
	void restartAtProgress(float64 progress);
	void restartAtSeekPosition(crl::time position);

	void refreshClipControllerGeometry();
	void refreshCaptionGeometry();

	bool initStreaming(
		const StartStreaming &startStreaming = StartStreaming());
	void startStreamingPlayer(const StartStreaming &startStreaming);
	void initStreamingThumbnail();
	void streamingReady(Streaming::Information &&info);
	[[nodiscard]] bool createStreamingObjects();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);
	void updatePowerSaveBlocker(const Player::TrackState &state);

	void initThemePreview();
	void destroyThemePreview();
	void updateThemePreviewGeometry();

	void initSponsoredButton();
	void refreshSponsoredButtonGeometry();
	void refreshSponsoredButtonWidth();

	void documentUpdated(not_null<DocumentData*> document);
	void changingMsgId(FullMsgId newId, MsgId oldId);

	[[nodiscard]] int finalContentRotation() const;
	[[nodiscard]] QRect finalContentRect() const;
	[[nodiscard]] ContentGeometry contentGeometry() const;
	[[nodiscard]] ContentGeometry storiesContentGeometry(
		const Stories::ContentLayout &layout,
		float64 scale = 1.) const;
	void updateContentRect();
	void contentSizeChanged();

	// Radial animation interface.
	[[nodiscard]] float64 radialProgress() const;
	[[nodiscard]] bool radialLoading() const;
	[[nodiscard]] QRect radialRect() const;
	void radialStart();
	[[nodiscard]] crl::time radialTimeShift() const;

	void updateHeader();
	void snapXY();

	void clearControlsState();
	bool stateAnimationCallback(crl::time ms);
	bool radialAnimationCallback(crl::time now);
	void waitingAnimationCallback();
	bool updateControlsAnimation(crl::time now);

	void zoomIn();
	void zoomOut();
	void zoomReset();
	void zoomUpdate(int32 &newZoom);

	void paintRadialLoading(not_null<Renderer*> renderer);
	void paintRadialLoadingContent(
		Painter &p,
		QRect inner,
		bool radial,
		float64 radialOpacity) const;
	void paintThemePreviewContent(Painter &p, QRect outer, QRect clip);
	void paintDocumentBubbleContent(
		Painter &p,
		QRect outer,
		QRect icon,
		QRect clip) const;
	void paintSaveMsgContent(Painter &p, QRect outer, QRect clip);
	void paintControls(not_null<Renderer*> renderer, float64 opacity);
	void paintFooterContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity);
	[[nodiscard]] QRect footerGeometry() const;
	void paintCaptionContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity);
	[[nodiscard]] QRect captionGeometry() const;
	void paintGroupThumbsContent(
		Painter &p,
		QRect outer,
		QRect clip,
		float64 opacity);

	[[nodiscard]] float64 controlOpacity(
		float64 progress,
		bool nonbright = false) const;
	[[nodiscard]] bool isSaveMsgShown() const;

	void updateOverRect(Over state);
	bool updateOverState(Over newState);
	float64 overLevel(Over control) const;

	void checkGroupThumbsAnimation();
	void initGroupThumbs();

	void validatePhotoImage(Image *image, bool blurred);
	void validatePhotoCurrentImage();

	[[nodiscard]] bool hasCopyMediaRestriction(
		bool skipPremiumCheck = false) const;
	[[nodiscard]] bool showCopyMediaRestriction(
		bool skipPRemiumCheck = false);

	[[nodiscard]] QSize flipSizeByRotation(QSize size) const;

	void applyVideoSize();
	[[nodiscard]] bool videoShown() const;
	[[nodiscard]] QSize videoSize() const;
	[[nodiscard]] bool streamingRequiresControls() const;
	[[nodiscard]] QImage videoFrame() const; // ARGB (changes prepare format)
	[[nodiscard]] QImage currentVideoFrameImage() const; // RGB (may convert)
	[[nodiscard]] Streaming::FrameWithInfo videoFrameWithInfo() const; // YUV
	[[nodiscard]] int streamedIndex() const;
	[[nodiscard]] QImage transformedShownContent() const;
	[[nodiscard]] QImage transformShownContent(
		QImage content,
		int rotation) const;
	[[nodiscard]] bool documentContentShown() const;
	[[nodiscard]] bool documentBubbleShown() const;
	void setStaticContent(QImage image);
	[[nodiscard]] bool contentShown() const;
	[[nodiscard]] bool opaqueContentShown() const;
	void clearStreaming(bool savePosition = true);
	[[nodiscard]] bool canInitStreaming() const;
	[[nodiscard]] bool saveControlLocked() const;
	void applyVideoQuality(VideoQuality value);

	[[nodiscard]] bool topShadowOnTheRight() const;
	void applyHideWindowWorkaround();
	[[nodiscard]] ClickHandlerPtr ensureCaptionExpandLink();

	Window::SessionController *findWindow(bool switchTo = true) const;

	bool _opengl = false;
	const std::unique_ptr<Ui::GL::Window> _wrap;
	const not_null<Ui::RpWindow*> _window;
	const std::unique_ptr<Platform::OverlayWidgetHelper> _helper;
	const not_null<Ui::RpWidget*> _body;
	const std::unique_ptr<Ui::RpWidget> _titleBugWorkaround;
	const std::unique_ptr<Ui::RpWidgetWrap> _surface;
	const not_null<QWidget*> _widget;
	QRect _normalGeometry;
	bool _wasWindowedMode = false;
	bool _fullscreenInited = false;
	bool _normalGeometryInited = false;
	bool _fullscreen = true;
	bool _windowed = false;

	base::weak_ptr<Window::Controller> _openedFrom;
	Main::Session *_session = nullptr;
	rpl::lifetime _sessionLifetime;
	PhotoData *_photo = nullptr;
	DocumentData *_document = nullptr;
	DocumentData *_chosenQuality = nullptr;
	PhotoData *_videoCover = nullptr;
	Media::VideoQuality _quality;
	QString _documentLoadingTo;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	std::shared_ptr<Data::PhotoMedia> _videoCoverMedia;
	base::flat_set<std::shared_ptr<Data::PhotoMedia>> _preloadPhotos;
	base::flat_set<std::shared_ptr<Data::DocumentMedia>> _preloadDocuments;
	int _rotation = 0;
	std::unique_ptr<SharedMedia> _sharedMedia;
	std::optional<SharedMediaWithLastSlice> _sharedMediaData;
	std::optional<SharedMediaWithLastSlice::Key> _sharedMediaDataKey;
	std::unique_ptr<UserPhotos> _userPhotos;
	std::optional<UserPhotosSlice> _userPhotosData;
	std::unique_ptr<Collage> _collage;
	std::optional<WebPageCollage> _collageData;

	QRect _leftNav, _leftNavOver, _leftNavIcon;
	QRect _rightNav, _rightNavOver, _rightNavIcon;
	QRect _headerNav, _nameNav, _dateNav;
	QRect _rotateNav, _rotateNavOver, _rotateNavIcon;
	QRect _shareNav, _shareNavOver, _shareNavIcon;
	QRect _saveNav, _saveNavOver, _saveNavIcon;
	QRect _moreNav, _moreNavOver, _moreNavIcon;
	bool _leftNavVisible = false;
	bool _rightNavVisible = false;
	bool _saveVisible = false;
	bool _shareVisible = false;
	bool _rotateVisible = false;
	bool _headerHasLink = false;
	QString _dateText;
	QString _headerText;

	bool _streamingStartPaused = false;
	bool _fullScreenVideo = false;
	int _fullScreenZoomCache = 0;
	float64 _lastPositiveVolume = 1.;

	std::unique_ptr<GroupThumbs> _groupThumbs;
	QRect _groupThumbsRect;
	int _groupThumbsAvailableWidth = 0;
	int _groupThumbsLeft = 0;
	int _groupThumbsTop = 0;
	Ui::Text::String _caption;
	QRect _captionRect;
	ClickHandlerPtr _captionExpandLink;
	int _captionShowMoreWidth = 0;
	int _captionSkipBlockWidth = 0;

	int _topNotchSize = 0;
	int _width = 0;
	int _height = 0;
	int _skipTop = 0;
	int _availableHeight = 0;
	int _minUsedTop = 0; // Geometry without top notch on macOS.
	int _maxUsedHeight = 0;
	int _x = 0, _y = 0, _w = 0, _h = 0;
	int _xStart = 0, _yStart = 0;
	int _zoom = 0; // < 0 - out, 0 - none, > 0 - in
	float64 _zoomToScreen = 0.; // for documents
	float64 _zoomToDefault = 0.;
	QPoint _mStart;
	bool _pressed = false;
	bool _cursorOverriden = false;
	int32 _dragging = 0;
	QImage _staticContent;
	bool _staticContentTransparent = false;
	bool _blurred = true;
	bool _reShow = false;

	ContentGeometry _oldGeometry;
	Ui::Animations::Simple _geometryAnimation;
	rpl::lifetime _screenGeometryLifetime;
	std::unique_ptr<QObject> _applicationEventFilter;

	std::unique_ptr<Streamed> _streamed;
	std::unique_ptr<PipWrap> _pip;
	QImage _streamedQualityChangeFrame;
	crl::time _streamedPosition = 0;
	int _streamedCreated = 0;
	bool _streamedQualityChangeFinished = false;
	bool _showAsPip = false;

	Qt::Orientations _flip;

	std::unique_ptr<Stories::View> _stories;
	std::shared_ptr<Show> _cachedShow;
	rpl::event_stream<> _storiesChanged;
	Main::Session *_storiesSession = nullptr;
	rpl::event_stream<ChatHelpers::FileChosen> _storiesStickerOrEmojiChosen;
	std::unique_ptr<Ui::LayerManager> _layerBg;

	const style::icon *_docIcon = nullptr;
	style::color _docIconColor;
	QString _docName, _docSize, _docExt;
	int _docNameWidth = 0, _docSizeWidth = 0, _docExtWidth = 0;
	QRect _docRect, _docIconRect;
	QImage _docRectImage;
	int _docThumbx = 0, _docThumby = 0, _docThumbw = 0;
	object_ptr<Ui::LinkButton> _docDownload;
	object_ptr<Ui::LinkButton> _docSaveAs;
	object_ptr<Ui::LinkButton> _docCancel;

	QRect _bottomShadowRect;
	QRect _topShadowRect;
	rpl::variable<bool> _topShadowRight = false;

	QRect _photoRadialRect;
	Ui::RadialAnimation _radial;

	History *_migrated = nullptr;
	History *_history = nullptr; // if conversation photos or files overview
	MsgId _topicRootId = 0;
	PeerData *_peer = nullptr;
	UserData *_user = nullptr; // if user profile photos overview

	// We save the information about the reason of the current mediaview show:
	// did we open a peer profile photo or a photo from some message.
	// We use it when trying to delete a photo: if we've opened a peer photo,
	// then we'll delete group photo instead of the corresponding message.
	bool _firstOpenedPeerPhoto = false;

	PeerData *_from = nullptr;
	QString _fromName;
	Ui::Text::String _fromNameLabel;

	std::optional<int> _index; // Index in current _sharedMedia data.
	std::optional<int> _fullIndex; // Index in full shared media.
	std::optional<int> _fullCount;
	HistoryItem *_message = nullptr;

	mtpRequestId _loadRequest = 0;

	Over _over = Over::None;
	Over _down = Over::None;
	QPoint _lastAction, _lastMouseMovePos;
	bool _ignoringDropdown = false;

	Ui::Animations::Basic _stateAnimation;

	enum ControlsState {
		ControlsShowing,
		ControlsShown,
		ControlsHiding,
		ControlsHidden,
	};
	ControlsState _controlsState = ControlsShown;
	crl::time _controlsAnimStarted = 0;
	base::Timer _controlsHideTimer;
	anim::value _controlsOpacity = { 1. };
	bool _mousePressed = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	object_ptr<Ui::DropdownMenu> _dropdown;
	base::Timer _dropdownShowTimer;

	base::unique_qptr<SponsoredButton> _sponsoredButton;

	bool _receiveMouse = true;
	bool _processingKeyPress = false;

	bool _touchPress = false;
	bool _touchMove = false;
	bool _touchRightButton = false;
	base::Timer _touchTimer;
	QPoint _touchStart;

	QString _saveMsgFilename;
	QRect _saveMsg;
	Ui::Text::String _saveMsgText;
	SavePhotoVideo _savePhotoVideoWhenLoaded = SavePhotoVideo::None;
	// _saveMsgAnimation -> _saveMsgTimer -> _saveMsgAnimation.
	Ui::Animations::Simple _saveMsgAnimation;
	base::Timer _saveMsgTimer;

	base::flat_map<Over, crl::time> _animations;
	base::flat_map<Over, anim::value> _animationOpacities;

	rpl::event_stream<Media::Player::TrackState> _touchbarTrackState;
	rpl::event_stream<TouchBarItemType> _touchbarDisplay;
	rpl::event_stream<bool> _touchbarFullscreenToggled;

	int _verticalWheelDelta = 0;

	bool _themePreviewShown = false;
	uint64 _themePreviewId = 0;
	QRect _themePreviewRect;
	std::unique_ptr<Window::Theme::Preview> _themePreview;
	object_ptr<Ui::RoundButton> _themeApply = { nullptr };
	object_ptr<Ui::RoundButton> _themeCancel = { nullptr };
	object_ptr<Ui::RoundButton> _themeShare = { nullptr };
	Data::CloudTheme _themeCloudData;

	std::unique_ptr<Ui::RpWidget> _hideWorkaround;

};

} // namespace Media::View
