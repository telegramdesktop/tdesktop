/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_copy_selection.h"

#include "base/qt_signal_producer.h"
#include "base/weak_ptr.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "iv/iv_rich_message_html_export.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "media/view/media_view_recognition_selection.h"
#include "platform/platform_specific.h"
#include "platform/platform_text_recognition.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/controls/text_copied_toast.h"
#include "ui/image/image.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kMaxRecognitionsInFlight = 2;

auto RecognitionsInFlight = 0;

[[nodiscard]] QImage LoadedLargeImage(not_null<PhotoData*> photo) {
	if (const auto media = photo->activeMediaView()) {
		if (const auto image = media->image(Data::PhotoSize::Large)) {
			return image->original();
		}
	}
	return QImage();
}

[[nodiscard]] QImage ReadCachedImage(QByteArray bytes, bool webFile) {
	auto image = Images::Read({ .content = std::move(bytes) }).image;
	if (webFile && image.format() == QImage::Format_ARGB32) {
		image = Images::Opaque(std::move(image));
	}
	const auto limit = PhotoData::SideLimit();
	if (image.width() > limit || image.height() > limit) {
		image = image.scaled(
			limit,
			limit,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return image;
}

class ImageTextCopy final : public base::has_weak_ptr {
public:
	ImageTextCopy(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		HistorySelectedTextParts &&text,
		Iv::RichPageBlocksSlice &&richBlocks);

	[[nodiscard]] bool ready() const;
	void start();
	void complete();

private:
	struct Photo {
		not_null<PhotoData*> photo;
		QImage image;
		QByteArray bytes;
		Storage::Cache::Key key;
		bool webFile = false;
		bool reading = false;
		bool recognizing = false;
		bool resolved = false;
	};

	[[nodiscard]] Media::View::RecognitionId recognitionId(
		not_null<PhotoData*> photo) const;
	[[nodiscard]] const Media::View::RecognitionResult *cached(
		not_null<PhotoData*> photo) const;
	[[nodiscard]] bool hasPhoto(not_null<PhotoData*> photo) const;
	void setBlock(
		not_null<PhotoData*> photo,
		const Media::View::RecognitionResult &result);
	void cacheRead(int index, QByteArray &&bytes);
	void check();
	void recognize(int index);
	void finishIfResolved();

	static void Recognized(
		const base::weak_ptr<ImageTextCopy> &weak,
		int index,
		Media::View::RecognitionId id,
		std::optional<Media::View::RecognitionResult> &&result);

	const std::shared_ptr<Ui::Show> _show;
	const not_null<Main::Session*> _session;
	const int64 _clipboardChangeCount = 0;
	HistorySelectedTextParts _text;
	Iv::RichPageBlocksSlice _richBlocks;
	std::vector<Photo> _photos;
	base::flat_map<not_null<PhotoData*>, QString> _blocks;
	std::unique_ptr<Ui::TextCopiedToastPending> _toast;
	rpl::lifetime _lifetime;

};

[[nodiscard]] std::unique_ptr<ImageTextCopy> &PendingCopy() {
	static auto result = std::unique_ptr<ImageTextCopy>();
	return result;
}

ImageTextCopy::ImageTextCopy(
	std::shared_ptr<Ui::Show> show,
	not_null<Main::Session*> session,
	HistorySelectedTextParts &&text,
	Iv::RichPageBlocksSlice &&richBlocks)
: _show(std::move(show))
, _session(session)
, _clipboardChangeCount(Platform::ClipboardChangeCount())
, _text(std::move(text))
, _richBlocks(std::move(richBlocks)) {
	const auto large = Data::PhotoSize::Large;
	for (const auto &image : _text.images) {
		const auto photo = image.photo;
		if (hasPhoto(photo)) {
			continue;
		}
		auto entry = Photo{ .photo = photo };
		if (const auto result = cached(photo)) {
			entry.resolved = true;
			setBlock(photo, *result);
		} else {
			entry.image = LoadedLargeImage(photo);
			if (entry.image.isNull()) {
				const auto &file = photo->location(large).file();
				entry.key = file.cacheKey();
				entry.webFile = v::is<WebFileLocation>(file.data);
				entry.resolved = !entry.key;
			}
		}
		_photos.push_back(std::move(entry));
	}
}

bool ImageTextCopy::ready() const {
	return ranges::all_of(_photos, &Photo::resolved);
}

void ImageTextCopy::start() {
	_toast = std::make_unique<Ui::TextCopiedToastPending>(_show);
	rpl::merge(
		_session->account().sessionChanges() | rpl::to_empty,
		base::qt_signal_producer(
			QGuiApplication::clipboard(),
			&QClipboard::changed
		) | rpl::filter([](QClipboard::Mode mode) {
			return (mode == QClipboard::Clipboard);
		}) | rpl::to_empty
	) | rpl::on_next([] {
		PendingCopy() = nullptr;
	}, _lifetime);
	// WHY: the cache queue is serial, so lookups queued in the press turn
	// run before any later download's put; a photo still downloading at the
	// press must be skipped, not read once its download lands.
	const auto weak = base::make_weak(this);
	for (auto i = 0, count = int(_photos.size()); i != count; ++i) {
		auto &entry = _photos[i];
		if (entry.resolved || !entry.image.isNull()) {
			continue;
		}
		entry.reading = true;
		_session->data().cache().get(entry.key, [=](QByteArray &&value) {
			crl::on_main(weak, [=, value = std::move(value)]() mutable {
				cacheRead(i, std::move(value));
			});
		});
	}
	check();
}

void ImageTextCopy::complete() {
	_lifetime.destroy();
	const auto text = _text.composed(_blocks);
	// Qt sees an outside pasteboard write only on activation.
	if (text.empty()
		|| Platform::ClipboardChangeCount() != _clipboardChangeCount) {
		return;
	}
	Iv::SetRichBlocksClipboard(text, std::move(_richBlocks), _session);
	if (_toast) {
		_toast->finished();
	} else {
		Ui::ShowTextCopiedToast(_show);
	}
}

Media::View::RecognitionId ImageTextCopy::recognitionId(
		not_null<PhotoData*> photo) const {
	return {
		.sessionUniqueId = _session->uniqueId(),
		.photoId = photo->id,
		.documentId = DocumentId(),
	};
}

const Media::View::RecognitionResult *ImageTextCopy::cached(
		not_null<PhotoData*> photo) const {
	const auto cache = Media::View::RecognitionCache();
	if (!cache) {
		return nullptr;
	}
	const auto i = cache->find(recognitionId(photo));
	return (i != end(*cache)) ? &i->second : nullptr;
}

bool ImageTextCopy::hasPhoto(not_null<PhotoData*> photo) const {
	return ranges::contains(_photos, photo, &Photo::photo);
}

void ImageTextCopy::setBlock(
		not_null<PhotoData*> photo,
		const Media::View::RecognitionResult &result) {
	if (!result.success) {
		return;
	}
	const auto text = Media::View::RecognizedText(result);
	if (!text.isEmpty()) {
		_blocks[photo] = tr::lng_context_copy_text_from_image(tr::now)
			+ '\n'
			+ text;
	}
}

void ImageTextCopy::cacheRead(int index, QByteArray &&bytes) {
	auto &entry = _photos[index];
	if (entry.resolved) {
		return;
	}
	entry.reading = false;
	if (bytes.isEmpty() || bytes.startsWith("partial:")) {
		entry.resolved = true;
	} else {
		entry.bytes = std::move(bytes);
	}
	check();
}

void ImageTextCopy::check() {
	for (auto i = 0, count = int(_photos.size()); i != count; ++i) {
		auto &entry = _photos[i];
		if (entry.resolved || entry.reading) {
			continue;
		} else if (const auto result = cached(entry.photo)) {
			entry.resolved = true;
			entry.image = QImage();
			entry.bytes = QByteArray();
			setBlock(entry.photo, *result);
		} else if (!entry.recognizing
			&& RecognitionsInFlight < kMaxRecognitionsInFlight) {
			recognize(i);
		}
	}
	finishIfResolved();
}

void ImageTextCopy::recognize(int index) {
	auto &entry = _photos[index];
	entry.recognizing = true;
	++RecognitionsInFlight;
	crl::async([
		weak = base::make_weak(this),
		index,
		id = recognitionId(entry.photo),
		image = base::take(entry.image),
		bytes = base::take(entry.bytes),
		webFile = entry.webFile
	]() mutable {
		if (image.isNull()) {
			image = ReadCachedImage(std::move(bytes), webFile);
		}
		auto result = std::optional<Media::View::RecognitionResult>();
		if (!image.isNull()) {
			result = Platform::TextRecognition::RecognizeText(image);
		}
		crl::on_main([
			weak = std::move(weak),
			index,
			id,
			result = std::move(result)
		]() mutable {
			Recognized(weak, index, id, std::move(result));
		});
	});
}

void ImageTextCopy::Recognized(
		const base::weak_ptr<ImageTextCopy> &weak,
		int index,
		Media::View::RecognitionId id,
		std::optional<Media::View::RecognitionResult> &&result) {
	--RecognitionsInFlight;
	if (!result) {
		if (const auto strong = weak.get()) {
			strong->_photos[index].resolved = true;
		}
	} else if (const auto cache = Media::View::RecognitionCache()) {
		(*cache)[id] = std::move(*result);
	}
	if (const auto current = PendingCopy().get()) {
		current->check();
	}
}

void ImageTextCopy::finishIfResolved() {
	if (!ready()) {
		return;
	}
	const auto self = base::take(PendingCopy());
	self->complete();
}

} // namespace

void CopySelection(CopySelectionArgs &&args) {
	if (!args.withImageText || args.text.images.empty()) {
		const auto text = args.text.composed();
		if (text.empty()) {
			return;
		}
		PendingCopy() = nullptr;
		Iv::SetRichBlocksClipboard(
			text,
			std::move(args.blocks),
			args.session);
		return;
	}
	PendingCopy() = nullptr;
	auto copy = std::make_unique<ImageTextCopy>(
		std::move(args.show),
		args.session,
		std::move(args.text),
		std::move(args.blocks));
	if (copy->ready()) {
		copy->complete();
		return;
	}
	const auto raw = copy.get();
	PendingCopy() = std::move(copy);
	raw->start();
}

bool IsCopyWithImageTextShortcut(
		not_null<QKeyEvent*> e,
		not_null<Main::Session*> session) {
	const auto modifiers = e->modifiers() & ~Qt::KeypadModifier;
	return (e->key() == Qt::Key_C)
		&& (modifiers == (Qt::ControlModifier | Qt::ShiftModifier))
		&& !e->isAutoRepeat()
		&& !session->supportMode()
		&& Platform::TextRecognition::IsAvailable();
}

} // namespace HistoryView
