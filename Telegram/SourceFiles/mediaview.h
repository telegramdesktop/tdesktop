/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/radial_animation.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player
namespace Clip {
class Controller;
} // namespace Clip
} // namespace Media

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

namespace Notify {
struct PeerUpdate;
} // namespace Notify

class MediaView : public TWidget, private base::Subscriber, public RPCSender, public ClickHandlerHost {
	Q_OBJECT

public:
	MediaView();

	void setVisible(bool visible) override;

	void showPhoto(not_null<PhotoData*> photo, HistoryItem *context);
	void showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> context);
	void showDocument(not_null<DocumentData*> document, HistoryItem *context);

	void leaveToChildEvent(QEvent *e, QWidget *child) override { // e -- from enterEvent() of child TWidget
		updateOverState(OverNone);
	}
	void enterFromChildEvent(QEvent *e, QWidget *child) override { // e -- from leaveEvent() of child TWidget
		updateOver(mapFromGlobal(QCursor::pos()));
	}

	void close();

	void activateControls();
	void onDocClick();

	void clipCallback(Media::Clip::Notification notification);
	PeerData *ui_getPeerForMouseAction();

	void clearData();

	~MediaView();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

protected:
	void paintEvent(QPaintEvent *e) override;

	void keyPressEvent(QKeyEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void touchEvent(QTouchEvent *e);

	bool event(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
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
	void onMenuDestroy(QObject *obj);
	void receiveMouse();

	void onDropdown();

	void onTouchTimer();

	void updateImage();

	void onVideoPauseResume();
	void onVideoSeekProgress(TimeMs positionMs);
	void onVideoSeekFinished(TimeMs positionMs);
	void onVideoVolumeChanged(float64 volume);
	void onVideoToggleFullScreen();
	void onVideoPlayProgress(const AudioMsgId &audioId);

private:
	enum OverState {
		OverNone,
		OverLeftNav,
		OverRightNav,
		OverClose,
		OverHeader,
		OverName,
		OverDate,
		OverSave,
		OverMore,
		OverIcon,
		OverVideo,
	};

	void updateOver(QPoint mpos);
	void moveToScreen();
	bool moveToNext(int delta);
	void preloadData(int delta);
	struct Entity {
		base::optional_variant<
			not_null<PhotoData*>,
			not_null<DocumentData*>> data;
		HistoryItem *item;
	};
	Entity entityForUserPhotos(int index) const;
	Entity entityForSharedMedia(int index) const;
	Entity entityByIndex(int index) const;
	void setContext(base::optional_variant<
		not_null<HistoryItem*>,
		not_null<PeerData*>> context);

	void refreshLang();
	void showSaveMsgFile();
	void updateMixerVideoVolume() const;

	struct SharedMedia;
	using SharedMediaType = SharedMediaWithLastSlice::Type;
	using SharedMediaKey = SharedMediaWithLastSlice::Key;
	base::optional<SharedMediaType> sharedMediaType() const;
	base::optional<SharedMediaKey> sharedMediaKey() const;
	bool validSharedMedia() const;
	void validateSharedMedia();
	void handleSharedMediaUpdate(SharedMediaWithLastSlice &&update);

	struct UserPhotos;
	using UserPhotosKey = UserPhotosSlice::Key;
	base::optional<UserPhotosKey> userPhotosKey() const;
	bool validUserPhotos() const;
	void validateUserPhotos();
	void handleUserPhotosUpdate(UserPhotosSlice &&update);

	void refreshMediaViewer();
	void refreshNavVisibility();

	void dropdownHidden();
	void updateDocSize();
	void updateControls();
	void updateActions();

	void displayPhoto(not_null<PhotoData*> photo, HistoryItem *item);
	void displayDocument(DocumentData *document, HistoryItem *item);
	void displayFinished();
	void findCurrent();

	void updateCursor();
	void setZoomLevel(int newZoom);

	void updateVideoPlaybackState(const Media::Player::TrackState &state);
	void updateSilentVideoPlaybackState();
	void restartVideoAtSeekPosition(TimeMs positionMs);

	void createClipController();
	void setClipControllerGeometry();

	void initAnimation();
	void createClipReader();
	Images::Options videoThumbOptions() const;

	void initThemePreview();
	void destroyThemePreview();
	void updateThemePreviewGeometry();

	void documentUpdated(DocumentData *doc);
	void changingMsgId(not_null<HistoryItem*> row, MsgId newId);

	// Radial animation interface.
	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	TimeMs radialTimeShift() const;

	void deletePhotosDone(const MTPVector<MTPlong> &result);
	bool deletePhotosFail(const RPCError &error);

	void updateHeader();
	void snapXY();

	void step_state(TimeMs ms, bool timer);
	void step_radial(TimeMs ms, bool timer);

	void zoomIn();
	void zoomOut();
	void zoomReset();
	void zoomUpdate(int32 &newZoom);

	void paintDocRadialLoading(Painter &p, bool radial, float64 radialOpacity);
	void paintThemePreview(Painter &p, QRect clip);

	void updateOverRect(OverState state);
	bool updateOverState(OverState newState);
	float64 overLevel(OverState control) const;

	QBrush _transparentBrush;

	PhotoData *_photo = nullptr;
	DocumentData *_doc = nullptr;
	std::unique_ptr<SharedMedia> _sharedMedia;
	base::optional<SharedMediaWithLastSlice> _sharedMediaData;
	base::optional<SharedMediaWithLastSlice::Key> _sharedMediaDataKey;
	std::unique_ptr<UserPhotos> _userPhotos;
	base::optional<UserPhotosSlice> _userPhotosData;

	QRect _closeNav, _closeNavIcon;
	QRect _leftNav, _leftNavIcon, _rightNav, _rightNavIcon;
	QRect _headerNav, _nameNav, _dateNav;
	QRect _saveNav, _saveNavIcon, _moreNav, _moreNavIcon;
	bool _leftNavVisible = false;
	bool _rightNavVisible = false;
	bool _saveVisible = false;
	bool _headerHasLink = false;
	QString _dateText;
	QString _headerText;

	object_ptr<Media::Clip::Controller> _clipController = { nullptr };
	DocumentData *_autoplayVideoDocument = nullptr;
	bool _fullScreenVideo = false;
	int _fullScreenZoomCache = 0;

	Text _caption;
	QRect _captionRect;

	TimeMs _animStarted;

	int _width = 0;
	int _x = 0, _y = 0, _w = 0, _h = 0;
	int _xStart = 0, _yStart = 0;
	int _zoom = 0; // < 0 - out, 0 - none, > 0 - in
	float64 _zoomToScreen = 0.; // for documents
	QPoint _mStart;
	bool _pressed = false;
	int32 _dragging = 0;
	QPixmap _current;
	Media::Clip::ReaderPointer _gif;
	int32 _full = -1; // -1 - thumb, 0 - medium, 1 - full

	// Video without audio stream playback information.
	bool _videoIsSilent = false;
	bool _videoPaused = false;
	bool _videoStopped = false;
	TimeMs _videoPositionMs = 0;
	TimeMs _videoDurationMs = 0;
	int32 _videoFrequencyMs = 1000; // 1000 ms per second.

	bool fileShown() const;
	bool gifShown() const;
	bool fileBubbleShown() const;
	void stopGif();

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
	Text _fromName;

	base::optional<int> _index; // Index in current _sharedMedia data.
	base::optional<int> _fullIndex; // Index in full shared media.
	base::optional<int> _fullCount;
	FullMsgId _msgid;
	bool _canForwardItem = false;
	bool _canDeleteItem = false;

	mtpRequestId _loadRequest = 0;

	OverState _over = OverNone;
	OverState _down = OverNone;
	QPoint _lastAction, _lastMouseMovePos;
	bool _ignoringDropdown = false;

	BasicAnimation _a_state;

	enum ControlsState {
		ControlsShowing,
		ControlsShown,
		ControlsHiding,
		ControlsHidden,
	};
	ControlsState _controlsState = ControlsShown;
	TimeMs _controlsAnimStarted = 0;
	QTimer _controlsHideTimer;
	anim::value a_cOpacity;
	bool _mousePressed = false;

	Ui::PopupMenu *_menu = nullptr;
	object_ptr<Ui::DropdownMenu> _dropdown;
	object_ptr<QTimer> _dropdownShowTimer;

	struct ActionData {
		QString text;
		const char *member;
	};
	QList<ActionData> _actions;

	bool _receiveMouse = true;

	bool _touchPress = false, _touchMove = false, _touchRightButton = false;
	QTimer _touchTimer;
	QPoint _touchStart;
	QPoint _accumScroll;

	QString _saveMsgFilename;
	TimeMs _saveMsgStarted = 0;
	anim::value _saveMsgOpacity;
	QRect _saveMsg;
	QTimer _saveMsgUpdater;
	Text _saveMsgText;

	typedef QMap<OverState, TimeMs> Showing;
	Showing _animations;
	typedef QMap<OverState, anim::value> ShowingOpacities;
	ShowingOpacities _animOpacities;

	int _verticalWheelDelta = 0;

	bool _themePreviewShown = false;
	uint64 _themePreviewId = 0;
	QRect _themePreviewRect;
	std::unique_ptr<Window::Theme::Preview> _themePreview;
	object_ptr<Ui::RoundButton> _themeApply = { nullptr };
	object_ptr<Ui::RoundButton> _themeCancel = { nullptr };

};
