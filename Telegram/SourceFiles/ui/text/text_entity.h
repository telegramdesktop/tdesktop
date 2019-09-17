/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QList>
#include <QtCore/QVector>
#include <QtGui/QClipboard>

enum class EntityType {
	Invalid = 0,

	Url,
	CustomUrl,
	Email,
	Hashtag,
	Cashtag,
	Mention,
	MentionName,
	BotCommand,

	Bold,
	Italic,
	Underline,
	StrikeOut,
	Code, // inline
	Pre,  // block
};

class EntityInText;
using EntitiesInText = QList<EntityInText>;

class EntityInText {
public:
	EntityInText(
		EntityType type,
		int offset,
		int length,
		const QString &data = QString());

	EntityType type() const {
		return _type;
	}
	int offset() const {
		return _offset;
	}
	int length() const {
		return _length;
	}
	QString data() const {
		return _data;
	}

	void extendToLeft(int extent) {
		_offset -= extent;
		_length += extent;
	}
	void shrinkFromRight(int shrink) {
		_length -= shrink;
	}
	void shiftLeft(int shift) {
		_offset -= shift;
		if (_offset < 0) {
			_length += _offset;
			_offset = 0;
			if (_length < 0) {
				_length = 0;
			}
		}
	}
	void shiftRight(int shift) {
		_offset += shift;
	}
	void updateTextEnd(int textEnd) {
		if (_offset > textEnd) {
			_offset = textEnd;
			_length = 0;
		} else if (_offset + _length > textEnd) {
			_length = textEnd - _offset;
		}
	}

	static int FirstMonospaceOffset(
		const EntitiesInText &entities,
		int textLength);

	explicit operator bool() const {
		return type() != EntityType::Invalid;
	}

private:
	EntityType _type = EntityType::Invalid;
	int _offset = 0;
	int _length = 0;
	QString _data;

};

inline bool operator==(const EntityInText &a, const EntityInText &b) {
	return (a.type() == b.type())
		&& (a.offset() == b.offset())
		&& (a.length() == b.length())
		&& (a.data() == b.data());
}

inline bool operator!=(const EntityInText &a, const EntityInText &b) {
	return !(a == b);
}

struct TextWithEntities {
	QString text;
	EntitiesInText entities;

	bool empty() const {
		return text.isEmpty();
	}

	void reserve(int size, int entitiesCount = 0) {
		text.reserve(size);
		entities.reserve(entitiesCount);
	}

	TextWithEntities &append(TextWithEntities &&other) {
		const auto shift = text.size();
		for (auto &entity : other.entities) {
			entity.shiftRight(shift);
		}
		text.append(other.text);
		entities.append(other.entities);
		return *this;
	}
	TextWithEntities &append(const QString &other) {
		text.append(other);
		return *this;
	}
	TextWithEntities &append(QLatin1String other) {
		text.append(other);
		return *this;
	}
	TextWithEntities &append(QChar other) {
		text.append(other);
		return *this;
	}

	static TextWithEntities Simple(const QString &simple) {
		auto result = TextWithEntities();
		result.text = simple;
		return result;
	}
};

inline bool operator==(
		const TextWithEntities &a,
		const TextWithEntities &b) {
	return (a.text == b.text) && (a.entities == b.entities);
}

inline bool operator!=(
		const TextWithEntities &a,
		const TextWithEntities &b) {
	return !(a == b);
}

struct TextForMimeData {
	QString expanded;
	TextWithEntities rich;

	bool empty() const {
		return expanded.isEmpty();
	}

	void reserve(int size, int entitiesCount = 0) {
		expanded.reserve(size);
		rich.reserve(size, entitiesCount);
	}
	TextForMimeData &append(TextForMimeData &&other) {
		expanded.append(other.expanded);
		rich.append(std::move(other.rich));
		return *this;
	}
	TextForMimeData &append(TextWithEntities &&other) {
		expanded.append(other.text);
		rich.append(std::move(other));
		return *this;
	}
	TextForMimeData &append(const QString &other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}
	TextForMimeData &append(QLatin1String other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}
	TextForMimeData &append(QChar other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}

	static TextForMimeData Rich(TextWithEntities &&rich) {
		auto result = TextForMimeData();
		result.expanded = rich.text;
		result.rich = std::move(rich);
		return result;
	}
	static TextForMimeData Simple(const QString &simple) {
		auto result = TextForMimeData();
		result.expanded = result.rich.text = simple;
		return result;
	}
};

enum {
	TextParseMultiline = 0x001,
	TextParseLinks = 0x002,
	TextParseRichText = 0x004,
	TextParseMentions = 0x008,
	TextParseHashtags = 0x010,
	TextParseBotCommands = 0x020,
	TextParseMarkdown = 0x040,

	TextTwitterMentions = 0x100,
	TextTwitterHashtags = 0x200,
	TextInstagramMentions = 0x400,
	TextInstagramHashtags = 0x800,
};

struct TextWithTags {
	struct Tag {
		int offset = 0;
		int length = 0;
		QString id;
	};
	using Tags = QVector<Tag>;

	QString text;
	Tags tags;
};

inline bool operator==(const TextWithTags::Tag &a, const TextWithTags::Tag &b) {
	return (a.offset == b.offset) && (a.length == b.length) && (a.id == b.id);
}
inline bool operator!=(const TextWithTags::Tag &a, const TextWithTags::Tag &b) {
	return !(a == b);
}

inline bool operator==(const TextWithTags &a, const TextWithTags &b) {
	return (a.text == b.text) && (a.tags == b.tags);
}
inline bool operator!=(const TextWithTags &a, const TextWithTags &b) {
	return !(a == b);
}

// Parsing helpers.

namespace TextUtilities {

bool IsValidProtocol(const QString &protocol);
bool IsValidTopDomain(const QString &domain);

const QRegularExpression &RegExpMailNameAtEnd();
const QRegularExpression &RegExpHashtag();
const QRegularExpression &RegExpHashtagExclude();
const QRegularExpression &RegExpMention();
const QRegularExpression &RegExpBotCommand();
QString MarkdownBoldGoodBefore();
QString MarkdownBoldBadAfter();
QString MarkdownItalicGoodBefore();
QString MarkdownItalicBadAfter();
QString MarkdownStrikeOutGoodBefore();
QString MarkdownStrikeOutBadAfter();
QString MarkdownCodeGoodBefore();
QString MarkdownCodeBadAfter();
QString MarkdownPreGoodBefore();
QString MarkdownPreBadAfter();

// Text preprocess.
QString Clean(const QString &text);
QString EscapeForRichParsing(const QString &text);
QString SingleLine(const QString &text);
QString RemoveAccents(const QString &text);
QString RemoveEmoji(const QString &text);
QStringList PrepareSearchWords(const QString &query, const QRegularExpression *SplitterOverride = nullptr);
bool CutPart(TextWithEntities &sending, TextWithEntities &left, int limit);

struct MentionNameFields {
	MentionNameFields(int32 userId = 0, uint64 accessHash = 0) : userId(userId), accessHash(accessHash) {
	}
	int32 userId = 0;
	uint64 accessHash = 0;
};
inline MentionNameFields MentionNameDataToFields(const QString &data) {
	auto components = data.split('.');
	if (!components.isEmpty()) {
		return { components.at(0).toInt(), (components.size() > 1) ? components.at(1).toULongLong() : 0 };
	}
	return MentionNameFields {};
}

inline QString MentionNameDataFromFields(const MentionNameFields &fields) {
	auto result = QString::number(fields.userId);
	if (fields.accessHash) {
		result += '.' + QString::number(fields.accessHash);
	}
	return result;
}

// New entities are added to the ones that are already in result.
// Changes text if (flags & TextParseMarkdown).
TextWithEntities ParseEntities(const QString &text, int32 flags);
void ParseEntities(TextWithEntities &result, int32 flags, bool rich = false);

void PrepareForSending(TextWithEntities &result, int32 flags);
void Trim(TextWithEntities &result);

enum class PrepareTextOption {
	IgnoreLinks,
	CheckLinks,
};
inline QString PrepareForSending(const QString &text, PrepareTextOption option = PrepareTextOption::IgnoreLinks) {
	auto result = TextWithEntities { text };
	auto prepareFlags = (option == PrepareTextOption::CheckLinks) ? (TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands) : 0;
	PrepareForSending(result, prepareFlags);
	return result.text;
}

// Replace bad symbols with space and remove '\r'.
void ApplyServerCleaning(TextWithEntities &result);

QByteArray SerializeTags(const TextWithTags::Tags &tags);
TextWithTags::Tags DeserializeTags(QByteArray data, int textLength);
QString TagsMimeType();
QString TagsTextMimeType();

inline const auto kMentionTagStart = qstr("mention://user.");

[[nodiscard]] bool IsMentionLink(const QString &link);
EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags);
TextWithTags::Tags ConvertEntitiesToTextTags(
	const EntitiesInText &entities);
std::unique_ptr<QMimeData> MimeDataFromText(const TextForMimeData &text);
std::unique_ptr<QMimeData> MimeDataFromText(TextWithTags &&text);
void SetClipboardText(
	const TextForMimeData &text,
	QClipboard::Mode mode = QClipboard::Clipboard);

} // namespace TextUtilities
