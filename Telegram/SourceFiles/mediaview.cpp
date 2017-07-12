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
#include "mediaview.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "core/file_utilities.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "media/media_clip_reader.h"
#include "media/view/media_clip_controller.h"
#include "styles/style_mediaview.h"
#include "styles/style_history.h"
#include "media/media_audio.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "window/themes/window_theme_preview.h"
#include "base/task_queue.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "messenger.h"
#include "storage/file_download.h"
#include "calls/calls_instance.h"

namespace {

TextParseOptions _captionTextOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _captionBotOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText | TextParseBotCommands, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

bool typeHasMediaOverview(MediaOverviewType type) {
	switch (type) {
	case OverviewPhotos:
	case OverviewVideos:
	case OverviewMusicFiles:
	case OverviewFiles:
	case OverviewVoiceFiles:
	case OverviewLinks: return true;
	default: break;
	}
	return false;
}

} // namespace

MediaView::MediaView(QWidget*) : TWidget(nullptr)
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
	_saveMsgText.setRichText(st::mediaviewSaveMsgStyle, lang(lng_mediaview_saved), _textDlgOptions, custom);
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::mediaviewSaveMsgPadding.left() + st::mediaviewSaveMsgPadding.right(), st::mediaviewSaveMsgStyle.font->height + st::mediaviewSaveMsgPadding.top() + st::mediaviewSaveMsgPadding.bottom());
	_saveMsgText.setLink(1, MakeShared<LambdaClickHandler>([this] { showSaveMsgFile(); }));

	connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(onScreenResized(int)));

	// While we have one mediaview for all authsessions we have to do this.
	auto handleAuthSessionChange = [this] {
		if (AuthSession::Exists()) {
			subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
				if (!isHidden()) {
					updateControls();
				}
			});
			subscribe(AuthSession::Current().calls().currentCallChanged(), [this](Calls::Call *call) {
				if (call && _clipController && !_videoPaused) {
					onVideoPauseResume();
				}
			});
		}
	};
	subscribe(Messenger::Instance().authSessionChanged(), [handleAuthSessionChange] {
		handleAuthSessionChange();
	});
	handleAuthSessionChange();

	auto observeEvents = Notify::PeerUpdate::Flag::SharedMediaChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		mediaOverviewUpdated(update);
	}));

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);

	hide();
	createWinId();
	if (cPlatform() == dbipWindows) {
		setWindowState(Qt::WindowFullScreen);
	}

	_saveMsgUpdater.setSingleShot(true);
	connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onCheckActive()));

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
	if (App::wnd() && windowHandle() && App::wnd()->windowHandle() && windowHandle()->screen() != App::wnd()->windowHandle()->screen()) {
		windowHandle()->setScreen(App::wnd()->windowHandle()->screen());
	}

	auto wndCenter = App::wnd()->geometry().center();
	QRect avail = Sandbox::screenGeometry(wndCenter);
	if (avail != geometry()) {
		setGeometry(avail);
	}

	int32 navSkip = 2 * st::mediaviewControlMargin + st::mediaviewControlSize;
	_closeNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, st::mediaviewControlMargin, st::mediaviewControlSize, st::mediaviewControlSize);
	_closeNavIcon = centerrect(_closeNav, st::mediaviewClose);
	_leftNav = myrtlrect(st::mediaviewControlMargin, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_leftNavIcon = centerrect(_leftNav, st::mediaviewLeft);
	_rightNav = myrtlrect(width() - st::mediaviewControlMargin - st::mediaviewControlSize, navSkip, st::mediaviewControlSize, height() - 2 * navSkip);
	_rightNavIcon = centerrect(_rightNav, st::mediaviewRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
}

void MediaView::mediaOverviewUpdated(const Notify::PeerUpdate &update) {
	if (isHidden() || (!_photo && !_doc)) return;
	if (_photo && _overview == OverviewChatPhotos && _history && !_history->peer->isUser()) {
		auto lastChatPhoto = computeLastOverviewChatPhoto();
		if (_index < 0 && _photo == lastChatPhoto.photo && _photo == _additionalChatPhoto) {
			auto firstOpened = _firstOpenedPeerPhoto;
			showPhoto(_photo, lastChatPhoto.item);
			_firstOpenedPeerPhoto = firstOpened;
			return;
		}
		computeAdditionalChatPhoto(_history->peer, lastChatPhoto.photo);
	}

	if (_history && (_history->peer == update.peer || (_migrated && _migrated->peer == update.peer)) && (update.mediaTypesMask & (1 << _overview)) && _msgid) {
		_index = -1;
		if (_msgmigrated) {
			for (int i = 0, l = _migrated->overview[_overview].size(); i < l; ++i) {
				if (_migrated->overview[_overview].at(i) == _msgid) {
					_index = i;
					break;
				}
			}
		} else {
			for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
				if (_history->overview[_overview].at(i) == _msgid) {
					_index = i;
					break;
				}
			}
		}
		updateControls();
		preloadData(0);
	} else if (_user == update.peer && update.mediaTypesMask & (1 << OverviewCount)) {
		if (!_photo) return;

		_index = -1;
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == _photo) {
				_index = i;
				break;
			}
		}
		updateControls();
		preloadData(0);
	}
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
			if (_doc && (_doc->isVideo() || _doc->isRoundVideo()) && _autoplayVideoDocument != _doc && !_gif->videoPaused()) {
				_gif->pauseResumeVideo();
				const_cast<MediaView*>(this)->_videoPaused = _gif->videoPaused();
			}
			auto rounding = (_doc && _doc->isRoundVideo()) ? ImageRoundRadius::Ellipse : ImageRoundRadius::None;
			_gif->start(_gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), _gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), rounding, ImageRoundCorner::All);
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

void MediaView::changingMsgId(HistoryItem *row, MsgId newId) {
	if (row->id == _msgid) {
		_msgid = newId;
	}

	// Send a fake update.
	Notify::PeerUpdate update(row->history()->peer);
	update.flags |= Notify::PeerUpdate::Flag::SharedMediaChanged;
	update.mediaTypesMask |= (1 << _overview);
	mediaOverviewUpdated(update);
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

	QDateTime d, dNow(date(unixtime()));
	if (_photo) {
		d = date(_photo->date);
	} else if (_doc) {
		d = date(_doc->date);
	} else if (HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		d = item->date;
	}
	if (d.date() == dNow.date()) {
		_dateText = lng_mediaview_today(lt_time, d.time().toString(cTimeFormat()));
	} else if (d.date().addDays(1) == dNow.date()) {
		_dateText = lng_mediaview_yesterday(lt_time, d.time().toString(cTimeFormat()));
	} else {
		_dateText = lng_mediaview_date_time(lt_date, d.date().toString(qsl("dd.MM.yy")), lt_time, d.time().toString(cTimeFormat()));
	}
	if (_from) {
		_fromName.setText(st::mediaviewTextStyle, (_from->migrateTo() ? _from->migrateTo() : _from)->name, _textNameOptions);
		_nameNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, qMin(_fromName.maxWidth(), width() / 3), st::mediaviewFont->height);
		_dateNav = myrtlrect(st::mediaviewTextLeft + _nameNav.width() + st::mediaviewTextSkip, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = myrtlrect(st::mediaviewTextLeft, height() - st::mediaviewTextTop, st::mediaviewFont->width(_dateText), st::mediaviewFont->height);
	}
	updateHeader();
	if (_photo || (_history && _overview != OverviewCount)) {
		_leftNavVisible = (_index > 0) || (_index == 0 && (
			(!_msgmigrated && _history && _history->overview[_overview].size() < _history->overviewCount(_overview)) ||
			(_msgmigrated && _migrated && _migrated->overview[_overview].size() < _migrated->overviewCount(_overview)) ||
			(!_msgmigrated && _history && _migrated && (!_migrated->overview[_overview].isEmpty() || _migrated->overviewCount(_overview) > 0)))) ||
			(_index < 0 && _photo == _additionalChatPhoto &&
				((_history && _history->overviewCount(_overview) > 0) ||
				(_migrated && _history->overviewLoaded(_overview) && _migrated->overviewCount(_overview) > 0))
			);
		_rightNavVisible = (_index >= 0) && (
			(!_msgmigrated && _history && _index + 1 < _history->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _index + 1 < _migrated->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _history && (!_history->overview[_overview].isEmpty() || _history->overviewCount(_overview) > 0)) ||
			(!_msgmigrated && _history && _index + 1 == _history->overview[_overview].size() && _additionalChatPhoto) ||
			(_msgmigrated && _migrated && _index + 1 == _migrated->overview[_overview].size() && _history->overviewCount(_overview) == 0 && _additionalChatPhoto) ||
			(!_history && _user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount)));
		if (_msgmigrated && !_history->overviewLoaded(_overview)) {
			_leftNavVisible = _rightNavVisible = false;
		}
	} else {
		_leftNavVisible = _rightNavVisible = false;
	}

	if (!_caption.isEmpty()) {
		int32 skipw = qMax(_dateNav.left() + _dateNav.width(), _headerNav.left() + _headerNav.width());
		int32 maxw = qMin(qMax(width() - 2 * skipw - st::mediaviewCaptionPadding.left() - st::mediaviewCaptionPadding.right() - 2 * st::mediaviewCaptionMargin.width(), int(st::msgMinWidth)), _caption.maxWidth());
		int32 maxh = qMin(_caption.countHeight(maxw), int(height() / 4 - st::mediaviewCaptionPadding.top() - st::mediaviewCaptionPadding.bottom() - 2 * st::mediaviewCaptionMargin.height()));
		_captionRect = QRect((width() - maxw) / 2, height() - maxh - st::mediaviewCaptionPadding.bottom() - st::mediaviewCaptionMargin.height(), maxw, maxh);
	} else {
		_captionRect = QRect();
	}
	if (_clipController) {
		setClipControllerGeometry();
	}
	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

void MediaView::updateActions() {
	_actions.clear();

	if (_doc && _doc->loading()) {
		_actions.push_back({ lang(lng_cancel), SLOT(onSaveCancel()) });
	}
	if (_msgid > 0 && _msgid < ServerMaxMsgId) {
		_actions.push_back({ lang(lng_context_to_msg), SLOT(onToMessage()) });
	}
	if (_doc && !_doc->filepath(DocumentData::FilePathResolveChecked).isEmpty()) {
		_actions.push_back({ lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), SLOT(onShowInFolder()) });
	}
	if ((_doc && fileShown()) || (_photo && _photo->loaded())) {
		_actions.push_back({ lang(lng_mediaview_copy), SLOT(onCopy()) });
	}
	if (_canForward) {
		_actions.push_back({ lang(lng_mediaview_forward), SLOT(onForward()) });
	}
	if (_canDelete || (_photo && App::self() && _user == App::self()) || (_photo && _photo->peer && _photo->peer->photoId == _photo->id && (_photo->peer->isChat() || (_photo->peer->isChannel() && _photo->peer->asChannel()->amCreator())))) {
		_actions.push_back({ lang(lng_mediaview_delete), SLOT(onDelete()) });
	}
	_actions.push_back({ lang(lng_mediaview_save_as), SLOT(onSaveAs()) });
	if (_history && typeHasMediaOverview(_overview)) {
		_actions.push_back({ lang(_doc ? lng_mediaview_files_all : lng_mediaview_photos_all), SLOT(onOverview()) });
	}
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
		QRegion toUpdate = QRegion() + (_over == OverLeftNav ? _leftNav : _leftNavIcon) + (_over == OverRightNav ? _rightNav : _rightNavIcon) + (_over == OverClose ? _closeNav : _closeNavIcon) + _saveNavIcon + _moreNavIcon + _headerNav + _nameNav + _dateNav + _captionRect.marginsAdded(st::mediaviewCaptionPadding);
		update(toUpdate);
		if (dt < 1) result = true;
	}
	if (!result && _animations.isEmpty()) {
		_a_state.stop();
	}
}

void MediaView::updateCursor() {
	setCursor(_controlsState == ControlsHidden ? Qt::BlankCursor : (_over == OverNone ? style::cur_default : style::cur_pointer));
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
	auto wasAnimating = _radial.animating();
	_radial.update(radialProgress(), !radialLoading(), ms + radialTimeShift());
	if (timer && (wasAnimating || _radial.animating())) {
		update(radialRect());
	}
	if (_doc && _doc->loaded() && _doc->size < App::kImageSizeLimit && (!_radial.animating() || _doc->isAnimation() || _doc->isVideo())) {
		if (_doc->isVideo() || _doc->isRoundVideo()) {
			_autoplayVideoDocument = _doc;
		}
		if (!_doc->data().isEmpty() && (_doc->isAnimation() || _doc->isVideo())) {
			displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
		} else {
			auto &location = _doc->location(true);
			if (location.accessEnable()) {
				if (_doc->isAnimation() || _doc->isVideo() || _doc->isTheme() || QImageReader(location.name()).canRead()) {
					displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
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
	_history = _migrated = nullptr;
	_peer = _from = nullptr;
	_user = nullptr;
	_photo = _additionalChatPhoto = nullptr;
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
	if (_doc && (_doc->isVideo() || _doc->isRoundVideo())) {
		Media::Player::mixer()->setVideoVolume(Global::VideoVolume());
	}
}

void MediaView::close() {
	if (_menu) _menu->hideMenu(true);
	if (App::wnd()) {
		Ui::hideLayer(true);
	}
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
		auto item = (_msgid ? App::histItemById(_msgmigrated ? 0 : _channel, _msgid) : nullptr);
		if (_photo) {
			displayPhoto(_photo, item);
		} else if (_doc) {
			displayDocument(_doc, item);
		}
	}
}

void MediaView::onToMessage() {
	if (auto item = _msgid ? App::histItemById(_msgmigrated ? 0 : _channel, _msgid) : 0) {
		if (App::wnd()) {
			close();
			Ui::showPeerHistoryAtItem(item);
		}
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
			MimeType mimeType = mimeTypeForName(_doc->mime);
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
			file = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, true, alreadyDir);
			psShowOverAll(this);
			if (!file.isEmpty() && file != location.name()) {
				if (_doc->data().isEmpty()) {
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
		FileDialog::GetWritePath(lang(lng_save_photo), filter, filedialogDefaultName(qsl("photo"), qsl(".jpg"), QString(), false, _photo->date), base::lambda_guarded(this, [this, photo = _photo](const QString &result) {
			if (!result.isEmpty() && _photo == photo && photo->loaded()) {
				photo->full->pix().toImage().save(result, "JPG");
			}
			psShowOverAll(this);
		}), base::lambda_guarded(this, [this] {
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
		if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
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
				_videoIsSilent = _doc && (_doc->isVideo() || _doc->isRoundVideo()) && !_gif->hasAudio();
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
	return _history ? _history->peer : nullptr;
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
			toName = filedialogNextFilename(_doc->name, location.name(), path);
			if (toName != location.name() && !QFile(location.name()).copy(toName)) {
				toName = QString();
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
	auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid);
	if (!_msgid || !item || item->id < 0 || item->serviceMsg()) return;

	if (App::wnd()) {
		close();
		if (auto main = App::main()) {
			auto items = SelectedItemSet();
			items.insert(item->id, item);
			main->showForwardLayer(items);
		}
	}
}

void MediaView::onDelete() {
	close();
	auto deletingPeerPhoto = [this]() {
		if (!_msgid) return true;
		if (_photo && _history) {
			auto lastPhoto = computeLastOverviewChatPhoto();
			if (lastPhoto.photo == _photo && _history->peer->photoId == _photo->id) {
				return _firstOpenedPeerPhoto;
			}
		}
		return false;
	};

	if (deletingPeerPhoto()) {
		App::main()->deletePhotoLayer(_photo);
	} else if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		App::contextItem(item);
		App::main()->deleteLayer();
	}
}

void MediaView::onOverview() {
	if (_menu) _menu->hideMenu(true);
	if (!_history || !typeHasMediaOverview(_overview)) {
		update();
		return;
	}
	close();
	if (_history->peer) App::main()->showMediaOverview(_history->peer, _overview);
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

void MediaView::showPhoto(PhotoData *photo, HistoryItem *context) {
	_history = context ? context->history() : nullptr;
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	}
	_additionalChatPhoto = nullptr;
	_firstOpenedPeerPhoto = false;
	_peer = 0;
	_user = 0;
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

	_index = -1;
	_msgid = context ? context->id : 0;
	_msgmigrated = context ? (context->history() == _migrated) : false;
	_channel = _history ? _history->channelId() : NoChannel;
	_canForward = context ? context->canForward() : false;
	_canDelete = context ? context->canDelete() : false;
	_photo = photo;
	if (_history) {
		if (context && !context->toHistoryMessage()) {
			_overview = OverviewChatPhotos;
			if (!_history->peer->isUser()) {
				computeAdditionalChatPhoto(_history->peer, computeLastOverviewChatPhoto().photo);
			}
		} else {
			_overview = OverviewPhotos;
		}
		findCurrent();
	}

	displayPhoto(photo, context);
	preloadData(0);
	activateControls();
}

void MediaView::showPhoto(PhotoData *photo, PeerData *context) {
	_history = _migrated = nullptr;
	_additionalChatPhoto = nullptr;
	_firstOpenedPeerPhoto = true;
	_peer = context;
	_user = context->asUser();
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_msgid = 0;
	_msgmigrated = false;
	_channel = NoChannel;
	_canForward = _canDelete = false;
	_index = -1;
	_photo = photo;
	_overview = OverviewCount;
	if (_user) {
		if (_user->photos.isEmpty() && _user->photosCount < 0 && _user->photoId && _user->photoId != UnknownPeerPhotoId) {
			_index = 0;
		}
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == photo) {
				_index = i;
				break;
			}
		}

		if (_user->photosCount < 0) {
			loadBack();
		}
	} else if ((_history = App::historyLoaded(_peer))) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}

		auto lastChatPhoto = computeLastOverviewChatPhoto();
		if (_photo == lastChatPhoto.photo) {
			showPhoto(_photo, lastChatPhoto.item);
			_firstOpenedPeerPhoto = true;
			return;
		}

		computeAdditionalChatPhoto(_history->peer, lastChatPhoto.photo);
		if (_additionalChatPhoto == _photo) {
			_overview = OverviewChatPhotos;
			findCurrent();
		} else {
			_additionalChatPhoto = nullptr;
			_history = _migrated = nullptr;
		}
	}
	displayPhoto(photo, 0);
	preloadData(0);
	activateControls();
}

void MediaView::showDocument(DocumentData *doc, HistoryItem *context) {
	_photo = 0;
	_history = context ? context->history() : nullptr;
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	}
	_additionalChatPhoto = nullptr;
	_saveMsgStarted = 0;
	_peer = 0;
	_user = 0;
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

	_index = -1;
	_msgid = context ? context->id : 0;
	_msgmigrated = context ? (context->history() == _migrated) : false;
	_channel = _history ? _history->channelId() : NoChannel;
	_canForward = context ? context->canForward() : false;
	_canDelete = context ? context->canDelete() : false;
	if (_history) {
		_overview = doc->isGifv() ? OverviewGIFs : doc->isVideo() ? OverviewVideos : OverviewFiles;
		findCurrent();
	}
	if (doc->isVideo() || doc->isRoundVideo()) {
		_autoplayVideoDocument = doc;
	}
	displayDocument(doc, context);
	preloadData(0);
	activateControls();
}

void MediaView::displayPhoto(PhotoData *photo, HistoryItem *item) {
	stopGif();
	destroyThemePreview();
	_doc = nullptr;
	_fullScreenVideo = false;
	_photo = photo;
	_radial.stop();

	_photoRadialRect = QRect(QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2), st::radialSize);

	_zoom = 0;

	_caption = Text();
	if (auto itemMsg = item ? item->toHistoryMessage() : nullptr) {
		if (auto photoMsg = dynamic_cast<HistoryPhoto*>(itemMsg->getMedia())) {
			_caption.setMarkedText(st::mediaviewCaptionStyle, photoMsg->getCaption(), (item->author()->isUser() && item->author()->asUser()->botInfo) ? _captionBotOptions : _captionTextOptions);
		}
	}

	_zoomToScreen = 0;
	AuthSession::Current().downloader().clearPriorities();
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
		_from = item->peerOriginal();
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
	auto documentChanged = (!doc || doc != _doc || (item && (item->id != _msgid || (item->history() != (_msgmigrated ? _migrated : _history)))));
	if (documentChanged || (!doc->isAnimation() && !doc->isVideo())) {
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

	if (_autoplayVideoDocument && _doc != _autoplayVideoDocument) {
		_autoplayVideoDocument = nullptr;
	}

	_caption = Text();
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

			if (_doc->isAnimation() || _doc->isVideo()) {
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
			_docName = (_doc->type == StickerDocument) ? lang(lng_in_dlg_sticker) : (_doc->type == AnimatedDocument ? qsl("GIF") : (_doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name));
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
		_from = item->peerOriginal();
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
	if (_doc && _doc->isRoundVideo()) {
		options |= Images::Option::Circled;
	}
	return options;
}

void MediaView::initAnimation() {
	Expects(_doc != nullptr);
	Expects(_doc->isAnimation() || _doc->isVideo());

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
	Expects(_doc->isAnimation() || _doc->isVideo());

	if (_doc->dimensions.width() && _doc->dimensions.height()) {
		int w = _doc->dimensions.width();
		int h = _doc->dimensions.height();
		_current = _doc->thumb->pixNoCache(w, h, videoThumbOptions(), w / cIntRetinaFactor(), h / cIntRetinaFactor());
		if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
	} else {
		_current = _doc->thumb->pixNoCache(_doc->thumb->width(), _doc->thumb->height(), videoThumbOptions(), st::mediaviewFileIconSize, st::mediaviewFileIconSize);
	}
	auto mode = (_doc->isVideo() || _doc->isRoundVideo()) ? Media::Clip::Reader::Mode::Video : Media::Clip::Reader::Mode::Gif;
	_gif = Media::Clip::MakeReader(_doc, FullMsgId(_channel, _msgid), [this](Media::Clip::Notification notification) {
		clipCallback(notification);
	}, mode);

	// Correct values will be set when gif gets inited.
	_videoPaused = _videoIsSilent = _videoStopped = false;
	_videoPositionMs = 0ULL;
	_videoDurationMs = _doc->duration() * 1000ULL;

	createClipController();
}

void MediaView::initThemePreview() {
	t_assert(_doc && _doc->isTheme());

	auto &location = _doc->location();
	if (!location.isEmpty() && location.accessEnable()) {
		_themePreviewShown = true;
		auto path = _doc->location().name();
		auto id = _themePreviewId = rand_value<uint64>();
		auto ready = base::lambda_guarded(this, [this, id](std::unique_ptr<Window::Theme::Preview> result) {
			if (id != _themePreviewId) {
				return;
			}
			_themePreviewId = 0;
			_themePreview = std::move(result);
			if (_themePreview) {
				_themeApply.create(this, langFactory(lng_theme_preview_apply), st::themePreviewApplyButton);
				_themeApply->show();
				_themeApply->setClickedCallback([this] {
					auto preview = std::move(_themePreview);
					close();
					Window::Theme::Apply(std::move(preview));
				});
				_themeCancel.create(this, langFactory(lng_cancel), st::themePreviewCancelButton);
				_themeCancel->show();
				_themeCancel->setClickedCallback([this] { close(); });
				updateControls();
			}
			update();
		});

		Window::Theme::CurrentData current;
		current.backgroundId = Window::Theme::Background()->id();
		current.backgroundImage = Window::Theme::Background()->pixmap();
		current.backgroundTiled = Window::Theme::Background()->tile();
		base::TaskQueue::Normal().Put([ready = std::move(ready), path, current]() mutable {
			auto preview = Window::Theme::GeneratePreview(path, current);
			base::TaskQueue::Main().Put([ready = std::move(ready), result = std::move(preview)]() mutable {
				ready(std::move(result));
			});
		});
		location.accessDisable();
	}
}

void MediaView::createClipController() {
	Expects(_doc != nullptr);
	if (!_doc->isVideo() && !_doc->isRoundVideo()) return;

	_clipController.create(this);
	setClipControllerGeometry();
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

void MediaView::setClipControllerGeometry() {
	t_assert(_clipController != nullptr);

	int controllerBottom = _captionRect.isEmpty() ? height() : _captionRect.y();
	_clipController->setGeometry(
		(width() - _clipController->width()) / 2,
		controllerBottom - _clipController->height() - st::mediaviewCaptionPadding.bottom() - st::mediaviewCaptionMargin.height(),
		st::mediaviewControllerSize.width(),
		st::mediaviewControllerSize.height());
	myEnsureResized(_clipController);
}

void MediaView::onVideoPauseResume() {
	if (!_gif) return;

	if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		if (_gif->state() == Media::Clip::State::Error) {
			displayDocument(_doc, item);
		} else if (_gif->state() == Media::Clip::State::Finished) {
			restartVideoAtSeekPosition(0);
		} else {
			_gif->pauseResumeVideo();
			_videoPaused = _gif->videoPaused();
			if (_videoIsSilent) {
				updateSilentVideoPlaybackState();
			}
		}
	} else {
		stopGif();
		updateControls();
		update();
	}
}

void MediaView::restartVideoAtSeekPosition(TimeMs positionMs) {
	_autoplayVideoDocument = _doc;

	if (_current.isNull()) {
		auto rounding = (_doc && _doc->isRoundVideo()) ? ImageRoundRadius::Ellipse : ImageRoundRadius::None;
		_current = _gif->current(_gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), _gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), rounding, ImageRoundCorner::All, getms());
	}
	_gif = Media::Clip::MakeReader(_doc, FullMsgId(_channel, _msgid), [this](Media::Clip::Notification notification) {
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
		AuthSession::Current().data().setLastTimeVideoPlayedAt(getms(true));
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
		if (_doc && _doc->isRoundVideo()) {
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
			auto rounding = (_doc && _doc->isRoundVideo()) ? ImageRoundRadius::Ellipse : ImageRoundRadius::None;
			auto toDraw = _current.isNull() ? _gif->current(_gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), _gif->width() / cIntRetinaFactor(), _gif->height() / cIntRetinaFactor(), rounding, ImageRoundCorner::None, ms) : _current;
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
				return &st::historyFileInCancel;
			}
			return &st::historyFileInDownload;
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
			p.drawPixmapLeft(_themePreviewRect.x(), _themePreviewRect.y(), width(), _themePreview->preview);
		} else {
			p.fillRect(fill, st::themePreviewBg);
			p.setFont(st::themePreviewLoadingFont);
			p.setPen(st::themePreviewLoadingFg);
			p.drawText(_themePreviewRect, lang(_themePreviewId ? lng_theme_preview_generating : lng_theme_preview_invalid), QTextOption(style::al_center));
		}
	}

	auto fillOverlay = [this, &p, clip](QRect fill) {
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
		} else if (_doc && (_doc->isVideo() || _doc->isRoundVideo())) {
			onVideoPauseResume();
		}
	} else if (e->key() == Qt::Key_Left) {
		moveToNext(-1);
	} else if (e->key() == Qt::Key_Right) {
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

bool MediaView::moveToNext(int32 delta) {
	if (_index < 0) {
		if (delta == -1 && _photo == _additionalChatPhoto) {
			auto lastChatPhoto = computeLastOverviewChatPhoto();
			if (lastChatPhoto.item) {
				if (lastChatPhoto.item->history() == _history) {
					_index = _history->overview[_overview].size() - 1;
					_msgmigrated = false;
				} else {
					_index = _migrated->overview[_overview].size() - 1;
					_msgmigrated = true;
				}
				_msgid = lastChatPhoto.item->id;
				_channel = _history ? _history->channelId() : NoChannel;
				_canForward = lastChatPhoto.item->canForward();
				_canDelete = lastChatPhoto.item->canDelete();
				displayPhoto(lastChatPhoto.photo, lastChatPhoto.item);
				preloadData(delta);
				return true;
			} else if (_history && (_history->overviewCount(OverviewChatPhotos) != 0 || (
				_migrated && _migrated->overviewCount(OverviewChatPhotos) != 0))) {
				loadBack();
				return true;
			}
		}
		return false;
	}
	if (_overview == OverviewCount && (_history || !_user)) {
		return false;
	}
	if (_msgmigrated && !_history->overviewLoaded(_overview)) {
		return true;
	}

	int32 newIndex = _index + delta;
	if (_history && _overview != OverviewCount) {
		bool newMigrated = _msgmigrated;
		if (!newMigrated && newIndex < 0 && _migrated) {
			newIndex += _migrated->overview[_overview].size();
			newMigrated = true;
		} else if (newMigrated && newIndex >= _migrated->overview[_overview].size()) {
			newIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
			newMigrated = false;
		}
		if (newIndex >= 0 && newIndex < (newMigrated ? _migrated : _history)->overview[_overview].size()) {
			if (auto item = App::histItemById(newMigrated ? 0 : _channel, (newMigrated ? _migrated : _history)->overview[_overview][newIndex])) {
				_index = newIndex;
				_msgid = item->id;
				_msgmigrated = (item->history() == _migrated);
				_channel = _history ? _history->channelId() : NoChannel;
				_canForward = item->canForward();
				_canDelete = item->canDelete();
				stopGif();
				if (auto media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: displayPhoto(static_cast<HistoryPhoto*>(item->getMedia())->photo(), item); preloadData(delta); break;
					case MediaTypeFile:
					case MediaTypeVideo:
					case MediaTypeGif:
					case MediaTypeSticker: displayDocument(media->getDocument(), item); preloadData(delta); break;
					}
				} else {
					displayDocument(nullptr, item);
					preloadData(delta);
				}
			}
		} else if (!newMigrated && newIndex == _history->overview[_overview].size() && _additionalChatPhoto) {
			_index = -1;
			_msgid = 0;
			_msgmigrated = false;
			_canForward = false;
			_canDelete = false;
			displayPhoto(_additionalChatPhoto, 0);
		}
		if (delta < 0 && _index < MediaOverviewStartPerPage) {
			loadBack();
		}
	} else if (_user) {
		if (newIndex >= 0 && newIndex < _user->photos.size()) {
			_index = newIndex;
			displayPhoto(_user->photos[_index], 0);
			preloadData(delta);
		}
		if (delta > 0 && _index > _user->photos.size() - MediaOverviewStartPerPage) {
			loadBack();
		}
	}
	return true;
}

void MediaView::preloadData(int32 delta) {
	int indexInOverview = _index;
	bool indexOfMigratedItem = _msgmigrated;
	if (_index < 0) {
		if (_overview != OverviewChatPhotos || !_history) return;
		indexInOverview = _history->overview[OverviewChatPhotos].size();
		indexOfMigratedItem = false;
	}
	if (!_user && _overview == OverviewCount) return;

	int32 from = indexInOverview + (delta ? delta : -1), to = indexInOverview + (delta ? delta * MediaOverviewPreloadCount : 1);
	if (from > to) qSwap(from, to);
	if (_history && _overview != OverviewCount) {
		int32 forgetIndex = indexInOverview - delta * 2;
		History *forgetHistory = indexOfMigratedItem ? _migrated : _history;
		if (_migrated) {
			if (indexOfMigratedItem && forgetIndex >= _migrated->overview[_overview].size()) {
				forgetHistory = _history;
				forgetIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
			} else if (!indexOfMigratedItem && forgetIndex < 0) {
				forgetHistory = _migrated;
				forgetIndex += _migrated->overview[_overview].size();
			}
		}
		if (forgetIndex >= 0 && forgetIndex < forgetHistory->overview[_overview].size() && (forgetHistory != (indexOfMigratedItem ? _migrated : _history) || forgetIndex != indexInOverview)) {
			if (HistoryItem *item = App::histItemById(forgetHistory->channelId(), forgetHistory->overview[_overview][forgetIndex])) {
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->forget(); break;
					case MediaTypeFile:
					case MediaTypeVideo:
					case MediaTypeGif:
					case MediaTypeSticker: media->getDocument()->forget(); break;
					}
				}
			}
		}

		for (int32 i = from; i <= to; ++i) {
			History *previewHistory = indexOfMigratedItem ? _migrated : _history;
			int32 previewIndex = i;
			if (_migrated) {
				if (indexOfMigratedItem && previewIndex >= _migrated->overview[_overview].size()) {
					previewHistory = _history;
					previewIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
				} else if (!indexOfMigratedItem && previewIndex < 0) {
					previewHistory = _migrated;
					previewIndex += _migrated->overview[_overview].size();
				}
			}
			if (previewIndex >= 0 && previewIndex < previewHistory->overview[_overview].size() && (previewHistory != (indexOfMigratedItem ? _migrated : _history) || previewIndex != indexInOverview)) {
				if (HistoryItem *item = App::histItemById(previewHistory->channelId(), previewHistory->overview[_overview][previewIndex])) {
					if (HistoryMedia *media = item->getMedia()) {
						switch (media->type()) {
						case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->download(); break;
						case MediaTypeFile:
						case MediaTypeVideo:
						case MediaTypeGif: {
							DocumentData *doc = media->getDocument();
							doc->thumb->load();
							doc->automaticLoad(item);
						} break;
						case MediaTypeSticker: media->getDocument()->sticker()->img->load(); break;
						}
					}
				}
			}
		}
	} else if (_user) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != indexInOverview) {
				_user->photos[i]->thumb->load();
			}
		}
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != indexInOverview) {
				_user->photos[i]->download();
			}
		}
		int32 forgetIndex = indexInOverview - delta * 2;
		if (forgetIndex >= 0 && forgetIndex < _user->photos.size() && forgetIndex != indexInOverview) {
			_user->photos[forgetIndex]->forget();
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
	} else if ((_msgid > 0 && _msgid < ServerMaxMsgId) && _dateNav.contains(pos)) {
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
		if ((_doc->isVideo() || _doc->isRoundVideo()) && _gif) {
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
		if (App::wnd() && _from) {
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
	activateControls();
}

void MediaView::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos())) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new Ui::PopupMenu(nullptr, st::mediaviewPopupMenu);
		updateActions();
		for_const (auto &action, _actions) {
			_menu->addAction(action.text, this, action.member)->setEnabled(true);
		}
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
		activateControls();
	}
}

void MediaView::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && App::wnd()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(App::wnd()->mapFromGlobal(_touchStart));

			QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			pressEvent.accept();
			mousePressEvent(&pressEvent);

			QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			mouseReleaseEvent(&releaseEvent);

			if (_touchRightButton) {
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
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
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
			auto mouseEvent = static_cast<QMouseEvent*>(e);
			auto mousePosition = mapFromGlobal(mouseEvent->globalPos());
			bool activate = (mousePosition != _lastMouseMovePos);
			_lastMouseMovePos = mousePosition;
			if (type == QEvent::MouseButtonPress) {
				_mousePressed = true;
				activate = true;
			} else if (type == QEvent::MouseButtonRelease) {
				_mousePressed = false;
				activate = true;
			}
			if (activate) activateControls();
		}
	}
	return TWidget::eventFilter(obj, e);
}

void MediaView::setVisible(bool visible) {
	if (!visible) {
		_controlsHideTimer.stop();
		_controlsState = ControlsShown;
		a_cOpacity = anim::value(1, 1);
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

void MediaView::onCheckActive() {
	if (App::wnd() && isVisible()) {
		if (App::wnd()->isActiveWindow() && App::wnd()->hasFocus()) {
			activateWindow();
			Sandbox::setActiveWindow(this);
			setFocus();
		}
	}
}

void MediaView::onTouchTimer() {
	_touchRightButton = true;
}

void MediaView::updateImage() {
	update(_saveMsg);
}

void MediaView::findCurrent() {
	if (_msgmigrated) {
		for (int i = 0, l = _migrated->overview[_overview].size(); i < l; ++i) {
			if (_migrated->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		if (!_history->overviewCountLoaded(_overview)) {
			loadBack();
		} else if (_history->overviewLoaded(_overview) && !_migrated->overviewLoaded(_overview)) { // all loaded
			if (!_migrated->overviewCountLoaded(_overview) || (_index < 2 && _migrated->overviewCount(_overview) > 0)) {
				loadBack();
			}
		}
	} else {
		for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
			if (_history->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		if (!_history->overviewLoaded(_overview)) {
			if (!_history->overviewCountLoaded(_overview) || (_index < 2 && _history->overviewCount(_overview) > 0) || (_index < 1 && _migrated && !_migrated->overviewLoaded(_overview))) {
				loadBack();
			}
		} else if (_index < 1 && _migrated && !_migrated->overviewLoaded(_overview)) {
			loadBack();
		}
		if (_migrated && !_migrated->overviewCountLoaded(_overview)) {
			App::main()->preloadOverview(_migrated->peer, _overview);
		}
	}
}

void MediaView::loadBack() {
	if (_loadRequest || (_overview == OverviewCount && !_user)) {
		return;
	}
	if (_index < 0 && (!_additionalChatPhoto || _photo != _additionalChatPhoto || !_history)) {
		return;
	}

	if (_history && _overview != OverviewCount && (!_history->overviewLoaded(_overview) || (_migrated && !_migrated->overviewLoaded(_overview)))) {
		if (App::main()) {
			if (_msgmigrated || (_migrated && _index == 0 && _history->overviewLoaded(_overview))) {
				App::main()->loadMediaBack(_migrated->peer, _overview);
			} else {
				App::main()->loadMediaBack(_history->peer, _overview);
				if (_migrated && _index == 0 && (_migrated->overviewCount(_overview) < 0 || _migrated->overview[_overview].isEmpty()) && !_migrated->overviewLoaded(_overview)) {
					App::main()->loadMediaBack(_migrated->peer, _overview);
				}
			}
			if (_msgmigrated && !_history->overviewCountLoaded(_overview)) {
				App::main()->preloadOverview(_history->peer, _overview);
			}
		}
	} else if (_user && _user->photosCount != 0) {
		int32 limit = (_index < MediaOverviewStartPerPage && _user->photos.size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
		_loadRequest = MTP::send(MTPphotos_GetUserPhotos(_user->inputUser, MTP_int(_user->photos.size()), MTP_long(0), MTP_int(limit)), rpcDone(&MediaView::userPhotosLoaded, _user));
	}
}

MediaView::LastChatPhoto MediaView::computeLastOverviewChatPhoto() {
	LastChatPhoto emptyResult = { nullptr, nullptr };
	auto lastPhotoInOverview = [&emptyResult](auto history, auto list) -> LastChatPhoto {
		if (auto item = App::histItemById(history->channelId(), list.back())) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypePhoto && !item->toHistoryMessage()) {
					return { item, static_cast<HistoryPhoto*>(media)->photo() };
				}
			}
		}
		return emptyResult;
	};

	if (!_history) return emptyResult;
	auto &list = _history->overview[OverviewChatPhotos];
	if (!list.isEmpty()) {
		return lastPhotoInOverview(_history, list);
	}

	if (!_migrated || !_history->overviewLoaded(OverviewChatPhotos)) return emptyResult;
	auto &migratedList = _migrated->overview[OverviewChatPhotos];
	if (!migratedList.isEmpty()) {
		return lastPhotoInOverview(_migrated, migratedList);
	}
	return emptyResult;
}

void MediaView::computeAdditionalChatPhoto(PeerData *peer, PhotoData *lastOverviewPhoto) {
	if (!peer->photoId || peer->photoId == UnknownPeerPhotoId) {
		_additionalChatPhoto = nullptr;
	} else if (lastOverviewPhoto && lastOverviewPhoto->id == peer->photoId) {
		_additionalChatPhoto = nullptr;
	} else {
		_additionalChatPhoto = App::photo(peer->photoId);
	}
}

void MediaView::userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPPhoto> *v = nullptr;
	switch (photos.type()) {
	case mtpc_photos_photos: {
		auto &d = photos.c_photos_photos();
		App::feedUsers(d.vusers);
		v = &d.vphotos.v;
		u->photosCount = 0;
	} break;

	case mtpc_photos_photosSlice: {
		auto &d = photos.c_photos_photosSlice();
		App::feedUsers(d.vusers);
		u->photosCount = d.vcount.v;
		v = &d.vphotos.v;
	} break;

	default: return;
	}

	if (v->isEmpty()) {
		u->photosCount = 0;
	}

	for (auto i = v->cbegin(), e = v->cend(); i != e; ++i) {
		auto photo = App::feedPhoto(*i);
		photo->thumb->load();
		u->photos.push_back(photo);
	}
	Notify::mediaOverviewUpdated(u, OverviewCount);
}

void MediaView::updateHeader() {
	int32 index = _index, count = 0, addcount = (_migrated && _overview != OverviewCount) ? _migrated->overviewCount(_overview) : 0;
	if (_history) {
		if (_overview != OverviewCount) {
			bool lastOverviewPhotoLoaded = (!_history->overview[_overview].isEmpty() || (
				_migrated && _history->overviewCount(_overview) == 0 && !_migrated->overview[_overview].isEmpty()));
			count = _history->overviewCount(_overview);
			if (addcount >= 0 && count >= 0) {
				count += addcount;
			}
			if (index >= 0 && (_msgmigrated ? (count >= 0 && addcount >= 0 && _history->overviewLoaded(_overview)) : (count >= 0))) {
				if (_msgmigrated) {
					index += addcount - _migrated->overview[_overview].size();
				} else {
					index += count - _history->overview[_overview].size();
				}
				if (_additionalChatPhoto && lastOverviewPhotoLoaded) {
					++count;
				}
			} else if (index < 0 && _additionalChatPhoto && _photo == _additionalChatPhoto && lastOverviewPhotoLoaded) {
				// Additional chat photo (not in the list => place it at the end of the list).
				index = count;
				++count;
			} else {
				count = 0; // unknown yet
			}
		}
	} else if (_user) {
		count = _user->photosCount ? _user->photosCount : _user->photos.size();
	}
	if (index >= 0 && index < count && count > 1) {
		if (_doc) {
			_headerText = lng_mediaview_file_n_of_count(lt_file, _doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name, lt_n, QString::number(index + 1), lt_count, QString::number(count));
		} else {
			_headerText = lng_mediaview_n_of_count(lt_n, QString::number(index + 1), lt_count, QString::number(count));
		}
	} else {
		if (_doc) {
			_headerText = _doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name;
		} else if (_user) {
			_headerText = lang(lng_mediaview_profile_photo);
		} else if ((_channel && !_history->isMegagroup()) || (_peer && _peer->isChannel() && !_peer->isMegagroup())) {
			_headerText = lang(lng_mediaview_channel_photo);
		} else if (_peer) {
			_headerText = lang(lng_mediaview_group_photo);
		} else {
			_headerText = lang(lng_mediaview_single_photo);
		}
	}
	_headerHasLink = _history && typeHasMediaOverview(_overview);
	int32 hwidth = st::mediaviewThickFont->width(_headerText);
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
