/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_web_page.h"
#include "data/data_cloud_themes.h" // Data::CloudTheme.
#include "media/view/media_view_playback_controls.h"

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

namespace Ui {
class PopupMenu;
class LinkButton;
class RoundButton;
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
enum class Error;
} // namespace Streaming
} // namespace Media

namespace Media {
namespace View {

class GroupThumbs;
class Pip;

#if defined Q_OS_MAC && !defined OS_MAC_OLD
#define USE_OPENGL_OVERLAY_WIDGET
#endif // Q_OS_MAC && !OS_MAC_OLD

struct OverlayParentTraits : Ui::RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = false;
};

#ifdef USE_OPENGL_OVERLAY_WIDGET
using OverlayParent = Ui::RpWidgetWrap<QOpenGLWidget, OverlayParentTraits>;
#else // USE_OPENGL_OVERLAY_WIDGET
using OverlayParent = Ui::RpWidgetWrap<QWidget, OverlayParentTraits>;
#endif // USE_OPENGL_OVERLAY_WIDGET

class OverlayWidget final
	: public OverlayParent
	, public ClickHandlerHost
	, private PlaybackControls::Delegate {
	Q_OBJECT

public:
	OverlayWidget();

	enum class TouchBarItemType {
		Photo,
		Video,
		None,
	};

	void showPhoto(not_null<PhotoData*> photo, HistoryItem *context);
	void showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> context);
	void showDocument(
		not_null<DocumentData*> document,
		HistoryItem *context);
	void showTheme(
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloud);

	void leaveToChildEvent(QEvent *e, QWidget *child) override { // e -- from enterEvent() of child TWidget
		updateOverState(OverNone);
	}
	void enterFromChildEvent(QEvent *e, QWidget *child) override { // e -- from leaveEvent() of child TWidget
		updateOver(mapFromGlobal(QCursor::pos()));
	}

	void close();

	void activateControls();
	void onDocClick();

	PeerData *ui_getPeerForMouseAction();

	void notifyFileDialogShown(bool shown);

	void clearSession();

	~OverlayWidget();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

private Q_SLOTS:
	void onHideControls(bool force = false);

	void onScreenResized(int screen);

	void onToMessage();
	void onSaveAs();
	void onDownload();
	void onSaveCancel();
	void onShowInFolder();
	void onForward();
	void onDelete();
	void onOverview();
	void onCopy();
	void receiveMouse();
	void onPhotoAttachedStickers();
	void onDocumentAttachedStickers();

	void onDropdown();

	void onTouchTimer();

	void updateImage();

private:
	struct Streamed;
	struct PipWrap;

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

	void paintEvent(QPaintEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void keyPressEvent(QKeyEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void touchEvent(QTouchEvent *e);

	bool eventHook(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	void setVisibleHook(bool visible) override;

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

	void assignMediaPointer(DocumentData *document);
	void assignMediaPointer(not_null<PhotoData*> photo);

	void updateOver(QPoint mpos);
	void moveToScreen();
	void updateGeometry();
	bool moveToNext(int delta);
	void preloadData(int delta);

	void handleVisibleChanged(bool visible);
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
	void updateMixerVideoVolume() const;

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

	void showDocument(
		not_null<DocumentData*> document,
		HistoryItem *context,
		const Data::CloudTheme &cloud,
		bool continueStreaming);
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

	[[nodiscard]] int contentRotation() const;
	[[nodiscard]] QRect contentRect() const;
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

	void paintRadialLoading(Painter &p, bool radial, float64 radialOpacity);
	void paintRadialLoadingContent(
		Painter &p,
		QRect inner,
		bool radial,
		float64 radialOpacity) const;
	void paintThemePreview(Painter &p, QRect clip);

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
	[[nodiscard]] QImage videoFrame() const;
	[[nodiscard]] QImage videoFrameForDirectPaint() const;
	[[nodiscard]] QImage transformVideoFrame(QImage frame) const;
	[[nodiscard]] QImage transformStaticContent(QPixmap content) const;
	[[nodiscard]] bool documentContentShown() const;
	[[nodiscard]] bool documentBubbleShown() const;
	void paintTransformedVideoFrame(Painter &p);
	void paintTransformedStaticContent(Painter &p);
	void clearStreaming(bool savePosition = true);
	bool canInitStreaming() const;

	void applyHideWindowWorkaround();

	QBrush _transparentBrush;

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
	QPixmap _staticContent;
	bool _blurred = true;

	std::unique_ptr<Streamed> _streamed;
	std::unique_ptr<PipWrap> _pip;
	bool _showAsPip = false;

	const style::icon *_docIcon = nullptr;
	style::color _docIconColor;
	QString _docName, _docSize, _docExt;
	int _docNameWidth = 0, _docSizeWidth = 0, _docExtWidth = 0;
	QRect _docRect, _docIconRect;
	int _docThumbx = 0, _docThumby = 0, _docThumbw = 0;
	object_ptr<Ui::LinkButton> _docDownload;
	object_ptr<Ui::LinkButton> _docSaveAs;
	object_ptr<Ui::LinkButton> _docCancel;

	QRect _photoRadialRect;
	Ui::RadialAnimation _radial;
	QImage _radialCache;

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
	QPoint _accumScroll;

	QString _saveMsgFilename;
	crl::time _saveMsgStarted = 0;
	anim::value _saveMsgOpacity;
	QRect _saveMsg;
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

	bool _wasRepainted = false;

};

} // namespace View
} // namespace Media
