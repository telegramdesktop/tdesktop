/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mediaview.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/text_options.h"
#include "media/media_clip_reader.h"
#include "media/view/media_clip_controller.h"
#include "media/view/media_view_group_thumbs.h"
#include "media/media_audio.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_peer_menu.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "messenger.h"
#include "layout.h"
#include "storage/file_download.h"
#include "calls/calls_instance.h"
#include "styles/style_mediaview.h"
#include "styles/style_history.h"

namespace {

constexpr auto kPreloadCount = 4;

// Preload X message ids before and after current.
constexpr auto kIdsLimit = 48;

// Preload next messages if we went further from current than that.
constexpr auto kIdsPreloadAfter = 28;

} // namespace

struct MediaView::SharedMedia {
	SharedMedia(SharedMediaWithLastSlice::Key key) : key(key) {
	}

	SharedMediaWithLastSlice::Key key;
	rpl::lifetime lifetime;
};

struct MediaView::UserPhotos {
	UserPhotos(UserPhotosSlice::Key key) : key(key) {
	}

	UserPhotosSlice::Key key;
	rpl::lifetime lifetime;
};

MediaView::MediaView()
: TWidget(nullptr)
, _transparentBrush(style::transparentPlaceholderBrush())
, _animStarted(getms())
, _docDownload(this, lang(lng_media_download), st::mediaviewFileLink)
, _docSaveAs(this, lang(lng_mediaview_save_as), st::mediaviewFileLink)
, _docCancel(this, lang(lng_cancel), st::mediaviewFileLink)
, _radial(animation(this, &MediaView::step_radial))
, _lastAction(-st::mediaviewDeltaFromLastAction, -st::mediaviewDeltaFromLastAction)
, _a_state(animation(this, &MediaView::step_state))
, _dropdown(this, st::mediaviewDropdownMenu)
, _dropdownShowTimer(this) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	TextCustomTagsMap custom;
	custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
	_saveMsgText.setRichText(st::mediaviewSaveMsgStyle, lang(lng_mediaview_saved), Ui::DialogTextOptions(), custom);
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::mediaviewSaveMsgPadding.left() + st::mediaviewSaveMsgPadding.right(), st::mediaviewSaveMsgStyle.font->height + st::mediaviewSaveMsgPadding.top() + st::mediaviewSaveMsgPadding.bottom());
	_saveMsgText.setLink(1, std::make_shared<LambdaClickHandler>([this] { showSaveMsgFile(); }));

	connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(onScreenResized(int)));

	// While we have one mediaview for all authsessions we have to do this.
	auto handleAuthSessionChange = [this] {
		if (AuthSession::Exists()) {
			subscribe(Auth().downloaderTaskFinished(), [this] {
				if (!isHidden()) {
					updateControls();
				}
			});
			subscribe(Auth().calls().currentCallChanged(), [this](Calls::Call *call) {
				if (call && _clipController && !_videoPaused) {
					onVideoPauseResume();
				}
			});
			subscribe(Auth().documentUpdated, [this](DocumentData *document) {
				if (!isHidden()) {
					documentUpdated(document);
				}
			});
			subscribe(Auth().messageIdChanging, [this](std::pair<not_null<HistoryItem*>, MsgId> update) {
				changingMsgId(update.first, update.second);
			});
		} else {
			_sharedMedia = nullptr;
			_userPhotos = nullptr;
		}
	};
	subscribe(Messenger::Instance().authSessionChanged(), [handleAuthSessionChange] {
		handleAuthSessionChange();
	});
	handleAuthSessionChange();

	setWindowFlags(Qt::FramelessWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);

	hide();
	createWinId();
	if (cPlatform() != dbipMac && cPlatform() != dbipMacOld) {
		setWindowState(Qt::WindowFullScreen);
	}

	_saveMsgUpdater.setSingleShot(true);
	connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	_controlsHideTimer.setSingleShot(true);
	connect(&_controlsHideTimer, SIGNAL(timeout()), this, SLOT(onHideControls()));

	connect(_docDownload, SIGNAL(clicked()), this, SLOT(onDownload()));
	connect(_docSaveAs, SIGNAL(clicked()), this, SLOT(onSaveAs()));
	connect(_docCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));

	_dropdown->setHiddenCallback([this] { dropdownHidden(); });
	_dropdownShowTimer->setSingleShot(true);
	connect(_dropdownShowTimer, SIGNAL(timeout()), this, SLOT(onDropdown()));
}

void MediaView::refreshLang() {
	InvokeQueued(this, [this] { updateThemePreviewGeometry(); });
}

void MediaView::moveToScreen() {
	auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (auto handle = widget ? widget->windowHandle() : nullptr) {
			return handle->screen();
		}
		return nullptr;
	};
	auto activeWindow = Messenger::Instance().getActiveWindow();
	auto activeWindowScreen = widgetScreen(activeWindow);
	auto myScreen = widgetScreen(this);
	if (activeWindowScreen && myScreen && myScreen != activeWindowScreen) {
		windowHandle()->setScreen(activeWindowScreen);
	}
	const auto screen = activeWindowScreen ? activeWindowScreen : QApplication::primaryScreen();
	const auto available = screen->geometry();
	if (geometry() != available) {
		setGeometry(available);
	}

	auto navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
	_closeNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, st::mediaviewControlMargin, st::mediaviewControlSize, st::mediaviewControlSize);
	_closeNavIcon = centerrect(_closeNav, st::mediaviewClose);
	_leftNav = myrtlrect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_leftNavIcon = centerrect(_leftNav, st::mediaviewLeft);
	_rightNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_rightNavIcon = centerrect(_rightNav, st::mediaviewRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
}

bool MediaView::fileShown() const {
	return !_current.isNull() || gifShown();
}

bool MediaView::fileBubbleShown() const {
	return (!_photo && !_doc) || (_doc && !fileShown() && !_themePreviewShown);
}

bool MediaView::gifShown() const {
	if (_gif && _gif->ready()) {
		if (!_gif->started()) {
			const auto streamVideo = _doc
				&& (_doc->isVideoFile() || _doc->isVideoMessage());
			const auto pauseOnStart = (_autoplayVideoDocument != _doc);
			if (streamVideo && pauseOnStart && !_gif->videoPaused()) {
				const_cast<MediaView*>(this)->toggleVideoPaused();
			}
			const auto rounding = (_doc && _doc->isVideoMessage())
				? ImageRoundRadius::Ellipse
				: ImageRoundRadius::None;
			_gif->start(
				_gif->width() / cIntRetinaFactor(),
				_gif->height() / cIntRetinaFactor(),
				_gif->width() / cIntRetinaFactor(),
				_gif->height() / cIntRetinaFactor(),
				rounding,
				RectPart::AllCorners);
			const_cast<MediaView*>(this)->_current = QPixmap();
			updateMixerVideoVolume();
			Global::RefVideoVolumeChanged().notify();
		}
		return true;// _gif->state() != Media::Clip::State::Error;
	}
	return false;
}

void MediaView::stopGif() {
	_gif = nullptr;
	_videoPaused = _videoStopped = _videoIsSilent = false;
	_fullScreenVideo = false;
	_clipController.destroy();
	disconnect(Media::Player::mixer(), SIGNAL(updated(const AudioMsgId&)), this, SLOT(onVideoPlayProgress(const AudioMsgId&)));
}

void MediaView::documentUpdated(DocumentData *doc) {
	if (fileBubbleShown() && _doc && _doc == doc) {
		if ((_doc->loading() && _docCancel->isHidden()) || (!_doc->loading() && !_docCancel->isHidden())) {
			updateControls();
		} else if (_doc->loading()) {
			updateDocSize();
			update(_docRect);
		}
	}
}

void MediaView::changingMsgId(not_null<HistoryItem*> row, MsgId newId) {
	if (row->fullId() == _msgid) {
		_msgid = FullMsgId(_msgid.channel, newId);
		refreshMediaViewer();
	}
}

void MediaView::updateDocSize() {
	if (!_doc || !fileBubbleShown()) return;

	if (_doc->loading()) {
		quint64 ready = _doc->loadOffset(), total = _doc->size;
		QString readyStr, totalStr, mb;
		if (total >= 1024 * 1024) { // more than 1 mb
			qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
			readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
			totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
			mb = qsl("MB");
		} else if (total >= 1024) {
			qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
			readyStr = QString::number(readyKb);
			totalStr = QString::number(totalKb);
			mb = qsl("KB");
		} else {
			readyStr = QString::number(ready);
			totalStr = QString::number(total);
			mb = qsl("B");
		}
		_docSize = lng_media_save_progress(lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
	} else {
		_docSize = formatSizeText(_doc->size);
	}
	_docSizeWidth = st::mediaviewFont->width(_docSize);
	int32 maxw = st::mediaviewFileSize.width() - st::mediaviewFileIconSize - st::mediaviewFilePadding * 3;
	if (_docSizeWidth > maxw) {
		_docSize = st::mediaviewFont->elided(_docSize, maxw);
		_docSizeWidth = st::mediaviewFont->width(_docSize);
	}
}

void MediaView::refreshNavVisibility() {
	if (_sharedMediaData) {
		_leftNavVisible = _index && (*_index > 0);
		_rightNavVisible = _index && (*_index + 1 < _sharedMediaData->size());
	} else if (_userPhotosData) {
		_leftNavVisible = _index && (*_index > 0);
		_rightNavVisible = _index && (*_index + 1 < _userPhotosData->size());
	} else {
		_leftNavVisible = false;
		_rightNavVisible = false;
	}
}

void MediaView::updateControls() {
	if (_doc && fileBubbleShown()) {
		if (_doc->loading()) {
			_docDownload->hide();
			_docSaveAs->hide();
			_docCancel->moveToLeft(_docRect.x() + 2 * st::mediaviewFilePadding + st::mediaviewFileIconSize, _docRect.y() + st::mediaviewFilePadding + st::mediaviewFileLinksTop);
			_docCancel->show();
		} else {
			if (_doc->loaded(DocumentData::FilePathResolveChecked)) {
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

	_saveVisible = ((_photo && _photo->loaded()) || (_doc && (_doc->loaded(DocumentData::FilePathResolveChecked) || (!fileShown() && (_photo || _doc)))));
	_saveNav = myrtlrect(width() - st::mediaviewIconSize.width() * 2, height() - st::mediaviewIconSize.height(), st::mediaviewIconSize.width(), st::mediaviewIconSize.height());
	_saveNavIcon = centerrect(_saveNav, st::mediaviewSave);
	_moreNav = myrtlrect(width() - st::mediaviewIconSize.width(), height() - st::mediaviewIconSize.height(), st::mediaviewIconSize.width(), st::mediaviewIconSize.height());
	_moreNavIcon = centerrect(_moreNav, st::mediaviewMore);

	const auto dNow = QDateTime::currentDateTime();
	const auto d = [&] {
		if (_photo) {
			return ParseDateTime(_photo->date);
		} else if (_doc) {
			return ParseDateTime(_doc->date);
		} else if (const auto item = App::histItemById(_msgid)) {
			return ItemDateTime(item);
		}
		return dNow;
	}();
	if (d.date() == dNow.date()) {
		_dateText = lng_mediaview_today(lt_time, d.time().toString(cTimeFormat()));
	} else if (d.date().addDays(1) == dNow.date()) {
		_dateText = lng_mediaview_yesterday(lt_time, d.time().toString(cTimeFormat()));
	} else {
		_dateText = lng_mediaview_date_time(lt_date, d.date().toString(qsl("dd.MM.yy")), lt_time, d.time().toString(cTimeFormat()));
	}
	if (_from) {
		_fromName.setText(st::mediaviewTextStyle, (_from->migrateTo() ? _from->migrateTo() : _from)->name, Ui::NameTextOptions());
		_nameNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, qMin(_fromName.maxWidth(), width() / 3), st::mediaviewFont->height);
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

void MediaView::resizeCenteredControls() {
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

void MediaView::refreshCaptionGeometry() {
	if (_caption.isEmpty()) {
		_captionRect = QRect();
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto captionBottom = _clipController
		? (_clipController->y() - st::mediaviewCaptionMargin.height())
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

void MediaView::updateActions() {
	_actions.clear();

	if (_doc && _doc->loading()) {
		_actions.push_back({ lang(lng_cancel), SLOT(onSaveCancel()) });
	}
	if (IsServerMsgId(_msgid.msg)) {
		_actions.push_back({ lang(lng_context_to_msg), SLOT(onToMessage()) });
	}
	if (_doc && !_doc->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
		_actions.push_back({ lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), SLOT(onShowInFolder()) });
	}
	if ((_doc && fileShown()) || (_photo && _photo->loaded())) {
		_actions.push_back({ lang(lng_mediaview_copy), SLOT(onCopy()) });
	}
	if (_canForwardItem) {
		_actions.push_back({ lang(lng_mediaview_forward), SLOT(onForward()) });
	}
	auto canDelete = [&] {
		if (_canDeleteItem) {
			return true;
		} else if (!_msgid && _photo && App::self() && _user == App::self()) {
			return _userPhotosData && _fullIndex && _fullCount;
		} else if (_photo && _photo->peer && _photo->peer->userpicPhotoId() == _photo->id) {
			if (auto chat = _photo->peer->asChat()) {
				return chat->canEdit();
			} else if (auto channel = _photo->peer->asChannel()) {
				return channel->canEditInformation();
			}
		}
		return false;
	}();
	if (canDelete) {
		_actions.push_back({ lang(lng_mediaview_delete), SLOT(onDelete()) });
	}
	_actions.push_back({ lang(lng_mediaview_save_as), SLOT(onSaveAs()) });

	if (const auto overviewType = computeOverviewType()) {
		_actions.push_back({ lang(_doc ? lng_mediaview_files_all : lng_mediaview_photos_all), SLOT(onOverview()) });
	}
}

auto MediaView::computeOverviewType() const
-> base::optional<SharedMediaType> {
	if (const auto mediaType = sharedMediaType()) {
		if (const auto overviewType = SharedMediaOverviewType(*mediaType)) {
			return overviewType;
		} else if (mediaType == SharedMediaType::PhotoVideo) {
			if (_photo) {
				return SharedMediaOverviewType(SharedMediaType::Photo);
			} else if (_doc) {
				return SharedMediaOverviewType(SharedMediaType::Video);
			}
		}
	}
	return base::none;
}

void MediaView::step_state(TimeMs ms, bool timer) {
	bool result = false;
	for (Showing::iterator i = _animations.begin(); i != _animations.end();) {
		TimeMs start = i.value();
		switch (i.key()) {
		case OverLeftNav: update(_leftNav); break;
		case OverRightNav: update(_rightNav); break;
		case OverName: update(_nameNav); break;
		case OverDate: update(_dateNav); break;
		case OverHeader: update(_headerNav); break;
		case OverClose: update(_closeNav); break;
		case OverSave: update(_saveNav); break;
		case OverIcon: update(_docIconRect); break;
		case OverMore: update(_moreNav); break;
		default: break;
		}
		float64 dt = float64(ms - start) / st::mediaviewFadeDuration;
		if (dt >= 1) {
			_animOpacities.remove(i.key());
			i = _animations.erase(i);
		} else {
			_animOpacities[i.key()].update(dt, anim::linear);
			++i;
		}
	}
	if (_controlsState == ControlsShowing || _controlsState == ControlsHiding) {
		float64 dt = float64(ms - _controlsAnimStarted) / (_controlsState == ControlsShowing ? st::mediaviewShowDuration : st::mediaviewHideDuration);
		if (dt >= 1) {
			a_cOpacity.finish();
			_controlsState = (_controlsState == ControlsShowing ? ControlsShown : ControlsHidden);
			updateCursor();
		} else {
			a_cOpacity.update(dt, anim::linear);
		}
		const auto toUpdate = QRegion()
			+ (_over == OverLeftNav ? _leftNav : _leftNavIcon)
			+ (_over == OverRightNav ? _rightNav : _rightNavIcon)
			+ (_over == OverClose ? _closeNav : _closeNavIcon)
			+ _saveNavIcon
			+ _moreNavIcon
			+ _headerNav
			+ _nameNav
			+ _dateNav
			+ _captionRect.marginsAdded(st::mediaviewCaptionPadding)
			+ _groupThumbsRect;
		update(toUpdate);
		if (dt < 1) result = true;
	}
	if (!result && _animations.isEmpty()) {
		_a_state.stop();
	}
}

void MediaView::updateCursor() {
	setCursor(_controlsState == ControlsHidden
		? Qt::BlankCursor
		: (_over == OverNone ? style::cur_default : style::cur_pointer));
}

float64 MediaView::radialProgress() const {
	if (_doc) {
		return _doc->progress();
	} else if (_photo) {
		return _photo->full->progress();
	}
	return 1.;
}

bool MediaView::radialLoading() const {
	if (_doc) {
		return _doc->loading();
	} else if (_photo) {
		return _photo->full->loading();
	}
	return false;
}

QRect MediaView::radialRect() const {
	if (_doc) {
		return _docIconRect;
	} else if (_photo) {
		return _photoRadialRect;
	}
	return QRect();
}

void MediaView::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (auto shift = radialTimeShift()) {
			_radial.update(radialProgress(), !radialLoading(), getms() + shift);
		}
	}
}

TimeMs MediaView::radialTimeShift() const {
	return _photo ? st::radialDuration : 0;
}

void MediaView::step_radial(TimeMs ms, bool timer) {
	if (!_doc && !_photo) {
		_radial.stop();
		return;
	}
	const auto wasAnimating = _radial.animating();
	_radial.update(radialProgress(), !radialLoading(), ms + radialTimeShift());
	if (timer && (wasAnimating || _radial.animating())) {
		update(radialRect());
	}
	const auto ready = _doc && _doc->loaded();
	const auto streamVideo = ready && (_doc->isAnimation() || _doc->isVideoFile());
	const auto tryOpenImage = ready && (_doc->size < App::kImageSizeLimit);
	if (ready && ((tryOpenImage && !_radial.animating()) || streamVideo)) {
		if (_doc->isVideoFile() || _doc->isVideoMessage()) {
			_autoplayVideoDocument = _doc;
		}
		if (!_doc->data().isEmpty() && streamVideo) {
			displayDocument(_doc, App::histItemById(_msgid));
		} else {
			auto &location = _doc->location(true);
			if (location.accessEnable()) {
				if (streamVideo
					|| _doc->isTheme()
					|| QImageReader(location.name()).canRead()) {
					displayDocument(_doc, App::histItemById(_msgid));
				}
				location.accessDisable();
			}
		}
	}
}

void MediaView::zoomIn() {
	int32 newZoom = _zoom;
	if (newZoom == ZoomToScreenLevel) {
		if (qCeil(_zoomToScreen) <= MaxZoomLevel) {
			newZoom = qCeil(_zoomToScreen);
		}
	} else {
		if (newZoom < _zoomToScreen && (newZoom + 1 > _zoomToScreen || (_zoomToScreen > MaxZoomLevel && newZoom == MaxZoomLevel))) {
			newZoom = ZoomToScreenLevel;
		} else if (newZoom < MaxZoomLevel) {
			++newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void MediaView::zoomOut() {
	int32 newZoom = _zoom;
	if (newZoom == ZoomToScreenLevel) {
		if (qFloor(_zoomToScreen) >= -MaxZoomLevel) {
			newZoom = qFloor(_zoomToScreen);
		}
	} else {
		if (newZoom > _zoomToScreen && (newZoom - 1 < _zoomToScreen || (_zoomToScreen < -MaxZoomLevel && newZoom == -MaxZoomLevel))) {
			newZoom = ZoomToScreenLevel;
		} else if (newZoom > -MaxZoomLevel) {
			--newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void MediaView::zoomReset() {
	int32 newZoom = _zoom;
	if (_zoom == 0) {
		if (qFloor(_zoomToScreen) == qCeil(_zoomToScreen) && qRound(_zoomToScreen) >= -MaxZoomLevel && qRound(_zoomToScreen) <= MaxZoomLevel) {
			newZoom = qRound(_zoomToScreen);
		} else {
			newZoom = ZoomToScreenLevel;
		}
	} else {
		newZoom = 0;
	}
	_x = -_width / 2;
	_y = -((gifShown() ? _gif->height() : (_current.height() / cIntRetinaFactor())) / 2);
	float64 z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
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

void MediaView::zoomUpdate(int32 &newZoom) {
	if (newZoom != ZoomToScreenLevel) {
		while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h) {
			++newZoom;
		}
	}
	setZoomLevel(newZoom);
}

void MediaView::clearData() {
	if (!isHidden()) {
		hide();
	}
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	stopGif();
	delete _menu;
	_menu = nullptr;
	setContext(base::none);
	_from = nullptr;
	_photo = nullptr;
	_doc = nullptr;
	_fullScreenVideo = false;
	_caption.clear();
}

MediaView::~MediaView() {
	delete base::take(_menu);
}

void MediaView::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	setCursor((active || ClickHandler::getPressed()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void MediaView::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	setCursor((pressed || ClickHandler::getActive()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void MediaView::showSaveMsgFile() {
	File::ShowInFolder(_saveMsgFilename);
}

void MediaView::updateMixerVideoVolume() const {
	if (_doc && (_doc->isVideoFile() || _doc->isVideoMessage())) {
		Media::Player::mixer()->setVideoVolume(Global::VideoVolume());
	}
}

void MediaView::close() {
	Messenger::Instance().hideMediaView();
}

void MediaView::activateControls() {
	if (!_menu && !_mousePressed) {
		_controlsHideTimer.start(int(st::mediaviewWaitHide));
	}
	if (_fullScreenVideo) {
		if (_clipController) {
			_clipController->showAnimated();
		}
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) {
		_controlsState = ControlsShowing;
		_controlsAnimStarted = getms();
		a_cOpacity.start(1);
		if (!_a_state.animating()) _a_state.start();
	}
}

void MediaView::onHideControls(bool force) {
	if (!force) {
		if (!_dropdown->isHidden()
			|| _menu
			|| _mousePressed
			|| (_fullScreenVideo && _clipController && _clipController->geometry().contains(_lastMouseMovePos))) {
			return;
		}
	}
	if (_fullScreenVideo) {
		if (_clipController) {
			_clipController->hideAnimated();
		}
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;

	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	_controlsState = ControlsHiding;
	_controlsAnimStarted = getms();
	a_cOpacity.start(0);
	if (!_a_state.animating()) _a_state.start();
}

void MediaView::dropdownHidden() {
	setFocus();
	_ignoringDropdown = true;
	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	updateOver(_lastMouseMovePos);
	_ignoringDropdown = false;
	if (!_controlsHideTimer.isActive()) {
		onHideControls(true);
	}
}

void MediaView::onScreenResized(int screen) {
	if (isHidden()) return;

	bool ignore = false;
	auto screens = QApplication::screens();
	if (screen >= 0 && screen < screens.size()) {
		if (auto screenHandle = windowHandle()->screen()) {
			if (screens.at(screen) != screenHandle) {
				ignore = true;
			}
		}
	}
	if (!ignore) {
		moveToScreen();
		auto item = (_msgid ? App::histItemById(_msgid) : nullptr);
		if (_photo) {
			displayPhoto(_photo, item);
		} else if (_doc) {
			displayDocument(_doc, item);
		}
	}
}

void MediaView::onToMessage() {
	if (auto item = _msgid ? App::histItemById(_msgid) : 0) {
		close();
		Ui::showPeerHistoryAtItem(item);
	}
}

void MediaView::onSaveAs() {
	QString file;
	if (_doc) {
		const FileLocation &location(_doc->location(true));
		if (!_doc->data().isEmpty() || location.accessEnable()) {
			QFileInfo alreadyInfo(location.name());
			QDir alreadyDir(alreadyInfo.dir());
			QString name = alreadyInfo.fileName(), filter;
			const auto mimeType = Core::MimeTypeForName(_doc->mimeString());
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

			psBringToBack(this);
			file = FileNameForSave(lang(lng_save_file), filter, qsl("doc"), name, true, alreadyDir);
			psShowOverAll(this);
			if (!file.isEmpty() && file != location.name()) {
				if (_doc->data().isEmpty()) {
					QFile(file).remove();
					QFile(location.name()).copy(file);
				} else {
					QFile f(file);
					f.open(QIODevice::WriteOnly);
					f.write(_doc->data());
				}
			}

			if (_doc->data().isEmpty()) location.accessDisable();
		} else {
			if (!fileShown()) {
				DocumentSaveClickHandler::doSave(_doc, true);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		}
	} else {
		if (!_photo || !_photo->loaded()) return;

		psBringToBack(this);
		auto filter = qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter();
		FileDialog::GetWritePath(
			this,
			lang(lng_save_photo),
			filter,
			filedialogDefaultName(
				qsl("photo"),
				qsl(".jpg"),
				QString(),
				false,
				_photo->date),
			crl::guard(this, [this, photo = _photo](const QString &result) {
				if (!result.isEmpty() && _photo == photo && photo->loaded()) {
					photo->full->pix().toImage().save(result, "JPG");
				}
				psShowOverAll(this);
			}), crl::guard(this, [this] {
				psShowOverAll(this);
			}));
	}
	activateWindow();
	Sandbox::setActiveWindow(this);
	setFocus();
}

void MediaView::onDocClick() {
	if (_doc->loading()) {
		onSaveCancel();
	} else {
		DocumentOpenClickHandler::doOpen(_doc, nullptr, ActionOnLoadNone);
		if (_doc->loading() && !_radial.animating()) {
			_radial.start(_doc->progress());
		}
	}
}

void MediaView::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	if (!_gif) return;

	switch (notification) {
	case NotificationReinit: {
		if (auto item = App::histItemById(_msgid)) {
			if (_gif->state() == State::Error) {
				stopGif();
				updateControls();
				update();
				break;
			} else if (_gif->state() == State::Finished) {
				_videoPositionMs = _videoDurationMs;
				_videoStopped = true;
				updateSilentVideoPlaybackState();
			} else {
				_videoIsSilent = _doc && (_doc->isVideoFile() || _doc->isVideoMessage()) && !_gif->hasAudio();
				_videoDurationMs = _gif->getDurationMs();
				_videoPositionMs = _gif->getPositionMs();
				if (_videoIsSilent) {
					updateSilentVideoPlaybackState();
				}
			}
			displayDocument(_doc, item);
		} else {
			stopGif();
			updateControls();
			update();
		}
	} break;

	case NotificationRepaint: {
		if (!_gif->currentDisplayed()) {
			_videoPositionMs = _gif->getPositionMs();
			if (_videoIsSilent) {
				updateSilentVideoPlaybackState();
			}
			update(_x, _y, _w, _h);
		}
	} break;
	}
}

PeerData *MediaView::ui_getPeerForMouseAction() {
	return _history ? _history->peer.get() : nullptr;
}

void MediaView::onDownload() {
	if (Global::AskDownloadPath()) {
		return onSaveAs();
	}

	QString path;
	if (Global::DownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = Global::DownloadPath();
	}
	QString toName;
	if (_doc) {
		const FileLocation &location(_doc->location(true));
		if (location.accessEnable()) {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogNextFilename(
				_doc->filename(),
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
			if (!fileShown()) {
				DocumentSaveClickHandler::doSave(_doc);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		}
	} else {
		if (!_photo || !_photo->loaded()) {
			_saveVisible = false;
			update(_saveNav);
		} else {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogDefaultName(qsl("photo"), qsl(".jpg"), path);
			if (!_photo->full->pix().toImage().save(toName, "JPG")) {
				toName = QString();
			}
		}
	}
	if (!toName.isEmpty()) {
		_saveMsgFilename = toName;
		_saveMsgStarted = getms();
		_saveMsgOpacity.start(1);
		updateImage();
	}
}

void MediaView::onSaveCancel() {
	if (_doc && _doc->loading()) {
		_doc->cancel();
	}
}

void MediaView::onShowInFolder() {
	if (!_doc) return;

	auto filepath = _doc->filepath(DocumentData::FilePathResolveChecked);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void MediaView::onForward() {
	auto item = App::histItemById(_msgid);
	if (!item || !IsServerMsgId(item->id) || item->serviceMsg()) {
		return;
	}

	close();
	Window::ShowForwardMessagesBox({ 1, item->fullId() });
}

void MediaView::onDelete() {
	close();
	const auto deletingPeerPhoto = [this] {
		if (!_msgid) {
			return true;
		}
		if (_photo && _history) {
			if (_history->peer->userpicPhotoId() == _photo->id) {
				return _firstOpenedPeerPhoto;
			}
		}
		return false;
	};

	if (deletingPeerPhoto()) {
		App::main()->deletePhotoLayer(_photo);
	} else {
		App::main()->deleteLayer(_msgid);
	}
}

void MediaView::onOverview() {
	if (_menu) _menu->hideMenu(true);
	update();
	if (const auto overviewType = computeOverviewType()) {
		close();
		SharedMediaShowOverview(*overviewType, _history);
	}
}

void MediaView::onCopy() {
	_dropdown->hideAnimated(Ui::DropdownMenu::HideOption::IgnoreShow);
	if (_doc) {
		if (!_current.isNull()) {
			QApplication::clipboard()->setPixmap(_current);
		} else if (gifShown()) {
			QApplication::clipboard()->setPixmap(_gif->frameOriginal());
		}
	} else {
		if (!_photo || !_photo->loaded()) return;

		QApplication::clipboard()->setPixmap(_photo->full->pix());
	}
}

base::optional<MediaView::SharedMediaType> MediaView::sharedMediaType() const {
	using Type = SharedMediaType;
	if (auto item = App::histItemById(_msgid)) {
		if (_photo) {
			if (item->toHistoryMessage()) {
				return Type::PhotoVideo;
			}
			return Type::ChatPhoto;
		} else if (_doc) {
			if (_doc->isGifv()) {
				return Type::GIF;
			} else if (_doc->isVideoFile()) {
				return Type::PhotoVideo;
			}
			return Type::File;
		}
	}
	return base::none;
}

base::optional<MediaView::SharedMediaKey> MediaView::sharedMediaKey() const {
	if (!_msgid && _peer && !_user && _photo && _peer->userpicPhotoId() == _photo->id) {
		return SharedMediaKey {
			_history->peer->id,
			_migrated ? _migrated->peer->id : 0,
			SharedMediaType::ChatPhoto,
			_peer->userpicPhotoId()
		};
	}
	if (!IsServerMsgId(_msgid.msg)) {
		return base::none;
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

bool MediaView::validSharedMedia() const {
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
					: base::optional<int>();
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

void MediaView::validateSharedMedia() {
	if (auto key = sharedMediaKey()) {
		_sharedMedia = std::make_unique<SharedMedia>(*key);
		auto viewer = (key->type == SharedMediaType::ChatPhoto)
			? SharedMediaWithLastReversedViewer
			: SharedMediaWithLastViewer;
		viewer(
			*key,
			kIdsLimit,
			kIdsLimit
		) | rpl::start_with_next([this](
				SharedMediaWithLastSlice &&update) {
			handleSharedMediaUpdate(std::move(update));
		}, _sharedMedia->lifetime);
	} else {
		_sharedMedia = nullptr;
		_sharedMediaData = base::none;
		_sharedMediaDataKey = base::none;
	}
}

void MediaView::handleSharedMediaUpdate(SharedMediaWithLastSlice &&update) {
	if ((!_photo && !_doc) || !_sharedMedia) {
		_sharedMediaData = base::none;
		_sharedMediaDataKey = base::none;
	} else {
		_sharedMediaData = std::move(update);
		_sharedMediaDataKey = _sharedMedia->key;
	}
	findCurrent();
	updateControls();
	preloadData(0);
}

base::optional<MediaView::UserPhotosKey> MediaView::userPhotosKey() const {
	if (!_msgid && _user && _photo) {
		return UserPhotosKey {
			_user->bareId(),
			_photo->id
		};
	}
	return base::none;
}

bool MediaView::validUserPhotos() const {
	if (auto key = userPhotosKey()) {
		if (!_userPhotos) {
			return false;
		}
		auto countDistanceInData = [](const auto &a, const auto &b) {
			return [&](const UserPhotosSlice &data) {
				return data.distance(a, b);
			};
		};

		auto distance = (key == _userPhotos->key) ? 0 :
			_userPhotosData
			| countDistanceInData(*key, _userPhotos->key)
			| func::abs;
		if (distance) {
			return (*distance < kIdsPreloadAfter);
		}
	}
	return (_userPhotos == nullptr);
}

void MediaView::validateUserPhotos() {
	if (auto key = userPhotosKey()) {
		_userPhotos = std::make_unique<UserPhotos>(*key);
		UserPhotosReversedViewer(
			*key,
			kIdsLimit,
			kIdsLimit
		) | rpl::start_with_next([this](
				UserPhotosSlice &&update) {
			handleUserPhotosUpdate(std::move(update));
		}, _userPhotos->lifetime);
	} else {
		_userPhotos = nullptr;
		_userPhotosData = base::none;
	}
}

void MediaView::handleUserPhotosUpdate(UserPhotosSlice &&update) {
	if (!_photo || !_userPhotos) {
		_userPhotosData = base::none;
	} else {
		_userPhotosData = std::move(update);
	}
	findCurrent();
	updateControls();
	preloadData(0);
}

void MediaView::refreshMediaViewer() {
	if (!validSharedMedia()) {
		validateSharedMedia();
	}
	if (!validUserPhotos()) {
		validateUserPhotos();
	}
	findCurrent();
	updateControls();
	preloadData(0);
}

void MediaView::refreshCaption(HistoryItem *item) {
	_caption = Text();
	if (!item) {
		return;
	}
	const auto caption = item->originalText();
	if (caption.text.isEmpty()) {
		return;
	}
	const auto asBot = [&] {
		if (const auto author = item->author()->asUser()) {
			return author->botInfo != nullptr;
		}
		return false;
	}();
	_caption = Text(st::msgMinWidth);
	_caption.setMarkedText(
		st::mediaviewCaptionStyle,
		caption,
		Ui::ItemTextOptions(item));
}

void MediaView::refreshGroupThumbs() {
	const auto existed = (_groupThumbs != nullptr);
	if (_index && _sharedMediaData) {
		Media::View::GroupThumbs::Refresh(
			_groupThumbs,
			*_sharedMediaData,
			*_index,
			_groupThumbsAvailableWidth);
	} else if (_index && _userPhotosData) {
		Media::View::GroupThumbs::Refresh(
			_groupThumbs,
			*_userPhotosData,
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

void MediaView::initGroupThumbs() {
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
	) | rpl::start_with_next([this](Media::View::GroupThumbs::Key key) {
		if (const auto photoId = base::get_if<PhotoId>(&key)) {
			const auto photo = Auth().data().photo(*photoId);
			moveToEntity({ photo, nullptr });
		} else if (const auto itemId = base::get_if<FullMsgId>(&key)) {
			moveToEntity(entityForItemId(*itemId));
		}
	}, _groupThumbs->lifetime());

	_groupThumbsRect = QRect(
		_groupThumbsLeft,
		_groupThumbsTop,
		width() - 2 * _groupThumbsLeft,
		height() - _groupThumbsTop);
}

void MediaView::showPhoto(not_null<PhotoData*> photo, HistoryItem *context) {
	if (context) {
		setContext(context);
	} else {
		setContext(base::none);
	}

	_firstOpenedPeerPhoto = false;
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_photo = photo;

	refreshMediaViewer();

	displayPhoto(photo, context);
	preloadData(0);
	activateControls();
}

void MediaView::showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> context) {
	setContext(context);

	_firstOpenedPeerPhoto = true;
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_photo = photo;

	refreshMediaViewer();

	displayPhoto(photo, 0);
	preloadData(0);
	activateControls();
}

void MediaView::showDocument(not_null<DocumentData*> document, HistoryItem *context) {
	if (context) {
		setContext(context);
	} else {
		setContext(base::none);
	}

	_photo = nullptr;
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_down = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	if (document->isVideoFile() || document->isVideoMessage()) {
		_autoplayVideoDocument = document;
	}
	displayDocument(document, context);
	preloadData(0);
	activateControls();
}

void MediaView::displayPhoto(not_null<PhotoData*> photo, HistoryItem *item) {
	stopGif();
	destroyThemePreview();
	_doc = _autoplayVideoDocument = nullptr;
	_fullScreenVideo = false;
	_photo = photo;
	_radial.stop();

	refreshMediaViewer();

	_photoRadialRect = QRect(QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2), st::radialSize);

	_zoom = 0;

	refreshCaption(item);

	_zoomToScreen = 0;
	Auth().downloader().clearPriorities();
	_full = -1;
	_current = QPixmap();
	_down = OverNone;
	_w = convertScale(photo->full->width());
	_h = convertScale(photo->full->height());
	if (isHidden()) {
		moveToScreen();
	}
	if (_w > width()) {
		_h = qRound(_h * width() / float64(_w));
		_w = width();
	}
	if (_h > height()) {
		_w = qRound(_w * height() / float64(_h));
		_h = height();
	}
	_x = (width() - _w) / 2;
	_y = (height() - _h) / 2;
	_width = _w;
	if (_msgid && item) {
		_from = item->senderOriginal();
	} else {
		_from = _user;
	}
	_photo->download();
	displayFinished();
}

void MediaView::destroyThemePreview() {
	_themePreviewId = 0;
	_themePreviewShown = false;
	_themePreview.reset();
	_themeApply.destroy();
	_themeCancel.destroy();
}

void MediaView::displayDocument(DocumentData *doc, HistoryItem *item) { // empty messages shown as docs: doc can be NULL
	auto documentChanged = (!doc || doc != _doc || (item && item->fullId() != _msgid));
	if (documentChanged || (!doc->isAnimation() && !doc->isVideoFile())) {
		_fullScreenVideo = false;
		_current = QPixmap();
		stopGif();
	} else if (gifShown()) {
		_current = QPixmap();
	}
	if (documentChanged || !doc->isTheme()) {
		destroyThemePreview();
	}
	_doc = doc;
	_photo = nullptr;
	_radial.stop();

	refreshMediaViewer();

	if (_autoplayVideoDocument && _doc != _autoplayVideoDocument) {
		_autoplayVideoDocument = nullptr;
	}

	if (documentChanged) {
		refreshCaption(item);
	}
	if (_doc) {
		if (_doc->sticker()) {
			_doc->checkSticker();
			if (!_doc->sticker()->img->isNull()) {
				_current = _doc->sticker()->img->pix();
			} else {
				_current = _doc->thumb->pixBlurred(_doc->dimensions.width(), _doc->dimensions.height());
			}
		} else {
			_doc->automaticLoad(item);

			if (_doc->isAnimation() || _doc->isVideoFile()) {
				initAnimation();
			} else if (_doc->isTheme()) {
				initThemePreview();
			} else {
				auto &location = _doc->location(true);
				if (location.accessEnable()) {
					if (QImageReader(location.name()).canRead()) {
						_current = App::pixmapFromImageInPlace(App::readImage(location.name(), 0, false));
					}
				}
				location.accessDisable();
			}
		}
	}

	_docIconRect = QRect((width() - st::mediaviewFileIconSize) / 2, (height() - st::mediaviewFileIconSize) / 2, st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	if (fileBubbleShown()) {
		if (!_doc || _doc->thumb->isNull()) {
			int32 colorIndex = documentColorIndex(_doc, _docExt);
			_docIconColor = documentColor(colorIndex);
			const style::icon *(thumbs[]) = { &st::mediaviewFileBlue, &st::mediaviewFileGreen, &st::mediaviewFileRed, &st::mediaviewFileYellow };
			_docIcon = thumbs[colorIndex];

			int32 extmaxw = (st::mediaviewFileIconSize - st::mediaviewFileExtPadding * 2);
			_docExtWidth = st::mediaviewFileExtFont->width(_docExt);
			if (_docExtWidth > extmaxw) {
				_docExt = st::mediaviewFileNameFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
				_docExtWidth = st::mediaviewFileNameFont->width(_docExt);
			}
		} else {
			_doc->thumb->load();
			int32 tw = _doc->thumb->width(), th = _doc->thumb->height();
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

		if (_doc) {
			_docName = (_doc->type == StickerDocument)
				? lang(lng_in_dlg_sticker)
				: (_doc->type == AnimatedDocument
					? qsl("GIF")
					: (_doc->filename().isEmpty()
						? lang(lng_mediaview_doc_image)
						: _doc->filename()));
		} else {
			_docName = lang(lng_message_empty);
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
	} else if (!_current.isNull()) {
		_current.setDevicePixelRatio(cRetinaFactor());
		_w = convertScale(_current.width());
		_h = convertScale(_current.height());
	} else {
		_w = convertScale(_gif->width());
		_h = convertScale(_gif->height());
	}
	if (isHidden()) {
		moveToScreen();
	}
	_width = _w;
	if (_w > 0 && _h > 0) {
		_zoomToScreen = float64(width()) / _w;
		if (_h * _zoomToScreen > height()) {
			_zoomToScreen = float64(height()) / _h;
		}
		if (_zoomToScreen >= 1.) {
			_zoomToScreen -= 1.;
		} else {
			_zoomToScreen = 1. - (1. / _zoomToScreen);
		}
	} else {
		_zoomToScreen = 0;
	}
	if ((_w > width()) || (_h > height()) || _fullScreenVideo) {
		_zoom = ZoomToScreenLevel;
		if (_zoomToScreen >= 0) {
			_w = qRound(_w * (_zoomToScreen + 1));
			_h = qRound(_h * (_zoomToScreen + 1));
		} else {
			_w = qRound(_w / (-_zoomToScreen + 1));
			_h = qRound(_h / (-_zoomToScreen + 1));
		}
		snapXY();
	} else {
		_zoom = 0;
	}
	_x = (width() - _w) / 2;
	_y = (height() - _h) / 2;
	if (_msgid && item) {
		_from = item->senderOriginal();
	} else {
		_from = _user;
	}
	_full = 1;
	displayFinished();
}

void MediaView::updateThemePreviewGeometry() {
	if (_themePreviewShown) {
		auto previewRect = QRect((width() - st::themePreviewSize.width()) / 2, (height() - st::themePreviewSize.height()) / 2, st::themePreviewSize.width(), st::themePreviewSize.height());
		_themePreviewRect = previewRect.marginsAdded(st::themePreviewMargin);
		if (_themeApply) {
			auto right = qMax(width() - _themePreviewRect.x() - _themePreviewRect.width(), 0) + st::themePreviewMargin.right();
			auto bottom = qMin(height(), _themePreviewRect.y() + _themePreviewRect.height());
			_themeApply->moveToRight(right, bottom - st::themePreviewMargin.bottom() + (st::themePreviewMargin.bottom() - _themeApply->height()) / 2);
			right += _themeApply->width() + st::themePreviewButtonsSkip;
			_themeCancel->moveToRight(right, _themeApply->y());
		}

		// For context menu event.
		_x = _themePreviewRect.x();
		_y = _themePreviewRect.y();
		_w = _themePreviewRect.width();
		_h = _themePreviewRect.height();
	}
}

void MediaView::displayFinished() {
	updateControls();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
		psShowOverAll(this);
		activateWindow();
		Sandbox::setActiveWindow(this);
		setFocus();
	}
}

Images::Options MediaView::videoThumbOptions() const {
	auto options = Images::Option::Smooth | Images::Option::Blurred;
	if (_doc && _doc->isVideoMessage()) {
		options |= Images::Option::Circled;
	}
	return options;
}

void MediaView::initAnimation() {
	Expects(_doc != nullptr);
	Expects(_doc->isAnimation() || _doc->isVideoFile());

	auto &location = _doc->location(true);
	if (!_doc->data().isEmpty()) {
		createClipReader();
	} else if (location.accessEnable()) {
		createClipReader();
		location.accessDisable();
	} else if (_doc->dimensions.width() && _doc->dimensions.height()) {
		auto w = _doc->dimensions.width();
		auto h = _doc->dimensions.height();
		_current = _doc->thumb->pixNoCache(w, h, videoThumbOptions(), w / cIntRetinaFactor(), h / cIntRetinaFactor());
		if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
	} else {
		_current = _doc->thumb->pixNoCache(_doc->thumb->width(), _doc->thumb->height(), videoThumbOptions(), st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	}
}

void MediaView::createClipReader() {
	if (_gif) return;

	Expects(_doc != nullptr);
	Expects(_doc->isAnimation() || _doc->isVideoFile());

	if (_doc->dimensions.width() && _doc->dimensions.height()) {
		int w = _doc->dimensions.width();
		int h = _doc->dimensions.height();
		_current = _doc->thumb->pixNoCache(w, h, videoThumbOptions(), w / cIntRetinaFactor(), h / cIntRetinaFactor());
		if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
	} else {
		_current = _doc->thumb->pixNoCache(_doc->thumb->width(), _doc->thumb->height(), videoThumbOptions(), st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	}
	auto mode = (_doc->isVideoFile() || _doc->isVideoMessage())
		? Media::Clip::Reader::Mode::Video
		: Media::Clip::Reader::Mode::Gif;
	_gif = Media::Clip::MakeReader(_doc, _msgid, [this](Media::Clip::Notification notification) {
		clipCallback(notification);
	}, mode);

	// Correct values will be set when gif gets inited.
	_videoPaused = _videoIsSilent = _videoStopped = false;
	_videoPositionMs = 0ULL;
	_videoDurationMs = _doc->duration() * 1000ULL;

	createClipController();
}

void MediaView::initThemePreview() {
	Assert(_doc && _doc->isTheme());

	auto &location = _doc->location();
	if (!location.isEmpty() && location.accessEnable()) {
		_themePreviewShown = true;

		Window::Theme::CurrentData current;
		current.backgroundId = Window::Theme::Background()->id();
		current.backgroundImage = Window::Theme::Background()->pixmap().toImage();
		current.backgroundTiled = Window::Theme::Background()->tile();

		const auto path = _doc->location().name();
		const auto id = _themePreviewId = rand_value<uint64>();
		const auto weak = make_weak(this);
		crl::async([=, data = std::move(current)]() mutable {
			auto preview = Window::Theme::GeneratePreview(
				path,
				std::move(data));
			crl::on_main(weak, [=, result = std::move(preview)]() mutable {
				if (id != _themePreviewId) {
					return;
				}
				_themePreviewId = 0;
				_themePreview = std::move(result);
				if (_themePreview) {
					_themeApply.create(
						this,
						langFactory(lng_theme_preview_apply),
						st::themePreviewApplyButton);
					_themeApply->show();
					_themeApply->setClickedCallback([this] {
						auto preview = std::move(_themePreview);
						close();
						Window::Theme::Apply(std::move(preview));
					});
					_themeCancel.create(
						this,
						langFactory(lng_cancel),
						st::themePreviewCancelButton);
					_themeCancel->show();
					_themeCancel->setClickedCallback([this] { close(); });
					updateControls();
				}
				update();
			});
		});
		location.accessDisable();
	}
}

void MediaView::createClipController() {
	Expects(_doc != nullptr);
	if (!_doc->isVideoFile() && !_doc->isVideoMessage()) return;

	_clipController.create(this);
	refreshClipControllerGeometry();
	_clipController->show();

	connect(_clipController, SIGNAL(playPressed()), this, SLOT(onVideoPauseResume()));
	connect(_clipController, SIGNAL(pausePressed()), this, SLOT(onVideoPauseResume()));
	connect(_clipController, SIGNAL(seekProgress(TimeMs)), this, SLOT(onVideoSeekProgress(TimeMs)));
	connect(_clipController, SIGNAL(seekFinished(TimeMs)), this, SLOT(onVideoSeekFinished(TimeMs)));
	connect(_clipController, SIGNAL(volumeChanged(float64)), this, SLOT(onVideoVolumeChanged(float64)));
	connect(_clipController, SIGNAL(toFullScreenPressed()), this, SLOT(onVideoToggleFullScreen()));
	connect(_clipController, SIGNAL(fromFullScreenPressed()), this, SLOT(onVideoToggleFullScreen()));

	connect(Media::Player::mixer(), SIGNAL(updated(const AudioMsgId&)), this, SLOT(onVideoPlayProgress(const AudioMsgId&)));
}

void MediaView::refreshClipControllerGeometry() {
	if (!_clipController) {
		return;
	}

	if (_groupThumbs && _groupThumbs->hiding()) {
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	const auto controllerBottom = _groupThumbs
		? _groupThumbsTop
		: height();
	_clipController->setGeometry(
		(width() - _clipController->width()) / 2,
		controllerBottom - _clipController->height() - st::mediaviewCaptionPadding.bottom() - st::mediaviewCaptionMargin.height(),
		st::mediaviewControllerSize.width(),
		st::mediaviewControllerSize.height());
	Ui::SendPendingMoveResizeEvents(_clipController);
}

void MediaView::onVideoPauseResume() {
	if (!_gif) return;

	if (auto item = App::histItemById(_msgid)) {
		if (_gif->state() == Media::Clip::State::Error) {
			displayDocument(_doc, item);
		} else if (_gif->state() == Media::Clip::State::Finished) {
			restartVideoAtSeekPosition(0);
		} else {
			toggleVideoPaused();
		}
	} else {
		stopGif();
		updateControls();
		update();
	}
}

void MediaView::toggleVideoPaused() {
	_gif->pauseResumeVideo();
	_videoPaused = _gif->videoPaused();
	if (_videoIsSilent) {
		updateSilentVideoPlaybackState();
	}
}

void MediaView::restartVideoAtSeekPosition(TimeMs positionMs) {
	_autoplayVideoDocument = _doc;

	if (_current.isNull()) {
		auto rounding = (_doc && _doc->isVideoMessage()) ? ImageRoundRadius::Ellipse : ImageRoundRadius::None;
		_current = _gif->current(_gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), _gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), rounding, RectPart::AllCorners, getms());
	}
	_gif = Media::Clip::MakeReader(_doc, _msgid, [this](Media::Clip::Notification notification) {
		clipCallback(notification);
	}, Media::Clip::Reader::Mode::Video, positionMs);

	// Correct values will be set when gif gets inited.
	_videoPaused = _videoIsSilent = _videoStopped = false;
	_videoPositionMs = positionMs;

	Media::Player::TrackState state;
	state.state = Media::Player::State::Playing;
	state.position = _videoPositionMs;
	state.length = _videoDurationMs;
	state.frequency = _videoFrequencyMs;
	updateVideoPlaybackState(state);
}

void MediaView::onVideoSeekProgress(TimeMs positionMs) {
	if (!_videoPaused && !_videoStopped) {
		onVideoPauseResume();
	}
}

void MediaView::onVideoSeekFinished(TimeMs positionMs) {
	restartVideoAtSeekPosition(positionMs);
}

void MediaView::onVideoVolumeChanged(float64 volume) {
	Global::SetVideoVolume(volume);
	updateMixerVideoVolume();
	Global::RefVideoVolumeChanged().notify();
}

void MediaView::onVideoToggleFullScreen() {
	if (!_clipController) return;

	_fullScreenVideo = !_fullScreenVideo;
	if (_fullScreenVideo) {
		_fullScreenZoomCache = _zoom;
		setZoomLevel(ZoomToScreenLevel);
	} else {
		setZoomLevel(_fullScreenZoomCache);
		_clipController->showAnimated();
	}

	_clipController->setInFullScreen(_fullScreenVideo);
	updateControls();
	update();
}

void MediaView::onVideoPlayProgress(const AudioMsgId &audioId) {
	if (!_gif || _gif->audioMsgId() != audioId) {
		return;
	}

	auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Video);
	if (state.id == _gif->audioMsgId()) {
		if (state.length) {
			updateVideoPlaybackState(state);
		}
		Auth().settings().setLastTimeVideoPlayedAt(getms(true));
	}
}

void MediaView::updateVideoPlaybackState(const Media::Player::TrackState &state) {
	if (state.frequency) {
		if (Media::Player::IsStopped(state.state)) {
			_videoStopped = true;
		}
		_clipController->updatePlayback(state);
	} else { // Audio has stopped already.
		_videoIsSilent = true;
		updateSilentVideoPlaybackState();
	}
}

void MediaView::updateSilentVideoPlaybackState() {
	Media::Player::TrackState state;
	if (_videoPaused) {
		state.state = Media::Player::State::Paused;
	} else if (_videoPositionMs == _videoDurationMs) {
		state.state = Media::Player::State::StoppedAtEnd;
	} else {
		state.state = Media::Player::State::Playing;
	}
	state.position = _videoPositionMs;
	state.length = _videoDurationMs;
	state.frequency = _videoFrequencyMs;
	updateVideoPlaybackState(state);
}

void MediaView::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QRegion region(e->region());
	QVector<QRect> rs(region.rects());

	auto ms = getms();

	Painter p(this);

	bool name = false;

	p.setClipRegion(region);

	// main bg
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	if (_fullScreenVideo) {
		for (int i = 0, l = region.rectCount(); i < l; ++i) {
			p.fillRect(rs.at(i), st::mediaviewVideoBg);
		}
		if (_doc && _doc->isVideoMessage()) {
			p.setCompositionMode(m);
		}
	} else {
		for (int i = 0, l = region.rectCount(); i < l; ++i) {
			p.fillRect(rs.at(i), st::mediaviewBg);
		}
		p.setCompositionMode(m);
	}

	// photo
	if (_photo) {
		int32 w = _width * cIntRetinaFactor();
		if (_full <= 0 && _photo->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->full->pixNoCache(w, h, Images::Option::Smooth);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 1;
		} else if (_full < 0 && _photo->medium->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->medium->pixNoCache(w, h, Images::Option::Smooth | Images::Option::Blurred);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 0;
		} else if (_current.isNull() && _photo->thumb->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->thumb->pixNoCache(w, h, Images::Option::Smooth | Images::Option::Blurred);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
		} else if (_current.isNull()) {
			_current = _photo->thumb->pix();
		}
	}
	p.setOpacity(1);
	if (_photo || fileShown()) {
		QRect imgRect(_x, _y, _w, _h);
		if (imgRect.intersects(r)) {
			auto rounding = (_doc && _doc->isVideoMessage()) ? ImageRoundRadius::Ellipse : ImageRoundRadius::None;
			auto toDraw = _current.isNull() ? _gif->current(_gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), _gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), rounding, RectPart::AllCorners, ms) : _current;
			if (!_gif && (!_doc || !_doc->sticker() || _doc->sticker()->img->isNull()) && toDraw.hasAlpha()) {
				p.fillRect(imgRect, _transparentBrush);
			}
			if (toDraw.width() != _w * cIntRetinaFactor()) {
				PainterHighQualityEnabler hq(p);
				p.drawPixmap(QRect(_x, _y, _w, _h), toDraw);
			} else {
				p.drawPixmap(_x, _y, toDraw);
			}

			bool radial = false;
			float64 radialOpacity = 0;
			if (_radial.animating()) {
				_radial.step(ms);
				radial = _radial.animating();
				radialOpacity = _radial.opacity();
			}
			if (_photo) {
				if (radial) {
					auto inner = radialRect();

					p.setPen(Qt::NoPen);
					p.setOpacity(radialOpacity);
					p.setBrush(st::radialBg);

					{
						PainterHighQualityEnabler hq(p);
						p.drawEllipse(inner);
					}

					p.setOpacity(1);
					QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
					_radial.draw(p, arc, st::radialLine, st::radialFg);
				}
			} else if (_doc) {
				paintDocRadialLoading(p, radial, radialOpacity);
			}

			if (_saveMsgStarted) {
				auto ms = getms();
				float64 dt = float64(ms) - _saveMsgStarted, hidingDt = dt - st::mediaviewSaveMsgShowing - st::mediaviewSaveMsgShown;
				if (dt < st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + st::mediaviewSaveMsgHiding) {
					if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5) {
						_saveMsgOpacity.start(0);
					}
					float64 progress = (hidingDt >= 0) ? (hidingDt / st::mediaviewSaveMsgHiding) : (dt / st::mediaviewSaveMsgShowing);
					_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
                    if (_saveMsgOpacity.current() > 0) {
						p.setOpacity(_saveMsgOpacity.current());
						App::roundRect(p, _saveMsg, st::mediaviewSaveMsgBg, MediaviewSaveCorners);
						st::mediaviewSaveMsgCheck.paint(p, _saveMsg.topLeft() + st::mediaviewSaveMsgCheckPos, width());

						p.setPen(st::mediaviewSaveMsgFg);
						p.setTextPalette(st::mediaviewTextPalette);
						_saveMsgText.draw(p, _saveMsg.x() + st::mediaviewSaveMsgPadding.left(), _saveMsg.y() + st::mediaviewSaveMsgPadding.top(), _saveMsg.width() - st::mediaviewSaveMsgPadding.left() - st::mediaviewSaveMsgPadding.right());
						p.restoreTextPalette();
						p.setOpacity(1);
					}
					if (_full >= 1) {
                        auto nextFrame = (dt < st::mediaviewSaveMsgShowing || hidingDt >= 0) ? int(AnimationTimerDelta) : (st::mediaviewSaveMsgShowing + st::mediaviewSaveMsgShown + 1 - dt);
						_saveMsgUpdater.start(nextFrame);
					}
				} else {
					_saveMsgStarted = 0;
				}
			}
		}
	} else if (_themePreviewShown) {
		paintThemePreview(p, r);
	} else {
		if (_docRect.intersects(r)) {
			p.fillRect(_docRect, st::mediaviewFileBg);
			if (_docIconRect.intersects(r)) {
				bool radial = false;
				float64 radialOpacity = 0;
				if (_radial.animating()) {
					_radial.step(ms);
					radial = _radial.animating();
					radialOpacity = _radial.opacity();
				}
				if (!_doc || _doc->thumb->isNull()) {
					p.fillRect(_docIconRect, _docIconColor);
					if ((!_doc || _doc->loaded()) && (!radial || radialOpacity < 1) && _docIcon) {
						_docIcon->paint(p, _docIconRect.x() + (_docIconRect.width() - _docIcon->width()), _docIconRect.y(), width());
						p.setPen(st::mediaviewFileExtFg);
						p.setFont(st::mediaviewFileExtFont);
						if (!_docExt.isEmpty()) {
							p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2, _docIconRect.y() + st::mediaviewFileExtTop + st::mediaviewFileExtFont->ascent, _docExt);
						}
					}
				} else {
					int32 rf(cIntRetinaFactor());
					p.drawPixmap(_docIconRect.topLeft(), _doc->thumb->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mediaviewFileIconSize * rf, st::mediaviewFileIconSize * rf));
				}

				paintDocRadialLoading(p, radial, radialOpacity);
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

	float64 co = _fullScreenVideo ? 0. : a_cOpacity.current();
	if (co > 0) {
		// left nav bar
		if (_leftNav.intersects(r) && _leftNavVisible) {
			auto o = overLevel(OverLeftNav);
			if (o > 0) {
				p.setOpacity(o * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _leftNav.intersected(rs.at(i));
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
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _rightNav.intersected(rs.at(i));
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
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _closeNav.intersected(rs.at(i));
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
		if (_from && _nameNav.intersects(r)) {
			float64 o = overLevel(OverName);
			p.setOpacity((o * st::mediaviewIconOverOpacity + (1 - o) * st::mediaviewIconOpacity) * co);
			_fromName.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());

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
				width(),
				ms);
			if (_groupThumbs->hidden()) {
				_groupThumbs = nullptr;
				_groupThumbsRect = QRect();
			}
		}
	}
	checkGroupThumbsAnimation();
}

void MediaView::checkGroupThumbsAnimation() {
	if (_groupThumbs && (!_gif || _gif->started())) {
		_groupThumbs->checkForAnimationStart();
	}
}

void MediaView::paintDocRadialLoading(Painter &p, bool radial, float64 radialOpacity) {
	float64 o = overLevel(OverIcon);
	if (radial || (_doc && !_doc->loaded())) {
		QRect inner(QPoint(_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2), _docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)), st::radialSize);

		p.setPen(Qt::NoPen);
		p.setOpacity(_doc->loaded() ? radialOpacity : 1.);
		p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, o));

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(1.);
		auto icon = ([radial, this]() -> const style::icon* {
			if (radial || _doc->loading()) {
				return &st::historyFileThumbCancel;
			}
			return &st::historyFileThumbDownload;
		})();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			p.setOpacity(1);
			QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
			_radial.draw(p, arc, st::radialLine, st::radialFg);
		}
	}
}

void MediaView::paintThemePreview(Painter &p, QRect clip) {
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
				lang(_themePreviewId
					? lng_theme_preview_generating
					: lng_theme_preview_invalid),
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
		p.drawTextLeft(titleRect.x(), titleRect.y(), width(), lang(lng_theme_preview_title));
	}

	auto buttonsRect = QRect(_themePreviewRect.x(), _themePreviewRect.y() + _themePreviewRect.height() - st::themePreviewMargin.bottom(), _themePreviewRect.width(), st::themePreviewMargin.bottom());
	if (auto fillButtonsRect = (buttonsRect.y() + buttonsRect.height() > height())) {
		buttonsRect.moveTop(height() - buttonsRect.height());
		fillOverlay(buttonsRect);
	}
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (_clipController) {
		auto toggle1 = (e->key() == Qt::Key_F && e->modifiers().testFlag(Qt::ControlModifier));
		auto toggle2 = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) && (e->modifiers().testFlag(Qt::AltModifier) || e->modifiers().testFlag(Qt::ControlModifier));
		if (toggle1 || toggle2) {
			onVideoToggleFullScreen();
			return;
		}
		if (_fullScreenVideo) {
			if (e->key() == Qt::Key_Escape) {
				onVideoToggleFullScreen();
			} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
				onVideoPauseResume();
			}
			return;
		}
	}
	if (!_menu && e->key() == Qt::Key_Escape) {
		if (_doc && _doc->loading()) {
			onDocClick();
		} else {
			close();
		}
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSaveAs();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		onCopy();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		if (_doc && !_doc->loading() && (fileBubbleShown() || !_doc->loaded())) {
			onDocClick();
		} else if (_doc && (_doc->isVideoFile() || _doc->isVideoMessage())) {
			onVideoPauseResume();
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
	} else if (e->modifiers().testFlag(Qt::ControlModifier) && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == ']' || e->key() == Qt::Key_Asterisk || e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore || e->key() == Qt::Key_0)) {
		if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Asterisk || e->key() == ']') {
			zoomIn();
		} else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore) {
			zoomOut();
		} else {
			zoomReset();
		}
	}
}

void MediaView::wheelEvent(QWheelEvent *e) {
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

void MediaView::setZoomLevel(int newZoom) {
	if (_zoom == newZoom) return;

	float64 nx, ny, z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
	_w = gifShown() ? convertScale(_gif->width()) : (convertScale(_current.width()) / cIntRetinaFactor());
	_h = gifShown() ? convertScale(_gif->height()) : (convertScale(_current.height()) / cIntRetinaFactor());
	if (z >= 0) {
		nx = (_x - width() / 2.) / (z + 1);
		ny = (_y - height() / 2.) / (z + 1);
	} else {
		nx = (_x - width() / 2.) * (-z + 1);
		ny = (_y - height() / 2.) * (-z + 1);
	}
	_zoom = newZoom;
	z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
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

MediaView::Entity MediaView::entityForUserPhotos(int index) const {
	Expects(!!_userPhotosData);

	if (index < 0 || index >= _userPhotosData->size()) {
		return { base::none, nullptr };
	}
	if (auto photo = Auth().data().photo((*_userPhotosData)[index])) {
		return { photo, nullptr };
	}
	return { base::none, nullptr };
}

MediaView::Entity MediaView::entityForSharedMedia(int index) const {
	Expects(!!_sharedMediaData);

	if (index < 0 || index >= _sharedMediaData->size()) {
		return { base::none, nullptr };
	}
	auto value = (*_sharedMediaData)[index];
	if (const auto photo = base::get_if<not_null<PhotoData*>>(&value)) {
		// Last peer photo.
		return { *photo, nullptr };
	} else if (const auto itemId = base::get_if<FullMsgId>(&value)) {
		return entityForItemId(*itemId);
	}
	return { base::none, nullptr };
}

MediaView::Entity MediaView::entityForItemId(const FullMsgId &itemId) const {
	if (const auto item = App::histItemById(itemId)) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				return { photo, item };
			} else if (const auto document = media->document()) {
				return { document, item };
			}
		}
		return { base::none, item };
	}
	return { base::none, nullptr };
}

MediaView::Entity MediaView::entityByIndex(int index) const {
	if (_sharedMediaData) {
		return entityForSharedMedia(index);
	} else if (_userPhotosData) {
		return entityForUserPhotos(index);
	}
	return { base::none, nullptr };
}

void MediaView::setContext(base::optional_variant<
		not_null<HistoryItem*>,
		not_null<PeerData*>> context) {
	if (auto item = base::get_if<not_null<HistoryItem*>>(&context)) {
		_msgid = (*item)->fullId();
		_canForwardItem = (*item)->allowsForward();
		_canDeleteItem = (*item)->canDelete();
		_history = (*item)->history();
		_peer = _history->peer;
	} else if (auto peer = base::get_if<not_null<PeerData*>>(&context)) {
		_msgid = FullMsgId();
		_canForwardItem = _canDeleteItem = false;
		_history = App::history(*peer);
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
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	}
	_user = _peer ? _peer->asUser() : nullptr;
}

bool MediaView::moveToNext(int delta) {
	if (!_index) {
		return false;
	}
	auto newIndex = *_index + delta;
	return moveToEntity(entityByIndex(newIndex));
}

bool MediaView::moveToEntity(const Entity &entity, int preloadDelta) {
	if (!entity.data && !entity.item) {
		return false;
	}
	if (const auto item = entity.item) {
		setContext(item);
	} else if (_peer) {
		setContext(_peer);
	} else {
		setContext(base::none);
	}
	stopGif();
	if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data)) {
		displayPhoto(*photo, entity.item);
	} else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data)) {
		displayDocument(*document, entity.item);
	} else {
		displayDocument(nullptr, entity.item);
	}
	preloadData(preloadDelta);
	return true;
}

void MediaView::preloadData(int delta) {
	if (!_index) {
		return;
	}
	auto from = *_index + (delta ? delta : -1);
	auto till = *_index + (delta ? delta * kPreloadCount : 1);
	if (from > till) std::swap(from, till);

	if (delta != 0) {
		auto forgetIndex = *_index - delta * 2;
		auto entity = entityByIndex(forgetIndex);
		if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data)) {
			(*photo)->forget();
		} else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data)) {
			(*document)->forget();
		}
	}

	for (auto index = from; index != till; ++index) {
		auto entity = entityByIndex(index);
		if (auto photo = base::get_if<not_null<PhotoData*>>(&entity.data)) {
			(*photo)->download();
		} else if (auto document = base::get_if<not_null<DocumentData*>>(&entity.data)) {
			if (auto sticker = (*document)->sticker()) {
				sticker->img->load();
			} else {
				(*document)->thumb->load();
				(*document)->automaticLoad(entity.item);
			}
		}
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
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
			} else if (_over == OverName) {
				_down = OverName;
			} else if (_over == OverDate) {
				_down = OverDate;
			} else if (_over == OverHeader) {
				_down = OverHeader;
			} else if (_over == OverSave) {
				_down = OverSave;
			} else if (_over == OverIcon) {
				_down = OverIcon;
			} else if (_over == OverMore) {
				_down = OverMore;
			} else if (_over == OverClose) {
				_down = OverClose;
			} else if (_over == OverVideo) {
				_down = OverVideo;
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

void MediaView::mouseDoubleClickEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (_over == OverVideo) {
		onVideoToggleFullScreen();
		onVideoPauseResume();
	} else {
		e->ignore();
		return TWidget::mouseDoubleClickEvent(e);
	}
}

void MediaView::snapXY() {
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

void MediaView::mouseMoveEvent(QMouseEvent *e) {
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

void MediaView::updateOverRect(OverState state) {
	switch (state) {
	case OverLeftNav: update(_leftNav); break;
	case OverRightNav: update(_rightNav); break;
	case OverName: update(_nameNav); break;
	case OverDate: update(_dateNav); break;
	case OverSave: update(_saveNavIcon); break;
	case OverIcon: update(_docIconRect); break;
	case OverHeader: update(_headerNav); break;
	case OverClose: update(_closeNav); break;
	case OverMore: update(_moreNavIcon); break;
	}
}

bool MediaView::updateOverState(OverState newState) {
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
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(0);
			} else {
				_animOpacities.insert(_over, anim::value(1, 0));
			}
			if (!_a_state.animating()) _a_state.start();
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(1);
			} else {
				_animOpacities.insert(_over, anim::value(0, 1));
			}
			if (!_a_state.animating()) _a_state.start();
		}
		updateCursor();
	}
	return result;
}

void MediaView::updateOver(QPoint pos) {
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
	} else if (_nameNav.contains(pos)) {
		updateOverState(OverName);
	} else if (IsServerMsgId(_msgid.msg) && _dateNav.contains(pos)) {
		updateOverState(OverDate);
	} else if (_headerHasLink && _headerNav.contains(pos)) {
		updateOverState(OverHeader);
	} else if (_saveVisible && _saveNav.contains(pos)) {
		updateOverState(OverSave);
	} else if (_doc && fileBubbleShown() && _docIconRect.contains(pos)) {
		updateOverState(OverIcon);
	} else if (_moreNav.contains(pos)) {
		updateOverState(OverMore);
	} else if (_closeNav.contains(pos)) {
		updateOverState(OverClose);
	} else if (_doc && fileShown() && QRect(_x, _y, _w, _h).contains(pos)) {
		if ((_doc->isVideoFile() || _doc->isVideoMessage()) && _gif) {
			updateOverState(OverVideo);
		} else if (!_doc->loaded()) {
			updateOverState(OverIcon);
		} else if (_over != OverNone) {
			updateOverState(OverNone);
		}
	} else if (_over != OverNone) {
		updateOverState(OverNone);
	}
}

void MediaView::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		App::activateClickHandler(activated, e->button());
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
	} else if (_over == OverIcon && _down == OverIcon) {
		onDocClick();
	} else if (_over == OverMore && _down == OverMore) {
		QTimer::singleShot(0, this, SLOT(onDropdown()));
	} else if (_over == OverClose && _down == OverClose) {
		close();
	} else if (_over == OverVideo && _down == OverVideo) {
		onVideoPauseResume();
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
			} else if (!_doc || fileShown() || !_docRect.contains(e->pos())) {
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

void MediaView::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos())) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
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

void MediaView::touchEvent(QTouchEvent *e) {
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
		auto weak = make_weak(this);
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

bool MediaView::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
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
				if (ev->orientation() == Qt::Horizontal) {
					if (_accumScroll.x() * _accumScroll.x() > _accumScroll.y() * _accumScroll.y() && _accumScroll.x() != 0) {
						moveToNext(_accumScroll.x() > 0 ? -1 : 1);
					}
					_accumScroll = QPoint();
				}
			}
		}
	}
	return TWidget::event(e);
}

bool MediaView::eventFilter(QObject *obj, QEvent *e) {
	auto type = e->type();
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
	return TWidget::eventFilter(obj, e);
}

void MediaView::setVisible(bool visible) {
	if (!visible) {
		_sharedMedia = nullptr;
		_sharedMediaData = base::none;
		_sharedMediaDataKey = base::none;
		_userPhotos = nullptr;
		_userPhotosData = base::none;
		if (_menu) _menu->hideMenu(true);
		_controlsHideTimer.stop();
		_controlsState = ControlsShown;
		a_cOpacity = anim::value(1, 1);
		_groupThumbs = nullptr;
		_groupThumbsRect = QRect();
	}
	TWidget::setVisible(visible);
	if (visible) {
		QCoreApplication::instance()->installEventFilter(this);
	} else {
		QCoreApplication::instance()->removeEventFilter(this);

		stopGif();
		destroyThemePreview();
		_radial.stop();
	}
}

void MediaView::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
		activateControls();
	}
	_receiveMouse = false;
	QTimer::singleShot(0, this, SLOT(receiveMouse()));
}

void MediaView::receiveMouse() {
	_receiveMouse = true;
}

void MediaView::onDropdown() {
	updateActions();
	_dropdown->clearActions();
	for_const (auto &action, _actions) {
		_dropdown->addAction(action.text, this, action.member);
	}
	_dropdown->moveToRight(0, height() - _dropdown->height());
	_dropdown->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	_dropdown->setFocus();
}

void MediaView::onTouchTimer() {
	_touchRightButton = true;
}

void MediaView::updateImage() {
	update(_saveMsg);
}

void MediaView::findCurrent() {
	using namespace rpl::mappers;
	if (_sharedMediaData) {
		_index = _msgid
			? _sharedMediaData->indexOf(_msgid)
			: _photo ? _sharedMediaData->indexOf(_photo) : base::none;
		_fullIndex = _sharedMediaData->skippedBefore()
			? (_index | func::add(*_sharedMediaData->skippedBefore()))
			: base::none;
		_fullCount = _sharedMediaData->fullCount();
	} else if (_userPhotosData) {
		_index = _photo ? _userPhotosData->indexOf(_photo->id) : base::none;
		_fullIndex = _userPhotosData->skippedBefore()
			? (_index | func::add(*_userPhotosData->skippedBefore()))
			: base::none;
		_fullCount = _userPhotosData->fullCount();
	} else {
		_index = _fullIndex = _fullCount = base::none;
	}
}

void MediaView::updateHeader() {
	auto index = _fullIndex ? *_fullIndex : -1;
	auto count = _fullCount ? *_fullCount : -1;
	if (index >= 0 && index < count && count > 1) {
		if (_doc) {
			_headerText = lng_mediaview_file_n_of_count(lt_file, _doc->filename().isEmpty() ? lang(lng_mediaview_doc_image) : _doc->filename(), lt_n, QString::number(index + 1), lt_count, QString::number(count));
		} else {
			_headerText = lng_mediaview_n_of_count(lt_n, QString::number(index + 1), lt_count, QString::number(count));
		}
	} else {
		if (_doc) {
			_headerText = _doc->filename().isEmpty() ? lang(lng_mediaview_doc_image) : _doc->filename();
		} else if (_msgid) {
			_headerText = lang(lng_mediaview_single_photo);
		} else if (_user) {
			_headerText = lang(lng_mediaview_profile_photo);
		} else if ((_history && _history->channelId() && !_history->isMegagroup())
			|| (_peer && _peer->isChannel() && !_peer->isMegagroup())) {
			_headerText = lang(lng_mediaview_channel_photo);
		} else if (_peer) {
			_headerText = lang(lng_mediaview_group_photo);
		} else {
			_headerText = lang(lng_mediaview_single_photo);
		}
	}
	_headerHasLink = computeOverviewType() != base::none;
	auto hwidth = st::mediaviewThickFont->width(_headerText);
	if (hwidth > width() / 3) {
		hwidth = width() / 3;
		_headerText = st::mediaviewThickFont->elided(_headerText, hwidth, Qt::ElideMiddle);
	}
	_headerNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewHeaderTop, hwidth, st::mediaviewThickFont->height);
}

float64 MediaView::overLevel(OverState control) const {
	auto i = _animOpacities.constFind(control);
	return (i == _animOpacities.cend()) ? (_over == control ? 1 : 0) : i->current();
}
