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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "settingswidget.h"
#include "mainwidget.h"
#include "application.h"
#include "boxes/photocropbox.h"
#include "boxes/connectionbox.h"
#include "boxes/addcontactbox.h"
#include "boxes/emojibox.h"
#include "boxes/confirmbox.h"
#include "boxes/downloadpathbox.h"
#include "boxes/usernamebox.h"
#include "boxes/languagebox.h"
#include "langloaderplain.h"
#include "gui/filedialog.h"

#include "localstorage.h"

Slider::Slider(QWidget *parent, const style::slider &st, int32 count, int32 sel) : QWidget(parent),
_count(count), _sel(snap(sel, 0, _count)), _wasSel(_sel), _st(st), _pressed(false) {
	resize(_st.width, _st.bar.pxHeight());
	setCursor(style::cur_pointer);
}

void Slider::mousePressEvent(QMouseEvent *e) {
	_pressed = true;
	mouseMoveEvent(e);
}

void Slider::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed) {
		int32 newSel = snap(qRound((_count - 1) * float64(e->pos().x() - _st.bar.pxWidth() / 2) / (width() - _st.bar.pxWidth())), 0, _count - 1);
		if (newSel != _sel) {
			_sel = newSel;
			update();
		}
	}
}

void Slider::mouseReleaseEvent(QMouseEvent *e) {
	_pressed = false;
	if (_sel != _wasSel) {
		emit changed(_wasSel);
		_wasSel = _sel;
	}
}

int32 Slider::selected() const {
	return _sel;
}

void Slider::setSelected(int32 sel) {
	if (_sel != sel) {
		_sel = sel;
		emit changed(_wasSel);
		_wasSel = _sel;
		update();
	}
}

void Slider::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(0, (height() - _st.thikness) / 2, width(), _st.thikness, _st.color->b);

	int32 x = qFloor(_sel * float64(width() - _st.bar.pxWidth()) / (_count - 1)), y = (height() - _st.bar.pxHeight()) / 2;
	p.drawPixmap(QPoint(x, y), App::sprite(), _st.bar);
}

QString scaleLabel(DBIScale scale) {
	switch (scale) {
	case dbisOne: return qsl("100%");
	case dbisOneAndQuarter: return qsl("125%");
	case dbisOneAndHalf: return qsl("150%");
	case dbisTwo: return qsl("200%");
	}
	return QString();
}

bool scaleIs(DBIScale scale) {
	return cRealScale() == scale || (cRealScale() == dbisAuto && cScreenScale() == scale);
}

SettingsInner::SettingsInner(SettingsWidget *parent) : QWidget(parent),
	_self(App::self()),

	// profile
	_nameCache(self() ? self()->name : QString()),
    _uploadPhoto(this, lang(lng_settings_upload), st::btnSetUpload),
    _cancelPhoto(this, lang(lng_cancel)), _nameOver(false), _photoOver(false), a_photo(0),

	// contact info
	_phoneText(self() ? App::formatPhone(self()->phone) : QString()),
	_usernameText((self() && !self()->username.isEmpty()) ? ('@' + self()->username) : QString()),
	_phoneLeft(st::linkFont->m.width(lang(lng_settings_phone_number)) + st::linkFont->spacew),
	_usernameLeft(st::linkFont->m.width(lang(lng_settings_username)) + st::linkFont->spacew),
	_chooseUsername(this, lang(lng_settings_choose_username)),
	_changeUsername(this, lang(lng_settings_change_username)),

	// notifications
	_desktopNotify(this, lang(lng_settings_desktop_notify), cDesktopNotify()),
	_senderName(this, lang(lng_settings_show_name), cNotifyView() <= dbinvShowName),
	_messagePreview(this, lang(lng_settings_show_preview), cNotifyView() <= dbinvShowPreview),
	_soundNotify(this, lang(lng_settings_sound_notify), cSoundNotify()),

	// general
	_changeLanguage(this, lang(lng_settings_change_lang)),
	_autoUpdate(this, lang(lng_settings_auto_update), cAutoUpdate()),
	_checkNow(this, lang(lng_settings_check_now)),
	_restartNow(this, lang(lng_settings_update_now)),

    _supportTray(cSupportTray()),
	_workmodeTray(this, lang(lng_settings_workmode_tray), (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray)),
	_workmodeWindow(this, lang(lng_settings_workmode_window), (cWorkMode() == dbiwmWindowOnly || cWorkMode() == dbiwmWindowAndTray)),

	_autoStart(this, lang(lng_settings_auto_start), cAutoStart()),
	_startMinimized(this, lang(lng_settings_start_min), cStartMinimized()),
	_sendToMenu(this, lang(lng_settings_add_sendto), cSendToMenu()),

	_dpiAutoScale(this, lng_settings_scale_auto(lt_cur, scaleLabel(cScreenScale())), (cConfigScale() == dbisAuto)),
	_dpiSlider(this, st::dpiSlider, dbisScaleCount - 1, cEvalScale(cConfigScale()) - 1),
	_dpiWidth1(st::dpiFont1->m.width(scaleLabel(dbisOne))),
	_dpiWidth2(st::dpiFont2->m.width(scaleLabel(dbisOneAndQuarter))),
	_dpiWidth3(st::dpiFont3->m.width(scaleLabel(dbisOneAndHalf))),
	_dpiWidth4(st::dpiFont4->m.width(scaleLabel(dbisTwo))),

	// chat options
	_replaceEmojis(this, lang(lng_settings_replace_emojis), cReplaceEmojis()),
	_viewEmojis(this, lang(lng_settings_view_emojis)),

	_enterSend(this, qsl("send_key"), 0, lang(lng_settings_send_enter), !cCtrlEnter()),
    _ctrlEnterSend(this, qsl("send_key"), 1, lang((cPlatform() == dbipMac) ? lng_settings_send_cmdenter : lng_settings_send_ctrlenter), cCtrlEnter()),

	_dontAskDownloadPath(this, lang(lng_download_path_dont_ask), !cAskDownloadPath()),
    _downloadPathWidth(st::linkFont->m.width(lang(lng_download_path_label)) + st::linkFont->spacew),
	_downloadPathEdit(this, cDownloadPath().isEmpty() ? lang(lng_download_path_default) : ((cDownloadPath() == qsl("tmp")) ? lang(lng_download_path_temp) : st::linkFont->m.elidedText(QDir::toNativeSeparators(cDownloadPath()), Qt::ElideRight, st::setWidth - st::setVersionLeft - _downloadPathWidth))),
	_downloadPathClear(this, lang(lng_download_path_clear)),
	_tempDirClearingWidth(st::linkFont->m.width(lang(lng_download_path_clearing))),
	_tempDirClearedWidth(st::linkFont->m.width(lang(lng_download_path_cleared))),
	_tempDirClearFailedWidth(st::linkFont->m.width(lang(lng_download_path_clear_failed))),

	_catsAndDogs(this, lang(lng_settings_cats_and_dogs), cCatsAndDogs()),

	// local storage
	_localStorageClear(this, lang(lng_local_storage_clear)),
	_localStorageHeight(1),
	_storageClearingWidth(st::linkFont->m.width(lang(lng_local_storage_clearing))),
	_storageClearedWidth(st::linkFont->m.width(lang(lng_local_storage_cleared))),
	_storageClearFailedWidth(st::linkFont->m.width(lang(lng_local_storage_clear_failed))),

	// advanced
	_connectionType(this, lng_connection_auto(lt_type, QString())),
	_resetSessions(this, lang(lng_settings_reset)),
	_logOut(this, lang(lng_settings_logout), st::btnLogout),
    _resetDone(false)
{
	if (self()) {
		_nameText.setText(st::setNameFont, _nameCache, _textNameOptions);
		PhotoData *selfPhoto = self()->photoId ? App::photo(self()->photoId) : 0;
		if (selfPhoto && selfPhoto->date) _photoLink = TextLinkPtr(new PhotoLink(selfPhoto, self()));
		MTP::send(MTPusers_GetFullUser(self()->inputUser), rpcDone(&SettingsInner::gotFullSelf));

		connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
		connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));
	}

	// profile
	connect(&_uploadPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhoto()));
	connect(&_cancelPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhotoCancel()));

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUpdateDone(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUpdateFail(PeerId)));

	// contact info
	connect(&_chooseUsername, SIGNAL(clicked()), this, SLOT(onUsername()));
	connect(&_changeUsername, SIGNAL(clicked()), this, SLOT(onUsername()));

	// notifications
	_senderName.setDisabled(!_desktopNotify.checked());
	_messagePreview.setDisabled(_senderName.disabled() || !_senderName.checked());
	connect(&_desktopNotify, SIGNAL(changed()), this, SLOT(onDesktopNotify()));
	connect(&_senderName, SIGNAL(changed()), this, SLOT(onSenderName()));
	connect(&_messagePreview, SIGNAL(changed()), this, SLOT(onMessagePreview()));
	connect(&_soundNotify, SIGNAL(changed()), this, SLOT(onSoundNotify()));

	// general
	connect(&_changeLanguage, SIGNAL(clicked()), this, SLOT(onChangeLanguage()));
	connect(&_autoUpdate, SIGNAL(changed()), this, SLOT(onAutoUpdate()));
	connect(&_checkNow, SIGNAL(clicked()), this, SLOT(onCheckNow()));
	connect(&_restartNow, SIGNAL(clicked()), this, SLOT(onRestartNow()));

	connect(&_workmodeTray, SIGNAL(changed()), this, SLOT(onWorkmodeTray()));
	connect(&_workmodeWindow, SIGNAL(changed()), this, SLOT(onWorkmodeWindow()));

	_startMinimized.setDisabled(!_autoStart.checked());
	connect(&_autoStart, SIGNAL(changed()), this, SLOT(onAutoStart()));
	connect(&_startMinimized, SIGNAL(changed()), this, SLOT(onStartMinimized()));
	connect(&_sendToMenu, SIGNAL(changed()), this, SLOT(onSendToMenu()));

	connect(&_dpiAutoScale, SIGNAL(changed()), this, SLOT(onScaleAuto()));
	connect(&_dpiSlider, SIGNAL(changed(int32)), this, SLOT(onScaleChange()));

	_curVersionText = lng_settings_current_version(lt_version, QString::fromWCharArray(AppVersionStr)) + ' ';
	_curVersionWidth = st::linkFont->m.width(_curVersionText);
	_newVersionText = lang(lng_settings_update_ready) + ' ';
	_newVersionWidth = st::linkFont->m.width(_newVersionText);

	connect(App::app(), SIGNAL(updateChecking()), this, SLOT(onUpdateChecking()));
	connect(App::app(), SIGNAL(updateLatest()), this, SLOT(onUpdateLatest()));
	connect(App::app(), SIGNAL(updateDownloading(qint64,qint64)), this, SLOT(onUpdateDownloading(qint64,qint64)));
	connect(App::app(), SIGNAL(updateReady()), this, SLOT(onUpdateReady()));
	connect(App::app(), SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));

	// chat options
	connect(&_replaceEmojis, SIGNAL(changed()), this, SLOT(onReplaceEmojis()));
	connect(&_viewEmojis, SIGNAL(clicked()), this, SLOT(onViewEmojis()));

	connect(&_enterSend, SIGNAL(changed()), this, SLOT(onEnterSend()));
	connect(&_ctrlEnterSend, SIGNAL(changed()), this, SLOT(onCtrlEnterSend()));

	connect(&_dontAskDownloadPath, SIGNAL(changed()), this, SLOT(onDontAskDownloadPath()));
	connect(&_downloadPathEdit, SIGNAL(clicked()), this, SLOT(onDownloadPathEdit()));
	connect(&_downloadPathClear, SIGNAL(clicked()), this, SLOT(onDownloadPathClear()));
	switch (App::wnd()->tempDirState()) {
	case Window::TempDirEmpty: _tempDirClearState = TempDirEmpty; break;
	case Window::TempDirExists: _tempDirClearState = TempDirExists; break;
	case Window::TempDirRemoving: _tempDirClearState = TempDirClearing; break;
	}
	connect(App::wnd(), SIGNAL(tempDirCleared(int)), this, SLOT(onTempDirCleared(int)));
	connect(App::wnd(), SIGNAL(tempDirClearFailed(int)), this, SLOT(onTempDirClearFailed(int)));

	connect(&_catsAndDogs, SIGNAL(changed()), this, SLOT(onCatsAndDogs()));

	// local storage
	connect(&_localStorageClear, SIGNAL(clicked()), this, SLOT(onLocalStorageClear()));
	switch (App::wnd()->localStorageState()) {
	case Window::TempDirEmpty: _storageClearState = TempDirEmpty; break;
	case Window::TempDirExists: _storageClearState = TempDirExists; break;
	case Window::TempDirRemoving: _storageClearState = TempDirClearing; break;
	}

	// advanced
	connect(&_connectionType, SIGNAL(clicked()), this, SLOT(onConnectionType()));
	connect(&_resetSessions, SIGNAL(clicked()), this, SLOT(onResetSessions()));
	connect(&_logOut, SIGNAL(clicked()), App::wnd(), SLOT(onLogout()));

	_connectionTypeText = lang(lng_connection_type) + ' ';
	_connectionTypeWidth = st::linkFont->m.width(_connectionTypeText);

    if (App::main()) {
        connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
    }

	updateOnlineDisplay();

	switch (App::app()->updatingState()) {
	case Application::UpdatingDownload:
		setUpdatingState(UpdatingDownload, true);
		setDownloadProgress(App::app()->updatingReady(), App::app()->updatingSize());
	break;
	case Application::UpdatingReady: setUpdatingState(UpdatingReady, true); break;
	default: setUpdatingState(UpdatingNone, true); break;
	}

	updateConnectionType();

	setMouseTracking(true);
}

void SettingsInner::peerUpdated(PeerData *data) {
	if (self() && data == self()) {
		if (self()->photoId) {
			PhotoData *selfPhoto = App::photo(self()->photoId);
			if (selfPhoto->date) {
				_photoLink = TextLinkPtr(new PhotoLink(selfPhoto, self()));
			} else {
				_photoLink = TextLinkPtr();
				MTP::send(MTPusers_GetFullUser(self()->inputUser), rpcDone(&SettingsInner::gotFullSelf));
			}
		} else {
			_photoLink = TextLinkPtr();
		}

		if (_nameCache != self()->name) {
			_nameCache = self()->name;
			_nameText.setText(st::setNameFont, _nameCache, _textNameOptions);
			update();
		}
	}
}

void SettingsInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setClipRect(e->rect());

	int32 top = 0;
	if (self()) {
		// profile
		top += st::setTop;

		_nameText.drawElided(p, _uploadPhoto.x() + st::setNameLeft, top + st::setNameTop, _uploadPhoto.width() - st::setNameLeft);
		if (!_cancelPhoto.isHidden()) {
			p.setFont(st::linkFont->f);
			p.setPen(st::black->p);
			p.drawText(_uploadPhoto.x() + st::setStatusLeft, _cancelPhoto.y() + st::linkFont->ascent, lang(lng_settings_uploading_photo));
		}

		if (_photoLink) {
			p.drawPixmap(_left, top, self()->photo->pix(st::setPhotoSize));
		} else {
			if (a_photo.current() < 1) {
				p.drawPixmap(QPoint(_left, top), App::sprite(), st::setPhotoImg);
			}
			if (a_photo.current() > 0) {
				p.setOpacity(a_photo.current());
				p.drawPixmap(QPoint(_left, top), App::sprite(), st::setOverPhotoImg);
				p.setOpacity(1);
			}
		}

		p.setFont(st::setStatusFont->f);
		bool connecting = App::wnd()->connectingVisible();
		p.setPen((connecting ? st::profileOfflineColor : st::profileOnlineColor)->p);
		p.drawText(_uploadPhoto.x() + st::setStatusLeft, top + st::setStatusTop + st::setStatusFont->ascent, lang(connecting ? lng_status_connecting : lng_status_online));

		top += st::setPhotoSize;

		if (!_errorText.isEmpty()) {
			p.setFont(st::setErrFont->f);
			p.setPen(st::setErrColor->p);
			p.drawText(QRect(_uploadPhoto.x(), _uploadPhoto.y() + _uploadPhoto.height() + st::setLittleSkip, _uploadPhoto.width(), st::setErrFont->height), _errorText, style::al_center);
		}

		// contact info
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_contact_info));
		top += st::setHeaderSkip;

		p.setFont(st::linkFont->f);
		p.setPen(st::black->p);
		p.drawText(_left, top + st::linkFont->ascent, lang(lng_settings_phone_number));
		p.drawText(_left + _phoneLeft, top + st::linkFont->ascent, _phoneText);
		top += st::linkFont->height + st::setLittleSkip;

		p.drawText(_left, top + st::linkFont->ascent, lang(lng_settings_username));
		if (!_usernameText.isEmpty()) {
			p.drawText(_left + _usernameLeft, top + st::linkFont->ascent, _usernameText);
		}
		top += st::linkFont->height;

		// notifications
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_notify));
		top += st::setHeaderSkip;

		top += _desktopNotify.height() + st::setLittleSkip;
		top += _senderName.height() + st::setLittleSkip;
		top += _messagePreview.height() + st::setSectionSkip;
		top += _soundNotify.height();
	}

	// general
	p.setFont(st::setHeaderFont->f);
	p.setPen(st::setHeaderColor->p);
	p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_general));
	top += st::setHeaderSkip;

	top += _autoUpdate.height();
	QString textToDraw;
	if (cAutoUpdate()) {
		switch (_updatingState) {
		case UpdatingNone: textToDraw = _curVersionText; break;
		case UpdatingCheck: textToDraw = lang(lng_settings_update_checking); break;
		case UpdatingLatest: textToDraw = lang(lng_settings_latest_installed); break;
		case UpdatingDownload: textToDraw = _newVersionDownload; break;
		case UpdatingReady: textToDraw = _newVersionText; break;
		case UpdatingFail: textToDraw = lang(lng_settings_update_fail); break;
		}
	} else {
		textToDraw = _curVersionText;
	}
	p.setFont(st::linkFont->f);
	p.setPen(st::setVersionColor->p);
	p.drawText(_left + st::setVersionLeft, top + st::setVersionTop + st::linkFont->ascent, textToDraw);
	top += st::setVersionHeight;

    if (cPlatform() == dbipWindows) {
        top += _workmodeTray.height() + st::setLittleSkip;
        top += _workmodeWindow.height() + st::setSectionSkip;

        top += _autoStart.height() + st::setLittleSkip;
        top += _startMinimized.height() + st::setSectionSkip;

		top += _sendToMenu.height();
    } else if (_supportTray) {
		top += _workmodeTray.height();
	}

    if (!cRetina()) {
        p.setFont(st::setHeaderFont->f);
        p.setPen(st::setHeaderColor->p);
        p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_scale_label));
        top += st::setHeaderSkip;
        top += _dpiAutoScale.height() + st::setLittleSkip;

        top += _dpiSlider.height() + st::dpiFont4->height;
        int32 sLeft = _dpiSlider.x() + _dpiWidth1 / 2, sWidth = _dpiSlider.width();
        float64 sStep = (sWidth - _dpiWidth1 / 2 - _dpiWidth4 / 2) / float64(dbisScaleCount - 2);
        p.setFont(st::dpiFont1->f);

        p.setPen((scaleIs(dbisOne) ? st::dpiActive : st::dpiInactive)->p);
        p.drawText(sLeft + qRound(0 * sStep) - _dpiWidth1 / 2, top - (st::dpiFont4->height - st::dpiFont1->height) / 2 - st::dpiFont1->descent, scaleLabel(dbisOne));
        p.setFont(st::dpiFont2->f);
        p.setPen((scaleIs(dbisOneAndQuarter) ? st::dpiActive : st::dpiInactive)->p);
        p.drawText(sLeft + qRound(1 * sStep) - _dpiWidth2 / 2, top - (st::dpiFont4->height - st::dpiFont2->height) / 2 - st::dpiFont2->descent, scaleLabel(dbisOneAndQuarter));
        p.setFont(st::dpiFont3->f);
        p.setPen((scaleIs(dbisOneAndHalf) ? st::dpiActive : st::dpiInactive)->p);
        p.drawText(sLeft + qRound(2 * sStep) - _dpiWidth3 / 2, top - (st::dpiFont4->height - st::dpiFont3->height) / 2 - st::dpiFont3->descent, scaleLabel(dbisOneAndHalf));
        p.setFont(st::dpiFont4->f);
        p.setPen((scaleIs(dbisTwo) ? st::dpiActive : st::dpiInactive)->p);
        p.drawText(sLeft + qRound(3 * sStep) - _dpiWidth4 / 2, top - (st::dpiFont4->height - st::dpiFont4->height) / 2 - st::dpiFont4->descent, scaleLabel(dbisTwo));
        p.setFont(st::linkFont->f);
    }

	if (self()) {
		// chat options
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_chat));
		top += st::setHeaderSkip;

		top += _replaceEmojis.height() + st::setSectionSkip;
		top += _enterSend.height() + st::setLittleSkip;
		top += _ctrlEnterSend.height() + st::setSectionSkip;

		top += _dontAskDownloadPath.height();
		if (!cAskDownloadPath()) {
			top += st::setLittleSkip;
			p.setFont(st::linkFont->f);
			p.setPen(st::black->p);
			p.drawText(_left + st::setVersionLeft, top + st::linkFont->ascent, lang(lng_download_path_label));
			if (cDownloadPath() == qsl("tmp")) {
				QString clearText;
				int32 clearWidth = 0;
				switch (_tempDirClearState) {
				case TempDirClearing: clearText = lang(lng_download_path_clearing); clearWidth = _tempDirClearingWidth; break;
				case TempDirCleared: clearText = lang(lng_download_path_cleared); clearWidth = _tempDirClearedWidth; break;
				case TempDirClearFailed: clearText = lang(lng_download_path_clear_failed); clearWidth = _tempDirClearFailedWidth; break;
				}
				if (clearWidth) {
					p.drawText(_left + st::setWidth - clearWidth, top + st::linkFont->ascent, clearText);
				}
			}
			top += _downloadPathEdit.height();
		}
		top += st::setSectionSkip;

		top += _catsAndDogs.height();

		// local storage
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_cache));

		p.setFont(st::linkFont->f);
		p.setPen(st::black->p);
		QString clearText;
		int32 clearWidth = 0;
		switch (_storageClearState) {
		case TempDirClearing: clearText = lang(lng_local_storage_clearing); clearWidth = _storageClearingWidth; break;
		case TempDirCleared: clearText = lang(lng_local_storage_cleared); clearWidth = _storageClearedWidth; break;
		case TempDirClearFailed: clearText = lang(lng_local_storage_clear_failed); clearWidth = _storageClearFailedWidth; break;
		}
		if (clearWidth) {
			p.drawText(_left + st::setWidth - clearWidth, top + st::setHeaderTop + st::setHeaderFont->ascent, clearText);
		}

		top += st::setHeaderSkip;

		int32 cntImages = Local::hasImages() + Local::hasStickers(), cntAudios = Local::hasAudios();
		if (cntImages > 0 && cntAudios > 0) {
			if (_localStorageHeight != 2) {
				cntAudios = 0;
				QTimer::singleShot(0, this, SLOT(onUpdateLocalStorage()));
			}
		} else {
			if (_localStorageHeight != 1) {
				QTimer::singleShot(0, this, SLOT(onUpdateLocalStorage()));
			}
		}
		if (cntImages > 0) {
			QString cnt = lng_settings_images_cached(lt_count, cntImages, lt_size, formatSizeText(Local::storageImagesSize() + Local::storageStickersSize()));
			p.drawText(_left + st::setHeaderLeft, top + st::linkFont->ascent, cnt);
		}
		if (_localStorageHeight == 2) top += _localStorageClear.height() + st::setLittleSkip;
		if (cntAudios > 0) {
			QString cnt = lng_settings_audios_cached(lt_count, cntAudios, lt_size, formatSizeText(Local::storageAudiosSize()));
			p.drawText(_left + st::setHeaderLeft, top + st::linkFont->ascent, cnt);
		} else if (cntImages <= 0) {
			p.drawText(_left + st::setHeaderLeft, top + st::linkFont->ascent, lang(lng_settings_no_data_cached));
		}
		top += _localStorageClear.height();
	}

	// advanced
	p.setFont(st::setHeaderFont->f);
	p.setPen(st::setHeaderColor->p);
	p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_advanced));
	top += st::setHeaderSkip;

	p.setFont(st::linkFont->f);
	p.setPen(st::black->p);
	p.drawText(_left + st::setHeaderLeft, _connectionType.y() + st::linkFont->ascent, _connectionTypeText);

	if (self() && _resetDone) {
		p.drawText(_resetSessions.x(), _resetSessions.y() + st::linkFont->ascent, lang(lng_settings_reset_done));
	}
}

void SettingsInner::resizeEvent(QResizeEvent *e) {
	_left = (width() - st::setWidth) / 2;

	int32 top = 0;

	if (self()) {
		// profile
		top += st::setTop;
		top += st::setPhotoSize;
		_uploadPhoto.move(_left + st::setWidth - _uploadPhoto.width(), top - _uploadPhoto.height());
		_cancelPhoto.move(_left + st::setWidth - _cancelPhoto.width(), top - _uploadPhoto.height() + st::btnSetUpload.textTop + st::btnSetUpload.font->ascent - st::linkFont->ascent);

		// contact info
		top += st::setHeaderSkip;
		top += st::linkFont->height + st::setLittleSkip;
		_chooseUsername.move(_left + _usernameLeft, top);
		_changeUsername.move(_left + st::setWidth - _changeUsername.width(), top); top += st::linkFont->height;

		// notifications
		top += st::setHeaderSkip;
		_desktopNotify.move(_left, top); top += _desktopNotify.height() + st::setLittleSkip;
		_senderName.move(_left, top); top += _senderName.height() + st::setLittleSkip;
		_messagePreview.move(_left, top); top += _messagePreview.height() + st::setSectionSkip;
		_soundNotify.move(_left, top); top += _soundNotify.height();
	}

	// general
	top += st::setHeaderSkip;
	_changeLanguage.move(_left + st::setWidth - _changeLanguage.width(), top - st::setHeaderSkip + st::setHeaderTop + st::setHeaderFont->ascent - st::linkFont->ascent);
	_autoUpdate.move(_left, top);
	_checkNow.move(_left + st::setWidth - _checkNow.width(), top + st::cbDefFlat.textTop); top += _autoUpdate.height();
	_restartNow.move(_left + st::setWidth - _restartNow.width(), top + st::setVersionTop);
	top += st::setVersionHeight;

    if (cPlatform() == dbipWindows) {
        _workmodeTray.move(_left, top); top += _workmodeTray.height() + st::setLittleSkip;
        _workmodeWindow.move(_left, top); top += _workmodeWindow.height() + st::setSectionSkip;

        _autoStart.move(_left, top); top += _autoStart.height() + st::setLittleSkip;
        _startMinimized.move(_left, top); top += _startMinimized.height() + st::setSectionSkip;

		_sendToMenu.move(_left, top); top += _sendToMenu.height();
    } else if (_supportTray) {
		_workmodeTray.move(_left, top); top += _workmodeTray.height();
	}
    if (!cRetina()) {
        top += st::setHeaderSkip;
        _dpiAutoScale.move(_left, top); top += _dpiAutoScale.height() + st::setLittleSkip;
        _dpiSlider.move(_left, top); top += _dpiSlider.height() + st::dpiFont4->height;
    }

	// chat options
	if (self()) {
		top += st::setHeaderSkip;
		_viewEmojis.move(_left + st::setWidth - _viewEmojis.width(), top + st::cbDefFlat.textTop);
		_replaceEmojis.move(_left, top); top += _replaceEmojis.height() + st::setSectionSkip;
		_enterSend.move(_left, top); top += _enterSend.height() + st::setLittleSkip;
		_ctrlEnterSend.move(_left, top); top += _ctrlEnterSend.height() + st::setSectionSkip;
		_dontAskDownloadPath.move(_left, top); top += _dontAskDownloadPath.height();
		if (!cAskDownloadPath()) {
			top += st::setLittleSkip;
			_downloadPathEdit.move(_left + st::setVersionLeft + _downloadPathWidth, top);
			if (cDownloadPath() == qsl("tmp")) {
				_downloadPathClear.move(_left + st::setWidth - _downloadPathClear.width(), top);
			}
			top += _downloadPathEdit.height();
		}
		top += st::setSectionSkip;
		_catsAndDogs.move(_left, top); top += _catsAndDogs.height();

		// local storage
		_localStorageClear.move(_left + st::setWidth - _localStorageClear.width(), top + st::setHeaderTop + st::setHeaderFont->ascent - st::linkFont->ascent);
		top += st::setHeaderSkip;
		if ((Local::hasImages() || Local::hasStickers()) && Local::hasAudios()) {
			_localStorageHeight = 2;
			top += _localStorageClear.height() + st::setLittleSkip;
		} else {
			_localStorageHeight = 1;
		}
		top += _localStorageClear.height();
	}

	// advanced
	top += st::setHeaderSkip;
	_connectionType.move(_left + st::setHeaderLeft + _connectionTypeWidth, top); top += _connectionType.height() + st::setLittleSkip;
	if (self()) {
		_resetSessions.move(_left, top); top += _resetSessions.height() + st::setSectionSkip;
		_logOut.move(_left, top);
	}
}

void SettingsInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		App::wnd()->showSettings();
	}
}

void SettingsInner::mouseMoveEvent(QMouseEvent *e) {
	if (!self()) {
		setCursor(style::cur_default);
	} else {
		bool nameOver = QRect(_uploadPhoto.x() + st::setNameLeft, st::setTop + st::setNameTop, qMin(_uploadPhoto.width() - int(st::setNameLeft), _nameText.maxWidth()), st::setNameFont->height).contains(e->pos());
		if (nameOver != _nameOver) {
			_nameOver = nameOver;
		}

		bool photoOver = QRect(_left, st::setTop, st::setPhotoSize, st::setPhotoSize).contains(e->pos());
		if (photoOver != _photoOver) {
			_photoOver = photoOver;
			if (!_photoLink) {
				a_photo.start(_photoOver ? 1 : 0);
				anim::start(this);
			}
		}

		setCursor((_nameOver || _photoOver) ? style::cur_pointer : style::cur_default);
	}
}

void SettingsInner::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (!self()) {
		return;
	}
	if (QRect(_uploadPhoto.x() + st::setNameLeft, st::setTop + st::setNameTop, qMin(_uploadPhoto.width() - int(st::setNameLeft), _nameText.maxWidth()), st::setNameFont->height).contains(e->pos())) {
		App::wnd()->showLayer(new AddContactBox(self()));
	} else if (QRect(_left, st::setTop, st::setPhotoSize, st::setPhotoSize).contains(e->pos())) {
		if (_photoLink) {
			App::photo(self()->photoId)->full->load();
			_photoLink->onClick(e->button());
		} else {
			onUpdatePhoto();
		}
	}
}

void SettingsInner::contextMenuEvent(QContextMenuEvent *e) {
}

bool SettingsInner::animStep(float64 ms) {
	float64 dt = ms / st::setPhotoDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_photo.finish();
	} else {
		a_photo.update(dt, anim::linear);
	}
	update(_left, st::setTop, st::setPhotoSize, st::setPhotoSize);
	return res;
}

void SettingsInner::updateSize(int32 newWidth) {
	if (_logOut.isHidden()) {
		resize(newWidth, _connectionType.geometry().bottom() + st::setBottom);
	} else {
		resize(newWidth, _logOut.geometry().bottom() + st::setBottom);
	}
}

void SettingsInner::updateOnlineDisplay() {
}

void SettingsInner::updateConnectionType() {
	switch (cConnectionType()) {
	case dbictAuto: {
		QString transport = MTP::dctransport();
		if (transport.isEmpty()) {
			_connectionType.setText(lang(lng_connection_auto_connecting));
		} else {
			_connectionType.setText(lng_connection_auto(lt_type, transport));
		}
	} break;
	case dbictHttpProxy: _connectionType.setText(lang(lng_connection_http_proxy)); break;
	case dbictTcpProxy: _connectionType.setText(lang(lng_connection_tcp_proxy)); break;
	}
}

void SettingsInner::gotFullSelf(const MTPUserFull &selfFull) {
	if (!self()) return;
	App::feedPhoto(selfFull.c_userFull().vprofile_photo);
	App::feedUsers(MTP_vector<MTPUser>(1, selfFull.c_userFull().vuser));
	PhotoData *selfPhoto = self()->photoId ? App::photo(self()->photoId) : 0;
	if (selfPhoto && selfPhoto->date) {
		_photoLink = TextLinkPtr(new PhotoLink(selfPhoto, self()));
	} else {
		_photoLink = TextLinkPtr();
	}
}

void SettingsInner::usernameChanged() {
	_usernameText = (self() && !self()->username.isEmpty()) ? ('@' + self()->username) : QString();
	showAll();
	update();
}

void SettingsInner::showAll() {
	// profile
	if (self()) {
		if (App::app()->isPhotoUpdating(self()->id)) {
			_cancelPhoto.show();
			_uploadPhoto.hide();
		} else {
			_cancelPhoto.hide();
			_uploadPhoto.show();
		}
	} else {
		_uploadPhoto.hide();
		_cancelPhoto.hide();
	}

	// contact info
	if (self()) {
		if (self()->username.isEmpty()) {
			_chooseUsername.show();
			_changeUsername.hide();
		} else {
			_chooseUsername.hide();
			_changeUsername.show();
		}
	} else {
		_chooseUsername.hide();
		_changeUsername.hide();
	}

	// notifications
	if (self()) {
		_desktopNotify.show();
		_senderName.show();
		_messagePreview.show();
		_soundNotify.show();
	} else {
		_desktopNotify.hide();
		_senderName.hide();
		_messagePreview.hide();
		_soundNotify.hide();
	}

	// general
	_changeLanguage.show();
	_autoUpdate.show();
	setUpdatingState(_updatingState, true);
    if (cPlatform() == dbipWindows) {
        _workmodeTray.show();
        _workmodeWindow.show();

        _autoStart.show();
        _startMinimized.show();

		_sendToMenu.show();
    } else {
        if (_supportTray) {
			_workmodeTray.show();
		} else {
			_workmodeTray.hide();
		}
        _workmodeWindow.hide();

        _autoStart.hide();
        _startMinimized.hide();

		_sendToMenu.hide();
	}
    if (cRetina()) {
        _dpiSlider.hide();
        _dpiAutoScale.hide();
    } else {
        _dpiSlider.show();
        _dpiAutoScale.show();
    }

	// chat options
	if (self()) {
		_replaceEmojis.show();
		if (cReplaceEmojis()) {
			_viewEmojis.show();
		} else {
			_viewEmojis.hide();
		}
		_enterSend.show();
		_ctrlEnterSend.show();
		_catsAndDogs.show();
		_dontAskDownloadPath.show();
		if (cAskDownloadPath()) {
			_downloadPathEdit.hide();
			_downloadPathClear.hide();
		} else {
			_downloadPathEdit.show();
			if (cDownloadPath() == qsl("tmp") && _tempDirClearState == TempDirExists) { // dir exists, not clearing right now
				_downloadPathClear.show();
			} else {
				_downloadPathClear.hide();
			}
		}

	} else {
		_replaceEmojis.hide();
		_viewEmojis.hide();
		_enterSend.hide();
		_ctrlEnterSend.hide();
		_catsAndDogs.hide();
		_dontAskDownloadPath.hide();
		_downloadPathEdit.hide();
		_downloadPathClear.hide();
	}

	// local storage
	if (self() && _storageClearState == TempDirExists) {
		_localStorageClear.show();
	} else {
		_localStorageClear.hide();
	}

	// advanced
	if (self()) {
		if (_resetDone) {
			_resetSessions.hide();
		} else {
			_resetSessions.show();
		}
		_logOut.show();
	} else {
		_resetSessions.hide();
		_logOut.hide();
	}
}

void SettingsInner::saveError(const QString &str) {
	_errorText = str;
	resizeEvent(0);
	update();
}

void SettingsInner::onUpdatePhotoCancel() {
	if (self()) {
		App::app()->cancelPhotoUpdate(self()->id);
	}
	showAll();
	update();
}

void SettingsInner::onUpdatePhoto() {
	saveError();

	QStringList imgExtensions(cImgExtensions());
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	QImage img;
	QString file;
	QByteArray remoteContent;
	if (filedialogGetOpenFile(file, remoteContent, lang(lng_choose_images), filter)) {
		if (!remoteContent.isEmpty()) {
			img = App::readImage(remoteContent);
		} else {
			if (!file.isEmpty()) {
				img = App::readImage(file);
			}
		}
	} else {
		return;
	}

	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		saveError(lang(lng_bad_photo));
		return;
	}
	PhotoCropBox *box = new PhotoCropBox(img, self()->id);
	connect(box, SIGNAL(closed()), this, SLOT(onPhotoUpdateStart()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onResetSessions() {
	ConfirmBox *box = new ConfirmBox(lang(lng_settings_reset_sure), lang(lng_settings_reset_button));
	connect(box, SIGNAL(confirmed()), this, SLOT(onResetSessionsSure()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onResetSessionsSure() {
	App::wnd()->layerHidden();
	MTP::send(MTPauth_ResetAuthorizations(), rpcDone(&SettingsInner::doneResetSessions));
}

void SettingsInner::doneResetSessions(const MTPBool &res) {
	if (res.v) {
		_resetDone = true;
		showAll();
		update();
	}
}

void SettingsInner::onChangeLanguage() {
	if ((_changeLanguage.clickModifiers() & Qt::ShiftModifier) && (_changeLanguage.clickModifiers() & Qt::AltModifier)) {
		QString file;
		QByteArray arr;
		if (filedialogGetOpenFile(file, arr, qsl("Choose language .strings file"), qsl("Language files (*.strings)"))) {
			_testlang = QFileInfo(file).absoluteFilePath();
			LangLoaderPlain loader(_testlang, LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_continue));
			if (loader.errors().isEmpty()) {
				LangLoaderResult result = loader.found();
				QString text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
					save = result.value(lng_continue, langOriginal(lng_continue)),
					cancel = result.value(lng_cancel, langOriginal(lng_cancel));
				ConfirmBox *box = new ConfirmBox(text, save, cancel);
				connect(box, SIGNAL(confirmed()), this, SLOT(onSaveTestLang()));
				App::wnd()->showLayer(box);
			} else {
				App::wnd()->showLayer(new ConfirmBox("Custom lang failed :(\n\nError: " + loader.errors(), true, lang(lng_close)));
			}
		}
	} else {
		App::wnd()->showLayer(new LanguageBox());
	}
}

void SettingsInner::onSaveTestLang() {
	cSetLangFile(_testlang);
	cSetLang(languageTest);
	App::writeConfig();
	cSetRestarting(true);
	App::quit();
}

void SettingsInner::onUpdateLocalStorage() {
	resizeEvent(0);
	updateSize(width());
	update();
}

void SettingsInner::onAutoUpdate() {
	cSetAutoUpdate(!cAutoUpdate());
	App::writeConfig();
	resizeEvent(0);
	if (cAutoUpdate()) {
		App::app()->startUpdateCheck();
		if (_updatingState == UpdatingNone) {
			_checkNow.show();
		} else if (_updatingState == UpdatingReady) {
			_restartNow.show();
		}
	} else {
		App::app()->stopUpdate();
		_restartNow.hide();
		_checkNow.hide();
	}
	update();
}

void SettingsInner::onCheckNow() {
	if (!cAutoUpdate()) return;

	cSetLastUpdateCheck(0);
	App::app()->startUpdateCheck();
}

void SettingsInner::onRestartNow() {
	psCheckReadyUpdate();
	if (_updatingState == UpdatingReady) {
		cSetRestartingUpdate(true);
	} else {
		cSetRestarting(true);
		cSetRestartingToSettings(true);
	}
	App::quit();
}

void SettingsInner::onConnectionType() {
	ConnectionBox *box = new ConnectionBox();
	connect(box, SIGNAL(closed()), this, SLOT(updateConnectionType()), Qt::QueuedConnection);
	App::wnd()->showLayer(box);
}

void SettingsInner::onUsername() {
	UsernameBox *box = new UsernameBox();
	connect(box, SIGNAL(closed()), this, SLOT(usernameChanged()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onWorkmodeTray() {
	if ((!_workmodeTray.checked() || cPlatform() != dbipWindows) && !_workmodeWindow.checked()) {
		_workmodeWindow.setChecked(true);
	}
	DBIWorkMode newMode = (_workmodeTray.checked() && _workmodeWindow.checked()) ? dbiwmWindowAndTray : (_workmodeTray.checked() ? dbiwmTrayOnly : dbiwmWindowOnly);
	if (cWorkMode() != newMode && (newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)) {
		cSetSeenTrayTooltip(false);
	}
	cSetWorkMode(newMode);
	App::wnd()->psUpdateWorkmode();
	App::writeConfig();
}

void SettingsInner::onWorkmodeWindow() {
	if (!_workmodeTray.checked() && !_workmodeWindow.checked()) {
		_workmodeTray.setChecked(true);
	}
	DBIWorkMode newMode = (_workmodeTray.checked() && _workmodeWindow.checked()) ? dbiwmWindowAndTray : (_workmodeTray.checked() ? dbiwmTrayOnly : dbiwmWindowOnly);
	if (cWorkMode() != newMode && (newMode == dbiwmWindowAndTray || newMode == dbiwmTrayOnly)) {
		cSetSeenTrayTooltip(false);
	}
	cSetWorkMode(newMode);
	App::wnd()->psUpdateWorkmode();
	App::writeConfig();
}

void SettingsInner::onAutoStart() {
	_startMinimized.setDisabled(!_autoStart.checked());
	cSetAutoStart(_autoStart.checked());
	if (!_autoStart.checked() && _startMinimized.checked()) {
		psAutoStart(false);
		_startMinimized.setChecked(false);
	} else {
		psAutoStart(_autoStart.checked());
		App::writeConfig();
	}
}

void SettingsInner::onStartMinimized() {
	cSetStartMinimized(_startMinimized.checked());
	App::writeConfig();
}

void SettingsInner::onSendToMenu() {
	cSetSendToMenu(_sendToMenu.checked());
	psSendToMenu(_sendToMenu.checked());
	App::writeConfig();
}

void SettingsInner::onScaleAuto() {
	DBIScale newScale = _dpiAutoScale.checked() ? dbisAuto : cEvalScale(cConfigScale());
	if (newScale == cScreenScale()) {
		if (newScale != cScale()) {
			newScale = cScale();
		} else {
			switch (newScale) {
			case dbisOne: newScale = dbisOneAndQuarter; break;
			case dbisOneAndQuarter: newScale = dbisOne; break;
			case dbisOneAndHalf: newScale = dbisOneAndQuarter; break;
			case dbisTwo: newScale = dbisOneAndHalf; break;
			}
		}
	}
	setScale(newScale);
}

void SettingsInner::onScaleChange() {
	DBIScale newScale = dbisAuto;
	switch (_dpiSlider.selected()) {
	case 0: newScale = dbisOne; break;
	case 1: newScale = dbisOneAndQuarter; break;
	case 2: newScale = dbisOneAndHalf; break;
	case 3: newScale = dbisTwo; break;
	}
	if (newScale == cScreenScale()) {
		newScale = dbisAuto;
	}
	setScale(newScale);
}

void SettingsInner::setScale(DBIScale newScale) {
	if (cConfigScale() == newScale) return;

	cSetConfigScale(newScale);
	App::writeConfig();
	App::wnd()->getTitle()->showUpdateBtn();
	if (newScale == dbisAuto && !_dpiAutoScale.checked()) {
		_dpiAutoScale.setChecked(true);
	} else if (newScale != dbisAuto && _dpiAutoScale.checked()) {
		_dpiAutoScale.setChecked(false);
	}
	if (newScale == dbisAuto) newScale = cScreenScale();
	if (_dpiSlider.selected() != newScale - 1) {
		_dpiSlider.setSelected(newScale - 1);
	}
	if (cEvalScale(cConfigScale()) != cEvalScale(cRealScale())) {
		ConfirmBox *box = new ConfirmBox(lang(lng_settings_need_restart), lang(lng_settings_restart_now), lang(lng_settings_restart_later));
		connect(box, SIGNAL(confirmed()), this, SLOT(onRestartNow()));
		App::wnd()->showLayer(box);
	}
}

void SettingsInner::onSoundNotify() {
	cSetSoundNotify(_soundNotify.checked());
	App::writeUserConfig();
}

void SettingsInner::onDesktopNotify() {
	cSetDesktopNotify(_desktopNotify.checked());
	if (!_desktopNotify.checked()) {
		App::wnd()->notifyClear();
		_senderName.setDisabled(true);
		_messagePreview.setDisabled(true);
		App::writeUserConfig();
	} else {
		_senderName.setDisabled(false);
		_messagePreview.setDisabled(!_senderName.checked());
		App::writeUserConfig();
	}
}

void SettingsInner::onSenderName() {
	_messagePreview.setDisabled(!_senderName.checked());
	if (!_senderName.checked() && _messagePreview.checked()) {
		_messagePreview.setChecked(false);
	} else {
		if (_messagePreview.checked()) {
			cSetNotifyView(dbinvShowPreview);
		} else if (_senderName.checked()) {
			cSetNotifyView(dbinvShowName);
		} else {
			cSetNotifyView(dbinvShowNothing);
		}
		App::writeUserConfig();
		App::wnd()->notifyUpdateAll();
	}
}

void SettingsInner::onMessagePreview() {
	if (_messagePreview.checked()) {
		cSetNotifyView(dbinvShowPreview);
	} else if (_senderName.checked()) {
		cSetNotifyView(dbinvShowName);
	} else {
		cSetNotifyView(dbinvShowNothing);
	}
	App::writeUserConfig();
	App::wnd()->notifyUpdateAll();
}

void SettingsInner::onReplaceEmojis() {
	cSetReplaceEmojis(_replaceEmojis.checked());
	App::writeUserConfig();

	if (_replaceEmojis.checked()) {
		_viewEmojis.show();
	} else {
		_viewEmojis.hide();
	}
}

void SettingsInner::onViewEmojis() {
	App::wnd()->showLayer(new EmojiBox());
}

void SettingsInner::onEnterSend() {
	if (_enterSend.checked()) {
		cSetCtrlEnter(false);
		App::writeUserConfig();
	}
}

void SettingsInner::onCtrlEnterSend() {
	if (_ctrlEnterSend.checked()) {
		cSetCtrlEnter(true);
		App::writeUserConfig();
	}
}

void SettingsInner::onCatsAndDogs() {
	cSetCatsAndDogs(_catsAndDogs.checked());
	App::writeUserConfig();
}

void SettingsInner::onDontAskDownloadPath() {
	cSetAskDownloadPath(!_dontAskDownloadPath.checked());
	App::writeUserConfig();

	showAll();
	resizeEvent(0);
	update();
}

void SettingsInner::onDownloadPathEdit() {
	DownloadPathBox *box = new DownloadPathBox();
	connect(box, SIGNAL(closed()), this, SLOT(onDownloadPathEdited()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onDownloadPathEdited() {
	QString path;
	if (cDownloadPath().isEmpty()) {
		path = lang(lng_download_path_default);
	} else if (cDownloadPath() == qsl("tmp")) {
		path = lang(lng_download_path_temp);
	} else {
		path = st::linkFont->m.elidedText(QDir::toNativeSeparators(cDownloadPath()), Qt::ElideRight, st::setWidth - st::setVersionLeft - _downloadPathWidth);
	}
	_downloadPathEdit.setText(path);
	showAll();
}

void SettingsInner::onDownloadPathClear() {
	ConfirmBox *box = new ConfirmBox(lang(lng_sure_clear_downloads));
	connect(box, SIGNAL(confirmed()), this, SLOT(onDownloadPathClearSure()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onDownloadPathClearSure() {
	App::wnd()->hideLayer();
	App::wnd()->tempDirDelete(Local::ClearManagerDownloads);
	_tempDirClearState = TempDirClearing;
	showAll();
	update();
}

void SettingsInner::onLocalStorageClear() {
	App::wnd()->tempDirDelete(Local::ClearManagerStorage);
	_storageClearState = TempDirClearing;
	showAll();
	update();
}

void SettingsInner::onTempDirCleared(int task) {
	if (task & Local::ClearManagerDownloads) {
		_tempDirClearState = TempDirCleared;
	} else if (task & Local::ClearManagerStorage) {
		_storageClearState = TempDirCleared;
	}
	showAll();
	update();
}

void SettingsInner::onTempDirClearFailed(int task) {
	if (task & Local::ClearManagerDownloads) {
		_tempDirClearState = TempDirClearFailed;
	} else if (task & Local::ClearManagerStorage) {
		_storageClearState = TempDirClearFailed;
	}
	showAll();
	update();
}

void SettingsInner::setUpdatingState(UpdatingState state, bool force) {
	if (_updatingState != state || force) {
		_updatingState = state;
		if (cAutoUpdate()) {
			switch (state) {
			case UpdatingNone:
			case UpdatingLatest: _checkNow.show(); _restartNow.hide(); break;
			case UpdatingReady: _checkNow.hide(); _restartNow.show(); break;
			case UpdatingCheck:
			case UpdatingDownload:
			case UpdatingFail: _checkNow.hide(); _restartNow.hide(); break;
			}
			update(0, _restartNow.y() - 10, width(), _restartNow.height() + 20);
		} else {
			_checkNow.hide();
			_restartNow.hide();
		}
	}
}

void SettingsInner::setDownloadProgress(qint64 ready, qint64 total) {
	qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
	QString readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
	QString totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
	QString res = lng_settings_downloading(lt_ready, readyStr, lt_total, totalStr);
	if (_newVersionDownload != res) {
		_newVersionDownload = res;
		if (cAutoUpdate()) {
			update(0, _restartNow.y() - 10, width(), _restartNow.height() + 20);
		}
	}
}

void SettingsInner::onUpdateChecking() {
	setUpdatingState(UpdatingCheck);
}

void SettingsInner::onUpdateLatest() {
	setUpdatingState(UpdatingLatest);
}

void SettingsInner::onUpdateDownloading(qint64 ready, qint64 total) {
	setUpdatingState(UpdatingDownload);
	setDownloadProgress(ready, total);
}

void SettingsInner::onUpdateReady() {
	setUpdatingState(UpdatingReady);
}

void SettingsInner::onUpdateFailed() {
	setUpdatingState(UpdatingFail);
}

void SettingsInner::onPhotoUpdateStart() {
	showAll();
	update();
}

void SettingsInner::onPhotoUpdateFail(PeerId peer) {
	if (!self() || self()->id != peer) return;
	saveError(lang(lng_bad_photo));
	showAll();
	update();
}

void SettingsInner::onPhotoUpdateDone(PeerId peer) {
	if (!self() || self()->id != peer) return;
	showAll();
	update();
}

SettingsWidget::SettingsWidget(Window *parent) : QWidget(parent),
	_scroll(this, st::setScroll), _inner(this), _close(this, st::setClose) {
	_scroll.setWidget(&_inner);

	connect(App::wnd(), SIGNAL(resized(const QSize &)), this, SLOT(onParentResize(const QSize &)));
	connect(&_close, SIGNAL(clicked()), App::wnd(), SLOT(showSettings()));

	setGeometry(QRect(0, st::titleHeight, Application::wnd()->width(), Application::wnd()->height() - st::titleHeight));

	showAll();
}

void SettingsWidget::onParentResize(const QSize &newSize) {
	resize(newSize);
}

void SettingsWidget::animShow(const QPixmap &bgAnimCache, bool back) {
	_bgAnimCache = bgAnimCache;

	anim::stop(this);
	showAll();
	_animCache = myGrab(this, rect());

	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);

	hideAll();
	anim::start(this);
	show();
}

bool SettingsWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();

		_animCache = _bgAnimCache = QPixmap();

		showAll();
		_inner.setFocus();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	return res;
}

void SettingsWidget::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	bool trivial = (rect() == r);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(r);
	}
	if (animating()) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
	} else {
		p.fillRect(rect(), st::setBG->b);
	}
}

void SettingsWidget::showAll() {
	_scroll.show();
	_inner.show();
	_inner.showAll();
	if (cWideMode()) {
		_close.show();
	} else {
		_close.hide();
	}
}

void SettingsWidget::hideAll() {
	_scroll.hide();
	_close.hide();
}

void SettingsWidget::resizeEvent(QResizeEvent *e) {
	_scroll.resize(size());
	_inner.updateSize(width());
	_close.move(st::setClosePos.x(), st::setClosePos.y());
}

void SettingsWidget::dragEnterEvent(QDragEnterEvent *e) {

}

void SettingsWidget::dropEvent(QDropEvent *e) {
}

void SettingsWidget::updateWideMode() {
	if (cWideMode()) {
		_close.show();
	} else {
		_close.hide();
	}
}

void SettingsWidget::updateOnlineDisplay() {
	_inner.updateOnlineDisplay();
}

void SettingsWidget::updateConnectionType() {
	_inner.updateConnectionType();
}

void SettingsWidget::rpcInvalidate() {
	_inner.rpcInvalidate();
}

void SettingsWidget::usernameChanged() {
	_inner.usernameChanged();
}

SettingsWidget::~SettingsWidget() {
	if (App::wnd()) App::wnd()->noSettings(this);
}
