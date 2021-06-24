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
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_web_page.h"
#include "data/data_cloud_themes.h" // Data::CloudTheme.
#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_open_common.h"

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

namespace Ui {
class PopupMenu;
class LinkButton;
class RoundButton;
namespace GL {
struct ChosenRenderer;
struct Capabilities;
} // namespace GL
} // namespace Ui

namespace Window {
namespace Theme {
struct Preview;
} // namespace Theme
} // namespace Window

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player
namespace Streaming {
struct Information;
struct Update;
struct FrameWithInfo;
enum class Error;
} // namespace Streaming
} // namespace Media

namespace Media::View {

class GroupThumbs;
class Pip;

class OverlayWidget final
	: public ClickHandlerHost
	, private PlaybackControls::Delegate {
public:
	OverlayWidget();
	~OverlayWidget();

	enum class TouchBarItemType {
		Photo,
		Video,
		None,
	};

	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] not_null<QWidget*> widget() const;
	void hide();
	void setCursor(style::cursor cursor);
	void setFocus();
	void activate();

	void show(OpenRequest request);

	//void leaveToChildEvent(QEvent *e, QWidget *child) override {
	//	// e -- from enterEvent() of child TWidget
	//	updateOverState(OverNone);
	//}
	//void enterFromChildEvent(QEvent *e, QWidget *child) override {
	//	// e -- from leaveEvent() of child TWidget
	//	updateOver(mapFromGlobal(QCursor::pos()));
	//}

	void activateControls();
	void close();

	PeerData *ui_getPeerForMouseAction();

	void notifyFileDialogShown(bool shown);

	void clearSession();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	rpl::lifetime &lifetime();

private:
	struct Streamed;
	struct PipWrap;
	class Renderer;
	class RendererSW;
	class RendererGL;

	// If changing, see paintControls()!
	enum OverState {
		OverNone,
		OverLeftNav,
		OverRightNav,
		OverClose,
		OverHeader,
		OverName,
		OverDate,
		OverSave,
		OverRotate,
		OverMore,
		OverIcon,
		OverVideo,
	};
	struct Entity {
		std::variant<
			v::null_t,
			not_null<PhotoData*>,
			not_null<DocumentData*>> data;
		HistoryItem *item;
	};
	enum class SavePhotoVideo {
		None,
		QuickSave,
		SaveAs,
	};
	struct ContentGeometry {
		QRectF rect;
		qreal rotation = 0.;
	};

	[[nodiscard]] not_null<QWindow*> window() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	void update();
	void update(const QRegion &region);

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Capabilities capabilities);
	void paint(not_null<Renderer*> renderer);

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
	float64 playbackControlsCurrentSpeed() override;
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
	void updateImage();

	void clearBeforeHide();
	void clearAfterHide();

	void assignMediaPointer(DocumentData *document);
	void assignMediaPointer(not_null<PhotoData*> photo);

	void updateOver(QPoint mpos);
	void moveToScreen();
	void updateGeometry();
	bool moveToNext(int delta);
	void preloadData(int delta);

	void handleScreenChanged(QScreen *screen);

	bool contentCanBeSaved() const;
	void checkForSaveLoaded();

	Entity entityForUserPhotos(int index) const;
	Entity entityForSharedMedia(int index) const;
	Entity entityForCollage(int index) const;
	Entity entityByIndex(int index) const;
	Entity entityForItemId(const FullMsgId &itemId) const;
	bool moveToEntity(const Entity &entity, int preloadDelta = 0);
	void setContext(std::variant<
		v::null_t,
		not_null<HistoryItem*>,
		not_null<PeerData*>> context);

	void refreshLang();
	void showSaveMsgFile();

	struct SharedMedia;
	using SharedMediaType = SharedMediaWithLastSlice::Type;
	using SharedMediaKey = SharedMediaWithLastSlice::Key;
	std::optional<SharedMediaType> sharedMediaType() const;
	std::optional<SharedMediaKey> sharedMediaKey() const;
	std::optional<SharedMediaType> computeOverviewType() const;
	bool validSharedMedia() const;
	void validateSharedMedia();
	void handleSharedMediaUpdate(SharedMediaWithLastSlice &&update);

	struct UserPhotos;
	using UserPhotosKey = UserPhotosSlice::Key;
	std::optional<UserPhotosKey> userPhotosKey() const;
	bool validUserPhotos() const;
	void validateUserPhotos();
	void handleUserPhotosUpdate(UserPhotosSlice &&update);

	struct Collage;
	using CollageKey = WebPageCollage::Item;
	std::optional<CollageKey> collageKey() const;
	bool validCollage() const;
	void validateCollage();

	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] Data::FileOrigin fileOrigin(const Entity& entity) const;

	void refreshFromLabel(HistoryItem *item);
	void refreshCaption(HistoryItem *item);
	void refreshMediaViewer();
	void refreshNavVisibility();
	void refreshGroupThumbs();

	void dropdownHidden();
	void updateDocSize();
	void updateControls();
	void updateControlsGeometry();

	using MenuCallback = Fn<void(const QString &, Fn<void()>)>;
	void fillContextMenuActions(const MenuCallback &addAction);

	void resizeCenteredControls();
	void resizeContentByScreenSize();

	void displayPhoto(not_null<PhotoData*> photo, HistoryItem *item);
	void displayDocument(
		DocumentData *document,
		HistoryItem *item,
		const Data::CloudTheme &cloud = Data::CloudTheme(),
		bool continueStreaming = false);
	void displayFinished();
	void redisplayContent();
	void findCurrent();

	void updateCursor();
	void setZoomLevel(int newZoom, bool force = false);

	void updatePlaybackState();
	void restartAtSeekPosition(crl::time position);

	void refreshClipControllerGeometry();
	void refreshCaptionGeometry();

	bool initStreaming(bool continueStreaming = false);
	void startStreamingPlayer();
	void initStreamingThumbnail();
	void streamingReady(Streaming::Information &&info);
	[[nodiscard]] bool createStreamingObjects();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);

	void initThemePreview();
	void destroyThemePreview();
	void updateThemePreviewGeometry();

	void documentUpdated(DocumentData *doc);
	void changingMsgId(not_null<HistoryItem*> row, MsgId oldId);

	[[nodiscard]] int finalContentRotation() const;
	[[nodiscard]] QRect finalContentRect() const;
	[[nodiscard]] ContentGeometry contentGeometry() const;
	void updateContentRect();
	void contentSizeChanged();

	// Radial animation interface.
	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	crl::time radialTimeShift() const;

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

	void updateSaveMsgState();

	void updateOverRect(OverState state);
	bool updateOverState(OverState newState);
	float64 overLevel(OverState control) const;

	void checkGroupThumbsAnimation();
	void initGroupThumbs();

	void validatePhotoImage(Image *image, bool blurred);
	void validatePhotoCurrentImage();

	[[nodiscard]] QSize flipSizeByRotation(QSize size) const;

	void applyVideoSize();
	[[nodiscard]] bool videoShown() const;
	[[nodiscard]] QSize videoSize() const;
	[[nodiscard]] bool videoIsGifOrUserpic() const;
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
	bool canInitStreaming() const;

	void applyHideWindowWorkaround();

	Window::SessionController *findWindow() const;

	bool _opengl = false;
	const std::unique_ptr<Ui::RpWidgetWrap> _surface;
	const not_null<QWidget*> _widget;

	base::weak_ptr<Window::Controller> _window;
	Main::Session *_session = nullptr;
	rpl::lifetime _sessionLifetime;
	PhotoData *_photo = nullptr;
	DocumentData *_document = nullptr;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
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

	QRect _closeNav, _closeNavIcon;
	QRect _leftNav, _leftNavIcon, _rightNav, _rightNavIcon;
	QRect _headerNav, _nameNav, _dateNav;
	QRect _rotateNav, _rotateNavIcon, _saveNav, _saveNavIcon, _moreNav, _moreNavIcon;
	bool _leftNavVisible = false;
	bool _rightNavVisible = false;
	bool _saveVisible = false;
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

	int _width = 0;
	int _height = 0;
	int _x = 0, _y = 0, _w = 0, _h = 0;
	int _xStart = 0, _yStart = 0;
	int _zoom = 0; // < 0 - out, 0 - none, > 0 - in
	float64 _zoomToScreen = 0.; // for documents
	float64 _zoomToDefault = 0.;
	QPoint _mStart;
	bool _pressed = false;
	int32 _dragging = 0;
	QImage _staticContent;
	bool _staticContentTransparent = false;
	bool _blurred = true;

	ContentGeometry _oldGeometry;
	Ui::Animations::Simple _geometryAnimation;
	rpl::lifetime _screenGeometryLifetime;
	std::unique_ptr<QObject> _applicationEventFilter;

	std::unique_ptr<Streamed> _streamed;
	std::unique_ptr<PipWrap> _pip;
	int _streamedCreated = 0;
	bool _showAsPip = false;

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

	QRect _photoRadialRect;
	Ui::RadialAnimation _radial;

	History *_migrated = nullptr;
	History *_history = nullptr; // if conversation photos or files overview
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
	FullMsgId _msgid;
	bool _canForwardItem = false;
	bool _canDeleteItem = false;

	mtpRequestId _loadRequest = 0;

	OverState _over = OverNone;
	OverState _down = OverNone;
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
	anim::value _controlsOpacity;
	bool _mousePressed = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	object_ptr<Ui::DropdownMenu> _dropdown;
	base::Timer _dropdownShowTimer;

	bool _receiveMouse = true;

	bool _touchPress = false;
	bool _touchMove = false;
	bool _touchRightButton = false;
	base::Timer _touchTimer;
	QPoint _touchStart;

	QString _saveMsgFilename;
	crl::time _saveMsgStarted = 0;
	anim::value _saveMsgOpacity;
	QRect _saveMsg;
	QImage _saveMsgImage;
	base::Timer _saveMsgUpdater;
	Ui::Text::String _saveMsgText;
	SavePhotoVideo _savePhotoVideoWhenLoaded = SavePhotoVideo::None;

	base::flat_map<OverState, crl::time> _animations;
	base::flat_map<OverState, anim::value> _animationOpacities;

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
