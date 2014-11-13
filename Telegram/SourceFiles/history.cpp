/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
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

TextParseOptions _textNameOptions = {
	0, // flags
	4096, // maxw
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _textDlgOptions = {
	0, // flags
	0, // maxw is style-dependent
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

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
		ImagePtr(":/ava/art/usercolor1.png"),
		ImagePtr(":/ava/art/usercolor2.png"),
		ImagePtr(":/ava/art/usercolor3.png"),
		ImagePtr(":/ava/art/usercolor4.png"),
		ImagePtr(":/ava/art/usercolor5.png"),
		ImagePtr(":/ava/art/usercolor6.png"),
		ImagePtr(":/ava/art/usercolor7.png"),
		ImagePtr(":/ava/art/usercolor8.png")
	};
	return userDefPhotos[index];
}

ImagePtr chatDefPhoto(int32 index) {
	static const ImagePtr chatDefPhotos[4] = {
		ImagePtr(":/ava/art/chatcolor1.png"),
		ImagePtr(":/ava/art/chatcolor2.png"),
		ImagePtr(":/ava/art/chatcolor3.png"),
		ImagePtr(":/ava/art/chatcolor4.png")
	};
	return chatDefPhotos[index];
}

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

	TextParseOptions _historyTextOptions = {
		TextParseLinks | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
	TextParseOptions _historySrvOptions = {
		TextParseLinks | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // lang-dependent
	};

	inline void _initTextOptions() {
		_historySrvOptions.dir = _textNameOptions.dir = _textDlgOptions.dir = langDir();
		_textDlgOptions.maxw = st::dlgMaxWidth * 2;
	}

	class AnimatedGif : public Animated {
	public:

		AnimatedGif() : msg(0), reader(0), w(0), h(0), frame(0), framesCount(0), duration(0) {
		}

		bool animStep(float64 ms) {
			int32 f = frame;
			while (f < frames.size() && ms > delays[f]) {
				++f;
				if (f == frames.size() && frames.size() < framesCount) {
					if (reader->read(&img)) {
						int64 d = reader->nextImageDelay(), delay = delays[f - 1];
						if (!d) d = 1;
						delay += d;
						frames.push_back(QPixmap::fromImage(img.size() == QSize(w, h) ? img : img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
						delays.push_back(delay);
						for (int32 i = 0; i < frames.size(); ++i) {
							if (!frames[i].isNull()) {
								frames[i] = QPixmap();
								break;
							}
						}
					} else {
						framesCount = frames.size();
					}
				}
				if (f == frames.size()) {
					if (!duration) {
						duration = delays.isEmpty() ? 1 : delays.back();
					}

					f = 0;
					for (int32 i = 0, s = delays.size() - 1; i <= s; ++i) {
						delays[i] += duration;
					}
					if (frames[f].isNull()) {
						QString fname = reader->fileName();
						delete reader;
						reader = new QImageReader(fname);
					}
				}
				if (frames[f].isNull() && reader->read(&img)) {
					frames[f] = QPixmap::fromImage(img.size() == QSize(w, h) ? img : img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
				}
			}
			if (frame != f) {
				frame = f;
				if (App::main()) App::main()->msgUpdated(msg->history()->peer->id, msg);
			}
			return true;
		}

		void start(HistoryItem *row, const QString &file) {
			if (reader) {
				stop();
			}
			reader = new QImageReader(file);
			if (!reader->canRead() || !reader->supportsAnimation()) {
				stop();
				return;
			}

			QSize s = reader->size();
			w = s.width();
			h = s.height();
			framesCount = reader->imageCount();
			if (!w || !h || !framesCount) {
				stop();
				return;
			}

			frames.reserve(framesCount);
			delays.reserve(framesCount);
			
			int32 sizeLeft = MediaViewImageSizeLimit, delay = 0;
			for (bool read = reader->read(&img); read; read = reader->read(&img)) {
				sizeLeft -= w * h * 4;
				frames.push_back(QPixmap::fromImage(img.size() == s ? img : img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
				int32 d = reader->nextImageDelay();
				if (!d) d = 1;
				delay += d;
				delays.push_back(delay);
				if (sizeLeft < 0) break;
			}

			msg = row;

			anim::start(this);
			msg->initDimensions();
			App::main()->itemResized(msg);
		}

		void stop(bool onItemRemoved = false) {
			delete reader;
			reader = 0;
			HistoryItem *row = msg;
			msg = 0;
			frames.clear();
			delays.clear();
			w = h = frame = framesCount = duration = 0;

			anim::stop(this);
			if (row && !onItemRemoved) {
				row->initDimensions();
				if (App::main()) App::main()->itemResized(row);
			}
		}

		~AnimatedGif() {
			stop(true);
		}

		HistoryItem *msg;
		QImage img;
		QImageReader *reader;
		QVector<QPixmap> frames;
		QVector<int64> delays;
		int32 w, h, frame, framesCount, duration;
	};

	AnimatedGif animated;
}

void historyInit() {
	_initTextOptions();
}

void startGif(HistoryItem *row, const QString &file) {
	if (row == animated.msg) {
		stopGif();
	} else {
		animated.start(row, file);
	}
}

void itemReplacedGif(HistoryItem *oldItem, HistoryItem *newItem) {
	if (oldItem == animated.msg) {
		animated.msg = newItem;
	}
}

void itemRemovedGif(HistoryItem *item) {
	if (item == animated.msg) {
		animated.stop(true);
	}
}

void stopGif() {
	animated.stop();
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

void PeerData::updateName(const QString &newName, const QString &newNameOrPhone) {
	if (name == newName && nameOrPhone == newNameOrPhone) return;

	++nameVersion;
	name = newName;
	nameOrPhone = newNameOrPhone;
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
		photo = userDefPhoto(colorIndex);
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
	bool updName = !first.isEmpty() || !last.isEmpty();

	if (username != usern) {
		username = usern;
		if (App::main()) {
			App::main()->peerUsernameChanged(this);
		}
	}
	if (updName && first.trimmed().isEmpty()) {
		firstName = last;
		lastName = QString();
		updateName(firstName, phoneName);
	} else {
		if (updName) {
			firstName = first;
			lastName = last;
		}
		updateName(firstName + ' ' + lastName, phoneName);
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

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir = QDir()) {
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
			cSetDialogLastPath(dir.absolutePath());
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

void VideoSaveLink::doSave(bool forceSavingAs) const {
	VideoData *data = video();
	if (!data->user && !data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
		psOpenFile(already, true);
	} else {
		QDir alreadyDir(already.isEmpty() ? QDir() : QFileInfo(already).dir());
		QString name = already.isEmpty() ? QString(".mov") : already;
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
	doSave();
}

void VideoCancelLink::onClick(Qt::MouseButton button) const {
	VideoData *data = video();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	data->cancel();
}

void VideoData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputVideoFileLocation, toFile, size);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(videoLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*,bool)), App::main(), SLOT(videoLoadFailed(mtpFileLoader*,bool)));
	loader->start();
}

void AudioOpenLink::onClick(Qt::MouseButton button) const {
	AudioData *data = audio();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	QString already = data->already(true);
	bool play = audioVoice();
	if (!already.isEmpty() || (!data->data.isEmpty() && play)) {
		if (play) {
			AudioData *playing = 0;
			VoiceMessageState playingState = VoiceMessageStopped;
			audioVoice()->currentState(&playing, &playingState);
			if (playing == data && playingState != VoiceMessageStopped) {
				audioVoice()->pauseresume();
			} else {
				audioVoice()->play(data);
			}
		} else {
			psOpenFile(already);
		}
		return;
	}
	
	if (data->status != FileReady) return;

	QString filename = saveFileName(lang(lng_save_audio), qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), qsl(".ogg"), false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
		data->save(filename);
	}
}

void AudioSaveLink::doSave(bool forceSavingAs) const {
	AudioData *data = audio();
	if (!data->user && !data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
		psOpenFile(already, true);
	} else {
		QDir alreadyDir(already.isEmpty() ? QDir() : QFileInfo(already).dir());
		QString name = already.isEmpty() ? QString(".ogg") : already;
		QString filename = saveFileName(lang(lng_save_audio), qsl("OGG Opus Audio (*.ogg);;All files (*.*)"), qsl("audio"), name, forceSavingAs, alreadyDir);
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
	doSave();
}

void AudioCancelLink::onClick(Qt::MouseButton button) const {
	AudioData *data = audio();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	data->cancel();
}

void AudioData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputAudioFileLocation, toFile, size, (size < AudioVoiceMsgInMemory));
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(audioLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*,bool)), App::main(), SLOT(audioLoadFailed(mtpFileLoader*,bool)));
	loader->start();
}

void DocumentOpenLink::onClick(Qt::MouseButton button) const {
	DocumentData *data = document();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	QString already = data->already(true);
	if (!already.isEmpty()) {
		if (data->size < MediaViewImageSizeLimit) {
			QImageReader reader(already);
			if (reader.canRead()) {
				if (reader.supportsAnimation() && reader.imageCount() > 1 && App::hoveredLinkItem()) {
					startGif(App::hoveredLinkItem(), already);
				} else {
					App::wnd()->showDocument(data, QPixmap::fromImage(reader.read()), App::hoveredLinkItem());
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
	QMimeType mimeType = QMimeDatabase().mimeTypeForName(data->mime);
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

	QString filename = saveFileName(lang(lng_save_document), filter, qsl("doc"), name, false);
	if (!filename.isEmpty()) {
		data->openOnSave = 1;
		data->openOnSaveMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->id : 0;
		data->save(filename);
	}
}

void DocumentSaveLink::doSave(bool forceSavingAs) const {
	DocumentData *data = document();
	if (!data->user && !data->date) return;

	QString already = data->already(true);
	if (!already.isEmpty() && !forceSavingAs) {
		psOpenFile(already, true);
	} else {
		QDir alreadyDir(already.isEmpty() ? QDir() : QFileInfo(already).dir());
		QString name = already.isEmpty() ? data->name : already, filter;
		QMimeType mimeType = QMimeDatabase().mimeTypeForName(data->mime);
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

		QString filename = saveFileName(lang(lng_save_document), filter, qsl("doc"), name, forceSavingAs, alreadyDir);
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
	doSave();
}

void DocumentCancelLink::onClick(Qt::MouseButton button) const {
	DocumentData *data = document();
	if ((!data->user && !data->date) || button != Qt::LeftButton) return;

	data->cancel();
}

void DocumentData::save(const QString &toFile) {
	cancel(true);
	loader = new mtpFileLoader(dc, id, access, mtpc_inputDocumentFileLocation, toFile, size);
	loader->connect(loader, SIGNAL(progress(mtpFileLoader*)), App::main(), SLOT(documentLoadProgress(mtpFileLoader*)));
	loader->connect(loader, SIGNAL(failed(mtpFileLoader*, bool)), App::main(), SLOT(documentLoadFailed(mtpFileLoader*, bool)));
	loader->start();
}

void PeerLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton && App::main()) {
		App::main()->showPeerProfile(peer());
	}
}

MsgId clientMsgId() {
	static MsgId current = -2000000000;
	return ++current;
}

void DialogRow::paint(QPainter &p, int32 w, bool act, bool sel) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	
	p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->chat) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgLeft, rectForName.top() + st::dlgChatImgTop), App::sprite(), (act ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgChatImgSkip);
	}

	HistoryItem *last = history->last;
	if (!last) {
		p.setFont(st::dlgHistFont->f);
		p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
		if (history->typing.isEmpty()) {
			p.drawText(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgFont->ascent + st::dlgSep, lang(lng_empty_history));
		} else {
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, namewidth);
		}
	} else {
		// draw date
		QDateTime now(QDateTime::currentDateTime()), lastTime(last->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt;
		if (lastDate == nowDate) {
			dt = lastTime.toString(qsl("hh:mm"));
		} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
			dt = langDayOfWeek(lastDate);
		} else {
			dt = lastDate.toString(qsl("d.MM.yy"));
		}
		int32 dtWidth = st::dlgDateFont->m.width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
		p.setFont(st::dlgDateFont->f);
		p.setPen((act ? st::dlgActiveDateColor : st::dlgDateColor)->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

		// draw check
		if (last->out() && last->needCheck()) {
			const style::sprite *check;
			if (last->id > 0) {
				if (last->unread()) {
					check = act ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
				} else {
					check = act ? &st::dlgActiveDblCheckImg: &st::dlgDblCheckImg;
				}
			} else {
				check = act ? &st::dlgActiveSendImg : &st::dlgSendImg;
			}
			rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dlgCheckSkip);
			p.drawPixmap(QPoint(rectForName.left() + rectForName.width() + st::dlgCheckLeft, rectForName.top() + st::dlgCheckTop), App::sprite(), *check);
		}

		// draw unread
		int32 lastWidth = namewidth, unread = history->unreadCount;
		if (unread) {
			QString unreadStr = QString::number(unread);
			int32 unreadWidth = st::dlgUnreadFont->m.width(unreadStr);
			int32 unreadRectWidth = unreadWidth + 2 * st::dlgUnreadPaddingHor;
			int32 unreadRectHeight = st::dlgUnreadFont->height + 2 * st::dlgUnreadPaddingVer;
			int32 unreadRectLeft = w - st::dlgPaddingHor - unreadRectWidth;
			int32 unreadRectTop = st::dlgHeight - st::dlgPaddingVer - unreadRectHeight;
			lastWidth -= unreadRectWidth + st::dlgUnreadPaddingHor;
			p.setBrush((act ? st::dlgActiveUnreadBG : st::dlgUnreadBG)->b);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight, st::dlgUnreadRadius, st::dlgUnreadRadius);
			p.setFont(st::dlgUnreadFont->f);
			p.setPen((act ? st::dlgActiveUnreadColor : st::dlgUnreadColor)->p);
			p.drawText(unreadRectLeft + st::dlgUnreadPaddingHor, unreadRectTop + st::dlgUnreadPaddingVer + st::dlgUnreadFont->ascent, unreadStr);
		}
		if (history->typing.isEmpty()) {
			last->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), act, history->textCachedFor, history->lastItemTextCache);
		} else {
			p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth);
		}
	}

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	history->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void FakeDialogRow::paint(QPainter &p, int32 w, bool act, bool sel) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);

	History *history = _item->history();

	p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->chat) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgLeft, rectForName.top() + st::dlgChatImgTop), App::sprite(), (act ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgChatImgSkip);
	}

	// draw date
	QDateTime now(QDateTime::currentDateTime()), lastTime(_item->date);
	QDate nowDate(now.date()), lastDate(lastTime.date());
	QString dt;
	if (lastDate == nowDate) {
		dt = lastTime.toString(qsl("hh:mm"));
	} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
		dt = langDayOfWeek(lastDate);
	} else {
		dt = lastDate.toString(qsl("d.MM.yy"));
	}
	int32 dtWidth = st::dlgDateFont->m.width(dt);
	rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
	p.setFont(st::dlgDateFont->f);
	p.setPen((act ? st::dlgActiveDateColor : st::dlgDateColor)->p);
	p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

	// draw check
	if (_item->out() && _item->needCheck()) {
		const style::sprite *check;
		if (_item->id > 0) {
			if (_item->unread()) {
				check = act ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
			} else {
				check = act ? &st::dlgActiveDblCheckImg : &st::dlgDblCheckImg;
			}
		} else {
			check = act ? &st::dlgActiveSendImg : &st::dlgSendImg;
		}
		rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dlgCheckSkip);
		p.drawPixmap(QPoint(rectForName.left() + rectForName.width() + st::dlgCheckLeft, rectForName.top() + st::dlgCheckTop), App::sprite(), *check);
	}

	// draw unread
	int32 lastWidth = namewidth, unread = history->unreadCount;
	_item->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), act, _cacheFor, _cache);

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	history->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

History::History(const PeerId &peerId) : width(0), height(0)
, msgCount(0)
, unreadCount(0)
, inboxReadTill(0)
, outboxReadTill(0)
, showFrom(0)
, unreadBar(0)
, peer(App::peer(peerId))
, oldLoaded(false)
, newLoaded(true)
, last(0)
, activeMsgId(0)
, lastWidth(0)
, lastScrollTop(History::ScrollMax)
, mute(isNotifyMuted(peer->notify))
, sendRequestId(0)
, textCachedFor(0)
, lastItemTextCache(st::dlgRichMinWidth)
, posInDialogs(0)
, typingText(st::dlgRichMinWidth)
, myTyping(0)
{
	for (int32 i = 0; i < OverviewCount; ++i) {
		_overviewCount[i] = -1; // not loaded yet
	}
}

void History::updateNameText() {
	nameText.setText(st::msgNameFont, peer->nameOrPhone.isEmpty() ? peer->name : peer->nameOrPhone, _textNameOptions);
}

bool History::updateTyping(uint64 ms, uint32 dots, bool force) {
	if (!ms) ms = getms(true);
	bool changed = force;
	for (TypingUsers::iterator i = typing.begin(), e = typing.end(); i != e;) {
		if (ms >= i.value()) {
			i = typing.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		QString newTypingStr;
		int32 cnt = typing.size();
		if (cnt > 2) {
			newTypingStr = lang(lng_many_typing).replace(qsl("{n}"), QString("%1").arg(cnt));
		} else if (cnt > 1) {
			newTypingStr = lang(lng_users_typing).replace(qsl("{user1}"), typing.begin().key()->firstName).replace(qsl("{user2}"), (typing.end() - 1).key()->firstName);
		} else if (cnt) {
			newTypingStr = peer->chat ? lang(lng_user_typing).replace(qsl("{user}"), typing.begin().key()->firstName) : lang(lng_typing);
		}
		if (!newTypingStr.isEmpty()) {
			newTypingStr += qsl("...");
		}
		if (typingStr != newTypingStr) {
			typingText.setText(st::dlgHistFont, (typingStr = newTypingStr), _textNameOptions);
		}
	}
	if (!typingStr.isEmpty()) {
		if (typingText.lastDots(dots % 4)) {
			changed = true;
		}
	}
	return changed;
}

bool DialogsList::del(const PeerId &peerId, DialogRow *replacedBy) {
	RowByPeer::iterator i = rowByPeer.find(peerId);
	if (i == rowByPeer.cend()) return false;

	DialogRow *row = i.value();
	emit App::main()->dialogRowReplaced(row, replacedBy);

	if (row == current) {
		current = row->next;
	}
	for (DialogRow *change = row->next; change != end; change = change->next) {
		change->pos--;
	}
	end->pos--;
	remove(row);
	delete row;
	--count;
	rowByPeer.erase(i);

	return true;
}

void DialogsIndexed::peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (byName) {
		DialogRow *mainRow = list.adjustByName(peer);
		if (!mainRow) return;

		History *history = mainRow->history;

		PeerData::NameFirstChars toRemove = oldChars, toAdd;
		for (PeerData::NameFirstChars::const_iterator i = peer->chars.cbegin(), e = peer->chars.cend(); i != e; ++i) {
			PeerData::NameFirstChars::iterator j = toRemove.find(*i);
			if (j == toRemove.cend()) {
				toAdd.insert(*i);
			} else {
				toRemove.erase(j);
				DialogsIndex::iterator k = index.find(*i);
				if (k != index.cend()) {
					k.value()->adjustByName(peer);
				}
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toRemove.cbegin(), e = toRemove.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j != index.cend()) {
				j.value()->del(peer->id, mainRow);
			}
		}
		if (!toAdd.isEmpty()) {
			for (PeerData::NameFirstChars::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
				DialogsIndex::iterator j = index.find(*i);
				if (j == index.cend()) {
					j = index.insert(*i, new DialogsList(byName));
				}
				j.value()->addByName(history);
			}
		}
	} else {
		DialogsList::RowByPeer::const_iterator i = list.rowByPeer.find(peer->id);
		if (i == list.rowByPeer.cend()) return;

		DialogRow *mainRow = i.value();
		History *history = mainRow->history;

		PeerData::NameFirstChars toRemove = oldChars, toAdd;
		for (PeerData::NameFirstChars::const_iterator i = peer->chars.cbegin(), e = peer->chars.cend(); i != e; ++i) {
			PeerData::NameFirstChars::iterator j = toRemove.find(*i);
			if (j == toRemove.cend()) {
				toAdd.insert(*i);
			} else {
				toRemove.erase(j);
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toRemove.cbegin(), e = toRemove.cend(); i != e; ++i) {
			history->dialogs.remove(*i);
			DialogsIndex::iterator j = index.find(*i);
			if (j != index.cend()) {
				j.value()->del(peer->id, mainRow);
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j == index.cend()) {
				j = index.insert(*i, new DialogsList(byName));
			}
			history->dialogs.insert(*i, j.value()->addByPos(history));
		}
	}
}

void DialogsIndexed::clear() {
	for (DialogsIndex::iterator i = index.begin(), e = index.end(); i != e; ++i) {
		delete i.value();
	}
	index.clear();
	list.clear();
}

void Histories::clear() {
	App::historyClearMsgs();
	for (Parent::const_iterator i = cbegin(), e = cend(); i != e; ++i) {
		delete i.value();
	}
	App::historyClearItems();
	typing.clear();
	Parent::clear();
}

Histories::Parent::iterator Histories::erase(Histories::Parent::iterator i) {
	delete i.value();
	return Parent::erase(i);
}

HistoryItem *Histories::addToBack(const MTPmessage &msg, int msgState) {
	PeerId from_id = 0, to_id = 0;
	switch (msg.type()) {
	case mtpc_message:
		from_id = App::peerFromUser(msg.c_message().vfrom_id);
		to_id = App::peerFromMTP(msg.c_message().vto_id);
	break;
	case mtpc_messageForwarded:
		from_id = App::peerFromUser(msg.c_messageForwarded().vfrom_id);
		to_id = App::peerFromMTP(msg.c_messageForwarded().vto_id);
	break;
	case mtpc_messageService:
		from_id = App::peerFromUser(msg.c_messageService().vfrom_id);
		to_id = App::peerFromMTP(msg.c_messageService().vto_id);
	break;
	}
	PeerId peer = (to_id == App::peerFromUser(MTP::authedId())) ? from_id : to_id;

	if (!peer) return 0;

	iterator h = find(peer);
	if (h == end()) {
		h = insert(peer, new History(peer));
	}
	if (msgState < 0) {
		return h.value()->addToHistory(msg);
	}
	if (!h.value()->loadedAtBottom()) {
		HistoryItem *item = h.value()->addToHistory(msg);
		if (item) {
			h.value()->last = item;
			if (msgState > 0) {
				h.value()->newItemAdded(item);
			}
		}
		return item;
	}
	return h.value()->addToBack(msg, msgState > 0);
}

/*
HistoryItem *Histories::addToBack(const MTPgeoChatMessage &msg, bool newMsg) {
	PeerId peer = 0;
	switch (msg.type()) {
	case mtpc_geoChatMessage:
		peer = App::peerFromChat(msg.c_geoChatMessage().vchat_id);
	break;
	case mtpc_geoChatMessageService:
		peer = App::peerFromChat(msg.c_geoChatMessageService().vchat_id);
	break;
	}
	if (!peer) return 0;

	iterator h = find(peer);
	if (h == end()) {
		h = insert(peer, new History(peer));
	}
	return h.value()->addToBack(msg, newMsg);
}/**/

HistoryItem *History::createItem(HistoryBlock *block, const MTPmessage &msg, bool newMsg, bool returnExisting) {
	HistoryItem *result = 0;

	switch (msg.type()) {
	case mtpc_messageEmpty:
		result = new HistoryServiceMsg(this, block, msg.c_messageEmpty().vid.v, date(), lang(lng_message_empty));
	break;

	case mtpc_message:
		result = new HistoryMessage(this, block, msg.c_message());
	break;

	case mtpc_messageForwarded:
		result = new HistoryForwarded(this, block, msg.c_messageForwarded());
	break;

	case mtpc_messageService: {
		const MTPDmessageService &d(msg.c_messageService());
		result = new HistoryServiceMsg(this, block, d);

		if (newMsg) {
			const MTPmessageAction &action(d.vaction);
			switch (d.vaction.type()) {
			case mtpc_messageActionChatAddUser: {
				const MTPDmessageActionChatAddUser &d(action.c_messageActionChatAddUser());
				// App::user(App::peerFromUser(d.vuser_id)); added
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				ChatData *chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				const MTPDmessageActionChatDeleteUser &d(action.c_messageActionChatDeleteUser());
				// App::peer(App::peerFromUser(d.vuser_id)); left
			} break;

			case mtpc_messageActionChatEditPhoto: {
				const MTPDmessageActionChatEditPhoto &d(action.c_messageActionChatEditPhoto());
				if (d.vphoto.type() == mtpc_photo) {
					const QVector<MTPPhotoSize> &sizes(d.vphoto.c_photo().vsizes.c_vector().v);
					if (!sizes.isEmpty()) {
						ChatData *chat = peer->asChat();
						if (chat) {
							PhotoData *photo = App::feedPhoto(d.vphoto.c_photo());
							if (photo) photo->chat = chat;
							const MTPPhotoSize &smallSize(sizes.front()), &bigSize(sizes.back());
							const MTPFileLocation *smallLoc = 0, *bigLoc = 0;
							switch (smallSize.type()) {
							case mtpc_photoSize: smallLoc = &smallSize.c_photoSize().vlocation; break;
							case mtpc_photoCachedSize: smallLoc = &smallSize.c_photoCachedSize().vlocation; break;
							}
							switch (bigSize.type()) {
							case mtpc_photoSize: bigLoc = &bigSize.c_photoSize().vlocation; break;
							case mtpc_photoCachedSize: bigLoc = &bigSize.c_photoCachedSize().vlocation; break;
							}
							if (smallLoc && bigLoc) {
								chat->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
								chat->photo->load();
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditTitle: {
				const MTPDmessageActionChatEditTitle &d(action.c_messageActionChatEditTitle());
				ChatData *chat = peer->asChat();
				if (chat) chat->updateName(qs(d.vtitle), QString());
			} break;
			}
		}
	} break;
	}

	return regItem(result, returnExisting);
}

HistoryItem *History::createItemForwarded(HistoryBlock *block, MsgId id, HistoryMessage *msg) {
	HistoryItem *result = 0;

	result = new HistoryForwarded(this, block, id, msg);

	return regItem(result);
}

/*
HistoryItem *History::createItem(HistoryBlock *block, const MTPgeoChatMessage &msg, bool newMsg) {
	HistoryItem *result = 0;

	switch (msg.type()) {
	case mtpc_geoChatMessageEmpty:
		result = new HistoryServiceMsg(this, block, msg.c_geoChatMessageEmpty().vid.v, date(), lang(lng_message_empty));
	break;

	case mtpc_geoChatMessage:
		result = new HistoryMessage(this, block, msg.c_geoChatMessage());
	break;

	case mtpc_geoChatMessageService:
		result = new HistoryServiceMsg(this, block, msg.c_geoChatMessageService());
	break;
	}

	return regItem(result);
}
/**/
HistoryItem *History::addToBackService(MsgId msgId, QDateTime date, const QString &text, bool out, bool unread, HistoryMedia *media, bool newMsg) {
	HistoryBlock *to = 0;
	bool newBlock = isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = back();
	}

	return doAddToBack(to, newBlock, regItem(new HistoryServiceMsg(this, to, msgId, date, text, out, unread, media)), newMsg);
}

HistoryItem *History::addToBack(const MTPmessage &msg, bool newMsg) {
	HistoryBlock *to = 0;
	bool newBlock = isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = back();
	}
	return doAddToBack(to, newBlock, createItem(to, msg, newMsg), newMsg);
}

HistoryItem *History::addToHistory(const MTPmessage &msg) {
	return createItem(0, msg, false, true);
}

HistoryItem *History::addToBackForwarded(MsgId id, HistoryMessage *item) {
	HistoryBlock *to = 0;
	bool newBlock = isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = back();
	}
	return doAddToBack(to, newBlock, createItemForwarded(to, id, item), true);
}

/*
HistoryItem *History::addToBack(const MTPgeoChatMessage &msg, bool newMsg) {
	HistoryBlock *to = 0;
	bool newBlock = isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = back();
	}

	return doAddToBack(to, newBlock, createItem(to, msg, newMsg), newMsg);
}
/**/

void History::createInitialDateBlock(const QDateTime &date) {
	HistoryBlock *dateBlock = new HistoryBlock(this); // date block
	HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, date);
	dateBlock->push_back(dayItem);
	if (width) {
		int32 dh = dayItem->resize(width);
		dateBlock->height = dh;
		height += dh;
		for (int32 i = 0, l = size(); i < l; ++i) {
			(*this)[i]->y += dh;
		}
	}
	push_front(dateBlock); // date block
}

HistoryItem *History::doAddToBack(HistoryBlock *to, bool newBlock, HistoryItem *adding, bool newMsg) {
	if (!adding) {
		if (newBlock) delete to;
		return adding;
	}

	if (newBlock) {
		createInitialDateBlock(adding->date);

		to->y = height;
		push_back(to);
	} else if (to->back()->date.date() != adding->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, to, adding->date);
		to->push_back(dayItem);
		dayItem->y = to->height;
		if (width) {
			int32 dh = dayItem->resize(width);
			to->height += dh;
			height += dh;
		}
	}
	to->push_back(adding);
	last = adding;
	adding->y = to->height;
	if (width) {
		int32 dh = adding->resize(width);
		to->height += dh;
		height += dh;
	}
	setMsgCount(msgCount + 1);
	if (newMsg) {
		newItemAdded(adding);
	}
	HistoryMedia *media = adding->getMedia(true);
	if (media) {
		MediaOverviewType t = mediaToOverviewType(media->type());
		if (t != OverviewCount) {
			if (_overviewIds[t].constFind(adding->id) == _overviewIds[t].cend()) {
				_overview[t].push_back(adding->id);
				_overviewIds[t].insert(adding->id, NullType());
				if (_overviewCount[t] > 0) ++_overviewCount[t];
				if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer);
			}
		}
	}
	return adding;
}

void History::unregTyping(UserData *from) {
	TypingUsers::iterator i = typing.find(from);
	if (i != typing.end()) {
		uint64 ms = getms(true);
		i.value() = ms;
		updateTyping(ms, 0, true);
		App::main()->topBar()->update();
	}
}

void History::newItemAdded(HistoryItem *item) {
	App::checkImageCacheSize();
	if (item->from()) {
		unregTyping(item->from());
	}
	if (item->out()) {
		if (unreadBar) unreadBar->destroy();
	} else if (item->unread()) {
		notifies.push_back(item);
		App::main()->newUnreadMsg(this, item->id);
	}
	if (dialogs.isEmpty()) {
		App::main()->createDialogAtTop(this, unreadCount);
	} else {
		emit App::main()->dialogToTop(dialogs);
	}
}

void History::addToFront(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		oldLoaded = true;
		return;
	}

	int32 addToH = 0, skip = 0;
	if (!isEmpty()) {
		addToH = -front()->height;
		pop_front(); // remove date block
	}
	HistoryItem *till = isEmpty() ? 0 : front()->front(), *prev = 0;

	HistoryBlock *block = new HistoryBlock(this);
	block->reserve(slice.size());
	int32 wasMsgCount = msgCount;
	for (QVector<MTPmessage>::const_iterator i = slice.cend() - 1, e = slice.cbegin(); ; --i) {
		HistoryItem *adding = createItem(block, *i, false);
		if (adding) {
			if (prev && prev->date.date() != adding->date.date()) {
				HistoryItem *dayItem = createDayServiceMsg(this, block, adding->date);
				block->push_back(dayItem);
				dayItem->y = block->height;
				block->height += dayItem->resize(width);
			}
			block->push_back(adding);
			adding->y = block->height;
			block->height += adding->resize(width);
			setMsgCount(msgCount + 1);
			prev = adding;
		}
		if (i == e) break;
	}
	if (till && prev && prev->date.date() != till->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, block, till->date);
		block->push_back(dayItem);
		dayItem->y = block->height;
		block->height += dayItem->resize(width);
	}
	if (block->size()) {
		if (wasMsgCount < unreadCount && msgCount >= unreadCount && !activeMsgId) {
			for (int32 i = block->size(); i > 0; --i) {
				if ((*block)[i - 1]->itemType() == HistoryItem::MsgType) {
					++wasMsgCount;
					if (wasMsgCount == unreadCount) {
						showFrom = (*block)[i - 1];
						break;
					}
				}
			}
		}
		push_front(block);
		addToH += block->height;
		++skip;

		if (loadedAtBottom()) { // add photos to overview
			for (int32 i = block->size(); i > 0; --i) {
				HistoryItem *item = (*block)[i - 1];
				HistoryMedia *media = item->getMedia(true);
				if (media) {
					MediaOverviewType t = mediaToOverviewType(media->type());
					if (t != OverviewCount) {
						if (_overviewIds[t].constFind(item->id) == _overviewIds[t].cend()) {
							_overview[t].push_front(item->id);
							_overviewIds[t].insert(item->id, NullType());
						}
					}
				}
			}
			if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer);
		}
	} else {
		delete block;
	}
	if (!isEmpty()) {
		HistoryBlock *dateBlock = new HistoryBlock(this);
		HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, front()->front()->date);
		dateBlock->push_back(dayItem);
		int32 dh = dayItem->resize(width);
		dateBlock->height = dh;
		if (skip) {
			front()->y += dh;
		}
		push_front(dateBlock); // date block
		addToH += dh;
		++skip;
	}
	if (addToH) {
		for (iterator i = begin(), e = end(); i != e; ++i) {
			if (skip) {
				--skip;
			} else {
				(*i)->y += addToH;
			}
		}
		height += addToH;
	}
}

void History::addToBack(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		newLoaded = true;
		return;
	}

	bool wasEmpty = isEmpty();

	HistoryItem *prev = isEmpty() ? 0 : back()->back();

	HistoryBlock *block = new HistoryBlock(this);
	block->reserve(slice.size());
	int32 wasMsgCount = msgCount;
	for (QVector<MTPmessage>::const_iterator i = slice.cend(), e = slice.cbegin(); i != e;) {
		--i;
		HistoryItem *adding = createItem(block, *i, false);
		if (adding) {
			if (prev && prev->date.date() != adding->date.date()) {
				HistoryItem *dayItem = createDayServiceMsg(this, block, adding->date);
				prev->block()->push_back(dayItem);
				dayItem->y = prev->block()->height;
				prev->block()->height += dayItem->resize(width);
				if (prev->block() != block) {
					height += dayItem->height();
				}
			}
			block->push_back(adding);
			adding->y = block->height;
			block->height += adding->resize(width);
			setMsgCount(msgCount + 1);
			prev = adding;
		}
		if (i == e) break;
	}
	bool wasLoadedAtBottom = loadedAtBottom();
	if (block->size()) {
		block->y = height;
		push_back(block);
		height += block->height;
	} else {
		newLoaded = true;
		fixLastMessage(true);
		delete block;
	}
	if (!wasLoadedAtBottom && loadedAtBottom()) { // add all loaded photos to overview
		for (int32 i = 0; i < OverviewCount; ++i) {
			if (_overviewCount[i] == 0) continue; // all loaded
			_overview[i].clear();
			_overviewIds[i].clear();
		}
		for (int32 i = 0; i < size(); ++i) {
			HistoryBlock *b = (*this)[i];
			for (int32 j = 0; j < b->size(); ++j) {
				HistoryItem *item = (*b)[j];
				HistoryMedia *media = item->getMedia(true);
				if (media) {
					MediaOverviewType t = mediaToOverviewType(media->type());
					if (t != OverviewCount && _overviewCount[t] != 0) {
						_overview[t].push_back(item->id);
						_overviewIds[t].insert(item->id, NullType());
					}
				}
			}
		}
		if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer);
	}
	if (wasEmpty && !isEmpty()) {
		HistoryBlock *dateBlock = new HistoryBlock(this);
		HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, front()->front()->date);
		dateBlock->push_back(dayItem);
		int32 dh = dayItem->resize(width);
		dateBlock->height = dh;
		for (iterator i = begin(), e = end(); i != e; ++i) {
			(*i)->y += dh;
		}
		push_front(dateBlock); // date block
		height += dh;
	}
}

void History::inboxRead(HistoryItem *wasRead) {
	if (unreadCount) {
		if (wasRead && loadedAtBottom()) App::main()->historyToDown(this);
		setUnreadCount(0);
	}
	if (!isEmpty()) {
		int32 till = (wasRead ? wasRead : back()->back())->id;
		if (inboxReadTill < till) inboxReadTill = till;
	}
	if (!dialogs.isEmpty()) {
		if (App::main()) App::main()->dlgUpdated(dialogs[0]);
	}
	App::wnd()->notifyClear(this);
	clearNotifications();
}

void History::outboxRead(HistoryItem *wasRead) {
	if (!isEmpty()) {
		int32 till = wasRead->id;
		if (outboxReadTill < till) outboxReadTill = till;
	}
}

void History::setUnreadCount(int32 newUnreadCount, bool psUpdate) {
	if (unreadCount != newUnreadCount) {
		if (!unreadCount && newUnreadCount == 1 && loadedAtBottom()) {
			showFrom = isEmpty() ? 0 : back()->back();
		} else if (!newUnreadCount) {
			showFrom = 0;
		}
		App::histories().unreadFull += newUnreadCount - unreadCount;
		if (mute) App::histories().unreadMuted += newUnreadCount - unreadCount;
		unreadCount = newUnreadCount;
		if (psUpdate) App::wnd()->psUpdateCounter();
		if (unreadBar) unreadBar->setCount(unreadCount);
	}
}

void History::setMsgCount(int32 newMsgCount) {
	if (msgCount != newMsgCount) {
		msgCount = newMsgCount;
	}
}

 void History::setMute(bool newMute) {
	if (mute != newMute) {
		App::histories().unreadMuted += newMute ? unreadCount : (-unreadCount);
		mute = newMute;
		App::wnd()->psUpdateCounter();
	}
}

void History::getNextShowFrom(HistoryBlock *block, int32 i) {
	if (!loadedAtBottom()) {
		showFrom = 0;
		return;
	}
	if (i >= 0) {
		int32 l = block->size();
		for (++i; i < l; ++i) {
			if ((*block)[i]->itemType() == HistoryItem::MsgType) {
				showFrom = (*block)[i];
				return;
			}
		}
	}

	int32 j = indexOf(block), s = size();
	if (j >= 0) {
		for (++j; j < s; ++j) {
			block = (*this)[j];
			for (int32 i = 0, l = block->size(); i < l; ++i) {
				if ((*block)[i]->itemType() == HistoryItem::MsgType) {
					showFrom = (*block)[i];
					return;
				}
			}
		}
	}
	showFrom = 0;
}

void History::addUnreadBar() {
	if (unreadBar || !showFrom || !unreadCount || !loadedAtBottom()) return;

	HistoryBlock *block = showFrom->block();
	int32 i = block->indexOf(showFrom);
	int32 j = indexOf(block);
	if (i < 0 || j < 0) return;

	HistoryUnreadBar *bar = new HistoryUnreadBar(this, block, unreadCount, showFrom->date);
	block->insert(i, bar);
	unreadBar = bar;

	unreadBar->y = showFrom->y;

	int32 dh = unreadBar->resize(width), l = block->size();
	for (++i; i < l; ++i) {
		(*block)[i]->y += dh;
	}
	block->height += dh;
	for (++j, l = size(); j < l; ++j) {
		(*this)[j]->y += dh;
	}
	height += dh;
}

void History::clearNotifications() {
	notifies.clear();
}

bool History::readyForWork() const {
	return activeMsgId ? !isEmpty() : (unreadCount <= msgCount);
}

bool History::loadedAtBottom() const {
	return newLoaded;
}

bool History::loadedAtTop() const {
	return oldLoaded;
}

void History::fixLastMessage(bool wasAtBottom) {
	if (wasAtBottom && isEmpty()) {
		wasAtBottom = false;
	}
	if (wasAtBottom) {
		last = back()->back();
	} else {
		last = 0;
		if (App::main()) {
			App::main()->checkPeerHistory(peer);
		}
	}
}

void History::loadAround(MsgId msgId) {
	if (activeMsgId != msgId) {
		activeMsgId = msgId;
		lastWidth = 0;
		if (activeMsgId) {
			HistoryItem *item = App::histItemById(activeMsgId);
			if (!item || !item->block()) {
				clear(true);
			}
			newLoaded = last && !last->detached();
		} else {
			if (!loadedAtBottom()) {
				clear(true);
			}
			newLoaded = isEmpty() || (last && !last->detached());
		}
	}
}

bool History::canShowAround(MsgId msgId) const {
	if (activeMsgId != msgId) {
		if (msgId) {
			HistoryItem *item = App::histItemById(msgId);
			return item && item->block();
		} else {
			return loadedAtBottom();
		}
	}
	return true;
}

MsgId History::minMsgId() const {
	for (const_iterator i = cbegin(), e = cend(); i != e; ++i) {
		for (HistoryBlock::const_iterator j = (*i)->cbegin(), en = (*i)->cend(); j != en; ++j) {
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (const_iterator i = cend(), e = cbegin(); i != e;) {
		--i;
		for (HistoryBlock::const_iterator j = (*i)->cend(), en = (*i)->cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

int32 History::geomResize(int32 newWidth, int32 *ytransform, bool dontRecountText) {
	if (width != newWidth || dontRecountText) {
		int32 y = 0;
		for (iterator i = begin(), e = end(); i != e; ++i) {
			HistoryBlock *block = *i;
			bool updTransform = ytransform && (*ytransform >= block->y) && (*ytransform < block->y + block->height);
			if (updTransform) *ytransform -= block->y;
			if (block->y != y) {
				block->y = y;
			}
			y += block->geomResize(newWidth, ytransform, dontRecountText);
			if (updTransform) {
				*ytransform += block->y;
				ytransform = 0;
			}
		}
		width = newWidth;
		height = y;
	}
	return height;
}

void History::clear(bool leaveItems) {
	if (unreadBar) {
		unreadBar->destroy();
	}
	if (showFrom) {
		showFrom = 0;
	}
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (_overviewCount[i] == 0) _overviewCount[i] = _overview[i].size();
		_overview[i].clear();
		_overviewIds[i].clear();
	}
	if (App::wnd() && !App::quiting()) App::wnd()->mediaOverviewUpdated(peer);
	for (Parent::const_iterator i = cbegin(), e = cend(); i != e; ++i) {
		if (leaveItems) {
			(*i)->clear(true);
		}
		delete *i;
	}
	Parent::clear();
	setMsgCount(0);
	if (!leaveItems) {
		setUnreadCount(0);
		last = 0;
	}
	height = 0;
	oldLoaded = false;
}

History::Parent::iterator History::erase(History::Parent::iterator i) {
	delete *i;
	return Parent::erase(i);
}

void History::blockResized(HistoryBlock *block, int32 dh) {
	int32 i = indexOf(block), l = size();
	if (i >= 0) {
		for (++i; i < l; ++i) {
			(*this)[i]->y -= dh;
		}
		height -= dh;
	}
}

void History::removeBlock(HistoryBlock *block) {
	int32 i = indexOf(block), h = block->height;
	if (i >= 0) {
		removeAt(i);
		int32 l = size();
		if (i > 0 && l == 1) { // only fake block with date left
			removeBlock((*this)[0]);
			height = 0;
		} else if (h) {
			for (; i < l; ++i) {
				(*this)[i]->y -= h;
			}
			height -= h;
		}
	}
	delete block;
}

int32 HistoryBlock::geomResize(int32 newWidth, int32 *ytransform, bool dontRecountText) {
	int32 y = 0;
	for (iterator i = begin(), e = end(); i != e; ++i) {
		HistoryItem *item = *i;
		bool updTransform = ytransform && (*ytransform >= item->y) && (*ytransform < item->y + item->height());
		if (updTransform) *ytransform -= item->y;
		item->y = y;
		y += item->resize(newWidth, dontRecountText);
		if (updTransform) {
			*ytransform += item->y;
			ytransform = 0;
		}
	}
	height = y;
	return height;
}

void HistoryBlock::clear(bool leaveItems) {
	if (leaveItems) {
		for (Parent::const_iterator i = cbegin(), e = cend(); i != e; ++i) {
			(*i)->detachFast();
		}
	} else {
		for (Parent::const_iterator i = cbegin(), e = cend(); i != e; ++i) {
			delete *i;
		}
	}
	Parent::clear();
}

HistoryBlock::Parent::iterator HistoryBlock::erase(HistoryBlock::Parent::iterator i) {
	delete *i;
	return Parent::erase(i);
}

void HistoryBlock::removeItem(HistoryItem *item) {
	int32 i = indexOf(item), dh = 0;
	if (history->showFrom == item) {
		history->getNextShowFrom(this, i);
	}
	if (i < 0) {
		return;
	}

	bool createInitialDate = false;
	QDateTime initialDateTime;
	int32 myIndex = history->indexOf(this);
	if (myIndex >= 0 && item->itemType() != HistoryItem::DateType) { // fix date items
		HistoryItem *nextItem = (i < size() - 1) ? (*this)[i + 1] : ((myIndex < history->size() - 1) ? (*(*history)[myIndex + 1])[0] : 0);
		if (nextItem && nextItem == history->unreadBar) { // skip unread bar
			if (i < size() - 2) {
				nextItem = (*this)[i + 2];
			} else if (i < size() - 1) {
				nextItem = ((myIndex < history->size() - 1) ? (*(*history)[myIndex + 1])[0] : 0);
			} else if (myIndex < history->size() - 1) {
				if (0 < (*history)[myIndex + 1]->size() - 1) {
					nextItem = (*(*history)[myIndex + 1])[1];
				} else if (myIndex < history->size() - 2) {
					nextItem = (*(*history)[myIndex + 2])[0];
				} else {
					nextItem = 0;
				}
			} else {
				nextItem = 0;
			}
		}
		if (!nextItem || nextItem->itemType() == HistoryItem::DateType) { // only if there is no next item or it is a date item
			HistoryItem *prevItem = (i > 0) ? (*this)[i - 1] : 0;
			if (prevItem && prevItem == history->unreadBar) { // skip unread bar
				prevItem = (i > 1) ? (*this)[i - 2] : 0;
			}
			if (prevItem) {
				if (prevItem->itemType() == HistoryItem::DateType) {
					prevItem->destroy();
					--i;
				}
			} else if (myIndex > 0) {
				HistoryBlock *prevBlock = (*history)[myIndex - 1];
				if (prevBlock->isEmpty() || ((myIndex == 1) && (prevBlock->size() != 1 || (*prevBlock->cbegin())->itemType() != HistoryItem::DateType))) {
					LOG(("App Error: Found bad history, with no first date block: %1").arg((*history)[0]->size()));
				} else if ((*prevBlock)[prevBlock->size() - 1]->itemType() == HistoryItem::DateType) {
					(*prevBlock)[prevBlock->size() - 1]->destroy();
					if (nextItem && myIndex == 1) { // destroy next date (for creating initial then)
						initialDateTime = nextItem->date;
						createInitialDate = true;
						nextItem->destroy();
					}
				}
			}
		}
	}
	// myIndex can be invalid now, because of destroying previous blocks

	dh = item->height();
	remove(i);
	int32 l = size();
	if (!item->out() && item->unread() && history->unreadCount) {
		history->setUnreadCount(history->unreadCount - 1);
	}
	int32 itemType = item->itemType();
	if (itemType == HistoryItem::MsgType) {
		history->setMsgCount(history->msgCount - 1);
	} else if (itemType == HistoryItem::UnreadBarType) {
		if (history->unreadBar == item) {
			history->unreadBar = 0;
		}
	}
	if (createInitialDate) {
		history->createInitialDateBlock(initialDateTime);
	}
	History *h = history;
	if (l) {
		for (; i < l; ++i) {
			(*this)[i]->y -= dh;
		}
		height -= dh;
		history->blockResized(this, dh);
	} else {
		history->removeBlock(this);
	}
}

bool ItemAnimations::animStep(float64 ms) {
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		const HistoryItem *item = i.key();
		if (item->animating()) {
			App::main()->msgUpdated(item->history()->peer->id, item);
			++i;
		} else {
			i = _animations.erase(i);
		}
	}
	return !_animations.isEmpty();
}

uint64 ItemAnimations::animate(const HistoryItem *item, uint64 ms) {
	if (_animations.isEmpty()) {
		_animations.insert(item, ms);
		anim::start(this);
		return 0;
	}
	Animations::const_iterator i = _animations.constFind(item);
	if (i == _animations.cend()) i = _animations.insert(item, ms);
	return ms - i.value();
}

void ItemAnimations::remove(const HistoryItem *item) {
	_animations.remove(item);
}

namespace {
	ItemAnimations _itemAnimations;
}

ItemAnimations &itemAnimations() {
	return _itemAnimations;
}

HistoryItem::HistoryItem(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime msgDate, int32 from) : y(0)
, id(msgId)
, date(msgDate)
, _from(App::user(from))
, _fromVersion(_from->nameVersion)
, _history(history)
, _block(block)
, _out(out)
, _unread(unread)
{
}

void HistoryItem::markRead() {
	if (_unread) {
		if (_out) {
			_history->outboxRead(this);
		} else {
			_history->inboxRead(this);
		}
		App::main()->msgUpdated(_history->peer->id, this);
		_unread = false;
	}
}

void HistoryItem::destroy() {
	if (!out()) markRead();
	bool wasAtBottom = history()->loadedAtBottom();
	_history->removeNotification(this);
	detach();
	if (history()->last == this) {
		history()->fixLastMessage(wasAtBottom);
	}
	HistoryMedia *m = getMedia(true);
	MediaOverviewType t = m ? mediaToOverviewType(m->type()) : OverviewCount;
	if (t != OverviewCount && !history()->_overviewIds[t].isEmpty()) {
		History::MediaOverviewIds::iterator i = history()->_overviewIds[t].find(id);
		if (i != history()->_overviewIds[t].cend()) {
			history()->_overviewIds[t].erase(i);
			for (History::MediaOverview::iterator i = history()->_overview[t].begin(), e = history()->_overview[t].end(); i != e; ++i) {
				if ((*i) == id) {
					history()->_overview[t].erase(i);
					if (history()->_overviewCount[t] > 0) {
						--history()->_overviewCount[t];
						if (!history()->_overviewCount[t]) {
							history()->_overviewCount[t] = -1;
						}
					}
					break;
				}
			}
			if (App::wnd()) App::wnd()->mediaOverviewUpdated(history()->peer);
		}
	}
	delete this;
}

void HistoryItem::detach() {
	if (_history && _history->unreadBar == this) {
		_history->unreadBar = 0;
	}
	if (_block) {
		_block->removeItem(this);
		detachFast();
		App::historyItemDetached(this);
	} else {
		if (_history->showFrom == this) {
			_history->showFrom = 0;
		}
	}
	if (_history && _history->unreadBar && _history->back()->back() == _history->unreadBar) {
		_history->unreadBar->destroy();
	}
}

void HistoryItem::detachFast() {
	_block = 0;
}

HistoryItem::~HistoryItem() {
	itemAnimations().remove(this);
	App::historyUnregItem(this);
	if (id < 0) {
		App::app()->uploader()->cancel(id);
	}
}

HistoryItem *regItem(HistoryItem *item, bool returnExisting) {
	if (!item) return 0;
	HistoryItem *existing = App::historyRegItem(item);
	if (existing) {
		delete item;
		return returnExisting ? existing : 0;
	}
	return item;
}

HistoryPhoto::HistoryPhoto(const MTPDphoto &photo, int32 width) : data(App::feedPhoto(photo))
, openl(new PhotoLink(data))
, w(width) {
	init();
}

HistoryPhoto::HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width) : data(App::feedPhoto(photo))
, openl(new PhotoLink(data, chat))
, w(width) {
	init();
}

void HistoryPhoto::init() {
	data->thumb->load();
}

void HistoryPhoto::initDimensions(const HistoryItem *parent) {
	int32 tw = convertScale(data->full->width()), th = convertScale(data->full->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	int32 thumbw = qMax(tw, int32(st::minPhotoWidth)), maxthumbh = thumbw;
	int32 thumbh = qRound(th * float64(thumbw) / tw);
	if (thumbh > maxthumbh) {
		thumbw = qRound(thumbw * float64(maxthumbh) / thumbh);
		thumbh = maxthumbh;
		if (thumbw < st::minPhotoWidth) {
			thumbw = st::minPhotoWidth;
		}
	}
	if (thumbh < st::minPhotoHeight) {
		thumbh = st::minPhotoHeight;
	}
	if (!w) {
		w = thumbw;
	}
	_maxw = w;
	_height = _minh = thumbh;
}

int32 HistoryPhoto::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;

	int32 tw = convertScale(data->full->width()), th = convertScale(data->full->height());
	_height = th;
	if (tw > w) {
		_height = (w * _height / tw);
	} else {
		w = tw;
	}
	if (_height > width) {
		w = (w * width) / _height;
		_height = width;
	}
	if (w < st::minPhotoWidth) {
		w = st::minPhotoWidth;
	}
	if (_height < st::minPhotoHeight) {
		_height = st::minPhotoHeight;
	}
	return _height;
}

const QString HistoryPhoto::inDialogsText() const {
	return lang(lng_in_dlg_photo);
}

bool HistoryPhoto::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

TextLinkPtr HistoryPhoto::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (x >= 0 && y >= 0 && x < width && y < _height) {
		return openl;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryPhoto::clone() const {
	return new HistoryPhoto(*this);
}

void HistoryPhoto::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	data->full->load(false, false);
	bool out = parent->out();
	bool full = data->full->loaded();
	QPixmap pix;
	if (full) {
		pix = data->full->pixSingle(width);
	} else {
		pix = data->thumb->pixBlurredSingle(width);
	}
	if (pix.height() >= _height * cIntRetinaFactor()) {
		p.drawPixmap(QPoint(0, 0), pix, QRect(0, (pix.height() - _height * cIntRetinaFactor()) / 2, width * cIntRetinaFactor(), _height * cIntRetinaFactor()));
	} else {
		int32 usewidth = (width * pix.height()) / (_height * cIntRetinaFactor());
		p.drawPixmap(QRect(0, 0, width, _height), pix, QRect((width - usewidth) * cIntRetinaFactor() / 2, 0, usewidth * cIntRetinaFactor(), pix.height()));
	}
	if (!full) {
		uint64 dt = itemAnimations().animate(parent, getms());
		int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

		int32 x = (width - st::photoLoader.width()) / 2, y = (_height - st::photoLoader.height()) / 2;
		p.fillRect(x, y, st::photoLoader.width(), st::photoLoader.height(), st::photoLoaderBg->b);
		x += (st::photoLoader.width() - cnt * st::photoLoaderPoint.width() - (cnt - 1) * st::photoLoaderSkip) / 2;
		y += (st::photoLoader.height() - st::photoLoaderPoint.height()) / 2;
		QColor c(st::white->c);
		QBrush b(c);
		for (int32 i = 0; i < cnt; ++i) {
			t -= delta;
			while (t < 0) t += period;
				
			float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
			c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
			b.setColor(c);
			p.fillRect(x + i * (st::photoLoaderPoint.width() + st::photoLoaderSkip), y, st::photoLoaderPoint.width(), st::photoLoaderPoint.height(), b);
		}
	}

	if (selected) {
		p.fillRect(0, 0, width, _height, textstyleCurrent()->selectOverlay->b);
	}
	style::color shadow(selected ? st::msgInSelectShadow : st::msgInShadow);
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	// date
	QString time(parent->time());
	if (time.isEmpty()) return;
	int32 dateX = width - parent->timeWidth() - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
	int32 dateY = _height - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
	if (parent->out()) {
		dateX -= st::msgCheckRect.pxWidth() + st::msgDateImgCheckSpace;
	}
	int32 dateW = width - dateX - st::msgDateImgDelta;
	int32 dateH = _height - dateY - st::msgDateImgDelta;

	p.fillRect(dateX, dateY, dateW, dateH, st::msgDateImgBg->b);
	p.setFont(st::msgDateFont->f);
	p.setPen(st::msgDateImgColor->p);
	p.drawText(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y() + st::msgDateFont->ascent, time);
	if (out) {
		QPoint iconPos(dateX - 2 + dateW - st::msgDateImgCheckSpace - st::msgCheckRect.pxWidth(), dateY + (dateH - st::msgCheckRect.pxHeight()) / 2);
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &st::msgImgCheckRect;
			} else {
				iconRect = &st::msgImgDblCheckRect;
			}
		} else {
			iconRect = &st::msgImgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

QString formatSizeText(qint64 size) {
	if (size >= 1024 * 1024) { // more than 1 mb
		qint64 sizeTenthMb = (size * 10 / (1024 * 1024));
		return QString::number(sizeTenthMb / 10) + '.' + QString::number(sizeTenthMb % 10) + qsl("Mb");
	}
	qint64 sizeTenthKb = (size * 10 / 1024);
	return QString::number(sizeTenthKb / 10) + '.' + QString::number(sizeTenthKb % 10) + qsl("Kb");
}

QString formatDownloadText(qint64 ready, qint64 total) {
	QString readyStr, totalStr, mb;
	if (total >= 1024 * 1024) { // more than 1 mb
		qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
		readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
		totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
		mb = qsl("Mb");
	} else {
		qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
		readyStr = QString::number(readyKb);
		totalStr = QString::number(totalKb);
		mb = qsl("Kb");
	}
	return lang(lng_save_downloaded).replace(qsl("{ready}"), readyStr).replace(qsl("{total}"), totalStr).replace(qsl("{mb}"), mb);
}

QString formatDurationText(qint64 duration) {
	qint64 hours = (duration / 3600), minutes = (duration % 3600) / 60, seconds = duration % 60;
	return (hours ? QString::number(hours) + ':' : QString()) + (minutes >= 10 ? QString() : QString('0')) + QString::number(minutes) + ':' + (seconds >= 10 ? QString() : QString('0')) + QString::number(seconds);
}

QString formatDurationAndSizeText(qint64 duration, qint64 size) {
	return lang(lng_duration_and_size).replace(qsl("{duration}"), formatDurationText(duration)).replace(qsl("{size}"), formatSizeText(size));
}

int32 _downloadWidth = 0, _openWithWidth = 0, _cancelWidth = 0, _buttonWidth = 0;

HistoryVideo::HistoryVideo(const MTPDvideo &video, int32 width) : data(App::feedVideo(video))
, _openl(new VideoOpenLink(data))
, _savel(new VideoSaveLink(data))
, _cancell(new VideoCancelLink(data))
, w(width)
, _dldDone(0)
, _uplDone(0)
{
	_size = formatDurationAndSizeText(data->duration, data->size);

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->m.width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->m.width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->m.width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}

	data->thumb->load();

	int32 tw = data->thumb->width(), th = data->thumb->height();
	if (data->thumb->isNull() || !tw || !th) {
		_thumbw = _thumbx = _thumby = 0;
	} else if (tw > th) {
		_thumbw = (tw * st::mediaThumbSize) / th;
		_thumbx = (_thumbw - st::mediaThumbSize) / 2;
		_thumby = 0;
	} else {
		_thumbw = st::mediaThumbSize;
		_thumbx = 0;
		_thumby = ((th * _thumbw) / tw - st::mediaThumbSize) / 2;
	}
}

void HistoryVideo::initDimensions(const HistoryItem *parent) {
	_maxw = st::mediaMaxWidth;
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	if (!parent->out()) { // add Download / Save As button
		_maxw += st::mediaSaveDelta + _buttonWidth;
	}
	_height = _minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
}

void HistoryVideo::regItem(HistoryItem *item) {
	App::regVideoItem(data, item);
}

void HistoryVideo::unregItem(HistoryItem *item) {
	App::unregVideoItem(data, item);
}

int32 HistoryVideo::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;
	return _height;
}

const QString HistoryVideo::inDialogsText() const {
	return lang(lng_in_dlg_video);
}

bool HistoryVideo::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

TextLinkPtr HistoryVideo::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return TextLinkPtr();

	bool out = parent->out(), hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!out) { // draw Download / Save As button
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			return data->loader ? _cancell : _savel;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (x >= 0 && y >= 0 && x < width && y < _height && !data->loader && data->access) {
		return _openl;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryVideo::clone() const {
	return new HistoryVideo(*this);
}

void HistoryVideo::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	data->thumb->checkload();

	bool out = parent->out(), hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!out) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		p.fillRect(QRect(btnx, btny, btnw, btnh), (selected ? st::msgInSelectBG : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor))->b);

		style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
		p.fillRect(btnx, btny + btnh, btnw, st::msgShadow, shadow->b);

		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (data->already().isEmpty() ? lng_media_download : lng_media_open_with)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (data->already().isEmpty() ? _downloadWidth : _openWithWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (out ? st::msgOutSelectBG : st::msgInSelectBG) : (out ? st::msgOutBG : st::msgInBG));
	p.fillRect(QRect(0, 0, width, _height), bg->b);

	style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	if (_thumbw) {
        int32 rf(cIntRetinaFactor());
		p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), data->thumb->pix(_thumbw), QRect(_thumbx * rf, _thumby * rf, st::mediaThumbSize * rf, st::mediaThumbSize * rf));
	} else {
		p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), App::sprite(), (out ? st::mediaDocOutImg : st::mediaDocInImg));
	}
	if (selected) {
		p.fillRect(st::mediaPadding.left(), st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, (out ? st::msgOutSelectOverlay : st::msgInSelectOverlay)->b);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 fullTimeWidth = parent->timeWidth() + st::msgDateSpace + (out ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) + st::msgPadding.right() - st::msgDateDelta.x();
	int32 secondwidth = width - tleft - fullTimeWidth;

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, lang(lng_media_video));

	QString statusText;

	style::color status(selected ? (out ? st::mediaOutSelectColor : st::mediaInSelectColor) : (out ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	if (data->loader) {
		if (_dldTextCache.isEmpty() || _dldDone != data->loader->currentOffset()) {
			_dldDone = data->loader->currentOffset();
			_dldTextCache = formatDownloadText(_dldDone, data->size);
		}
		statusText = _dldTextCache;
	} else {
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
		} else {
			statusText = _size;
		}
	}
	p.drawText(tleft, st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);

	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (out ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (out ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	p.drawText(width + st::msgDateDelta.x() - fullTimeWidth + st::msgDateSpace, _height - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgDateFont->descent, parent->time());
	if (out) {
		QPoint iconPos(width + 5 - st::msgPadding.right() - st::msgCheckRect.pxWidth(), _height + 1 - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgCheckRect.pxHeight());
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &(selected ? st::msgSelectCheckRect : st::msgCheckRect);
			} else {
				iconRect = &(selected ? st::msgSelectDblCheckRect : st::msgDblCheckRect);
			}
		} else {
			iconRect = &st::msgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

HistoryAudio::HistoryAudio(const MTPDaudio &audio, int32 width) : data(App::feedAudio(audio))
, _openl(new AudioOpenLink(data))
, _savel(new AudioSaveLink(data))
, _cancell(new AudioCancelLink(data))
, w(width)
, _dldDone(0)
, _uplDone(0)
{
	_size = formatDurationAndSizeText(data->duration, data->size);

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->m.width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->m.width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->m.width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}
}

void HistoryAudio::initDimensions(const HistoryItem *parent) {
	_maxw = st::mediaMaxWidth;
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	if (!parent->out()) { // add Download / Save As button
		_maxw += st::mediaSaveDelta + _buttonWidth;
	}

	_height = _minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
}

void HistoryAudio::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	bool out = parent->out(), hovered, pressed, already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!data->loader && data->status != FileFailed && !already && !hasdata && data->size < AudioVoiceMsgInMemory) {
		data->save(QString());
	}

	if (!out) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		p.fillRect(QRect(btnx, btny, btnw, btnh), (selected ? st::msgInSelectBG : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor))->b);
		
		style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
		p.fillRect(btnx, btny + btnh, btnw, st::msgShadow, shadow->b);
		
		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (already ? lng_media_open_with : lng_media_download)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (already ? _openWithWidth : _downloadWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (out ? st::msgOutSelectBG : st::msgInSelectBG) : (out ? st::msgOutBG : st::msgInBG));
	p.fillRect(QRect(0, 0, width, _height), bg->b);

	style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	AudioData *playing = 0;
	VoiceMessageState playingState = VoiceMessageStopped;
	int64 playingPosition = 0, playingDuration = 0;
	if (audioVoice()) {
		audioVoice()->currentState(&playing, &playingState, &playingPosition, &playingDuration);
	}
	QRect img;
	if (already || hasdata) {
		bool showPause = (playing == data) && (playingState == VoiceMessagePlaying || playingState == VoiceMessageResuming || playingState == VoiceMessageStarting);
		img = out ? (showPause ? st::mediaPauseOutImg : st::mediaPlayOutImg) : (showPause ? st::mediaPauseInImg : st::mediaPlayInImg);
	} else {
		img = out ? st::mediaAudioOutImg : st::mediaAudioInImg;
	}
	p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), App::sprite(), img);
	if (selected) {
		p.fillRect(st::mediaPadding.left(), st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, (out ? st::msgOutSelectOverlay : st::msgInSelectOverlay)->b);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 fullTimeWidth = parent->timeWidth() + st::msgDateSpace + (out ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) + st::msgPadding.right() - st::msgDateDelta.x();
	int32 secondwidth = width - tleft - fullTimeWidth;

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, lang(lng_media_audio));

	QString statusText;

	style::color status(selected ? (out ? st::mediaOutSelectColor : st::mediaInSelectColor) : (out ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);
	if (already || hasdata) {
		if (playing == data && playingState != VoiceMessageStopped) {
			statusText = formatDurationText(playingPosition / AudioVoiceMsgFrequency) + qsl(" / ") + formatDurationText(playingDuration / AudioVoiceMsgFrequency);
		} else {
			statusText = formatDurationText(data->duration);
		}
	} else {
		if (data->loader) {
			if (_dldTextCache.isEmpty() || _dldDone != data->loader->currentOffset()) {
				_dldDone = data->loader->currentOffset();
				_dldTextCache = formatDownloadText(_dldDone, data->size);
			}
			statusText = _dldTextCache;
		} else {
			if (data->status == FileFailed) {
				statusText = lang(lng_attach_failed);
			} else if (data->status == FileUploading) {
				if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
					_uplDone = data->uploadOffset;
					_uplTextCache = formatDownloadText(_uplDone, data->size);
				}
				statusText = _uplTextCache;
			} else {
				statusText = _size;
			}
		}
	}
	p.drawText(tleft, st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);
	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (out ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (out ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	p.drawText(width + st::msgDateDelta.x() - fullTimeWidth + st::msgDateSpace, _height - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgDateFont->descent, parent->time());
	if (out) {
		QPoint iconPos(width + 5 - st::msgPadding.right() - st::msgCheckRect.pxWidth(), _height + 1 - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgCheckRect.pxHeight());
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &(selected ? st::msgSelectCheckRect : st::msgCheckRect);
			} else {
				iconRect = &(selected ? st::msgSelectDblCheckRect : st::msgDblCheckRect);
			}
		} else {
			iconRect = &st::msgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

void HistoryAudio::regItem(HistoryItem *item) {
	App::regAudioItem(data, item);
}

void HistoryAudio::unregItem(HistoryItem *item) {
	App::unregAudioItem(data, item);
}

int32 HistoryAudio::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;
	return _height;
}

const QString HistoryAudio::inDialogsText() const {
	return lang(lng_in_dlg_audio);
}

bool HistoryAudio::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

TextLinkPtr HistoryAudio::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return TextLinkPtr();

	bool out = parent->out(), hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!out) { // draw Download / Save As button
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			return data->loader ? _cancell : _savel;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (x >= 0 && y >= 0 && x < width && y < _height && !data->loader && data->access) {
		return _openl;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryAudio::clone() const {
	return new HistoryAudio(*this);
}

HistoryDocument::HistoryDocument(const MTPDdocument &document, int32 width) : data(App::feedDocument(document))
, _openl(new DocumentOpenLink(data))
, _savel(new DocumentSaveLink(data))
, _cancell(new DocumentCancelLink(data))
, w(width)
, _name(data->name)
, _dldDone(0)
, _uplDone(0)
{
	_namew = st::mediaFont->m.width(_name.isEmpty() ? qsl("Document") : _name);

	_size = formatSizeText(data->size);

	_height = _minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->m.width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->m.width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->m.width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}

	data->thumb->load();

	int32 tw = data->thumb->width(), th = data->thumb->height();
	if (data->thumb->isNull() || !tw || !th) {
		_thumbw = _thumbx = _thumby = 0;
	} else if (tw > th) {
		_thumbw = (tw * st::mediaThumbSize) / th;
		_thumbx = (_thumbw - st::mediaThumbSize) / 2;
		_thumby = 0;
	} else {
		_thumbw = st::mediaThumbSize;
		_thumbx = 0;
		_thumby = ((th * _thumbw) / tw - st::mediaThumbSize) / 2;
	}
}

void HistoryDocument::initDimensions(const HistoryItem *parent) {
	if (parent == animated.msg) {
		_maxw = animated.w;
		_minh = animated.h;
		_height = resize(w, true, parent);
	} else {
		_maxw = st::mediaMaxWidth;
		int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
		if (_namew + tleft + st::mediaPadding.right() > _maxw) {
			_maxw = _namew + tleft + st::mediaPadding.right();
		}
		if (!parent->out()) { // add Download / Save As button
			_maxw += st::mediaSaveDelta + _buttonWidth;
		}
		_height = _minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	}
}

void HistoryDocument::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	bool out = parent->out(), hovered, pressed;
	if (parent == animated.msg) {
		if (width >= animated.w) {
			p.drawPixmap(0, 0, animated.frames[animated.frame]);
			if (selected) {
				p.fillRect(0, 0, animated.w, animated.h, (out ? st::msgOutSelectOverlay : st::msgInSelectOverlay)->b);
			}
		} else {
			bool s = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
			if (!s) p.setRenderHint(QPainter::SmoothPixmapTransform);
			int32 h = (width == w) ? _height : (width * animated.h / animated.w);
			if (h < 1) h = 1;
			p.drawPixmap(QRect(0, 0, width, h), animated.frames[animated.frame]);
			if (!s) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			if (selected) {
				p.fillRect(0, 0, width, h, (out ? st::msgOutSelectOverlay : st::msgInSelectOverlay)->b);
			}
		}
		return;
	}

	data->thumb->checkload();

	if (width >= _maxw) {
		width = _maxw;
	}

	if (!out) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		p.fillRect(QRect(btnx, btny, btnw, btnh), (selected ? st::msgInSelectBG : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor))->b);

		style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
		p.fillRect(btnx, btny + btnh, btnw, st::msgShadow, shadow->b);

		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (data->already().isEmpty() ? lng_media_download : lng_media_open_with)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (data->already().isEmpty() ? _downloadWidth : _openWithWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (out ? st::msgOutSelectBG : st::msgInSelectBG) : (out ? st::msgOutBG : st::msgInBG));
	p.fillRect(QRect(0, 0, width, _height), bg->b);

	style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	if (_thumbw) {
        int32 rf(cIntRetinaFactor());
		p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), data->thumb->pix(_thumbw), QRect(_thumbx * rf, _thumby * rf, st::mediaThumbSize * rf, st::mediaThumbSize * rf));
	} else {
		p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), App::sprite(), (out ? st::mediaDocOutImg : st::mediaDocInImg));
	}
	if (selected) {
		p.fillRect(st::mediaPadding.left(), st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, (out ? st::msgOutSelectOverlay : st::msgInSelectOverlay)->b);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 fullTimeWidth = parent->timeWidth() + st::msgDateSpace + (out ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) + st::msgPadding.right() - st::msgDateDelta.x();
	int32 secondwidth = width - tleft - fullTimeWidth;

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	if (twidth < _namew) {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->m.elidedText(_name, Qt::ElideRight, twidth));
	} else {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, _name);
	}

	QString statusText;

	style::color status(selected ? (out ? st::mediaOutSelectColor : st::mediaInSelectColor) : (out ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	if (data->loader) {
		if (_dldTextCache.isEmpty() || _dldDone != data->loader->currentOffset()) {
			_dldDone = data->loader->currentOffset();
			_dldTextCache = formatDownloadText(_dldDone, data->size);
		}
		statusText = _dldTextCache;
	} else {
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
		} else {
			statusText = _size;
		}
	}
	p.drawText(tleft, st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);

	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (out ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (out ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	p.drawText(width + st::msgDateDelta.x() - fullTimeWidth + st::msgDateSpace, _height - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgDateFont->descent, parent->time());
	if (out) {
		QPoint iconPos(width + 5 - st::msgPadding.right() - st::msgCheckRect.pxWidth(), _height + 1 - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgCheckRect.pxHeight());
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &(selected ? st::msgSelectCheckRect : st::msgCheckRect);
			} else {
				iconRect = &(selected ? st::msgSelectDblCheckRect : st::msgDblCheckRect);
			}
		} else {
			iconRect = &st::msgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

void HistoryDocument::regItem(HistoryItem *item) {
	App::regDocumentItem(data, item);
}

void HistoryDocument::unregItem(HistoryItem *item) {
	App::unregDocumentItem(data, item);
}

void HistoryDocument::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, data);
	}
}

int32 HistoryDocument::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;
	if (parent == animated.msg) {
		_height = animated.h;
		if (animated.w > w) {
			_height = (w * _height / animated.w);
			if (_height <= 0) _height = 1;
		}
	}
	return _height;
}

const QString HistoryDocument::inDialogsText() const {
	return data->name.isEmpty() ? lang(lng_in_dlg_document) : data->name;
}

bool HistoryDocument::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		return (x >= 0 && y >= 0 && x < width && y < h);
	}
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

int32 HistoryDocument::countHeight(const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		return h;
	}
	return _height;
}

TextLinkPtr HistoryDocument::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return TextLinkPtr();

	bool out = parent->out(), hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		return (x >= 0 && y >= 0 && x < width && y < h) ? _openl : TextLinkPtr();
	}

	if (!out) { // draw Download / Save As button
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = (_height - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			return data->loader ? _cancell : _savel;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (x >= 0 && y >= 0 && x < width && y < _height && !data->loader && data->access) {
		return _openl;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryDocument::clone() const {
	return new HistoryDocument(*this);
}

HistoryContact::HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone) : userId(userId)
, w(0)
, phone(App::formatPhone(phone))
, contact(App::userLoaded(userId))
{
	_maxw = st::mediaMaxWidth;
	name.setText(st::mediaFont, (first + ' ' + last).trimmed(), _textNameOptions);

	_height = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	phonew = st::mediaFont->m.width(phone);

	if (contact) {
		if (contact->phone.isEmpty()) {
			contact->setPhone(phone);
		}
		if (contact->contact < 0) {
			contact->contact = 0;
		}
		contact->photo->load();
	}
}

void HistoryContact::initDimensions(const HistoryItem *parent) {
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 fullTimeWidth = parent->timeWidth() + st::msgDateSpace + (parent->out() ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) + st::msgPadding.right() - st::msgDateDelta.x();
	if (name.maxWidth() + tleft + fullTimeWidth > _maxw) {
		_maxw = name.maxWidth() + tleft + fullTimeWidth;
	}
	if (phonew + tleft + st::mediaPadding.right() > _maxw) {
		_maxw = phonew + tleft + st::mediaPadding.right();
	}
}

int32 HistoryContact::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;
	return _height;
}

const QString HistoryContact::inDialogsText() const {
	return lang(lng_in_dlg_contact);
}

bool HistoryContact::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y <= 0 && x < w && y < _height);
}

TextLinkPtr HistoryContact::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (x >= 0 && y >= 0 && x < w && y < _height && contact) {
		return contact->lnk;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryContact::clone() const {
	QStringList names = name.original(0, 0xFFFF, false).split(QChar(' '), QString::SkipEmptyParts);
	if (names.isEmpty()) {
		names.push_back(QString());
	}
	QString fname = names.front();
	names.pop_front();
	HistoryContact *result = new HistoryContact(userId, fname, names.join(QChar(' ')), phone);
	return result;
}

void HistoryContact::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	bool out = parent->out();
	if (width >= _maxw) {
		width = _maxw;
	}

	style::color bg(selected ? (out ? st::msgOutSelectBG : st::msgInSelectBG) : (out ? st::msgOutBG : st::msgInBG));
	p.fillRect(QRect(0, 0, width, _height), bg->b);

	style::color shadow(selected ? (out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (out ? st::msgOutShadow : st::msgInShadow));
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	p.drawPixmap(st::mediaPadding.left(), st::mediaPadding.top(), (contact ? contact->photo : userDefPhoto(1))->pix(st::mediaThumbSize));

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 fullTimeWidth = parent->timeWidth() + st::msgDateSpace + (out ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) + st::msgPadding.right() - st::msgDateDelta.x();
	int32 secondwidth = width - tleft - fullTimeWidth;

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	if (twidth < phonew) {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->m.elidedText(phone, Qt::ElideRight, twidth));
	} else {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, phone);
	}

	style::color status(selected ? (out ? st::mediaOutSelectColor : st::mediaInSelectColor) : (out ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	name.drawElided(p, tleft, st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->height, secondwidth);

	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (out ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (out ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	p.drawText(width + st::msgDateDelta.x() - fullTimeWidth + st::msgDateSpace, _height - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgDateFont->descent, parent->time());
	if (out) {
		QPoint iconPos(width + 5 - st::msgPadding.right() - st::msgCheckRect.pxWidth(), _height + 1 - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgCheckRect.pxHeight());
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &(selected ? st::msgSelectCheckRect : st::msgCheckRect);
			} else {
				iconRect = &(selected ? st::msgSelectDblCheckRect : st::msgDblCheckRect);
			}
		} else {
			iconRect = &st::msgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

void HistoryContact::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaContact) {
		userId = media.c_messageMediaContact().vuser_id.v;
		contact = App::userLoaded(userId);
		if (contact) {
			if (contact->phone.isEmpty()) {
				contact->setPhone(phone);
			}
			if (contact->contact < 0) {
				contact->contact = 0;
			}
			contact->photo->load();
		}
	}
}

namespace {
	QRegularExpression reYouTube1(qsl("^(https?://)?(www\\.)?youtube\\.com/watch\\?v=([a-z0-9_-]+)(&|$)"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpression reYouTube2(qsl("^(https?://)?(www\\.)?youtu\\.be/([a-z0-9_-]+)(\\?|$)"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpression reInstagram(qsl("^(https?://)?(www\\.)?instagram\\.com/p/([a-z0-9_-]+)(/|$)"), QRegularExpression::CaseInsensitiveOption);

	ImageLinkManager manager;
}

void ImageLinkManager::init() {
	if (manager) delete manager;
	manager = new QNetworkAccessManager();
	App::setProxySettings(*manager);

	connect(manager, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>&errors)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));

	if (black) delete black;
	QImage b(cIntRetinaFactor(), cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	{
		QPainter p(&b);
		p.fillRect(QRect(0, 0, cIntRetinaFactor(), cIntRetinaFactor()), st::white->b);
	}
	QPixmap p = QPixmap::fromImage(b);
	p.setDevicePixelRatio(cRetinaFactor());
	black = new ImagePtr(p, "PNG");
}

void ImageLinkManager::reinit() {
	if (manager) App::setProxySettings(*manager);
}

void ImageLinkManager::deinit() {
	if (manager) {
		delete manager;
		manager = 0;
	}
	if (black) {
		delete black;
		black = 0;
	}
	dataLoadings.clear();
	imageLoadings.clear();
}

void initImageLinkManager() {
	manager.init();
}

void reinitImageLinkManager() {
	manager.reinit();
}

void deinitImageLinkManager() {
	manager.deinit();
}

void ImageLinkManager::getData(ImageLinkData *data) {
	if (!manager) {
		DEBUG_LOG(("App Error: getting image link data without manager init!"));
		return failed(data);
	}
	QString url;
	switch (data->type) {
	case YouTubeLink: {
		url = qsl("https://gdata.youtube.com/feeds/api/videos/") + data->id.mid(8) + qsl("?v=2&alt=json");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		dataLoadings[reply] = data;
	} break;
	case InstagramLink: {
		//url = qsl("https://api.instagram.com/oembed?url=http://instagr.am/p/") + data->id.mid(10) + '/';
		url = qsl("https://instagram.com/p/") + data->id.mid(10) + qsl("/media/?size=l");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		imageLoadings[reply] = data;
	} break;
	case GoogleMapsLink: {
		int32 w = st::locationSize.width(), h = st::locationSize.height();
		int32 zoom = 13, scale = 1;
		if (cScale() == dbisTwo || cRetina()) {
			scale = 2;
		} else {
			w = convertScale(w);
			h = convertScale(h);
		}
		url = qsl("https://maps.googleapis.com/maps/api/staticmap?center=") + data->id.mid(9) + qsl("&zoom=%1&size=%2x%3&maptype=roadmap&scale=%4&markers=color:red|size:big|").arg(zoom).arg(w).arg(h).arg(scale) + data->id.mid(9) + qsl("&sensor=false");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		imageLoadings[reply] = data;
	} break;
	default: {
		failed(data);
	} break;
	}
}

void ImageLinkManager::onFinished(QNetworkReply *reply) {
	if (!manager) return;
	if (reply->error() != QNetworkReply::NoError) return onFailed(reply);

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
				if (i != dataLoadings.cend()) {
					ImageLinkData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					dataLoadings.erase(i);
					dataLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				} else if ((i = imageLoadings.find(reply)) != imageLoadings.cend()) {
					ImageLinkData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					imageLoadings.erase(i);
					imageLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				}
			}
		}
		if (status != 200) {
			DEBUG_LOG(("Network Error: Bad HTTP status received in onFinished() for image link: %1").arg(status));
			return onFailed(reply);
		}
	}

	ImageLinkData *d = 0;
	QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);

		QJsonParseError e;
		QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &e);
		if (e.error != QJsonParseError::NoError) {
			DEBUG_LOG(("JSON Error: Bad json received in onFinished() for image link"));
			return onFailed(reply);
		}
		QJsonObject obj = doc.object();
		switch (d->type) {
		case YouTubeLink: {
			QString thumb;
			int32 seconds = 0;
			QJsonObject::const_iterator entryIt = obj.constFind(qsl("entry"));
			if (entryIt != obj.constEnd() && entryIt.value().isObject()) {
				QJsonObject entry = entryIt.value().toObject();
				QJsonObject::const_iterator mediaIt = entry.constFind(qsl("media$group"));
				if (mediaIt != entry.constEnd() && mediaIt.value().isObject()) {
					QJsonObject media = mediaIt.value().toObject();

					// title from media
					QJsonObject::const_iterator titleIt = media.constFind(qsl("media$title"));
					if (titleIt != media.constEnd() && titleIt.value().isObject()) {
						QJsonObject title = titleIt.value().toObject();
						QJsonObject::const_iterator tIt = title.constFind(qsl("$t"));
						if (tIt != title.constEnd() && tIt.value().isString()) {
							d->title = tIt.value().toString();
						}
					}

					// thumb
					QJsonObject::const_iterator thumbnailsIt = media.constFind(qsl("media$thumbnail"));
					int32 bestLevel = 0;
					if (thumbnailsIt != media.constEnd() && thumbnailsIt.value().isArray()) {
						QJsonArray thumbnails = thumbnailsIt.value().toArray();
						for (int32 i = 0, l = thumbnails.size(); i < l; ++i) {
							QJsonValue thumbnailVal = thumbnails.at(i);
							if (!thumbnailVal.isObject()) continue;
							
							QJsonObject thumbnail = thumbnailVal.toObject();
							QJsonObject::const_iterator urlIt = thumbnail.constFind(qsl("url"));
							if (urlIt == thumbnail.constEnd() || !urlIt.value().isString()) continue;

							int32 level = 0;
							if (thumbnail.constFind(qsl("time")) == thumbnail.constEnd()) {
								level += 10;
							}
							QJsonObject::const_iterator wIt = thumbnail.constFind(qsl("width"));
							if (wIt != thumbnail.constEnd()) {
								int32 w = 0;
								if (wIt.value().isDouble()) {
									w = qMax(qRound(wIt.value().toDouble()), 0);
								} else if (wIt.value().isString()) {
									w = qMax(qRound(wIt.value().toString().toDouble()), 0);
								}
								switch (w) {
								case 640: level += 4; break;
								case 480: level += 3; break;
								case 320: level += 2; break;
								case 120: level += 1; break;
								}
							}
							if (level > bestLevel) {
								thumb = urlIt.value().toString();
								bestLevel = level;
							}
						}
					}

					// duration
					QJsonObject::const_iterator durationIt = media.constFind(qsl("yt$duration"));
					if (durationIt != media.constEnd() && durationIt.value().isObject()) {
						QJsonObject duration = durationIt.value().toObject();
						QJsonObject::const_iterator secondsIt = duration.constFind(qsl("seconds"));
						if (secondsIt != duration.constEnd()) {
							if (secondsIt.value().isDouble()) {
								seconds = qRound(secondsIt.value().toDouble());
							} else if (secondsIt.value().isString()) {
								seconds = qRound(secondsIt.value().toString().toDouble());
							}
						}
					}
				}

				// title field
				if (d->title.isEmpty()) {
					QJsonObject::const_iterator titleIt = entry.constFind(qsl("title"));
					if (titleIt != entry.constEnd() && titleIt.value().isObject()) {
						QJsonObject title = titleIt.value().toObject();
						QJsonObject::const_iterator tIt = title.constFind(qsl("$t"));
						if (tIt != title.constEnd() && tIt.value().isString()) {
							d->title = tIt.value().toString();
						}
					}
				}
			}

			if (seconds > 0) {
				d->duration = formatDurationText(seconds);
			}
			if (thumb.isEmpty()) {
				failed(d);
			} else {
				imageLoadings.insert(manager->get(QNetworkRequest(thumb)), d);
			}
		} break;

		case InstagramLink: failed(d); break;
		case GoogleMapsLink: failed(d); break;
		}

		if (App::main()) App::main()->update();
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);

			QPixmap thumb;
			QByteArray format;
			QByteArray data(reply->readAll());
			{
				QBuffer buffer(&data);
				QImageReader reader(&buffer);
				thumb = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
				format = reader.format();
				thumb.setDevicePixelRatio(cRetinaFactor());
				if (format.isEmpty()) format = QByteArray("JPG");
			}
			d->loading = false;
			d->thumb = thumb.isNull() ? (*black) : ImagePtr(thumb, format);
			serverRedirects.remove(d);
			if (App::main()) App::main()->update();
		}
	}
}

void ImageLinkManager::onFailed(QNetworkReply *reply) {
	if (!manager) return;

	ImageLinkData *d = 0;
	QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);
		}
	}
	DEBUG_LOG(("Network Error: failed to get data for image link %1, error %2").arg(d ? d->id : 0).arg(reply->errorString()));
	if (d) {
		failed(d);
	}
}

void ImageLinkManager::failed(ImageLinkData *data) {
	data->loading = false;
	data->thumb = *black;
	serverRedirects.remove(data);
}

void ImageLinkData::load() {
	if (!thumb->isNull()) return thumb->load(false, false);
	if (loading) return;

	loading = true;
	manager.getData(this);
}

HistoryImageLink::HistoryImageLink(const QString &url, int32 width) : w(width) {
	if (url.startsWith(qsl("location:"))) {
		data = App::imageLink(url, GoogleMapsLink, qsl("https://maps.google.com/maps?q=") + url.mid(9) + qsl("&ll=") + url.mid(9) + qsl("&z=17"));
	} else {
		QRegularExpressionMatch m = reYouTube1.match(url);
		if (!m.hasMatch()) m = reYouTube2.match(url);
		if (m.hasMatch()) {
			data = App::imageLink(qsl("youtube:") + m.captured(3), YouTubeLink, url);
		} else {
			m = reInstagram.match(url);
			if (m.hasMatch()) {
				data = App::imageLink(qsl("instagram:") + m.captured(3), InstagramLink, url);
			} else {
				data = 0;
			}
		}
	}
}

int32 HistoryImageLink::fullWidth() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return 640;
		case InstagramLink: return 640;
		case GoogleMapsLink: return st::locationSize.width();
		}
	}
	return st::minPhotoWidth;
}

int32 HistoryImageLink::fullHeight() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return 480;
		case InstagramLink: return 640;
		case GoogleMapsLink: return st::locationSize.height();
		}
	}
	return st::minPhotoHeight;
}

void HistoryImageLink::initDimensions(const HistoryItem *parent) {
	int32 tw = convertScale(fullWidth()), th = convertScale(fullHeight());
	int32 thumbw = qMax(tw, int32(st::minPhotoWidth)), maxthumbh = thumbw;
	int32 thumbh = qRound(th * float64(thumbw) / tw);
	if (thumbh > maxthumbh) {
		thumbw = qRound(thumbw * float64(maxthumbh) / thumbh);
		thumbh = maxthumbh;
		if (thumbw < st::minPhotoWidth) {
			thumbw = st::minPhotoWidth;
		}
	}
	if (thumbh < st::minPhotoHeight) {
		thumbh = st::minPhotoHeight;
	}
	if (!w) {
		w = thumbw;
	}
	_maxw = w;
	_height = _minh = thumbh;
}

void HistoryImageLink::draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;

	data->load();
	bool out = parent->out();
	QPixmap toDraw;
	if (data && !data->thumb->isNull()) {
		int32 w = data->thumb->width(), h = data->thumb->height();
		if (width * h == _height * w || (w == convertScale(fullWidth()) && h == convertScale(fullHeight()))) {
			p.drawPixmap(QPoint(0, 0), data->thumb->pixSingle(width, _height));
		} else {
			p.fillRect(QRect(0, 0, width, _height), st::black->b);
			if (width * h > _height * w) {
				int32 nw = _height * w / h;
				p.drawPixmap(QPoint((width - nw) / 2, 0), data->thumb->pixSingle(nw, _height));
			} else {
				int32 nh = width * h / w;
				p.drawPixmap(QPoint(0, (_height - nh) / 2), data->thumb->pixSingle(width, nh));
			}
		}
	} else {
		p.fillRect(QRect(0, 0, width, _height), st::black->b);
	}
	if (data) {
		switch (data->type) {
		case YouTubeLink: p.drawPixmap(QPoint((width - st::youtubeIcon.pxWidth()) / 2, (_height - st::youtubeIcon.pxHeight()) / 2), App::sprite(), st::youtubeIcon); break;
		case InstagramLink: p.drawPixmap(QPoint((width - st::instagramIcon.pxWidth()) / 2, (_height - st::instagramIcon.pxHeight()) / 2), App::sprite(), st::instagramIcon); break;
		}
		if (!data->title.isEmpty() || !data->duration.isEmpty()) {
			p.fillRect(0, 0, width, st::msgDateFont->height + 2 * st::msgDateImgPadding.y(), st::msgDateImgBg->b);
			p.setFont(st::msgDateFont->f);
			p.setPen(st::msgDateImgColor->p);
			int32 titleWidth = width - 2 * st::msgDateImgPadding.x();
			if (!data->duration.isEmpty()) {
				int32 durationWidth = st::msgDateFont->m.width(data->duration);
				p.drawText(width - st::msgDateImgPadding.x() - durationWidth, st::msgDateImgPadding.y() + st::msgDateFont->ascent, data->duration);
				titleWidth -= durationWidth + st::msgDateImgPadding.x();
			}
			if (!data->title.isEmpty()) {
				p.drawText(st::msgDateImgPadding.x(), st::msgDateImgPadding.y() + st::msgDateFont->ascent, st::msgDateFont->m.elidedText(data->title, Qt::ElideRight, titleWidth));
			}
		}
	}
	if (selected) {
		p.fillRect(0, 0, width, _height, textstyleCurrent()->selectOverlay->b);
	}
	style::color shadow(selected ? st::msgInSelectShadow : st::msgInShadow);
	p.fillRect(0, _height, width, st::msgShadow, shadow->b);

	// date
	QString time(parent->time());
	if (time.isEmpty()) return;
	int32 dateX = width - parent->timeWidth() - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
	int32 dateY = _height - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
	if (parent->out()) {
		dateX -= st::msgCheckRect.pxWidth() + st::msgDateImgCheckSpace;
	}
	int32 dateW = width - dateX - st::msgDateImgDelta;
	int32 dateH = _height - dateY - st::msgDateImgDelta;

	p.fillRect(dateX, dateY, dateW, dateH, st::msgDateImgBg->b);
	p.setFont(st::msgDateFont->f);
	p.setPen(st::msgDateImgColor->p);
	p.drawText(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y() + st::msgDateFont->ascent, time);
	if (out) {
		QPoint iconPos(dateX - 2 + dateW - st::msgDateImgCheckSpace - st::msgCheckRect.pxWidth(), dateY + (dateH - st::msgCheckRect.pxHeight()) / 2);
		const QRect *iconRect;
		if (parent->id > 0) {
			if (parent->unread()) {
				iconRect = &st::msgImgCheckRect;
			} else {
				iconRect = &st::msgImgDblCheckRect;
			}
		} else {
			iconRect = &st::msgImgSendingRect;
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

int32 HistoryImageLink::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	w = width;

	int32 tw = convertScale(fullWidth()), th = convertScale(fullHeight());
	_height = th;
	if (tw > w) {
		_height = (w * _height / tw);
	} else {
		w = tw;
	}
	if (_height > width) {
		w = (w * width) / _height;
		_height = width;
	}
	if (w < st::minPhotoWidth) {
		w = st::minPhotoWidth;
	}
	if (_height < st::minPhotoHeight) {
		_height = st::minPhotoHeight;
	}
	return _height;
}

const QString HistoryImageLink::inDialogsText() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return qsl("YouTube Video");
		case InstagramLink: return qsl("Instagram Link");
		case GoogleMapsLink: return lang(lng_maps_point);
		}
	}
	return QString();
}

bool HistoryImageLink::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

TextLinkPtr HistoryImageLink::getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (x >= 0 && y >= 0 && x < width && y < _height && data) {
		return data->openl;
	}
	return TextLinkPtr();
}

HistoryMedia *HistoryImageLink::clone() const {
	return new HistoryImageLink(*this);
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg) :
	HistoryItem(history, block, msg.vid.v, (msg.vflags.v & 0x02), (msg.vflags.v & 0x01), ::date(msg.vdate), msg.vfrom_id.v)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
{
	QString text(textClean(qs(msg.vmessage)));
	initMedia(msg.vmedia, text);
	initDimensions(text);
}

//HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, const MTPDgeoChatMessage &msg) :
//	HistoryItem(history, block, msg.vid.v, (msg.vfrom_id.v == MTP::authedId()), false, ::date(msg.vdate), msg.vfrom_id.v), media(0), message(st::msgMinWidth) {
//	QString text(qs(msg.vmessage));
//	initMedia(msg.vmedia, text);
//	initDimensions(text);
//}

//HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg) : message(st::msgMinWidth),
//	HistoryItem(history, block, msgId, out, unread, date, from), media(0), _text(st::msgMinWidth), _textWidth(0), _textHeight(0) {
//	initDimensions(textClean(msg));
//}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg, const MTPMessageMedia &media) :
	HistoryItem(history, block, msgId, out, unread, date, from)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
{
	QString text(msg);
	initMedia(media, text);
	initDimensions(text);
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg, HistoryMedia *fromMedia) :
	HistoryItem(history, block, msgId, out, unread, date, from)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
{
	QString text(msg);
	if (fromMedia) {
		_media = fromMedia->clone();
		_media->regItem(this);
	}
	initDimensions(text);
}

void HistoryMessage::initMedia(const MTPMessageMedia &media, QString &currentText) {
	switch (media.type()) {
	case mtpc_messageMediaEmpty: {
		QString lnk = currentText.trimmed();
		if (reYouTube1.match(currentText).hasMatch() || reYouTube2.match(currentText).hasMatch() || reInstagram.match(currentText).hasMatch()) {
			_media = new HistoryImageLink(lnk);
			currentText = QString();
		}
	} break;
	case mtpc_messageMediaContact: {
		const MTPDmessageMediaContact &d(media.c_messageMediaContact());
		_media = new HistoryContact(d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number));
	} break;
	case mtpc_messageMediaGeo: {
		const MTPGeoPoint &point(media.c_messageMediaGeo().vgeo);
		if (point.type() == mtpc_geoPoint) {
			const MTPDgeoPoint &d(point.c_geoPoint());
			_media = new HistoryImageLink(qsl("location:%1,%2").arg(d.vlat.v).arg(d.vlong.v));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		const MTPPhoto &photo(media.c_messageMediaPhoto().vphoto);
		if (photo.type() == mtpc_photo) {
			_media = new HistoryPhoto(photo.c_photo());
		}
	} break;
	case mtpc_messageMediaVideo: {
		const MTPVideo &video(media.c_messageMediaVideo().vvideo);
		if (video.type() == mtpc_video) {
			_media = new HistoryVideo(video.c_video());
		}
	} break;
	case mtpc_messageMediaAudio: {
		const MTPAudio &audio(media.c_messageMediaAudio().vaudio);
		if (audio.type() == mtpc_audio) {
			_media = new HistoryAudio(audio.c_audio());
		}
	} break;
	case mtpc_messageMediaDocument: {
		const MTPDocument &document(media.c_messageMediaDocument().vdocument);
		if (document.type() == mtpc_document) {
			_media = new HistoryDocument(document.c_document());
		}
	} break;
	case mtpc_messageMediaUnsupported:
	default: currentText += " (unsupported media)"; break;
	};
	if (_media) _media->regItem(this);
}

void HistoryMessage::initDimensions(const QString &text) {
	_time = date.toString(qsl("hh:mm"));
	_timeWidth = st::msgDateFont->m.width(_time);
	if (!_media) {
		_timeWidth += st::msgDateSpace + (out() ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) - st::msgDateDelta.x();
		_text.setText(st::msgFont, text + textcmdSkipBlock(_timeWidth, st::msgDateFont->height - st::msgDateDelta.y()), _historyTextOptions);
	}
	initDimensions();
}

void HistoryMessage::initDimensions(const HistoryItem *parent) {
	if (_media) {
		_media->initDimensions(this);
		_maxw = _media->maxWidth();
		_minh = _media->height();
	} else {
		_maxw = _text.maxWidth();
		_minh = _text.minHeight();
		_maxw += st::msgPadding.left() + st::msgPadding.right();
	}
	fromNameUpdated();
}

void HistoryMessage::fromNameUpdated() const {
	if (_media) return;
	int32 _namew = ((!_out && _history->peer->chat) ? _from->nameText.maxWidth() : 0) + st::msgPadding.left() + st::msgPadding.right();
	if (_namew > _maxw) _maxw = _namew;
}

bool HistoryMessage::uploading() const {
	return _media ? _media->uploading() : false;
}

QString HistoryMessage::selectedText(uint32 selection) const {
	if (_media && selection == FullItemSel) {
		return _text.original(0, 0xFFFF) + '[' + _media->inDialogsText() + ']';
	}
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo);
}

HistoryMedia *HistoryMessage::getMedia(bool inOverview) const {
	return _media;
}

void HistoryMessage::draw(QPainter &p, uint32 selection) const {
	textstyleSet(&(out() ? st::outTextStyle : st::inTextStyle));

	if (id == _history->activeMsgId) {
		uint64 ms = App::main() ? App::main()->animActiveTime() : 0;
		if (ms) {
			if (ms > st::activeFadeInDuration + st::activeFadeOutDuration) {
				App::main()->stopAnimActive();
			} else {
				float64 dt = (ms > st::activeFadeInDuration) ? (1 - (ms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (ms / float64(st::activeFadeInDuration));
				float64 o = p.opacity();
				p.setOpacity(o * dt);
				p.fillRect(0, 0, _history->width, _height, textstyleCurrent()->selectOverlay->b);
				p.setOpacity(o);
			}
		}
	}

	bool selected = (selection == FullItemSel);
	if (_from->nameVersion > _fromVersion) {
		fromNameUpdated();
		_fromVersion = _from->nameVersion;
	}
	int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (_media && _media->maxWidth() > mwidth) mwidth = _media->maxWidth();
	if (width > mwidth) {
		if (_out) left += width - mwidth;
		width = mwidth;
	}

	if (!_out && _history->peer->chat) {
		p.drawPixmap(left, _height - st::msgMargin.bottom() - st::msgPhotoSize, _from->photo->pix(st::msgPhotoSize));
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (_out) left += width - _maxw;
		width = _maxw;
	}
	if (_media) {
		p.save();
		p.translate(left, st::msgMargin.top());
		_media->draw(p, this, selected);
		p.restore();
	} else {
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());

		style::color bg(selected ? (_out ? st::msgOutSelectBG : st::msgInSelectBG) : (_out ? st::msgOutBG : st::msgInBG));
		p.fillRect(r, bg->b);

		style::color shadow(selected ? (_out ? st::msgOutSelectShadow : st::msgInSelectShadow) : (_out ? st::msgOutShadow : st::msgInShadow));
		p.fillRect(left, _height - st::msgMargin.bottom(), width, st::msgShadow, shadow->b);

		if (!_out && _history->peer->chat) {
			p.setFont(st::msgNameFont->f);
			p.setPen(_from->color->p);
			_from->nameText.drawElided(p, r.left() + st::msgPadding.left(), r.top() + st::msgPadding.top(), width - st::msgPadding.left() - st::msgPadding.right());
			r.setTop(r.top() + st::msgNameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));
		drawMessageText(p, trect, selection);

		p.setFont(st::msgDateFont->f);

		style::color date(selected ? (_out ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (_out ? st::msgOutDateColor : st::msgInDateColor));
		p.setPen(date->p);

		p.drawText(r.right() - st::msgPadding.right() + st::msgDateDelta.x() - _timeWidth + st::msgDateSpace, r.bottom() - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgDateFont->descent, _time);
		if (_out) {
			QPoint iconPos(r.right() + 5 - st::msgPadding.right() - st::msgCheckRect.pxWidth(), r.bottom() + 1 - st::msgPadding.bottom() + st::msgDateDelta.y() - st::msgCheckRect.pxHeight());
			const QRect *iconRect;
			if (id > 0) {
				if (unread()) {
					iconRect = &(selected ? st::msgSelectCheckRect : st::msgCheckRect);
				} else {
					iconRect = &(selected ? st::msgSelectDblCheckRect : st::msgDblCheckRect);
				}
			} else {
				iconRect = &st::msgSendingRect;
			}
			p.drawPixmap(iconPos, App::sprite(), *iconRect);
		}
	}
}

void HistoryMessage::drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const {
	p.setPen(st::msgColor->p);
	p.setFont(st::msgFont->f);
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignLeft, 0, -1, selectedFrom, selectedTo);

	textstyleRestore();
}

int32 HistoryMessage::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	width -= st::msgMargin.left() + st::msgMargin.right();
	if (_media) {
		_height = _media->resize(width, dontRecountText, this);
	} else {
		if (dontRecountText) return _height;

		if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
			width = st::msgPadding.left() + st::msgPadding.right() + 1;
		} else if (width > st::msgMaxWidth) {
			width = st::msgMaxWidth;
		}
		int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
		if (nwidth != _textWidth) {
			_textWidth = nwidth;
			_textHeight = _text.countHeight(nwidth);
		}
		if (width >= _maxw) {
			_height = _minh;
		} else {
			_height = _textHeight;
		}
		if (!_out && _history->peer->chat) {
			_height += st::msgNameFont->height;
		}
		_height += st::msgPadding.top() + st::msgPadding.bottom();
	}
	_height += st::msgMargin.top() + st::msgMargin.bottom();
	return _height;
}

bool HistoryMessage::hasPoint(int32 x, int32 y) const {
	int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (_media && _media->maxWidth() > mwidth) mwidth = _media->maxWidth();
	if (width > mwidth) {
		if (_out) left += width - mwidth;
		width = mwidth;
	}

	if (!_out && _history->peer->chat) { // from user left photo
		left += st::msgPhotoSkip;
	}
	if (width < 1) return false;

	if (width >= _maxw) {
		if (_out) left += width - _maxw;
		width = _maxw;
	}
	if (_media) {
		return _media->hasPoint(x - left, y - st::msgMargin.top(), this);
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	return r.contains(x, y);
}

void HistoryMessage::getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const {
	inText = false;
	lnk = TextLinkPtr();

	int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (_media && _media->maxWidth() > mwidth) mwidth = _media->maxWidth();
	if (width > mwidth) {
		if (_out) left += width - mwidth;
		width = mwidth;
	}

	if (!_out && _history->peer->chat) { // from user left photo
		if (x >= left && x < left + st::msgPhotoSize && y >= _height - st::msgMargin.bottom() - st::msgPhotoSize && y < _height - st::msgMargin.bottom()) {
			lnk = _from->lnk;
			return;
		}
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (_out) left += width - _maxw;
		width = _maxw;
	}
	if (_media) {
		lnk = _media->getLink(x - left, y - st::msgMargin.top(), this);
		return;
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	if (!_out && _history->peer->chat) { // from user left name
		if (x >= r.left() + st::msgPadding.left() && y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + st::msgNameFont->height && x < r.right() - st::msgPadding.right() && x < r.left() + st::msgPadding.left() + _from->nameText.maxWidth()) {
			lnk = _from->lnk;
			return;
		}
		r.setTop(r.top() + st::msgNameFont->height);
	}
	QRect trect(r.marginsAdded(-st::msgPadding));
	_text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width());
}

void HistoryMessage::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;
	if (_media) return;

	int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right();
	if (width > st::msgMaxWidth) {
		if (_out) left += width - st::msgMaxWidth;
		width = st::msgMaxWidth;
	}

	if (!_out && _history->peer->chat) { // from user left photo
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (_out) left += width - _maxw;
		width = _maxw;
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	if (!_out && _history->peer->chat) { // from user left name
		r.setTop(r.top() + st::msgNameFont->height);
	}
	QRect trect(r.marginsAdded(-st::msgPadding));
	_text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width());
}

void HistoryMessage::drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		QString msg(_media ? _media->inDialogsText() : _text.original(0, 0xFFFF, false));
		if (_history->peer->chat || out()) {
			TextCustomTagsMap custom;
			custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
			msg = lang(lng_message_with_from).replace(qsl("{from}"), textRichPrepare((_from == App::self()) ? lang(lng_from_you) : _from->firstName)).replace(qsl("{message}"), textRichPrepare(msg));
			cache.setRichText(st::dlgHistFont, msg, _textDlgOptions, custom);
		} else {
			cache.setText(st::dlgHistFont, msg, _textDlgOptions);
		}
	}
	if (r.width()) {
		textstyleSet(&(act ? st::dlgActiveTextStyle : st::dlgTextStyle));
		p.setFont(st::dlgHistFont->f);
		p.setPen((act ? st::dlgActiveColor : (_media ? st::dlgSystemColor : st::dlgTextColor))->p);
		cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dlgHistFont->height);
	}
}

QString HistoryMessage::notificationHeader() const {
    return _history->peer->chat ? from()->name : QString();
}

QString HistoryMessage::notificationText() const {
    QString msg(_media ? _media->inDialogsText() : _text.original(0, 0xFFFF, false));
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("..");
// subtitle used
//    if (_history->peer->chat || out()) {
//        msg = lang(lng_message_with_from).replace(qsl("[c]"), QString()).replace(qsl("[/c]"), QString()).replace(qsl("{from}"), textRichPrepare((_from == App::self()) ? lang(lng_from_you) : _from->firstName)).replace(qsl("{message}"), textRichPrepare(msg));
//    }
    return msg;
}

HistoryMessage::~HistoryMessage() {
	if (_media) {
		_media->unregItem(this);
	}
	delete _media;
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessageForwarded &msg) : HistoryMessage(history, block, msg.vid.v, (msg.vflags.v & 0x02), (msg.vflags.v & 0x01), ::date(msg.vdate), msg.vfrom_id.v, textClean(qs(msg.vmessage)), msg.vmedia)
, fwdDate(::date(msg.vfwd_date))
, fwdFrom(App::user(msg.vfwd_from_id.v))
, fwdFromName(4096)
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->m.width(lang(lng_forwarded_from)))
{
	fwdNameUpdated();
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, MsgId id, HistoryMessage *msg) : HistoryMessage(history, block, id, true, true, ::date(unixtime()), MTP::authedId(), msg->HistoryMessage::selectedText(FullItemSel), msg->getMedia())
, fwdDate(dynamic_cast<HistoryForwarded*>(msg) ? dynamic_cast<HistoryForwarded*>(msg)->dateForwarded() : msg->date)
, fwdFrom(dynamic_cast<HistoryForwarded*>(msg) ? dynamic_cast<HistoryForwarded*>(msg)->fromForwarded() : msg->from())
, fwdFromName(4096)
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->m.width(lang(lng_forwarded_from)))
{
	fwdNameUpdated();
}

QString HistoryForwarded::selectedText(uint32 selection) const {
	if (selection != FullItemSel) return HistoryMessage::selectedText(selection);
	QString result, original = HistoryMessage::selectedText(selection);
	result.reserve(lang(lng_forwarded_from).size() + fwdFrom->name.size() + 3 + original.size());
	result.append('[').append(lang(lng_forwarded_from)).append(fwdFrom->name).append(qsl("]\n")).append(original);
	return result;
}

void HistoryForwarded::fwdNameUpdated() const {
	if (_media) return;
	fwdFromName.setText(st::msgServiceNameFont, App::peerName(fwdFrom), _textNameOptions);
	int32 _namew = fromWidth + fwdFromName.maxWidth() + st::msgPadding.left() + st::msgPadding.right();
	if (_namew > _maxw) _maxw = _namew;
}

void HistoryForwarded::draw(QPainter &p, uint32 selection) const {
	if (!_media && fwdFrom->nameVersion > fwdFromVersion) {
		fwdNameUpdated();
		fwdFromVersion = fwdFrom->nameVersion;
	}
	HistoryMessage::draw(p, selection);
}

void HistoryForwarded::drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const {
	style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);
	p.setPen((_out ? st::msgOutServiceColor : st::msgInServiceColor)->p);
	p.setFont(serviceFont->f);

	int32 h1 = 0, h2 = serviceName->height, h = h1 + (h1 > h2 ? h1 : h2);

	if (trect.width() >= fromWidth) {
		p.drawText(trect.x(), trect.y() + h1 + serviceFont->ascent, lang(lng_forwarded_from));

		p.setFont(serviceName->f);
		fwdFromName.drawElided(p, trect.x() + fromWidth, trect.y() + h1, trect.width() - fromWidth);
	} else {
		p.drawText(trect.x(), trect.y() + h1 + serviceFont->ascent, serviceFont->m.elidedText(lang(lng_forwarded_from), Qt::ElideRight, trect.width()));
	}

	QRect realtrect(trect);
	realtrect.setY(trect.y() + h);
	HistoryMessage::drawMessageText(p, realtrect, selection);
}

int32 HistoryForwarded::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	HistoryMessage::resize(width, dontRecountText, parent);
	if (!_media && !dontRecountText) {
		int32 h1 = 0, h2 = st::msgServiceNameFont->height;
		_height += h1 + (h1 > h2 ? h1 : h2);
	}
	return _height;
}

bool HistoryForwarded::hasPoint(int32 x, int32 y) const {
	if (!_media) {
		int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right();
		if (width > st::msgMaxWidth) {
			if (_out) left += width - st::msgMaxWidth;
			width = st::msgMaxWidth;
		}

		if (!_out && _history->peer->chat) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return false;

		if (width >= _maxw) {
			if (_out) left += width - _maxw;
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	}
	return HistoryMessage::hasPoint(x, y);
}

void HistoryForwarded::getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	inText = false;

	if (!_media) {
		int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right();
		if (width > st::msgMaxWidth) {
			if (_out) left += width - st::msgMaxWidth;
			width = st::msgMaxWidth;
		}

		if (!_out && _history->peer->chat) { // from user left photo
			if (x >= left && x < left + st::msgPhotoSize) {
				return HistoryMessage::getState(lnk, inText, x, y);
			}
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (_out) left += width - _maxw;
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (!_out && _history->peer->chat) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getState(lnk, inText, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		int32 h1 = 0, h2 = st::msgServiceNameFont->height;
		if (y >= trect.top() + h1 && y < trect.top() + (h1 + h2)) {
			if (x >= trect.left() + fromWidth && x < trect.right() && x < trect.left() + fromWidth + fwdFromName.maxWidth()) {
				lnk = fwdFrom->lnk;
			}
			return;
		}
		y -= h1 + (h1 > h2 ? h1 : h2);
	}
	return HistoryMessage::getState(lnk, inText, x, y);
}

void HistoryForwarded::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	if (!_media) {
		int32 left = _out ? st::msgMargin.right() : st::msgMargin.left(), width = _history->width - st::msgMargin.left() - st::msgMargin.right();
		if (width > st::msgMaxWidth) {
			if (_out) left += width - st::msgMaxWidth;
			width = st::msgMaxWidth;
		}

		if (!_out && _history->peer->chat) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (_out) left += width - _maxw;
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (!_out && _history->peer->chat) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getSymbol(symbol, after, upon, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		int32 h1 = 0, h2 = st::msgServiceNameFont->height;
		y -= h1 + (h1 > h2 ? h1 : h2);
	}
	return HistoryMessage::getSymbol(symbol, after, upon, x, y);
}

QString HistoryServiceMsg::messageByAction(const MTPmessageAction &action, TextLinkPtr &second) {
	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		const MTPDmessageActionChatAddUser &d(action.c_messageActionChatAddUser());
		if (App::peerFromUser(d.vuser_id) == _from->id) {
			return lang(lng_action_user_joined);
		}
		UserData *u = App::user(App::peerFromUser(d.vuser_id));
		second = TextLinkPtr(new PeerLink(u));
		return lang(lng_action_add_user).replace(qsl("{user}"), textcmdLink(2, u->name));
	} break;

	case mtpc_messageActionChatCreate: {
		const MTPDmessageActionChatCreate &d(action.c_messageActionChatCreate());
		return lang(lng_action_created_chat).replace(qsl("{title}"), textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChatDeletePhoto: {
		return lang(lng_action_removed_photo);
	} break;

	case mtpc_messageActionChatDeleteUser: {
		const MTPDmessageActionChatDeleteUser &d(action.c_messageActionChatDeleteUser());
		if (App::peerFromUser(d.vuser_id) == _from->id) {
			return lang(lng_action_user_left);
		}
		UserData *u = App::user(App::peerFromUser(d.vuser_id));
		second = TextLinkPtr(new PeerLink(u));
		return lang(lng_action_kick_user).replace(qsl("{user}"), textcmdLink(2, u->name));
	} break;

	case mtpc_messageActionChatEditPhoto: {
		const MTPDmessageActionChatEditPhoto &d(action.c_messageActionChatEditPhoto());
		if (d.vphoto.type() == mtpc_photo) {
			_media = new HistoryPhoto(history()->peer, d.vphoto.c_photo(), st::msgServicePhotoWidth);
		}
		return lang(lng_action_changed_photo);
	} break;

	case mtpc_messageActionChatEditTitle: {
		const MTPDmessageActionChatEditTitle &d(action.c_messageActionChatEditTitle());
		return lang(lng_action_changed_title).replace(qsl("{title}"), textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionGeoChatCheckin: {
		return lang(lng_action_checked_in);
	} break;

	case mtpc_messageActionGeoChatCreate: {
		const MTPDmessageActionGeoChatCreate &d(action.c_messageActionGeoChatCreate());
		return lang(lng_action_created_chat).replace(qsl("{title}"), textClean(qs(d.vtitle)));
	} break;
	}

	return lang(lng_message_empty);
}

HistoryServiceMsg::HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDmessageService &msg) :
	HistoryItem(history, block, msg.vid.v, (msg.vflags.v & 0x02), (msg.vflags.v & 0x01), ::date(msg.vdate), msg.vfrom_id.v)
, _text(st::msgMinWidth)
, _media(0)
{

	TextLinkPtr second;
	QString text(messageByAction(msg.vaction, second));
	int32 fromPos = text.indexOf(qsl("{from}"));
	if (fromPos >= 0) {
		text = text.replace(qsl("{from}"), textcmdLink(1, _from->name));
	}
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	if (fromPos >= 0) {
		_text.setLink(1, TextLinkPtr(new PeerLink(_from)));
	}
	if (second) {
		_text.setLink(2, second);
	}
	initDimensions();
}
/*
HistoryServiceMsg::HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDgeoChatMessageService &msg) :
	HistoryItem(history, block, msg.vid.v, (msg.vfrom_id.v == MTP::authedId()), false, ::date(msg.vdate), msg.vfrom_id.v), media(0), message(st::msgMinWidth) {

	QString text(messageByAction(msg.vaction));
	text = text.replace(qsl("{from}"), _from->name);
	message.setText(st::msgServiceFont, text);
	_maxw = message.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = message.minHeight();
}
/**/
HistoryServiceMsg::HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, bool out, bool unread, HistoryMedia *media) :
	HistoryItem(history, block, msgId, out, unread, date, 0)
, _text(st::msgServiceFont, msg, _historySrvOptions, st::dlgMinWidth)
, _media(media)
{
	initDimensions();
}

void HistoryServiceMsg::initDimensions(const HistoryItem *parent) {
	_maxw = _text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = _text.minHeight();
	if (_media) _media->initDimensions();
}

QString HistoryServiceMsg::selectedText(uint32 selection) const {
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo);
}

void HistoryServiceMsg::draw(QPainter &p, uint32 selection) const {
	textstyleSet(&st::serviceTextStyle);

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
		p.save();
		p.translate(st::msgServiceMargin.left() + (width - _media->maxWidth()) / 2, st::msgServiceMargin.top() + height + st::msgServiceMargin.top());
		_media->draw(p, this, selection == FullItemSel);
		p.restore();
	}

	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));

	if (width > _maxw) {
		left += (width - _maxw) / 2;
		width = _maxw;
	}
//	QRect r(0, st::msgServiceMargin.top(), _history->width, height);
	QRect r(left, st::msgServiceMargin.top(), width, height);
	p.setBrush(st::msgServiceBG->b);
	p.setPen(Qt::NoPen);
//	p.fillRect(r, st::msgServiceBG->b);
	p.drawRoundedRect(r, st::msgServiceRadius, st::msgServiceRadius);
	if (selection == FullItemSel) {
		p.setBrush(st::msgServiceSelectBG->b);
		p.drawRoundedRect(r, st::msgServiceRadius, st::msgServiceRadius);
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceColor->p);
	p.setFont(st::msgServiceFont->f);
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, selectedFrom, selectedTo);
	textstyleRestore();
}

int32 HistoryServiceMsg::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	if (dontRecountText) return _height;

	width -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
	if (width < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) width = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;

	int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
	if (nwidth != _textWidth) {
		_textWidth = nwidth;
		_textHeight = _text.countHeight(nwidth);
	}
	if (width >= _maxw) {
		_height = _minh;
	} else {
		_height = _textHeight;
	}
	_height += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
	if (_media) {
		_height += st::msgServiceMargin.top() + _media->height();
	}
	return _height;
}

bool HistoryServiceMsg::hasPoint(int32 x, int32 y) const {
	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return false;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	return QRect(left, st::msgServiceMargin.top(), width, height).contains(x, y);
}

void HistoryServiceMsg::getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	inText = false;

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	if (trect.contains(x, y)) {
		return _text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
	}
	if (_media) {
		lnk = _media->getLink(x - st::msgServiceMargin.left() - (width - _media->maxWidth()) / 2, y - st::msgServiceMargin.top() - height - st::msgServiceMargin.top(), this);
	}
}

void HistoryServiceMsg::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	return _text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
}

void HistoryServiceMsg::drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dlgHistFont, _text.original(0, 0xFFFF), _textDlgOptions);
	}
	QRect tr(r);
	p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
	cache.drawElided(p, tr.left(), tr.top(), tr.width(), tr.height() / st::dlgHistFont->height);
}

QString HistoryServiceMsg::notificationText() const {
    QString msg = _text.original(0, 0xFFFF);
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("..");
    return msg;
}

HistoryMedia *HistoryServiceMsg::getMedia(bool inOverview) const {
	return inOverview ? 0 : _media;
}

HistoryServiceMsg::~HistoryServiceMsg() {
	delete _media;
}

HistoryDateMsg::HistoryDateMsg(History *history, HistoryBlock *block, const QDate &date) : HistoryServiceMsg(history, block, clientMsgId(), QDateTime(date), langDayOfMonth(date)) {
}

HistoryItem *createDayServiceMsg(History *history, HistoryBlock *block, QDateTime date) {
	return regItem(new HistoryDateMsg(history, block, date.date()));
}

HistoryUnreadBar::HistoryUnreadBar(History *history, HistoryBlock *block, int32 count, const QDateTime &date) : HistoryItem(history, block, clientMsgId(), false, false, date, 0), freezed(false) {
	setCount(count);
	initDimensions();
}

void HistoryUnreadBar::initDimensions(const HistoryItem *parent) {
	_maxw = st::msgPadding.left() + st::msgPadding.right() + 1;
	_minh = st::unreadBarHeight;
}

void HistoryUnreadBar::setCount(int32 count) {
	if (!count) freezed = true;
	if (freezed) return;
	text = lang(lng_unread_bar).arg(count);
}

void HistoryUnreadBar::draw(QPainter &p, uint32 selection) const {
	p.fillRect(0, st::lineWidth, _history->width, st::unreadBarHeight - 2 * st::lineWidth, st::unreadBarBG->b);
	p.fillRect(0, st::unreadBarHeight - st::lineWidth, _history->width, st::lineWidth, st::unreadBarBorder->b);
	p.setFont(st::unreadBarFont->f);
	p.setPen(st::unreadBarColor->p);
	p.drawText(QRect(0, 0, _history->width, st::unreadBarHeight - st::lineWidth), text, style::al_center);
}

int32 HistoryUnreadBar::resize(int32 width, bool dontRecountText, const HistoryItem *parent) {
	_height = st::unreadBarHeight;
	return _height;
}

void HistoryUnreadBar::drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
}

QString HistoryUnreadBar::notificationText() const {
    return QString();
}

