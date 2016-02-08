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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "mediaview.h"
#include "mainwidget.h"
#include "window.h"
#include "application.h"
#include "gui/filedialog.h"

namespace {
	class SaveMsgLink : public ITextLink {
		TEXT_LINK_CLASS(SaveMsgLink)

	public:

		SaveMsgLink(MediaView *view) : _view(view) {
		}

		void onClick(Qt::MouseButton button) const {
			if (button == Qt::LeftButton) {
				_view->showSaveMsgFile();
			}
		}

	private:

		MediaView *_view;
	};

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
}

MediaView::MediaView() : TWidget(App::wnd())
, _photo(0)
, _doc(0)
, _overview(OverviewCount)
, _leftNavVisible(false)
, _rightNavVisible(false)
, _saveVisible(false)
, _headerHasLink(false)
, _animStarted(getms())
, _width(0)
, _x(0)
, _y(0)
, _w(0)
, _h(0)
, _xStart(0)
, _yStart(0)
, _zoom(0)
, _zoomToScreen(0)
, _pressed(false)
, _dragging(0)
, _gif(0)
, _full(-1)
, _docNameWidth(0)
, _docSizeWidth(0)
, _docThumbx(0)
, _docThumby(0)
, _docThumbw(0)
, _docRadial(animation(this, &MediaView::step_radial))
, _docDownload(this, lang(lng_media_download), st::mvDocLink)
, _docSaveAs(this, lang(lng_mediaview_save_as), st::mvDocLink)
, _docCancel(this, lang(lng_cancel), st::mvDocLink)
, _migrated(0)
, _history(0)
, _peer(0)
, _user(0)
, _from(0)
, _index(-1)
, _msgid(0)
, _msgmigrated(false)
, _channel(NoChannel)
, _canForward(false)
, _canDelete(false)
, _loadRequest(0)
, _over(OverNone)
, _down(OverNone)
, _lastAction(-st::mvDeltaFromLastAction, -st::mvDeltaFromLastAction)
, _ignoringDropdown(false)
, _a_state(animation(this, &MediaView::step_state))
, _controlsState(ControlsShown)
, _controlsAnimStarted(0)
, _menu(0)
, _dropdown(this, st::mvDropdown)
, _receiveMouse(true)
, _touchPress(false)
, _touchMove(false)
, _touchRightButton(false)
, _saveMsgStarted(0)
, _saveMsgOpacity(0) {
	TextCustomTagsMap custom;
	custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
	_saveMsgText.setRichText(st::medviewSaveMsgFont, lang(lng_mediaview_saved), _textDlgOptions, custom);
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::medviewSaveMsgPadding.left() + st::medviewSaveMsgPadding.right(), st::medviewSaveMsgFont->height + st::medviewSaveMsgPadding.top() + st::medviewSaveMsgPadding.bottom());
	_saveMsgText.setLink(1, TextLinkPtr(new SaveMsgLink(this)));

	_transparentBrush = QBrush(App::sprite().copy(st::mvTransparentBrush));

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);

	hide();
	createWinId();

	_saveMsgUpdater.setSingleShot(true);
	connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onCheckActive()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	_btns.push_back(_btnSaveCancel = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_cancel))));
	connect(_btnSaveCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));
	_btns.push_back(_btnToMessage = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_context_to_msg))));
	connect(_btnToMessage, SIGNAL(clicked()), this, SLOT(onToMessage()));
	_btns.push_back(_btnShowInFolder = _dropdown.addButton(new IconedButton(this, st::mvButton, lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder))));
	connect(_btnShowInFolder, SIGNAL(clicked()), this, SLOT(onShowInFolder()));
	_btns.push_back(_btnCopy = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_copy))));
	connect(_btnCopy, SIGNAL(clicked()), this, SLOT(onCopy()));
	_btns.push_back(_btnForward = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_forward))));
	connect(_btnForward, SIGNAL(clicked()), this, SLOT(onForward()));
	_btns.push_back(_btnDelete = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_delete))));
	connect(_btnDelete, SIGNAL(clicked()), this, SLOT(onDelete()));
	_btns.push_back(_btnSaveAs = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_save_as))));
	connect(_btnSaveAs, SIGNAL(clicked()), this, SLOT(onSaveAs()));
	_btns.push_back(_btnViewAll = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_photos_all))));
	connect(_btnViewAll, SIGNAL(clicked()), this, SLOT(onOverview()));

	_dropdown.hide();
	connect(&_dropdown, SIGNAL(hiding()), this, SLOT(onDropdownHiding()));

	_controlsHideTimer.setSingleShot(true);
	connect(&_controlsHideTimer, SIGNAL(timeout()), this, SLOT(onHideControls()));

	connect(&_docDownload, SIGNAL(clicked()), this, SLOT(onDownload()));
	connect(&_docSaveAs, SIGNAL(clicked()), this, SLOT(onSaveAs()));
	connect(&_docCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));
}

void MediaView::moveToScreen() {
	if (App::wnd() && windowHandle() && App::wnd()->windowHandle() && windowHandle()->screen() != App::wnd()->windowHandle()->screen()) {
		windowHandle()->setScreen(App::wnd()->windowHandle()->screen());
	}

	QPoint wndCenter(App::wnd()->x() + App::wnd()->width() / 2, App::wnd()->y() + App::wnd()->height() / 2);
	QRect avail = Sandbox::screenGeometry(wndCenter);
	if (avail != geometry()) {
		setGeometry(avail);
	}

	int32 navSkip = 2 * st::mvControlMargin + st::mvControlSize;
	_closeNav = myrtlrect(width() - st::mvControlMargin - st::mvControlSize, st::mvControlMargin, st::mvControlSize, st::mvControlSize);
	_closeNavIcon = centersprite(_closeNav, st::mvClose);
	_leftNav = myrtlrect(st::mvControlMargin, navSkip, st::mvControlSize, height() - 2 * navSkip);
	_leftNavIcon = centersprite(_leftNav, st::mvLeft);
	_rightNav = myrtlrect(width() - st::mvControlMargin - st::mvControlSize, navSkip, st::mvControlSize, height() - 2 * navSkip);
	_rightNavIcon = centersprite(_rightNav, st::mvRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
}

void MediaView::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (!_photo && !_doc) return;
	if (_history && (_history->peer == peer || (_migrated && _migrated->peer == peer)) && type == _overview) {
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
	} else if (_user == peer && type == OverviewCount) {
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

bool MediaView::gifShown() const {
	if (_gif && _gif->ready()) {
		if (!_gif->started()) {
			_gif->start(_gif->width(), _gif->height(), _gif->width(), _gif->height(), false);
			const_cast<MediaView*>(this)->_current = QPixmap();
		}
		return _gif->state() != ClipError;
	}
	return false;
}

void MediaView::stopGif() {
	delete _gif;
	_gif = 0;
}

void MediaView::documentUpdated(DocumentData *doc) {
	if (_doc && _doc == doc && !fileShown()) {
		if ((_doc->loading() && _docCancel.isHidden()) || (!_doc->loading() && !_docCancel.isHidden())) {
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
	mediaOverviewUpdated(row->history()->peer, _overview);
}

void MediaView::updateDocSize() {
	if (!_doc || fileShown()) return;

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
	_docSizeWidth = st::mvFont->width(_docSize);
	int32 maxw = st::mvDocSize.width() - st::mvDocIconSize - st::mvDocPadding * 3;
	if (_docSizeWidth > maxw) {
		_docSize = st::mvFont->elided(_docSize, maxw);
		_docSizeWidth = st::mvFont->width(_docSize);
	}
}

void MediaView::updateControls() {
	if (_doc && !fileShown()) {
		if (_doc->loading()) {
			_docDownload.hide();
			_docSaveAs.hide();
			_docCancel.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
			_docCancel.show();
			if (!_docRadial.animating()) {
				_docRadial.start(_doc->progress());
			}
		} else {
			if (_doc->loaded(true)) {
				_docDownload.hide();
				_docSaveAs.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docSaveAs.show();
				_docCancel.hide();
			} else {
				_docDownload.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docDownload.show();
				_docSaveAs.moveToLeft(_docRect.x() + 2.5 * st::mvDocPadding + st::mvDocIconSize + _docDownload.width(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docSaveAs.show();
				_docCancel.hide();
			}
		}
		updateDocSize();
	} else {
		_docDownload.hide();
		_docSaveAs.hide();
		_docCancel.hide();
	}

	_saveVisible = ((_photo && _photo->loaded()) || (_doc && (_doc->loaded(true) || (!fileShown() && (_photo || _doc)))));
	_saveNav = myrtlrect(width() - st::mvIconSize.width() * 2, height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_saveNavIcon = centersprite(_saveNav, st::mvSave);
	_moreNav = myrtlrect(width() - st::mvIconSize.width(), height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_moreNavIcon = centersprite(_moreNav, st::mvMore);

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
		_fromName.setText(st::mvFont, (_from->migrateTo() ? _from->migrateTo() : _from)->name, _textNameOptions);
		_nameNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, qMin(_fromName.maxWidth(), width() / 3), st::mvFont->height);
		_dateNav = myrtlrect(st::mvTextLeft + _nameNav.width() + st::mvTextSkip, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	}
	updateHeader();
	if (_photo || (_history && (_overview == OverviewPhotos || _overview == OverviewDocuments))) {
		_leftNavVisible = (_index > 0) || (_index == 0 && (
			(!_msgmigrated && _history && _history->overview[_overview].size() < _history->overviewCount(_overview)) ||
			(_msgmigrated && _migrated && _migrated->overview[_overview].size() < _migrated->overviewCount(_overview)) ||
			(!_msgmigrated && _history && _migrated && (!_migrated->overview[_overview].isEmpty() || _migrated->overviewCount(_overview) > 0))));
		_rightNavVisible = (_index >= 0) && (
			(!_msgmigrated && _history && _index + 1 < _history->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _index + 1 < _migrated->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _history && (!_history->overview[_overview].isEmpty() || _history->overviewCount(_overview) > 0)) ||
			(!_history && _user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount)));
		if (_msgmigrated && !_history->overviewLoaded(_overview)) {
			_leftNavVisible = _rightNavVisible = false;
		}
	} else {
		_leftNavVisible = _rightNavVisible = false;
	}
	if (!_caption.isEmpty()) {
		int32 skipw = qMax(_dateNav.left() + _dateNav.width(), _headerNav.left() + _headerNav.width());
		int32 maxw = qMin(qMax(width() - 2 * skipw - st::mvCaptionPadding.left() - st::mvCaptionPadding.right() - 2 * st::mvCaptionMargin.width(), int(st::msgMinWidth)), _caption.maxWidth());
		int32 maxh = qMin(_caption.countHeight(maxw), int(height() / 4 - st::mvCaptionPadding.top() - st::mvCaptionPadding.bottom() - 2 * st::mvCaptionMargin.height()));
		_captionRect = QRect((width() - maxw) / 2, height() - maxh - st::mvCaptionPadding.bottom() - st::mvCaptionMargin.height(), maxw, maxh);
	} else {
		_captionRect = QRect();
	}
	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

void MediaView::updateDropdown() {
	_btnSaveCancel->setVisible(_doc && _doc->loading());
	_btnToMessage->setVisible(_msgid > 0);
	_btnShowInFolder->setVisible(_doc && !_doc->already(true).isEmpty());
	_btnSaveAs->setVisible(true);
	_btnCopy->setVisible((_doc && fileShown()) || (_photo && _photo->loaded()));
	_btnForward->setVisible(_canForward);
	_btnDelete->setVisible(_canDelete || (_photo && App::self() && App::self()->photoId == _photo->id) || (_photo && _photo->peer && _photo->peer->photoId == _photo->id && (_photo->peer->isChat() || (_photo->peer->isChannel() && _photo->peer->asChannel()->amCreator()))));
	_btnViewAll->setVisible((_overview != OverviewCount) && _history);
	_btnViewAll->setText(lang(_doc ? lng_mediaview_files_all : lng_mediaview_photos_all));
	_dropdown.updateButtons();
	_dropdown.moveToRight(0, height() - _dropdown.height());
}

void MediaView::step_state(uint64 ms, bool timer) {
	bool result = false;
	for (Showing::iterator i = _animations.begin(); i != _animations.end();) {
		int64 start = i.value();
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
		float64 dt = float64(ms - start) / st::mvFadeDuration;
		if (dt >= 1) {
			_animOpacities.remove(i.key());
			i = _animations.erase(i);
		} else {
			_animOpacities[i.key()].update(dt, anim::linear);
			++i;
		}
	}
	if (_controlsState == ControlsShowing || _controlsState == ControlsHiding) {
		float64 dt = float64(ms - _controlsAnimStarted) / (_controlsState == ControlsShowing ? st::mvShowDuration : st::mvHideDuration);
		if (dt >= 1) {
			a_cOpacity.finish();
			_controlsState = (_controlsState == ControlsShowing ? ControlsShown : ControlsHidden);
			setCursor(_controlsState == ControlsHidden ? Qt::BlankCursor : (_over == OverNone ? style::cur_default : style::cur_pointer));
		} else {
			a_cOpacity.update(dt, anim::linear);
		}
		QRegion toUpdate = QRegion() + (_over == OverLeftNav ? _leftNav : _leftNavIcon) + (_over == OverRightNav ? _rightNav : _rightNavIcon) + (_over == OverClose ? _closeNav : _closeNavIcon) + _saveNavIcon + _moreNavIcon + _headerNav + _nameNav + _dateNav + _captionRect.marginsAdded(st::mvCaptionPadding);
		update(toUpdate);
		if (dt < 1) result = true;
	}
	if (!result && _animations.isEmpty()) {
		_a_state.stop();
	}
}

void MediaView::step_radial(uint64 ms, bool timer) {
	if (!_doc) {
		_docRadial.stop();
		return;
	}
	_docRadial.update(_doc->progress(), !_doc->loading(), ms);
	if (timer && _docRadial.animating()) {
		update(_docIconRect);
	}
	if (_doc->loaded() && _doc->size < MediaViewImageSizeLimit && (!_docRadial.animating() || _doc->isAnimation())) {
		if (!_doc->data().isEmpty() && _doc->isAnimation()) {
			displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
		} else {
			const FileLocation &location(_doc->location(true));
			if (location.accessEnable()) {
				if (_doc->isAnimation() || QImageReader(location.name()).canRead()) {
					displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
				}
				location.accessDisable();
			}
		}

	}
}

MediaView::~MediaView() {
	deleteAndMark(_gif);
	deleteAndMark(_menu);
}

void MediaView::showSaveMsgFile() {
	psShowInFolder(_saveMsgFilename);
}

void MediaView::close() {
	if (_menu) _menu->hideMenu(true);
	if (App::wnd()) {
		Ui::hideLayer(true);
	}
}

void MediaView::activateControls() {
	if (!_menu) _controlsHideTimer.start(int(st::mvWaitHide));
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) {
		_controlsState = ControlsShowing;
		_controlsAnimStarted = getms();
		a_cOpacity.start(1);
		if (!_a_state.animating()) _a_state.start();
	}
}

void MediaView::onHideControls(bool force) {
	if (!force && (!_dropdown.isHidden() || _menu)) return;
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;
	_controlsState = ControlsHiding;
	_controlsAnimStarted = getms();
	a_cOpacity.start(0);
	if (!_a_state.animating()) _a_state.start();
}

void MediaView::onDropdownHiding() {
	setFocus();
	_ignoringDropdown = true;
	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	updateOver(_lastMouseMovePos);
	_ignoringDropdown = false;
	if (!_controlsHideTimer.isActive()) {
		onHideControls(true);
	}
}

void MediaView::onToMessage() {
	if (HistoryItem *item = _msgid ? App::histItemById(_msgmigrated ? 0 : _channel, _msgid) : 0) {
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
				filter = mimeType.filterString() + qsl(";;All files (*.*)");
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
				DocumentSaveLink::doSave(_doc, true);
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
		bool gotName = filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")));
		psShowOverAll(this);
		if (gotName) {
			if (!file.isEmpty()) {
				_photo->full->pix().toImage().save(file, "JPG");
			}
		}
	}
	activateWindow();
	Sandbox::setActiveWindow(this);
	setFocus();
}

void MediaView::onDocClick() {
	if (_doc->loading()) {
		onSaveCancel();
	} else {
		DocumentOpenLink::doOpen(_doc, ActionOnLoadNone);
		if (_doc->loading() && !_docRadial.animating()) {
			_docRadial.start(_doc->progress());
		}
	}
}

void MediaView::clipCallback(ClipReaderNotification notification) {
	if (!_gif) return;

	switch (notification) {
	case ClipReaderReinit: {
		if (HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
			if (_gif->state() == ClipError) {
				_current = QPixmap();
			}
			displayDocument(_doc, item);
		} else {
			stopGif();
		}
	} break;

	case ClipReaderRepaint: {
		if (!_gif->currentDisplayed()) {
			update(_x, _y, _w, _h);
		}
	} break;
	}
}

void MediaView::onDownload() {
	if (cAskDownloadPath()) {
		return onSaveAs();
	}

	QString path;
	if (cDownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (cDownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = cDownloadPath();
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
				DocumentSaveLink::doSave(_doc);
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
	QString already(_doc->already(true));
	if (!already.isEmpty()) psShowInFolder(already);
}

void MediaView::onForward() {
	HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid);
	if (!_msgid || !item) return;

	if (App::wnd()) {
		close();
		if (App::main()) {
			App::contextItem(item);
			App::main()->forwardLayer();
		}
	}
}

void MediaView::onDelete() {
	close();
	if (!_msgid) {
		if (App::self() && _photo && App::self()->photoId == _photo->id) {
			App::app()->peerClearPhoto(App::self()->id);
		} else if (_photo->peer && _photo->peer->photoId == _photo->id) {
			App::app()->peerClearPhoto(_photo->peer->id);
		}
	} else {
		HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid);
		if (item) {
			App::contextItem(item);
			App::main()->deleteLayer();
		}
	}
}

void MediaView::onOverview() {
	if (_menu) _menu->hideMenu(true);
	if (!_history || _overview == OverviewCount) {
		update();
		return;
	}
	close();
	if (_history->peer) App::main()->showMediaOverview(_history->peer, _overview);
}

void MediaView::onCopy() {
	if (!_dropdown.isHidden()) {
		_dropdown.ignoreShow();
		_dropdown.hideStart();
	}
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
	_history = context ? context->history() : 0;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	} else {
		_migrated = 0;
	}
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
	_canForward = _msgid > 0;
	_canDelete = context ? context->canDelete() : false;
	_photo = photo;
	if (_history) {
		_overview = OverviewPhotos;
		findCurrent();
	}

	displayPhoto(photo, context);
	preloadData(0);
	activateControls();
}

void MediaView::showPhoto(PhotoData *photo, PeerData *context) {
	_history = _migrated = 0;
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
	}
	displayPhoto(photo, 0);
	preloadData(0);
	activateControls();
}

void MediaView::showDocument(DocumentData *doc, HistoryItem *context) {
	_photo = 0;
	_history = context ? context->history() : 0;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	} else {
		_migrated = 0;
	}
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
	_canForward = _msgid > 0;
	_canDelete = context ? context->canDelete() : false;
	if (_history) {
		_overview = OverviewDocuments;
		findCurrent();
	}
	displayDocument(doc, context);
	preloadData(0);
	activateControls();
}

void MediaView::displayPhoto(PhotoData *photo, HistoryItem *item) {
	stopGif();
	_doc = 0;
	_photo = photo;

	_zoom = 0;

	_caption = Text();
	if (HistoryMessage *itemMsg = item ? item->toHistoryMessage() : 0) {
		if (HistoryPhoto *photoMsg = dynamic_cast<HistoryPhoto*>(itemMsg->getMedia())) {
			_caption.setText(st::mvCaptionFont, photoMsg->getCaption(), (item->from()->isUser() && item->from()->asUser()->botInfo) ? _captionBotOptions : _captionTextOptions);
		}
	}

	_zoomToScreen = 0;
	MTP::clearLoaderPriorities();
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
		if (HistoryForwarded *fwd = item->toHistoryForwarded()) {
			_from = fwd->fromForwarded();
		} else {
			_from = item->from();
		}
	} else {
		_from = _user;
	}
	updateControls();
	_photo->download();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
		psShowOverAll(this);
		activateWindow();
		setFocus();
	}
}

void MediaView::displayDocument(DocumentData *doc, HistoryItem *item) { // empty messages shown as docs: doc can be NULL
	if (!doc || !doc->isAnimation() || doc != _doc || (item && (item->id != _msgid || (item->history() != (_msgmigrated ? _migrated : _history))))) {
		stopGif();
	}
	_doc = doc;
	_photo = 0;

	_current = QPixmap();

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

			const FileLocation &location(_doc->location(true));
			if (!_doc->data().isEmpty() && _doc->isAnimation()) {
				if (!_gif) {
					if (_doc->dimensions.width() && _doc->dimensions.height()) {
						_current = _doc->thumb->pixNoCache(_doc->dimensions.width(), _doc->dimensions.height(), true, true, false, _doc->dimensions.width(), _doc->dimensions.height());
					}
					_gif = new ClipReader(location, _doc->data(), func(this, &MediaView::clipCallback));
				}
			} else if (location.accessEnable()) {
				if (_doc->isAnimation()) {
					if (!_gif) {
						if (_doc->dimensions.width() && _doc->dimensions.height()) {
							_current = _doc->thumb->pixNoCache(_doc->dimensions.width(), _doc->dimensions.height(), true, true, false, _doc->dimensions.width(), _doc->dimensions.height());
						}
						_gif = new ClipReader(location, _doc->data(), func(this, &MediaView::clipCallback));
					}
				} else {
					if (QImageReader(location.name()).canRead()) {
						_current = QPixmap::fromImage(App::readImage(location.name(), 0, false), Qt::ColorOnly);
					}
				}
				location.accessDisable();
			}
		}
	}

	if (!fileShown()) {
		if (!_doc || _doc->thumb->isNull()) {
			int32 colorIndex = documentColorIndex(_doc, _docExt);
			_docIconColor = documentColor(colorIndex);
			style::sprite thumbs[] = { st::mvDocBlue, st::mvDocGreen, st::mvDocRed, st::mvDocYellow };
			_docIcon = thumbs[colorIndex];

			int32 extmaxw = (st::mvDocIconSize - st::mvDocExtPadding * 2);
			_docExtWidth = st::mvDocExtFont->width(_docExt);
			if (_docExtWidth > extmaxw) {
				_docExt = st::mvDocNameFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
				_docExtWidth = st::mvDocNameFont->width(_docExt);
			}
		} else {
			_doc->thumb->load();
			int32 tw = _doc->thumb->width(), th = _doc->thumb->height();
			if (!tw || !th) {
				_docThumbx = _docThumby = _docThumbw = 0;
			} else if (tw > th) {
				_docThumbw = (tw * st::mvDocIconSize) / th;
				_docThumbx = (_docThumbw - st::mvDocIconSize) / 2;
				_docThumby = 0;
			} else {
				_docThumbw = st::mvDocIconSize;
				_docThumbx = 0;
				_docThumby = ((th * _docThumbw) / tw - st::mvDocIconSize) / 2;
			}
		}

		int32 maxw = st::mvDocSize.width() - st::mvDocIconSize - st::mvDocPadding * 3;

		if (_doc) {
			_docName = (_doc->type == StickerDocument) ? lang(lng_in_dlg_sticker) : (_doc->type == AnimatedDocument ? qsl("GIF") : (_doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name));
		} else {
			_docName = lang(lng_message_empty);
		}
		_docNameWidth = st::mvDocNameFont->width(_docName);
		if (_docNameWidth > maxw) {
			_docName = st::mvDocNameFont->elided(_docName, maxw, Qt::ElideMiddle);
			_docNameWidth = st::mvDocNameFont->width(_docName);
		}

		_docRadial.stop();
		// _docSize is updated in updateControls()

		_docRect = QRect((width() - st::mvDocSize.width()) / 2, (height() - st::mvDocSize.height()) / 2, st::mvDocSize.width(), st::mvDocSize.height());
		_docIconRect = myrtlrect(_docRect.x() + st::mvDocPadding, _docRect.y() + st::mvDocPadding, st::mvDocIconSize, st::mvDocIconSize);
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
	if ((_w > width()) || (_h > height())) {
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
	if (HistoryForwarded *fwd = item->toHistoryForwarded()) {
		_from = fwd->fromForwarded();
	} else {
		_from = item->from();
	}
	_full = 1;
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

void MediaView::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QRegion region(e->region());
	QVector<QRect> rs(region.rects());

	uint64 ms = getms();

	Painter p(this);

	bool name = false, icon = false;

	p.setClipRegion(region);

	// main bg
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setOpacity(st::mvBgOpacity);
	for (int i = 0, l = region.rectCount(); i < l; ++i) {
		p.fillRect(rs.at(i), st::mvBgColor->b);
	}
	p.setCompositionMode(m);

	// photo
	if (_photo) {
		int32 w = _width * cIntRetinaFactor();
		if (_full <= 0 && _photo->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->full->pixNoCache(w, h, true);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 1;
		} else if (_full < 0 && _photo->medium->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->medium->pixNoCache(w, h, true, true);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 0;
		} else if (_current.isNull() && _photo->thumb->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->thumb->pixNoCache(w, h, true, true);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
		} else if (_current.isNull()) {
			_current = _photo->thumb->pix();
		}
	}
	p.setOpacity(1);
	if (_photo || fileShown()) {
		QRect imgRect(_x, _y, _w, _h);
		if (imgRect.intersects(r)) {
			QPixmap toDraw = _current.isNull() ? _gif->current(_gif->width(), _gif->height(), _gif->width(), _gif->height(), ms) : _current;
			if (!_gif && (!_doc || !_doc->sticker() || _doc->sticker()->img->isNull()) && toDraw.hasAlpha()) {
				p.fillRect(imgRect, _transparentBrush);
			}
			if (toDraw.width() != _w * cIntRetinaFactor()) {
				bool was = (p.renderHints() & QPainter::SmoothPixmapTransform);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, true);
				p.drawPixmap(QRect(_x, _y, _w, _h), toDraw);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			} else {
				p.drawPixmap(_x, _y, toDraw);
			}

			uint64 ms = 0;
			if (_full < 1) {
				ms = getms();
				uint64 dt = ms - _animStarted;
				int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

				int32 x = (width() - st::mediaviewLoader.width()) / 2;
				int32 y = (height() - st::mediaviewLoader.height()) / 2;
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
				_saveMsgUpdater.start(AnimationTimerDelta);
			}
			if (_saveMsgStarted) {
				if (!ms) ms = getms();
				float64 dt = float64(ms) - _saveMsgStarted, hidingDt = dt - st::medviewSaveMsgShowing - st::medviewSaveMsgShown;
				if (dt < st::medviewSaveMsgShowing + st::medviewSaveMsgShown + st::medviewSaveMsgHiding) {
					if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5) {
						_saveMsgOpacity.start(0);
					}
					float64 progress = (hidingDt >= 0) ? (hidingDt / st::medviewSaveMsgHiding) : (dt / st::medviewSaveMsgShowing);
					_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
                    if (_saveMsgOpacity.current() > 0) {
						p.setOpacity(_saveMsgOpacity.current());
						App::roundRect(p, _saveMsg, st::medviewSaveMsg, MediaviewSaveCorners);
						p.drawPixmap(_saveMsg.topLeft() + st::medviewSaveMsgCheckPos, App::sprite(), st::medviewSaveMsgCheck);

						p.setPen(st::white->p);
						textstyleSet(&st::medviewSaveAsTextStyle);
						_saveMsgText.draw(p, _saveMsg.x() + st::medviewSaveMsgPadding.left(), _saveMsg.y() + st::medviewSaveMsgPadding.top(), _saveMsg.width() - st::medviewSaveMsgPadding.left() - st::medviewSaveMsgPadding.right());
						textstyleRestore();
						p.setOpacity(1);
					}
					if (_full >= 1) {
                        uint64 nextFrame = (dt < st::medviewSaveMsgShowing || hidingDt >= 0) ? int(AnimationTimerDelta) : (st::medviewSaveMsgShowing + st::medviewSaveMsgShown + 1 - dt);
						_saveMsgUpdater.start(nextFrame);
					}
				} else {
					_saveMsgStarted = 0;
				}
			}
		}
	} else {
		if (_docRect.intersects(r)) {
			p.fillRect(_docRect, st::mvDocBg->b);
			if (_docIconRect.intersects(r)) {
				bool radial = false;
				float64 radialOpacity = 0;
				if (_docRadial.animating()) {
					_docRadial.step(ms);
					radial = _docRadial.animating();
					radialOpacity = _docRadial.opacity();
				}
				icon = true;
				if (!_doc || _doc->thumb->isNull()) {
					p.fillRect(_docIconRect, _docIconColor->b);
					if ((!_doc || _doc->loaded()) && (!radial || radialOpacity < 1)) {
						p.drawSprite(_docIconRect.topLeft() + QPoint(rtl() ? 0 : (_docIconRect.width() - _docIcon.pxWidth()), 0), _docIcon);
						p.setPen(st::mvDocExtColor->p);
						p.setFont(st::mvDocExtFont->f);
						if (!_docExt.isEmpty()) {
							p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2, _docIconRect.y() + st::mvDocExtTop + st::mvDocExtFont->ascent, _docExt);
						}
					}
				} else {
					int32 rf(cIntRetinaFactor());
					p.drawPixmap(_docIconRect.topLeft(), _doc->thumb->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mvDocIconSize * rf, st::mvDocIconSize * rf));
				}

				float64 o = overLevel(OverIcon);
				if (radial) {
					if (!_doc->loaded() && radialOpacity < 1) {
						p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity) * (1 - radialOpacity));
						p.drawSpriteCenter(_docIconRect, st::radialDownload);
					}

					QRect inner(QPoint(_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2), _docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)), st::radialSize);
					p.setPen(Qt::NoPen);
					p.setBrush(st::black);
					p.setOpacity(radialOpacity * st::radialBgOpacity);

					p.setRenderHint(QPainter::HighQualityAntialiasing);
					p.drawEllipse(inner);
					p.setRenderHint(QPainter::HighQualityAntialiasing, false);

					p.setOpacity((o * 1. + (1 - o) * st::radialCancelOpacity) * radialOpacity);
					p.drawSpriteCenter(_docIconRect, st::radialCancel);
					p.setOpacity(1);

					QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
					_docRadial.draw(p, arc, st::radialLine, st::white);
				} else if (_doc && !_doc->loaded()) {
					p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity));
					p.drawSpriteCenter(_docIconRect, st::radialDownload);
				}
			}

			if (!_docIconRect.contains(r)) {
				name = true;
				p.setPen(st::mvDocNameColor->p);
				p.setFont(st::mvDocNameFont->f);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocNameTop, width(), _docName, _docNameWidth);

				p.setPen(st::mvDocSizeColor->p);
				p.setFont(st::mvFont->f);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocSizeTop, width(), _docSize, _docSizeWidth);
			}
		}
	}

	float64 co = a_cOpacity.current();
	if (co > 0) {
		// left nav bar
		if (_leftNav.intersects(r) && _leftNavVisible) {
			float64 o = overLevel(OverLeftNav);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					QRect fill(_leftNav.intersected(rs.at(i)));
					if (!fill.isEmpty()) p.fillRect(fill, st::black->b);
				}
			}
			if (_leftNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				p.drawPixmap(_leftNavIcon.topLeft(), App::sprite(), st::mvLeft);
			}
		}

		// right nav bar
		if (_rightNav.intersects(r) && _rightNavVisible) {
			float64 o = overLevel(OverRightNav);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					QRect fill(_rightNav.intersected(rs.at(i)));
					if (!fill.isEmpty()) p.fillRect(fill, st::black->b);
				}
			}
			if (_rightNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				p.drawPixmap(_rightNavIcon.topLeft(), App::sprite(), st::mvRight);
			}
		}

		// close button
		if (_closeNav.intersects(r)) {
			float64 o = overLevel(OverClose);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					QRect fill(_closeNav.intersected(rs.at(i)));
					if (!fill.isEmpty()) p.fillRect(fill, st::black->b);
				}
			}
			if (_closeNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				p.drawPixmap(_closeNavIcon.topLeft(), App::sprite(), st::mvClose);
			}
		}

		// save button
		if (_saveVisible && _saveNavIcon.intersects(r)) {
			float64 o = overLevel(OverSave);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawPixmap(_saveNavIcon.topLeft(), App::sprite(), st::mvSave);
		}

		// more area
		if (_moreNavIcon.intersects(r)) {
			float64 o = overLevel(OverMore);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawPixmap(_moreNavIcon.topLeft(), App::sprite(), st::mvMore);
		}

		p.setPen(st::white->p);
		p.setFont(st::mvThickFont->f);

		// header
		if (_headerNav.intersects(r)) {
			float64 o = _headerHasLink ? overLevel(OverHeader) : 0;
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawText(_headerNav.left(), _headerNav.top() + st::mvThickFont->ascent, _headerText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_headerNav.left(), _headerNav.top() + st::mvThickFont->ascent + 1, _headerNav.right(), _headerNav.top() + st::mvThickFont->ascent + 1);
			}
		}

		p.setFont(st::mvFont->f);

		// name
		if (_from && _nameNav.intersects(r)) {
			float64 o = overLevel(OverName);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			_fromName.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_nameNav.left(), _nameNav.top() + st::mvFont->ascent + 1, _nameNav.right(), _nameNav.top() + st::mvFont->ascent + 1);
			}
		}

		// date
		if (_dateNav.intersects(r)) {
			float64 o = overLevel(OverDate);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawText(_dateNav.left(), _dateNav.top() + st::mvFont->ascent, _dateText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_dateNav.left(), _dateNav.top() + st::mvFont->ascent + 1, _dateNav.right(), _dateNav.top() + st::mvFont->ascent + 1);
			}
		}

		// caption
		if (!_caption.isEmpty()) {
			QRect outer(_captionRect.marginsAdded(st::mvCaptionPadding));
			if (outer.intersects(r)) {
				p.setOpacity(co);
				p.setBrush(st::mvCaptionBg->b);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(outer, st::mvCaptionRadius, st::mvCaptionRadius);
				if (_captionRect.intersects(r)) {
					textstyleSet(&st::medviewSaveAsTextStyle);
					p.setPen(st::white->p);
					_caption.drawElided(p, _captionRect.x(), _captionRect.y(), _captionRect.width(), _captionRect.height() / st::mvCaptionFont->height);
					textstyleRestore();
				}
			}
		}
	}
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (!_menu && e->key() == Qt::Key_Escape) {
		close();
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSaveAs();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		onCopy();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		if (_doc && !_doc->loading() && !fileShown()) {
			onDocClick();
		}
	} else if (e->key() == Qt::Key_Left) {
		moveToNext(-1);
	} else if (e->key() == Qt::Key_Right) {
		moveToNext(1);
    } else if (e->modifiers().testFlag(Qt::ControlModifier) && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == ']' || e->key() == Qt::Key_Asterisk || e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore || e->key() == Qt::Key_0)) {
		int32 newZoom = _zoom;
        if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Asterisk || e->key() == ']') {
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
		} else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore) {
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
		} else {
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
		}
		if (newZoom != ZoomToScreenLevel) {
			while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h) {
				++newZoom;
			}
		}
		if (_zoom != newZoom) {
			float64 nx, ny, z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
			_w = gifShown() ? _gif->width() : (_current.width() / cIntRetinaFactor());
			_h = gifShown() ? _gif->height() : (_current.height() / cIntRetinaFactor());
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
	}
}

void MediaView::moveToNext(int32 delta) {
	if (_index < 0 || (_history && _overview != OverviewPhotos && _overview != OverviewDocuments) || (_overview == OverviewCount && !_user)) {
		return;
	}
	if (_msgmigrated && !_history->overviewLoaded(_overview)) {
		return;
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
			if (HistoryItem *item = App::histItemById(newMigrated ? 0 : _channel, (newMigrated ? _migrated : _history)->overview[_overview][newIndex])) {
				_index = newIndex;
				_msgid = item->id;
				_msgmigrated = (item->history() == _migrated);
				_channel = _history ? _history->channelId() : NoChannel;
				_canForward = _msgid > 0;
				_canDelete = item->canDelete();
				stopGif();
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: displayPhoto(static_cast<HistoryPhoto*>(item->getMedia())->photo(), item); preloadData(delta); break;
					case MediaTypeDocument:
					case MediaTypeGif:
					case MediaTypeSticker: displayDocument(media->getDocument(), item); preloadData(delta); break;
					}
				} else {
					displayDocument(0, item);
					preloadData(delta);
				}
			}
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
}

void MediaView::preloadData(int32 delta) {
	if (_index < 0 || (!_user && _overview == OverviewCount)) return;

	int32 from = _index + (delta ? delta : -1), to = _index + (delta ? delta * MediaOverviewPreloadCount : 1);
	if (from > to) qSwap(from, to);
	if (_history && _overview != OverviewCount) {
		int32 forgetIndex = _index - delta * 2;
		History *forgetHistory = _msgmigrated ? _migrated : _history;
		if (_migrated) {
			if (_msgmigrated && forgetIndex >= _migrated->overview[_overview].size()) {
				forgetHistory = _history;
				forgetIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
			} else if (!_msgmigrated && forgetIndex < 0) {
				forgetHistory = _migrated;
				forgetIndex += _migrated->overview[_overview].size();
			}
		}
		if (forgetIndex >= 0 && forgetIndex < forgetHistory->overview[_overview].size() && (forgetHistory != (_msgmigrated ? _migrated : _history) || forgetIndex != _index)) {
			if (HistoryItem *item = App::histItemById(forgetHistory->channelId(), forgetHistory->overview[_overview][forgetIndex])) {
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->forget(); break;
					case MediaTypeDocument:
					case MediaTypeGif:
					case MediaTypeSticker: media->getDocument()->forget(); break;
					}
				}
			}
		}

		for (int32 i = from; i <= to; ++i) {
			History *previewHistory = _msgmigrated ? _migrated : _history;
			int32 previewIndex = i;
			if (_migrated) {
				if (_msgmigrated && previewIndex >= _migrated->overview[_overview].size()) {
					previewHistory = _history;
					previewIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
				} else if (!_msgmigrated && previewIndex < 0) {
					previewHistory = _migrated;
					previewIndex += _migrated->overview[_overview].size();
				}
			}
			if (previewIndex >= 0 && previewIndex < previewHistory->overview[_overview].size() && (previewHistory != (_msgmigrated ? _migrated : _history) || previewIndex != _index)) {
				if (HistoryItem *item = App::histItemById(previewHistory->channelId(), previewHistory->overview[_overview][previewIndex])) {
					if (HistoryMedia *media = item->getMedia()) {
						switch (media->type()) {
						case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->download(); break;
						case MediaTypeDocument:
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
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->thumb->load();
			}
		}
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->download();
			}
		}
		int32 forgetIndex = _index - delta * 2;
		if (forgetIndex >= 0 && forgetIndex < _user->photos.size() && forgetIndex != _index) {
			_user->photos[forgetIndex]->forget();
		}
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_menu || !_receiveMouse) return;

	if (textlnkDown() != textlnkOver()) {
		textlnkDown(textlnkOver());
	}

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (!textlnkDown()) {
			if (_over == OverLeftNav && _index >= 0) {
				moveToNext(-1);
				_lastAction = e->pos();
			} else if (_over == OverRightNav && _index >= 0) {
				moveToNext(1);
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
			} else if (!_saveMsg.contains(e->pos()) || !_saveMsgStarted) {
				_pressed = true;
				_dragging = 0;
				setCursor(style::cur_default);
				_mStart = e->pos();
				_xStart = _x;
				_yStart = _y;
			}
		}
	}
	activateControls();
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
	bool moved = (e->pos() != _lastMouseMovePos);
	_lastMouseMovePos = e->pos();

	updateOver(e->pos());
	if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::mvDeltaFromLastAction) {
		_lastAction = QPoint(-st::mvDeltaFromLastAction, -st::mvDeltaFromLastAction);
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
	if (moved) activateControls();
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
			QTimer::singleShot(0, this, SLOT(onDropdown()));
		}
		updateOverRect(_over);
		updateOverRect(newState);
		if (_over != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(0);
			} else {
				_animOpacities.insert(_over, anim::fvalue(1, 0));
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
				_animOpacities.insert(_over, anim::fvalue(0, 1));
			}
			if (!_a_state.animating()) _a_state.start();
			setCursor(style::cur_pointer);
		} else {
			setCursor(style::cur_default);
		}
	}
	return result;
}

void MediaView::updateOver(QPoint pos) {
	TextLinkPtr lnk;
	bool inText;

	if (_saveMsgStarted && _saveMsg.contains(pos)) {
        _saveMsgText.getState(lnk, inText, pos.x() - _saveMsg.x() - st::medviewSaveMsgPadding.left(), pos.y() - _saveMsg.y() - st::medviewSaveMsgPadding.top(), _saveMsg.width() - st::medviewSaveMsgPadding.left() - st::medviewSaveMsgPadding.right());
	} else if (_captionRect.contains(pos)) {
		_caption.getState(lnk, inText, pos.x() - _captionRect.x(), pos.y() - _captionRect.y(), _captionRect.width());
	}

	// retina
	if (pos.x() == width()) {
		pos.setX(pos.x() - 1);
	}
	if (pos.y() == height()) {
		pos.setY(pos.y() - 1);
	}

	if (lnk != textlnkOver()) {
		textlnkOver(lnk);
		setCursor((textlnkOver() || textlnkDown()) ? style::cur_pointer : style::cur_default);
		update(QRegion(_saveMsg) + _captionRect);
	}

	if (_pressed || _dragging) return;

	if (_leftNavVisible && _leftNav.contains(pos)) {
		updateOverState(OverLeftNav);
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		updateOverState(OverRightNav);
	} else if (_nameNav.contains(pos)) {
		updateOverState(OverName);
	} else if (_msgid && _dateNav.contains(pos)) {
		updateOverState(OverDate);
	} else if (_headerHasLink && _headerNav.contains(pos)) {
		updateOverState(OverHeader);
	} else if (_saveVisible && _saveNav.contains(pos)) {
		updateOverState(OverSave);
	} else if (_doc && !fileShown() && _docIconRect.contains(pos)) {
		updateOverState(OverIcon);
	} else if (_moreNav.contains(pos)) {
		updateOverState(OverMore);
	} else if (_closeNav.contains(pos)) {
		updateOverState(OverClose);
	} else if (_over != OverNone) {
		updateOverState(OverNone);
	}
}

void MediaView::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());
	TextLinkPtr lnk = textlnkDown();
	textlnkDown(TextLinkPtr());
	if (lnk && textlnkOver() == lnk) {
		if (reHashtag().match(lnk->encoded()).hasMatch() && _history && _history->isChannel() && !_history->isMegagroup()) {
			App::wnd()->hideMediaview();
			App::searchByHashtag(lnk->encoded(), _history->peer);
		} else {
			if (reBotCommand().match(lnk->encoded()).hasMatch() && _history) {
				App::wnd()->hideMediaview();
				Ui::showPeerHistory(_history, ShowAtTheEndMsgId);
			}
			lnk->onClick(e->button());
		}
		return;
	}
	if (_over == OverName && _down == OverName) {
		if (App::wnd() && _from) {
			close();
			if (App::main()) App::main()->showPeerProfile(_from);
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
		} else if ((e->pos() - _lastAction).manhattanLength() >= st::mvDeltaFromLastAction && (!_doc || fileShown() || !_docRect.contains(e->pos()))) {
			close();
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
		_menu = new PopupMenu(st::mvPopupMenu);
		updateDropdown();
		for (int32 i = 0, l = _btns.size(); i < l; ++i) {
			if (!_btns.at(i)->isHidden()) _menu->addAction(_btns.at(i)->getText(), _btns.at(i), SIGNAL(clicked()))->setEnabled(true);
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
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::mvSwipeDistance || d.x() < -st::mvSwipeDistance)) {
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
	return QWidget::event(e);
}

void MediaView::hide() {
	_controlsHideTimer.stop();
	_controlsState = ControlsShown;
	a_cOpacity = anim::fvalue(1, 1);
	QWidget::hide();
	stopGif();

	Notify::clipStopperHidden(ClipStopperMediaview);
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
	updateDropdown();
	_dropdown.ignoreShow(false);
	_dropdown.showStart();
	_dropdown.setFocus();
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
	if (_loadRequest || _index < 0 || (_overview == OverviewCount && !_user)) return;

	if (_history && _overview != OverviewCount && (!_history->overviewLoaded(_overview) || (_migrated && !_migrated->overviewLoaded(_overview)))) {
		if (App::main()) {
			if (_msgmigrated || (_migrated && _index == 0 && _history->overviewLoaded(_overview))) {
				App::main()->loadMediaBack(_migrated->peer, _overview);
			} else {
				App::main()->loadMediaBack(_history->peer, _overview);
				if (_migrated && _index == 0 && _migrated->overview[_overview].isEmpty() && !_migrated->overviewLoaded(_overview)) {
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

void MediaView::userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPPhoto> *v = 0;
	switch (photos.type()) {
	case mtpc_photos_photos: {
		const MTPDphotos_photos &d(photos.c_photos_photos());
		App::feedUsers(d.vusers);
		v = &d.vphotos.c_vector().v;
		u->photosCount = 0;
	} break;

	case mtpc_photos_photosSlice: {
		const MTPDphotos_photosSlice &d(photos.c_photos_photosSlice());
		App::feedUsers(d.vusers);
		u->photosCount = d.vcount.v;
		v = &d.vphotos.c_vector().v;
	} break;

	default: return;
	}

	if (v->isEmpty()) {
		u->photosCount = 0;
	}

	for (QVector<MTPPhoto>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		PhotoData *photo = App::feedPhoto(*i);
		photo->thumb->load();
		u->photos.push_back(photo);
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(u, OverviewCount);
}

void MediaView::updateHeader() {
	int32 index = _index, count = 0, addcount = (_migrated && _overview != OverviewCount) ? _migrated->overviewCount(_overview) : 0;
	if (_history) {
		if (_overview != OverviewCount) {
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
			} else {
				count = 0; // unknown yet
			}
		}
	} else if (_user) {
		count = _user->photosCount ? _user->photosCount : _user->photos.size();
	}
	if (_index >= 0 && _index < count && count > 1) {
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
	_headerHasLink = (_overview != OverviewCount) && _history;
	int32 hwidth = st::mvThickFont->width(_headerText);
	if (hwidth > width() / 3) {
		hwidth = width() / 3;
		_headerText = st::mvThickFont->elided(_headerText, hwidth, Qt::ElideMiddle);
	}
	_headerNav = myrtlrect(st::mvTextLeft, height() - st::mvHeaderTop, hwidth, st::mvThickFont->height);
}

QColor MediaView::overColor(const QColor &a, float64 ca, const QColor &b, float64 cb) {
	QColor res;
	float64 o = a.alphaF() * ca + b.alphaF() * cb - a.alphaF() * ca * b.alphaF() * cb;
	float64 ka = (o > 0.001) ? (a.alphaF() * ca * (1 - (b.alphaF() * cb)) / o) : 0;
	float64 kb = (o > 0.001) ? (b.alphaF() * cb / o) : 0;
	res.setRedF(a.redF() * ka + b.redF() * kb);
	res.setGreenF(a.greenF() * ka + b.greenF() * kb);
	res.setBlueF(a.blueF() * ka + b.blueF() * kb);
	res.setAlphaF(o);
	return res;
}

float64 MediaView::overLevel(OverState control) {
	ShowingOpacities::const_iterator i = _animOpacities.constFind(control);
	return (i == _animOpacities.cend()) ? (_over == control ? 1 : 0) : i->current();
}
