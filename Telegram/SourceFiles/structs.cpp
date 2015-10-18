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
	static const ImagePtr userDefPhotos[8] = {
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
, photo(isChat() ? chatDefPhoto(colorIndex) : (isChannel() ? channelDefPhoto(colorIndex) : userDefPhoto(colorIndex)))
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
		delete botInfo;
		botInfo = 0;
	} else if (!botInfo) {
		botInfo = new BotInfo();
		botInfo->version = version;
	} else if (botInfo->version < version) {
		if (!botInfo->commands.isEmpty()) {
			botInfo->commands.clear();
			if (App::main()) App::main()->botCommandsChanged(this);
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
		if (botInfo && !botInfo->commands.isEmpty()) {
			if (App::main()) App::main()->botCommandsChanged(this);
		}
		delete botInfo;
		botInfo = 0;
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

		if (changedCommands && App::main()) {
			App::main()->botCommandsChanged(this);
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
		newPhoto = newPhotoLoc.isNull() ? channelDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
//		photoFull = ImagePtr(640, 640, d.vphoto_big, channelDefPhoto(colorIndex));
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = channelDefPhoto(colorIndex);
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

void PhotoLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton) {
		App::wnd()->showPhoto(this, App::hoveredLinkItem());
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
	VideoData *data = video();
	if (!data->date || button != Qt::LeftButton) return;

	QString already = data->already(true);
	if (!already.isEmpty()) {
		psOpenFile(already);
		if (App::main()) App::main()->videoMarkRead(data);
		return;
	}

	if (data->status != FileReady) return;

	QString filename = saveFileName(lang(lng_save_video), qsl("MOV Video (*.mov);;All files (*.*)"), qsl("video"), qsl(".mov"), false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
		data->save(filename);
	}
}

void VideoSaveLink::doSave(VideoData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
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
			if (forceSavingAs) {
				data->cancel();
			} else if (!already.isEmpty()) {
				data->openOnSave = -1;
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : FullMsgId();
			}
			data->save(filename);
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

VideoData::VideoData(const VideoId &id, const uint64 &access, int32 date, int32 duration, int32 w, int32 h, const ImagePtr &thumb, int32 dc, int32 size) :
id(id), access(access), date(date), duration(duration), w(w), h(h), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), fileType(0), openOnSave(0), loader(0) {
	location = Local::readFileLocation(mediaKey(VideoFileLocation, dc, id));
}

void VideoData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, VideoFileLocation, toFile, size);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(videoLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(videoLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString VideoData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(VideoFileLocation, dc, id));
	return location.name;
}

void AudioOpenLink::onClick(Qt::MouseButton button) const {
	AudioData *data = audio();
	if (!data->date || button != Qt::LeftButton) return;

	QString already = data->already(true);
	bool play = App::hoveredLinkItem() && audioPlayer();
	if (!already.isEmpty() || (!data->data.isEmpty() && play)) {
		if (play) {
			AudioMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing.msgId == App::hoveredLinkItem()->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewAudios);
			} else {
				audioPlayer()->play(AudioMsgId(data, App::hoveredLinkItem()->fullId()));
				if (App::main()) App::main()->audioMarkRead(data);
			}
		} else {
			psOpenFile(already);
			if (App::main()) App::main()->audioMarkRead(data);
		}
		return;
	}

	if (data->status != FileReady) return;

	bool mp3 = (data->mime == qstr("audio/mp3"));
	QString filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), mp3 ? qsl(".mp3") : qsl(".ogg"), false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
		data->save(filename);
	}
}

void AudioSaveLink::doSave(AudioData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
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
			if (forceSavingAs) {
				data->cancel();
			} else if (!already.isEmpty()) {
				data->openOnSave = -1;
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			}
			data->save(filename);
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

	data->cancel();
}

bool StickerData::setInstalled() const {
	switch (set.type()) {
	case mtpc_inputStickerSetID: {
		return (cStickerSets().constFind(set.c_inputStickerSetID().vid.v) != cStickerSets().cend());
	} break;
	case mtpc_inputStickerSetShortName: {
		QString name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (StickerSets::const_iterator i = cStickerSets().cbegin(), e = cStickerSets().cend(); i != e; ++i) {
			if (i->shortName.toLower() == name) return true;
		}
	} break;
	}
	return false;
}

AudioData::AudioData(const AudioId &id, const uint64 &access, int32 date, const QString &mime, int32 duration, int32 dc, int32 size) :
id(id), access(access), date(date), mime(mime), duration(duration), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), loader(0) {
	location = Local::readFileLocation(mediaKey(AudioFileLocation, dc, id));
}

void AudioData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, AudioFileLocation, toFile, size, (size < AudioVoiceMsgInMemory));
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(audioLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(audioLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString AudioData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(AudioFileLocation, dc, id));
	return location.name;
}

void DocumentOpenLink::doOpen(DocumentData *data) {
	if (!data->date) return;

	bool play = data->song() && App::hoveredLinkItem() && audioPlayer();
	QString already = data->already(true);
	if (!already.isEmpty() || (!data->data.isEmpty() && play)) {
		if (play) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			audioPlayer()->currentState(&playing, &playingState);
			if (playing.msgId == App::hoveredLinkItem()->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(OverviewDocuments);
			} else {
				SongMsgId song(data, App::hoveredLinkItem()->fullId());
				audioPlayer()->play(song);
				if (App::main()) App::main()->documentPlayProgress(song);
			}
		} else if (data->size < MediaViewImageSizeLimit) {
			QImageReader reader(already);
			if (reader.canRead()) {
				if (reader.supportsAnimation() && reader.imageCount() > 1 && App::hoveredLinkItem()) {
					startGif(App::hoveredLinkItem(), already);
				} else if (App::hoveredLinkItem() || App::contextItem()) {
					App::wnd()->showDocument(data, App::hoveredLinkItem() ? App::hoveredLinkItem() : App::contextItem());
				} else {
					psOpenFile(already);
				}
			} else {
				psOpenFile(already);
			}
		} else {
			psOpenFile(already);
		}
		return;
	}

	if (data->status != FileReady) return;

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

	QString filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
		data->save(filename);
	}
}

void DocumentOpenLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doOpen(document());
}

void DocumentSaveLink::doSave(DocumentData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
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
			if (forceSavingAs) {
				data->cancel();
			} else if (!already.isEmpty()) {
				data->openOnSave = -1;
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			}
			data->save(filename);
		}
	}
}

void DocumentSaveLink::onClick(Qt::MouseButton button) const {
	if (button != Qt::LeftButton) return;
	doSave(document());
}

void DocumentCancelLink::onClick(Qt::MouseButton button) const {
	DocumentData *data = document();
	if (!data->date || button != Qt::LeftButton) return;

	data->cancel();
}

DocumentData::DocumentData(const DocumentId &id, const uint64 &access, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size) :
id(id), type(FileDocument), access(access), date(date), mime(mime), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), loader(0), _additional(0) {
	setattributes(attributes);
	location = Local::readFileLocation(mediaKey(DocumentFileLocation, dc, id));
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			const MTPDdocumentAttributeImageSize &d(attributes[i].c_documentAttributeImageSize());
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument) {
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
				sticker->alt = qs(d.valt);
				sticker->set = d.vstickerset;
			}
		} break;
		case mtpc_documentAttributeVideo: {
			const MTPDdocumentAttributeVideo &d(attributes[i].c_documentAttributeVideo());
			type = VideoDocument;
//			duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			const MTPDdocumentAttributeAudio &d(attributes[i].c_documentAttributeAudio());
			type = SongDocument;
			SongData *song = new SongData();
			_additional = song;
			song->duration = d.vduration.v;
			song->title = qs(d.vtitle);
			song->performer = qs(d.vperformer);
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

void DocumentData::save(const QString &toFile) {
	cancel(true);
	bool isSticker = (type == StickerDocument) && (dimensions.width() > 0) && (dimensions.height() > 0) && (size < StickerInMemory);
	loader = new mtpFileLoader(dc, id, access, DocumentFileLocation, toFile, size, isSticker);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(documentLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(documentLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString DocumentData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(DocumentFileLocation, dc, id));
	return location.name;
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, DocumentData *doc, int32 duration, const QString &author, int32 pendingTill) :
id(id), type(type), url(url), displayUrl(displayUrl), siteName(siteName), title(title), description(description), duration(duration), author(author), photo(photo), doc(doc), pendingTill(pendingTill) {
}

void PeerLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		if (peer() && peer()->isChannel() && App::main()->historyPeer() != peer()) {
			if (!peer()->asChannel()->isPublic() && !peer()->asChannel()->amIn()) {
				App::wnd()->showLayer(new InformBox(lang(lng_channel_not_accessible)));
			} else {
				App::main()->showPeerHistory(peer()->id, ShowAtUnreadMsgId);
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
		App::main()->showPeerHistory(peer(), msgid());
	}
}

void CommentsLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main() && _item->history()->isChannel()) {
		App::main()->showPeerHistory(_item->history()->peer->id, _item->id);
	}
}

MsgId clientMsgId() {
	static MsgId current = -2000000000;
	return ++current;
}
