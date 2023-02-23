/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_menu_item.h"

#include "base/random.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/userpic/info_userpic_emoji_builder.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <random>

namespace UserpicBuilder {
namespace {

constexpr auto kTimeout = crl::time(1500);

class StickerProvider final {
public:
	StickerProvider(not_null<Data::Session*> owner);

	void setDocuments(std::vector<DocumentId> documents);
	[[nodiscard]] DocumentId documentId() const;
	[[nodiscard]] auto documentChanged() const
	-> rpl::producer<not_null<DocumentData*>>;

private:
	void processDocumentIndex(int documentIndex);
	[[nodiscard]] DocumentData *lookupAndRememberSticker(int documentIndex);
	[[nodiscard]] std::pair<DocumentData*, int> lookupSticker(
		int documentIndex) const;

	const not_null<Data::Session*> _owner;

	int _documentIndex = 0;
	std::vector<DocumentId> _shuffledDocuments;

	base::Timer _timer;

	rpl::event_stream<not_null<DocumentData*>> _documentChanged;
	rpl::lifetime _resolvingLifetime;
	rpl::lifetime _downloadFinishedLifetime;

};

StickerProvider::StickerProvider(not_null<Data::Session*> owner)
: _owner(owner) {
	_timer.setCallback([=] {
		_documentIndex++;
		if (_documentIndex >= _shuffledDocuments.size()) {
			_documentIndex = 0;
		}
		processDocumentIndex(_documentIndex);
	});
}

DocumentId StickerProvider::documentId() const {
	const auto &[document, index] = lookupSticker(_documentIndex);
	return document ? document->id : DocumentId(0);
}

void StickerProvider::setDocuments(std::vector<DocumentId> documents) {
	if (documents.empty()) {
		return;
	}
	auto rd = std::random_device();
	ranges::shuffle(documents, std::mt19937(rd()));
	_shuffledDocuments = std::move(documents);
	_documentIndex = 0;
	processDocumentIndex(_documentIndex);
}

auto StickerProvider::documentChanged() const
-> rpl::producer<not_null<DocumentData*>> {
	return _documentChanged.events();
}

void StickerProvider::processDocumentIndex(int documentIndex) {
	if (const auto document = lookupAndRememberSticker(documentIndex)) {
		_resolvingLifetime.destroy();
		_owner->customEmojiManager().resolve(
			document->id
		) | rpl::start_with_next([=](not_null<DocumentData*> d) {
			_resolvingLifetime.destroy();
			_downloadFinishedLifetime.destroy();

			const auto mediaView = d->createMediaView();
			_downloadFinishedLifetime.add([=] {
				[[maybe_unused]] const auto copy = mediaView;
			});
			mediaView->checkStickerLarge();
			mediaView->goodThumbnailWanted();
			rpl::single(
				rpl::empty_value()
			) | rpl::then(
				_owner->session().downloaderTaskFinished()
			) | rpl::start_with_next([=] {
				if (mediaView->loaded()) {
					_timer.callOnce(kTimeout);
					_documentChanged.fire_copy(mediaView->owner());
					_downloadFinishedLifetime.destroy();
				}
			}, _downloadFinishedLifetime);
		}, _resolvingLifetime);
	} else if (!_resolvingLifetime) {
		_timer.callOnce(kTimeout);
	}
}

DocumentData *StickerProvider::lookupAndRememberSticker(int documentIndex) {
	const auto &[document, index] = lookupSticker(documentIndex);
	if (document) {
		_documentIndex = index;
	}
	return document;
}

std::pair<DocumentData*, int> StickerProvider::lookupSticker(
		int documentIndex) const {
	const auto size = _shuffledDocuments.size();
	for (auto i = 0; i < size; i++) {
		const auto unrestrictedIndex = documentIndex + i;
		const auto index = (unrestrictedIndex >= size)
			? (unrestrictedIndex - size)
			: unrestrictedIndex;
		const auto id = _shuffledDocuments[index];
		const auto document = _owner->document(id);
		if (document->sticker()) {
			return { document, index };
		}
	}
	return { nullptr, 0 };
}

} // namespace

void AddEmojiBuilderAction(
		not_null<Window::SessionController*> controller,
		not_null<Ui::PopupMenu*> menu,
		rpl::producer<std::vector<DocumentId>> documents,
		Fn<void(UserpicBuilder::Result)> &&done,
		bool isForum) {

	struct State final {
		State(not_null<Window::SessionController*> controller)
		: manager(&controller->session().data())
		, colorIndex(rpl::single(
			rpl::empty_value()
		) | rpl::then(
			manager.documentChanged() | rpl::skip(1) | rpl::to_empty
		) | rpl::map([] {
			return base::RandomIndex(std::numeric_limits<int>::max());
		})) {
		}

		StickerProvider manager;
		rpl::variable<int> colorIndex;
	};
	const auto state = menu->lifetime().make_state<State>(controller);
	auto item = base::make_unique_q<Ui::Menu::Action>(
		menu.get(),
		menu->st().menu,
		Ui::Menu::CreateAction(
			menu.get(),
			tr::lng_attach_profile_emoji(tr::now),
			[=, done = std::move(done), docs = rpl::duplicate(documents)] {
				const auto id = state->manager.documentId();
				UserpicBuilder::ShowLayer(
					controller,
					{ id, state->colorIndex.current(), docs, {}, isForum },
					base::duplicate(done));
			}),
		nullptr,
		nullptr);
	const auto icon = UserpicBuilder::CreateEmojiUserpic(
		item.get(),
		st::restoreUserpicIcon.size,
		state->manager.documentChanged(),
		state->colorIndex.value(),
		isForum);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->move(menu->st().menu.itemIconPosition
		+ QPoint(
			(st::menuIconRemove.width() - icon->width()) / 2,
			(st::menuIconRemove.height() - icon->height()) / 2));

	rpl::duplicate(
		documents
	) | rpl::start_with_next([=](std::vector<DocumentId> documents) {
		state->manager.setDocuments(std::move(documents));
	}, item->lifetime());

	menu->addAction(std::move(item));
}

} // namespace UserpicBuilder
