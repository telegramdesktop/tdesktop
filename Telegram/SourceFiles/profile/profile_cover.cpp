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
#include "ui/flatlabel.h"
#include "ui/flatbutton.h"
#include "observer_peer.h"
#include "boxes/confirmbox.h"
#include "boxes/contactsbox.h"
#include "boxes/photocropbox.h"
#include "lang.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"

namespace Profile {
namespace {

using UpdateFlag = Notify::PeerUpdate::Flag;
const auto ButtonsUpdateFlags = UpdateFlag::UserCanShareContact
	| UpdateFlag::BotCanAddToGroups
	| UpdateFlag::ChatCanEdit
	| UpdateFlag::ChannelCanEditPhoto
	| UpdateFlag::ChannelCanAddMembers
	| UpdateFlag::ChannelAmIn;

} // namespace

CoverWidget::CoverWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _userpicButton(this, peer)
, _name(this, st::profileNameLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);

	_name->setSelectable(true);
	_name->setContextCopyText(lang(lng_profile_copy_fullname));

	auto observeEvents = ButtonsUpdateFlags
		| UpdateFlag::NameChanged
		| UpdateFlag::UserOnlineChanged;
	Notify::registerPeerObserver(observeEvents, this, &CoverWidget::notifyPeerUpdated);
	FileDialog::registerObserver(this, &CoverWidget::notifyFileQueryUpdated);

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUploadStatusChanged(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUploadStatusChanged(PeerId)));

	connect(_userpicButton, SIGNAL(clicked()), this, SLOT(onPhotoShow()));
	validatePhoto();

	refreshNameText();
	refreshStatusText();

	refreshButtons();
}

PhotoData *CoverWidget::validatePhoto() const {
	PhotoData *photo = (_peer->photoId && _peer->photoId != UnknownPeerPhotoId) ? App::photo(_peer->photoId) : nullptr;
	if ((_peer->photoId == UnknownPeerPhotoId) || (_peer->photoId && (!photo || !photo->date))) {
		App::api()->requestFullPeer(_peer);
		return nullptr;
	}
	return photo;
}

void CoverWidget::onPhotoShow() {
	if (auto photo = validatePhoto()) {
		App::wnd()->showPhoto(photo, _peer);
	}
}

void CoverWidget::onCancelPhotoUpload() {
	if (auto app = App::app()) {
		app->cancelPhotoUpdate(_peer->id);
		refreshStatusText();
	}
}

int CoverWidget::countPhotoLeft(int newWidth) const {
	int result = st::profilePhotoLeftMin;
	result += (newWidth - st::wndMinWidth) / 2;
	return qMin(result, st::profilePhotoLeftMax);
}

void CoverWidget::resizeToWidth(int newWidth) {
	int newHeight = 0;

	newHeight += st::profileMarginTop;

	_photoLeft = countPhotoLeft(newWidth);
	_userpicButton->moveToLeft(_photoLeft, newHeight);

	refreshNameGeometry(newWidth);

	int infoLeft = _userpicButton->x() + _userpicButton->width();
	_statusPosition = QPoint(infoLeft + st::profileStatusLeft, _userpicButton->y() + st::profileStatusTop);
	if (_cancelPhotoUpload) {
		_cancelPhotoUpload->moveToLeft(_statusPosition.x() + st::profileStatusFont->width(_statusText) + st::profileStatusFont->spacew, _statusPosition.y());
	}

	moveAndToggleButtons(newWidth);

	newHeight += st::profilePhotoSize;
	newHeight += st::profileMarginBottom;

	_dividerTop = newHeight;
	newHeight += st::profileDividerFill.height();

	newHeight += st::profileBlocksTop;

	resizeDropArea();
	resize(newWidth, newHeight);
	update();
}

void CoverWidget::refreshNameGeometry(int newWidth) {
	int infoLeft = _userpicButton->x() + _userpicButton->width();
	int nameLeft = infoLeft + st::profileNameLeft - st::profileNameLabel.margin.left();
	int nameTop = _userpicButton->y() + st::profileNameTop - st::profileNameLabel.margin.top();
	int nameWidth = newWidth - infoLeft - st::profileNameLeft;
	if (_peer->isVerified()) {
		nameWidth -= st::profileVerifiedCheckPosition.x() + st::profileVerifiedCheck.width();
	}
	int marginsAdd = st::profileNameLabel.margin.left() + st::profileNameLabel.margin.right();
	_name->resizeToWidth(qMin(nameWidth - marginsAdd, _name->naturalWidth()) + marginsAdd);
	_name->moveToLeft(nameLeft, nameTop);
}

// A more generic solution would be allowing an optional icon button
// for each text button. But currently we use only one, so it is done easily:
// There can be primary + secondary + icon buttons. If primary + secondary fit,
// then icon is hidden, otherwise secondary is hidden and icon is shown.
void CoverWidget::moveAndToggleButtons(int newWiddth) {
	int buttonLeft = _userpicButton->x() + _userpicButton->width() + st::profileButtonLeft;
	int buttonsRight = newWiddth - st::profileButtonSkip;
	for (int i = 0, count = _buttons.size(); i < count; ++i) {
		auto &button = _buttons.at(i);
		button.widget->moveToLeft(buttonLeft, st::profileButtonTop);
		if (button.replacement) {
			button.replacement->moveToLeft(buttonLeft, st::profileButtonTop);
			if (buttonLeft + button.widget->width() > buttonsRight) {
				button.widget->hide();
				button.replacement->show();
				buttonLeft += button.replacement->width() + st::profileButtonSkip;
			} else {
				button.widget->show();
				button.replacement->hide();
				buttonLeft += button.widget->width() + st::profileButtonSkip;
			}
		} else if (i == 1 && (buttonLeft + button.widget->width() > buttonsRight)) {
			// If second button is not fitting.
			button.widget->hide();
		} else {
			button.widget->show();
			buttonLeft += button.widget->width() + st::profileButtonSkip;
		}
	}
}

void CoverWidget::showFinished() {
	_userpicButton->showFinished();
}

bool CoverWidget::shareContactButtonShown() const {
	return _peerUser && (_buttons.size() > 1) && !(_buttons.at(1).widget->isHidden());
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);

	p.setFont(st::profileStatusFont);
	p.setPen(_statusTextIsOnline ? st::profileStatusFgActive : st::profileStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);

	if (_peer->isVerified()) {
		st::profileVerifiedCheck.paint(p, QPoint(_name->x() + _name->width(), _name->y()) + st::profileVerifiedCheckPosition, width());
	}

	paintDivider(p);
}

void CoverWidget::resizeDropArea() {
	if (_dropArea) {
		_dropArea->setGeometry(0, 0, width(), _dividerTop);
	}
}

void CoverWidget::dropAreaHidden(CoverDropArea *dropArea) {
	if (_dropArea == dropArea) {
		_dropArea.destroyDelayed();
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

	const auto &urls = mimeData->urls();
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
		const auto &urls = mimeData->urls();
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
	if (update.flags & UpdateFlag::NameChanged) {
		refreshNameText();
	}
	if (update.flags & UpdateFlag::UserOnlineChanged) {
		refreshStatusText();
	}
}

void CoverWidget::refreshNameText() {
	_name->setText(App::peerName(_peer));
	refreshNameGeometry(width());
}

void CoverWidget::refreshStatusText() {
	if (auto app = App::app()) {
		if (app->isPhotoUpdating(_peer->id)) {
			_statusText = lang(lng_settings_uploading_photo);
			if (!_cancelPhotoUpload) {
				_cancelPhotoUpload = new LinkButton(this, lang(lng_cancel), st::btnDefLink);
				connect(_cancelPhotoUpload, SIGNAL(clicked()), this, SLOT(onCancelPhotoUpload()));
				_cancelPhotoUpload->show();
				_cancelPhotoUpload->moveToLeft(_statusPosition.x() + st::profileStatusFont->width(_statusText) + st::profileStatusFont->spacew, _statusPosition.y());
			}
			update();
			return;
		}
	}

	_cancelPhotoUpload.destroy();
	int currentTime = unixtime();
	if (_peerUser) {
		_statusText = App::onlineText(_peerUser, currentTime, true);
		_statusTextIsOnline = App::onlineColorUse(_peerUser, currentTime);
	} else if (_peerChat && _peerChat->amIn()) {
		int fullCount = qMax(_peerChat->count, _peerChat->participants.size());
		if (_onlineCount > 0 && _onlineCount <= fullCount) {
			_statusText = lng_chat_status_members_online(lt_count, fullCount, lt_count_online, _onlineCount);
		} else {
			_statusText = lng_chat_status_members(lt_count, _peerChat->count);
		}
	} else if (_peerChannel) {
		int fullCount = _peerChannel->membersCount();
		if (_onlineCount > 0 && _onlineCount <= fullCount) {
			_statusText = lng_chat_status_members_online(lt_count, fullCount, lt_count_online, _onlineCount);
		} else if (fullCount > 0 ) {
			_statusText = lng_chat_status_members(lt_count, _peerChannel->membersCount());
		} else {
			_statusText = lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	} else {
		_statusText = lang(lng_chat_status_unaccessible);
	}
	update();
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
	if (_peerUser->botInfo && !_peerUser->botInfo->cantJoinGroups) {
		addButton(lang(lng_profile_invite_to_group), SLOT(onAddBotToGroup()), &st::profileAddMemberButton);
	} else if (_peerUser->canShareThisContact()) {
		addButton(lang(lng_profile_share_contact), SLOT(onShareContact()));
	}
}

void CoverWidget::setChatButtons() {
	if (_peerChat->canEdit()) {
		addButton(lang(lng_profile_set_group_photo), SLOT(onSetPhoto()));
		addButton(lang(lng_profile_add_participant), SLOT(onAddMember()), &st::profileAddMemberButton);
	}
}

void CoverWidget::setMegagroupButtons() {
	if (_peerMegagroup->amIn()) {
		if (_peerMegagroup->canEditPhoto()) {
			addButton(lang(lng_profile_set_group_photo), SLOT(onSetPhoto()));
		}
	} else {
		addButton(lang(lng_profile_join_channel), SLOT(onJoin()));
	}
	if (_peerMegagroup->canAddMembers()) {
		addButton(lang(lng_profile_add_participant), SLOT(onAddMember()), &st::profileAddMemberButton);
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
		delete button.widget;
		delete button.replacement;
	}
}

void CoverWidget::addButton(const QString &text, const char *slot, const style::RoundButton *replacementStyle) {
	auto &buttonStyle = _buttons.isEmpty() ? st::profilePrimaryButton : st::profileSecondaryButton;
	auto button = new Ui::RoundButton(this, text, buttonStyle);
	button->setTextTransform(Ui::RoundButton::TextTransform::ToUpper);
	connect(button, SIGNAL(clicked()), this, slot);
	button->show();

	auto replacement = replacementStyle ? new Ui::RoundButton(this, QString(), *replacementStyle) : nullptr;
	if (replacement) {
		connect(replacement, SIGNAL(clicked()), this, slot);
		replacement->hide();
	}

	_buttons.push_back({ button, replacement });
}

void CoverWidget::onOnlineCountUpdated(int onlineCount) {
	_onlineCount = onlineCount;
	refreshStatusText();
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
	connect(box, SIGNAL(closed()), this, SLOT(onPhotoUploadStatusChanged()));
	Ui::showLayer(box);
}

void CoverWidget::onPhotoUploadStatusChanged(PeerId peerId) {
	if (!peerId || peerId == _peer->id) {
		refreshStatusText();
	}
}

void CoverWidget::onAddMember() {
	if (_peerChat) {
		if (_peerChat->count >= Global::ChatSizeMax() && _peerChat->amCreator()) {
			Ui::showLayer(new ConvertToSupergroupBox(_peerChat));
		} else {
			Ui::showLayer(new ContactsBox(_peerChat, MembersFilterRecent));
		}
	} else if (_peerChannel && _peerChannel->mgInfo) {
		MembersAlreadyIn already;
		for_const (auto user, _peerChannel->mgInfo->lastParticipants) {
			already.insert(user);
		}
		Ui::showLayer(new ContactsBox(_peerChannel, MembersFilterRecent, already));
	}
}

void CoverWidget::onAddBotToGroup() {
	if (_peerUser && _peerUser->botInfo) {
		Ui::showLayer(new ContactsBox(_peerUser));
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
