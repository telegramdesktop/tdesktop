/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document.h"

#include "data/data_document_resolver.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_document_media.h"
#include "data/data_reply_preview.h"
#include "data/data_web_page.h"
#include "lang/lang_keys.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/stickers/data_stickers.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_loader_mtproto.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "storage/streamed_file_downloader.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_download_web.h"
#include "base/options.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_gif.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "base/base_file_utilities.h"
#include "mainwindow.h"
#include "core/application.h"
#include "lottie/lottie_animation.h"
#include "boxes/abstract_box.h" // Ui::hideLayer().

#include <QtCore/QBuffer>
#include <QtCore/QMimeType>
#include <QtCore/QMimeDatabase>

namespace {

constexpr auto kDefaultCoverThumbnailSize = 100;
constexpr auto kMaxAllowedPreloadPrefix = 6 * 1024 * 1024;
constexpr auto kDefaultWebmEmojiSize = 100;
constexpr auto kDefaultWebmStickerLargerSize = kStickerSideSize;

const auto kLottieStickerDimensions = QSize(
	kStickerSideSize,
	kStickerSideSize);

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

void UpdateStickerSetIdentifier(
		StickerSetIdentifier &now,
		const MTPInputStickerSet &from) {
	now = from.match([&](const MTPDinputStickerSetID &data) {
		return StickerSetIdentifier{
			.id = data.vid().v,
			.accessHash = data.vaccess_hash().v,
		};
	}, [](const auto &) {
		return StickerSetIdentifier();
	});
}

} // namespace

QString FileNameUnsafe(
		not_null<Main::Session*> session,
		const QString &title,
		const QString &filter,
		const QString &prefix,
		QString name,
		bool savingAs,
		const QDir &dir) {
	name = base::FileNameFromUserString(name);
	if (Core::App().settings().askDownloadPath() || savingAs) {
		if (!name.isEmpty() && name.at(0) == QChar::fromLatin1('.')) {
			name = filedialogDefaultName(prefix, name);
		} else if (dir.path() != u"."_q) {
			QString path = dir.absolutePath();
			if (path != cDialogLastPath()) {
				cSetDialogLastPath(path);
				Local::writeSettings();
			}
		}

		// check if extension of filename is present in filter
		// it should be in first filter section on the first place
		// place it there, if it is not
		QString ext = QFileInfo(name).suffix(), fil = filter, sep = u";;"_q;
		if (!ext.isEmpty()) {
			if (QRegularExpression(u"^[a-zA-Z_0-9]+$"_q).match(ext).hasMatch()) {
				QStringList filters = filter.split(sep);
				if (filters.size() > 1) {
					const auto &first = filters.at(0);
					int32 start = first.indexOf(u"(*."_q);
					if (start >= 0) {
						if (!QRegularExpression(u"\\(\\*\\."_q + ext + u"[\\)\\s]"_q, QRegularExpression::CaseInsensitiveOption).match(first).hasMatch()) {
							QRegularExpressionMatch m = QRegularExpression(u" \\*\\."_q + ext + u"[\\)\\s]"_q, QRegularExpression::CaseInsensitiveOption).match(first);
							if (m.hasMatch() && m.capturedStart() > start + 3) {
								int32 oldpos = m.capturedStart(), oldend = m.capturedEnd();
								fil = first.mid(0, start + 3) + ext + u" *."_q + first.mid(start + 3, oldpos - start - 3) + first.mid(oldend - 1) + sep + JoinStringList(filters.mid(1), sep);
							} else {
								fil = first.mid(0, start + 3) + ext + u" *."_q + first.mid(start + 3) + sep + JoinStringList(filters.mid(1), sep);
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

	auto path = [&] {
		const auto path = Core::App().settings().downloadPath();
		if (path.isEmpty()) {
			return File::DefaultDownloadPath(session);
		} else if (path == FileDialog::Tmp()) {
			return session->local().tempDirectory();
		} else {
			return path;
		}
	}();
	if (path.isEmpty()) return QString();
	if (name.isEmpty()) name = u".unknown"_q;
	if (name.at(0) == QChar::fromLatin1('.')) {
		if (!QDir().exists(path)) QDir().mkpath(path);
		return filedialogDefaultName(prefix, name, path);
	}
	if (dir.path() != u"."_q) {
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
	for (int i = 0; QFileInfo::exists(name); ++i) {
		name = nameBase + u" (%1)"_q.arg(i + 2) + extension;
	}

	if (!QDir().exists(path)) QDir().mkpath(path);
	return name;
}

QString FileNameForSave(
		not_null<Main::Session*> session,
		const QString &title,
		const QString &filter,
		const QString &prefix,
		QString name,
		bool savingAs,
		const QDir &dir) {
	const auto result = FileNameUnsafe(
		session,
		title,
		filter,
		prefix,
		name,
		savingAs,
		dir);
#ifdef Q_OS_WIN
	const auto lower = result.trimmed().toLower();
	const auto kBadExtensions = { u".lnk"_q, u".scf"_q };
	const auto kMaskExtension = u".download"_q;
	for (const auto extension : kBadExtensions) {
		if (lower.endsWith(extension)) {
			return result + kMaskExtension;
		}
	}
#endif // Q_OS_WIN
	return result;
}

QString DocumentFileNameForSave(
		not_null<const DocumentData*> data,
		bool forceSavingAs,
		const QString &already,
		const QDir &dir) {
	auto alreadySavingFilename = data->loadingFilePath();
	if (!alreadySavingFilename.isEmpty()) {
		return alreadySavingFilename;
	}

	QString name, filter, caption, prefix;
	const auto mimeType = Core::MimeTypeForName(data->mimeString());
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->isVoiceMessage()) {
		auto mp3 = data->hasMimeType(u"audio/mp3"_q);
		name = already.isEmpty() ? (mp3 ? u".mp3"_q : u".ogg"_q) : already;
		filter = mp3 ? u"MP3 Audio (*.mp3);;"_q : u"OGG Opus Audio (*.ogg);;"_q;
		filter += FileDialog::AllFilesFilter();
		caption = tr::lng_save_audio(tr::now);
		prefix = u"audio"_q;
	} else if (data->isVideoFile()) {
		name = already.isEmpty() ? data->filename() : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? u".mov"_q : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = u"MOV Video (*.mov);;"_q + FileDialog::AllFilesFilter();
		} else {
			filter = mimeType.filterString() + u";;"_q + FileDialog::AllFilesFilter();
		}
		caption = tr::lng_save_video(tr::now);
		prefix = u"video"_q;
	} else {
		name = already.isEmpty() ? data->filename() : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? u".unknown"_q : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = QString();
		} else {
			filter = mimeType.filterString() + u";;"_q + FileDialog::AllFilesFilter();
		}
		caption = data->isAudioFile()
			? tr::lng_save_audio_file(tr::now)
			: tr::lng_save_file(tr::now);
		prefix = u"doc"_q;
	}

	return FileNameForSave(
		&data->session(),
		caption,
		filter,
		prefix,
		name,
		forceSavingAs,
		dir);
}

Data::FileOrigin StickerData::setOrigin() const {
	return set.id
		? Data::FileOrigin(
			Data::FileOriginStickerSet(set.id, set.accessHash))
		: Data::FileOrigin();
}

bool StickerData::isStatic() const {
	return (type == StickerType::Webp);
}

bool StickerData::isLottie() const {
	return (type == StickerType::Tgs);
}

bool StickerData::isAnimated() const {
	return !isStatic();
}

bool StickerData::isWebm() const {
	return (type == StickerType::Webm);
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

DocumentData::~DocumentData() {
	base::take(_thumbnail.loader).reset();
	base::take(_videoThumbnail.loader).reset();
	destroyLoader();
}

Data::Session &DocumentData::owner() const {
	return *_owner;
}

Main::Session &DocumentData::session() const {
	return _owner->session();
}

void DocumentData::setattributes(
		const QVector<MTPDocumentAttribute> &attributes) {
	_duration = -1;
	_flags &= ~(Flag::ImageType
		| Flag::HasAttachedStickers
		| Flag::UseTextColor
		| Flag::SilentVideo
		| kStreamingSupportedMask);
	_flags |= kStreamingSupportedUnknown;

	validateLottieSticker();

	auto wasVideoData = isVideoFile() ? std::move(_additional) : nullptr;

	_videoPreloadPrefix = 0;
	for (const auto &attribute : attributes) {
		attribute.match([&](const MTPDdocumentAttributeImageSize &data) {
			dimensions = QSize(data.vw().v, data.vh().v);
		}, [&](const MTPDdocumentAttributeAnimated &data) {
			if (type == FileDocument
				|| type == VideoDocument
				|| (sticker() && sticker()->type != StickerType::Webm)) {
				type = AnimatedDocument;
				_additional = nullptr;
			}
		}, [&](const MTPDdocumentAttributeSticker &data) {
			const auto was = type;
			if (type == FileDocument || type == VideoDocument) {
				type = StickerDocument;
				_additional = std::make_unique<StickerData>();
			}
			if (const auto info = sticker()) {
				info->setType = data.is_mask()
					? Data::StickersType::Masks
					: Data::StickersType::Stickers;
				if (was == VideoDocument) {
					info->type = StickerType::Webm;
				}
				info->alt = qs(data.valt());
				UpdateStickerSetIdentifier(info->set, data.vstickerset());
			}
		}, [&](const MTPDdocumentAttributeCustomEmoji &data) {
			const auto was = type;
			if (type == FileDocument || type == VideoDocument) {
				type = StickerDocument;
				_additional = std::make_unique<StickerData>();
			}
			if (const auto info = sticker()) {
				info->setType = Data::StickersType::Emoji;
				if (was == VideoDocument) {
					info->type = StickerType::Webm;
				}
				info->alt = qs(data.valt());
				if (data.is_free()) {
					_flags &= ~Flag::PremiumSticker;
				} else {
					_flags |= Flag::PremiumSticker;
				}
				if (data.is_text_color()) {
					_flags |= Flag::UseTextColor;
				}
				UpdateStickerSetIdentifier(info->set, data.vstickerset());
			}
		}, [&](const MTPDdocumentAttributeVideo &data) {
			if (type == FileDocument) {
				type = data.is_round_message()
					? RoundVideoDocument
					: VideoDocument;
				if (data.is_round_message()) {
					_additional = std::make_unique<RoundData>();
				} else {
					if (const auto size = data.vpreload_prefix_size()) {
						if (size->v > 0
							&& size->v < kMaxAllowedPreloadPrefix) {
							_videoPreloadPrefix = size->v;
						}
					}
					_additional = wasVideoData
						? std::move(wasVideoData)
						: std::make_unique<VideoData>();
					video()->codec = qs(
						data.vvideo_codec().value_or_empty());
				}
			} else if (type == VideoDocument && wasVideoData) {
				_additional = std::move(wasVideoData);
			} else if (const auto info = sticker()) {
				info->type = StickerType::Webm;
			}
			_duration = crl::time(
				base::SafeRound(data.vduration().v * 1000));
			setMaybeSupportsStreaming(data.is_supports_streaming());
			if (data.is_nosound()) {
				_flags |= Flag::SilentVideo;
			}
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
			if (const auto voiceData = voice() ? voice() : round()) {
				_duration = data.vduration().v * crl::time(1000);
				voiceData->waveform = documentWaveformDecode(
					data.vwaveform().value_or_empty());
				voiceData->wavemax = voiceData->waveform.empty()
					? uchar(0)
					: *ranges::max_element(voiceData->waveform);
			} else if (const auto songData = song()) {
				_duration = data.vduration().v * crl::time(1000);
				songData->title = qs(data.vtitle().value_or_empty());
				songData->performer = qs(data.vperformer().value_or_empty());
				refreshPossibleCoverThumbnail();
			}
		}, [&](const MTPDdocumentAttributeFilename &data) {
			setFileName(qs(data.vfile_name()));
		}, [&](const MTPDdocumentAttributeHasStickers &data) {
			_flags |= Flag::HasAttachedStickers;
		});
	}

	// Any "video/webm" file is treated as a video-sticker.
	if (hasMimeType(u"video/webm"_q)) {
		if (type == FileDocument) {
			type = StickerDocument;
			_additional = std::make_unique<StickerData>();
		}
		if (type == StickerDocument) {
			sticker()->type = StickerType::Webm;
		}
	}

	// If "video/webm" sticker without dimensions we set them to default.
	if (const auto info = sticker(); info
		&& info->set
		&& info->type == StickerType::Webm
		&& dimensions.isEmpty()) {
		if (info->setType == Data::StickersType::Emoji) {
			// Always fixed.
			dimensions = { kDefaultWebmEmojiSize, kDefaultWebmEmojiSize };
		} else if (info->setType == Data::StickersType::Stickers) {
			// May have aspect != 1, so we count it from the thumbnail.
			const auto thumbnail = QSize(
				_thumbnail.location.width(),
				_thumbnail.location.height()
			).scaled(
				kDefaultWebmStickerLargerSize,
				kDefaultWebmStickerLargerSize,
				Qt::KeepAspectRatio);
			if (!thumbnail.isEmpty()) {
				dimensions = thumbnail;
			}
		}
	}

	// Check sticker size/dimensions properties (for sticker of any type).
	if (type == StickerDocument
		&& ((size > Storage::kMaxStickerBytesSize)
			|| (!sticker()->isLottie()
				&& !GoodStickerDimensions(
					dimensions.width(),
					dimensions.height())))) {
		type = FileDocument;
		_additional = nullptr;
	}

	if (!_filename.isEmpty()) {
		using Type = Core::NameType;
		if (type == VideoDocument
			|| type == AnimatedDocument
			|| type == RoundVideoDocument
			|| isAnimation()) {
			if (!enforceNameType(Type::Video)) {
				type = FileDocument;
				_additional = nullptr;
			}
		}
		if (type == SongDocument || type == VoiceDocument || isAudioFile()) {
			if (!enforceNameType(Type::Audio)) {
				type = FileDocument;
				_additional = nullptr;
			}
		}
		if (!Core::NameTypeAllowsThumbnail(_nameType)) {
			_inlineThumbnailBytes = {};
			_flags &= ~Flag::InlineThumbnailIsPath;
			_thumbnail.clear();
			_videoThumbnail.clear();
		}
	}

	if (isAudioFile()
		|| isAnimation()
		|| isVoiceMessage()
		|| storyMedia()) {
		setMaybeSupportsStreaming(true);
	}
}

void DocumentData::setVideoQualities(const QVector<MTPDocument> &list) {
	auto qualities = std::vector<not_null<DocumentData*>>();
	qualities.reserve(list.size());
	for (const auto &document : list) {
		qualities.push_back(owner().processDocument(document));
	}
	setVideoQualities(std::move(qualities));
}

void DocumentData::setVideoQualities(
		std::vector<not_null<DocumentData*>> qualities) {
	const auto data = video();
	if (!data) {
		return;
	}
	auto count = int(qualities.size());
	if (qualities.empty()) {
		return;
	}
	const auto good = [&](not_null<DocumentData*> document) {
		return document->isVideoFile()
			&& !document->dimensions.isEmpty()
			&& !document->inappPlaybackFailed()
			&& document->useStreamingLoader()
			&& document->canBeStreamed(nullptr);
	};
	ranges::sort(
		qualities,
		ranges::greater(),
		&DocumentData::resolveVideoQuality);
	for (auto i = 0; i != count - 1;) {
		const auto my = qualities[i];
		const auto next = qualities[i + 1];
		const auto myQuality = my->resolveVideoQuality();
		const auto nextQuality = next->resolveVideoQuality();
		const auto myGood = good(my);
		const auto nextGood = good(next);
		if (!myGood || !nextGood || myQuality == nextQuality) {
			const auto removeMe = !myGood
				|| (nextGood && (my->size > next->size));
			const auto from = i + (removeMe ? 1 : 2);
			for (auto j = from; j != count; ++j) {
				qualities[j - 1] = qualities[j];
			}
			--count;
		} else {
			++i;
		}
	}
	if (!qualities[count - 1]->resolveVideoQuality()) {
		--count;
	}
	qualities.erase(qualities.begin() + count, qualities.end());
	if (!qualities.empty()) {
		if (const auto mine = resolveVideoQuality()) {
			if (mine > qualities.front()->resolveVideoQuality()) {
				qualities.insert(begin(qualities), this);
			}
		}
	}
	data->qualities = std::move(qualities);
}

int DocumentData::resolveVideoQuality() const {
	const auto size = isVideoFile() ? dimensions : QSize();
	return size.isEmpty() ? 0 : std::min(size.width(), size.height());
}

auto DocumentData::resolveQualities(HistoryItem *context) const
-> const std::vector<not_null<DocumentData*>> & {
	static const auto empty = std::vector<not_null<DocumentData*>>();
	const auto info = video();
	const auto media = context ? context->media() : nullptr;
	if (!info || !media || media->document() != this) {
		return empty;
	}
	return media->hasQualitiesList() ? info->qualities : empty;
}

not_null<DocumentData*> DocumentData::chooseQuality(
		HistoryItem *context,
		Media::VideoQuality request) {
	const auto &list = resolveQualities(context);
	if (list.empty() || !request.height) {
		return this;
	}
	const auto height = int(request.height);
	auto closest = this;
	auto closestAbs = std::abs(height - resolveVideoQuality());
	auto closestSize = size;
	for (const auto &quality : list) {
		const auto abs = std::abs(height - quality->resolveVideoQuality());
		if (abs < closestAbs
			|| (abs == closestAbs && quality->size < closestSize)) {
			closest = quality;
			closestAbs = abs;
			closestSize = quality->size;
		}
	}
	return closest;
}

void DocumentData::validateLottieSticker() {
	if (type == FileDocument
		&& hasMimeType(u"application/x-tgsticker"_q)) {
		type = StickerDocument;
		_additional = std::make_unique<StickerData>();
		sticker()->type = StickerType::Tgs;
		dimensions = kLottieStickerDimensions;
	}
}

void DocumentData::setDataAndCache(const QByteArray &data) {
	if (const auto media = activeMediaView()) {
		media->setBytes(data);
	}
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
		|| !hasThumbnail()
		|| dimensions.isEmpty()
		|| dimensions.width() > Storage::kMaxWallPaperDimension
		|| dimensions.height() > Storage::kMaxWallPaperDimension
		|| size > Storage::kMaxWallPaperInMemory) {
		return false;
	}
	type = WallPaperDocument;
	return true;
}

void DocumentData::updateThumbnails(
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker) {
	if (!_filename.isEmpty()
		&& !Core::NameTypeAllowsThumbnail(Core::DetectNameType(_filename))) {
		return;
	}
	if (!inlineThumbnail.bytes.isEmpty()
		&& _inlineThumbnailBytes.isEmpty()) {
		_inlineThumbnailBytes = inlineThumbnail.bytes;
		if (inlineThumbnail.isPath) {
			_flags |= Flag::InlineThumbnailIsPath;
		} else {
			_flags &= ~Flag::InlineThumbnailIsPath;
		}
	}
	if (!sticker() || sticker()->setType != Data::StickersType::Emoji) {
		if (isPremiumSticker) {
			_flags |= Flag::PremiumSticker;
		} else {
			_flags &= ~Flag::PremiumSticker;
		}
	}
	Data::UpdateCloudFile(
		_thumbnail,
		thumbnail,
		owner().cache(),
		Data::kImageCacheTag,
		[&](Data::FileOrigin origin) { loadThumbnail(origin); },
		[&](QImage preloaded, QByteArray) {
			if (const auto media = activeMediaView()) {
				media->setThumbnail(std::move(preloaded));
			}
		});
	Data::UpdateCloudFile(
		_videoThumbnail,
		videoThumbnail,
		owner().cache(),
		Data::kAnimationCacheTag,
		[&](Data::FileOrigin origin) { loadVideoThumbnail(origin); });
}

bool DocumentData::isWallPaper() const {
	return (type == WallPaperDocument);
}

bool DocumentData::isPatternWallPaper() const {
	return isWallPaper()
		&& (isPatternWallPaperPNG() || isPatternWallPaperSVG());
}

bool DocumentData::isPatternWallPaperPNG() const {
	return isWallPaper() && hasMimeType(u"image/png"_q);
}

bool DocumentData::isPatternWallPaperSVG() const {
	return isWallPaper() && hasMimeType(u"application/x-tgwallpattern"_q);
}

bool DocumentData::isPremiumSticker() const {
	if (!(_flags & Flag::PremiumSticker)) {
		return false;
	}
	const auto info = sticker();
	return info && info->setType == Data::StickersType::Stickers;
}

bool DocumentData::isPremiumEmoji() const {
	if (!(_flags & Flag::PremiumSticker)) {
		return false;
	}
	const auto info = sticker();
	return info && info->setType == Data::StickersType::Emoji;
}

bool DocumentData::emojiUsesTextColor() const {
	return (_flags & Flag::UseTextColor);
}

void DocumentData::overrideEmojiUsesTextColor(bool value) {
	if (value) {
		_flags |= Flag::UseTextColor;
	} else {
		_flags &= ~Flag::UseTextColor;
	}
}

bool DocumentData::hasThumbnail() const {
	return _thumbnail.location.valid()
		&& !thumbnailFailed()
		&& !(_flags & Flag::PossibleCoverThumbnail);
}

bool DocumentData::thumbnailLoading() const {
	return _thumbnail.loader != nullptr;
}

bool DocumentData::thumbnailFailed() const {
	return (_thumbnail.flags & Data::CloudFile::Flag::Failed);
}

void DocumentData::loadThumbnail(Data::FileOrigin origin) {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return !active->thumbnail();
		}
		return true;
	};
	const auto done = [=](QImage result, QByteArray) {
		_flags &= ~Flag::PossibleCoverThumbnail;
		if (const auto active = activeMediaView()) {
			active->setThumbnail(std::move(result));
		}
	};
	Data::LoadCloudFile(
		&session(),
		_thumbnail,
		origin,
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kImageCacheTag,
		finalCheck,
		done);
}

const ImageLocation &DocumentData::thumbnailLocation() const {
	return _thumbnail.location;
}

int DocumentData::thumbnailByteSize() const {
	return _thumbnail.byteSize;
}

bool DocumentData::hasVideoThumbnail() const {
	return _videoThumbnail.location.valid();
}

bool DocumentData::videoThumbnailLoading() const {
	return _videoThumbnail.loader != nullptr;
}

bool DocumentData::videoThumbnailFailed() const {
	return (_videoThumbnail.flags & Data::CloudFile::Flag::Failed);
}

void DocumentData::loadVideoThumbnail(Data::FileOrigin origin) {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return active->videoThumbnailContent().isEmpty();
		}
		return true;
	};
	const auto done = [=](QByteArray result) {
		if (const auto active = activeMediaView()) {
			active->setVideoThumbnail(std::move(result));
		}
	};
	Data::LoadCloudFile(
		&session(),
		_videoThumbnail,
		origin,
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kAnimationCacheTag,
		finalCheck,
		done);
}

const ImageLocation &DocumentData::videoThumbnailLocation() const {
	return _videoThumbnail.location;
}

int DocumentData::videoThumbnailByteSize() const {
	return _videoThumbnail.byteSize;
}

Storage::Cache::Key DocumentData::goodThumbnailCacheKey() const {
	return Data::DocumentThumbCacheKey(_dc, id);
}

bool DocumentData::goodThumbnailChecked() const {
	return (_goodThumbnailState & GoodThumbnailFlag::Mask)
		== GoodThumbnailFlag::Checked;
}

bool DocumentData::goodThumbnailGenerating() const {
	return (_goodThumbnailState & GoodThumbnailFlag::Mask)
		== GoodThumbnailFlag::Generating;
}

bool DocumentData::goodThumbnailNoData() const {
	return (_goodThumbnailState & GoodThumbnailFlag::Mask)
		== GoodThumbnailFlag::NoData;
}

void DocumentData::setGoodThumbnailGenerating() {
	_goodThumbnailState = (_goodThumbnailState & ~GoodThumbnailFlag::Mask)
		| GoodThumbnailFlag::Generating;
}

void DocumentData::setGoodThumbnailDataReady() {
	_goodThumbnailState = GoodThumbnailFlag::DataReady
		| (goodThumbnailNoData()
			? GoodThumbnailFlag(0)
			: (_goodThumbnailState & GoodThumbnailFlag::Mask));
}

void DocumentData::setGoodThumbnailChecked(bool hasData) {
	if (!hasData && (_goodThumbnailState & GoodThumbnailFlag::DataReady)) {
		_goodThumbnailState &= ~GoodThumbnailFlag::DataReady;
		_goodThumbnailState &= ~GoodThumbnailFlag::Mask;
		Data::DocumentMedia::CheckGoodThumbnail(this);
		return;
	}
	_goodThumbnailState = (_goodThumbnailState & ~GoodThumbnailFlag::Mask)
		| (hasData
			? GoodThumbnailFlag::Checked
			: GoodThumbnailFlag::NoData);
}

std::shared_ptr<Data::DocumentMedia> DocumentData::createMediaView() {
	if (auto result = activeMediaView()) {
		return result;
	}
	auto result = std::make_shared<Data::DocumentMedia>(this);
	_media = result;
	return result;
}

std::shared_ptr<Data::DocumentMedia> DocumentData::activeMediaView() const {
	return _media.lock();
}

void DocumentData::setGoodThumbnailPhoto(not_null<PhotoData*> photo) {
	_goodThumbnailPhoto = photo;
}

PhotoData *DocumentData::goodThumbnailPhoto() const {
	return _goodThumbnailPhoto;
}

Storage::Cache::Key DocumentData::bigFileBaseCacheKey() const {
	return hasRemoteLocation()
		? StorageFileLocation(
			_dc,
			session().userId(),
			MTP_inputDocumentFileLocation(
				MTP_long(id),
				MTP_long(_access),
				MTP_bytes(_fileReference),
				MTP_string())).bigFileBaseCacheKey()
		: Storage::Cache::Key();
}

void DocumentData::forceToCache(bool force) {
	if (force) {
		_flags |= Flag::ForceToCache;
	} else {
		_flags &= ~Flag::ForceToCache;
	}
}

bool DocumentData::saveToCache() const {
	return (size < Storage::kMaxFileInMemory)
		&& ((type == StickerDocument)
			|| (_flags & Flag::ForceToCache)
			|| isAnimation()
			|| isVoiceMessage()
			|| isWallPaper()
			|| isTheme()
			|| (hasMimeType(u"image/png"_q)
				&& _filename.startsWith("image_")));
}

void DocumentData::automaticLoadSettingsChanged() {
	if (!cancelled() || status != FileReady) {
		return;
	}
	_loader = nullptr;
	resetCancelled();
}

void DocumentData::finishLoad() {
	// NB! _loader may be in ~FileLoader() already.
	const auto guard = gsl::finally([&] {
		destroyLoader();
	});
	if (!_loader || _loader->cancelled()) {
		_flags |= Flag::DownloadCancelled;
		return;
	}
	setLocation(Core::FileLocation(_loader->fileName()));
	setGoodThumbnailDataReady();
	if (const auto media = activeMediaView()) {
		media->setBytes(_loader->bytes());
		media->checkStickerLarge(_loader.get());
	}
}

void DocumentData::destroyLoader() {
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
		? !_loader->loadingLocal()
		: (uploading() && !waitingForAlbum());
}

float64 DocumentData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ float64(uploadingData->size);
			return std::clamp(result, 0., 1.);
		}
		return 0.;
	}
	return loading() ? _loader->currentProgress() : 0.;
}

int64 DocumentData::loadOffset() const {
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
	if (filepath().isEmpty()) {
		if (loadedInMediaCache()) {
			session().local().writeFileLocation(
				mediaKey(),
				Core::FileLocation::InMediaCacheLocation());
		} else {
			session().local().removeFileLocation(mediaKey());
		}
		owner().requestDocumentViewRepaint(this);
	}
}

ChatRestriction DocumentData::requiredSendRight() const {
	return isVideoFile()
		? ChatRestriction::SendVideos
		: isSong()
		? ChatRestriction::SendMusic
		: isVoiceMessage()
		? ChatRestriction::SendVoiceMessages
		: isVideoMessage()
		? ChatRestriction::SendVideoMessages
		: sticker()
		? ChatRestriction::SendStickers
		: isAnimation()
		? ChatRestriction::SendGifs
		: ChatRestriction::SendFiles;
}

void DocumentData::setFileName(const QString &remoteFileName) {
	_filename = remoteFileName;

	// We don't want LTR/RTL mark/embedding/override/isolate chars
	// in filenames, because they introduce a security issue, when
	// an executable "Fil[x]gepj.exe" may look like "Filexe.jpeg".
	QChar controls[] = {
		QChar(0x200E), // LTR Mark
		QChar(0x200F), // RTL Mark
		QChar(0x202A), // LTR Embedding
		QChar(0x202B), // RTL Embedding
		QChar(0x202D), // LTR Override
		QChar(0x202E), // RTL Override
		QChar(0x2066), // LTR Isolate
		QChar(0x2067), // RTL Isolate
	};
	for (const auto &ch : controls) {
		_filename = std::move(_filename).replace(ch, "_");
	}
	_nameType = Core::DetectNameType(_filename);
}

bool DocumentData::enforceNameType(Core::NameType nameType) {
	if (_nameType == nameType) {
		return true;
	}
	const auto base = _filename.isEmpty() ? u"file"_q : _filename;
	const auto mime = Core::MimeTypeForName(mimeString());
	const auto patterns = mime.globPatterns();
	for (const auto &pattern : mime.globPatterns()) {
		const auto now = base + QString(pattern).replace('*', QString());
		if (Core::DetectNameType(now) == nameType) {
			_filename = now;
			_nameType = nameType;
			return true;
		}
	}
	return false;
}

void DocumentData::setLoadedInMediaCacheLocation() {
	_location = Core::FileLocation();
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
	if (const auto media = activeMediaView(); media && media->loaded(true)) {
		auto &l = location(true);
		if (!toFile.isEmpty()) {
			if (!media->bytes().isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(media->bytes());
				f.close();

				setLocation(Core::FileLocation(toFile));
				session().local().writeFileLocation(
					mediaKey(),
					Core::FileLocation(toFile));
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
	resetCancelled();

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			_loader->permitLoadFromCloud();
		}
	} else {
		status = FileReady;
		auto reader = owner().streaming().sharedReader(this, origin, true);
		if (reader) {
			_loader = std::make_unique<Storage::StreamedFileDownloader>(
				&session(),
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
				&session(),
				_urlLocation,
				size,
				size,
				fromCloud,
				autoLoading,
				cacheTag());
		} else if (!_access && !_url.isEmpty()) {
			_loader = std::make_unique<webFileLoader>(
				&session(),
				_url,
				toFile,
				fromCloud,
				autoLoading,
				cacheTag());
		} else {
			_loader = std::make_unique<mtpFileLoader>(
				&session(),
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
				size,
				(saveToCache() ? LoadToCacheAsWell : LoadToFileOnly),
				fromCloud,
				autoLoading,
				cacheTag());
		}
		handleLoaderUpdates();
	}
	if (loading()) {
		_loader->start();
	}
	// This affects a display of tooltips.
	// _owner->notifyDocumentLayoutChanged(this);
}

void DocumentData::handleLoaderUpdates() {
	_loader->updates(
	) | rpl::start_with_next_error_done([=] {
		_owner->documentLoadProgress(this);
	}, [=](FileLoader::Error error) {
		using FailureReason = FileLoader::FailureReason;
		if (error.started && _loader) {
			const auto origin = _loader->fileOrigin();
			const auto failedFileName = _loader->fileName();
			const auto retry = [=] {
				Ui::hideLayer();
				save(origin, failedFileName);
			};
			Ui::show(Ui::MakeConfirmBox({
				tr::lng_download_finish_failed(),
				crl::guard(&session(), retry)
			}));
		} else if (error.failureReason == FailureReason::FileWriteFailure) {
			if (!Core::App().settings().downloadPath().isEmpty()) {
				Core::App().settings().setDownloadPathBookmark(QByteArray());
				Core::App().settings().setDownloadPath(QString());
				Core::App().saveSettingsDelayed();
				InvokeQueued(qApp, [] {
					Ui::show(
						Ui::MakeInformBox(
							tr::lng_download_path_failed(tr::now)));
				});
			}
		}
		finishLoad();
		status = FileDownloadFailed;
		_owner->documentLoadFail(this, error.started);
	}, [=] {
		finishLoad();
		_owner->documentLoadDone(this);
	}, _loader->lifetime());

}

void DocumentData::cancel() {
	if (!loading()) {
		return;
	}

	_flags |= Flag::DownloadCancelled;
	destroyLoader();
	_owner->documentLoadDone(this);
}

bool DocumentData::cancelled() const {
	return (_flags & Flag::DownloadCancelled);
}

void DocumentData::resetCancelled() {
	_flags &= ~Flag::DownloadCancelled;
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
	for (auto i = 0, l = int(waveform.size()); i < l; ++i) {
		auto byteIndex = (i * 5) / 8;
		auto bitShift = (i * 5) % 8;
		auto value = (static_cast<uint16>(waveform[i]) & 0x1F) << bitShift;
		*reinterpret_cast<uint16*>(bitsData + byteIndex) |= value;
	}
	result.resize(bytesCount);
	return result;
}

const Core::FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const auto location = session().local().readFileLocation(mediaKey());
		const auto that = const_cast<DocumentData*>(this);
		if (location.inMediaCache()) {
			that->setLoadedInMediaCacheLocation();
		} else {
			that->_location = location;
		}
	}
	return _location;
}

void DocumentData::setLocation(const Core::FileLocation &loc) {
	if (loc.inMediaCache()) {
		setLoadedInMediaCacheLocation();
	} else if (loc.check()) {
		_location = loc;
	}
}

QString DocumentData::filepath(bool check) const {
	return (check && _location.name().isEmpty())
		? QString()
		: location(check).name();
}

bool DocumentData::saveFromData() {
	return !filepath(true).isEmpty() || saveFromDataChecked();
}

bool DocumentData::saveFromDataSilent() {
	return !filepath(true).isEmpty()
		|| (Core::App().canSaveFileWithoutAskingForPath()
			&& saveFromDataChecked());
}

bool DocumentData::saveFromDataChecked() {
	const auto media = activeMediaView();
	if (!media) {
		return false;
	}
	const auto bytes = media->bytes();
	if (bytes.isEmpty()) {
		return false;
	}
	const auto path = DocumentFileNameForSave(this);
	if (path.isEmpty()) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(bytes) != bytes.size()) {
		return false;
	}
	file.close();
	_location = Core::FileLocation(path);
	session().local().writeFileLocation(mediaKey(), _location);
	return true;
}

void DocumentData::refreshPossibleCoverThumbnail() {
	Expects(isSong());

	if (_thumbnail.location.valid()) {
		return;
	}
	const auto songData = song();
	if (songData->performer.isEmpty()
		|| songData->title.isEmpty()
		// Ignore cover for voice chat records.
		|| hasMimeType(u"audio/ogg"_q)) {
		return;
	}
	const auto size = kDefaultCoverThumbnailSize;
	const auto location = ImageWithLocation{
		.location = ImageLocation(
			{ AudioAlbumThumbLocation{ id } },
			size,
			size)
	};
	_flags |= Flag::PossibleCoverThumbnail;
	updateThumbnails({}, location, {}, false);
	loadThumbnail({});
}

bool DocumentData::isStickerSetInstalled() const {
	Expects(sticker() != nullptr);

	using SetFlag = Data::StickersSetFlag;

	const auto &sets = _owner->stickers().sets();
	if (const auto id = sticker()->set.id) {
		const auto i = sets.find(id);
		return (i != sets.cend())
			&& !(i->second->flags & SetFlag::Archived)
			&& (i->second->flags & SetFlag::Installed);
	} else {
		return false;
	}
}

Image *DocumentData::getReplyPreview(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler) {
	if (!hasThumbnail()) {
		return nullptr;
	} else if (!_replyPreview) {
		_replyPreview = std::make_unique<Data::ReplyPreview>(this);
	}
	return _replyPreview->image(origin, context, spoiler);
}

Image *DocumentData::getReplyPreview(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto spoiler = media && media->hasSpoiler();
	return getReplyPreview(item->fullId(), item->history()->peer, spoiler);
}

bool DocumentData::replyPreviewLoaded(bool spoiler) const {
	if (!hasThumbnail()) {
		return true;
	} else if (!_replyPreview) {
		return false;
	}
	return _replyPreview->loaded(spoiler);
}

StickerData *DocumentData::sticker() const {
	return (type == StickerDocument)
		? static_cast<StickerData*>(_additional.get())
		: nullptr;
}

Data::FileOrigin DocumentData::stickerSetOrigin() const {
	if (const auto data = sticker()) {
		if (const auto result = data->setOrigin()) {
			return result;
		} else if (owner().stickers().isFaved(this)) {
			return Data::FileOriginStickerSet(Data::Stickers::FavedSetId, 0);
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

RoundData *DocumentData::round() {
	return isVideoMessage()
		? static_cast<RoundData*>(_additional.get())
		: nullptr;
}

const RoundData *DocumentData::round() const {
	return const_cast<DocumentData*>(this)->round();
}

VideoData *DocumentData::video() {
	return isVideoFile()
		? static_cast<VideoData*>(_additional.get())
		: nullptr;
}

const VideoData *DocumentData::video() const {
	return const_cast<DocumentData*>(this)->video();
}

bool DocumentData::hasRemoteLocation() const {
	return (_dc != 0 && _access != 0);
}

bool DocumentData::useStreamingLoader() const {
	if (size <= 0) {
		return false;
	} else if (const auto info = sticker()) {
		return info->isWebm();
	}
	return isAnimation()
		|| isVideoFile()
		|| isAudioFile()
		|| isVoiceMessage();
}

bool DocumentData::canBeStreamed(HistoryItem *item) const {
	// Streaming couldn't be used with external player
	// Maybe someone brave will implement this once upon a time...
	static const auto &ExternalVideoPlayer = base::options::lookup<bool>(
		Data::kOptionExternalVideoPlayer);
	return hasRemoteLocation()
		&& supportsStreaming()
		&& (!isVideoFile()
			|| storyMedia()
			|| !ExternalVideoPlayer.value()
			|| (item && !item->allowsForward()));
}

void DocumentData::setInappPlaybackFailed() {
	_flags |= Flag::StreamingPlaybackFailed;
}

bool DocumentData::inappPlaybackFailed() const {
	return (_flags & Flag::StreamingPlaybackFailed);
}

int DocumentData::videoPreloadPrefix() const {
	return _videoPreloadPrefix;
}

StorageFileLocation DocumentData::videoPreloadLocation() const {
	return hasRemoteLocation()
		? StorageFileLocation(
			_dc,
			session().userId(),
			MTP_inputDocumentFileLocation(
				MTP_long(id),
				MTP_long(_access),
				MTP_bytes(_fileReference),
				MTP_string()))
		: StorageFileLocation();
}

auto DocumentData::createStreamingLoader(
	Data::FileOrigin origin,
	bool forceRemoteLoader) const
-> std::unique_ptr<Media::Streaming::Loader> {
	if (!useStreamingLoader()) {
		return nullptr;
	}
	if (!forceRemoteLoader) {
		const auto media = activeMediaView();
		const auto &location = this->location(true);
		if (media && !media->bytes().isEmpty()) {
			return Media::Streaming::MakeBytesLoader(media->bytes());
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
	return !hasRemoteLocation()
		&& !hasWebLocation()
		&& _url.isEmpty()
		&& !uploading()
		&& _location.isEmpty();
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
	_thumbnail.location.refreshFileReference(value);
	_videoThumbnail.location.refreshFileReference(value);
}

QString DocumentData::filename() const {
	return _filename;
}

Core::NameType DocumentData::nameType() const {
	return _nameType;
}

QString DocumentData::mimeString() const {
	return _mimeString;
}

bool DocumentData::hasMimeType(const QString &mime) const {
	return (_mimeString == mime);
}

void DocumentData::setMimeString(const QString &mime) {
	_mimeString = mime;
	_mimeString = std::move(_mimeString).toLower();
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
	} else if (isWallPaper()) {
		return Data::kImageCacheTag;
	}
	return 0;
}

LocationType DocumentData::locationType() const {
	return isVoiceMessage()
		? AudioFileLocation
		: isVideoFile()
		? VideoFileLocation
		: DocumentFileLocation;
}

void DocumentData::forceIsStreamedAnimation() {
	type = AnimatedDocument;
	_additional = nullptr;
	setMaybeSupportsStreaming(true);
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
		|| ((_filename.isEmpty()
			|| _nameType == Core::NameType::Image
			|| _nameType == Core::NameType::Video)
			&& hasMimeType(u"image/gif"_q)
			&& !(_flags & Flag::StreamingPlaybackFailed));
}

bool DocumentData::isGifv() const {
	return (type == AnimatedDocument)
		&& hasMimeType(u"video/mp4"_q);
}

bool DocumentData::isTheme() const {
	return _filename.endsWith(u".tdesktop-theme"_q, Qt::CaseInsensitive)
		|| _filename.endsWith(u".tdesktop-palette"_q, Qt::CaseInsensitive)
		|| (hasMimeType(u"application/x-tgtheme-tdesktop"_q)
			&& (_filename.isEmpty()
				|| _nameType == Core::NameType::ThemeFile));
}

bool DocumentData::isSong() const {
	return (type == SongDocument);
}

bool DocumentData::isSongWithCover() const {
	return isSong() && hasThumbnail();
}

bool DocumentData::isAudioFile() const {
	if (isVoiceMessage() || isVideoFile()) {
		return false;
	} else if (isSong()) {
		return true;
	}
	const auto prefix = u"audio/"_q;
	if (!_mimeString.startsWith(prefix, Qt::CaseInsensitive)) {
		if (_filename.endsWith(u".opus"_q, Qt::CaseInsensitive)) {
			return true;
		}
		return false;
	} else if (!_filename.isEmpty()
		&& _nameType != Core::NameType::Audio
		&& _nameType != Core::NameType::Video) {
		return false;
	}
	const auto left = _mimeString.mid(prefix.size());
	const auto types = { u"x-wav"_q, u"wav"_q, u"mp4"_q };
	return ranges::contains(types, left);
}

bool DocumentData::isSharedMediaMusic() const {
	return isSong();
}

bool DocumentData::isVideoFile() const {
	return (type == VideoDocument);
}

bool DocumentData::isSilentVideo() const {
	return _flags & Flag::SilentVideo;
}

crl::time DocumentData::duration() const {
	return std::max(_duration, crl::time());
}

bool DocumentData::hasDuration() const {
	return _duration >= 0;
}

bool DocumentData::isImage() const {
	return (_flags & Flag::ImageType);
}

bool DocumentData::hasAttachedStickers() const {
	return (_flags & Flag::HasAttachedStickers);
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
		&& Core::FileIsImage(filename(), mimeString());
	if (isImage) {
		_flags |= Flag::ImageType;
	} else {
		_flags &= ~Flag::ImageType;
	}
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
				session().local().writeFileLocation(mediaKey(), _location);
			} else {
				_location = session().local().readFileLocation(mediaKey());
				if (_location.inMediaCache()) {
					setLoadedInMediaCacheLocation();
				} else if (_location.isEmpty() && loadedInMediaCache()) {
					session().local().writeFileLocation(
						mediaKey(),
						Core::FileLocation::InMediaCacheLocation());
				}
			}
		}
	}
}

void DocumentData::setStoryMedia(bool value) {
	if (value) {
		_flags |= Flag::StoryDocument;
		setMaybeSupportsStreaming(true);
	} else {
		_flags &= ~Flag::StoryDocument;
	}
}

bool DocumentData::storyMedia() const {
	return (_flags & Flag::StoryDocument);
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
	if (const auto localMedia = local->activeMediaView()) {
		auto media = createMediaView();
		media->collectLocalData(localMedia.get());
		_owner->keepAlive(std::move(media));
	}
	if (!local->_location.inMediaCache() && !local->_location.isEmpty()) {
		_location = local->_location;
		session().local().writeFileLocation(mediaKey(), _location);
	}
}

PhotoData *LookupVideoCover(
		not_null<DocumentData*> document,
		HistoryItem *item) {
	const auto media = item ? item->media() : nullptr;
	if (const auto webpage = media ? media->webpage() : nullptr) {
		if (webpage->document == document && webpage->photoIsVideoCover) {
			return webpage->photo;
		}
		return nullptr;
	}
	return (media && media->document() == document)
		? media->videoCover()
		: nullptr;
}
