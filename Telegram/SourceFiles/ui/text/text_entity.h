/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum EntityInTextType {
	EntityInTextInvalid = 0,

	EntityInTextUrl,
	EntityInTextCustomUrl,
	EntityInTextEmail,
	EntityInTextHashtag,
	EntityInTextMention,
	EntityInTextMentionName,
	EntityInTextBotCommand,

	EntityInTextBold,
	EntityInTextItalic,
	EntityInTextCode, // inline
	EntityInTextPre,  // block
};

class EntityInText;
using EntitiesInText = QList<EntityInText>;

class EntityInText {
public:
	EntityInText(EntityInTextType type, int offset, int length, const QString &data = QString())
		: _type(type)
		, _offset(offset)
		, _length(length)
		, _data(data) {
	}

	EntityInTextType type() const {
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

	static int firstMonospaceOffset(const EntitiesInText &entities, int textLength) {
		int result = textLength;
		for_const (auto &entity, entities) {
			if (entity.type() == EntityInTextPre || entity.type() == EntityInTextCode) {
				accumulate_min(result, entity.offset());
			}
		}
		return result;
	}

	explicit operator bool() const {
		return type() != EntityInTextInvalid;
	}

private:
	EntityInTextType _type;
	int _offset, _length;
	QString _data;

};

struct TextWithEntities {
	QString text;
	EntitiesInText entities;

	bool empty() const {
		return text.isEmpty();
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

// Parsing helpers.

namespace TextUtilities {

bool IsValidProtocol(const QString &protocol);
bool IsValidTopDomain(const QString &domain);

const QRegularExpression &RegExpDomain();
const QRegularExpression &RegExpDomainExplicit();
const QRegularExpression &RegExpMailNameAtEnd();
const QRegularExpression &RegExpHashtag();
const QRegularExpression &RegExpMention();
const QRegularExpression &RegExpBotCommand();
const QRegularExpression &RegExpMarkdownBold();
const QRegularExpression &RegExpMarkdownItalic();
const QRegularExpression &RegExpMarkdownMonoInline();
const QRegularExpression &RegExpMarkdownMonoBlock();

inline void Append(TextWithEntities &to, TextWithEntities &&append) {
	auto entitiesShiftRight = to.text.size();
	for (auto &entity : append.entities) {
		entity.shiftRight(entitiesShiftRight);
	}
	to.text += append.text;
	to.entities += append.entities;
}

// Text preprocess.
QString Clean(const QString &text);
QString EscapeForRichParsing(const QString &text);
QString SingleLine(const QString &text);
QString RemoveAccents(const QString &text);
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

EntitiesInText EntitiesFromMTP(const QVector<MTPMessageEntity> &entities);
enum class ConvertOption {
	WithLocal,
	SkipLocal,
};
MTPVector<MTPMessageEntity> EntitiesToMTP(const EntitiesInText &entities, ConvertOption option = ConvertOption::WithLocal);

// New entities are added to the ones that are already in result.
// Changes text if (flags & TextParseMarkdown).
void ParseEntities(TextWithEntities &result, int32 flags, bool rich = false);
QString ApplyEntities(const TextWithEntities &text);

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

} // namespace TextUtilities

namespace Lang {

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<TextWithEntities> {
	static inline TextWithEntities Call(QString &&langString) {
		return { std::move(langString), EntitiesInText() };
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<TextWithEntities> {
	static TextWithEntities Call(TextWithEntities &&original, ushort tag, const TextWithEntities &replacement);

};

} // namespace Lang
