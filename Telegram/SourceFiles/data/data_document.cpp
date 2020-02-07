/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document.h"

#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_document_good_thumbnail.h"
#include "lang/lang_keys.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "mainwidget.h"
#include "core/file_utilities.h"
#include "core/media_active_cache.h"
#include "core/mime_type.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_loader_mtproto.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "storage/localstorage.h"
#include "storage/streamed_file_downloader.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_download_web.h"
#include "platform/platform_specific.h"
#include "history/history.h"
#include "history/history_item.h"
#include "window/window_session_controller.h"
#include "storage/cache/storage_cache_database.h"
#include "boxes/confirm_box.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "ui/text/text_utilities.h"
#include "base/base_file_utilities.h"
#include "mainwindow.h"
#include "core/application.h"
#include "lottie/lottie_animation.h"
#include "facades.h"
#include "app.h"

namespace {

// Updated Mar 3, 2020: Increase the size of the memory cache for media, to prevent items still being displayed from being unloaded.
constexpr auto kMemoryForCache = 128 * 1024 * 1024; // was 32, updated to 128
const auto kAnimatedStickerDimensions = QSize(512, 512);

using FilePathResolve = DocumentData::FilePathResolve;

Core::MediaActiveCache<DocumentData> &ActiveCache() {
	static auto Instance = Core::MediaActiveCache<DocumentData>(
		kMemoryForCache,
		[](DocumentData *document) { document->unload(); });
	return Instance;
}

int64 ComputeUsage(StickerData *sticker) {
	return (sticker != nullptr && sticker->image != nullptr)
		? sticker->image->width() * sticker->image->height() * 4
		: 0;
}

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

void LaunchWithWarning(const QString &name, HistoryItem *item) {
	const auto warn = [&] {
		if (!Data::IsExecutableName(name)) {
			return false;
		} else if (!Auth().settings().exeLaunchWarning()) {
			return false;
		} else if (item && item->history()->peer->isVerified()) {
			return false;
		}
		return true;
	}();
	if (!warn) {
		File::Launch(name);
		return;
	}
	const auto extension = '.' + Data::FileExtension(name);
	const auto callback = [=](bool checked) {
		if (checked) {
			Auth().settings().setExeLaunchWarning(false);
			Auth().saveSettingsDelayed();
		}
		File::Launch(name);
	};
	Ui::show(Box<ConfirmDontWarnBox>(
		tr::lng_launch_exe_warning(
			lt_extension,
			rpl::single(Ui::Text::Bold(extension)),
			Ui::Text::WithEntities),
		tr::lng_launch_exe_dont_ask(tr::now),
		tr::lng_launch_exe_sure(),
		callback));
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

QString FileNameUnsafe(
		const QString &title,
		const QString &filter,
		const QString &prefix,
		QString name,
		bool savingAs,
		const QDir &dir) {
	name = base::FileNameFromUserString(name);
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
					const auto &first = filters.at(0);
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
		path = File::DefaultDownloadPath();
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

QString FileNameForSave(
		const QString &title,
		const QString &filter,
		const QString &prefix,
		QString name,
		bool savingAs,
		const QDir &dir) {
	const auto result = FileNameUnsafe(
		title,
		filter,
		prefix,
		name,
		savingAs,
		dir);
#ifdef Q_OS_WIN
	const auto lower = result.trimmed().toLower();
	const auto kBadExtensions = { qstr(".lnk"), qstr(".scf") };
	const auto kMaskExtension = qsl(".download");
	for (const auto extension : kBadExtensions) {
		if (lower.endsWith(extension)) {
			return result + kMaskExtension;
		}
	}
#endif // Q_OS_WIN
	return result;
}

QString documentSaveFilename(const DocumentData *data, bool forceSavingAs = false, const QString already = QString(), const QDir &dir = QDir()) {
	auto alreadySavingFilename = data->loadingFilePath();
	if (!alreadySavingFilename.isEmpty()) {
		return alreadySavingFilename;
	}

	QString name, filter, caption, prefix;
	const auto mimeType = Core::MimeTypeForName(data->mimeString());
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->isVoiceMessage()) {
		auto mp3 = data->hasMimeType(qstr("audio/mp3"));
		name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : already;
		filter = mp3 ? qsl("MP3 Audio (*.mp3);;") : qsl("OGG Opus Audio (*.ogg);;");
		filter += FileDialog::AllFilesFilter();
		caption = tr::lng_save_audio(tr::now);
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
		caption = tr::lng_save_video(tr::now);
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
		caption = data->isAudioFile()
			? tr::lng_save_audio_file(tr::now)
			: tr::lng_save_file(tr::now);
		prefix = qsl("doc");
	}

	return FileNameForSave(caption, filter, prefix, name, forceSavingAs, dir);
}

void DocumentOpenClickHandler::Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> data,
		HistoryItem *context) {
	if (!data->date) {
		return;
	}

	const auto openFile = [&] {
		const auto &location = data->location(true);
		if (data->size < App::kImageSizeLimit && location.accessEnable()) {
			const auto guard = gsl::finally([&] {
				location.accessDisable();
			});
			const auto path = location.name();
			if (QImageReader(path).canRead()) {
				Core::App().showDocument(data, context);
				return;
			}
		}
		LaunchWithWarning(location.name(), context);
	};
	const auto &location = data->location(true);
	if (data->isTheme() && data->loaded(DocumentData::FilePathResolve::Checked)) {
		Core::App().showDocument(data, context);
		location.accessDisable();
	} else if (data->canBePlayed()) {
		if (data->isAudioFile()
			|| data->isVoiceMessage()
			|| data->isVideoMessage()) {
			const auto msgId = context ? context->fullId() : FullMsgId();
			Media::Player::instance()->playPause({ data, msgId });
		} else if (context && data->isAnimation()) {
			data->owner().requestAnimationPlayInline(context);
		} else {
			Core::App().showDocument(data, context);
		}
	} else if (!location.isEmpty()
		|| (data->loaded()
			&& !data->filepath(
				FilePathResolve::SaveFromDataSilent).isEmpty())) {
		openFile();
	} else if (data->status == FileReady
		|| data->status == FileDownloadFailed) {
		DocumentSaveClickHandler::Save(origin, data);
	}
}

void DocumentOpenClickHandler::onClickImpl() const {
	Open(context(), document(), getActionItem());
}

void DocumentSaveClickHandler::Save(
		Data::FileOrigin origin,
		not_null<DocumentData*> data,
		Mode mode) {
	if (!data->date) {
		return;
	}

	auto savename = QString();
	if (mode != Mode::ToCacheOrFile || !data->saveToCache()) {
		const auto filepath = data->filepath(FilePathResolve::Checked);
		if (mode != Mode::ToNewFile
			&& (!filepath.isEmpty()
				|| !data->filepath(
					FilePathResolve::SaveFromData).isEmpty())) {
			return;
		}
		const auto fileinfo = QFileInfo(filepath);
		const auto filedir = filepath.isEmpty()
			? QDir()
			: fileinfo.dir();
		const auto filename = filepath.isEmpty()
			? QString()
			: fileinfo.fileName();
		savename = documentSaveFilename(
			data,
			(mode == Mode::ToNewFile),
			filename,
			filedir);
		if (savename.isEmpty()) {
			return;
		}
	}
	data->save(origin, savename);
}

void DocumentSaveClickHandler::onClickImpl() const {
	Save(context(), document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	const auto data = document();
	if (!data->date) return;

	if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			App::main()->cancelUploadLayer(item);
		}
	} else {
		data->cancel();
	}
}

void DocumentOpenWithClickHandler::Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> data) {
	if (!data->date) {
		return;
	}

	if (data->loaded()) {
		const auto path = data->filepath(
			FilePathResolve::SaveFromDataSilent);
		if (!path.isEmpty()) {
			File::OpenWith(path, QCursor::pos());
			return;
		}
	}
	DocumentSaveClickHandler::Save(
		origin,
		data,
		DocumentSaveClickHandler::Mode::ToFile);
}

void DocumentOpenWithClickHandler::onClickImpl() const {
	Open(context(), document());
}

Data::FileOrigin StickerData::setOrigin() const {
	return set.match([&](const MTPDinputStickerSetID &data) {
		return Data::FileOrigin(
			Data::FileOriginStickerSet(data.vid().v, data.vaccess_hash().v));
	}, [&](const auto &) {
		return Data::FileOrigin();
	});
}

VoiceData::~VoiceData() {
	if (!waveform.isEmpty()
		&& waveform[0] == -1
		&& waveform.size() > int32(sizeof(TaskId))) {
		auto taskId = TaskId();
		memcpy(&taskId, waveform.constData() + 1, sizeof(taskId));
		Local::cancelTask(taskId);
	}
}

DocumentData::DocumentData(not_null<Data::Session*> owner, DocumentId id)
: id(id)
, _owner(owner) {
}

Data::Session &DocumentData::owner() const {
	return *_owner;
}

Main::Session &DocumentData::session() const {
	return _owner->session();
}

void DocumentData::setattributes(
		const QVector<MTPDocumentAttribute> &attributes) {
	_flags &= ~(Flag::ImageType | kStreamingSupportedMask);
	_flags |= kStreamingSupportedUnknown;

	validateLottieSticker();

	for (const auto &attribute : attributes) {
		attribute.match([&](const MTPDdocumentAttributeImageSize &data) {
			dimensions = QSize(data.vw().v, data.vh().v);
		}, [&](const MTPDdocumentAttributeAnimated &data) {
			if (type == FileDocument
				|| type == StickerDocument
				|| type == VideoDocument) {
				type = AnimatedDocument;
				_additional = nullptr;
			}
		}, [&](const MTPDdocumentAttributeSticker &data) {
			if (type == FileDocument) {
				type = StickerDocument;
				_additional = std::make_unique<StickerData>();
			}
			if (sticker()) {
				sticker()->alt = qs(data.valt());
				if (sticker()->set.type() != mtpc_inputStickerSetID
					|| data.vstickerset().type() == mtpc_inputStickerSetID) {
					sticker()->set = data.vstickerset();
				}
			}
		}, [&](const MTPDdocumentAttributeVideo &data) {
			if (type == FileDocument) {
				type = data.is_round_message()
					? RoundVideoDocument
					: VideoDocument;
			}
			_duration = data.vduration().v;
			setMaybeSupportsStreaming(data.is_supports_streaming());
			dimensions = QSize(data.vw().v, data.vh().v);
		}, [&](const MTPDdocumentAttributeAudio &data) {
			if (type == FileDocument) {
				if (data.is_voice()) {
					type = VoiceDocument;
					_additional = std::make_unique<VoiceData>();
				} else {
					type = SongDocument;
					_additional = std::make_unique<SongData>();
				}
			}
			if (const auto voiceData = voice()) {
				voiceData->duration = data.vduration().v;
				voiceData->waveform = documentWaveformDecode(
					data.vwaveform().value_or_empty());
				voiceData->wavemax = voiceData->waveform.empty()
					? uchar(0)
					: *ranges::max_element(voiceData->waveform);
			} else if (const auto songData = song()) {
				songData->duration = data.vduration().v;
				songData->title = qs(data.vtitle().value_or_empty());
				songData->performer = qs(data.vperformer().value_or_empty());
			}
		}, [&](const MTPDdocumentAttributeFilename &data) {
			_filename = qs(data.vfile_name());

			// We don't want LTR/RTL mark/embedding/override/isolate chars
			// in filenames, because they introduce a security issue, when
			// an executable "Fil[x]gepj.exe" may look like "Filexe.jpeg".
			QChar controls[] = {
				0x200E, // LTR Mark
				0x200F, // RTL Mark
				0x202A, // LTR Embedding
				0x202B, // RTL Embedding
				0x202D, // LTR Override
				0x202E, // RTL Override
				0x2066, // LTR Isolate
				0x2067, // RTL Isolate
			};
			for (const auto ch : controls) {
				_filename = std::move(_filename).replace(ch, "_");
			}
		}, [&](const MTPDdocumentAttributeHasStickers &data) {
		});
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
	validateGoodThumbnail();
	if (isAudioFile() || isAnimation() || isVoiceMessage()) {
		setMaybeSupportsStreaming(true);
	}
}

void DocumentData::validateLottieSticker() {
	if (type == FileDocument
		&& _mimeString == qstr("application/x-tgsticker")
		&& _thumbnail) {
		type = StickerDocument;
		_additional = std::make_unique<StickerData>();
		sticker()->animated = true;
		dimensions = kAnimatedStickerDimensions;
	}
}

void DocumentData::setDataAndCache(const QByteArray &data) {
	setData(data);
	if (saveToCache() && data.size() <= Storage::kMaxFileInMemory) {
		owner().cache().put(
			cacheKey(),
			Storage::Cache::Database::TaggedValue(
				base::duplicate(data),
				cacheTag()));
	}
}

bool DocumentData::checkWallPaperProperties() {
	if (type == WallPaperDocument) {
		return true;
	}
	if (type != FileDocument
		|| !_thumbnail
		|| !dimensions.width()
		|| !dimensions.height()
		|| dimensions.width() > Storage::kMaxWallPaperDimension
		|| dimensions.height() > Storage::kMaxWallPaperDimension
		|| size > Storage::kMaxWallPaperInMemory
		|| mimeString() == qstr("application/x-tgwallpattern")) {
		return false; // #TODO themes support svg patterns
	}
	type = WallPaperDocument;
	validateGoodThumbnail();
	return true;
}

void DocumentData::updateThumbnails(
		ImagePtr thumbnailInline,
		ImagePtr thumbnail) {
	if (thumbnailInline && !_thumbnailInline) {
		_thumbnailInline = thumbnailInline;
	}
	if (thumbnail
		&& (!_thumbnail
			|| (sticker()
				&& (_thumbnail->width() < thumbnail->width()
					|| _thumbnail->height() < thumbnail->height())))) {
		_thumbnail = thumbnail;
	}
}

bool DocumentData::isWallPaper() const {
	return (type == WallPaperDocument);
}

bool DocumentData::isPatternWallPaper() const {
	return isWallPaper() && hasMimeType(qstr("image/png"));
}

bool DocumentData::hasThumbnail() const {
	return !_thumbnail->isNull();
}

Image *DocumentData::thumbnailInline() const {
	return _thumbnailInline ? _thumbnailInline.get() : nullptr;
}

Image *DocumentData::thumbnail() const {
	return _thumbnail ? _thumbnail.get() : nullptr;
}

void DocumentData::loadThumbnail(Data::FileOrigin origin) {
	if (_thumbnail && !_thumbnail->loaded()) {
		_thumbnail->load(origin);
	}
}

Storage::Cache::Key DocumentData::goodThumbnailCacheKey() const {
	return Data::DocumentThumbCacheKey(_dc, id);
}

Image *DocumentData::goodThumbnail() const {
	return _goodThumbnail.get();
}

void DocumentData::validateGoodThumbnail() {
	if (!isVideoFile()
		&& !isAnimation()
		&& !isWallPaper()
		&& !isTheme()
		&& (!sticker() || !sticker()->animated)) {
		_goodThumbnail = nullptr;
	} else if (!_goodThumbnail && hasRemoteLocation()) {
		_goodThumbnail = std::make_unique<Image>(
			std::make_unique<Data::GoodThumbSource>(this));
	}
}

void DocumentData::refreshGoodThumbnail() {
	if (_goodThumbnail && hasRemoteLocation()) {
		replaceGoodThumbnail(std::make_unique<Data::GoodThumbSource>(this));
	}
}

void DocumentData::replaceGoodThumbnail(
		std::unique_ptr<Images::Source> &&source) {
	_goodThumbnail->replaceSource(std::move(source));
}

void DocumentData::setGoodThumbnailOnUpload(
		QImage &&image,
		QByteArray &&bytes) {
	Expects(uploadingData != nullptr);

	if (image.isNull()) {
		return;
	}
	_goodThumbnail = std::make_unique<Image>(
		std::make_unique<Images::LocalFileSource>(
			QString(),
			std::move(bytes),
			sticker() ? "WEBP" : "JPG",
			std::move(image)));
}

auto DocumentData::bigFileBaseCacheKey() const
-> std::optional<Storage::Cache::Key> {
	if (hasRemoteLocation()) {
		return StorageFileLocation(
			_dc,
			session().userId(),
			MTP_inputDocumentFileLocation(
				MTP_long(id),
				MTP_long(_access),
				MTP_bytes(_fileReference),
				MTP_string())).bigFileBaseCacheKey();
	}
	return std::nullopt;
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument && size < Storage::kMaxStickerInMemory)
		|| (isAnimation() && size < Storage::kMaxAnimationInMemory)
		|| (isVoiceMessage() && size < Storage::kMaxVoiceInMemory)
		|| (type == WallPaperDocument)
		|| (isTheme() && size < Storage::kMaxFileInMemory);
}

void DocumentData::unload() {
	// Forget thumb only when image cache limit exceeds.
	//
	// Also, you can't unload() images that you don't own
	// from the destructor, because they're already destroyed.
	//
	//_thumbnailInline->unload();
	//_thumbnail->unload();
	if (sticker()) {
		if (sticker()->image) {
			ActiveCache().decrement(ComputeUsage(sticker()));
			sticker()->image = nullptr;
		}
	}
	_replyPreview.clear();
	if (!_data.isEmpty()) {
		ActiveCache().decrement(_data.size());
		_data.clear();
	}
}

void DocumentData::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (status != FileReady || loaded() || cancelled()) {
		return;
	} else if (!item && type != StickerDocument && !isAnimation()) {
		return;
	}
	const auto toCache = saveToCache();
	if (!toCache && Global::AskDownloadPath()) {
		// We need a filename, but we're supposed to ask user for it.
		// No automatic download in this case.
		return;
	}
	const auto filename = toCache
		? QString()
		: documentSaveFilename(this);
	const auto shouldLoadFromCloud = !Data::IsExecutableName(filename)
		&& (item
			? Data::AutoDownload::Should(
				session().settings().autoDownload(),
				item->history()->peer,
				this)
			: Data::AutoDownload::Should(
				session().settings().autoDownload(),
				this));
	const auto loadFromCloud = shouldLoadFromCloud
		? LoadFromCloudOrLocal
		: LoadFromLocalOnly;
	save(
		origin,
		filename,
		loadFromCloud,
		true);
}

void DocumentData::automaticLoadSettingsChanged() {
	if (!cancelled() || status != FileReady || loaded()) {
		return;
	}
	_loader = nullptr;
	_flags &= ~Flag::DownloadCancelled;
}

bool DocumentData::loaded(FilePathResolve resolve) const {
	if (loading() && _loader->finished()) {
		if (_loader->cancelled()) {
			_flags |= Flag::DownloadCancelled;
			destroyLoader();
		} else {
			auto that = const_cast<DocumentData*>(this);
			that->setLocation(FileLocation(_loader->fileName()));
			ActiveCache().decrement(that->_data.size());
			that->_data = _loader->bytes();
			ActiveCache().increment(that->_data.size());

			if (that->sticker()
				&& !that->sticker()->image
				&& !_loader->imageData().isNull()) {
				that->sticker()->image = std::make_unique<Image>(
					std::make_unique<Images::LocalFileSource>(
						QString(),
						_data,
						_loader->imageFormat(),
						_loader->imageData()));
				ActiveCache().increment(ComputeUsage(that->sticker()));
			}

			that->refreshGoodThumbnail();
			destroyLoader();

			if (!that->_data.isEmpty() || that->getStickerLarge()) {
				ActiveCache().up(that);
			}
		}
		_owner->notifyDocumentLayoutChanged(this);
	}
	return !data().isEmpty() || !filepath(resolve).isEmpty();
}

void DocumentData::destroyLoader() const {
	if (!_loader) {
		return;
	}
	const auto loader = base::take(_loader);
	if (cancelled()) {
		loader->cancel();
	}
}

bool DocumentData::loading() const {
	return (_loader != nullptr);
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

int DocumentData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return (uploadingData != nullptr);
}

bool DocumentData::loadedInMediaCache() const {
	return (_flags & Flag::LoadedInMediaCache);
}

void DocumentData::setLoadedInMediaCache(bool loaded) {
	const auto flags = loaded
		? (_flags | Flag::LoadedInMediaCache)
		: (_flags & ~Flag::LoadedInMediaCache);
	if (_flags == flags) {
		return;
	}
	_flags = flags;
	if (!this->loaded()) {
		if (loadedInMediaCache()) {
			Local::writeFileLocation(
				mediaKey(),
				FileLocation::InMediaCacheLocation());
		} else {
			Local::removeFileLocation(mediaKey());
		}
		owner().requestDocumentViewRepaint(this);
	}
}

void DocumentData::setLoadedInMediaCacheLocation() {
	_location = FileLocation();
	_flags |= Flag::LoadedInMediaCache;
}

void DocumentData::setWaitingForAlbum() {
	if (uploading()) {
		uploadingData->waitingForAlbum = true;
	}
}

bool DocumentData::waitingForAlbum() const {
	return uploading() && uploadingData->waitingForAlbum;
}

void DocumentData::save(
		Data::FileOrigin origin,
		const QString &toFile,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	if (loaded(FilePathResolve::Checked)) {
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
				const auto &alreadyName = l.name();
				if (alreadyName != toFile) {
					QFile(toFile).remove();
					QFile(alreadyName).copy(toFile);
				}
				l.accessDisable();
			}
		}
		return;
	}

	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel();
		}
	}
	_flags &= ~Flag::DownloadCancelled;

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			_loader->permitLoadFromCloud();
		}
	} else {
		status = FileReady;
		auto reader = owner().streaming().sharedReader(this, origin, true);
		if (reader) {
			_loader = std::make_unique<Storage::StreamedFileDownloader>(
				id,
				_dc,
				origin,
				Data::DocumentCacheKey(_dc, id),
				mediaKey(),
				std::move(reader),
				toFile,
				size,
				locationType(),
				(saveToCache() ? LoadToCacheAsWell : LoadToFileOnly),
				fromCloud,
				autoLoading,
				cacheTag());
		} else if (hasWebLocation()) {
			_loader = std::make_unique<mtpFileLoader>(
				_urlLocation,
				size,
				fromCloud,
				autoLoading,
				cacheTag());
		} else if (!_access && !_url.isEmpty()) {
			_loader = std::make_unique<webFileLoader>(
				_url,
				toFile,
				fromCloud,
				autoLoading,
				cacheTag());
		} else {
			_loader = std::make_unique<mtpFileLoader>(
				StorageFileLocation(
					_dc,
					session().userId(),
					MTP_inputDocumentFileLocation(
						MTP_long(id),
						MTP_long(_access),
						MTP_bytes(_fileReference),
						MTP_string())),
				origin,
				locationType(),
				toFile,
				size,
				(saveToCache() ? LoadToCacheAsWell : LoadToFileOnly),
				fromCloud,
				autoLoading,
				cacheTag());
		}

		QObject::connect(
			_loader.get(),
			&FileLoader::progress,
			App::main(),
			[=](FileLoader *l) { App::main()->documentLoadProgress(l); });
		QObject::connect(
			_loader.get(),
			&FileLoader::failed,
			App::main(),
			&MainWidget::documentLoadFailed);
	}
	if (loading()) {
		_loader->start();
	}
	_owner->notifyDocumentLayoutChanged(this);
}

void DocumentData::cancel() {
	if (!loading()) {
		return;
	}

	_flags |= Flag::DownloadCancelled;
	destroyLoader();
	_owner->notifyDocumentLayoutChanged(this);
	App::main()->documentLoadProgress(this);
}

bool DocumentData::cancelled() const {
	return (_flags & Flag::DownloadCancelled);
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
	if (!_data.isEmpty()) {
		ActiveCache().up(const_cast<DocumentData*>(this));
	}
	return _data;
}

const FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const auto location = Local::readFileLocation(mediaKey());
		const auto that = const_cast<DocumentData*>(this);
		if (location.inMediaCache()) {
			that->setLoadedInMediaCacheLocation();
		} else {
			that->_location = location;
		}
	}
	return _location;
}

void DocumentData::setLocation(const FileLocation &loc) {
	if (loc.inMediaCache()) {
		setLoadedInMediaCacheLocation();
	} else if (loc.check()) {
		_location = loc;
	}
}

QString DocumentData::filepath(FilePathResolve resolve) const {
	bool check = (resolve != FilePathResolve::Cached);
	QString result = (check && _location.name().isEmpty()) ? QString() : location(check).name();
	bool saveFromData = result.isEmpty() && !data().isEmpty();
	if (saveFromData) {
		if (resolve != FilePathResolve::SaveFromData
			&& resolve != FilePathResolve::SaveFromDataSilent) {
			saveFromData = false;
		} else if (resolve == FilePathResolve::SaveFromDataSilent
			&& Global::AskDownloadPath()) {
			saveFromData = false;
		}
	}
	if (saveFromData) {
		QString filename = documentSaveFilename(this);
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

bool DocumentData::isStickerSetInstalled() const {
	Expects(sticker() != nullptr);

	const auto &sets = _owner->stickerSets();
	return sticker()->set.match([&](const MTPDinputStickerSetID &data) {
		const auto i = sets.constFind(data.vid().v);
		return (i != sets.cend())
			&& !(i->flags & MTPDstickerSet::Flag::f_archived)
			&& (i->flags & MTPDstickerSet::Flag::f_installed_date);
	}, [&](const MTPDinputStickerSetShortName &data) {
		const auto name = qs(data.vshort_name()).toLower();
		for (const auto &set : sets) {
			if (set.shortName.toLower() == name) {
				return !(set.flags & MTPDstickerSet::Flag::f_archived)
					&& (set.flags & MTPDstickerSet::Flag::f_installed_date);
			}
		}
		return false;
	}, [](const MTPDinputStickerSetEmpty &) {
		return false;
	}, [](const MTPDinputStickerSetAnimatedEmoji &) {
		return false;
	}, [](const MTPDinputStickerSetDice &) {
		return false;
	});
}

Image *DocumentData::getReplyPreview(Data::FileOrigin origin) {
	if (!_thumbnail) {
		return nullptr;
	} else if (_replyPreview
		&& (_replyPreview.good() || !_thumbnail->loaded())) {
		return _replyPreview.image();
	}
	const auto option = isVideoMessage()
		? Images::Option::Circled
		: Images::Option::None;
	if (_thumbnail->loaded()) {
		_replyPreview.prepare(
			_thumbnail.get(),
			origin,
			option);
	} else {
		_thumbnail->load(origin);
		if (_thumbnailInline) {
			_replyPreview.prepare(
				_thumbnailInline.get(),
				origin,
				option | Images::Option::Blurred);
		}
	}
	return _replyPreview.image();
}

StickerData *DocumentData::sticker() const {
	return (type == StickerDocument)
		? static_cast<StickerData*>(_additional.get())
		: nullptr;
}

void DocumentData::checkStickerLarge() {
	const auto data = sticker();
	if (!data) return;

	automaticLoad(stickerSetOrigin(), nullptr);
	if (!data->image && !data->animated && loaded()) {
		if (_data.isEmpty()) {
			const auto &loc = location(true);
			if (loc.accessEnable()) {
				data->image = std::make_unique<Image>(
					std::make_unique<Images::LocalFileSource>(loc.name()));
				loc.accessDisable();
			}
		} else {
			auto format = QByteArray();
			auto image = App::readImage(_data, &format, false);
			data->image = std::make_unique<Image>(
				std::make_unique<Images::LocalFileSource>(
					QString(),
					_data,
					format,
					std::move(image)));
		}
		if (const auto usage = ComputeUsage(data)) {
			ActiveCache().increment(usage);
			ActiveCache().up(this);
		}
	}
}

void DocumentData::checkStickerSmall() {
	const auto data = sticker();
	if ((data && data->animated) || thumbnailEnoughForSticker()) {
		_thumbnail->load(stickerSetOrigin());
		if (data && data->animated) {
			automaticLoad(stickerSetOrigin(), nullptr);
		}
	} else {
		checkStickerLarge();
	}
}

Image *DocumentData::getStickerLarge() {
	checkStickerLarge();
	if (const auto data = sticker()) {
		return data->image.get();
	}
	return nullptr;
}

Image *DocumentData::getStickerSmall() {
	const auto data = sticker();
	if ((data && data->animated) || thumbnailEnoughForSticker()) {
		return _thumbnail->isNull() ? nullptr : _thumbnail.get();
	} else if (data) {
		return data->image.get();
	}
	return nullptr;
}

Data::FileOrigin DocumentData::stickerSetOrigin() const {
	if (const auto data = sticker()) {
		if (const auto result = data->setOrigin()) {
			return result;
		} else if (Stickers::IsFaved(this)) {
			return Data::FileOriginStickerSet(Stickers::FavedSetId, 0);
		}
	}
	return Data::FileOrigin();
}

Data::FileOrigin DocumentData::stickerOrGifOrigin() const {
	return (sticker()
		? stickerSetOrigin()
		: isGifv()
		? Data::FileOriginSavedGifs()
		: Data::FileOrigin());
}

SongData *DocumentData::song() {
	return isSong()
		? static_cast<SongData*>(_additional.get())
		: nullptr;
}

const SongData *DocumentData::song() const {
	return const_cast<DocumentData*>(this)->song();
}

VoiceData *DocumentData::voice() {
	return isVoiceMessage()
		? static_cast<VoiceData*>(_additional.get())
		: nullptr;
}

const VoiceData *DocumentData::voice() const {
	return const_cast<DocumentData*>(this)->voice();
}

bool DocumentData::hasRemoteLocation() const {
	return (_dc != 0 && _access != 0);
}

bool DocumentData::useStreamingLoader() const {
	return isAnimation()
		|| isVideoFile()
		|| isAudioFile()
		|| isVoiceMessage();
}

bool DocumentData::canBeStreamed() const {
	// For now video messages are not streamed.
	return hasRemoteLocation() && supportsStreaming();
}

bool DocumentData::canBePlayed() const {
	return !(_flags & Flag::StreamingPlaybackFailed)
		&& useStreamingLoader()
		&& (loaded() || canBeStreamed());
}

void DocumentData::setInappPlaybackFailed() {
	_flags |= Flag::StreamingPlaybackFailed;
}

bool DocumentData::inappPlaybackFailed() const {
	return (_flags & Flag::StreamingPlaybackFailed);
}

auto DocumentData::createStreamingLoader(
	Data::FileOrigin origin,
	bool forceRemoteLoader) const
-> std::unique_ptr<Media::Streaming::Loader> {
	if (!useStreamingLoader()) {
		return nullptr;
	}
	if (!forceRemoteLoader) {
		const auto &location = this->location(true);
		if (!data().isEmpty()) {
			return Media::Streaming::MakeBytesLoader(data());
		} else if (!location.isEmpty() && location.accessEnable()) {
			auto result = Media::Streaming::MakeFileLoader(location.name());
			location.accessDisable();
			return result;
		}
	}
	return hasRemoteLocation()
		? std::make_unique<Media::Streaming::LoaderMtproto>(
			&session().downloader(),
			StorageFileLocation(
				_dc,
				session().userId(),
				MTP_inputDocumentFileLocation(
					MTP_long(id),
					MTP_long(_access),
					MTP_bytes(_fileReference),
					MTP_string())),
			size,
			origin)
		: nullptr;
}

bool DocumentData::hasWebLocation() const {
	return !_urlLocation.url().isEmpty();
}

bool DocumentData::isNull() const {
	return !hasRemoteLocation() && !hasWebLocation() && _url.isEmpty();
}

MTPInputDocument DocumentData::mtpInput() const {
	if (_access) {
		return MTP_inputDocument(
			MTP_long(id),
			MTP_long(_access),
			MTP_bytes(_fileReference));
	}
	return MTP_inputDocumentEmpty();
}

QByteArray DocumentData::fileReference() const {
	return _fileReference;
}

void DocumentData::refreshFileReference(const QByteArray &value) {
	_fileReference = value;
	_thumbnail->refreshFileReference(value);
	if (const auto data = sticker()) {
		data->loc.refreshFileReference(value);
	}
}

void DocumentData::refreshStickerThumbFileReference() {
	if (const auto data = sticker()) {
		if (_thumbnail->loading()) {
			data->loc.refreshFileReference(
				_thumbnail->location().fileReference());
		}
	}
}

QString DocumentData::filename() const {
	return _filename;
}

QString DocumentData::mimeString() const {
	return _mimeString;
}

bool DocumentData::hasMimeType(QLatin1String mime) const {
	return !_mimeString.compare(mime, Qt::CaseInsensitive);
}

void DocumentData::setMimeString(const QString &mime) {
	_mimeString = mime;
}

MediaKey DocumentData::mediaKey() const {
	return ::mediaKey(locationType(), _dc, id);
}

Storage::Cache::Key DocumentData::cacheKey() const {
	if (hasWebLocation()) {
		return Data::WebDocumentCacheKey(_urlLocation);
	} else if (!_access && !_url.isEmpty()) {
		return Data::UrlCacheKey(_url);
	} else {
		return Data::DocumentCacheKey(_dc, id);
	}
}

uint8 DocumentData::cacheTag() const {
	if (type == StickerDocument) {
		return Data::kStickerCacheTag;
	} else if (isVoiceMessage()) {
		return Data::kVoiceMessageCacheTag;
	} else if (isVideoMessage()) {
		return Data::kVideoMessageCacheTag;
	} else if (isAnimation()) {
		return Data::kAnimationCacheTag;
	} else if (type == WallPaperDocument) {
		return Data::kImageCacheTag;
	}
	return 0;
}

QString DocumentData::composeNameString() const {
	if (auto songData = song()) {
		return ComposeNameString(
			_filename,
			songData->title,
			songData->performer);
	}
	return ComposeNameString(_filename, QString(), QString());
}

LocationType DocumentData::locationType() const {
	return isVoiceMessage()
		? AudioFileLocation
		: isVideoFile()
		? VideoFileLocation
		: DocumentFileLocation;
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
		|| (hasMimeType(qstr("image/gif"))
			&& !(_flags & Flag::StreamingPlaybackFailed));
}

bool DocumentData::isGifv() const {
	return (type == AnimatedDocument)
		&& hasMimeType(qstr("video/mp4"));
}

bool DocumentData::isTheme() const {
	return
		_mimeString == qstr("application/x-tgtheme-tdesktop")
		|| _filename.endsWith(
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
	const auto prefix = qstr("audio/");
	if (!_mimeString.startsWith(prefix, Qt::CaseInsensitive)) {
		if (_filename.endsWith(qstr(".opus"), Qt::CaseInsensitive)) {
			return true;
		}
		return false;
	}
	const auto left = _mimeString.midRef(prefix.size()).toString();
	const auto types = { qstr("x-wav"), qstr("wav"), qstr("mp4") };
	return ranges::find(types, left) != end(types);
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

TimeId DocumentData::getDuration() const {
	if (const auto song = this->song()) {
		return std::max(song->duration, 0);
	} else if (const auto voice = this->voice()) {
		return std::max(voice->duration, 0);
	} else if (isAnimation() || isVideoFile()) {
		return std::max(_duration, 0);
	}
	return -1;
}

bool DocumentData::isImage() const {
	return (_flags & Flag::ImageType);
}

bool DocumentData::supportsStreaming() const {
	return (_flags & kStreamingSupportedMask) == kStreamingSupportedMaybeYes;
}

void DocumentData::setNotSupportsStreaming() {
	_flags &= ~kStreamingSupportedMask;
	_flags |= kStreamingSupportedNo;
}

void DocumentData::setMaybeSupportsStreaming(bool supports) {
	if ((_flags & kStreamingSupportedMask) == kStreamingSupportedNo) {
		return;
	}
	_flags &= ~kStreamingSupportedMask;
	_flags |= supports
		? kStreamingSupportedMaybeYes
		: kStreamingSupportedMaybeNo;
}

void DocumentData::recountIsImage() {
	const auto isImage = !isAnimation()
		&& !isVideoFile()
		&& fileIsImage(filename(), mimeString());
	if (isImage) {
		_flags |= Flag::ImageType;
	} else {
		_flags &= ~Flag::ImageType;
	}
}

bool DocumentData::thumbnailEnoughForSticker() const {
	return !_thumbnail->isNull()
		&& ((_thumbnail->width() >= 128) || (_thumbnail->height() >= 128));
}

void DocumentData::setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference) {
	_fileReference = fileReference;
	if (_dc != dc || _access != access) {
		_dc = dc;
		_access = access;
		if (!isNull()) {
			if (_location.check()) {
				Local::writeFileLocation(mediaKey(), _location);
			} else {
				_location = Local::readFileLocation(mediaKey());
				if (_location.inMediaCache()) {
					setLoadedInMediaCacheLocation();
				} else if (_location.isEmpty() && loadedInMediaCache()) {
					Local::writeFileLocation(
						mediaKey(),
						FileLocation::InMediaCacheLocation());
				}
			}
		}
	}
	validateGoodThumbnail();
}

void DocumentData::setContentUrl(const QString &url) {
	_url = url;
}

void DocumentData::setWebLocation(const WebFileLocation &location) {
	_urlLocation = location;
}

void DocumentData::collectLocalData(not_null<DocumentData*> local) {
	if (local == this) {
		return;
	}

	_owner->cache().copyIfEmpty(local->cacheKey(), cacheKey());
	if (!local->_data.isEmpty()) {
		ActiveCache().decrement(_data.size());
		_data = local->_data;
		ActiveCache().increment(_data.size());
		if (!_data.isEmpty()) {
			ActiveCache().up(this);
		}
	}
	if (!local->_location.inMediaCache() && !local->_location.isEmpty()) {
		_location = local->_location;
		Local::writeFileLocation(mediaKey(), _location);
	}
}

DocumentData::~DocumentData() {
	destroyLoader();
	unload();
	ActiveCache().remove(this);
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

namespace Data {

QString FileExtension(const QString &filepath) {
	const auto reversed = ranges::view::reverse(filepath);
	const auto last = ranges::find_first_of(reversed, ".\\/");
	if (last == reversed.end() || *last != '.') {
		return QString();
	}
	return QString(last.base(), last - reversed.begin());
}

bool IsValidMediaFile(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto list = qsl("\
16svx 2sf 3g2 3gp 8svx aac aaf aif aifc aiff amr amv ape asf ast au aup \
avchd avi brstm bwf cam cdda cust dat divx drc dsh dsf dts dtshd dtsma \
dvr-ms dwd evo f4a f4b f4p f4v fla flac flr flv gif gifv gsf gsm gym iff \
ifo it jam la ly m1v m2p m2ts m2v m4a m4p m4v mcf mid mk3d mka mks mkv mng \
mov mp1 mp2 mp3 mp4 minipsf mod mpc mpe mpeg mpg mpv mscz mt2 mus mxf mxl \
niff nsf nsv off ofr ofs ogg ogv opus ots pac ps psf psf2 psflib ptb qsf \
qt ra raw rka rm rmj rmvb roq s3m shn sib sid smi smp sol spc spx ssf svi \
swa swf tak ts tta txm usf vgm vob voc vox vqf wav webm wma wmv wrap wtv \
wv xm xml ym yuv").split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();

	return ranges::binary_search(
		kExtensions,
		FileExtension(filepath).toLower());
}

bool IsExecutableName(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto joined =
#ifdef Q_OS_MAC
			qsl("\
applescript action app bin command csh osx workflow terminal url caction \
mpkg pkg scpt scptd xhtm webarchive");
#elif defined Q_OS_LINUX // Q_OS_MAC
			qsl("bin csh deb desktop ksh out pet pkg pup rpm run sh shar \
slp zsh");
#else // Q_OS_MAC || Q_OS_LINUX
			qsl("\
ad ade adp app application appref-ms asp asx bas bat bin cdxml cer cfg chi \
chm cmd cnt com cpl crt csh der diagcab dll drv eml exe fon fxp gadget grp \
hlp hpj hta htt inf ini ins inx isp isu its jar jnlp job js jse ksh lnk \
local lua mad maf mag mam manifest maq mar mas mat mau mav maw mcf mda mdb \
mde mdt mdw mdz mht mhtml mjs mmc mof msc msg msh msh1 msh2 msh1xml msh2xml \
mshxml msi msp mst ops osd paf pcd phar php php3 php4 php5 php7 phps php-s \
pht phtml pif pl plg pm pod prf prg ps1 ps2 ps1xml ps2xml psc1 psc2 psd1 \
psm1 pssc pst py py3 pyc pyd pyi pyo pyw pywz pyz rb reg rgs scf scr sct \
search-ms settingcontent-ms shb shs slk sys t tmp u3p url vb vbe vbp vbs \
vbscript vdx vsmacros vsd vsdm vsdx vss vssm vssx vst vstm vstx vsw vsx vtx \
website ws wsc wsf wsh xbap xll xnk xs");
#endif // !Q_OS_MAC && !Q_OS_LINUX
		const auto list = joined.split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();

	return ranges::binary_search(
		kExtensions,
		FileExtension(filepath).toLower());
}

base::binary_guard ReadImageAsync(
		not_null<DocumentData*> document,
		FnMut<QImage(QImage)> postprocess,
		FnMut<void(QImage&&)> done) {
	auto result = base::binary_guard();
	crl::async([
		bytes = document->data(),
		path = document->filepath(),
		postprocess = std::move(postprocess),
		guard = result.make_guard(),
		callback = std::move(done)
	]() mutable {
		auto format = QByteArray();
		if (bytes.isEmpty()) {
			QFile f(path);
			if (f.size() <= App::kImageSizeLimit
				&& f.open(QIODevice::ReadOnly)) {
				bytes = f.readAll();
			}
		}
		auto image = bytes.isEmpty()
			? QImage()
			: App::readImage(bytes, &format, false, nullptr);
		if (postprocess) {
			image = postprocess(std::move(image));
		}
		crl::on_main(std::move(guard), [
			image = std::move(image),
			callback = std::move(callback)
		]() mutable {
			callback(std::move(image));
		});
	});
	return result;
}

//void HandleUnsupportedMedia(
//		not_null<DocumentData*> document,
//		FullMsgId contextId) {
//	using Error = ::Media::Streaming::Error;
//
//	document->setInappPlaybackFailed();
//	const auto filepath = document->filepath(
//		DocumentData::FilePathResolve::SaveFromData);
//	if (filepath.isEmpty()) {
//		const auto save = [=] {
//			Ui::hideLayer();
//			DocumentSaveClickHandler::Save(
//				(contextId ? contextId : Data::FileOrigin()),
//				document,
//				document->owner().message(contextId));
//		};
//		Ui::show(Box<ConfirmBox>(
//			tr::lng_player_cant_stream(tr::now),
//			tr::lng_player_download(tr::now),
//			tr::lng_cancel(tr::now),
//			save));
//	} else if (IsValidMediaFile(filepath)) {
//		File::Launch(filepath);
//	}
//}

} // namespace Data
