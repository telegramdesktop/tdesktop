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

#include "history.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "window.h"
#include "gui/filedialog.h"

#include "boxes/confirmbox.h"

#include "audio.h"
#include "localstorage.h"

namespace {
	int32 peerColorIndex(const PeerId &peer) {
		int32 myId(MTP::authedId()), peerId(peerToBareInt(peer));
		QByteArray both(qsl("%1%2").arg(peerId).arg(myId).toUtf8());
		if (both.size() > 15) {
			both = both.mid(0, 15);
		}
		uchar md5[16];
		hashMd5(both.constData(), both.size(), md5);
		return (md5[peerId & 0x0F] & (peerIsUser(peer) ? 0x07 : 0x03));
	}
}

style::color peerColor(int32 index) {
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

ImagePtr userDefPhoto(int32 index) {
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

ImagePtr chatDefPhoto(int32 index) {
	static const ImagePtr chatDefPhotos[4] = {
		ImagePtr(qsl(":/ava/art/chatcolor1.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor2.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor3.png"), "PNG"),
		ImagePtr(qsl(":/ava/art/chatcolor4.png"), "PNG"),
	};
	return chatDefPhotos[index];
}

ImagePtr channelDefPhoto(int32 index) {
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

PeerData::PeerData(const PeerId &id) : id(id), lnk(new PeerLink(this))
, loaded(false)
, colorIndex(peerColorIndex(id))
, color(peerColor(colorIndex))
, photo((isChat() || isMegagroup()) ? chatDefPhoto(colorIndex) : (isChannel() ? channelDefPhoto(colorIndex) : userDefPhoto(colorIndex)))
, photoId(UnknownPeerPhotoId)
, nameVersion(0)
, notify(UnknownNotifySettings)
{
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
				asChannel()->flags &= ~MTPDchannel::flag_username;
			} else {
				asChannel()->flags |= MTPDchannel::flag_username;
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

const Text &BotCommand::descriptionText() const {
	if (_descriptionText.isEmpty() && !_description.isEmpty()) {
		_descriptionText.setText(st::mentionFont, _description, _textNameOptions);
	}
	return _descriptionText;
}

void UserData::setPhoto(const MTPUserProfilePhoto &p) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = photo;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_userProfilePhoto: {
		const MTPDuserProfilePhoto d(p.c_userProfilePhoto());
		newPhotoId = d.vphoto_id.v;
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? userDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
		//App::feedPhoto(App::photoFromUserPhoto(peerToUser(id), MTP_int(unixtime()), p));
	} break;
	default: {
		newPhotoId = 0;
		if (id == ServiceUserId) {
			if (photo->isNull()) {
				newPhoto = ImagePtr(QPixmap::fromImage(App::wnd()->iconLarge().scaledToWidth(160, Qt::SmoothTransformation), Qt::ColorOnly), "PNG");
			}
		} else {
			newPhoto = userDefPhoto(colorIndex);
		}
		newPhotoLoc = StorageImageLocation();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != photo.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		photo = newPhoto;
		photoLoc = newPhotoLoc;
		emit App::main()->peerPhotoChanged(this);
	}
}

void PeerData::fillNames() {
	names.clear();
	chars.clear();
	QString toIndex = textAccentFold(name);
	if (isUser()) {
		if (!asUser()->nameOrPhone.isEmpty() && asUser()->nameOrPhone != name) toIndex += ' ' + textAccentFold(asUser()->nameOrPhone);
		if (!asUser()->username.isEmpty()) toIndex += ' ' + textAccentFold(asUser()->username);
	} else if (isChannel()) {
		if (!asChannel()->username.isEmpty()) toIndex += ' ' + textAccentFold(asChannel()->username);
	}
	if (cRussianLetters().match(toIndex).hasMatch()) {
		toIndex += ' ' + translitRusEng(toIndex);
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

void UserData::setBotInfoVersion(int32 version) {
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
		botInfo->shareText.clear();
		botInfo->version = version;
		botInfo->inited = false;
	}
}

void UserData::setBotInfo(const MTPBotInfo &info) {
	switch (info.type()) {
	case mtpc_botInfoEmpty:
		if (botInfo) {
			if (!botInfo->commands.isEmpty()) {
				botInfo->commands.clear();
				Notify::botCommandsChanged(this);
			}
			delete botInfo;
			botInfo = 0;
			Notify::userIsBotChanged(this);
		}
	break;
	case mtpc_botInfo: {
		const MTPDbotInfo &d(info.c_botInfo());
		if (peerFromUser(d.vuser_id.v) != id) return;

		if (botInfo) {
			botInfo->version = d.vversion.v;
		} else {
			setBotInfoVersion(d.vversion.v);
		}

		QString desc = qs(d.vdescription);
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Text(st::msgMinWidth);
		}
		botInfo->shareText = qs(d.vshare_text);

		const QVector<MTPBotCommand> &v(d.vcommands.c_vector().v);
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
	ImagePtr newPhoto = photo;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const MTPDchatPhoto d(p.c_chatPhoto());
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
	if (newPhotoId != photoId || newPhoto.v() != photo.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		photo = newPhoto;
		photoLoc = newPhotoLoc;
		emit App::main()->peerPhotoChanged(this);
	}
}

void ChannelData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = photo;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const MTPDchatPhoto d(p.c_chatPhoto());
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
	if (newPhotoId != photoId || newPhoto.v() != photo.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		photo = newPhoto;
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

void PhotoLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton) {
		App::wnd()->showPhoto(this, App::hoveredLinkItem() ? App::hoveredLinkItem() : App::contextItem());
	}
}

void PhotoSaveLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;

	PhotoData *data = photo();
	if (!data->date) return;

	data->download();
}

void PhotoCancelLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;

	PhotoData *data = photo();
	if (!data->date) return;

	if (data->uploading()) {
		HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);
		if (HistoryMessage *msg = item->toHistoryMessage()) {
			if (msg->getMedia() && msg->getMedia()->type() == MediaTypePhoto && static_cast<HistoryPhoto*>(msg->getMedia())->photo() == data) {
				App::contextItem(item);
				App::main()->deleteLayer(-2);
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

void VideoOpenLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	VideoData *data = video();

	if (!data->date) return;

	HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);

	const FileLocation &location(data->location(true));
	if (!location.isEmpty()) {
		psOpenFile(location.name());
		if (App::main()) App::main()->videoMarkRead(data);
		return;
	}

	if (data->status != FileReady) return;

	QString filename;
	if (!data->saveToCache()) {
		filename = saveFileName(lang(lng_save_video), qsl("MOV Video (*.mov);;All files (*.*)"), qsl("video"), qsl(".mov"), false);
		if (filename.isEmpty()) return;
	}

	data->save(filename, ActionOnLoadOpen, item ? item->fullId() : FullMsgId());
}

void VideoSaveLink::doSave(VideoData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	bool openWith = !already.isEmpty();
	if (openWith && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
			psOpenFile(already, true);
		}
	} else {
		QFileInfo alreadyInfo(already);
		QDir alreadyDir(already.isEmpty() ? QDir() : alreadyInfo.dir());
		QString name = already.isEmpty() ? QString(".mov") : alreadyInfo.fileName();
		QString filename = saveFileName(lang(lng_save_video), qsl("MOV Video (*.mov);;All files (*.*)"), qsl("video"), name, forceSavingAs, alreadyDir);
		if (!filename.isEmpty()) {
			ActionOnLoad action = already.isEmpty() ? ActionOnLoadNone : ActionOnLoadOpenWith;
			FullMsgId actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(filename, action, actionMsgId);
		}
	}
}

void VideoSaveLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doSave(video());
}

void VideoCancelLink::onClick(Qt::MouseButton button) const {
	VideoData *data = video();
	if (!data->date || button != Qt::LeftButton) return;

	data->cancel();
}

VideoData::VideoData(const VideoId &id, const uint64 &access, int32 date, int32 duration, int32 w, int32 h, const ImagePtr &thumb, int32 dc, int32 size)
: id(id)
, access(access)
, date(date)
, duration(duration)
, w(w)
, h(h)
, thumb(thumb)
, dc(dc)
, size(size)
, status(FileReady)
, uploadOffset(0)
, _actionOnLoad(ActionOnLoadNone)
, _loader(0) {
	_location = Local::readFileLocation(mediaKey(VideoFileLocation, dc, id));
}

void VideoData::forget() {
	replyPreview->forget();
	thumb->forget();
}

void VideoData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	const FileLocation &loc(location(true));
	QString already = loc.name();
	if (already.isEmpty()) return;

	if (_actionOnLoad == ActionOnLoadOpenWith) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
			psOpenFile(already, true);
		}
	} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
		psOpenFile(already);
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool VideoData::loaded(bool check) const {
	if (loading() && _loader->done()) {
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = CancelledMtpFileLoader;
		} else {
			VideoData *that = const_cast<VideoData*>(this);
			that->_location = FileLocation(mtpToStorageType(_loader->fileType()), _loader->fileName());

			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = 0;
		}
		notifyLayoutChanged();
	}
	return !already(check).isEmpty();
}

bool VideoData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

bool VideoData::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : uploading();
}

float64 VideoData::progress() const {
	if (uploading()) {
		if (size > 0) {
			return float64(uploadOffset) / size;
		}
		return 0;
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);
}

int32 VideoData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool VideoData::uploading() const {
	return status == FileUploading;
}

void VideoData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded(true)) {
		const FileLocation &l(location(true));
		if (!toFile.isEmpty()) {
			if (l.accessEnable()) {
				QFile(l.name()).copy(toFile);
				l.accessDisable();
			}
		}
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
			_loader = 0;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		_loader = new mtpFileLoader(dc, id, access, VideoFileLocation, toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(videoLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(videoLoadFailed(FileLoader*,bool)));
		_loader->start();
	}

	notifyLayoutChanged();
}

void VideoData::cancel() {
	if (!loading()) return;

	mtpFileLoader *l = _loader;
	_loader = CancelledMtpFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->rpcInvalidate();

		notifyLayoutChanged();
	}
	_actionOnLoad = ActionOnLoadNone;
}

void VideoData::notifyLayoutChanged() const {
	const VideoItems &items(App::videoItems());
	VideoItems::const_iterator i = items.constFind(const_cast<VideoData*>(this));
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Notify::historyItemLayoutChanged(j.key());
		}
	}
}

QString VideoData::already(bool check) const {
	return location(check).name();
}

QByteArray VideoData::data() const {
	return QByteArray();
}

const FileLocation &VideoData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<VideoData*>(this)->_location = Local::readFileLocation(mediaKey(VideoFileLocation, dc, id));
	}
	return _location;
}

void VideoData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
}

void AudioOpenLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	AudioData *data = audio();

	if (!data->date) return;

	HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);

	bool play = audioPlayer() && item;
	const FileLocation &location(data->location(true));
	if (!location.isEmpty() || (!data->data().isEmpty() && play)) {
		if (play) {
			AudioMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing.msgId == item->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewAudios);
			} else {
				AudioMsgId audio(data, item->fullId());
				audioPlayer()->play(audio);
				if (App::main()) {
					App::main()->audioPlayProgress(audio);
					App::main()->audioMarkRead(data);
				}
			}
		} else {
			psOpenFile(location.name());
			if (App::main()) App::main()->audioMarkRead(data);
		}
		return;
	}

	if (data->status != FileReady) return;

	QString filename;
	if (!data->saveToCache()) {
		bool mp3 = (data->mime == qstr("audio/mp3"));
		filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), mp3 ? qsl(".mp3") : qsl(".ogg"), false);

		if (filename.isEmpty()) return;
	}

	data->save(filename, ActionOnLoadOpen, item ? item->fullId() : FullMsgId());
}

void AudioSaveLink::doSave(AudioData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	bool openWith = !already.isEmpty();
	if (openWith && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
			psOpenFile(already, true);
		}
	} else {
		QFileInfo alreadyInfo(already);
		QDir alreadyDir(already.isEmpty() ? QDir() : alreadyInfo.dir());
		bool mp3 = (data->mime == qstr("audio/mp3"));
		QString name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : alreadyInfo.fileName();
		QString filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), name, forceSavingAs, alreadyDir);
		if (!filename.isEmpty()) {
			ActionOnLoad action = already.isEmpty() ? ActionOnLoadNone : ActionOnLoadOpenWith;
			FullMsgId actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(filename, action, actionMsgId);
		}
	}
}

void AudioSaveLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doSave(audio());
}

void AudioCancelLink::onClick(Qt::MouseButton button) const {
	AudioData *data = audio();
	if (!data->date || button != Qt::LeftButton) return;

	if (data->uploading()) {
		HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);
		if (HistoryMessage *msg = item->toHistoryMessage()) {
			if (msg->getMedia() && msg->getMedia()->type() == MediaTypeAudio && static_cast<HistoryAudio*>(msg->getMedia())->audio() == data) {
				App::contextItem(item);
				App::main()->deleteLayer(-2);
			}
		}
	} else {
		data->cancel();
	}
}

bool StickerData::setInstalled() const {
	switch (set.type()) {
	case mtpc_inputStickerSetID: {
		StickerSets::const_iterator it = cStickerSets().constFind(set.c_inputStickerSetID().vid.v);
		return (it != cStickerSets().cend()) && !(it->flags & MTPDstickerSet::flag_disabled);
	} break;
	case mtpc_inputStickerSetShortName: {
		QString name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (StickerSets::const_iterator it = cStickerSets().cbegin(), e = cStickerSets().cend(); it != e; ++it) {
			if (it->shortName.toLower() == name) {
				return !(it->flags & MTPDstickerSet::flag_disabled);
			}
		}
	} break;
	}
	return false;
}

AudioData::AudioData(const AudioId &id, const uint64 &access, int32 date, const QString &mime, int32 duration, int32 dc, int32 size)
: id(id)
, access(access)
, date(date)
, mime(mime)
, duration(duration)
, dc(dc)
, size(size)
, status(FileReady)
, uploadOffset(0)
, _actionOnLoad(ActionOnLoadNone)
, _loader(0) {
	_location = Local::readFileLocation(mediaKey(AudioFileLocation, dc, id));
}

bool AudioData::saveToCache() const {
	return size < AudioVoiceMsgInMemory;
}

void AudioData::forget() {
	_data.clear();
}

void AudioData::automaticLoad(const HistoryItem *item) {
	if (loaded() || status != FileReady) return;

	if (saveToCache() && _loader != CancelledMtpFileLoader) {
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

void AudioData::automaticLoadSettingsChanged() {
	if (loaded() || status != FileReady || !saveToCache() || _loader != CancelledMtpFileLoader) return;
	_loader = 0;
}

void AudioData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	const FileLocation &loc(location(true));
	QString already = loc.name();
	bool play = _actionOnLoadMsgId.msg && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && audioPlayer();

	if (play) {
		if (loaded()) {
			AudioMsgId playing;
			AudioPlayerState state = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &state);
			if (playing.msgId == _actionOnLoadMsgId && !(state & AudioPlayerStoppedMask) && state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewAudios);
			} else {
				audioPlayer()->play(AudioMsgId(this, _actionOnLoadMsgId));
				if (App::main()) App::main()->audioMarkRead(this);
			}
		}
	} else {
		if (already.isEmpty()) return;
		if (_actionOnLoad == ActionOnLoadOpenWith) {
			if (already.isEmpty()) return;

			QPoint pos(QCursor::pos());
			if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
				psOpenFile(already, true);
			}
			if (App::main()) App::main()->audioMarkRead(this);
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			psOpenFile(already);
			if (App::main()) App::main()->audioMarkRead(this);
		}
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool AudioData::loaded(bool check) const {
	if (loading() && _loader->done()) {
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = CancelledMtpFileLoader;
		} else {
			AudioData *that = const_cast<AudioData*>(this);
			that->_location = FileLocation(mtpToStorageType(_loader->fileType()), _loader->fileName());
			that->_data = _loader->bytes();

			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = 0;
		}
		notifyLayoutChanged();
	}
	return !_data.isEmpty() || !already(check).isEmpty();
}

bool AudioData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

bool AudioData::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : uploading();
}

float64 AudioData::progress() const {
	if (uploading()) {
		if (size > 0) {
			return float64(uploadOffset) / size;
		}
		return 0;
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);
}

int32 AudioData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool AudioData::uploading() const {
	return status == FileUploading;
}

void AudioData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded(true)) {
		const FileLocation &l(location(true));
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
			} else if (l.accessEnable()) {
				QFile(l.name()).copy(toFile);
				l.accessDisable();
			}
		}
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
			_loader = 0;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		_loader = new mtpFileLoader(dc, id, access, AudioFileLocation, toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(audioLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(audioLoadFailed(FileLoader*,bool)));
		_loader->start();
	}

	notifyLayoutChanged();
}

void AudioData::cancel() {
	if (!loading()) return;

	mtpFileLoader *l = _loader;
	_loader = CancelledMtpFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->rpcInvalidate();

		notifyLayoutChanged();
	}
	_actionOnLoad = ActionOnLoadNone;
}

void AudioData::notifyLayoutChanged() const {
	const AudioItems &items(App::audioItems());
	AudioItems::const_iterator i = items.constFind(const_cast<AudioData*>(this));
	if (i != items.cend()) {
		for (HistoryItemsMap::const_iterator j = i->cbegin(), e = i->cend(); j != e; ++j) {
			Notify::historyItemLayoutChanged(j.key());
		}
	}
}

QString AudioData::already(bool check) const {
	return location(check).name();
}

QByteArray AudioData::data() const {
	return _data;
}

const FileLocation &AudioData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<AudioData*>(this)->_location = Local::readFileLocation(mediaKey(AudioFileLocation, dc, id));
	}
	return _location;
}

void AudioData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
}

void DocumentOpenLink::doOpen(DocumentData *data, ActionOnLoad action) {
	if (!data->date) return;

	HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);

	bool playMusic = data->song() && audioPlayer() && item;
	bool playAnimation = data->isAnimation() && item && item->getMedia();
	const FileLocation &location(data->location(true));
	if (!location.isEmpty() || (!data->data().isEmpty() && (playMusic || playAnimation))) {
		if (playMusic) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing.msgId == item->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewDocuments);
			} else {
				SongMsgId song(data, item->fullId());
				audioPlayer()->play(song);
				if (App::main()) App::main()->documentPlayProgress(song);
			}
		} else if (data->size < MediaViewImageSizeLimit) {
			if (!data->data().isEmpty() && playAnimation) {
				if (action == ActionOnLoadPlayInline) {
					item->getMedia()->playInline(item);
				} else {
					App::wnd()->showDocument(data, item);
				}
			} else if (location.accessEnable()) {
				if ((App::hoveredLinkItem() || App::contextItem()) && (data->isAnimation() || QImageReader(location.name()).canRead())) {
					if (action == ActionOnLoadPlayInline) {
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
		QString name = data->name, filter;
		MimeType mimeType = mimeTypeForName(data->mime);
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

		filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, false);

		if (filename.isEmpty()) return;
	}

	data->save(filename, action, item ? item->fullId() : FullMsgId());
}

void DocumentOpenLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doOpen(document());
}

void GifOpenLink::doOpen(DocumentData *data) {
	return DocumentOpenLink::doOpen(data, ActionOnLoadPlayInline);
}

void GifOpenLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doOpen(document());
}

void DocumentSaveLink::doSave(DocumentData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	bool openWith = !already.isEmpty();
	if (openWith && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
			psOpenFile(already, true);
		}
	} else {
		QFileInfo alreadyInfo(already);
		QDir alreadyDir(already.isEmpty() ? QDir() : alreadyInfo.dir());
		QString name = already.isEmpty() ? data->name : alreadyInfo.fileName(), filter;
		MimeType mimeType = mimeTypeForName(data->mime);
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

		QString filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, forceSavingAs, alreadyDir);
		if (!filename.isEmpty()) {
			ActionOnLoad action = already.isEmpty() ? ActionOnLoadNone : ActionOnLoadOpenWith;
			FullMsgId actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(filename, action, actionMsgId);
		}
	}
}

void DocumentSaveLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doSave(document());
}

void DocumentCancelLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;

	DocumentData *data = document();
	if (!data->date) return;

	if (data->uploading()) {
		HistoryItem *item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : 0);
		if (HistoryMessage *msg = item->toHistoryMessage()) {
			if (msg->getMedia() && msg->getMedia()->getDocument() == data) {
				App::contextItem(item);
				App::main()->deleteLayer(-2);
			}
		}
	} else {
		data->cancel();
	}
}

DocumentData::DocumentData(const DocumentId &id, const uint64 &access, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size) : id(id)
, type(FileDocument)
, access(access)
, date(date)
, mime(mime)
, thumb(thumb)
, dc(dc)
, size(size)
, status(FileReady)
, uploadOffset(0)
, _additional(0)
, _duration(-1)
, _actionOnLoad(ActionOnLoadNone)
, _loader(0) {
	_location = Local::readFileLocation(mediaKey(DocumentFileLocation, dc, id));
	setattributes(attributes);
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			const MTPDdocumentAttributeImageSize &d(attributes[i].c_documentAttributeImageSize());
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument || type == VideoDocument) {
			type = AnimatedDocument;
			delete _additional;
			_additional = 0;
		} break;
		case mtpc_documentAttributeSticker: {
			const MTPDdocumentAttributeSticker &d(attributes[i].c_documentAttributeSticker());
			if (type == FileDocument) {
				type = StickerDocument;
				StickerData *sticker = new StickerData();
				_additional = sticker;
			}
			if (sticker()) {
				sticker()->alt = qs(d.valt);
				sticker()->set = d.vstickerset;
			}
		} break;
		case mtpc_documentAttributeVideo: {
			const MTPDdocumentAttributeVideo &d(attributes[i].c_documentAttributeVideo());
			if (type == FileDocument) {
				type = VideoDocument;
			}
			_duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			const MTPDdocumentAttributeAudio &d(attributes[i].c_documentAttributeAudio());
			if (type == FileDocument) {
				type = SongDocument;
				SongData *song = new SongData();
				_additional = song;
			}
			if (song()) {
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
			delete _additional;
			_additional = 0;
		}
	}
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument) || (isAnimation() && size < AnimationInMemory);
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
		}
	}
}

void DocumentData::automaticLoadSettingsChanged() {
	if (loaded() || status != FileReady || !isAnimation() || !saveToCache() || _loader != CancelledMtpFileLoader) return;
	_loader = 0;
}

void DocumentData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	const FileLocation &loc(location(true));
	QString already = loc.name();
	HistoryItem *item = _actionOnLoadMsgId.msg ? App::histItemById(_actionOnLoadMsgId) : 0;
	bool showImage = item && (size < MediaViewImageSizeLimit);
	bool playMusic = song() && audioPlayer() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && item;
	bool playAnimation = isAnimation() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && showImage && item->getMedia();
	if (playMusic) {
		if (loaded()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing.msgId == item->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewDocuments);
			} else {
				SongMsgId song(this, item->fullId());
				audioPlayer()->play(song);
				if (App::main()) App::main()->documentPlayProgress(song);
			}
		}
	} else if (playAnimation) {
		if (loaded()) {
			if (_actionOnLoad == ActionOnLoadPlayInline) {
				item->getMedia()->playInline(item);
			} else {
				App::wnd()->showDocument(this, item);
			}
		}
	} else {
		if (already.isEmpty()) return;

		if (_actionOnLoad == ActionOnLoadOpenWith) {
			if (already.isEmpty()) return;

			QPoint pos(QCursor::pos());
			if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
				psOpenFile(already, true);
			}
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			if (loc.accessEnable()) {
				if (showImage && QImageReader(loc.name()).canRead()) {
					if (_actionOnLoad == ActionOnLoadPlayInline) {
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

bool DocumentData::loaded(bool check) const {
	if (loading() && _loader->done()) {
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = CancelledMtpFileLoader;
		} else {
			DocumentData *that = const_cast<DocumentData*>(this);
			that->_location = FileLocation(mtpToStorageType(_loader->fileType()), _loader->fileName());
			that->_data = _loader->bytes();
			if (that->sticker() && !_loader->imagePixmap().isNull()) {
				that->sticker()->img = ImagePtr(_data, _loader->imageFormat(), _loader->imagePixmap());
			}

			_loader->deleteLater();
			_loader->rpcInvalidate();
			_loader = 0;
		}
		notifyLayoutChanged();
	}
	return !_data.isEmpty() || !already(check).isEmpty();
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
	if (loaded(true)) {
		const FileLocation &l(location(true));
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
			} else if (l.accessEnable()) {
				QFile(l.name()).copy(toFile);
				l.accessDisable();
			}
		}
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
			_loader = 0;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		_loader = new mtpFileLoader(dc, id, access, DocumentFileLocation, toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(documentLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(documentLoadFailed(FileLoader*,bool)));
		_loader->start();
	}

	notifyLayoutChanged();
}

void DocumentData::cancel() {
	if (!loading()) return;

	mtpFileLoader *l = _loader;
	_loader = CancelledMtpFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->rpcInvalidate();

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
}

QString DocumentData::already(bool check) const {
	if (check && _location.name().isEmpty()) return QString();
	return location(check).name();
}

QByteArray DocumentData::data() const {
	return _data;
}

const FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<DocumentData*>(this)->_location = Local::readFileLocation(mediaKey(DocumentFileLocation, dc, id));
	}
	return _location;
}

void DocumentData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
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
	if (isAnimation() || type == VideoDocument) return;
	_duration = fileIsImage(name, mime) ? 1 : -1; // hack
}

DocumentData::~DocumentData() {
	delete _additional;
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, DocumentData *doc, int32 duration, const QString &author, int32 pendingTill) : id(id)
, type(type)
, url(url)
, displayUrl(displayUrl)
, siteName(siteName)
, title(title)
, description(description)
, duration(duration)
, author(author)
, photo(photo)
, doc(doc)
, pendingTill(pendingTill) {
}

void InlineResult::automaticLoadGif() {
	if (loaded() || type != qstr("gif") || (content_type != qstr("video/mp4") && content_type != "image/gif")) return;

	if (_loader != CancelledWebFileLoader) {
		// if load at least anywhere
		bool loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
		saveFile(QString(), loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
	}
}

void InlineResult::automaticLoadSettingsChangedGif() {
	if (loaded() || _loader != CancelledWebFileLoader) return;
	_loader = 0;
}

void InlineResult::saveFile(const QString &toFile, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded()) {
		return;
	}

	if (_loader == CancelledWebFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancelFile();
			_loader = 0;
		}
	}

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		_loader = new webFileLoader(content_url, toFile, fromCloud, autoLoading);
		App::regInlineResultLoader(_loader, this);

		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(inlineResultLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(inlineResultLoadFailed(FileLoader*,bool)));
		_loader->start();
	}
}

void InlineResult::cancelFile() {
	if (!loading()) return;

	App::unregInlineResultLoader(_loader);

	webFileLoader *l = _loader;
	_loader = CancelledWebFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->stop();
	}
}

QByteArray InlineResult::data() const {
	return _data;
}

bool InlineResult::loading() const {
	return _loader && _loader != CancelledWebFileLoader;
}

bool InlineResult::loaded() const {
	if (loading() && _loader->done()) {
		App::unregInlineResultLoader(_loader);
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->stop();
			_loader = CancelledWebFileLoader;
		} else {
			InlineResult *that = const_cast<InlineResult*>(this);
			that->_data = _loader->bytes();

			_loader->deleteLater();
			_loader->stop();
			_loader = 0;
		}
	}
	return !_data.isEmpty();
}

bool InlineResult::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : false;
}

void InlineResult::forget() {
	thumb->forget();
	_data.clear();
}

float64 InlineResult::progress() const {
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);	return false;
}

InlineResult::~InlineResult() {
	cancelFile();
}

void PeerLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
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

void MessageLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		HistoryItem *current = App::mousedItem();
		if (current && current->history()->peer->id == peer()) {
			App::main()->pushReplyReturn(current);
		}
		Ui::showPeerHistory(peer(), msgid());
	}
}

void CommentsLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main() && _item->history()->isChannel()) {
		Ui::showPeerHistoryAtItem(_item);
	}
}

MsgId clientMsgId() {
	static MsgId currentClientMsgId = StartClientMsgId;
	Q_ASSERT(currentClientMsgId < EndClientMsgId);
	return currentClientMsgId++;
}
