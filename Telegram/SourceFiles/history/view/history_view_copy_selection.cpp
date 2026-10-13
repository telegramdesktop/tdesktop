/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_copy_selection.h"

#include "base/qt_signal_producer.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "iv/iv_rich_message_html_export.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "media/view/media_view_recognition_selection.h"
#include "platform/platform_text_recognition.h"
#include "ui/controls/text_copied_toast.h"
#include "ui/image/image.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kMaxRecognitionsInFlight = 2;

auto RecognitionsInFlight = 0;

class ImageTextCopy final {
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
		std::shared_ptr<Data::PhotoMedia> media;
		Data::FileOrigin origin;
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
	void check();
	void recognize(not_null<PhotoData*> photo, QImage image);
	void finishIfResolved();

	static void Recognized(
		Media::View::RecognitionId id,
		Media::View::RecognitionResult &&result);

	const std::shared_ptr<Ui::Show> _show;
	const not_null<Main::Session*> _session;
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
, _text(std::move(text))
, _richBlocks(std::move(richBlocks)) {
	for (const auto &image : _text.images) {
		const auto photo = image.photo;
		if (hasPhoto(photo)) {
			continue;
		}
		auto entry = Photo{ .photo = photo, .origin = image.itemId };
		if (const auto result = cached(photo)) {
			entry.resolved = true;
			setBlock(photo, *result);
		} else {
			entry.media = photo->createMediaView();
			entry.resolved = !entry.media->image(Data::PhotoSize::Large)
				&& (photo->failed(Data::PhotoSize::Large)
					|| !photo->location(Data::PhotoSize::Large).valid());
			if (entry.resolved) {
				entry.media = nullptr;
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
	for (const auto &entry : _photos) {
		if (!entry.resolved) {
			entry.media->wanted(Data::PhotoSize::Large, entry.origin);
		}
	}
	rpl::merge(
		_session->downloaderTaskFinished(),
		_session->data().photoLoadProgress(
		) | rpl::filter([=](not_null<PhotoData*> photo) {
			return hasPhoto(photo);
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		check();
	}, _lifetime);
	check();
}

void ImageTextCopy::complete() {
	_lifetime.destroy();
	const auto text = _text.composed(_blocks);
	if (text.empty()) {
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

void ImageTextCopy::check() {
	const auto large = Data::PhotoSize::Large;
	for (auto &entry : _photos) {
		if (entry.resolved) {
			continue;
		}
		const auto photo = entry.photo;
		if (const auto result = cached(photo)) {
			entry.resolved = true;
			entry.media = nullptr;
			setBlock(photo, *result);
		} else if (entry.recognizing) {
			continue;
		} else if (const auto image = entry.media->image(large)) {
			if (RecognitionsInFlight < kMaxRecognitionsInFlight) {
				entry.recognizing = true;
				recognize(photo, image->original());
				entry.media = nullptr;
			}
		} else if (photo->failed(large) || photo->cancelled()) {
			entry.resolved = true;
			entry.media = nullptr;
		}
	}
	finishIfResolved();
}

void ImageTextCopy::recognize(not_null<PhotoData*> photo, QImage image) {
	++RecognitionsInFlight;
	crl::async([id = recognitionId(photo), image = std::move(image)] {
		auto result = Platform::TextRecognition::RecognizeText(image);
		crl::on_main([=, result = std::move(result)]() mutable {
			Recognized(id, std::move(result));
		});
	});
}

void ImageTextCopy::Recognized(
		Media::View::RecognitionId id,
		Media::View::RecognitionResult &&result) {
	--RecognitionsInFlight;
	if (const auto cache = Media::View::RecognitionCache()) {
		(*cache)[id] = std::move(result);
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
		Ui::ShowTextCopiedToast(args.show);
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
