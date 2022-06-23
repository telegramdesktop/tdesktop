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
		not_null<DocumentData*> document);

	void paint(QPainter &p, int x, int y) override;

private:
	not_null<DocumentData*> _document;

};

DocumentCustomEmoji::DocumentCustomEmoji(
	const QString &data,
	not_null<DocumentData*> document)
: CustomEmojiWithData(data)
, _document(document) {
}

void DocumentCustomEmoji::paint(QPainter &p, int x, int y) {
	const auto size = Ui::Emoji::GetSizeNormal() / style::DevicePixelRatio();
	p.fillRect(QRect{ x, y, size, size }, Qt::red);
}

class ResolvingCustomEmoji final : public CustomEmojiWithData {
public:
	explicit ResolvingCustomEmoji(const QString &data);

	void paint(QPainter &p, int x, int y) override;

private:
	std::optional<DocumentCustomEmoji> _resolved;

};

ResolvingCustomEmoji::ResolvingCustomEmoji(const QString &data)
: CustomEmojiWithData(data) {
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
		const QString &data) {
	const auto parsed = ParseCustomEmojiData(data);
	if (!parsed.id) {
		return nullptr;
	}
	const auto document = _owner->document(parsed.id);
	if (!document->isNull()) {
		return std::make_unique<DocumentCustomEmoji>(data, document);
	}
	return std::make_unique<ResolvingCustomEmoji>(data);
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
