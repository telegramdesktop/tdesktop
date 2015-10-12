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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "boxes/aboutbox.h"
#include "settingswidget.h"
#include "mainwidget.h"
#include "application.h"
#include "boxes/photocropbox.h"
#include "boxes/connectionbox.h"
#include "boxes/backgroundbox.h"
#include "boxes/addcontactbox.h"
#include "boxes/emojibox.h"
#include "boxes/confirmbox.h"
#include "boxes/downloadpathbox.h"
#include "boxes/usernamebox.h"
#include "boxes/languagebox.h"
#include "boxes/passcodebox.h"
#include "boxes/autolockbox.h"
#include "boxes/sessionsbox.h"
#include "langloaderplain.h"
#include "gui/filedialog.h"

#include "autoupdater.h"

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
	_chooseUsername(this, (self() && !self()->username.isEmpty()) ? ('@' + self()->username) : lang(lng_settings_choose_username)),

	// notifications
	_desktopNotify(this, lang(lng_settings_desktop_notify), cDesktopNotify()),
	_senderName(this, lang(lng_settings_show_name), cNotifyView() <= dbinvShowName),
	_messagePreview(this, lang(lng_settings_show_preview), cNotifyView() <= dbinvShowPreview),
	_windowsNotifications(this, lang(lng_settings_use_windows), cWindowsNotifications()),
	_soundNotify(this, lang(lng_settings_sound_notify), cSoundNotify()),
	_includeMuted(this, lang(lng_settings_include_muted), cIncludeMuted()),

	// general
	_changeLanguage(this, lang(lng_settings_change_lang)),
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_autoUpdate(this, lang(lng_settings_auto_update), cAutoUpdate()),
	_checkNow(this, lang(lng_settings_check_now)),
	_restartNow(this, lang(lng_settings_update_now)),
	#endif

    _supportTray(cSupportTray()),
	_workmodeTray(this, lang(lng_settings_workmode_tray), (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray)),
	_workmodeWindow(this, lang(lng_settings_workmode_window), (cWorkMode() == dbiwmWindowOnly || cWorkMode() == dbiwmWindowAndTray)),

	_autoStart(this, lang(lng_settings_auto_start), cAutoStart()),
	_startMinimized(this, lang(lng_settings_start_min), cStartMinimized()),
	_sendToMenu(this, lang(lng_settings_add_sendto), cSendToMenu()),

	_dpiAutoScale(this, lng_settings_scale_auto(lt_cur, scaleLabel(cScreenScale())), (cConfigScale() == dbisAuto)),
	_dpiSlider(this, st::dpiSlider, dbisScaleCount - 1, cEvalScale(cConfigScale()) - 1),
	_dpiWidth1(st::dpiFont1->width(scaleLabel(dbisOne))),
	_dpiWidth2(st::dpiFont2->width(scaleLabel(dbisOneAndQuarter))),
	_dpiWidth3(st::dpiFont3->width(scaleLabel(dbisOneAndHalf))),
	_dpiWidth4(st::dpiFont4->width(scaleLabel(dbisTwo))),

	// chat options
	_replaceEmojis(this, lang(lng_settings_replace_emojis), cReplaceEmojis()),
	_viewEmojis(this, lang(lng_settings_view_emojis)),

	_enterSend(this, qsl("send_key"), 0, lang(lng_settings_send_enter), !cCtrlEnter()),
    _ctrlEnterSend(this, qsl("send_key"), 1, lang((cPlatform() == dbipMac) ? lng_settings_send_cmdenter : lng_settings_send_ctrlenter), cCtrlEnter()),

	_dontAskDownloadPath(this, lang(lng_download_path_dont_ask), !cAskDownloadPath()),
    _downloadPathWidth(st::linkFont->width(lang(lng_download_path_label)) + st::linkFont->spacew),
	_downloadPathEdit(this, cDownloadPath().isEmpty() ? lang(lng_download_path_default) : ((cDownloadPath() == qsl("tmp")) ? lang(lng_download_path_temp) : st::linkFont->elided(QDir::toNativeSeparators(cDownloadPath()), st::setWidth - st::setVersionLeft - _downloadPathWidth))),
	_downloadPathClear(this, lang(lng_download_path_clear)),
	_tempDirClearingWidth(st::linkFont->width(lang(lng_download_path_clearing))),
	_tempDirClearedWidth(st::linkFont->width(lang(lng_download_path_cleared))),
	_tempDirClearFailedWidth(st::linkFont->width(lang(lng_download_path_clear_failed))),

	// chat background
	_backFromGallery(this, lang(lng_settings_bg_from_gallery)),
	_backFromFile(this, lang(lng_settings_bg_from_file)),
	_tileBackground(this, lang(lng_settings_bg_tile), cTileBackground()),
	_needBackgroundUpdate(false),

	// local storage
	_localStorageClear(this, lang(lng_local_storage_clear)),
	_localStorageHeight(1),
	_storageClearingWidth(st::linkFont->width(lang(lng_local_storage_clearing))),
	_storageClearedWidth(st::linkFont->width(lang(lng_local_storage_cleared))),
	_storageClearFailedWidth(st::linkFont->width(lang(lng_local_storage_clear_failed))),

	// advanced
	_passcodeEdit(this, lang(cHasPasscode() ? lng_passcode_change : lng_passcode_turn_on)),
	_passcodeTurnOff(this, lang(lng_passcode_turn_off)),
	_autoLock(this, (cAutoLock() % 3600) ? lng_passcode_autolock_minutes(lt_count, cAutoLock() / 60) : lng_passcode_autolock_hours(lt_count, cAutoLock() / 3600)),
	_autoLockText(lang(psIdleSupported() ? lng_passcode_autolock_away : lng_passcode_autolock_inactive) + ' '),
	_autoLockWidth(st::linkFont->width(_autoLockText)),
	_passwordEdit(this, lang(lng_cloud_password_set)),
	_passwordTurnOff(this, lang(lng_passcode_turn_off)),
	_hasPasswordRecovery(false),
	_connectionType(this, lang(lng_connection_auto_connecting)),
	_connectionTypeText(lang(lng_connection_type) + ' '),
	_connectionTypeWidth(st::linkFont->width(_connectionTypeText)),
	_showSessions(this, lang(lng_settings_show_sessions)),
	_askQuestion(this, lang(lng_settings_ask_question)),
	_telegramFAQ(this, lang(lng_settings_faq)),
	_logOut(this, lang(lng_settings_logout), st::btnLogout),
	_supportGetRequest(0)
{
	if (self()) {
		connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));

		_nameText.setText(st::setNameFont, _nameCache, _textNameOptions);
		PhotoData *selfPhoto = (self()->photoId && self()->photoId != UnknownPeerPhotoId) ? App::photo(self()->photoId) : 0;
		if (selfPhoto && selfPhoto->date) _photoLink = TextLinkPtr(new PhotoLink(selfPhoto, self()));
		MTP::send(MTPusers_GetFullUser(self()->inputUser), rpcDone(&SettingsInner::gotFullSelf), RPCFailHandlerPtr(), 0, 10);
		onReloadPassword();

		connect(App::main(), SIGNAL(peerPhotoChanged(PeerData *)), this, SLOT(peerUpdated(PeerData *)));
		connect(App::main(), SIGNAL(peerNameChanged(PeerData *, const PeerData::Names &, const PeerData::NameFirstChars &)), this, SLOT(peerUpdated(PeerData *)));

		connect(App::app(), SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onReloadPassword(Qt::ApplicationState)));
	}

	// profile
	connect(&_uploadPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhoto()));
	connect(&_cancelPhoto, SIGNAL(clicked()), this, SLOT(onUpdatePhotoCancel()));

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUpdateDone(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUpdateFail(PeerId)));

	// contact info
	connect(&_chooseUsername, SIGNAL(clicked()), this, SLOT(onUsername()));

	// notifications
	_senderName.setDisabled(!_desktopNotify.checked());
	_messagePreview.setDisabled(_senderName.disabled() || !_senderName.checked());
	connect(&_desktopNotify, SIGNAL(changed()), this, SLOT(onDesktopNotify()));
	connect(&_senderName, SIGNAL(changed()), this, SLOT(onSenderName()));
	connect(&_messagePreview, SIGNAL(changed()), this, SLOT(onMessagePreview()));
	connect(&_windowsNotifications, SIGNAL(changed()), this, SLOT(onWindowsNotifications()));
	connect(&_soundNotify, SIGNAL(changed()), this, SLOT(onSoundNotify()));
	connect(&_includeMuted, SIGNAL(changed()), this, SLOT(onIncludeMuted()));

	// general
	connect(&_changeLanguage, SIGNAL(clicked()), this, SLOT(onChangeLanguage()));
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	connect(&_autoUpdate, SIGNAL(changed()), this, SLOT(onAutoUpdate()));
	connect(&_checkNow, SIGNAL(clicked()), this, SLOT(onCheckNow()));
	connect(&_restartNow, SIGNAL(clicked()), this, SLOT(onRestartNow()));
	#endif

	connect(&_workmodeTray, SIGNAL(changed()), this, SLOT(onWorkmodeTray()));
	connect(&_workmodeWindow, SIGNAL(changed()), this, SLOT(onWorkmodeWindow()));

	_startMinimized.setDisabled(!_autoStart.checked());
	connect(&_autoStart, SIGNAL(changed()), this, SLOT(onAutoStart()));
	connect(&_startMinimized, SIGNAL(changed()), this, SLOT(onStartMinimized()));
	connect(&_sendToMenu, SIGNAL(changed()), this, SLOT(onSendToMenu()));

	connect(&_dpiAutoScale, SIGNAL(changed()), this, SLOT(onScaleAuto()));
	connect(&_dpiSlider, SIGNAL(changed(int32)), this, SLOT(onScaleChange()));

	_curVersionText = lng_settings_current_version(lt_version, QString::fromWCharArray(AppVersionStr) + (cDevVersion() ? " dev" : "")) + ' ';
	_curVersionWidth = st::linkFont->width(_curVersionText);
	_newVersionText = lang(lng_settings_update_ready) + ' ';
	_newVersionWidth = st::linkFont->width(_newVersionText);

	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	connect(App::app(), SIGNAL(updateChecking()), this, SLOT(onUpdateChecking()));
	connect(App::app(), SIGNAL(updateLatest()), this, SLOT(onUpdateLatest()));
	connect(App::app(), SIGNAL(updateDownloading(qint64,qint64)), this, SLOT(onUpdateDownloading(qint64,qint64)));
	connect(App::app(), SIGNAL(updateReady()), this, SLOT(onUpdateReady()));
	connect(App::app(), SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	#endif

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

	// chat background
	if (!cChatBackground()) App::initBackground();
	updateChatBackground();
	connect(&_backFromGallery, SIGNAL(clicked()), this, SLOT(onBackFromGallery()));
	connect(&_backFromFile, SIGNAL(clicked()), this, SLOT(onBackFromFile()));
	connect(&_tileBackground, SIGNAL(changed()), this, SLOT(onTileBackground()));

	// local storage
	connect(&_localStorageClear, SIGNAL(clicked()), this, SLOT(onLocalStorageClear()));
	switch (App::wnd()->localStorageState()) {
	case Window::TempDirEmpty: _storageClearState = TempDirEmpty; break;
	case Window::TempDirExists: _storageClearState = TempDirExists; break;
	case Window::TempDirRemoving: _storageClearState = TempDirClearing; break;
	}

	// advanced
	connect(&_passcodeEdit, SIGNAL(clicked()), this, SLOT(onPasscode()));
	connect(&_passcodeTurnOff, SIGNAL(clicked()), this, SLOT(onPasscodeOff()));
	connect(&_autoLock, SIGNAL(clicked()), this, SLOT(onAutoLock()));
	connect(&_passwordEdit, SIGNAL(clicked()), this, SLOT(onPassword()));
	connect(&_passwordTurnOff, SIGNAL(clicked()), this, SLOT(onPasswordOff()));
	connect(&_connectionType, SIGNAL(clicked()), this, SLOT(onConnectionType()));
	connect(&_showSessions, SIGNAL(clicked()), this, SLOT(onShowSessions()));
	connect(&_askQuestion, SIGNAL(clicked()), this, SLOT(onAskQuestion()));
	connect(&_telegramFAQ, SIGNAL(clicked()), this, SLOT(onTelegramFAQ()));
	connect(&_logOut, SIGNAL(clicked()), App::wnd(), SLOT(onLogout()));

    if (App::main()) {
        connect(App::main(), SIGNAL(peerUpdated(PeerData*)), this, SLOT(peerUpdated(PeerData*)));
    }

	updateOnlineDisplay();

	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	switch (App::app()->updatingState()) {
	case Application::UpdatingDownload:
		setUpdatingState(UpdatingDownload, true);
		setDownloadProgress(App::app()->updatingReady(), App::app()->updatingSize());
	break;
	case Application::UpdatingReady: setUpdatingState(UpdatingReady, true); break;
	default: setUpdatingState(UpdatingNone, true); break;
	}
	#else
	_updatingState = UpdatingNone;
	#endif

	updateConnectionType();

	setMouseTracking(true);
}

void SettingsInner::peerUpdated(PeerData *data) {
	if (self() && data == self()) {
		if (self()->photoId && self()->photoId != UnknownPeerPhotoId) {
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
	bool animateBackground = false;
	if (App::main() && App::main()->chatBackgroundLoading()) {
		App::main()->checkChatBackground();
		if (App::main()->chatBackgroundLoading()) {
			animateBackground = true;
		} else {
			updateChatBackground();
		}
	} else if (_needBackgroundUpdate) {
		updateChatBackground();
	}

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
		p.drawText(_left + st::setContactInfoLeft, top + st::linkFont->ascent, _phoneText);
		top += st::linkFont->height + st::setLittleSkip;

		p.drawText(_left, top + st::linkFont->ascent, lang(lng_settings_username));
		top += st::linkFont->height;

		// notifications
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_notify));
		top += st::setHeaderSkip;

		top += _desktopNotify.height() + st::setLittleSkip;
		top += _senderName.height() + st::setLittleSkip;
		top += _messagePreview.height() + st::setSectionSkip;
		if (App::wnd()->psHasNativeNotifications() && cPlatform() == dbipWindows) {
			top += _windowsNotifications.height() + st::setSectionSkip;
		}
		top += _soundNotify.height() + st::setSectionSkip;
		top += _includeMuted.height();
	}

	// general
	p.setFont(st::setHeaderFont->f);
	p.setPen(st::setHeaderColor->p);
	p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_general));
	top += st::setHeaderSkip;

	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
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
	#endif

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

		// chat background
		p.setFont(st::setHeaderFont->f);
		p.setPen(st::setHeaderColor->p);
		p.drawText(_left + st::setHeaderLeft, top + st::setHeaderTop + st::setHeaderFont->ascent, lang(lng_settings_section_background));
		top += st::setHeaderSkip;

		if (animateBackground) {
			const QPixmap &pix = App::main()->newBackgroundThumb()->pixBlurred(st::setBackgroundSize);

			p.drawPixmap(_left, top, st::setBackgroundSize, st::setBackgroundSize, pix, 0, (pix.height() - st::setBackgroundSize) / 2, st::setBackgroundSize, st::setBackgroundSize);

			uint64 dt = getms();
			int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

			int32 x = _left + (st::setBackgroundSize - st::mediaviewLoader.width()) / 2;
			int32 y = top + (st::setBackgroundSize - st::mediaviewLoader.height()) / 2;
			p.fillRect(x, y, st::mediaviewLoader.width(), st::mediaviewLoader.height(), st::photoLoaderBg->b);

			x += (st::mediaviewLoader.width() - cnt * st::mediaviewLoaderPoint.width() - (cnt - 1) * st::mediaviewLoaderSkip) / 2;
			y += (st::mediaviewLoader.height() - st::mediaviewLoaderPoint.height()) / 2;
			QColor c(st::white->c);
			QBrush b(c);
			for (int32 i = 0; i < cnt; ++i) {
				t -= delta;
				while (t < 0) t += period;

				float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
				c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
				b.setColor(c);
				p.fillRect(x + i * (st::mediaviewLoaderPoint.width() + st::mediaviewLoaderSkip), y, st::mediaviewLoaderPoint.width(), st::mediaviewLoaderPoint.height(), b);
			}
			QTimer::singleShot(AnimationTimerDelta, this, SLOT(updateBackgroundRect()));
		} else {
			p.drawPixmap(_left, top, _background);
		}
		top += st::setBackgroundSize;
		top += st::setLittleSkip;
		top += _tileBackground.height();

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
	if (self()) {
		top += _passcodeEdit.height() + st::setLittleSkip;
		if (cHasPasscode()) {
			p.drawText(_left, top + st::linkFont->ascent, _autoLockText);
			top += _autoLock.height() + st::setLittleSkip;
		}
		if (!_waitingConfirm.isEmpty()) p.drawText(_left, top + st::linkFont->ascent, _waitingConfirm);
		top += _passwordEdit.height() + st::setLittleSkip;
	}

	p.drawText(_left, _connectionType.y() + st::linkFont->ascent, _connectionTypeText);
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
		_chooseUsername.move(_left + st::setContactInfoLeft, top); top += st::linkFont->height;

		// notifications
		top += st::setHeaderSkip;
		_desktopNotify.move(_left, top); top += _desktopNotify.height() + st::setLittleSkip;
		_senderName.move(_left, top); top += _senderName.height() + st::setLittleSkip;
		_messagePreview.move(_left, top); top += _messagePreview.height() + st::setSectionSkip;
		if (App::wnd()->psHasNativeNotifications() && cPlatform() == dbipWindows) {
			_windowsNotifications.move(_left, top); top += _windowsNotifications.height() + st::setSectionSkip;
		}
		_soundNotify.move(_left, top); top += _soundNotify.height() + st::setSectionSkip;
		_includeMuted.move(_left, top); top += _includeMuted.height();
	}

	// general
	top += st::setHeaderSkip;
	_changeLanguage.move(_left + st::setWidth - _changeLanguage.width(), top - st::setHeaderSkip + st::setHeaderTop + st::setHeaderFont->ascent - st::linkFont->ascent);
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_autoUpdate.move(_left, top);
	_checkNow.move(_left + st::setWidth - _checkNow.width(), top + st::cbDefFlat.textTop); top += _autoUpdate.height();
	_restartNow.move(_left + st::setWidth - _restartNow.width(), top + st::setVersionTop);
	top += st::setVersionHeight;
	#endif

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

		// chat background
		top += st::setHeaderSkip;
		_backFromGallery.move(_left + st::setBackgroundSize + st::setLittleSkip, top);
		_backFromFile.move(_left + st::setBackgroundSize + st::setLittleSkip, top + _backFromGallery.height() + st::setLittleSkip);
		top += st::setBackgroundSize;

		top += st::setLittleSkip;
		_tileBackground.move(_left, top); top += _tileBackground.height();

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
	if (self()) {
		_passcodeEdit.move(_left, top);
		_passcodeTurnOff.move(_left + st::setWidth - _passcodeTurnOff.width(), top); top += _passcodeTurnOff.height() + st::setLittleSkip;
		if (cHasPasscode()) {
			_autoLock.move(_left + _autoLockWidth, top); top += _autoLock.height() + st::setLittleSkip;
		}
		_passwordEdit.move(_left, top);
		_passwordTurnOff.move(_left + st::setWidth - _passwordTurnOff.width(), top); top += _passwordTurnOff.height() + st::setLittleSkip;
	}

	_connectionType.move(_left + _connectionTypeWidth, top); top += _connectionType.height() + st::setLittleSkip;
	if (self()) {
		_showSessions.move(_left, top); top += _showSessions.height() + st::setSectionSkip;
		_askQuestion.move(_left, top); top += _askQuestion.height() + st::setLittleSkip;
		_telegramFAQ.move(_left, top); top += _telegramFAQ.height() + st::setSectionSkip;
		_logOut.move(_left, top);
	} else {
		top += st::setSectionSkip - st::setLittleSkip;
		_telegramFAQ.move(_left, top);
	}
}

void SettingsInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		App::wnd()->showSettings();
	}
	_secretText += e->text().toLower();
	int32 size = _secretText.size(), from = 0;
	while (size > from) {
		QStringRef str(_secretText.midRef(from));
		if (str == qstr("debugmode")) {
			QString text = cDebug() ? qsl("Do you want to disable DEBUG logs?") : qsl("Do you want to enable DEBUG logs?\n\nAll network events will be logged.");
			ConfirmBox *box = new ConfirmBox(text);
			connect(box, SIGNAL(confirmed()), App::app(), SLOT(onSwitchDebugMode()));
			App::wnd()->showLayer(box);
			from = size;
			break;
		} else if (str == qstr("testmode")) {
			QString text = cTestMode() ? qsl("Do you want to disable TEST mode?") : qsl("Do you want to enable TEST mode?\n\nYou will be switched to test cloud.");
			ConfirmBox *box = new ConfirmBox(text);
			connect(box, SIGNAL(confirmed()), App::app(), SLOT(onSwitchTestMode()));
			App::wnd()->showLayer(box);
			from = size;
			break;
        } else if (str == qstr("loadlang")) {
            chooseCustomLang();
        } else if (qsl("debugmode").startsWith(str) || qsl("testmode").startsWith(str) || qsl("loadlang").startsWith(str)) {
			break;
		}
		++from;
	}
	_secretText = (size > from) ? _secretText.mid(from) : QString();
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
		App::wnd()->showLayer(new EditNameTitleBox(self()));
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
		resize(newWidth, _telegramFAQ.geometry().bottom() + st::setBottom);
	} else {
		resize(newWidth, _logOut.geometry().bottom() + st::setBottom);
	}
}


void SettingsInner::updateOnlineDisplay() {
}

void SettingsInner::updateConnectionType() {
	QString connection;
	switch (cConnectionType()) {
	case dbictAuto: {
		QString transport = MTP::dctransport();
		connection = transport.isEmpty() ? lang(lng_connection_auto_connecting) : lng_connection_auto(lt_transport, transport);
	} break;
	case dbictHttpProxy:
	case dbictTcpProxy: {
		QString transport = MTP::dctransport();
		connection = transport.isEmpty() ? lang(lng_connection_proxy_connecting) : lng_connection_proxy(lt_transport, transport);
	} break;
	}
	_connectionType.setText(connection);
}

void SettingsInner::passcodeChanged() {
	resizeEvent(0);
	_passcodeEdit.setText(lang(cHasPasscode() ? lng_passcode_change : lng_passcode_turn_on));
	_autoLock.setText((cAutoLock() % 3600) ? lng_passcode_autolock_minutes(lt_count, cAutoLock() / 60) : lng_passcode_autolock_hours(lt_count, cAutoLock() / 3600));
//	_passwordEdit.setText()
	showAll();
}

void SettingsInner::updateBackgroundRect() {
	update(_left, _tileBackground.y() - st::setLittleSkip - st::setBackgroundSize, st::setBackgroundSize, st::setBackgroundSize);
}

void SettingsInner::gotFullSelf(const MTPUserFull &selfFull) {
	if (!self()) return;
	App::feedPhoto(selfFull.c_userFull().vprofile_photo);
	App::feedUsers(MTP_vector<MTPUser>(1, selfFull.c_userFull().vuser));
	PhotoData *selfPhoto = (self()->photoId && self()->photoId != UnknownPeerPhotoId) ? App::photo(self()->photoId) : 0;
	if (selfPhoto && selfPhoto->date) {
		_photoLink = TextLinkPtr(new PhotoLink(selfPhoto, self()));
	} else {
		_photoLink = TextLinkPtr();
	}
}

void SettingsInner::gotPassword(const MTPaccount_Password &result) {
	_waitingConfirm = QString();

	switch (result.type()) {
	case mtpc_account_noPassword: {
		const MTPDaccount_noPassword &d(result.c_account_noPassword());
		_curPasswordSalt = QByteArray();
		_hasPasswordRecovery = false;
		_curPasswordHint = QString();
		_newPasswordSalt = qba(d.vnew_salt);
		QString pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) _waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
	} break;

	case mtpc_account_password: {
		const MTPDaccount_password &d(result.c_account_password());
		_curPasswordSalt = qba(d.vcurrent_salt);
		_hasPasswordRecovery = d.vhas_recovery.v;
		_curPasswordHint = qs(d.vhint);
		_newPasswordSalt = qba(d.vnew_salt);
		QString pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) _waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
	} break;
	}
	_waitingConfirm = st::linkFont->elided(_waitingConfirm, st::setWidth - _passwordTurnOff.width());
	_passwordEdit.setText(lang(_curPasswordSalt.isEmpty() ? lng_cloud_password_set : lng_cloud_password_edit));
	showAll();
	update();

	_newPasswordSalt.resize(_newPasswordSalt.size() + 8);
	memset_rand(_newPasswordSalt.data() + _newPasswordSalt.size() - 8, 8);
}

void SettingsInner::offPasswordDone(const MTPBool &result) {
	onReloadPassword();
}

bool SettingsInner::offPasswordFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	onReloadPassword();
	return true;
}

void SettingsInner::usernameChanged() {
	_chooseUsername.setText((self() && !self()->username.isEmpty()) ? ('@' + self()->username) : lang(lng_settings_choose_username));
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
		_chooseUsername.show();
	} else {
		_chooseUsername.hide();
	}

	// notifications
	if (self()) {
		_desktopNotify.show();
		_senderName.show();
		_messagePreview.show();
		if (App::wnd()->psHasNativeNotifications() && cPlatform() == dbipWindows) {
			_windowsNotifications.show();
		} else {
			_windowsNotifications.hide();
		}
		_soundNotify.show();
		_includeMuted.show();
	} else {
		_desktopNotify.hide();
		_senderName.hide();
		_messagePreview.hide();
		_windowsNotifications.hide();
		_soundNotify.hide();
		_includeMuted.hide();
	}

	// general
	_changeLanguage.show();
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_autoUpdate.show();
	setUpdatingState(_updatingState, true);
	#endif
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
		_dontAskDownloadPath.hide();
		_downloadPathEdit.hide();
		_downloadPathClear.hide();
	}

	// chat background
	if (self()) {
		_backFromGallery.show();
		_backFromFile.show();
		_tileBackground.show();
	} else {
		_backFromGallery.hide();
		_backFromFile.hide();
		_tileBackground.hide();
	}

	// local storage
	if (self() && _storageClearState == TempDirExists) {
		_localStorageClear.show();
	} else {
		_localStorageClear.hide();
	}

	// advanced
	if (self()) {
		_passcodeEdit.show();
		if (cHasPasscode()) {
			_autoLock.show();
			_passcodeTurnOff.show();
		} else {
			_autoLock.hide();
			_passcodeTurnOff.hide();
		}
		if (_waitingConfirm.isEmpty()) {
			_passwordEdit.show();
		} else {
			_passwordEdit.hide();
		}
		if (_curPasswordSalt.isEmpty() && _waitingConfirm.isEmpty()) {
			_passwordTurnOff.hide();
		} else {
			_passwordTurnOff.show();
		}
		_showSessions.show();
		_askQuestion.show();
		_logOut.show();
	} else {
		_passcodeEdit.hide();
		_autoLock.hide();
		_passcodeTurnOff.hide();
		_passwordEdit.hide();
		_passwordTurnOff.hide();
		_showSessions.hide();
		_askQuestion.hide();
		_logOut.hide();
	}
	_telegramFAQ.show();
}

void SettingsInner::saveError(const QString &str) {
	_errorText = str;
	resizeEvent(0);
	update();
}

void SettingsInner::supportGot(const MTPhelp_Support &support) {
	if (!App::main()) return;

	if (support.type() == mtpc_help_support) {
		const MTPDhelp_support &d(support.c_help_support());
		UserData *u = App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
		App::main()->showPeerHistory(u->id, ShowAtUnreadMsgId);
		App::wnd()->hideSettings();
	}
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

void SettingsInner::onShowSessions() {
	SessionsBox *box = new SessionsBox();
	App::wnd()->showLayer(box);
}

void SettingsInner::onAskQuestion() {
	if (!App::self()) return;

	ConfirmBox *box = new ConfirmBox(lang(lng_settings_ask_sure), lang(lng_settings_ask_ok), st::defaultBoxButton, lang(lng_settings_faq_button));
	connect(box, SIGNAL(confirmed()), this, SLOT(onAskQuestionSure()));
	connect(box, SIGNAL(cancelPressed()), this, SLOT(onTelegramFAQ()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onAskQuestionSure() {
	if (_supportGetRequest) return;
	_supportGetRequest = MTP::send(MTPhelp_GetSupport(), rpcDone(&SettingsInner::supportGot));
}

void SettingsInner::onTelegramFAQ() {
	QDesktopServices::openUrl(telegramFaqLink());
}

void SettingsInner::chooseCustomLang() {
    QString file;
    QByteArray arr;
    if (filedialogGetOpenFile(file, arr, qsl("Choose language .strings file"), qsl("Language files (*.strings)"))) {
        _testlang = QFileInfo(file).absoluteFilePath();
		LangLoaderPlain loader(_testlang, LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
        if (loader.errors().isEmpty()) {
            LangLoaderResult result = loader.found();
            QString text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
				save = result.value(lng_box_ok, langOriginal(lng_box_ok)),
				cancel = result.value(lng_cancel, langOriginal(lng_cancel));
            ConfirmBox *box = new ConfirmBox(text, save, st::defaultBoxButton, cancel);
            connect(box, SIGNAL(confirmed()), this, SLOT(onSaveTestLang()));
            App::wnd()->showLayer(box);
        } else {
			App::wnd()->showLayer(new InformBox("Custom lang failed :(\n\nError: " + loader.errors()));
        }
    }
}

void SettingsInner::onChangeLanguage() {
	if ((_changeLanguage.clickModifiers() & Qt::ShiftModifier) && (_changeLanguage.clickModifiers() & Qt::AltModifier)) {
        chooseCustomLang();
	} else {
		App::wnd()->showLayer(new LanguageBox());
	}
}

void SettingsInner::onSaveTestLang() {
	cSetLangFile(_testlang);
	cSetLang(languageTest);
	Local::writeSettings();
	cSetRestarting(true);
	App::quit();
}

void SettingsInner::onUpdateLocalStorage() {
	resizeEvent(0);
	updateSize(width());
	update();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void SettingsInner::onAutoUpdate() {
	cSetAutoUpdate(!cAutoUpdate());
	Local::writeSettings();
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
#endif

void SettingsInner::onRestartNow() {
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	checkReadyUpdate();
	if (_updatingState == UpdatingReady) {
		cSetRestartingUpdate(true);
	} else {
		cSetRestarting(true);
		cSetRestartingToSettings(true);
	}
	#else
	cSetRestarting(true);
	cSetRestartingToSettings(true);
	#endif
	App::quit();
}

void SettingsInner::onPasscode() {
	PasscodeBox *box = new PasscodeBox();
	connect(box, SIGNAL(closed()), this, SLOT(passcodeChanged()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onPasscodeOff() {
	PasscodeBox *box = new PasscodeBox(true);
	connect(box, SIGNAL(closed()), this, SLOT(passcodeChanged()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onPassword() {
	PasscodeBox *box = new PasscodeBox(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint);
	connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
	App::wnd()->showLayer(box);
}

void SettingsInner::onPasswordOff() {
	if (_curPasswordSalt.isEmpty()) {
		_passwordTurnOff.hide();

//		int32 flags = MTPDaccount_passwordInputSettings::flag_new_salt | MTPDaccount_passwordInputSettings::flag_new_password_hash | MTPDaccount_passwordInputSettings::flag_hint | MTPDaccount_passwordInputSettings::flag_email;
		int32 flags = MTPDaccount_passwordInputSettings::flag_email;
		MTPaccount_PasswordInputSettings settings(MTP_account_passwordInputSettings(MTP_int(flags), MTP_string(QByteArray()), MTP_string(QByteArray()), MTP_string(QString()), MTP_string(QString())));
		MTP::send(MTPaccount_UpdatePasswordSettings(MTP_string(QByteArray()), settings), rpcDone(&SettingsInner::offPasswordDone), rpcFail(&SettingsInner::offPasswordFail));
	} else {
		PasscodeBox *box = new PasscodeBox(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint, true);
		connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
		App::wnd()->showLayer(box);
	}
}

void SettingsInner::onReloadPassword(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		MTP::send(MTPaccount_GetPassword(), rpcDone(&SettingsInner::gotPassword));
	}
}

void SettingsInner::onAutoLock() {
	AutoLockBox *box = new AutoLockBox();
	connect(box, SIGNAL(closed()), this, SLOT(passcodeChanged()));
	App::wnd()->showLayer(box);
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
	Local::writeSettings();
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
	Local::writeSettings();
}

void SettingsInner::onAutoStart() {
	_startMinimized.setDisabled(!_autoStart.checked());
	cSetAutoStart(_autoStart.checked());
	if (!_autoStart.checked() && _startMinimized.checked()) {
		psAutoStart(false);
		_startMinimized.setChecked(false);
	} else {
		psAutoStart(_autoStart.checked());
		Local::writeSettings();
	}
}

void SettingsInner::onStartMinimized() {
	cSetStartMinimized(_startMinimized.checked());
	Local::writeSettings();
}

void SettingsInner::onSendToMenu() {
	cSetSendToMenu(_sendToMenu.checked());
	psSendToMenu(_sendToMenu.checked());
	Local::writeSettings();
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
	Local::writeSettings();
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
		ConfirmBox *box = new ConfirmBox(lang(lng_settings_need_restart), lang(lng_settings_restart_now), st::defaultBoxButton, lang(lng_settings_restart_later));
		connect(box, SIGNAL(confirmed()), this, SLOT(onRestartNow()));
		App::wnd()->showLayer(box);
	}
}

void SettingsInner::onSoundNotify() {
	cSetSoundNotify(_soundNotify.checked());
	Local::writeUserSettings();
}

void SettingsInner::onIncludeMuted() {
	cSetIncludeMuted(_includeMuted.checked());
	if (App::wnd()) App::wnd()->updateCounter();
	Local::writeUserSettings();
}

void SettingsInner::onWindowsNotifications() {
	if (cPlatform() != dbipWindows) return;
	cSetWindowsNotifications(!cWindowsNotifications());
	App::wnd()->notifyClearFast();
	cSetCustomNotifies(!cWindowsNotifications());
	Local::writeUserSettings();
}

void SettingsInner::onDesktopNotify() {
	cSetDesktopNotify(_desktopNotify.checked());
	if (!_desktopNotify.checked()) {
		App::wnd()->notifyClear();
		_senderName.setDisabled(true);
		_messagePreview.setDisabled(true);
	} else {
		_senderName.setDisabled(false);
		_messagePreview.setDisabled(!_senderName.checked());
	}
	Local::writeUserSettings();
	if (App::wnd()) App::wnd()->updateTrayMenu();
}

void SettingsInner::enableDisplayNotify(bool enable)
{
	_desktopNotify.setChecked(enable);
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
		Local::writeUserSettings();
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
	Local::writeUserSettings();
	App::wnd()->notifyUpdateAll();
}

void SettingsInner::onReplaceEmojis() {
	cSetReplaceEmojis(_replaceEmojis.checked());
	Local::writeUserSettings();

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
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void SettingsInner::onCtrlEnterSend() {
	if (_ctrlEnterSend.checked()) {
		cSetCtrlEnter(true);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void SettingsInner::onBackFromGallery() {
	BackgroundBox *box = new BackgroundBox();
	App::wnd()->showLayer(box);
}

void SettingsInner::onBackFromFile() {
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
	}

	if (img.isNull() || img.width() <= 0 || img.height() <= 0) return;

	if (img.width() > 4096 * img.height()) {
		img = img.copy((img.width() - 4096 * img.height()) / 2, 0, 4096 * img.height(), img.height());
	} else if (img.height() > 4096 * img.width()) {
		img = img.copy(0, (img.height() - 4096 * img.width()) / 2, img.width(), 4096 * img.width());
	}

	App::initBackground(-1, img);
	_tileBackground.setChecked(false);
	updateChatBackground();
}

void SettingsInner::updateChatBackground() {
	int32 size = st::setBackgroundSize * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		QPainter p(&back);
		const QPixmap &pix(*cChatBackground());
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawPixmap(0, 0, st::setBackgroundSize, st::setBackgroundSize, pix, sx, sy, s, s);
	}
	_background = QPixmap::fromImage(back);
	_background.setDevicePixelRatio(cRetinaFactor());
	_needBackgroundUpdate = false;
	updateBackgroundRect();
}

void SettingsInner::needBackgroundUpdate(bool tile) {
	_needBackgroundUpdate = true;
	_tileBackground.setChecked(tile);
	updateChatBackground();
}

void SettingsInner::onTileBackground() {
	if (cTileBackground() != _tileBackground.checked()) {
		cSetTileBackground(_tileBackground.checked());
		if (App::main()) App::main()->clearCachedBackground();
		Local::writeUserSettings();
	}
}

void SettingsInner::onDontAskDownloadPath() {
	cSetAskDownloadPath(!_dontAskDownloadPath.checked());
	Local::writeUserSettings();

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
		path = st::linkFont->elided(QDir::toNativeSeparators(cDownloadPath()), st::setWidth - st::setVersionLeft - _downloadPathWidth);
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

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
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
#endif

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

void SettingsWidget::updateDisplayNotify()
{
	_inner.enableDisplayNotify(cDesktopNotify());
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

void SettingsWidget::setInnerFocus() {
	_inner.setFocus();
}

void SettingsWidget::needBackgroundUpdate(bool tile) {
	_inner.needBackgroundUpdate(tile);
}

SettingsWidget::~SettingsWidget() {
	if (App::wnd()) App::wnd()->noSettings(this);
}
