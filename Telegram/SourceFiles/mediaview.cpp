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

MediaView::MediaView() : TWidget(App::wnd()),
_photo(0), _doc(0), _overview(OverviewCount),
_leftNavVisible(false), _rightNavVisible(false), _saveVisible(false), _headerHasLink(false), _animStarted(getms()),
_width(0), _x(0), _y(0), _w(0), _h(0), _xStart(0), _yStart(0),
_zoom(0), _zoomToScreen(0), _pressed(false), _dragging(0), _full(-1),
_docNameWidth(0), _docSizeWidth(0),
_docThumbx(0), _docThumby(0), _docThumbw(0),
_docRadialFirst(0), _docRadialStart(0), _docRadialLast(0), _docRadialOpacity(1), a_docRadialStart(0, 1),
_docDownload(this, lang(lng_media_download), st::mvDocLink),
_docSaveAs(this, lang(lng_mediaview_save_as), st::mvDocLink),
_docCancel(this, lang(lng_cancel), st::mvDocLink),
_history(0), _peer(0), _user(0), _from(0), _index(-1), _msgid(0), _channel(NoChannel), _canForward(false), _canDelete(false),
_loadRequest(0), _over(OverNone), _down(OverNone), _lastAction(-st::mvDeltaFromLastAction, -st::mvDeltaFromLastAction), _ignoringDropdown(false),
_controlsState(ControlsShown), _controlsAnimStarted(0),
_menu(0), _dropdown(this, st::mvDropdown), _receiveMouse(true), _touchPress(false), _touchMove(false), _touchRightButton(false),
_saveMsgStarted(0), _saveMsgOpacity(0)
{
	TextCustomTagsMap custom;
	custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
	_saveMsgText.setRichText(st::medviewSaveMsgFont, lang(lng_mediaview_saved), _textDlgOptions, custom);
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::medviewSaveMsgPadding.left() + st::medviewSaveMsgPadding.right(), st::medviewSaveMsgFont->height + st::medviewSaveMsgPadding.top() + st::medviewSaveMsgPadding.bottom());
	_saveMsgText.setLink(1, TextLinkPtr(new SaveMsgLink(this)));

	_transparentBrush = QBrush(App::sprite().copy(st::mvTransparentBrush));
	_docRadialPen = QPen(st::white->p);
	_docRadialPen.setWidth(st::radialLine);

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

	connect(&_currentGif, SIGNAL(updated()), this, SLOT(onGifUpdated()));

	_btns.push_back(_btnSaveCancel = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_cancel))));
	connect(_btnSaveCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));
	_btns.push_back(_btnToMessage = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_context_to_msg))));
	connect(_btnToMessage, SIGNAL(clicked()), this, SLOT(onToMessage()));
	_btns.push_back(_btnShowInFolder = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(cPlatform() == dbipMac ? lng_context_show_in_finder : lng_context_show_in_folder))));
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
	QRect avail = App::app() ? App::app()->desktop()->screenGeometry(wndCenter) : QDesktopWidget().screenGeometry(wndCenter);
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
	if (_history && _history->peer == peer && type == _overview) {
		_index = -1;
		for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
			if (_history->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
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

void MediaView::documentUpdated(DocumentData *doc) {
	if (_doc && _doc == doc && _current.isNull() && _currentGif.isNull()) {
		if ((_doc->loader && _docCancel.isHidden()) || (!_doc->loader && !_docCancel.isHidden())) {
			updateControls();
		} else if (_doc->loader) {
			updateDocSize();
			update(_docRect);
		}
	}
}

void MediaView::onGifUpdated() {
	update(_x, _y, _w, _h);
}

void MediaView::changingMsgId(HistoryItem *row, MsgId newId) {
	if (row->id == _msgid) {
		_msgid = newId;
	}
	mediaOverviewUpdated(row->history()->peer, _overview);
}

void MediaView::updateDocSize() {
	if (!_doc || !_current.isNull() || !_currentGif.isNull()) return;

	if (_doc->loader) {
		quint64 ready = _doc->loader->currentOffset(), total = _doc->size;
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
	int32 maxw = st::mvDocSize.width() - st::mvDocBlue.pxWidth() - st::mvDocPadding * 3;
	if (_docSizeWidth > maxw) {
		_docSize = st::mvFont->elided(_docSize, maxw);
		_docSizeWidth = st::mvFont->width(_docSize);
	}
}

void MediaView::updateControls() {
	if (_doc && _current.isNull() && _currentGif.isNull()) {
		if (_doc->loader) {
			_docDownload.hide();
			_docSaveAs.hide();
			_docCancel.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocBlue.pxWidth(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
			_docCancel.show();
			if (!_docRadialFirst) _docRadialFirst = _docRadialLast = _docRadialStart = getms();
			if (!animating()) anim::start(this);
			anim::step(this);
		} else {
			if (_doc->already(true).isEmpty()) {
				_docDownload.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocBlue.pxWidth(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docDownload.show();
				_docSaveAs.moveToLeft(_docRect.x() + 2.5 * st::mvDocPadding + st::mvDocBlue.pxWidth() + _docDownload.width(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docSaveAs.show();
				_docCancel.hide();
			} else {
				_docDownload.hide();
				_docSaveAs.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocBlue.pxWidth(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
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

	_saveVisible = ((_photo && _photo->full->loaded()) || (_doc && (!_doc->already(true).isEmpty() || (_current.isNull() && _currentGif.isNull() && (_photo || _doc)))));
	_saveNav = myrtlrect(width() - st::mvIconSize.width() * 2, height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_saveNavIcon = centersprite(_saveNav, st::mvSave);
	_moreNav = myrtlrect(width() - st::mvIconSize.width(), height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_moreNavIcon = centersprite(_moreNav, st::mvMore);

	QDateTime d, dNow(date(unixtime()));
	if (_photo) {
		d = date(_photo->date);
	} else if (_doc) {
		d = date(_doc->date);
	} else if (HistoryItem *item = App::histItemById(_channel, _msgid)) {
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
		_fromName.setText(st::mvFont, _from->name);
		_nameNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, qMin(_fromName.maxWidth(), width() / 3), st::mvFont->height);
		_dateNav = myrtlrect(st::mvTextLeft + _nameNav.width() + st::mvTextSkip, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	}
	updateHeader();
	if (_photo || (_history && _overview == OverviewPhotos)) {
		_leftNavVisible = (_index > 0) || (_index == 0 && _history && _history->overview[_overview].size() < _history->overviewCount[_overview]);
		_rightNavVisible = (_index >= 0) && (
			(_history && _index + 1 < _history->overview[_overview].size()) ||
			(_user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount)));
	} else if (_history && _overview == OverviewDocuments) {
		_leftNavVisible = (_index > 0) || (_index == 0 && _history && _history->overview[_overview].size() < _history->overviewCount[_overview]);
		_rightNavVisible = (_index >= 0) && _history && (_index + 1 < _history->overview[_overview].size());
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
	_btnSaveCancel->setVisible(_doc && _doc->loader);
	_btnToMessage->setVisible(_msgid > 0);
	_btnShowInFolder->setVisible(_doc && !_doc->already(true).isEmpty());
	_btnSaveAs->setVisible(true);
	_btnCopy->setVisible((_doc && (!_current.isNull() || !_currentGif.isNull())) || (_photo && _photo->full->loaded()));
	_btnForward->setVisible(_canForward);
	_btnDelete->setVisible(_canDelete || (_photo && App::self() && App::self()->photoId == _photo->id) || (_photo && _photo->peer && _photo->peer->photoId == _photo->id && (_photo->peer->isChat() || (_photo->peer->isChannel() && _photo->peer->asChannel()->amCreator()))));
	_btnViewAll->setVisible((_overview != OverviewCount) && _history);
	_btnViewAll->setText(lang(_doc ? lng_mediaview_files_all : lng_mediaview_photos_all));
	_dropdown.updateButtons();
	_dropdown.moveToRight(0, height() - _dropdown.height());
}

bool MediaView::animStep(float64 msp) {
	bool result = false;
	uint64 ms = getms();
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
	if (_doc && _docRadialStart > 0) {
		float64 prg = _doc->loader ? qMax(_doc->loader->currentProgress(), 0.0001) : (_doc->status == FileFailed ? 0 : (_doc->already().isEmpty() ? 0 : 1));
		if (prg != a_docRadial.to()) {
			a_docRadial.start(prg);
			_docRadialStart = _docRadialLast;
		}
		_docRadialLast = ms;

		float64 dt = float64(ms - _docRadialStart), fulldt = float64(ms - _docRadialFirst);
		_docRadialOpacity = qMin(fulldt / st::radialDuration, 1.);
		if (_doc->loader) {
			a_docRadial.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
			result = true;
		} else if (dt >= st::radialDuration) {
			a_docRadial.update(1, anim::linear);
			result = true;
			_docRadialFirst = _docRadialLast = _docRadialStart = 0;
			a_docRadial = anim::fvalue(0, 0);
			if (!_doc->already().isEmpty() && _doc->size < MediaViewImageSizeLimit) {
				QString fname(_doc->already(true));
				QImageReader reader(fname);
				if (reader.canRead()) {
					displayDocument(_doc, App::histItemById(_channel, _msgid));
				}
			}
		} else {
			float64 r = dt / st::radialDuration;
			a_docRadial.update(r, anim::linear);
			result = true;
			_docRadialOpacity *= 1 - r;
		}
		float64 fromstart = fulldt / st::radialPeriod;
		a_docRadialStart.update(fromstart - qFloor(fromstart), anim::linear);
		update(_docIconRect);
	}
	return result || !_animations.isEmpty();
}

MediaView::~MediaView() {
	delete _menu;
}

void MediaView::showSaveMsgFile() {
	psShowInFolder(_saveMsgFilename);
}

void MediaView::close() {
	if (App::wnd()) {
		App::wnd()->hideLayer(true);
	}
}

void MediaView::activateControls() {
	_controlsHideTimer.start(int(st::mvWaitHide));
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) {
		_controlsState = ControlsShowing;
		_controlsAnimStarted = getms();
		a_cOpacity.start(1);
		if (!animating()) anim::start(this);
	}
}

void MediaView::onHideControls(bool force) {
	if (!force && !_dropdown.isHidden()) return;
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;
	_controlsState = ControlsHiding;
	_controlsAnimStarted = getms();
	a_cOpacity.start(0);
	if (!animating()) anim::start(this);
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
	if (_menu) _menu->fastHide();
	if (HistoryItem *item = _msgid ? App::histItemById(_channel, _msgid) : 0) {
		if (App::wnd()) {
			close();
			if (App::main()) App::main()->showPeerHistory(item->history()->peer->id, _msgid);
		}
	}
}

void MediaView::onSaveAs() {
	QString file;
	if (_doc) {
		QString cur = _doc->already(true);
		if (cur.isEmpty()) {
			if (_current.isNull() && _currentGif.isNull()) {
				DocumentSaveLink::doSave(_doc, true);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
			return;
		}

		QFileInfo alreadyInfo(cur);
		QDir alreadyDir(alreadyInfo.dir());
		QString name = alreadyInfo.fileName(), filter;
		MimeType mimeType = mimeTypeForName(_doc->mime);
		QStringList p = mimeType.globPatterns();
		QString pattern = p.isEmpty() ? QString() : p.front();
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
		}

		if (pattern.isEmpty()) {
			filter = qsl("All files (*.*)");
		} else {
			filter = mimeType.filterString() + qsl(";;All files (*.*)");
		}

		psBringToBack(this);
		file = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, true, alreadyDir);
		psShowOverAll(this);
		if (!file.isEmpty() && file != cur) {
			QFile(cur).copy(file);
		}
	} else {
		if (!_photo || !_photo->full->loaded()) return;

		psBringToBack(this);
		bool gotName = filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")));
		psShowOverAll(this);
		if (gotName) {
			if (!file.isEmpty()) {
				_photo->full->pix().toImage().save(file, "JPG");
			}
		}
	}
}

void MediaView::onDocClick() {
	QString fname = _doc->already(true);
	if (fname.isEmpty()) {
		if (_doc->loader) {
			onSaveCancel();
		} else {
			onDownload();
		}
	} else {
		psOpenFile(fname);
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
		QString cur = _doc->already(true);
		if (cur.isEmpty()) {
			if (_current.isNull() && _currentGif.isNull()) {
				DocumentSaveLink::doSave(_doc);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		} else {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogNextFilename(_doc->name, cur, path);
			if (toName != cur && !QFile(cur).copy(toName)) {
				toName = QString();
			}
		}
	} else {
		if (!_photo || !_photo->full->loaded()) {
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
	if (_doc && _doc->loader) {
		_doc->loader->cancel();
	}
}

void MediaView::onShowInFolder() {
	if (!_doc) return;
	QString already(_doc->already(true));
	if (!already.isEmpty()) psShowInFolder(already);
}

void MediaView::onForward() {
	HistoryItem *item = App::histItemById(_channel, _msgid);
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
		HistoryItem *item = App::histItemById(_channel, _msgid);
		if (item) {
			App::contextItem(item);
			App::main()->deleteLayer();
		}
	}
}

void MediaView::onOverview() {
	if (_menu) _menu->fastHide();
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
		} else if (!_currentGif.isNull()) {
			QApplication::clipboard()->setPixmap(_currentGif.current(_currentGif.w, _currentGif.h, false));
		}
	} else {
		if (!_photo || !_photo->full->loaded()) return;

		QApplication::clipboard()->setPixmap(_photo->full->pix());
	}
}

void MediaView::showPhoto(PhotoData *photo, HistoryItem *context) {
	_history = context ? context->history() : 0;
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
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_index = -1;
	_msgid = context ? context->id : 0;
	_channel = context ? context->channelId() : NoChannel;
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
	_history = 0;
	_peer = context;
	_user = context->asUser();
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_msgid = 0;
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
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_index = -1;
	_msgid = context ? context->id : 0;
	_channel = context ? context->channelId() : NoChannel;
	_canForward = _msgid > 0;
	_canDelete = context ? context->canDelete() : false;
	if (_history) {
		_overview = OverviewDocuments;

		for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
			if (_history->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
			}
		}

		if (_history->overviewCount[_overview] < 0) {
			loadBack();
		}
	}
	displayDocument(doc, context);
	preloadData(0);
	activateControls();
}

void MediaView::displayPhoto(PhotoData *photo, HistoryItem *item) {
	_photo = photo;
	_doc = 0;
	_zoom = 0;

	_caption = Text();
	if (HistoryMessage *itemMsg = item ? item->toHistoryMessage() : 0) {
		if (HistoryPhoto *photoMsg = dynamic_cast<HistoryPhoto*>(itemMsg->getMedia())) {
			_caption.setText(st::mvCaptionFont, photoMsg->captionForClone().original(0, 0xFFFF), (item->from()->isUser() && item->from()->asUser()->botInfo) ? _captionBotOptions : _captionTextOptions);
		}
	}

	_zoomToScreen = 0;
	MTP::clearLoaderPriorities();
	_full = -1;
	_current = QPixmap();
	_currentGif.stop();
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
	_photo->full->load();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
		psShowOverAll(this);
	}
}

void MediaView::displayDocument(DocumentData *doc, HistoryItem *item) { // empty messages shown as docs: doc can be NULL
	_doc = doc;
	_photo = 0;

	_caption = Text();
	if (_doc) {
		QString already = _doc->already(true);
		if (_doc->sticker() && !_doc->sticker()->img->isNull() && _doc->sticker()->img->loaded()) {
			_currentGif.stop();
			_current = _doc->sticker()->img->pix();
		} else if (!already.isEmpty()) {
			QImageReader reader(already);
			if (reader.canRead()) {
				if (reader.supportsAnimation() && reader.imageCount() > 1) {
					_currentGif.start(0, already);
					_current = QPixmap();
				} else {
					_currentGif.stop();
					QPixmap pix = QPixmap::fromImage(App::readImage(already, 0, false), Qt::ColorOnly);
					_current = pix;
				}
			} else {
				_currentGif.stop();
				_current = QPixmap();
			}
		} else {
			_currentGif.stop();
			_current = QPixmap();
		}
	} else {
		_currentGif.stop();
		_current = QPixmap();
	}

	if (_current.isNull() && _currentGif.isNull()) {
		if (!_doc || _doc->thumb->isNull()) {
			style::sprite thumbs[] = { st::mvDocBlue, st::mvDocGreen, st::mvDocRed, st::mvDocYellow };
			style::color colors[] = { st::mvDocBlueColor, st::mvDocGreenColor, st::mvDocRedColor, st::mvDocYellowColor };
			QString name = _doc ? _doc->name.toLower() : QString(), mime = _doc ? _doc->mime.toLower() : QString();
			if (name.endsWith(qstr(".doc")) ||
				name.endsWith(qstr(".txt")) ||
				name.endsWith(qstr(".psd")) ||
				mime.startsWith(qstr("text/"))
			) {
				_docIcon = thumbs[0];
				_docIconColor = colors[0];
			} else if (
				name.endsWith(qstr(".xls")) ||
				name.endsWith(qstr(".csv"))
			) {
				_docIcon = thumbs[1];
				_docIconColor = colors[1];
			} else if (
				name.endsWith(qstr(".pdf")) ||
				name.endsWith(qstr(".ppt")) ||
				name.endsWith(qstr(".key"))
			) {
				_docIcon = thumbs[2];
				_docIconColor = colors[2];
			} else if (
				name.endsWith(qstr(".zip")) ||
				name.endsWith(qstr(".rar")) ||
				name.endsWith(qstr(".ai")) ||
				name.endsWith(qstr(".mp3")) ||
				name.endsWith(qstr(".mov")) ||
				name.endsWith(qstr(".avi"))
			) {
				_docIcon = thumbs[3];
				_docIconColor = colors[3];
			} else {
				int ext = name.lastIndexOf('.');
				QChar ch = (ext >= 0 && ext + 1 < name.size()) ? name.at(ext + 1) : (name.isEmpty() ? (mime.isEmpty() ? '0' : mime.at(0)) : name.at(0));
				_docIcon = thumbs[ch.unicode() % 4];
				_docIconColor = colors[ch.unicode() % 4];
			}
		} else {
			_doc->thumb->load();
			int32 tw = _doc->thumb->width(), th = _doc->thumb->height();
			if (!tw || !th) {
				_docThumbx = _docThumby = _docThumbw = 0;
			} else if (tw > th) {
				_docThumbw = (tw * st::mvDocBlue.pxHeight()) / th;
				_docThumbx = (_docThumbw - st::mvDocBlue.pxWidth()) / 2;
				_docThumby = 0;
			} else {
				_docThumbw = st::mvDocBlue.pxWidth();
				_docThumbx = 0;
				_docThumby = ((th * _docThumbw) / tw - st::mvDocBlue.pxHeight()) / 2;
			}
		}

		int32 maxw = st::mvDocSize.width() - st::mvDocBlue.pxWidth() - st::mvDocPadding * 3;

		_docName = (!_doc || _doc->name.isEmpty()) ? lang(_doc ? (_doc->type == StickerDocument ? lng_in_dlg_sticker : lng_mediaview_doc_image) : lng_message_empty) : _doc->name;
		int32 lastDot = _docName.lastIndexOf('.');
		_docExt = _doc ? ((lastDot < 0 || lastDot + 2 > _docName.size()) ? _docName : _docName.mid(lastDot + 1)) : QString();
		_docNameWidth = st::mvDocNameFont->width(_docName);
		if (_docNameWidth > maxw) {
			_docName = st::mvDocNameFont->elided(_docName, maxw, Qt::ElideMiddle);
			_docNameWidth = st::mvDocNameFont->width(_docName);
		}

		int32 extmaxw = (st::mvDocBlue.pxWidth() - st::mvDocExtPadding * 2);

		_docExtWidth = st::mvDocExtFont->width(_docExt);
		if (_docExtWidth > extmaxw) {
			_docExt = st::mvDocNameFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
			_docExtWidth = st::mvDocNameFont->width(_docExt);
		}

		_docRadialFirst = _docRadialLast = _docRadialStart = 0;
		
		float64 prg = (_doc && _doc->loader) ? _doc->loader->currentProgress() : 0;
		a_docRadial = anim::fvalue(prg, qMax(prg, 0.0001));
		// _docSize is updated in updateControls()

		_docRect = QRect((width() - st::mvDocSize.width()) / 2, (height() - st::mvDocSize.height()) / 2, st::mvDocSize.width(), st::mvDocSize.height());
		_docIconRect = myrtlrect(_docRect.x() + st::mvDocPadding, _docRect.y() + st::mvDocPadding, st::mvDocBlue.pxWidth(), st::mvDocBlue.pxHeight());
	} else if (!_current.isNull()) {
		_current.setDevicePixelRatio(cRetinaFactor());
		_w = _current.width() / cIntRetinaFactor();
		_h = _current.height() / cIntRetinaFactor();
	} else {
		_w = _currentGif.w / cIntRetinaFactor();
		_h = _currentGif.h / cIntRetinaFactor();
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
	}
}

void MediaView::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QRegion region(e->region());
	QVector<QRect> rs(region.rects());
	if (rs.size() > 1) {
		int a = 0;
	}

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
		if (_full <= 0 && _photo->full->loaded()) {
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
		}
	}
	p.setOpacity(1);
	if (_photo || !_current.isNull() || !_currentGif.isNull()) {
		QRect imgRect(_x, _y, _w, _h);
		const QPixmap *toDraw = _currentGif.isNull() ? &_current : &_currentGif.current(_currentGif.w, _currentGif.h, false);
		if (imgRect.intersects(r)) {
			if (toDraw->hasAlpha() && (!_doc || !_doc->sticker() || _doc->sticker()->img->isNull())) {
				p.fillRect(imgRect, _transparentBrush);
			}
			if (_zoom) {
				bool was = (p.renderHints() & QPainter::SmoothPixmapTransform);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, true);
				p.drawPixmap(QRect(_x, _y, _w, _h), *toDraw);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			} else {
				p.drawPixmap(_x, _y, *toDraw);
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
				icon = true;
				if (!_doc || _doc->thumb->isNull()) {
					if ((!_doc || !_doc->already().isEmpty()) && (!_docRadialStart || _docRadialOpacity < 1)) {
						p.drawPixmap(_docIconRect.topLeft(), App::sprite(), _docIcon);
						p.setPen(st::mvDocExtColor->p);
						p.setFont(st::mvDocExtFont->f);
						if (!_docExt.isEmpty()) {
							p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2, _docIconRect.y() + st::mvDocExtTop + st::mvDocExtFont->ascent, _docExt);
						}
					} else {
						p.fillRect(_docIconRect, _docIconColor->b);
					}
				} else {
					int32 rf(cIntRetinaFactor());
					p.drawPixmap(_docIconRect.topLeft(), _doc->thumb->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mvDocBlue.pxWidth() * rf, st::mvDocBlue.pxHeight() * rf));
				}

				float64 o = overLevel(OverIcon);
				if (_doc && _docRadialStart > 0) {
					if (_doc->already().isEmpty() && _docRadialOpacity < 1) {
						p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity) * (1 - _docRadialOpacity));
						p.drawSpriteCenter(_docIconRect, st::radialDownload);
					}

					p.setRenderHint(QPainter::HighQualityAntialiasing);

					QRect inner(QPoint(_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2), _docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)), st::radialSize);
					p.setPen(Qt::NoPen);
					p.setBrush(st::black->b);
					p.setOpacity(_docRadialOpacity * st::radialBgOpacity);
					p.drawEllipse(inner);

					p.setOpacity((o * 1. + (1 - o) * st::radialCancelOpacity) * _docRadialOpacity);
					p.drawSpriteCenter(_docIconRect, st::radialCancel);

					QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));

					p.setOpacity(_docRadialOpacity);
					p.setPen(_docRadialPen);

					int len = 16 + a_docRadial.current() * 5744;
					p.drawArc(arc, 1440 - a_docRadialStart.current() * 5760 - len, len);

					p.setOpacity(1);
					p.setRenderHint(QPainter::HighQualityAntialiasing, false);
				} else if (_doc && _doc->already().isEmpty()) {
					p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity));
					p.drawSpriteCenter(_docIconRect, st::radialDownload);
				}
			}

			if (!_docIconRect.contains(r)) {
				name = true;
				p.setPen(st::mvDocNameColor->p);
				p.setFont(st::mvDocNameFont->f);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocBlue.pxWidth(), _docRect.y() + st::mvDocPadding + st::mvDocNameTop, width(), _docName, _docNameWidth);

				p.setPen(st::mvDocSizeColor->p);
				p.setFont(st::mvFont->f);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocBlue.pxWidth(), _docRect.y() + st::mvDocPadding + st::mvDocSizeTop, width(), _docSize, _docSizeWidth);
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
		if (_doc && !_doc->loader && _current.isNull() && _currentGif.isNull()) {
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
			_y = -(((_currentGif.isNull() ? _current.height() : _currentGif.h) / cIntRetinaFactor()) / 2);
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
			_w = (_currentGif.isNull() ? _current.width() : _currentGif.w) / cIntRetinaFactor();
			_h = (_currentGif.isNull() ? _current.height() : _currentGif.h) / cIntRetinaFactor();
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
	if (_index < 0 || (_history && _overview != OverviewPhotos && _overview != OverviewDocuments) || (_overview == OverviewCount && !_user)) return;

	int32 newIndex = _index + delta;
	if (_history && _overview != OverviewCount) {
		if (newIndex >= 0 && newIndex < _history->overview[_overview].size()) {
			_index = newIndex;
			if (HistoryItem *item = App::histItemById(_history->channelId(), _history->overview[_overview][_index])) {
				_msgid = item->id;
				_channel = item->channelId();
				_canForward = _msgid > 0;
				_canDelete = item->canDelete();
				if (item->getMedia()) {
					switch (item->getMedia()->type()) {
					case MediaTypePhoto: displayPhoto(static_cast<HistoryPhoto*>(item->getMedia())->photo(), item); preloadData(delta); break;
					case MediaTypeDocument: displayDocument(static_cast<HistoryDocument*>(item->getMedia())->document(), item); preloadData(delta); break;
					case MediaTypeSticker: displayDocument(static_cast<HistorySticker*>(item->getMedia())->document(), item); preloadData(delta); break;
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

	int32 from = _index + (delta ? delta : -1), to = _index + (delta ? delta * MediaOverviewPreloadCount : 1), forget = _index - delta * 2;
	if (from > to) qSwap(from, to);
	if (_history && _overview != OverviewCount) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _history->overview[_overview].size() && i != _index) {
				if (HistoryItem *item = App::histItemById(_history->channelId(), _history->overview[_overview][i])) {
					if (HistoryMedia *media = item->getMedia()) {
						switch (media->type()) {
						case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->full->load(); break;
						case MediaTypeDocument: static_cast<HistoryDocument*>(media)->document()->thumb->load(); break;
						case MediaTypeSticker: static_cast<HistorySticker*>(media)->document()->sticker()->img->load(); break;
						}
					}
				}
			}
		}
		if (forget >= 0 && forget < _history->overview[_overview].size() && forget != _index) {
			if (HistoryItem *item = App::histItemById(_history->channelId(), _history->overview[_overview][forget])) {
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->forget(); break;
					case MediaTypeDocument: static_cast<HistoryDocument*>(media)->document()->forget(); break;
					case MediaTypeSticker: static_cast<HistorySticker*>(media)->document()->forget(); break;
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
				_user->photos[i]->full->load();
			}
		}
		if (forget >= 0 && forget < _user->photos.size() && forget != _index) {
			_user->photos[forget]->forget();
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
			if (!animating()) anim::start(this);
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
			if (!animating()) anim::start(this);
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
	} else if (_doc && _current.isNull() && _currentGif.isNull() && _docIconRect.contains(pos)) {
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
		if (reHashtag().match(lnk->encoded()).hasMatch() && _history && _history->isChannel()) {
			App::wnd()->hideMediaview();
			App::searchByHashtag(lnk->encoded(), _history->peer);
		} else {
			if (reBotCommand().match(lnk->encoded()).hasMatch() && _history->peer->isUser() && _history->peer->asUser()->botInfo) {
				App::wnd()->hideMediaview();
				App::main()->showPeerHistory(_history->peer->id, ShowAtTheEndMsgId);
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
		} else if ((e->pos() - _lastAction).manhattanLength() >= st::mvDeltaFromLastAction && (!_doc || !_current.isNull() || !_currentGif.isNull() || !_docRect.contains(e->pos()))) {
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
		_menu = new ContextMenu(this, st::mvDropdown, st::mvContextButton);
		updateDropdown();
		for (int32 i = 0, l = _btns.size(); i < l; ++i) {
			if (!_btns.at(i)->isHidden()) _menu->addAction(_btns.at(i)->getText(), _btns.at(i), SIGNAL(clicked()))->setEnabled(true);
		}
		_menu->deleteOnHide();
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
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
}

void MediaView::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
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
	for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
		if (_history->overview[_overview].at(i) == _msgid) {
			_index = i;
			break;
		}
	}

	if (_history->overviewCount[_overview] < 0 || (!_index && _history->overviewCount[_overview] > 0)) {
		loadBack();
	}
}

void MediaView::loadBack() {
	if (_loadRequest || _index < 0 || (_overview == OverviewCount && !_user)) return;

	if (_history && _overview != OverviewCount && _history->overviewCount[_overview] != 0) {
		if (App::main()) App::main()->loadMediaBack(_history->peer, _overview);
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
	int32 index = _index, count = 0;
	if (_history) {
		if (_overview != OverviewCount) {
			count = _history->overviewCount[_overview] ? _history->overviewCount[_overview] : _history->overview[_overview].size();
			if (index >= 0) index += count - _history->overview[_overview].size();
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
		} else if (_channel) {
			_headerText = lang(lng_mediaview_group_photo);
		} else if (_peer) {
			_headerText = lang(lng_mediaview_channel_photo);
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
