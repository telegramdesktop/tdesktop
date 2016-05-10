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
#include "structs.h"

#include "lang.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "history.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwindow.h"
#include "ui/filedialog.h"
#include "apiwrap.h"
#include "boxes/confirmbox.h"
#include "audio.h"
#include "localstorage.h"

namespace {
	int peerColorIndex(const PeerId &peer) {
		UserId myId(MTP::authedId()), peerId(peerToBareInt(peer));
		QByteArray both(qsl("%1%2").arg(peerId).arg(myId).toUtf8());
		if (both.size() > 15) {
			both = both.mid(0, 15);
		}
		uchar md5[16];
		hashMd5(both.constData(), both.size(), md5);
		return (md5[peerId & 0x0F] & (peerIsUser(peer) ? 0x07 : 0x03));
	}
}

style::color peerColor(int index) {
	static const style::color peerColors[8] = {
		style::color(st::color1),
		style::color(st::color2),
		style::color(st::color3),
		style::color(st::color4),
		style::color(st::color5),
		style::color(st::color6),
		style::color(st::color7),
		style::color(st::color8)
	};
	return peerColors[index];
}

ImagePtr userDefPhoto(int index) {
	static const ImagePtr userDefPhotos[UserColorsCount] = {
		ImagePtr(qsl(":/ava/art/usercolor1.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor2.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor3.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor4.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor5.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor6.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor7.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/usercolor8.png"), "PNG"),
	};
	return userDefPhotos[index];
}

ImagePtr chatDefPhoto(int index) {
	static const ImagePtr chatDefPhotos[4] = {
		ImagePtr(qsl(":/ava/art/chatcolor1.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor2.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor3.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor4.png"), "PNG"),
	};
	return chatDefPhotos[index];
}

ImagePtr channelDefPhoto(int index) {
	static const ImagePtr channelDefPhotos[4] = {
		ImagePtr(qsl(":/ava/art/channelcolor1.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/channelcolor2.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/channelcolor3.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/channelcolor4.png"), "PNG"),
	};
	return channelDefPhotos[index];
}

NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
NotifySettingsPtr globalNotifyAllPtr = UnknownNotifySettings, globalNotifyUsersPtr = UnknownNotifySettings, globalNotifyChatsPtr = UnknownNotifySettings;

PeerData::PeerData(const PeerId &id) : id(id)
, loadedStatus(NotLoaded)
, colorIndex(peerColorIndex(id))
, color(peerColor(colorIndex))
, photoId(UnknownPeerPhotoId)
, nameVersion(0)
, notify(UnknownNotifySettings)
, _userpic(isUser() ? userDefPhoto(colorIndex) : ((isChat() || isMegagroup()) ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex))) {
	if (!peerIsUser(id) && !peerIsChannel(id)) updateName(QString(), QString(), QString());
}

void PeerData::updateName(const QString &newName, const QString &newNameOrPhone, const QString &newUsername) {
	if (name == newName && nameVersion > 0) {
		if (isUser()) {
			if (asUser()->nameOrPhone == newNameOrPhone && asUser()->username == newUsername) {
				return;
			}
		} else if (isChannel()) {
			if (asChannel()->username == newUsername) {
				return;
			}
		} else if (isChat()) {
			return;
		}
	}

	++nameVersion;
	name = newName;
	nameText.setText(st::msgNameFont, name, _textNameOptions);
	if (isUser()) {
		asUser()->username = newUsername;
		asUser()->setNameOrPhone(newNameOrPhone);
	} else if (isChannel()) {
		if (asChannel()->username != newUsername) {
			asChannel()->username = newUsername;
			if (newUsername.isEmpty()) {
				asChannel()->flags &= ~MTPDchannel::Flag::f_username;
			} else {
				asChannel()->flags |= MTPDchannel::Flag::f_username;
			}
			if (App::main()) {
				App::main()->peerUsernameChanged(this);
			}
		}
	}

	Names oldNames = names;
	NameFirstChars oldChars = chars;
	fillNames();

	if (App::main()) {
		emit App::main()->peerNameChanged(this, oldNames, oldChars);
	}
}

void PeerData::setUserpic(ImagePtr userpic) {
	_userpic = userpic;
}

ImagePtr PeerData::currentUserpic() const {
	if (_userpic->loaded()) {
		return _userpic;
	}
	_userpic->load();

	if (isUser()) {
		return userDefPhoto(colorIndex);
	} else if (isMegagroup() || isChat()) {
		return chatDefPhoto(colorIndex);
	}
	return channelDefPhoto(colorIndex);
}

void PeerData::paintUserpic(Painter &p, int size, int x, int y) const {
	p.drawPixmap(x, y, currentUserpic()->pixCircled(size, size));
}

StorageKey PeerData::userpicUniqueKey() const {
	if (photoLoc.isNull() || !_userpic->loaded()) {
		return StorageKey(0, (isUser() ? 0x1000 : ((isChat() || isMegagroup()) ? 0x2000 : 0x3000)) | colorIndex);
	}
	return storageKey(photoLoc);
}

void PeerData::saveUserpic(const QString &path) const {
	currentUserpic()->pixCircled().save(path, "PNG");
}

QPixmap PeerData::genUserpic(int size) const {
	return currentUserpic()->pixCircled(size, size);
}

const Text &BotCommand::descriptionText() const {
	if (_descriptionText.isEmpty() && !_description.isEmpty()) {
		_descriptionText.setText(st::mentionFont, _description, _textNameOptions);
	}
	return _descriptionText;
}

void UserData::setPhoto(const MTPUserProfilePhoto &p) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_userProfilePhoto: {
		const auto &d(p.c_userProfilePhoto());
		newPhotoId = d.vphoto_id.v;
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? userDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
		//App::feedPhoto(App::photoFromUserPhoto(peerToUser(id), MTP_int(unixtime()), p));
	} break;
	default: {
		newPhotoId = 0;
		if (id == ServiceUserId) {
			if (_userpic.v() == userDefPhoto(colorIndex).v()) {
				newPhoto = ImagePtr(QPixmap::fromImage(App::wnd()->iconLarge().scaledToWidth(160, Qt::SmoothTransformation), Qt::ColorOnly), "PNG");
			}
		} else {
			newPhoto = userDefPhoto(colorIndex);
		}
		newPhotoLoc = StorageImageLocation();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
	}
}

void PeerData::fillNames() {
	names.clear();
	chars.clear();
	QString toIndex = textAccentFold(name);
	if (cRussianLetters().match(toIndex).hasMatch()) {
		toIndex += ' ' + translitRusEng(toIndex);
	}
	if (isUser()) {
		if (!asUser()->nameOrPhone.isEmpty() && asUser()->nameOrPhone != name) toIndex += ' ' + textAccentFold(asUser()->nameOrPhone);
		if (!asUser()->username.isEmpty()) toIndex += ' ' + textAccentFold(asUser()->username);
	} else if (isChannel()) {
		if (!asChannel()->username.isEmpty()) toIndex += ' ' + textAccentFold(asChannel()->username);
	}
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	QStringList namesList = toIndex.toLower().split(cWordSplit(), QString::SkipEmptyParts);
	for (QStringList::const_iterator i = namesList.cbegin(), e = namesList.cend(); i != e; ++i) {
		names.insert(*i);
		chars.insert(i->at(0));
	}
}

void UserData::setName(const QString &first, const QString &last, const QString &phoneName, const QString &usern) {
	bool updName = !first.isEmpty() || !last.isEmpty(), updUsername = (username != usern);

	if (updName && first.trimmed().isEmpty()) {
		firstName = last;
		lastName = QString();
		updateName(firstName, phoneName, usern);
	} else {
		if (updName) {
			firstName = first;
			lastName = last;
		}
		updateName(lastName.isEmpty() ? firstName : lng_full_name(lt_first_name, firstName, lt_last_name, lastName), phoneName, usern);
	}
	if (updUsername) {
		if (App::main()) {
			App::main()->peerUsernameChanged(this);
		}
	}
}

void UserData::setPhone(const QString &newPhone) {
	phone = newPhone;
}

void UserData::setBotInfoVersion(int version) {
	if (version < 0) {
		if (botInfo) {
			if (!botInfo->commands.isEmpty()) {
				botInfo->commands.clear();
				Notify::botCommandsChanged(this);
			}
			delete botInfo;
			botInfo = 0;
			Notify::userIsBotChanged(this);
		}
	} else if (!botInfo) {
		botInfo = new BotInfo();
		botInfo->version = version;
		Notify::userIsBotChanged(this);
	} else if (botInfo->version < version) {
		if (!botInfo->commands.isEmpty()) {
			botInfo->commands.clear();
			Notify::botCommandsChanged(this);
		}
		botInfo->description.clear();
		botInfo->version = version;
		botInfo->inited = false;
	}
}

void UserData::setBotInfo(const MTPBotInfo &info) {
	switch (info.type()) {
	case mtpc_botInfo: {
		const auto &d(info.c_botInfo());
		if (peerFromUser(d.vuser_id.v) != id || !botInfo) return;

		QString desc = qs(d.vdescription);
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Text(st::msgMinWidth);
		}

		const auto &v(d.vcommands.c_vector().v);
		botInfo->commands.reserve(v.size());
		bool changedCommands = false;
		int32 j = 0;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).type() != mtpc_botCommand) continue;

			QString cmd = qs(v.at(i).c_botCommand().vcommand), desc = qs(v.at(i).c_botCommand().vdescription);
			if (botInfo->commands.size() <= j) {
				botInfo->commands.push_back(BotCommand(cmd, desc));
				changedCommands = true;
			} else {
				if (botInfo->commands[j].command != cmd) {
					botInfo->commands[j].command = cmd;
					changedCommands = true;
				}
				if (botInfo->commands[j].setDescription(desc)) {
					changedCommands = true;
				}
			}
			++j;
		}
		while (j < botInfo->commands.size()) {
			botInfo->commands.pop_back();
			changedCommands = true;
		}

		botInfo->inited = true;

		if (changedCommands) {
			Notify::botCommandsChanged(this);
		}
	} break;
	}
}

void UserData::setNameOrPhone(const QString &newNameOrPhone) {
	if (nameOrPhone != newNameOrPhone) {
		nameOrPhone = newNameOrPhone;
		phoneText.setText(st::msgNameFont, nameOrPhone, _textNameOptions);
	}
}

void UserData::madeAction() {
	if (botInfo || isServiceUser(id)) return;

	int32 t = unixtime();
	if (onlineTill <= 0 && -onlineTill < t) {
		onlineTill = -t - SetOnlineAfterActivity;
		App::markPeerUpdated(this);
	} else if (onlineTill > 0 && onlineTill < t + 1) {
		onlineTill = t + SetOnlineAfterActivity;
		App::markPeerUpdated(this);
	}
}

void ChatData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? chatDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
//		photoFull = ImagePtr(640, 640, d.vphoto_big, chatDefPhoto(colorIndex));
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = chatDefPhoto(colorIndex);
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		emit App::main()->peerPhotoChanged(this);
	}
}

void ChannelData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex)) : ImagePtr(newPhotoLoc);
//		photoFull = ImagePtr(640, 640, d.vphoto_big, (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex)));
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex));
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) emit App::main()->peerPhotoChanged(this);
	}
}

void ChannelData::setName(const QString &newName, const QString &usern) {
	bool updName = !newName.isEmpty(), updUsername = (username != usern);

	updateName(newName.isEmpty() ? name : newName, QString(), usern);
}

void ChannelData::updateFull(bool force) {
	if (!_lastFullUpdate || force || getms(true) > _lastFullUpdate + UpdateFullChannelTimeout) {
		if (App::api()) {
			App::api()->requestFullPeer(this);
			if (!amCreator() && !inviter) App::api()->requestSelfParticipant(this);
		}
	}
}

void ChannelData::fullUpdated() {
	_lastFullUpdate = getms(true);
}

void ChannelData::flagsUpdated() {
	if (isMegagroup()) {
		if (!mgInfo) {
			mgInfo = new MegagroupInfo();
		}
		if (History *h = App::historyLoaded(id)) {
			if (h->asChannelHistory()->onlyImportant()) {
				MsgId fixInScrollMsgId = 0;
				int32 fixInScrollMsgTop = 0;
				h->asChannelHistory()->getSwitchReadyFor(SwitchAtTopMsgId, fixInScrollMsgId, fixInScrollMsgTop);
			}
		}
	} else if (mgInfo) {
		delete mgInfo;
		mgInfo = 0;
	}
}

ChannelData::~ChannelData() {
	delete mgInfo;
}

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue) {
	return _queue.insert(uint64(uint32(_last)) << 32 | uint64(uint32(_count)), queue).key();
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll && App::main()) {
		App::main()->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) return;

	setWaitingForSkipped(channel, -1);

	if (!App::main() || _queue.isEmpty()) return;

	++_applySkippedLevel;
	for (QMap<uint64, PtsSkippedQueue>::const_iterator i = _queue.cbegin(), e = _queue.cend(); i != e; ++i) {
		switch (i.value()) {
		case SkippedUpdate: App::main()->feedUpdate(_updateQueue.value(i.key())); break;
		case SkippedUpdates: App::main()->feedUpdates(_updatesQueue.value(i.key())); break;
		}
	}
	--_applySkippedLevel;
	clearSkippedUpdates();
}

void PtsWaiter::clearSkippedUpdates() {
	_queue.clear();
	_updateQueue.clear();
	_updatesQueue.clear();
	_applySkippedLevel = 0;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	}
	return check(channel, pts, count);
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.insert(ptsKey(SkippedUpdates), updates);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.insert(ptsKey(SkippedUpdate), update);
	return false;
}

bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) { // return false if need to save that update and apply later
	if (!inited()) {
		init(pts);
		return true;
	}

	_last = qMax(_last, pts);
	_count += count;
	if (_last == _count) {
		_good = _last;
		return true;
	} else if (_last < _count) {
		setWaitingForSkipped(channel, 1);
	} else {
		setWaitingForSkipped(channel, WaitForSkippedTimeout);
	}
	return !count;
}

PhotoData::PhotoData(const PhotoId &id, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full)
: id(id)
, access(access)
, date(date)
, thumb(thumb)
, medium(medium)
, full(full)
, peer(0)
, uploadingData(0) {
}

void PhotoData::automaticLoad(const HistoryItem *item) {
	full->automaticLoad(item);
}

void PhotoData::automaticLoadSettingsChanged() {
	full->automaticLoadSettingsChanged();
}

void PhotoData::download() {
	full->loadEvenCancelled();
	notifyLayoutChanged();
}

bool PhotoData::loaded() const {
	bool wasLoading = loading();
	if (full->loaded()) {
		if (wasLoading) {
			notifyLayoutChanged();
		}
		return true;
	}
	return false;
}

bool PhotoData::loading() const {
	return full->loading();
}

bool PhotoData::displayLoading() const {
	return full->loading() ? full->displayLoading() : uploading();
}

void PhotoData::cancel() {
	full->cancel();
	notifyLayoutChanged();
}

void PhotoData::notifyLayoutChanged() const {
	const PhotoItems &items(App::photoItems());
	PhotoItems::const_iterator i = items.constFind(const_cast<PhotoData*>(this));
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Notify::historyItemLayoutChanged(j.key());
		}
	}
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			return float64(uploadingData->offset) / uploadingData->size;
		}
		return 0;
	}
	return full->progress();
}

int32 PhotoData::loadOffset() const {
	return full->loadOffset();
}

bool PhotoData::uploading() const {
	return uploadingData;
}

void PhotoData::forget() {
	thumb->forget();
	replyPreview->forget();
	medium->forget();
	full->forget();
}

ImagePtr PhotoData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

PhotoData::~PhotoData() {
	deleteAndMark(uploadingData);
}

void PhotoOpenClickHandler::onClickImpl() const {
	App::wnd()->showPhoto(this, App::hoveredLinkItem() ? App::hoveredLinkItem() : App::contextItem());
}

void PhotoSaveClickHandler::onClickImpl() const {
	PhotoData *data = photo();
	if (!data->date) return;

	data->download();
}

void PhotoCancelClickHandler::onClickImpl() const {
	PhotoData *data = photo();
	if (!data->date) return;

	if (data->uploading()) {
		if (HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0)) {
			if (HistoryMessage *msg = item->toHistoryMessage()) {
				if (msg->getMedia() && msg->getMedia()->type() == MediaTypePhoto && static_cast<HistoryPhoto*>(msg->getMedia())->photo() == data) {
					App::contextItem(item);
					App::main()->deleteLayer(-2);
				}
			}
		}
	} else {
		data->cancel();
	}
}

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir) {
#ifdef Q_OS_WIN
	name = name.replace(QRegularExpression(qsl("[\\\\\\/\\:\\*\\?\\\"\\<\\>\\|]")), qsl("_"));
#elif defined Q_OS_MAC
	name = name.replace(QRegularExpression(qsl("[\\:]")), qsl("_"));
#elif defined Q_OS_LINUX
	name = name.replace(QRegularExpression(qsl("[\\/]")), qsl("_"));
#endif
	if (cAskDownloadPath() || savingAs) {
		if (!name.isEmpty() && name.at(0) == QChar::fromLatin1('.')) {
			name = filedialogDefaultName(prefix, name);
		} else if (dir.path() != qsl(".")) {
			QString path = dir.absolutePath();
			if (path != cDialogLastPath()) {
				cSetDialogLastPath(path);
				Local::writeUserSettings();
			}
		}

		// check if extension of filename is present in filter
		// it should be in first filter section on the first place
		// place it there, if it is not
		QString ext = QFileInfo(name).suffix(), fil = filter, sep = qsl(";;");
		if (!ext.isEmpty()) {
			if (QRegularExpression(qsl("^[a-zA-Z_0-9]+$")).match(ext).hasMatch()) {
				QStringList filters = filter.split(sep);
				if (filters.size() > 1) {
					QString first = filters.at(0);
					int32 start = first.indexOf(qsl("(*."));
					if (start >= 0) {
						if (!QRegularExpression(qsl("\\(\\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first).hasMatch()) {
							QRegularExpressionMatch m = QRegularExpression(qsl(" \\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first);
							if (m.hasMatch() && m.capturedStart() > start + 3) {
								int32 oldpos = m.capturedStart(), oldend = m.capturedEnd();
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3, oldpos - start - 3) + first.mid(oldend - 1) + sep + filters.mid(1).join(sep);
							} else {
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3) + sep + filters.mid(1).join(sep);
							}
						}
					} else {
						fil = QString();
					}
				} else {
					fil = QString();
				}
			} else {
				fil = QString();
			}
		}
		return filedialogGetSaveFile(name, title, fil, name) ? name : QString();
	}

	QString path;
	if (cDownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (cDownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = cDownloadPath();
	}
	if (name.isEmpty()) name = qsl(".unknown");
	if (name.at(0) == QChar::fromLatin1('.')) {
		if (!QDir().exists(path)) QDir().mkpath(path);
		return filedialogDefaultName(prefix, name, path);
	}
	if (dir.path() != qsl(".")) {
		path = dir.absolutePath() + '/';
	}

	QString nameStart, extension;
	int32 extPos = name.lastIndexOf('.');
	if (extPos >= 0) {
		nameStart = name.mid(0, extPos);
		extension = name.mid(extPos);
	} else {
		nameStart = name;
	}
	QString nameBase = path + nameStart;
	name = nameBase + extension;
	for (int i = 0; QFileInfo(name).exists(); ++i) {
		name = nameBase + QString(" (%1)").arg(i + 2) + extension;
	}

	if (!QDir().exists(path)) QDir().mkpath(path);
	return name;
}

bool StickerData::setInstalled() const {
	switch (set.type()) {
	case mtpc_inputStickerSetID: {
		auto it = Global::StickerSets().constFind(set.c_inputStickerSetID().vid.v);
		return (it != Global::StickerSets().cend()) && !(it->flags & MTPDstickerSet::Flag::f_disabled);
	} break;
	case mtpc_inputStickerSetShortName: {
		QString name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (auto it = Global::StickerSets().cbegin(), e = Global::StickerSets().cend(); it != e; ++it) {
			if (it->shortName.toLower() == name) {
				return !(it->flags & MTPDstickerSet::Flag::f_disabled);
			}
		}
	} break;
	}
	return false;
}

QString documentSaveFilename(const DocumentData *data, bool forceSavingAs = false, const QString already = QString(), const QDir &dir = QDir()) {
	QString name, filter, caption, prefix;
	MimeType mimeType = mimeTypeForName(data->mime);
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->voice()) {
		bool mp3 = (data->mime == qstr("audio/mp3"));
		name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : already;
		filter = mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)");
		caption = lang(lng_save_audio);
		prefix = qsl("audio");
	} else if (data->isVideo()) {
		name = already.isEmpty() ? qsl(".mov") : already;
		filter = qsl("MOV Video (*.mov);;All files (*.*)");
		caption = lang(lng_save_video);
		prefix = qsl("video");
	} else {
		name = already.isEmpty() ? data->name : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = QString();
		} else {
			filter = mimeType.filterString() + qsl(";;All files (*.*)");
		}
		caption = lang(data->song() ? lng_save_audio_file : lng_save_file);
		prefix = qsl("doc");
	}

	return saveFileName(caption, filter, prefix, name, forceSavingAs, dir);
}

void DocumentOpenClickHandler::doOpen(DocumentData *data, ActionOnLoad action) {
	if (!data->date) return;

	HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr);
	FullMsgId msgId;
	if (item) {
		msgId = item->fullId();
	}
	bool playVoice = data->voice() && audioPlayer();
	bool playMusic = data->song() && audioPlayer();
	bool playAnimation = data->isAnimation() && item && item->getMedia();
	const FileLocation &location(data->location(true));
	if (!location.isEmpty() || (!data->data().isEmpty() && (playVoice || playMusic || playAnimation))) {
		if (playVoice) {
			AudioMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing == AudioMsgId(data, msgId) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewVoiceFiles);
			} else {
				AudioMsgId audio(data, msgId);
				audioPlayer()->play(audio);
				if (App::main()) {
					App::main()->audioPlayProgress(audio);
					App::main()->mediaMarkRead(data);
				}
			}
		} else if (playMusic) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing == SongMsgId(data, msgId) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewFiles);
			} else {
				SongMsgId song(data, msgId);
				audioPlayer()->play(song);
				if (App::main()) App::main()->documentPlayProgress(song);
			}
		} else if (data->voice() || data->isVideo()) {
			psOpenFile(location.name());
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->size < MediaViewImageSizeLimit) {
			if (!data->data().isEmpty() && playAnimation) {
				if (action == ActionOnLoadPlayInline && item->getMedia()) {
					item->getMedia()->playInline(item);
				} else {
					App::wnd()->showDocument(data, item);
				}
			} else if (location.accessEnable()) {
				if (data->isAnimation() || QImageReader(location.name()).canRead()) {
					if (action == ActionOnLoadPlayInline && item && item->getMedia()) {
						item->getMedia()->playInline(item);
					} else {
						App::wnd()->showDocument(data, item);
					}
				} else {
					psOpenFile(location.name());
				}
				location.accessDisable();
			} else {
				psOpenFile(location.name());
			}
		} else {
			psOpenFile(location.name());
		}
		return;
	}

	if (data->status != FileReady) return;

	QString filename;
	if (!data->saveToCache()) {
		filename = documentSaveFilename(data);
		if (filename.isEmpty()) return;
	}

	data->save(filename, action, msgId);
}

void DocumentOpenClickHandler::onClickImpl() const {
	doOpen(document(), document()->voice() ? ActionOnLoadNone : ActionOnLoadOpen);
}

void GifOpenClickHandler::onClickImpl() const {
	doOpen(document(), ActionOnLoadPlayInline);
}

void DocumentSaveClickHandler::doSave(DocumentData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString filepath = data->filepath(DocumentData::FilePathResolveSaveFromDataSilent, forceSavingAs);
	if (!filepath.isEmpty() && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), filepath)) {
			psOpenFile(filepath, true);
		}
	} else {
		QFileInfo fileinfo(filepath);
		QDir filedir(filepath.isEmpty() ? QDir() : fileinfo.dir());
		QString filename(filepath.isEmpty() ? QString() : fileinfo.fileName());
		QString newfname = documentSaveFilename(data, forceSavingAs, filename, filedir);
		if (!newfname.isEmpty()) {
			ActionOnLoad action = filename.isEmpty() ? ActionOnLoadNone : ActionOnLoadOpenWith;
			FullMsgId actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(newfname, action, actionMsgId);
		}
	}
}

void DocumentSaveClickHandler::onClickImpl() const {
	doSave(document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	DocumentData *data = document();
	if (!data->date) return;

	if (data->uploading()) {
		if (HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0)) {
			if (HistoryMessage *msg = item->toHistoryMessage()) {
				if (msg->getMedia() && msg->getMedia()->getDocument() == data) {
					App::contextItem(item);
					App::main()->deleteLayer(-2);
				}
			}
		}
	} else {
		data->cancel();
	}
}

VoiceData::~VoiceData() {
	if (!waveform.isEmpty() && waveform.at(0) == -1 && waveform.size() > int32(sizeof(TaskId))) {
		TaskId taskId = 0;
		memcpy(&taskId, waveform.constData() + 1, sizeof(taskId));
		Local::cancelTask(taskId);
	}
}

DocumentAdditionalData::~DocumentAdditionalData() {
}

DocumentData::DocumentData(DocumentId id, int32 dc, uint64 accessHash, const QString &url, const QVector<MTPDocumentAttribute> &attributes)
: id(id)
, _dc(dc)
, _access(accessHash)
, _url(url) {
	setattributes(attributes);
	if (_dc && _access) {
		_location = Local::readFileLocation(mediaKey());
	}
}

DocumentData *DocumentData::create(DocumentId id) {
	return new DocumentData(id, 0, 0, QString(), QVector<MTPDocumentAttribute>());
}

DocumentData *DocumentData::create(DocumentId id, int32 dc, uint64 accessHash, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, dc, accessHash, QString(), attributes);
}

DocumentData *DocumentData::create(DocumentId id, const QString &url, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, 0, 0, url, attributes);
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			const auto &d(attributes[i].c_documentAttributeImageSize());
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument || type == VideoDocument) {
			type = AnimatedDocument;
			_additional = nullptr;
		} break;
		case mtpc_documentAttributeSticker: {
			const auto &d(attributes[i].c_documentAttributeSticker());
			if (type == FileDocument) {
				type = StickerDocument;
				_additional = std_::make_unique<StickerData>();
			}
			if (sticker()) {
				sticker()->alt = qs(d.valt);
				sticker()->set = d.vstickerset;
			}
		} break;
		case mtpc_documentAttributeVideo: {
			const auto &d(attributes[i].c_documentAttributeVideo());
			if (type == FileDocument) {
				type = VideoDocument;
			}
			_duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			const auto &d(attributes[i].c_documentAttributeAudio());
			if (type == FileDocument) {
				if (d.is_voice()) {
					type = VoiceDocument;
					_additional = std_::make_unique<VoiceData>();
				} else {
					type = SongDocument;
					_additional = std_::make_unique<SongData>();
				}
			}
			if (voice()) {
				voice()->duration = d.vduration.v;
				VoiceWaveform waveform = documentWaveformDecode(qba(d.vwaveform));
				uchar wavemax = 0;
				for (int32 i = 0, l = waveform.size(); i < l; ++i) {
					uchar waveat = waveform.at(i);
					if (wavemax < waveat) wavemax = waveat;
				}
				voice()->waveform = waveform;
				voice()->wavemax = wavemax;
			} else if (song()) {
				song()->duration = d.vduration.v;
				song()->title = qs(d.vtitle);
				song()->performer = qs(d.vperformer);
			}
		} break;
		case mtpc_documentAttributeFilename: name = qs(attributes[i].c_documentAttributeFilename().vfile_name); break;
		}
	}
	if (type == StickerDocument) {
		if (dimensions.width() <= 0 || dimensions.height() <= 0 || dimensions.width() > StickerMaxSize || dimensions.height() > StickerMaxSize || size > StickerInMemory) {
			type = FileDocument;
			_additional = nullptr;
		}
	}
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument) || (isAnimation() && size < AnimationInMemory) || (voice() && size < AudioVoiceMsgInMemory);
}

void DocumentData::forget() {
	thumb->forget();
	if (sticker()) sticker()->img->forget();
	replyPreview->forget();
	_data.clear();
}

void DocumentData::automaticLoad(const HistoryItem *item) {
	if (loaded() || status != FileReady) return;

	if (saveToCache() && _loader != CancelledMtpFileLoader) {
		if (type == StickerDocument) {
			save(QString(), _actionOnLoad, _actionOnLoadMsgId);
		} else if (isAnimation()) {
			bool loadFromCloud = false;
			if (item) {
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoGroups);
				}
			} else { // if load at least anywhere
				loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
			}
			save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
		} else if (voice()) {
			if (item) {
				bool loadFromCloud = false;
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoGroups);
				}
				save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
			}
		}
	}
}

void DocumentData::automaticLoadSettingsChanged() {
	if (loaded() || status != FileReady || (!isAnimation() && !voice()) || !saveToCache() || _loader != CancelledMtpFileLoader) return;
	_loader = 0;
}

void DocumentData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	const FileLocation &loc(location(true));
	QString already = loc.name();
	HistoryItem *item = _actionOnLoadMsgId.msg ? App::histItemById(_actionOnLoadMsgId) : nullptr;
	bool showImage = !isVideo() && (size < MediaViewImageSizeLimit);
	bool playVoice = voice() && audioPlayer() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	bool playMusic = song() && audioPlayer() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	bool playAnimation = isAnimation() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && showImage && item && item->getMedia();
	if (playVoice) {
		if (loaded()) {
			AudioMsgId playing;
			AudioPlayerState state = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &state);
			if (playing == AudioMsgId(this, _actionOnLoadMsgId) && !(state & AudioPlayerStoppedMask) && state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewVoiceFiles);
			} else {
				audioPlayer()->play(AudioMsgId(this, _actionOnLoadMsgId));
				if (App::main()) App::main()->mediaMarkRead(this);
			}
		}
	} else if (playMusic) {
		if (loaded()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing == SongMsgId(this, _actionOnLoadMsgId) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewFiles);
			} else {
				SongMsgId song(this, _actionOnLoadMsgId);
				audioPlayer()->play(song);
				if (App::main()) App::main()->documentPlayProgress(song);
			}
		}
	} else if (playAnimation) {
		if (loaded()) {
			if (_actionOnLoad == ActionOnLoadPlayInline && item->getMedia()) {
				item->getMedia()->playInline(item);
			} else {
				App::wnd()->showDocument(this, item);
			}
		}
	} else {
		if (already.isEmpty()) return;

		if (_actionOnLoad == ActionOnLoadOpenWith) {
			QPoint pos(QCursor::pos());
			if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
				psOpenFile(already, true);
			}
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			if (voice() || isVideo()) {
				psOpenFile(already);
				if (App::main()) App::main()->mediaMarkRead(this);
			} else if (loc.accessEnable()) {
				if (showImage && QImageReader(loc.name()).canRead()) {
					if (_actionOnLoad == ActionOnLoadPlayInline && item && item->getMedia()) {
						item->getMedia()->playInline(item);
					} else {
						App::wnd()->showDocument(this, item);
					}
				} else {
					psOpenFile(already);
				}
				loc.accessDisable();
			} else {
				psOpenFile(already);
			}
		}
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool DocumentData::loaded(FilePathResolveType type) const {
	if (loading() && _loader->done()) {
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->stop();
			_loader = CancelledMtpFileLoader;
		} else {
			DocumentData *that = const_cast<DocumentData*>(this);
			that->_location = FileLocation(mtpToStorageType(_loader->fileType()), _loader->fileName());
			that->_data = _loader->bytes();
			if (that->sticker() && !_loader->imagePixmap().isNull()) {
				that->sticker()->img = ImagePtr(_data, _loader->imageFormat(), _loader->imagePixmap());
			}

			_loader->deleteLater();
			_loader->stop();
			_loader = nullptr;
		}
		notifyLayoutChanged();
	}
	return !data().isEmpty() || !filepath(type).isEmpty();
}

bool DocumentData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

bool DocumentData::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : uploading();
}

float64 DocumentData::progress() const {
	if (uploading()) {
		if (size > 0) {
			return float64(uploadOffset) / size;
		}
		return 0;
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);
}

int32 DocumentData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return status == FileUploading;
}

void DocumentData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;

	if (loaded(FilePathResolveChecked)) {
		const FileLocation &l(location(true));
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
				f.close();

				setLocation(FileLocation(StorageFilePartial, toFile));
				Local::writeFileLocation(mediaKey(), FileLocation(mtpToStorageType(mtpc_storage_filePartial), toFile));
			} else if (l.accessEnable()) {
				QFile(l.name()).copy(toFile);
				l.accessDisable();
			}
		}
		performActionOnLoad();
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
			_loader = 0;
		}
	}

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		if (!_access && !_url.isEmpty()) {
			_loader = new webFileLoader(_url, toFile, fromCloud, autoLoading);
		} else {
			_loader = new mtpFileLoader(_dc, id, _access, locationType(), toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		}
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(documentLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(documentLoadFailed(FileLoader*,bool)));
		_loader->start();
	}
	notifyLayoutChanged();
}

void DocumentData::cancel() {
	if (!loading()) return;

	FileLoader *l = _loader;
	_loader = CancelledMtpFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->stop();

		notifyLayoutChanged();
	}
	_actionOnLoad = ActionOnLoadNone;
}

void DocumentData::notifyLayoutChanged() const {
	const DocumentItems &items(App::documentItems());
	DocumentItems::const_iterator i = items.constFind(const_cast<DocumentData*>(this));
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Notify::historyItemLayoutChanged(j.key());
		}
	}

	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(const_cast<DocumentData*>(this))) {
			Notify::inlineItemLayoutChanged(item);
		}
	}
}

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit) {
	VoiceWaveform result((encoded5bit.size() * 8) / 5, 0);
	for (int32 i = 0, l = result.size(); i < l; ++i) { // read each 5 bit of encoded5bit as 0-31 unsigned char
		int32 byte = (i * 5) / 8, shift = (i * 5) % 8;
		result[i] = (((*(uint16*)(encoded5bit.constData() + byte)) >> shift) & 0x1F);
	}
	return result;
}

QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform) {
	QByteArray result((waveform.size() * 5 + 7) / 8, 0);
	for (int32 i = 0, l = waveform.size(); i < l; ++i) { // write each 0-31 unsigned char as 5 bit to result
		int32 byte = (i * 5) / 8, shift = (i * 5) % 8;
		(*(uint16*)(result.data() + byte)) |= (uint16(waveform.at(i) & 0x1F) << shift);
	}
	return result;
}

QByteArray DocumentData::data() const {
	return _data;
}

const FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<DocumentData*>(this)->_location = Local::readFileLocation(mediaKey());
	}
	return _location;
}

void DocumentData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
}

QString DocumentData::filepath(FilePathResolveType type, bool forceSavingAs) const {
	bool check = (type != FilePathResolveCached);
	QString result = (check && _location.name().isEmpty()) ? QString() : location(check).name();
	bool saveFromData = result.isEmpty() && !data().isEmpty();
	if (saveFromData) {
		if (type != FilePathResolveSaveFromData && type != FilePathResolveSaveFromDataSilent) {
			saveFromData = false;
		} else if (type == FilePathResolveSaveFromDataSilent && (cAskDownloadPath() || forceSavingAs)) {
			saveFromData = false;
		}
	}
	if (saveFromData) {
		QString filename = documentSaveFilename(this, forceSavingAs);
		if (!filename.isEmpty()) {
			QFile f(filename);
			if (f.open(QIODevice::WriteOnly)) {
				if (f.write(data()) == data().size()) {
					f.close();
					const_cast<DocumentData*>(this)->_location = FileLocation(StorageFilePartial, filename);
					Local::writeFileLocation(mediaKey(), _location);
					result = filename;
				}
			}
		}
	}
	return result;
}

ImagePtr DocumentData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

bool fileIsImage(const QString &name, const QString &mime) {
	QString lowermime = mime.toLower(), namelower = name.toLower();
	if (lowermime.startsWith(qstr("image/"))) {
		return true;
	} else if (namelower.endsWith(qstr(".bmp"))
		|| namelower.endsWith(qstr(".jpg"))
		|| namelower.endsWith(qstr(".jpeg"))
		|| namelower.endsWith(qstr(".gif"))
		|| namelower.endsWith(qstr(".webp"))
		|| namelower.endsWith(qstr(".tga"))
		|| namelower.endsWith(qstr(".tiff"))
		|| namelower.endsWith(qstr(".tif"))
		|| namelower.endsWith(qstr(".psd"))
		|| namelower.endsWith(qstr(".png"))) {
		return true;
	}
	return false;
}

void DocumentData::recountIsImage() {
	if (isAnimation() || isVideo()) return;
	_duration = fileIsImage(name, mime) ? 1 : -1; // hack
}

void DocumentData::setRemoteLocation(int32 dc, uint64 access) {
	_dc = dc;
	_access = access;
	if (isValid()) {
		if (_location.check()) {
			Local::writeFileLocation(mediaKey(), _location);
		} else {
			_location = Local::readFileLocation(mediaKey());
		}
	}
}

void DocumentData::setContentUrl(const QString &url) {
	_url = url;
}

void DocumentData::collectLocalData(DocumentData *local) {
	if (local == this) return;

	if (!local->_data.isEmpty()) {
		_data = local->_data;
		if (voice()) {
			if (!Local::copyAudio(local->mediaKey(), mediaKey())) {
				Local::writeAudio(mediaKey(), _data);
			}
		} else {
			if (!Local::copyStickerImage(local->mediaKey(), mediaKey())) {
				Local::writeStickerImage(mediaKey(), _data);
			}
		}
	}
	if (!local->_location.isEmpty()) {
		_location = local->_location;
		Local::writeFileLocation(mediaKey(), _location);
	}
}

DocumentData::~DocumentData() {
	if (loading()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = nullptr;
	}
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, DocumentData *document, PhotoData *photo, int32 duration, const QString &author, int32 pendingTill) : id(id)
, type(type)
, url(url)
, displayUrl(displayUrl)
, siteName(siteName)
, title(title)
, description(description)
, duration(duration)
, author(author)
, photo(photo)
, document(document)
, pendingTill(pendingTill) {
}

void PeerOpenClickHandler::onClickImpl() const {
	if (App::main()) {
		if (peer() && peer()->isChannel() && App::main()->historyPeer() != peer()) {
			if (!peer()->asChannel()->isPublic() && !peer()->asChannel()->amIn()) {
				Ui::showLayer(new InformBox(lang((peer()->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
			} else {
				Ui::showPeerHistory(peer(), ShowAtUnreadMsgId);
			}
		} else {
			App::main()->showPeerProfile(peer());
		}
	}
}

MsgId clientMsgId() {
	static MsgId currentClientMsgId = StartClientMsgId;
	Q_ASSERT(currentClientMsgId < EndClientMsgId);
	return currentClientMsgId++;
}
