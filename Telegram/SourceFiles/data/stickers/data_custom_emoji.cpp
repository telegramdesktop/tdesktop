/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_custom_emoji.h"

#include "chat_helpers/stickers_emoji_pack.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/text/text_block.h"

namespace Data {
namespace {

struct CustomEmojiId {
	StickerSetIdentifier set;
	uint64 id = 0;
};
[[nodiscard]] QString SerializeCustomEmojiId(const CustomEmojiId &id) {
	const auto &info = id.set;
	const auto set = info.id
		? (QString::number(info.id) + ':' + QString::number(info.accessHash))
		: info.shortName;
	return QString::number(id.id) + '@' + set;
}

[[nodiscard]] CustomEmojiId ParseCustomEmojiData(const QString &data) {
	const auto parts = data.split('@');
	if (parts.size() != 2) {
		return {};
	}
	const auto id = parts[0].toULongLong();
	if (!id) {
		return {};
	}
	const auto second = parts[1].split(':');
	if (const auto set = second[0].toULongLong()) {
		return {
			.set = {.id = set, .accessHash = second[1].toULongLong() },
			.id = id
		};
	}
	return {
		.set = {.shortName = second[1] },
		.id = id
	};
}

class CustomEmojiWithData : public Ui::Text::CustomEmoji {
public:
	explicit CustomEmojiWithData(const QString &data);

	QString entityData() final override;

private:
	const QString _data;

};

CustomEmojiWithData::CustomEmojiWithData(const QString &data) : _data(data) {
}

QString CustomEmojiWithData::entityData() {
	return _data;
}

class DocumentCustomEmoji final : public CustomEmojiWithData {
public:
	DocumentCustomEmoji(
		const QString &data,
		not_null<DocumentData*> document,
		Fn<void()> update);

	void paint(QPainter &p, int x, int y) override;

private:
	not_null<DocumentData*> _document;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	Fn<void()> _update;
	rpl::lifetime _lifetime;

};

DocumentCustomEmoji::DocumentCustomEmoji(
	const QString &data,
	not_null<DocumentData*> document,
	Fn<void()> update)
: CustomEmojiWithData(data)
, _document(document)
, _update(update) {
}

void DocumentCustomEmoji::paint(QPainter &p, int x, int y) {
	if (!_media) {
		_media = _document->createMediaView();
		_media->automaticLoad(_document->stickerSetOrigin(), nullptr);
	}
	if (_media->loaded() && !_lottie) {
		const auto size = Ui::Emoji::GetSizeNormal();
		_lottie = ChatHelpers::LottiePlayerFromDocument(
			_media.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::MessageHistory,
			QSize(size, size),
			Lottie::Quality::High);
		_lottie->updates() | rpl::start_with_next(_update, _lifetime);
	}
	if (_lottie && _lottie->ready()) {
		const auto frame = _lottie->frame();
		p.drawImage(
			QRect(
				x,
				y,
				frame.width() / frame.devicePixelRatio(),
				frame.height() / frame.devicePixelRatio()),
			frame);
		_lottie->markFrameShown();
	}
}

class ResolvingCustomEmoji final : public CustomEmojiWithData {
public:
	ResolvingCustomEmoji(const QString &data, Fn<void()> update);

	void paint(QPainter &p, int x, int y) override;

private:
	std::optional<DocumentCustomEmoji> _resolved;
	Fn<void()> _update;

};

ResolvingCustomEmoji::ResolvingCustomEmoji(
	const QString &data,
	Fn<void()> update)
: CustomEmojiWithData(data)
, _update(update) {
}

void ResolvingCustomEmoji::paint(QPainter &p, int x, int y) {
	if (_resolved) {
		_resolved->paint(p, x, y);
	}
}

} // namespace

CustomEmojiManager::CustomEmojiManager(not_null<Session*> owner)
: _owner(owner) {
}

CustomEmojiManager::~CustomEmojiManager() = default;

std::unique_ptr<Ui::Text::CustomEmoji> CustomEmojiManager::create(
		const QString &data,
		Fn<void()> update) {
	const auto parsed = ParseCustomEmojiData(data);
	if (!parsed.id) {
		return nullptr;
	}
	const auto document = _owner->document(parsed.id);
	if (!document->isNull()) {
		return std::make_unique<DocumentCustomEmoji>(data, document, update);
	}
	return std::make_unique<ResolvingCustomEmoji>(data, update);
}

Main::Session &CustomEmojiManager::session() const {
	return _owner->session();
}

Session &CustomEmojiManager::owner() const {
	return *_owner;
}

void FillTestCustomEmoji(
		not_null<Main::Session*> session,
		TextWithEntities &text) {
	const auto pack = &session->emojiStickersPack();
	const auto begin = text.text.constData(), end = begin + text.text.size();
	for (auto ch = begin; ch != end;) {
		auto length = 0;
		if (const auto emoji = Ui::Emoji::Find(ch, end, &length)) {
			if (const auto found = pack->stickerForEmoji(emoji)) {
				Expects(found.document->sticker() != nullptr);

				text.entities.push_back({
					EntityType::CustomEmoji,
					(ch - begin),
					length,
					SerializeCustomEmojiId({
						found.document->sticker()->set,
						found.document->id,
					}),
				});
			}
			ch += length;
		} else if (ch->isHighSurrogate()
			&& (ch + 1 != end)
			&& (ch + 1)->isLowSurrogate()) {
			ch += 2;
		} else {
			++ch;
		}
	}
	ranges::stable_sort(
		text.entities,
		ranges::less(),
		&EntityInText::offset);
}

} // namespace Data
