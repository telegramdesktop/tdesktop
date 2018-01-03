/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document.h"

#include "lang/lang_keys.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "mainwidget.h"
#include "core/file_utilities.h"
#include "media/media_audio.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "history/history_media_types.h"
#include "auth_session.h"
#include "messenger.h"

namespace {

QString JoinStringList(const QStringList &list, const QString &separator) {
	const auto count = list.size();
	if (!count) {
		return QString();
	}

	auto result = QString();
	auto fullsize = separator.size() * (count - 1);
	for (const auto &string : list) {
		fullsize += string.size();
	}
	result.reserve(fullsize);
	result.append(list[0]);
	for (auto i = 1; i != count; ++i) {
		result.append(separator).append(list[i]);
	}
	return result;
}

} // namespace

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

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir) {
#ifdef Q_OS_WIN
	name = name.replace(QRegularExpression(qsl("[\\\\\\/\\:\\*\\?\\\"\\<\\>\\|]")), qsl("_"));
#elif defined Q_OS_MAC
	name = name.replace(QRegularExpression(qsl("[\\:]")), qsl("_"));
#elif defined Q_OS_LINUX
	name = name.replace(QRegularExpression(qsl("[\\/]")), qsl("_"));
#endif
	if (Global::AskDownloadPath() || savingAs) {
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
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3, oldpos - start - 3) + first.mid(oldend - 1) + sep + JoinStringList(filters.mid(1), sep);
							} else {
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3) + sep + JoinStringList(filters.mid(1), sep);
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
	if (Global::DownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = Global::DownloadPath();
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
		auto it = Auth().data().stickerSets().constFind(set.c_inputStickerSetID().vid.v);
		return (it != Auth().data().stickerSets().cend()) && !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
	} break;
	case mtpc_inputStickerSetShortName: {
		auto name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (auto it = Auth().data().stickerSets().cbegin(), e = Auth().data().stickerSets().cend(); it != e; ++it) {
			if (it->shortName.toLower() == name) {
				return !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
			}
		}
	} break;
	}
	return false;
}

QString documentSaveFilename(const DocumentData *data, bool forceSavingAs = false, const QString already = QString(), const QDir &dir = QDir()) {
	auto alreadySavingFilename = data->loadingFilePath();
	if (!alreadySavingFilename.isEmpty()) {
		return alreadySavingFilename;
	}

	QString name, filter, caption, prefix;
	MimeType mimeType = mimeTypeForName(data->mimeString());
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->isVoiceMessage()) {
		auto mp3 = data->hasMimeType(qstr("audio/mp3"));
		name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : already;
		filter = mp3 ? qsl("MP3 Audio (*.mp3);;") : qsl("OGG Opus Audio (*.ogg);;");
		filter += FileDialog::AllFilesFilter();
		caption = lang(lng_save_audio);
		prefix = qsl("audio");
	} else if (data->isVideoFile()) {
		name = already.isEmpty() ? data->filename() : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".mov") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = qsl("MOV Video (*.mov);;") + FileDialog::AllFilesFilter();
		} else {
			filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
		}
		caption = lang(lng_save_video);
		prefix = qsl("video");
	} else {
		name = already.isEmpty() ? data->filename() : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = QString();
		} else {
			filter = mimeType.filterString() + qsl(";;") + FileDialog::AllFilesFilter();
		}
		caption = lang(data->isAudioFile() ? lng_save_audio_file : lng_save_file);
		prefix = qsl("doc");
	}

	return saveFileName(caption, filter, prefix, name, forceSavingAs, dir);
}

void DocumentOpenClickHandler::doOpen(
		not_null<DocumentData*> data,
		HistoryItem *context,
		ActionOnLoad action) {
	if (!data->date) return;

	auto msgId = context ? context->fullId() : FullMsgId();
	bool playVoice = data->isVoiceMessage();
	bool playMusic = data->isAudioFile();
	bool playVideo = data->isVideoFile();
	bool playAnimation = data->isAnimation();
	auto &location = data->location(true);
	if (data->isTheme()) {
		if (!location.isEmpty() && location.accessEnable()) {
			Messenger::Instance().showDocument(data, context);
			location.accessDisable();
			return;
		}
	}
	if (!location.isEmpty() || (!data->data().isEmpty() && (playVoice || playMusic || playVideo || playAnimation))) {
		using State = Media::Player::State;
		if (playVoice) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(data, msgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else {
				auto audio = AudioMsgId(data, msgId);
				Media::Player::mixer()->play(audio);
				Media::Player::Updated().notify(audio);
				if (App::main()) {
					App::main()->mediaMarkRead(data);
				}
			}
		} else if (playMusic) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(data, msgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else {
				auto song = AudioMsgId(data, msgId);
				Media::Player::mixer()->play(song);
				Media::Player::Updated().notify(song);
			}
		} else if (playVideo) {
			if (!data->data().isEmpty()) {
				Messenger::Instance().showDocument(data, context);
			} else if (location.accessEnable()) {
				Messenger::Instance().showDocument(data, context);
				location.accessDisable();
			} else {
				auto filepath = location.name();
				if (documentIsValidMediaFile(filepath)) {
					File::Launch(filepath);
				}
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->isVoiceMessage() || data->isAudioFile() || data->isVideoFile()) {
			auto filepath = location.name();
			if (documentIsValidMediaFile(filepath)) {
				File::Launch(filepath);
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->size < App::kImageSizeLimit) {
			if (!data->data().isEmpty() && playAnimation) {
				if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
					context->getMedia()->playInline();
				} else {
					Messenger::Instance().showDocument(data, context);
				}
			} else if (location.accessEnable()) {
				if (data->isAnimation() || QImageReader(location.name()).canRead()) {
					if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
						context->getMedia()->playInline();
					} else {
						Messenger::Instance().showDocument(data, context);
					}
				} else {
					File::Launch(location.name());
				}
				location.accessDisable();
			} else {
				File::Launch(location.name());
			}
		} else {
			File::Launch(location.name());
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
	const auto data = document();
	const auto action = data->isVoiceMessage()
		? ActionOnLoadNone
		: ActionOnLoadOpen;
	doOpen(data, getActionItem(), action);
}

void GifOpenClickHandler::onClickImpl() const {
	doOpen(document(), getActionItem(), ActionOnLoadPlayInline);
}

void DocumentSaveClickHandler::doSave(
		not_null<DocumentData*> data,
		bool forceSavingAs) {
	if (!data->date) return;

	auto filepath = data->filepath(DocumentData::FilePathResolveSaveFromDataSilent, forceSavingAs);
	if (!filepath.isEmpty() && !forceSavingAs) {
		File::OpenWith(filepath, QCursor::pos());
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filedir = filepath.isEmpty() ? QDir() : fileinfo.dir();
		auto filename = filepath.isEmpty() ? QString() : fileinfo.fileName();
		auto newfname = documentSaveFilename(data, forceSavingAs, filename, filedir);
		if (!newfname.isEmpty()) {
			auto action = (filename.isEmpty() || forceSavingAs) ? ActionOnLoadNone : ActionOnLoadOpenWith;
			auto actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(newfname, action, actionMsgId);
		}
	}
}

void DocumentSaveClickHandler::onClickImpl() const {
	doSave(document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	const auto data = document();
	if (!data->date) return;

	if (data->uploading()) {
		if (const auto item = App::histItemById(context())) {
			App::contextItem(item);
			App::main()->cancelUploadLayer();
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

DocumentData::DocumentData(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QString &url, const QVector<MTPDocumentAttribute> &attributes)
: id(id)
, _dc(dc)
, _access(accessHash)
, _version(version)
, _url(url) {
	setattributes(attributes);
	if (_dc && _access) {
		_location = Local::readFileLocation(mediaKey());
	}
}

DocumentData *DocumentData::create(DocumentId id) {
	return new DocumentData(id, 0, 0, 0, QString(), QVector<MTPDocumentAttribute>());
}

DocumentData *DocumentData::create(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, dc, accessHash, version, QString(), attributes);
}

DocumentData *DocumentData::create(DocumentId id, const QString &url, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, 0, 0, 0, url, attributes);
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			auto &d = attributes[i].c_documentAttributeImageSize();
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument || type == VideoDocument) {
			type = AnimatedDocument;
			_additional = nullptr;
		} break;
		case mtpc_documentAttributeSticker: {
			auto &d = attributes[i].c_documentAttributeSticker();
			if (type == FileDocument) {
				type = StickerDocument;
				_additional = std::make_unique<StickerData>();
			}
			if (sticker()) {
				sticker()->alt = qs(d.valt);
				if (sticker()->set.type() != mtpc_inputStickerSetID || d.vstickerset.type() == mtpc_inputStickerSetID) {
					sticker()->set = d.vstickerset;
				}
			}
		} break;
		case mtpc_documentAttributeVideo: {
			auto &d = attributes[i].c_documentAttributeVideo();
			if (type == FileDocument) {
				type = d.is_round_message() ? RoundVideoDocument : VideoDocument;
			}
			_duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			auto &d = attributes[i].c_documentAttributeAudio();
			if (type == FileDocument) {
				if (d.is_voice()) {
					type = VoiceDocument;
					_additional = std::make_unique<VoiceData>();
				} else {
					type = SongDocument;
					_additional = std::make_unique<SongData>();
				}
			}
			if (const auto voiceData = voice()) {
				voiceData->duration = d.vduration.v;
				VoiceWaveform waveform = documentWaveformDecode(qba(d.vwaveform));
				uchar wavemax = 0;
				for (int32 i = 0, l = waveform.size(); i < l; ++i) {
					uchar waveat = waveform.at(i);
					if (wavemax < waveat) wavemax = waveat;
				}
				voiceData->waveform = waveform;
				voiceData->wavemax = wavemax;
			} else if (const auto songData = song()) {
				songData->duration = d.vduration.v;
				songData->title = qs(d.vtitle);
				songData->performer = qs(d.vperformer);
			}
		} break;
		case mtpc_documentAttributeFilename: {
			auto &attribute = attributes[i];
			auto remoteFileName = qs(
				attribute.c_documentAttributeFilename().vfile_name);

			// We don't want RTL Override characters in filenames,
			// because they introduce a security issue, when a filename
			// "Fil[RTLO]gepj.exe" looks like "Filexe.jpeg" being ".exe"
			auto rtlOverride = QChar(0x202E);
			_filename = std::move(remoteFileName).replace(rtlOverride, "");
		} break;
		}
	}
	if (type == StickerDocument) {
		if (dimensions.width() <= 0
			|| dimensions.height() <= 0
			|| dimensions.width() > StickerMaxSize
			|| dimensions.height() > StickerMaxSize
			|| !saveToCache()) {
			type = FileDocument;
			_additional = nullptr;
		}
	}
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument && size < Storage::kMaxStickerInMemory)
		|| (isAnimation() && size < Storage::kMaxAnimationInMemory)
		|| (isVoiceMessage() && size < Storage::kMaxVoiceInMemory);
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
		} else if (isVoiceMessage()) {
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
	if (loaded() || status != FileReady || (!isAnimation() && !isVoiceMessage()) || !saveToCache() || _loader != CancelledMtpFileLoader) {
		return;
	}
	_loader = nullptr;
}

void DocumentData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	auto loc = location(true);
	auto already = loc.name();
	auto item = _actionOnLoadMsgId.msg ? App::histItemById(_actionOnLoadMsgId) : nullptr;
	auto showImage = !isVideoFile() && (size < App::kImageSizeLimit);
	auto playVoice = isVoiceMessage() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	auto playMusic = isAudioFile() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	auto playAnimation = isAnimation() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && showImage && item && item->getMedia();
	if (auto applyTheme = isTheme()) {
		if (!loc.isEmpty() && loc.accessEnable()) {
			Messenger::Instance().showDocument(this, item);
			loc.accessDisable();
			return;
		}
	}
	using State = Media::Player::State;
	if (playVoice) {
		if (loaded()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(this, _actionOnLoadMsgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else if (Media::Player::IsStopped(state.state)) {
				Media::Player::mixer()->play(AudioMsgId(this, _actionOnLoadMsgId));
				if (App::main()) App::main()->mediaMarkRead(this);
			}
		}
	} else if (playMusic) {
		if (loaded()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(this, _actionOnLoadMsgId) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (Media::Player::IsPaused(state.state) || state.state == State::Pausing) {
					Media::Player::mixer()->resume(state.id);
				} else {
					Media::Player::mixer()->pause(state.id);
				}
			} else if (Media::Player::IsStopped(state.state)) {
				auto song = AudioMsgId(this, _actionOnLoadMsgId);
				Media::Player::mixer()->play(song);
				Media::Player::Updated().notify(song);
			}
		}
	} else if (playAnimation) {
		if (loaded()) {
			if (_actionOnLoad == ActionOnLoadPlayInline && item->getMedia()) {
				item->getMedia()->playInline();
			} else {
				Messenger::Instance().showDocument(this, item);
			}
		}
	} else {
		if (already.isEmpty()) return;

		if (_actionOnLoad == ActionOnLoadOpenWith) {
			File::OpenWith(already, QCursor::pos());
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			if (isVoiceMessage() || isAudioFile() || isVideoFile()) {
				if (documentIsValidMediaFile(already)) {
					File::Launch(already);
				}
				if (App::main()) App::main()->mediaMarkRead(this);
			} else if (loc.accessEnable()) {
				if (showImage && QImageReader(loc.name()).canRead()) {
					if (_actionOnLoad == ActionOnLoadPlayInline && item && item->getMedia()) {
						item->getMedia()->playInline();
					} else {
						Messenger::Instance().showDocument(this, item);
					}
				} else {
					File::Launch(already);
				}
				loc.accessDisable();
			} else {
				File::Launch(already);
			}
		}
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool DocumentData::loaded(FilePathResolveType type) const {
	if (loading() && _loader->finished()) {
		if (_loader->cancelled()) {
			destroyLoaderDelayed(CancelledMtpFileLoader);
		} else {
			auto that = const_cast<DocumentData*>(this);
			that->_location = FileLocation(_loader->fileName());
			that->_data = _loader->bytes();
			if (that->sticker() && !_loader->imagePixmap().isNull()) {
				that->sticker()->img = ImagePtr(_data, _loader->imageFormat(), _loader->imagePixmap());
			}
			destroyLoaderDelayed();
		}
		notifyLayoutChanged();
	}
	return !data().isEmpty() || !filepath(type).isEmpty();
}

void DocumentData::destroyLoaderDelayed(mtpFileLoader *newValue) const {
	_loader->stop();
	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, newValue));
	Auth().downloader().delayedDestroyLoader(std::move(loader));
}

bool DocumentData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

QString DocumentData::loadingFilePath() const {
	return loading() ? _loader->fileName() : QString();
}

bool DocumentData::displayLoading() const {
	return loading()
		? (!_loader->loadingLocal() || !_loader->autoLoading())
		: (uploading() && !waitingForAlbum());
}

float64 DocumentData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ uploadingData->size;
			return snap(result, 0., 1.);
		}
		return 0.;
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1. : 0.);
}

int32 DocumentData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return (uploadingData != nullptr);
}

void DocumentData::setWaitingForAlbum() {
	if (uploading()) {
		uploadingData->waitingForAlbum = true;
	}
}

bool DocumentData::waitingForAlbum() const {
	return uploading() && uploadingData->waitingForAlbum;
}

void DocumentData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded(FilePathResolveChecked)) {
		auto &l = location(true);
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
				f.close();

				setLocation(FileLocation(toFile));
				Local::writeFileLocation(mediaKey(), FileLocation(toFile));
			} else if (l.accessEnable()) {
				auto alreadyName = l.name();
				if (alreadyName != toFile) {
					QFile(toFile).remove();
					QFile(alreadyName).copy(toFile);
				}
				l.accessDisable();
			}
		}
		_actionOnLoad = action;
		_actionOnLoadMsgId = actionMsgId;
		performActionOnLoad();
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = nullptr;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel(); // changes _actionOnLoad
			_loader = nullptr;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;
	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		if (!_access && !_url.isEmpty()) {
			_loader = new webFileLoader(_url, toFile, fromCloud, autoLoading);
		} else {
			_loader = new mtpFileLoader(_dc, id, _access, _version, locationType(), toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		}
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(documentLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(documentLoadFailed(FileLoader*,bool)));
		_loader->start();
	}
	notifyLayoutChanged();
}

void DocumentData::cancel() {
	if (!loading()) return;

	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, CancelledMtpFileLoader));
	loader->cancel();
	loader->stop();
	Auth().downloader().delayedDestroyLoader(std::move(loader));

	notifyLayoutChanged();
	if (auto main = App::main()) {
		main->documentLoadProgress(this);
	}

	_actionOnLoad = ActionOnLoadNone;
}

void DocumentData::notifyLayoutChanged() const {
	auto &items = App::documentItems();
	for (auto item : items.value(const_cast<DocumentData*>(this))) {
		Auth().data().markItemLayoutChanged(item);
	}

	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(const_cast<DocumentData*>(this))) {
			item->layoutChanged();
		}
	}
}

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit) {
	auto bitsCount = static_cast<int>(encoded5bit.size() * 8);
	auto valuesCount = bitsCount / 5;
	if (!valuesCount) {
		return VoiceWaveform();
	}

	// Read each 5 bit of encoded5bit as 0-31 unsigned char.
	// We count the index of the byte in which the desired 5-bit sequence starts.
	// And then we read a uint16 starting from that byte to guarantee to get all of those 5 bits.
	//
	// BUT! if it is the last byte we have, we're not allowed to read a uint16 starting with it.
	// Because it will be an overflow (we'll access one byte after the available memory).
	// We see, that only the last 5 bits could start in the last available byte and be problematic.
	// So we read in a general way all the entries in a general way except the last one.
	auto result = VoiceWaveform(valuesCount, 0);
	auto bitsData = encoded5bit.constData();
	for (auto i = 0, l = valuesCount - 1; i != l; ++i) {
		auto byteIndex = (i * 5) / 8;
		auto bitShift = (i * 5) % 8;
		auto value = *reinterpret_cast<const uint16*>(bitsData + byteIndex);
		result[i] = static_cast<char>((value >> bitShift) & 0x1F);
	}
	auto lastByteIndex = ((valuesCount - 1) * 5) / 8;
	auto lastBitShift = ((valuesCount - 1) * 5) % 8;
	auto lastValue = (lastByteIndex == encoded5bit.size() - 1)
		? static_cast<uint16>(*reinterpret_cast<const uchar*>(bitsData + lastByteIndex))
		: *reinterpret_cast<const uint16*>(bitsData + lastByteIndex);
	result[valuesCount - 1] = static_cast<char>((lastValue >> lastBitShift) & 0x1F);

	return result;
}

QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform) {
	auto bitsCount = waveform.size() * 5;
	auto bytesCount = (bitsCount + 7) / 8;
	auto result = QByteArray(bytesCount + 1, 0);
	auto bitsData = result.data();

	// Write each 0-31 unsigned char as 5 bit to result.
	// We reserve one extra byte to be able to dereference any of required bytes
	// as a uint16 without overflowing, even the byte with index "bytesCount - 1".
	for (auto i = 0, l = waveform.size(); i < l; ++i) {
		auto byteIndex = (i * 5) / 8;
		auto bitShift = (i * 5) % 8;
		auto value = (static_cast<uint16>(waveform[i]) & 0x1F) << bitShift;
		*reinterpret_cast<uint16*>(bitsData + byteIndex) |= value;
	}
	result.resize(bytesCount);
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
		} else if (type == FilePathResolveSaveFromDataSilent && (Global::AskDownloadPath() || forceSavingAs)) {
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
					const_cast<DocumentData*>(this)->_location = FileLocation(filename);
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
			auto thumbSize = (w > h) ? QSize(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : QSize(st::msgReplyBarSize.height(), h * st::msgReplyBarSize.height() / w);
			thumbSize *= cIntRetinaFactor();
			auto options = Images::Option::Smooth | (isVideoMessage() ? Images::Option::Circled : Images::Option::None) | Images::Option::TransparentBackground;
			auto outerSize = st::msgReplyBarSize.height();
			auto image = thumb->pixNoCache(thumbSize.width(), thumbSize.height(), options, outerSize, outerSize);
			replyPreview = ImagePtr(image, "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

bool DocumentData::isVoiceMessage() const {
	return (type == VoiceDocument);
}

bool DocumentData::isVideoMessage() const {
	return (type == RoundVideoDocument);
}

bool DocumentData::isAnimation() const {
	return (type == AnimatedDocument)
		|| isVideoMessage()
		|| hasMimeType(qstr("image/gif"));
}

bool DocumentData::isGifv() const {
	return (type == AnimatedDocument)
		&& hasMimeType(qstr("video/mp4"));
}

bool DocumentData::isTheme() const {
	return
		_filename.endsWith(
			qstr(".tdesktop-theme"),
			Qt::CaseInsensitive)
		|| _filename.endsWith(
			qstr(".tdesktop-palette"),
			Qt::CaseInsensitive);
}

bool DocumentData::isSong() const {
	return (type == SongDocument);
}

bool DocumentData::isAudioFile() const {
	if (isVoiceMessage()) {
		return false;
	} else if (isSong()) {
		return true;
	}
	return _mimeString.startsWith(qstr("audio/"), Qt::CaseInsensitive);
}

bool DocumentData::isSharedMediaMusic() const {
	if (const auto songData = song()) {
		return (songData->duration > 0);
	}
	return false;
}

bool DocumentData::isVideoFile() const {
	return (type == VideoDocument);
}

int32 DocumentData::duration() const {
	return (isAnimation() || isVideoFile()) ? _duration : -1;
}

bool DocumentData::isImage() const {
	return !isAnimation() && !isVideoFile() && (_duration > 0);
}

void DocumentData::recountIsImage() {
	if (isAnimation() || isVideoFile()) {
		return;
	}
	_duration = fileIsImage(filename(), mimeString()) ? 1 : -1; // hack
}

bool DocumentData::setRemoteVersion(int32 version) {
	if (_version == version) {
		return false;
	}
	_version = version;
	_location = FileLocation();
	_data = QByteArray();
	status = FileReady;
	if (loading()) {
		destroyLoaderDelayed();
	}
	return true;
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
		if (isVoiceMessage()) {
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
		destroyLoaderDelayed();
	}
}

QString DocumentData::ComposeNameString(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer) {
	if (songTitle.isEmpty() && songPerformer.isEmpty()) {
		return filename.isEmpty() ? qsl("Unknown File") : filename;
	}

	if (songPerformer.isEmpty()) {
		return songTitle;
	}

	auto trackTitle = (songTitle.isEmpty() ? qsl("Unknown Track") : songTitle);
	return songPerformer + QString::fromUtf8(" \xe2\x80\x93 ") + trackTitle;
}
