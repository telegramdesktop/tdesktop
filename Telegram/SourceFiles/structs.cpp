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

#include "history.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "window.h"
#include "gui/filedialog.h"

#include "audio.h"
#include "localstorage.h"

namespace {
	int32 peerColorIndex(const PeerId &peer) {
		int32 myId(MTP::authedId()), peerId(peer & 0xFFFFFFFFL);
		bool chat = (peer & 0x100000000L);
		if (chat) {
			int ch = 0;
		}
		QByteArray both(qsl("%1%2").arg(peerId).arg(myId).toUtf8());
		if (both.size() > 15) {
			both = both.mid(0, 15);
		}
		uchar md5[16];
		hashMd5(both.constData(), both.size(), md5);
		return (md5[peerId & 0x0F] & (chat ? 0x03 : 0x07));
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

NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
NotifySettingsPtr globalNotifyAllPtr = UnknownNotifySettings, globalNotifyUsersPtr = UnknownNotifySettings, globalNotifyChatsPtr = UnknownNotifySettings;

PeerData::PeerData(const PeerId &id) : id(id)
, loaded(false)
, chat(App::isChat(id))
, access(0)
, colorIndex(peerColorIndex(id))
, color(peerColor(colorIndex))
, photo(chat ? chatDefPhoto(colorIndex) : userDefPhoto(colorIndex))
, nameVersion(0)
, notify(UnknownNotifySettings)
{
}

UserData *PeerData::asUser() {
	return chat ? App::user(id & 0xFFFFFFFFL) : static_cast<UserData *>(this);
}

const UserData *PeerData::asUser() const {
	return chat ? App::user(id & 0xFFFFFFFFL) : static_cast<const UserData *>(this);
}

ChatData *PeerData::asChat() {
	return chat ? static_cast<ChatData *>(this) : App::chat(id | 0x100000000L);
}

const ChatData *PeerData::asChat() const {
	return chat ? static_cast<const ChatData *>(this) : App::chat(id | 0x100000000L);
}

void PeerData::updateName(const QString &newName, const QString &newNameOrPhone, const QString &newUsername) {
	if (name == newName && nameOrPhone == newNameOrPhone && (chat || asUser()->username == newUsername) && nameVersion > 0) return;

	++nameVersion;
	name = newName;
	nameOrPhone = newNameOrPhone;
	if (!chat) asUser()->username = newUsername;
	Names oldNames = names;
	NameFirstChars oldChars = chars;
	fillNames();
	App::history(id)->updateNameText();
	if (App::main()) {
		emit App::main()->peerNameChanged(this, oldNames, oldChars);
	}
	nameUpdated();
}

void UserData::setPhoto(const MTPUserProfilePhoto &p) {
	switch (p.type()) {
	case mtpc_userProfilePhoto: {
		const MTPDuserProfilePhoto d(p.c_userProfilePhoto());
		photoId = d.vphoto_id.v;
		photo = ImagePtr(160, 160, d.vphoto_small, userDefPhoto(colorIndex));
		//		App::feedPhoto(App::photoFromUserPhoto(MTP_int(id & 0xFFFFFFFF), MTP_int(unixtime()), p));
	} break;
	default: {
		photoId = 0;
		if (id == ServiceUserId) {
			photo = ImagePtr(QPixmap::fromImage(App::wnd()->iconLarge().scaledToWidth(160, Qt::SmoothTransformation), Qt::ColorOnly), "PNG");
		} else {
			photo = userDefPhoto(colorIndex);
		}
	} break;
	}
	emit App::main()->peerPhotoChanged(this);
}

void PeerData::fillNames() {
	names.clear();
	chars.clear();
	QString toIndex = textAccentFold(name);
	if (nameOrPhone != name) {
		toIndex += ' ' + textAccentFold(nameOrPhone);
	}
	if (!chat) {
		toIndex += ' ' + textAccentFold(asUser()->username);
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
		updateName(lastName.isEmpty() ? firstName : (firstName + ' ' + lastName), phoneName, usern);
	}
	if (updUsername) {
		if (App::main()) {
			App::main()->peerUsernameChanged(this);
		}
	}
}

void UserData::setPhone(const QString &newPhone) {
	phone = newPhone;
	++nameVersion;
}

void UserData::nameUpdated() {
	nameText.setText(st::msgNameFont, name, _textNameOptions);
}

void ChatData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) {
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const MTPDchatPhoto d(p.c_chatPhoto());
		photo = ImagePtr(160, 160, d.vphoto_small, chatDefPhoto(colorIndex));
		photoFull = ImagePtr(640, 640, d.vphoto_big, chatDefPhoto(colorIndex));
		if (phId) {
			photoId = phId;
		}
	} break;
	default: {
		photo = chatDefPhoto(colorIndex);
		photoFull = ImagePtr();
		photoId = 0;
	} break;
	}
	emit App::main()->peerPhotoChanged(this);
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

		return filedialogGetSaveFile(name, title, filter, name) ? name : QString();
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
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

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
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
		data->save(filename);
	}
}

void VideoSaveLink::doSave(VideoData *data, bool forceSavingAs) {
	if (!data->user && !data->date) return;

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
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
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
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	data->cancel();
}

VideoData::VideoData(const VideoId &id, const uint64 &access, int32 user, int32 date, int32 duration, int32 w, int32 h, const ImagePtr &thumb, int32 dc, int32 size) :
id(id), access(access), user(user), date(date), duration(duration), w(w), h(h), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), fileType(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
	location = Local::readFileLocation(mediaKey(mtpc_inputVideoFileLocation, dc, id));
}

void VideoData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputVideoFileLocation, toFile, size);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(videoLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(videoLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString VideoData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(mtpc_inputVideoFileLocation, dc, id));
	return location.name;
}

void AudioOpenLink::onClick(Qt::MouseButton button) const {
	AudioData *data = audio();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	bool mp3 = (data->mime == QLatin1String("audio/mp3"));

	QString already = data->already(true);
	bool play = !mp3 && audioVoice();
	if (!already.isEmpty() || (!data->data.isEmpty() && play)) {
		if (play) {
			AudioData *playing = 0;
			VoiceMessageState playingState = VoiceMessageStopped;
			audioVoice()->currentState(&playing, &playingState);
			if (playing == data && playingState != VoiceMessageStopped) {
				audioVoice()->pauseresume();
			} else {
				audioVoice()->play(data);
				if (App::main()) App::main()->audioMarkRead(data);
			}
		} else {
			psOpenFile(already);
			if (App::main()) App::main()->audioMarkRead(data);
		}
		return;
	}

	if (data->status != FileReady) return;

	QString filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), mp3 ? qsl(".mp3") : qsl(".ogg"), false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
		data->save(filename);
	}
}

void AudioSaveLink::doSave(AudioData *data, bool forceSavingAs) {
	if (!data->user && !data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
			psOpenFile(already, true);
		}
	} else {
		QFileInfo alreadyInfo(already);
		QDir alreadyDir(already.isEmpty() ? QDir() : alreadyInfo.dir());
		bool mp3 = (data->mime == QLatin1String("audio/mp3"));
		QString name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : alreadyInfo.fileName();
		QString filename = saveFileName(lang(lng_save_audio), mp3 ? qsl("MP3 Audio (*.mp3);;All files (*.*)") : qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), name, forceSavingAs, alreadyDir);
		if (!filename.isEmpty()) {
			if (forceSavingAs) {
				data->cancel();
			} else if (!already.isEmpty()) {
				data->openOnSave = -1;
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
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
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	data->cancel();
}

AudioData::AudioData(const AudioId &id, const uint64 &access, int32 user, int32 date, const QString &mime, int32 duration, int32 dc, int32 size) :
id(id), access(access), user(user), date(date), mime(mime), duration(duration), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
	location = Local::readFileLocation(mediaKey(mtpc_inputAudioFileLocation, dc, id));
}

void AudioData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputAudioFileLocation, toFile, size, (size < AudioVoiceMsgInMemory));
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(audioLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(audioLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString AudioData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(mtpc_inputAudioFileLocation, dc, id));
	return location.name;
}

void DocumentOpenLink::onClick(Qt::MouseButton button) const {
	DocumentData *data = document();
	if (!data->date || button != Qt::LeftButton) return;

	QString already = data->already(true);
	if (!already.isEmpty()) {
		if (data->size < MediaViewImageSizeLimit) {
			QImageReader reader(already);
			if (reader.canRead()) {
				if (reader.supportsAnimation() && reader.imageCount() > 1 && App::hoveredLinkItem()) {
					startGif(App::hoveredLinkItem(), already);
				} else {
					App::wnd()->showDocument(data, App::hoveredLinkItem());
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
		filter = qsl("All files (*.*)");
	} else {
		filter = mimeType.filterString() + qsl(";;All files (*.*)");
	}

	QString filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
		data->save(filename);
	}
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
			filter = qsl("All files (*.*)");
		} else {
			filter = mimeType.filterString() + qsl(";;All files (*.*)");
		}

		QString filename = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, forceSavingAs, alreadyDir);
		if (!filename.isEmpty()) {
			if (forceSavingAs) {
				data->cancel();
			} else if (!already.isEmpty()) {
				data->openOnSave = -1;
				data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
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
id(id), type(FileDocument), duration(0), access(access), date(date), mime(mime), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
	setattributes(attributes);
	location = Local::readFileLocation(mediaKey(mtpc_inputDocumentFileLocation, dc, id));
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			const MTPDdocumentAttributeImageSize &d(attributes[i].c_documentAttributeImageSize());
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument) type = AnimatedDocument; break;
		case mtpc_documentAttributeSticker: {
			const MTPDdocumentAttributeSticker &d(attributes[i].c_documentAttributeSticker());
			if (type == FileDocument) type = StickerDocument;
			alt = qs(d.valt);
		} break;
		case mtpc_documentAttributeVideo: {
			const MTPDdocumentAttributeVideo &d(attributes[i].c_documentAttributeVideo());
			type = VideoDocument;
			duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			const MTPDdocumentAttributeAudio &d(attributes[i].c_documentAttributeAudio());
			type = AudioDocument;
			duration = d.vduration.v;
		} break;
		case mtpc_documentAttributeFilename: name = qs(attributes[i].c_documentAttributeFilename().vfile_name); break;
		}
	}
}

void DocumentData::save(const QString &toFile) {
	cancel(true);
	bool isSticker = (type == StickerDocument) && (dimensions.width() > 0) && (dimensions.height() > 0) && (size < StickerInMemory);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputDocumentFileLocation, toFile, size, isSticker);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(documentLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(documentLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

QString DocumentData::already(bool check) {
	if (!check) return location.name;
	if (!location.check()) location = Local::readFileLocation(mediaKey(mtpc_inputDocumentFileLocation, dc, id));
	return location.name;
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, int32 duration, const QString &author, int32 pendingTill) :
id(id), type(type), url(url), displayUrl(displayUrl), siteName(siteName), title(title), description(description), duration(duration), author(author), photo(photo), pendingTill(pendingTill) {
}

void PeerLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		App::main()->showPeerProfile(peer());
	}
}

void MessageLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		HistoryItem *current = App::mousedItem();
		if (current && current->history()->peer->id == peer()) {
			App::main()->pushReplyReturn(current);
		}
		App::main()->showPeer(peer(), msgid());
	}
}

MsgId clientMsgId() {
	static MsgId current = -2000000000;
	return ++current;
}
