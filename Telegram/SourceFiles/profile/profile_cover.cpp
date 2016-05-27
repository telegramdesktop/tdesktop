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
#include "profile/profile_cover.h"

#include "styles/style_profile.h"
#include "profile/profile_cover_drop_area.h"
#include "profile/profile_userpic_button.h"
#include "ui/buttons/round_button.h"
#include "ui/filedialog.h"
#include "observer_peer.h"
#include "boxes/confirmbox.h"
#include "boxes/contactsbox.h"
#include "boxes/photocropbox.h"
#include "lang.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"

namespace Profile {
namespace {

class OnlineCounter {
public:
	OnlineCounter() : _currentTime(unixtime()), _self(App::self()) {
	}
	void feedUser(UserData *user) {
		if (App::onlineForSort(user, _currentTime) > _currentTime) {
			++_result;
			if (user != _self) {
				_onlyMe = false;
			}
		}
	}
	QString result(int fullCount) const {
		if (_result > 0 && !_onlyMe) {
			return lng_chat_status_members_online(lt_count, fullCount, lt_count_online, _result);
		}
		return lng_chat_status_members(lt_count, fullCount);
	}

private:
	bool _onlyMe = true;
	int _result = 0;
	int _currentTime;
	UserData *_self;

};

const Notify::PeerUpdateFlags ButtonsUpdateFlags = Notify::PeerUpdateFlag::UserCanShareContact
	| Notify::PeerUpdateFlag::ChatCanEdit
	| Notify::PeerUpdateFlag::ChannelCanEditPhoto
	| Notify::PeerUpdateFlag::ChannelCanAddMembers
	| Notify::PeerUpdateFlag::ChannelAmIn;

} // namespace

CoverWidget::CoverWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _userpicButton(this, peer)
, _name(this, QString(), st::profileNameLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);

	_name.setSelectable(true);
	_name.setContextCopyText(lang(lng_profile_copy_fullname));

	auto observeEvents = ButtonsUpdateFlags | Notify::PeerUpdateFlag::NameChanged;
	Notify::registerPeerObserver(observeEvents, this, &CoverWidget::notifyPeerUpdated);
	FileDialog::registerObserver(this, &CoverWidget::notifyFileQueryUpdated);

	connect(_userpicButton, SIGNAL(clicked()), this, SLOT(onPhotoShow()));

	refreshNameText();
	refreshStatusText();

	refreshButtons();
}

void CoverWidget::onPhotoShow() {
	PhotoData *photo = (_peer->photoId && _peer->photoId != UnknownPeerPhotoId) ? App::photo(_peer->photoId) : nullptr;
	if ((_peer->photoId == UnknownPeerPhotoId) || (_peer->photoId && !photo->date)) {
		App::api()->requestFullPeer(_peer);
	}
	if (photo && photo->date) {
		App::wnd()->showPhoto(photo, _peer);
	}
}

void CoverWidget::resizeToWidth(int newWidth) {
	int newHeight = 0;

	newHeight += st::profileMarginTop;
	_userpicButton->moveToLeft(st::profilePhotoLeft, newHeight);

	int infoLeft = _userpicButton->x() + _userpicButton->width();
	int nameLeft = infoLeft + st::profileNameLeft - st::profileNameLabel.margin.left();
	int nameTop = _userpicButton->y() + st::profileNameTop - st::profileNameLabel.margin.top();
	_name.moveToLeft(nameLeft, nameTop);
	int nameWidth = width() - infoLeft - st::profileNameLeft - st::profileButtonSkip;
	nameWidth += st::profileNameLabel.margin.left() + st::profileNameLabel.margin.right();
	_name.resizeToWidth(nameWidth);

	_statusPosition = QPoint(infoLeft + st::profileStatusLeft, _userpicButton->y() + st::profileStatusTop);

	int buttonLeft = st::profilePhotoLeft + _userpicButton->width() + st::profileButtonLeft;
	for_const (auto button, _buttons) {
		button->moveToLeft(buttonLeft, st::profileButtonTop);
		buttonLeft += button->width() + st::profileButtonSkip;
	}

	newHeight += st::profilePhotoSize;
	newHeight += st::profileMarginBottom;

	_dividerTop = newHeight;
	newHeight += st::profileDividerFill.height();

	newHeight += st::profileBlocksTop;

	resizeDropArea();
	resize(newWidth, newHeight);
	update();
}

void CoverWidget::showFinished() {
	_userpicButton->showFinished();
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);

	p.setFont(st::profileStatusFont);
	p.setPen(st::profileStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);

	paintDivider(p);
}

void CoverWidget::resizeDropArea() {
	if (_dropArea) {
		_dropArea->setGeometry(0, 0, width(), _dividerTop);
	}
}

void CoverWidget::dropAreaHidden(CoverDropArea *dropArea) {
	if (_dropArea == dropArea) {
		_dropArea->deleteLater();
		_dropArea = nullptr;
	}
}

bool CoverWidget::canEditPhoto() const {
	if (_peerChat && _peerChat->canEdit()) {
		return true;
	} else if (_peerMegagroup && _peerMegagroup->canEditPhoto()) {
		return true;
	} else if (_peerChannel && _peerChannel->canEditPhoto()) {
		return true;
	}
	return false;
}

bool CoverWidget::mimeDataHasImage(const QMimeData *mimeData) const {
	if (!mimeData) return false;

	if (mimeData->hasImage()) return true;

	auto uriListFormat = qsl("text/uri-list");
	if (!mimeData->hasFormat(uriListFormat)) return false;

	auto &urls = mimeData->urls();
	if (urls.size() != 1) return false;

	auto &url = urls.at(0);
	if (!url.isLocalFile()) return false;

	auto file = psConvertFileUrl(url);

	QFileInfo info(file);
	if (info.isDir()) return false;

	quint64 s = info.size();
	if (s >= MaxUploadDocumentSize) return false;

	for (auto &ext : cImgExtensions()) {
		if (file.endsWith(ext, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void CoverWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!canEditPhoto() || !mimeDataHasImage(e->mimeData())) {
		e->ignore();
		return;
	}
	if (!_dropArea) {
		auto title = lang(lng_profile_drop_area_title);
		QString subtitle;
		if (_peerChat || _peerMegagroup) {
			subtitle = lang(lng_profile_drop_area_subtitle);
		} else {
			subtitle = lang(lng_profile_drop_area_subtitle_channel);
		}
		_dropArea = new CoverDropArea(this, title, subtitle);
		resizeDropArea();
	}
	_dropArea->showAnimated();
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void CoverWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dropArea && !_dropArea->hiding()) {
		_dropArea->hideAnimated(func(this, &CoverWidget::dropAreaHidden));
	}
}

void CoverWidget::dropEvent(QDropEvent *e) {
	auto mimeData = e->mimeData();

	QImage img;
	if (mimeData->hasImage()) {
		img = qvariant_cast<QImage>(mimeData->imageData());
	} else {
		auto &urls = mimeData->urls();
		if (urls.size() == 1) {
			auto &url = urls.at(0);
			if (url.isLocalFile()) {
				img = App::readImage(psConvertFileUrl(url));
			}
		}
	}

	if (!_dropArea->hiding()) {
		_dropArea->hideAnimated(func(this, &CoverWidget::dropAreaHidden));
	}
	e->acceptProposedAction();

	showSetPhotoBox(img);
}

void CoverWidget::paintDivider(Painter &p) {
	st::profileDividerLeft.paint(p, QPoint(st::lineWidth, _dividerTop), width());

	int toFillLeft = st::lineWidth + st::profileDividerLeft.width();
	QRect toFill = rtlrect(toFillLeft, _dividerTop, width() - toFillLeft, st::profileDividerFill.height(), width());
	st::profileDividerFill.fill(p, toFill);
}

void CoverWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != _peer) {
		return;
	}
	if ((update.flags & ButtonsUpdateFlags) != 0) {
		refreshButtons();
	}
	if (update.flags & Notify::PeerUpdateFlag::NameChanged) {
		refreshNameText();
	}
}

void CoverWidget::refreshNameText() {
	_name.setText(App::peerName(_peer));
	update();
}

void CoverWidget::refreshStatusText() {
	int currentTime = unixtime();
	if (_peerUser) {
		_statusText = App::onlineText(_peerUser, currentTime, true);
	} else if (_peerChat && _peerChat->amIn()) {
		if (_peerChat->noParticipantInfo()) {
			App::api()->requestFullPeer(_peer);
			if (_statusText.isEmpty()) {
				_statusText = lng_chat_status_members(lt_count, _peerChat->count);
			}
		} else {
			OnlineCounter counter;
			auto &participants = _peerChat->participants;
			for (auto i = participants.cbegin(), e = participants.cend(); i != e; ++i) {
				counter.feedUser(i.key());
			}
			_statusText = counter.result(participants.size());
		}
	} else if (isUsingMegagroupOnlineCount()) {
		OnlineCounter counter;
		for_const (auto user, _peerMegagroup->mgInfo->lastParticipants) {
			counter.feedUser(user);
		}
		_statusText = counter.result(_peerMegagroup->count);
	} else if (_peerChannel) {
		if (_peerChannel->count > 0) {
			_statusText = lng_chat_status_members(lt_count, _peerChannel->count);
		} else {
			_statusText = lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	} else {
		_statusText = lang(lng_chat_status_unaccessible);
	}
	update();
}

bool CoverWidget::isUsingMegagroupOnlineCount() const {
	if (!_peerMegagroup || !_peerMegagroup->amIn()) {
		return false;
	}

	if (_peerMegagroup->count <= 0 || _peerMegagroup->count > Global::ChatSizeMax()) {
		return false;
	}

	if (_peerMegagroup->mgInfo->lastParticipants.isEmpty() || _peerMegagroup->lastParticipantsCountOutdated()) {
		App::api()->requestLastParticipants(_peerMegagroup);
		return false;
	}

	return true;
}

void CoverWidget::refreshButtons() {
	clearButtons();
	if (_peerUser) {
		setUserButtons();
	} else if (_peerChat) {
		setChatButtons();
	} else if (_peerMegagroup) {
		setMegagroupButtons();
	} else if (_peerChannel) {
		setChannelButtons();
	}
	resizeToWidth(width());
}

void CoverWidget::setUserButtons() {
	addButton(lang(lng_profile_send_message), SLOT(onSendMessage()));
	if (_peerUser->canShareThisContact()) {
		addButton(lang(lng_profile_share_contact), SLOT(onShareContact()));
	}
}

void CoverWidget::setChatButtons() {
	if (_peerChat->canEdit()) {
		addButton(lang(lng_profile_set_group_photo), SLOT(onSetPhoto()));
		addButton(lang(lng_profile_add_participant), SLOT(onAddMember()));
	}
}

void CoverWidget::setMegagroupButtons() {
	if (_peerMegagroup->canEditPhoto()) {
		addButton(lang(lng_profile_set_group_photo), SLOT(onSetPhoto()));
	}
	if (_peerMegagroup->canAddParticipants()) {
		addButton(lang(lng_profile_add_participant), SLOT(onAddMember()));
	}
}

void CoverWidget::setChannelButtons() {
	if (_peerChannel->amCreator()) {
		addButton(lang(lng_profile_set_group_photo), SLOT(onSetPhoto()));
	} else if (_peerChannel->amIn()) {
		addButton(lang(lng_profile_view_channel), SLOT(onViewChannel()));
	} else {
		addButton(lang(lng_profile_join_channel), SLOT(onJoin()));
	}
}

void CoverWidget::clearButtons() {
	auto buttons = createAndSwap(_buttons);
	for_const (auto button, buttons) {
		delete button;
	}
}

void CoverWidget::addButton(const QString &text, const char *slot) {
	if (!text.isEmpty()) {
		auto &buttonStyle = _buttons.isEmpty() ? st::profilePrimaryButton : st::profileSecondaryButton;
		_buttons.push_back(new Ui::RoundButton(this, text, buttonStyle));
		connect(_buttons.back(), SIGNAL(clicked()), this, slot);
		_buttons.back()->show();
	}
}

void CoverWidget::onSendMessage() {
	Ui::showPeerHistory(_peer, ShowAtUnreadMsgId);
}

void CoverWidget::onShareContact() {
	App::main()->shareContactLayer(_peerUser);
}

void CoverWidget::onSetPhoto() {
	QStringList imgExtensions(cImgExtensions());
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	_setPhotoFileQueryId = FileDialog::queryReadFile(lang(lng_choose_images), filter);
}

void CoverWidget::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_setPhotoFileQueryId != update.queryId) {
		return;
	}
	_setPhotoFileQueryId = 0;

	if (update.filePaths.isEmpty() && update.remoteContent.isEmpty()) {
		return;
	}

	QImage img;
	if (!update.remoteContent.isEmpty()) {
		img = App::readImage(update.remoteContent);
	} else {
		img = App::readImage(update.filePaths.front());
	}

	showSetPhotoBox(img);
}

void CoverWidget::showSetPhotoBox(const QImage &img) {
	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		Ui::showLayer(new InformBox(lang(lng_bad_photo)));
		return;
	}

	auto box = new PhotoCropBox(img, _peer);
	connect(box, SIGNAL(closed()), this, SLOT(onPhotoUpdateStart()));
	Ui::showLayer(box);
}

void CoverWidget::onAddMember() {
	if (_peerChat) {
		Ui::showLayer(new ContactsBox(_peerChat, MembersFilterRecent));
	} else if (_peerChannel && _peerChannel->mgInfo) {
		MembersAlreadyIn already;
		for_const (auto user, _peerChannel->mgInfo->lastParticipants) {
			already.insert(user);
		}
		Ui::showLayer(new ContactsBox(_peerChannel, MembersFilterRecent, already));
	}
}

void CoverWidget::onJoin() {
	if (!_peerChannel) return;

	App::api()->joinChannel(_peerChannel);
}

void CoverWidget::onViewChannel() {
	Ui::showPeerHistory(_peer, ShowAtUnreadMsgId);
}

} // namespace Profile
