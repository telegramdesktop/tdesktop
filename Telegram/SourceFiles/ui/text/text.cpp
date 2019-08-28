/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text.h"

#include "core/click_handler_types.h"
#include "core/crash_reports.h"
#include "ui/text/text_block.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "platform/platform_info.h"
#include "boxes/confirm_box.h"
#include "mainwindow.h"

#include <private/qfontengine_p.h>
#include <private/qharfbuzz_p.h>

namespace Ui {
namespace Text {
namespace {

constexpr auto kStringLinkIndexShift = uint16(0x8000);

Qt::LayoutDirection StringDirection(const QString &str, int32 from, int32 to) {
	const ushort *p = reinterpret_cast<const ushort*>(str.unicode()) + from;
	const ushort *end = p + (to - from);
	while (p < end) {
		uint ucs4 = *p;
		if (QChar::isHighSurrogate(ucs4) && p < end - 1) {
			ushort low = p[1];
			if (QChar::isLowSurrogate(low)) {
				ucs4 = QChar::surrogateToUcs4(ucs4, low);
				++p;
			}
		}
		switch (QChar::direction(ucs4)) {
		case QChar::DirL:
			return Qt::LeftToRight;
		case QChar::DirR:
		case QChar::DirAL:
			return Qt::RightToLeft;
		default:
			break;
		}
		++p;
	}
	return Qt::LayoutDirectionAuto;
}

TextWithEntities PrepareRichFromPlain(
		const QString &text,
		const TextParseOptions &options) {
	auto result = TextWithEntities{ text };
	if (options.flags & TextParseLinks) {
		TextUtilities::ParseEntities(
			result,
			options.flags,
			(options.flags & TextParseRichText));
	}
	return result;
}

TextWithEntities PrepareRichFromRich(
		const TextWithEntities &text,
		const TextParseOptions &options) {
	auto result = text;
	const auto &preparsed = text.entities;
	if ((options.flags & TextParseLinks) && !preparsed.isEmpty()) {
		bool parseMentions = (options.flags & TextParseMentions);
		bool parseHashtags = (options.flags & TextParseHashtags);
		bool parseBotCommands = (options.flags & TextParseBotCommands);
		bool parseMarkdown = (options.flags & TextParseMarkdown);
		if (!parseMentions || !parseHashtags || !parseBotCommands || !parseMarkdown) {
			int32 i = 0, l = preparsed.size();
			result.entities.clear();
			result.entities.reserve(l);
			const QChar s = result.text.size();
			for (; i < l; ++i) {
				auto type = preparsed.at(i).type();
				if (((type == EntityType::Mention || type == EntityType::MentionName) && !parseMentions) ||
					(type == EntityType::Hashtag && !parseHashtags) ||
					(type == EntityType::Cashtag && !parseHashtags) ||
					(type == EntityType::BotCommand && !parseBotCommands) || // #TODO entities
					(!parseMarkdown && (type == EntityType::Bold
						|| type == EntityType::Italic
						|| type == EntityType::Underline
						|| type == EntityType::StrikeOut
						|| type == EntityType::Code
						|| type == EntityType::Pre))) {
					continue;
				}
				result.entities.push_back(preparsed.at(i));
			}
		}
	}
	return result;
}

QFixed ComputeStopAfter(const TextParseOptions &options, const style::TextStyle &st) {
	return (options.maxw > 0 && options.maxh > 0)
		? ((options.maxh / st.font->height) + 1) * options.maxw
		: QFIXED_MAX;
}

// Open Sans tilde fix.
bool ComputeCheckTilde(const style::TextStyle &st) {
	const auto &font = st.font;
	return (font->size() * cIntRetinaFactor() == 13)
		&& (font->flags() == 0)
		&& (font->f.family() == qstr("Open Sans"));
}

} // namespace
} // namespace Text
} // namespace Ui

bool chIsBad(QChar ch) {
	return (ch == 0)
        || (ch >= 8232 && ch < 8237)
        || (ch >= 65024 && ch < 65040 && ch != 65039)
        || (ch >= 127 && ch < 160 && ch != 156)

		|| (Platform::IsMac()
			&& !Platform::IsMac10_7OrGreater()
			&& (ch == 8207 || ch == 8206 || ch == 8288))

        // qt harfbuzz crash see https://github.com/telegramdesktop/tdesktop/issues/4551
        || (Platform::IsMac() && ch == 6158)

        // tmp hack see https://bugreports.qt.io/browse/QTBUG-48910
		|| (Platform::IsMac10_11OrGreater()
			&& !Platform::IsMac10_12OrGreater()
			&& ch >= 0x0B00
            && ch <= 0x0B7F
            && chIsDiac(ch));
}

QString textcmdSkipBlock(ushort w, ushort h) {
	static QString cmd(5, TextCommand);
	cmd[1] = QChar(TextCommandSkipBlock);
	cmd[2] = QChar(w);
	cmd[3] = QChar(h);
	return cmd;
}

QString textcmdStartLink(ushort lnkIndex) {
	static QString cmd(4, TextCommand);
	cmd[1] = QChar(TextCommandLinkIndex);
	cmd[2] = QChar(lnkIndex);
	return cmd;
}

QString textcmdStartLink(const QString &url) {
	if (url.size() >= 4096) return QString();

	QString result;
	result.reserve(url.size() + 4);
	return result.append(TextCommand).append(QChar(TextCommandLinkText)).append(QChar(url.size())).append(url).append(TextCommand);
}

QString textcmdStopLink() {
	return textcmdStartLink(0);
}

QString textcmdLink(ushort lnkIndex, const QString &text) {
	QString result;
	result.reserve(4 + text.size() + 4);
	return result.append(textcmdStartLink(lnkIndex)).append(text).append(textcmdStopLink());
}

QString textcmdLink(const QString &url, const QString &text) {
	QString result;
	result.reserve(4 + url.size() + text.size() + 4);
	return result.append(textcmdStartLink(url)).append(text).append(textcmdStopLink());
}

QString textcmdStartSemibold() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandSemibold)).append(TextCommand);
}

QString textcmdStopSemibold() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandNoSemibold)).append(TextCommand);
}

const QChar *textSkipCommand(const QChar *from, const QChar *end, bool canLink) {
	const QChar *result = from + 1;
	if (*from != TextCommand || result >= end) return from;

	ushort cmd = result->unicode();
	++result;
	if (result >= end) return from;

	switch (cmd) {
	case TextCommandBold:
	case TextCommandNoBold:
	case TextCommandSemibold:
	case TextCommandNoSemibold:
	case TextCommandItalic:
	case TextCommandNoItalic:
	case TextCommandUnderline:
	case TextCommandNoUnderline:
		break;

	case TextCommandLinkIndex:
		if (result->unicode() > 0x7FFF) return from;
		++result;
		break;

	case TextCommandLinkText: {
		ushort len = result->unicode();
		if (len >= 4096 || !canLink) return from;
		result += len + 1;
	} break;

	case TextCommandSkipBlock:
		result += 2;
		break;

	case TextCommandLangTag:
		result += 1;
		break;
	}
	return (result < end && *result == TextCommand) ? (result + 1) : from;
}

const TextParseOptions _defaultOptions = {
	TextParseLinks | TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions _textPlainOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

namespace Ui {
namespace Text {

class Parser {
public:
	Parser(
		not_null<String*> string,
		const QString &text,
		const TextParseOptions &options);
	Parser(
		not_null<String*> string,
		const TextWithEntities &textWithEntities,
		const TextParseOptions &options);

private:
	struct ReadyToken {
	};

	enum LinkDisplayStatus {
		LinkDisplayedFull,
		LinkDisplayedElided,
	};

	struct TextLinkData {
		TextLinkData() = default;
		TextLinkData(
			EntityType type,
			const QString &text,
			const QString &data,
			LinkDisplayStatus displayStatus);
		EntityType type = EntityType::Invalid;
		QString text, data;
		LinkDisplayStatus displayStatus = LinkDisplayedFull;
	};

	class StartedEntity {
	public:
		explicit StartedEntity(TextBlockFlags flags);
		explicit StartedEntity(uint16 lnkIndex);

		std::optional<TextBlockFlags> flags() const;
		std::optional<uint16> lnkIndex() const;

	private:
		int _value = 0;

	};

	Parser(
		not_null<String*> string,
		TextWithEntities &&source,
		const TextParseOptions &options,
		ReadyToken);

	void trimSourceRange();
	void blockCreated();
	void createBlock(int32 skipBack = 0);
	void createSkipBlock(int32 w, int32 h);
	void createNewlineBlock();
	bool checkCommand();

	// Returns true if at least one entity was parsed in the current position.
	bool checkEntities();
	bool readSkipBlockCommand();
	bool readCommand();
	void parseCurrentChar();
	void parseEmojiFromCurrent();
	void checkForElidedSkipBlock();
	void finalize(const TextParseOptions &options);

	void finishEntities();
	void skipPassedEntities();
	void skipBadEntities();

	bool isInvalidEntity(const EntityInText &entity) const;
	bool isLinkEntity(const EntityInText &entity) const;

	void parse(const TextParseOptions &options);
	void computeLinkText(
		const QString &linkData,
		QString *outLinkText,
		LinkDisplayStatus *outDisplayStatus);

	static ClickHandlerPtr CreateHandlerForLink(
		const TextLinkData &link,
		const TextParseOptions &options);

	const not_null<String*> _t;
	const TextWithEntities _source;
	const QChar * const _start = nullptr;
	const QChar *_end = nullptr; // mutable, because we trim by decrementing.
	const QChar *_ptr = nullptr;
	const EntitiesInText::const_iterator _entitiesEnd;
	EntitiesInText::const_iterator _waitingEntity;
	const bool _rich = false;
	const bool _multiline = false;

	const QFixed _stopAfterWidth; // summary width of all added words
	const bool _checkTilde = false; // do we need a special text block for tilde symbol

	std::vector<TextLinkData> _links;
	base::flat_map<
		const QChar*,
		std::vector<StartedEntity>> _startedEntities;

	uint16 _maxLnkIndex = 0;

	// current state
	int32 _flags = 0;
	uint16 _lnkIndex = 0;
	EmojiPtr _emoji = nullptr; // current emoji, if current word is an emoji, or zero
	int32 _blockStart = 0; // offset in result, from which current parsed block is started
	int32 _diacs = 0; // diac chars skipped without good char
	QFixed _sumWidth;
	bool _sumFinished = false;
	bool _newlineAwaited = false;

	// current char data
	QChar _ch; // current char (low surrogate, if current char is surrogate pair)
	int32 _emojiLookback = 0; // how far behind the current ptr to look for current emoji
	bool _lastSkipped = false; // did we skip current char

};

Parser::TextLinkData::TextLinkData(
	EntityType type,
	const QString &text,
	const QString &data,
	LinkDisplayStatus displayStatus)
: type(type)
, text(text)
, data(data)
, displayStatus(displayStatus) {
}

Parser::StartedEntity::StartedEntity(TextBlockFlags flags) : _value(flags) {
	Expects(_value >= 0 && _value < int(kStringLinkIndexShift));
}

Parser::StartedEntity::StartedEntity(uint16 lnkIndex) : _value(lnkIndex) {
	Expects(_value >= kStringLinkIndexShift);
}

std::optional<TextBlockFlags> Parser::StartedEntity::flags() const {
	if (_value < int(kStringLinkIndexShift)) {
		return TextBlockFlags(_value);
	}
	return std::nullopt;
}

std::optional<uint16> Parser::StartedEntity::lnkIndex() const {
	if (_value >= int(kStringLinkIndexShift)) {
		return uint16(_value);
	}
	return std::nullopt;
}

Parser::Parser(
	not_null<String*> string,
	const QString &text,
	const TextParseOptions &options)
: Parser(
	string,
	PrepareRichFromPlain(text, options),
	options,
	ReadyToken()) {
}

Parser::Parser(
	not_null<String*> string,
	const TextWithEntities &textWithEntities,
	const TextParseOptions &options)
: Parser(
	string,
	PrepareRichFromRich(textWithEntities, options),
	options,
	ReadyToken()) {
}

Parser::Parser(
	not_null<String*> string,
	TextWithEntities &&source,
	const TextParseOptions &options,
	ReadyToken)
: _t(string)
, _source(std::move(source))
, _start(_source.text.constData())
, _end(_start + _source.text.size())
, _ptr(_start)
, _entitiesEnd(_source.entities.end())
, _waitingEntity(_source.entities.begin())
, _rich(options.flags & TextParseRichText)
, _multiline(options.flags & TextParseMultiline)
, _stopAfterWidth(ComputeStopAfter(options, *_t->_st))
, _checkTilde(ComputeCheckTilde(*_t->_st)) {
	parse(options);
}

void Parser::blockCreated() {
	_sumWidth += _t->_blocks.back()->f_width();
	if (_sumWidth.floor().toInt() > _stopAfterWidth) {
		_sumFinished = true;
	}
}

void Parser::createBlock(int32 skipBack) {
	if (_lnkIndex < kStringLinkIndexShift && _lnkIndex > _maxLnkIndex) {
		_maxLnkIndex = _lnkIndex;
	}

	int32 len = int32(_t->_text.size()) + skipBack - _blockStart;
	if (len > 0) {
		bool newline = !_emoji && (len == 1 && _t->_text.at(_blockStart) == QChar::LineFeed);
		if (_newlineAwaited) {
			_newlineAwaited = false;
			if (!newline) {
				_t->_text.insert(_blockStart, QChar::LineFeed);
				createBlock(skipBack - len);
			}
		}
		_lastSkipped = false;
		if (_emoji) {
			_t->_blocks.push_back(std::make_unique<EmojiBlock>(_t->_st->font, _t->_text, _blockStart, len, _flags, _lnkIndex, _emoji));
			_emoji = nullptr;
			_lastSkipped = true;
		} else if (newline) {
			_t->_blocks.push_back(std::make_unique<NewlineBlock>(_t->_st->font, _t->_text, _blockStart, len, _flags, _lnkIndex));
		} else {
			_t->_blocks.push_back(std::make_unique<TextBlock>(_t->_st->font, _t->_text, _t->_minResizeWidth, _blockStart, len, _flags, _lnkIndex));
		}
		_blockStart += len;
		blockCreated();
	}
}

void Parser::createSkipBlock(int32 w, int32 h) {
	createBlock();
	_t->_text.push_back('_');
	_t->_blocks.push_back(std::make_unique<SkipBlock>(_t->_st->font, _t->_text, _blockStart++, w, h, _lnkIndex));
	blockCreated();
}

void Parser::createNewlineBlock() {
	createBlock();
	_t->_text.push_back(QChar::LineFeed);
	createBlock();
}

bool Parser::checkCommand() {
	bool result = false;
	for (QChar c = ((_ptr < _end) ? *_ptr : 0); c == TextCommand; c = ((_ptr < _end) ? *_ptr : 0)) {
		if (!readCommand()) {
			break;
		}
		result = true;
	}
	return result;
}

void Parser::finishEntities() {
	while (!_startedEntities.empty()
		&& (_ptr >= _startedEntities.begin()->first || _ptr >= _end)) {
		auto list = std::move(_startedEntities.begin()->second);
		_startedEntities.erase(_startedEntities.begin());

		while (!list.empty()) {
			if (const auto flags = list.back().flags()) {
				if (_flags & (*flags)) {
					createBlock();
					_flags &= ~(*flags);
					if (((*flags) & TextBlockFPre)
						&& !_t->_blocks.empty()
						&& _t->_blocks.back()->type() != TextBlockTNewline) {
						_newlineAwaited = true;
					}
				}
			} else if (const auto lnkIndex = list.back().lnkIndex()) {
				if (_lnkIndex == *lnkIndex) {
					createBlock();
					_lnkIndex = 0;
				}
			}
			list.pop_back();
		}
	}
}

// Returns true if at least one entity was parsed in the current position.
bool Parser::checkEntities() {
	finishEntities();
	skipPassedEntities();
	if (_waitingEntity == _entitiesEnd
		|| _ptr < _start + _waitingEntity->offset()) {
		return false;
	}

	auto flags = TextBlockFlags();
	auto link = TextLinkData();
	const auto entityType = _waitingEntity->type();
	const auto entityLength = _waitingEntity->length();
	const auto entityBegin = _start + _waitingEntity->offset();
	const auto entityEnd = entityBegin + entityLength;
	if (entityType == EntityType::Bold) {
		flags = TextBlockFSemibold;
	} else if (entityType == EntityType::Italic) {
		flags = TextBlockFItalic;
	} else if (entityType == EntityType::Underline) {
		flags = TextBlockFUnderline;
	} else if (entityType == EntityType::StrikeOut) {
		flags = TextBlockFStrikeOut;
	} else if (entityType == EntityType::Code) { // #TODO entities
		flags = TextBlockFCode;
	} else if (entityType == EntityType::Pre) {
		flags = TextBlockFPre;
		createBlock();
		if (!_t->_blocks.empty() && _t->_blocks.back()->type() != TextBlockTNewline) {
			createNewlineBlock();
		}
	} else if (entityType == EntityType::Url
		|| entityType == EntityType::Email
		|| entityType == EntityType::Mention
		|| entityType == EntityType::Hashtag
		|| entityType == EntityType::Cashtag
		|| entityType == EntityType::BotCommand) {
		link.type = entityType;
		link.data = QString(entityBegin, entityLength);
		if (link.type == EntityType::Url) {
			computeLinkText(link.data, &link.text, &link.displayStatus);
		} else {
			link.text = link.data;
		}
	} else if (entityType == EntityType::CustomUrl
		|| entityType == EntityType::MentionName) {
		link.type = entityType;
		link.data = _waitingEntity->data();
		link.text = QString(_start + _waitingEntity->offset(), _waitingEntity->length());
	}

	if (link.type != EntityType::Invalid) {
		createBlock();

		_links.push_back(link);
		_lnkIndex = kStringLinkIndexShift + _links.size();

		_startedEntities[entityEnd].emplace_back(_lnkIndex);
	} else if (flags) {
		if (!(_flags & flags)) {
			createBlock();
			_flags |= flags;
			_startedEntities[entityEnd].emplace_back(flags);
		}
	}

	++_waitingEntity;
	skipBadEntities();
	return true;
}

void Parser::skipPassedEntities() {
	while (_waitingEntity != _entitiesEnd
		&& _start + _waitingEntity->offset() + _waitingEntity->length() <= _ptr) {
		++_waitingEntity;
	}
}

void Parser::skipBadEntities() {
	if (_links.size() >= 0x7FFF) {
		while (_waitingEntity != _entitiesEnd
			&& (isLinkEntity(*_waitingEntity)
				|| isInvalidEntity(*_waitingEntity))) {
			++_waitingEntity;
		}
	} else {
		while (_waitingEntity != _entitiesEnd && isInvalidEntity(*_waitingEntity)) {
			++_waitingEntity;
		}
	}
}

bool Parser::readSkipBlockCommand() {
	const QChar *afterCmd = textSkipCommand(_ptr, _end, _links.size() < 0x7FFF);
	if (afterCmd == _ptr) {
		return false;
	}

	ushort cmd = (++_ptr)->unicode();
	++_ptr;

	switch (cmd) {
	case TextCommandSkipBlock:
		createSkipBlock(_ptr->unicode(), (_ptr + 1)->unicode());
	break;
	}

	_ptr = afterCmd;
	return true;
}

bool Parser::readCommand() {
	const QChar *afterCmd = textSkipCommand(_ptr, _end, _links.size() < 0x7FFF);
	if (afterCmd == _ptr) {
		return false;
	}

	ushort cmd = (++_ptr)->unicode();
	++_ptr;

	switch (cmd) {
	case TextCommandBold:
		if (!(_flags & TextBlockFBold)) {
			createBlock();
			_flags |= TextBlockFBold;
		}
	break;

	case TextCommandNoBold:
		if (_flags & TextBlockFBold) {
			createBlock();
			_flags &= ~TextBlockFBold;
		}
	break;

	case TextCommandSemibold:
	if (!(_flags & TextBlockFSemibold)) {
		createBlock();
		_flags |= TextBlockFSemibold;
	}
	break;

	case TextCommandNoSemibold:
	if (_flags & TextBlockFSemibold) {
		createBlock();
		_flags &= ~TextBlockFSemibold;
	}
	break;

	case TextCommandItalic:
		if (!(_flags & TextBlockFItalic)) {
			createBlock();
			_flags |= TextBlockFItalic;
		}
	break;

	case TextCommandNoItalic:
		if (_flags & TextBlockFItalic) {
			createBlock();
			_flags &= ~TextBlockFItalic;
		}
	break;

	case TextCommandUnderline:
		if (!(_flags & TextBlockFUnderline)) {
			createBlock();
			_flags |= TextBlockFUnderline;
		}
	break;

	case TextCommandNoUnderline:
		if (_flags & TextBlockFUnderline) {
			createBlock();
			_flags &= ~TextBlockFUnderline;
		}
	break;

	case TextCommandStrikeOut:
		if (!(_flags & TextBlockFStrikeOut)) {
			createBlock();
			_flags |= TextBlockFStrikeOut;
		}
		break;

	case TextCommandNoStrikeOut:
		if (_flags & TextBlockFStrikeOut) {
			createBlock();
			_flags &= ~TextBlockFStrikeOut;
		}
		break;

	case TextCommandLinkIndex:
		if (_ptr->unicode() != _lnkIndex) {
			createBlock();
			_lnkIndex = _ptr->unicode();
		}
	break;

	case TextCommandLinkText: {
		createBlock();
		int32 len = _ptr->unicode();
		_links.emplace_back(EntityType::CustomUrl, QString(), QString(++_ptr, len), LinkDisplayedFull);
		_lnkIndex = kStringLinkIndexShift + _links.size();
	} break;

	case TextCommandSkipBlock:
		createSkipBlock(_ptr->unicode(), (_ptr + 1)->unicode());
	break;
	}

	_ptr = afterCmd;
	return true;
}

void Parser::parseCurrentChar() {
	_ch = ((_ptr < _end) ? *_ptr : 0);
	_emojiLookback = 0;
	const auto isNewLine = _multiline && chIsNewline(_ch);
	const auto isSpace = chIsSpace(_ch);
	const auto isDiac = chIsDiac(_ch);
	const auto isTilde = _checkTilde && (_ch == '~');
	const auto skip = [&] {
		if (chIsBad(_ch) || _ch.isLowSurrogate()) {
			return true;
		} else if (_ch == 0xFE0F && Platform::IsMac()) {
			// Some sequences like 0x0E53 0xFE0F crash OS X harfbuzz text processing :(
			return true;
		} else if (isDiac) {
			if (_lastSkipped || _emoji || ++_diacs > chMaxDiacAfterSymbol()) {
				return true;
			}
		} else if (_ch.isHighSurrogate()) {
			if (_ptr + 1 >= _end || !(_ptr + 1)->isLowSurrogate()) {
				return true;
			}
		}
		return false;
	}();

	if (_ch.isHighSurrogate() && !skip) {
		_t->_text.push_back(_ch);
		++_ptr;
		_ch = *_ptr;
		_emojiLookback = 1;
	}

	_lastSkipped = skip;
	if (skip) {
		_ch = 0;
	} else {
		if (isTilde) { // tilde fix in OpenSans
			if (!(_flags & TextBlockFTilde)) {
				createBlock(-_emojiLookback);
				_flags |= TextBlockFTilde;
			}
		} else {
			if (_flags & TextBlockFTilde) {
				createBlock(-_emojiLookback);
				_flags &= ~TextBlockFTilde;
			}
		}
		if (isNewLine) {
			createNewlineBlock();
		} else if (isSpace) {
			_t->_text.push_back(QChar::Space);
		} else {
			if (_emoji) {
				createBlock(-_emojiLookback);
			}
			_t->_text.push_back(_ch);
		}
		if (!isDiac) _diacs = 0;
	}
}

void Parser::parseEmojiFromCurrent() {
	int len = 0;
	auto e = Ui::Emoji::Find(_ptr - _emojiLookback, _end, &len);
	if (!e) return;

	for (int l = len - _emojiLookback - 1; l > 0; --l) {
		_t->_text.push_back(*++_ptr);
	}
	if (e->hasPostfix()) {
		Assert(!_t->_text.isEmpty());
		const auto last = _t->_text[_t->_text.size() - 1];
		if (last.unicode() != Ui::Emoji::kPostfix) {
			_t->_text.push_back(QChar(Ui::Emoji::kPostfix));
			++len;
		}
	}

	createBlock(-len);
	_emoji = e;
}

bool Parser::isInvalidEntity(const EntityInText &entity) const {
	const auto length = entity.length();
	return (_start + entity.offset() + length > _end) || (length <= 0);
}

bool Parser::isLinkEntity(const EntityInText &entity) const {
	const auto type = entity.type();
	const auto urls = {
		EntityType::Url,
		EntityType::CustomUrl,
		EntityType::Email,
		EntityType::Hashtag,
		EntityType::Cashtag,
		EntityType::Mention,
		EntityType::MentionName,
		EntityType::BotCommand
	};
	return ranges::find(urls, type) != std::end(urls);
}

void Parser::parse(const TextParseOptions &options) {
	skipBadEntities();
	trimSourceRange();

	_t->_text.resize(0);
	_t->_text.reserve(_end - _ptr);

	for (; _ptr <= _end; ++_ptr) {
		while (checkEntities() || (_rich && checkCommand())) {
		}
		parseCurrentChar();
		parseEmojiFromCurrent();

		if (_sumFinished || _t->_text.size() >= 0x8000) {
			break; // 32k max
		}
	}
	createBlock();
	checkForElidedSkipBlock();
	finalize(options);
}

void Parser::trimSourceRange() {
	const auto firstMonospaceOffset = EntityInText::FirstMonospaceOffset(
		_source.entities,
		_end - _start);

	while (_ptr != _end && chIsTrimmed(*_ptr, _rich) && _ptr != _start + firstMonospaceOffset) {
		++_ptr;
	}
	while (_ptr != _end && chIsTrimmed(*(_end - 1), _rich)) {
		--_end;
	}
}

void Parser::checkForElidedSkipBlock() {
	if (!_sumFinished || !_rich) {
		return;
	}
	// We could've skipped the final skip block command.
	for (; _ptr < _end; ++_ptr) {
		if (*_ptr == TextCommand && readSkipBlockCommand()) {
			break;
		}
	}
}

void Parser::finalize(const TextParseOptions &options) {
	_t->_links.resize(_maxLnkIndex);
	for (const auto &block : _t->_blocks) {
		const auto b = block.get();
		const auto shiftedIndex = b->lnkIndex();
		if (shiftedIndex <= kStringLinkIndexShift) {
			continue;
		}
		const auto realIndex = (shiftedIndex - kStringLinkIndexShift);
		const auto index = _maxLnkIndex + realIndex;
		b->setLnkIndex(index);
		if (_t->_links.size() >= index) {
			continue;
		}

		_t->_links.resize(index);
		const auto handler = CreateHandlerForLink(
			_links[realIndex - 1],
			options);
		if (handler) {
			_t->setLink(index, handler);
		}
	}
	_t->_links.squeeze();
	_t->_blocks.shrink_to_fit();
	_t->_text.squeeze();
}

void Parser::computeLinkText(const QString &linkData, QString *outLinkText, LinkDisplayStatus *outDisplayStatus) {
	auto url = QUrl(linkData);
	auto good = QUrl(url.isValid()
		? url.toEncoded()
		: QByteArray());
	auto readable = good.isValid()
		? good.toDisplayString()
		: linkData;
	*outLinkText = _t->_st->font->elided(readable, st::linkCropLimit);
	*outDisplayStatus = (*outLinkText == readable) ? LinkDisplayedFull : LinkDisplayedElided;
}

ClickHandlerPtr Parser::CreateHandlerForLink(
		const TextLinkData &link,
		const TextParseOptions &options) {
	switch (link.type) {
	case EntityType::CustomUrl:
		return !link.data.isEmpty()
			? std::make_shared<HiddenUrlClickHandler>(link.data)
			: nullptr;

	case EntityType::Email:
	case EntityType::Url:
		return std::make_shared<UrlClickHandler>(
			link.data,
			link.displayStatus == LinkDisplayedFull);

	case EntityType::BotCommand:
		return std::make_shared<BotCommandClickHandler>(link.data);

	case EntityType::Hashtag:
		if (options.flags & TextTwitterMentions) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://twitter.com/hashtag/")
					+ link.data.mid(1)
					+ qsl("?src=hash")),
				true);
		} else if (options.flags & TextInstagramMentions) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://instagram.com/explore/tags/")
					+ link.data.mid(1)
					+ '/'),
				true);
		}
		return std::make_shared<HashtagClickHandler>(link.data);

	case EntityType::Cashtag:
		return std::make_shared<CashtagClickHandler>(link.data);

	case EntityType::Mention:
		if (options.flags & TextTwitterMentions) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://twitter.com/") + link.data.mid(1),
				true);
		} else if (options.flags & TextInstagramMentions) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://instagram.com/") + link.data.mid(1) + '/',
				true);
		}
		return std::make_shared<MentionClickHandler>(link.data);

	case EntityType::MentionName: {
		auto fields = TextUtilities::MentionNameDataToFields(link.data);
		if (fields.userId) {
			return std::make_shared<MentionNameClickHandler>(
				link.text,
				fields.userId,
				fields.accessHash);
		} else {
			LOG(("Bad mention name: %1").arg(link.data));
		}
	} break;
	}
	return nullptr;
}

namespace {

// COPIED FROM qtextengine.cpp AND MODIFIED

struct BidiStatus {
	BidiStatus() {
		eor = QChar::DirON;
		lastStrong = QChar::DirON;
		last = QChar:: DirON;
		dir = QChar::DirON;
	}
	QChar::Direction eor;
	QChar::Direction lastStrong;
	QChar::Direction last;
	QChar::Direction dir;
};

enum { _MaxBidiLevel = 61 };
enum { _MaxItemLength = 4096 };

struct BidiControl {
	inline BidiControl(bool rtl)
		: base(rtl ? 1 : 0), level(rtl ? 1 : 0) {}

	inline void embed(bool rtl, bool o = false) {
		unsigned int toAdd = 1;
		if((level%2 != 0) == rtl ) {
			++toAdd;
		}
		if (level + toAdd <= _MaxBidiLevel) {
			ctx[cCtx].level = level;
			ctx[cCtx].override = override;
			cCtx++;
			override = o;
			level += toAdd;
		}
	}
	inline bool canPop() const { return cCtx != 0; }
	inline void pdf() {
		Q_ASSERT(cCtx);
		--cCtx;
		level = ctx[cCtx].level;
		override = ctx[cCtx].override;
	}

	inline QChar::Direction basicDirection() const {
		return (base ? QChar::DirR : QChar:: DirL);
	}
	inline unsigned int baseLevel() const {
		return base;
	}
	inline QChar::Direction direction() const {
		return ((level%2) ? QChar::DirR : QChar:: DirL);
	}

	struct {
		unsigned int level = 0;
		bool override = false;
	} ctx[_MaxBidiLevel];
	unsigned int cCtx = 0;
	const unsigned int base;
	unsigned int level;
	bool override = false;
};

static void eAppendItems(QScriptAnalysis *analysis, int &start, int &stop, const BidiControl &control, QChar::Direction dir) {
	if (start > stop)
		return;

	int level = control.level;

	if(dir != QChar::DirON && !control.override) {
		// add level of run (cases I1 & I2)
		if(level % 2) {
			if(dir == QChar::DirL || dir == QChar::DirAN || dir == QChar::DirEN)
				level++;
		} else {
			if(dir == QChar::DirR)
				level++;
			else if(dir == QChar::DirAN || dir == QChar::DirEN)
				level += 2;
		}
	}

	QScriptAnalysis *s = analysis + start;
	const QScriptAnalysis *e = analysis + stop;
	while (s <= e) {
		s->bidiLevel = level;
		++s;
	}
	++stop;
	start = stop;
}

inline int32 countBlockHeight(const AbstractBlock *b, const style::TextStyle *st) {
	return (b->type() == TextBlockTSkip) ? static_cast<const SkipBlock*>(b)->height() : (st->lineHeight > st->font->height) ? st->lineHeight : st->font->height;
}

} // namespace

class Renderer {
public:
	Renderer(Painter *p, const String *t)
	: _p(p)
	, _t(t)
	, _originalPen(p ? p->pen() : QPen()) {
	}

	~Renderer() {
		restoreAfterElided();
		if (_p) {
			_p->setPen(_originalPen);
		}
	}

	void draw(int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection = { 0, 0 }, bool fullWidthSelection = true) {
		if (_t->isEmpty()) return;

		_blocksSize = _t->_blocks.size();
		if (_p) {
			_p->setFont(_t->_st->font);
			_textPalette = &_p->textPalette();
			_originalPenSelected = (_textPalette->selectFg->c.alphaF() == 0) ? _originalPen : _textPalette->selectFg->p;
		}

		_x = left;
		_y = top;
		_yFrom = yFrom + top;
		_yTo = (yTo < 0) ? -1 : (yTo + top);
		_selection = selection;
		_fullWidthSelection = fullWidthSelection;
		_wLeft = _w = w;
		if (_elideLast) {
			_yToElide = _yTo;
			if (_elideRemoveFromEnd > 0 && !_t->_blocks.empty()) {
				int firstBlockHeight = countBlockHeight(_t->_blocks.front().get(), _t->_st);
				if (_y + firstBlockHeight >= _yToElide) {
					_wLeft -= _elideRemoveFromEnd;
				}
			}
		}
		_str = _t->_text.unicode();

		if (_p) {
			auto clip = _p->hasClipping() ? _p->clipBoundingRect() : QRect();
			if (clip.width() > 0 || clip.height() > 0) {
				if (_yFrom < clip.y()) _yFrom = clip.y();
				if (_yTo < 0 || _yTo > clip.y() + clip.height()) _yTo = clip.y() + clip.height();
			}
		}

		_align = align;

		_parDirection = _t->_startDir;
		if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = cLangDir();
		if ((*_t->_blocks.cbegin())->type() != TextBlockTNewline) {
			initNextParagraph(_t->_blocks.cbegin());
		}

		_lineStart = 0;
		_lineStartBlock = 0;

		_lineHeight = 0;
		_fontHeight = _t->_st->font->height;
		auto last_rBearing = QFixed(0);
		_last_rPadding = QFixed(0);

		auto blockIndex = 0;
		bool longWordLine = true;
		auto e = _t->_blocks.cend();
		for (auto i = _t->_blocks.cbegin(); i != e; ++i, ++blockIndex) {
			auto b = i->get();
			auto _btype = b->type();
			auto blockHeight = countBlockHeight(b, _t->_st);

			if (_btype == TextBlockTNewline) {
				if (!_lineHeight) _lineHeight = blockHeight;
				if (!drawLine((*i)->from(), i, e)) {
					return;
				}

				_y += _lineHeight;
				_lineHeight = 0;
				_lineStart = _t->countBlockEnd(i, e);
				_lineStartBlock = blockIndex + 1;

				last_rBearing = b->f_rbearing();
				_last_rPadding = b->f_rpadding();
				_wLeft = _w - (b->f_width() - last_rBearing);
				if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
					_wLeft -= _elideRemoveFromEnd;
				}

				_parDirection = static_cast<NewlineBlock*>(b)->nextDirection();
				if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = cLangDir();
				initNextParagraph(i + 1);

				longWordLine = true;
				continue;
			}

			auto b__f_rbearing = b->f_rbearing();
			auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + b->f_width() - b__f_rbearing);
			if (newWidthLeft >= 0) {
				last_rBearing = b__f_rbearing;
				_last_rPadding = b->f_rpadding();
				_wLeft = newWidthLeft;

				_lineHeight = qMax(_lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			if (_btype == TextBlockTText) {
				auto t = static_cast<TextBlock*>(b);
				if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
					_last_rPadding += b->f_rpadding();

					_lineHeight = qMax(_lineHeight, blockHeight);

					longWordLine = false;
					continue;
				}

				auto f_wLeft = _wLeft; // vars for saving state of the last word start
				auto f_lineHeight = _lineHeight; // f points to the last word-start element of t->_words
				for (auto j = t->_words.cbegin(), en = t->_words.cend(), f = j; j != en; ++j) {
					auto wordEndsHere = (j->f_width() >= 0);
					auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

					auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + j_width - j->f_rbearing());
					if (newWidthLeft >= 0) {
						last_rBearing = j->f_rbearing();
						_last_rPadding = j->f_rpadding();
						_wLeft = newWidthLeft;

						_lineHeight = qMax(_lineHeight, blockHeight);

						if (wordEndsHere) {
							longWordLine = false;
						}
						if (wordEndsHere || longWordLine) {
							f = j + 1;
							f_wLeft = _wLeft;
							f_lineHeight = _lineHeight;
						}
						continue;
					}

					auto elidedLineHeight = qMax(_lineHeight, blockHeight);
					auto elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
					if (elidedLine) {
						_lineHeight = elidedLineHeight;
					} else if (f != j && !_breakEverywhere) {
						// word did not fit completely, so we roll back the state to the beginning of this long word
						j = f;
						_wLeft = f_wLeft;
						_lineHeight = f_lineHeight;
						j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
					}
					if (!drawLine(elidedLine ? ((j + 1 == en) ? _t->countBlockEnd(i, e) : (j + 1)->from()) : j->from(), i, e)) {
						return;
					}
					_y += _lineHeight;
					_lineHeight = qMax(0, blockHeight);
					_lineStart = j->from();
					_lineStartBlock = blockIndex;

					last_rBearing = j->f_rbearing();
					_last_rPadding = j->f_rpadding();
					_wLeft = _w - (j_width - last_rBearing);
					if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
						_wLeft -= _elideRemoveFromEnd;
					}

					longWordLine = true;
					f = j + 1;
					f_wLeft = _wLeft;
					f_lineHeight = _lineHeight;
				}
				continue;
			}

			auto elidedLineHeight = qMax(_lineHeight, blockHeight);
			auto elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
			if (elidedLine) {
				_lineHeight = elidedLineHeight;
			}
			if (!drawLine(elidedLine ? _t->countBlockEnd(i, e) : b->from(), i, e)) {
				return;
			}
			_y += _lineHeight;
			_lineHeight = qMax(0, blockHeight);
			_lineStart = b->from();
			_lineStartBlock = blockIndex;

			last_rBearing = b__f_rbearing;
			_last_rPadding = b->f_rpadding();
			_wLeft = _w - (b->f_width() - last_rBearing);
			if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
				_wLeft -= _elideRemoveFromEnd;
			}

			longWordLine = true;
			continue;
		}
		if (_lineStart < _t->_text.size()) {
			if (!drawLine(_t->_text.size(), e, e)) return;
		}
		if (!_p && _lookupSymbol) {
			_lookupResult.symbol = _t->_text.size();
			_lookupResult.afterSymbol = false;
		}
	}

	void drawElided(int32 left, int32 top, int32 w, style::align align, int32 lines, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) {
		if (lines <= 0 || _t->isNull()) return;

		if (yTo < 0 || (lines - 1) * _t->_st->font->height < yTo) {
			yTo = lines * _t->_st->font->height;
			_elideLast = true;
			_elideRemoveFromEnd = removeFromEnd;
		}
		_breakEverywhere = breakEverywhere;
		draw(left, top, w, align, yFrom, yTo, selection);
	}

	StateResult getState(QPoint point, int w, StateRequest request) {
		if (!_t->isNull() && point.y() >= 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & StateRequest::Flag::LookupLink);
			if (_lookupSymbol || (_lookupX >= 0 && _lookupX < w)) {
				draw(0, 0, w, _lookupRequest.align, _lookupY, _lookupY + 1);
			}
		}
		return _lookupResult;
	}

	StateResult getStateElided(QPoint point, int w, StateRequestElided request) {
		if (!_t->isNull() && point.y() >= 0 && request.lines > 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & StateRequest::Flag::LookupLink);
			if (_lookupSymbol || (_lookupX >= 0 && _lookupX < w)) {
				int yTo = _lookupY + 1;
				if (yTo < 0 || (request.lines - 1) * _t->_st->font->height < yTo) {
					yTo = request.lines * _t->_st->font->height;
					_elideLast = true;
					_elideRemoveFromEnd = request.removeFromEnd;
				}
				draw(0, 0, w, _lookupRequest.align, _lookupY, _lookupY + 1);
			}
		}
		return _lookupResult;
	}

private:
	void initNextParagraph(String::TextBlocks::const_iterator i) {
		_parStartBlock = i;
		const auto e = _t->_blocks.cend();
		if (i == e) {
			_parStart = _t->_text.size();
			_parLength = 0;
		} else {
			_parStart = (*i)->from();
			for (; i != e; ++i) {
				if ((*i)->type() == TextBlockTNewline) {
					break;
				}
			}
			_parLength = ((i == e) ? _t->_text.size() : (*i)->from()) - _parStart;
		}
		_parAnalysis.resize(0);
	}

	void initParagraphBidi() {
		if (!_parLength || !_parAnalysis.isEmpty()) return;

		String::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		bool ignore = false;
		bool rtl = (_parDirection == Qt::RightToLeft);
		if (!ignore && !rtl) {
			ignore = true;
			const ushort *start = reinterpret_cast<const ushort*>(_str) + _parStart;
			const ushort *curr = start;
			const ushort *end = start + _parLength;
			while (curr < end) {
				while (n != e && (*n)->from() <= _parStart + (curr - start)) {
					i = n;
					++n;
				}
				if ((*i)->type() != TextBlockTEmoji && *curr >= 0x590) {
					ignore = false;
					break;
				}
				++curr;
			}
		}

		_parAnalysis.resize(_parLength);
		QScriptAnalysis *analysis = _parAnalysis.data();

		BidiControl control(rtl);

		_parHasBidi = false;
		if (ignore) {
			memset(analysis, 0, _parLength * sizeof(QScriptAnalysis));
			if (rtl) {
				for (int i = 0; i < _parLength; ++i)
					analysis[i].bidiLevel = 1;
				_parHasBidi = true;
			}
		} else {
			_parHasBidi = eBidiItemize(analysis, control);
		}
	}

	bool drawLine(uint16 _lineEnd, const String::TextBlocks::const_iterator &_endBlockIter, const String::TextBlocks::const_iterator &_end) {
		_yDelta = (_lineHeight - _fontHeight) / 2;
		if (_yTo >= 0 && (_y + _yDelta >= _yTo || _y >= _yTo)) return false;
		if (_y + _yDelta + _fontHeight <= _yFrom) {
			if (_lookupSymbol) {
				_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
				_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
			}
			return true;
		}

		// Trimming pending spaces, because they sometimes don't fit on the line.
		// They also are not counted in the line width, they're in the right padding.
		// Line width is a sum of block / word widths and paddings between them, without trailing one.
		auto trimmedLineEnd = _lineEnd;
		for (; trimmedLineEnd > _lineStart; --trimmedLineEnd) {
			auto ch = _t->_text[trimmedLineEnd - 1];
			if (ch != QChar::Space && ch != QChar::LineFeed) {
				break;
			}
		}

		auto _endBlock = (_endBlockIter == _end) ? nullptr : _endBlockIter->get();
		auto elidedLine = _elideLast && (_y + _lineHeight >= _yToElide);
		if (elidedLine) {
			// If we decided to draw the last line elided only because of the skip block
			// that did not fit on this line, we just draw the line till the very end.
			// Skip block is ignored in the elided lines, instead "removeFromEnd" is used.
			if (_endBlock && _endBlock->type() == TextBlockTSkip) {
				_endBlock = nullptr;
			}
			if (!_endBlock) {
				elidedLine = false;
			}
		}

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		const auto extendLeft = (currentBlock->from() < _lineStart)
			? qMin(_lineStart - currentBlock->from(), 2)
			: 0;
		_localFrom = _lineStart - extendLeft;
		const auto extendedLineEnd = (_endBlock && _endBlock->from() < trimmedLineEnd && !elidedLine)
			? qMin(uint16(trimmedLineEnd + 2), _t->countBlockEnd(_endBlockIter, _end))
			: trimmedLineEnd;

		auto lineText = _t->_text.mid(_localFrom, extendedLineEnd - _localFrom);
		auto lineStart = extendLeft;
		auto lineLength = trimmedLineEnd - _lineStart;

		if (elidedLine) {
			initParagraphBidi();
			prepareElidedLine(lineText, lineStart, lineLength, _endBlock);
		}

		auto x = _x;
		if (_align & Qt::AlignHCenter) {
			x += (_wLeft / 2).toInt();
		} else if (((_align & Qt::AlignLeft) && _parDirection == Qt::RightToLeft) || ((_align & Qt::AlignRight) && _parDirection == Qt::LeftToRight)) {
			x += _wLeft;
		}

		if (!_p) {
			if (_lookupX < x) {
				if (_lookupSymbol) {
					if (_parDirection == Qt::RightToLeft) {
						_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
						_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
					} else {
						_lookupResult.symbol = _lineStart;
						_lookupResult.afterSymbol = false;
//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineStart > 0)) ? true : false;
					}
				}
				if (_lookupLink) {
					_lookupResult.link = nullptr;
				}
				_lookupResult.uponSymbol = false;
				return false;
			} else if (_lookupX >= x + (_w - _wLeft)) {
				if (_parDirection == Qt::RightToLeft) {
					_lookupResult.symbol = _lineStart;
					_lookupResult.afterSymbol = false;
//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineStart > 0)) ? true : false;
				} else {
					_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
				}
				if (_lookupLink) {
					_lookupResult.link = nullptr;
				}
				_lookupResult.uponSymbol = false;
				return false;
			}
		}

		if (_fullWidthSelection) {
			const auto selectFromStart = (_selection.to > _lineStart)
				&& (_lineStart > 0)
				&& (_selection.from <= _lineStart);
			const auto selectTillEnd = (_selection.to > trimmedLineEnd)
				&& (trimmedLineEnd < _t->_text.size())
				&& (_selection.from <= trimmedLineEnd)
				&& (!_endBlock || _endBlock->type() != TextBlockTSkip);

			if ((selectFromStart && _parDirection == Qt::LeftToRight)
				|| (selectTillEnd && _parDirection == Qt::RightToLeft)) {
				if (x > _x) {
					fillSelectRange(_x, x);
				}
			}
			if ((selectTillEnd && _parDirection == Qt::LeftToRight)
				|| (selectFromStart && _parDirection == Qt::RightToLeft)) {
				if (x < _x + _wLeft) {
					fillSelectRange(x + _w - _wLeft, _x + _w);
				}
			}
		}
		if (trimmedLineEnd == _lineStart && !elidedLine) {
			return true;
		}

		if (!elidedLine) initParagraphBidi(); // if was not inited

		_f = _t->_st->font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
		if (!nItems) {
			return true;
		}

		int skipIndex = -1;
		QVarLengthArray<int> visualOrder(nItems);
		QVarLengthArray<uchar> levels(nItems);
		for (int i = 0; i < nItems; ++i) {
			auto &si = engine.layoutData->items[firstItem + i];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			auto _type = currentBlock->type();
			if (_type == TextBlockTSkip) {
				levels[i] = si.analysis.bidiLevel = 0;
				skipIndex = i;
			} else {
				levels[i] = si.analysis.bidiLevel;
			}
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
					si.width = currentBlock->f_width() + (nextBlock == _endBlock && (!nextBlock || nextBlock->from() >= trimmedLineEnd) ? 0 : currentBlock->f_rpadding());
				}
			}
		}
	    QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
		if (rtl() && skipIndex == nItems - 1) {
			for (int32 i = nItems; i > 1;) {
				--i;
				visualOrder[i] = visualOrder[i - 1];
			}
			visualOrder[0] = skipIndex;
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex].get();
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		int32 textY = _y + _yDelta + _t->_st->font->ascent, emojiY = (_t->_st->font->height - st::emojiSize) / 2;

		applyBlockProperties(currentBlock);
		for (int i = 0; i < nItems; ++i) {
			int item = firstItem + visualOrder[i];
			const QScriptItem &si = engine.layoutData->items.at(item);
			bool rtl = (si.analysis.bidiLevel % 2);

			while (blockIndex > _lineStartBlock + 1 && _t->_blocks[blockIndex - 1]->from() > _localFrom + si.position) {
				nextBlock = currentBlock;
				currentBlock = _t->_blocks[--blockIndex - 1].get();
				applyBlockProperties(currentBlock);
			}
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				applyBlockProperties(currentBlock);
			}
			if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
				TextBlockType _type = currentBlock->type();
				if (!_p && _lookupX >= x && _lookupX < x + si.width) { // _lookupRequest
					if (_lookupLink) {
						if (currentBlock->lnkIndex() && _lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
							_lookupResult.link = _t->_links.at(currentBlock->lnkIndex() - 1);
						}
					}
					if (_type != TextBlockTSkip) {
						_lookupResult.uponSymbol = true;
					}
					if (_lookupSymbol) {
						if (_type == TextBlockTSkip) {
							if (_parDirection == Qt::RightToLeft) {
								_lookupResult.symbol = _lineStart;
								_lookupResult.afterSymbol = false;
							} else {
								_lookupResult.symbol = (trimmedLineEnd > _lineStart) ? (trimmedLineEnd - 1) : _lineStart;
								_lookupResult.afterSymbol = (trimmedLineEnd > _lineStart) ? true : false;
							}
							return false;
						}

						// Emoji with spaces after symbol lookup
						auto chFrom = _str + currentBlock->from();
						auto chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						auto spacesWidth = (si.width - currentBlock->f_width());
						auto spacesCount = 0;
						while (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
							++spacesCount;
							--chTo;
						}
						if (spacesCount > 0) { // Check if we're over a space.
							if (rtl) {
								if (_lookupX < x + spacesWidth) {
									_lookupResult.symbol = (chTo - _str); // up to a space, included, rtl
									_lookupResult.afterSymbol = (_lookupX < x + (spacesWidth / 2)) ? true : false;
									return false;
								}
							} else if (_lookupX >= x + si.width - spacesWidth) {
								_lookupResult.symbol = (chTo - _str); // up to a space, inclided, ltr
								_lookupResult.afterSymbol = (_lookupX >= x + si.width - spacesWidth + (spacesWidth / 2)) ? true : false;
								return false;
							}
						}
						if (_lookupX < x + (rtl ? (si.width - currentBlock->f_width()) : 0) + (currentBlock->f_width() / 2)) {
							_lookupResult.symbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
							_lookupResult.afterSymbol = (rtl && chTo > chFrom) ? true : false;
						} else {
							_lookupResult.symbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
							_lookupResult.afterSymbol = (rtl || chTo <= chFrom) ? false : true;
						}
					}
					return false;
				} else if (_p && _type == TextBlockTEmoji) {
					auto glyphX = x;
					auto spacesWidth = (si.width - currentBlock->f_width());
					if (rtl) {
						glyphX += spacesWidth;
					}
					if (_localFrom + si.position < _selection.to) {
						auto chFrom = _str + currentBlock->from();
						auto chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						if (_localFrom + si.position >= _selection.from) { // could be without space
							if (chTo == chFrom || (chTo - 1)->unicode() != QChar::Space || _selection.to >= (chTo - _str)) {
								fillSelectRange(x, x + si.width);
							} else { // or with space
								fillSelectRange(glyphX, glyphX + currentBlock->f_width());
							}
						} else if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space && (chTo - 1 - _str) >= _selection.from) {
							if (rtl) { // rtl space only
								fillSelectRange(x, glyphX);
							} else { // ltr space only
								fillSelectRange(x + currentBlock->f_width(), x + si.width);
							}
						}
					}
					Ui::Emoji::Draw(
						*_p,
						static_cast<EmojiBlock*>(currentBlock)->emoji,
						Ui::Emoji::GetSizeNormal(),
						(glyphX + st::emojiPadding).toInt(),
						_y + _yDelta + emojiY);
//				} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
//					_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
				}
				x += si.width;
				continue;
			}

			unsigned short *logClusters = engine.logClusters(&si);
			QGlyphLayout glyphs = engine.shapedGlyphs(&si);

			int itemStart = qMax(line.from, si.position), itemEnd;
			int itemLength = engine.length(item);
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (line.from + line.length < si.position + itemLength) {
				itemEnd = line.from + line.length;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			QFixed itemWidth = 0;
			for (int g = glyphsStart; g < glyphsEnd; ++g)
				itemWidth += glyphs.effectiveAdvance(g);

			if (!_p && _lookupX >= x && _lookupX < x + itemWidth) { // _lookupRequest
				if (_lookupLink) {
					if (currentBlock->lnkIndex() && _lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
						_lookupResult.link = _t->_links.at(currentBlock->lnkIndex() - 1);
					}
				}
				_lookupResult.uponSymbol = true;
				if (_lookupSymbol) {
					QFixed tmpx = rtl ? (x + itemWidth) : x;
					for (int ch = 0, g, itemL = itemEnd - itemStart; ch < itemL;) {
						g = logClusters[itemStart - si.position + ch];
						QFixed gwidth = glyphs.effectiveAdvance(g);
						// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
						int ch2 = ch + 1;
						while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
							++ch2;
						}
						for (int charsCount = (ch2 - ch); ch < ch2; ++ch) {
							QFixed shift1 = QFixed(2 * (charsCount - (ch2 - ch)) + 2) * gwidth / QFixed(2 * charsCount),
								shift2 = QFixed(2 * (charsCount - (ch2 - ch)) + 1) * gwidth / QFixed(2 * charsCount);
							if ((rtl && _lookupX >= tmpx - shift1) ||
								(!rtl && _lookupX < tmpx + shift1)) {
								_lookupResult.symbol = _localFrom + itemStart + ch;
								if ((rtl && _lookupX >= tmpx - shift2) ||
									(!rtl && _lookupX < tmpx + shift2)) {
									_lookupResult.afterSymbol = false;
								} else {
									_lookupResult.afterSymbol = true;
								}
								return false;
							}
						}
						if (rtl) {
							tmpx -= gwidth;
						} else {
							tmpx += gwidth;
						}
					}
					if (itemEnd > itemStart) {
						_lookupResult.symbol = _localFrom + itemEnd - 1;
						_lookupResult.afterSymbol = true;
					} else {
						_lookupResult.symbol = _localFrom + itemStart;
						_lookupResult.afterSymbol = false;
					}
				}
				return false;
			} else if (_p) {
				QTextCharFormat format;
				QTextItemInt gf(glyphs.mid(glyphsStart, glyphsEnd - glyphsStart),
								&_e->fnt, engine.layoutData->string.unicode() + itemStart,
								itemEnd - itemStart, engine.fontEngine(si), format);
				gf.logClusters = logClusters + itemStart - si.position;
				gf.width = itemWidth;
				gf.justified = false;
				gf.initWithScriptItem(si);

				auto hasSelected = false;
				auto hasNotSelected = true;
				auto selectedRect = QRect();
				if (_localFrom + itemStart < _selection.to && _localFrom + itemEnd > _selection.from) {
					hasSelected = true;
					auto selX = x;
					auto selWidth = itemWidth;
					if (_localFrom + itemStart >= _selection.from && _localFrom + itemEnd <= _selection.to) {
						hasNotSelected = false;
					} else {
						selWidth = 0;
						int itemL = itemEnd - itemStart;
						int selStart = _selection.from - (_localFrom + itemStart), selEnd = _selection.to - (_localFrom + itemStart);
						if (selStart < 0) selStart = 0;
						if (selEnd > itemL) selEnd = itemL;
						for (int ch = 0, g; ch < selEnd;) {
							g = logClusters[itemStart - si.position + ch];
							QFixed gwidth = glyphs.effectiveAdvance(g);
							// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
							int ch2 = ch + 1;
							while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
								++ch2;
							}
							if (ch2 <= selStart) {
								selX += gwidth;
							} else if (ch >= selStart && ch2 <= selEnd) {
								selWidth += gwidth;
							} else {
								int sStart = ch, sEnd = ch2;
								if (ch < selStart) {
									sStart = selStart;
									selX += QFixed(sStart - ch) * gwidth / QFixed(ch2 - ch);
								}
								if (ch2 >= selEnd) {
									sEnd = selEnd;
									selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
									break;
								}
								selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
							}
							ch = ch2;
						}
					}
					if (rtl) selX = x + itemWidth - (selX - x) - selWidth;
					selectedRect = QRect(selX.toInt(), _y + _yDelta, (selX + selWidth).toInt() - selX.toInt(), _fontHeight);
					fillSelectRange(selX, selX + selWidth);
				}
				if (Q_UNLIKELY(hasSelected)) {
					if (Q_UNLIKELY(hasNotSelected)) {
						auto clippingEnabled = _p->hasClipping();
						auto clippingRegion = _p->clipRegion();
						_p->setClipRect(selectedRect, Qt::IntersectClip);
						_p->setPen(*_currentPenSelected);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
						auto externalClipping = clippingEnabled ? clippingRegion : QRegion(QRect((_x - _w).toInt(), _y - _lineHeight, (_x + 2 * _w).toInt(), _y + 2 * _lineHeight));
						_p->setClipRegion(externalClipping - selectedRect);
						_p->setPen(*_currentPen);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
						if (clippingEnabled) {
							_p->setClipRegion(clippingRegion);
						} else {
							_p->setClipping(false);
						}
					} else {
						_p->setPen(*_currentPenSelected);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
					}
				} else {
					_p->setPen(*_currentPen);
					_p->drawTextItem(QPointF(x.toReal(), textY), gf);
				}
			}

			x += itemWidth;
		}
		return true;
	}
	void fillSelectRange(QFixed from, QFixed to) {
		auto left = from.toInt();
		auto width = to.toInt() - left;
		_p->fillRect(left, _y + _yDelta, width, _fontHeight, _textPalette->selectBg);
	}

	void elideSaveBlock(int32 blockIndex, AbstractBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
		if (_elideSavedBlock) {
			restoreAfterElided();
		}

		_elideSavedIndex = blockIndex;
		auto mutableText = const_cast<String*>(_t);
		_elideSavedBlock = std::move(mutableText->_blocks[blockIndex]);
		mutableText->_blocks[blockIndex] = std::make_unique<TextBlock>(_t->_st->font, _t->_text, QFIXED_MAX, elideStart, 0, _elideSavedBlock->flags(), _elideSavedBlock->lnkIndex());
		_blocksSize = blockIndex + 1;
		_endBlock = (blockIndex + 1 < _t->_blocks.size() ? _t->_blocks[blockIndex + 1].get() : nullptr);
	}

	void setElideBidi(int32 elideStart, int32 elideLen) {
		int32 newParLength = elideStart + elideLen - _parStart;
		if (newParLength > _parAnalysis.size()) {
			_parAnalysis.resize(newParLength);
		}
		for (int32 i = elideLen; i > 0; --i) {
			_parAnalysis[newParLength - i].bidiLevel = (_parDirection == Qt::RightToLeft) ? 1 : 0;
		}
	}

	void prepareElidedLine(QString &lineText, int32 lineStart, int32 &lineLength, AbstractBlock *&_endBlock, int repeat = 0) {
		static const QString _Elide = qsl("...");

		_f = _t->_st->font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		auto elideWidth = _f->elidew;
		_wLeft = _w - elideWidth - _elideRemoveFromEnd;

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0, i;

		for (i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			TextBlockType _type = currentBlock->type();
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
					si.width = currentBlock->f_width() + currentBlock->f_rpadding();
				}
			}
			if (_type == TextBlockTEmoji || _type == TextBlockTSkip || _type == TextBlockTNewline) {
				if (_wLeft < si.width) {
					lineText = lineText.mid(0, currentBlock->from() - _localFrom) + _Elide;
					lineLength = currentBlock->from() + _Elide.size() - _lineStart;
					_selection.to = qMin(_selection.to, currentBlock->from());
					setElideBidi(currentBlock->from(), _Elide.size());
					elideSaveBlock(blockIndex - 1, _endBlock, currentBlock->from(), elideWidth);
					return;
				}
				_wLeft -= si.width;
			} else if (_type == TextBlockTText) {
				unsigned short *logClusters = engine.logClusters(&si);
				QGlyphLayout glyphs = engine.shapedGlyphs(&si);

				int itemStart = qMax(line.from, si.position), itemEnd;
				int itemLength = engine.length(firstItem + i);
				int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
				if (line.from + line.length < si.position + itemLength) {
					itemEnd = line.from + line.length;
					glyphsEnd = logClusters[itemEnd - si.position];
				} else {
					itemEnd = si.position + itemLength;
					glyphsEnd = si.num_glyphs;
				}

				for (auto g = glyphsStart; g < glyphsEnd; ++g) {
					auto adv = glyphs.effectiveAdvance(g);
					if (_wLeft < adv) {
						auto pos = itemStart;
						while (pos < itemEnd && logClusters[pos - si.position] < g) {
							++pos;
						}

						if (lineText.size() <= pos || repeat > 3) {
							lineText += _Elide;
							lineLength = _localFrom + pos + _Elide.size() - _lineStart;
							_selection.to = qMin(_selection.to, uint16(_localFrom + pos));
							setElideBidi(_localFrom + pos, _Elide.size());
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
						} else {
							lineText = lineText.mid(0, pos);
							lineLength = _localFrom + pos - _lineStart;
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
							prepareElidedLine(lineText, lineStart, lineLength, _endBlock, repeat + 1);
						}
						return;
					} else {
						_wLeft -= adv;
					}
				}
			}
		}

		int32 elideStart = _localFrom + lineText.size();
		_selection.to = qMin(_selection.to, uint16(elideStart));
		setElideBidi(elideStart, _Elide.size());

		lineText += _Elide;
		lineLength += _Elide.size();

		if (!repeat) {
			for (; blockIndex < _blocksSize && _t->_blocks[blockIndex].get() != _endBlock && _t->_blocks[blockIndex]->from() < elideStart; ++blockIndex) {
			}
			if (blockIndex < _blocksSize) {
				elideSaveBlock(blockIndex, _endBlock, elideStart, elideWidth);
			}
		}
	}

	void restoreAfterElided() {
		if (_elideSavedBlock) {
			const_cast<String*>(_t)->_blocks[_elideSavedIndex] = std::move(_elideSavedBlock);
		}
	}

	// COPIED FROM qtextengine.cpp AND MODIFIED
	void eShapeLine(const QScriptLine &line) {
		int item = _e->findItem(line.from);
		if (item == -1)
			return;

#ifdef OS_MAC_OLD
		auto end = _e->findItem(line.from + line.length - 1);
#else // OS_MAC_OLD
		auto end = _e->findItem(line.from + line.length - 1, item);
#endif // OS_MAC_OLD

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		eSetFont(currentBlock);
		for (; item <= end; ++item) {
			QScriptItem &si = _e->layoutData->items[item];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				eSetFont(currentBlock);
			}
			_e->shape(item);
		}
	}

	style::font applyFlags(int32 flags, const style::font &f) {
		if (!flags) {
			return f;
		}
		auto result = f;
		if ((flags & TextBlockFPre) || (flags & TextBlockFCode)) {
			result = App::monofont();
			if (result->size() != f->size() || result->flags() != f->flags()) {
				result = style::font(f->size(), f->flags(), result->family());
			}
		} else {
			if (flags & TextBlockFBold) {
				result = result->bold();
			} else if (flags & TextBlockFSemibold) {
				result = st::semiboldFont;
				if (result->size() != f->size() || result->flags() != f->flags()) {
					result = style::font(f->size(), f->flags(), result->family());
				}
			}
			if (flags & TextBlockFItalic) result = result->italic();
			if (flags & TextBlockFUnderline) result = result->underline();
			if (flags & TextBlockFStrikeOut) result = result->strikeout();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				result = st::semiboldFont;
			}
		}
		return result;
	}

	void eSetFont(AbstractBlock *block) {
		const auto flags = block->flags();
		const auto usedFont = [&] {
			if (const auto index = block->lnkIndex()) {
				return ClickHandler::showAsActive(_t->_links.at(index - 1))
					? _t->_st->linkFontOver
					: _t->_st->linkFont;
			}
			return _t->_st->font;
		}();
		const auto newFont = applyFlags(flags, usedFont);
		if (newFont != _f) {
			_f = (newFont->family() == _t->_st->font->family())
				? applyFlags(flags | newFont->flags(), _t->_st->font)
				: newFont;
			_e->fnt = _f->f;
			_e->resetFontEngineCache();
		}
	}

	void eItemize() {
		_e->validate();
		if (_e->layoutData->items.size())
			return;

		int length = _e->layoutData->string.length();
		if (!length)
			return;

		const ushort *string = reinterpret_cast<const ushort*>(_e->layoutData->string.unicode());

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		_e->layoutData->hasBidi = _parHasBidi;
		auto analysis = _parAnalysis.data() + (_localFrom - _parStart);

		{
			QVarLengthArray<uchar> scripts(length);
			QUnicodeTools::initScripts(string, length, scripts.data());
			for (int i = 0; i < length; ++i)
				analysis[i].script = scripts.at(i);
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex].get();
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		auto start = string;
		auto end = start + length;
		while (start < end) {
			while (nextBlock && nextBlock->from() <= _localFrom + (start - string)) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			auto _type = currentBlock->type();
			if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
				analysis->script = QChar::Script_Common;
				analysis->flags = QScriptAnalysis::Object;
			} else {
				analysis->flags = QScriptAnalysis::None;
			}
			analysis->script = hbscript_to_script(script_to_hbscript(analysis->script)); // retain the old behavior
			++start;
			++analysis;
		}

		{
			auto i_string = &_e->layoutData->string;
			auto i_analysis = _parAnalysis.data() + (_localFrom - _parStart);
			auto i_items = &_e->layoutData->items;

			blockIndex = _lineStartBlock;
			currentBlock = _t->_blocks[blockIndex].get();
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			auto startBlock = currentBlock;

			if (!length) {
				return;
			}
			auto start = 0;
			auto end = start + length;
			for (int i = start + 1; i < end; ++i) {
				while (nextBlock && nextBlock->from() <= _localFrom + i) {
					currentBlock = nextBlock;
					nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				}
				// According to the unicode spec we should be treating characters in the Common script
				// (punctuation, spaces, etc) as being the same script as the surrounding text for the
				// purpose of splitting up text. This is important because, for example, a fullstop
				// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
				// word.  Thus it must be passed along with the word in languages that have to calculate
				// word breaks.  For example the thai word "ครม." has no word breaks but the word "ครม"
				// does.
				// Unfortuntely because we split up the strings for both wordwrapping and for setting
				// the font and because Japanese and Chinese are also aliases of the script "Common",
				// doing this would break too many things.  So instead we only pass the full stop
				// along, and nothing else.
				if (currentBlock == startBlock
					&& i_analysis[i].bidiLevel == i_analysis[start].bidiLevel
					&& i_analysis[i].flags == i_analysis[start].flags
					&& (i_analysis[i].script == i_analysis[start].script || i_string->at(i) == QLatin1Char('.'))
//					&& i_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
					&& i - start < _MaxItemLength)
					continue;
				i_items->append(QScriptItem(start, i_analysis[start]));
				start = i;
				startBlock = currentBlock;
			}
			i_items->append(QScriptItem(start, i_analysis[start]));
		}
	}

	QChar::Direction eSkipBoundryNeutrals(QScriptAnalysis *analysis,
											const ushort *unicode,
											int &sor, int &eor, BidiControl &control,
											String::TextBlocks::const_iterator i) {
		String::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction dir = control.basicDirection();
		int level = sor > 0 ? analysis[sor - 1].bidiLevel : control.level;
		while (sor <= _parLength) {
			while (i != _parStartBlock && (*i)->from() > _parStart + sor) {
				n = i;
				--i;
			}
			while (n != e && (*n)->from() <= _parStart + sor) {
				i = n;
				++n;
			}

			TextBlockType _itype = (*i)->type();
			if (eor == _parLength)
				dir = control.basicDirection();
			else if (_itype == TextBlockTEmoji)
				dir = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dir = QChar::DirCS;
			else
				dir = QChar::direction(unicode[sor]);
			// Keep skipping DirBN as if it doesn't exist
			if (dir != QChar::DirBN)
				break;
			analysis[sor++].bidiLevel = level;
		}

		eor = sor;

		return dir;
	}

	// creates the next QScript items.
	bool eBidiItemize(QScriptAnalysis *analysis, BidiControl &control) {
		bool rightToLeft = (control.basicDirection() == 1);
		bool hasBidi = rightToLeft;

		int sor = 0;
		int eor = -1;

		const ushort *unicode = reinterpret_cast<const ushort*>(_t->_text.unicode()) + _parStart;
		int current = 0;

		QChar::Direction dir = rightToLeft ? QChar::DirR : QChar::DirL;
		BidiStatus status;

		String::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction sdir;
		TextBlockType _stype = (*_parStartBlock)->type();
		if (_stype == TextBlockTEmoji)
			sdir = QChar::DirCS;
		else if (_stype == TextBlockTSkip)
			sdir = QChar::DirCS;
		else
			sdir = QChar::direction(*unicode);
		if (sdir != QChar::DirL && sdir != QChar::DirR && sdir != QChar::DirEN && sdir != QChar::DirAN)
			sdir = QChar::DirON;
		else
			dir = QChar::DirON;

		status.eor = sdir;
		status.lastStrong = rightToLeft ? QChar::DirR : QChar::DirL;
		status.last = status.lastStrong;
		status.dir = sdir;

		while (current <= _parLength) {
			while (n != e && (*n)->from() <= _parStart + current) {
				i = n;
				++n;
			}

			QChar::Direction dirCurrent;
			TextBlockType _itype = (*i)->type();
			if (current == (int)_parLength)
				dirCurrent = control.basicDirection();
			else if (_itype == TextBlockTEmoji)
				dirCurrent = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dirCurrent = QChar::DirCS;
			else
				dirCurrent = QChar::direction(unicode[current]);

			switch (dirCurrent) {

				// embedding and overrides (X1-X9 in the BiDi specs)
			case QChar::DirRLE:
			case QChar::DirRLO:
			case QChar::DirLRE:
			case QChar::DirLRO:
				{
					bool rtl = (dirCurrent == QChar::DirRLE || dirCurrent == QChar::DirRLO);
					hasBidi |= rtl;
					bool override = (dirCurrent == QChar::DirLRO || dirCurrent == QChar::DirRLO);

					unsigned int level = control.level+1;
					if ((level%2 != 0) == rtl) ++level;
					if (level < _MaxBidiLevel) {
						eor = current-1;
						eAppendItems(analysis, sor, eor, control, dir);
						eor = current;
						control.embed(rtl, override);
						QChar::Direction edir = (rtl ? QChar::DirR : QChar::DirL);
						dir = status.eor = edir;
						status.lastStrong = edir;
					}
					break;
				}
			case QChar::DirPDF:
				{
					if (control.canPop()) {
						if (dir != control.direction()) {
							eor = current-1;
							eAppendItems(analysis, sor, eor, control, dir);
							dir = control.direction();
						}
						eor = current;
						eAppendItems(analysis, sor, eor, control, dir);
						control.pdf();
						dir = QChar::DirON; status.eor = QChar::DirON;
						status.last = control.direction();
						if (control.override)
							dir = control.direction();
						else
							dir = QChar::DirON;
						status.lastStrong = control.direction();
					}
					break;
				}

				// strong types
			case QChar::DirL:
				if(dir == QChar::DirON)
					dir = QChar::DirL;
				switch(status.last)
					{
					case QChar::DirL:
						eor = current; status.eor = QChar::DirL; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
						} else {
							eor = current; status.eor = dir;
						}
						break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(dir != QChar::DirL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR) {
								if(status.eor != QChar::DirR) {
									// AN or EN
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirR;
								}
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
							} else {
								if(status.eor != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirL;
								} else {
									eor = current; status.eor = QChar::DirL; break;
								}
							}
						} else {
							eor = current; status.eor = QChar::DirL;
						}
					default:
						break;
					}
				status.lastStrong = QChar::DirL;
				break;
			case QChar::DirAL:
			case QChar::DirR:
				hasBidi = true;
				if(dir == QChar::DirON) dir = QChar::DirR;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0)
							eAppendItems(analysis, sor, eor, control, dir);
						// fall through
					case QChar::DirR:
					case QChar::DirAL:
						dir = QChar::DirR; eor = current; status.eor = QChar::DirR; break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor != QChar::DirR && status.eor != QChar::DirAL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR
							   || status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
								eor = current;
							} else {
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
							}
						} else {
							eor = current; status.eor = QChar::DirR;
						}
					default:
						break;
					}
				status.lastStrong = dirCurrent;
				break;

				// weak types:

			case QChar::DirNSM:
				if (eor == current-1)
					eor = current;
				break;
			case QChar::DirEN:
				// if last strong was AL change EN to AN
				if(status.lastStrong != QChar::DirAL) {
					if(dir == QChar::DirON) {
						if(status.lastStrong == QChar::DirL)
							dir = QChar::DirL;
						else
							dir = QChar::DirEN;
					}
					switch(status.last)
						{
						case QChar::DirET:
							if (status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								dir = QChar::DirAN;
							}
							[[fallthrough]];
						case QChar::DirEN:
						case QChar::DirL:
							eor = current;
							status.eor = dirCurrent;
							break;
						case QChar::DirR:
						case QChar::DirAL:
						case QChar::DirAN:
							if (eor >= 0)
								eAppendItems(analysis, sor, eor, control, dir);
							else
								eor = current;
							status.eor = QChar::DirEN;
							dir = QChar::DirAN;
							break;
						case QChar::DirES:
						case QChar::DirCS:
							if(status.eor == QChar::DirEN || dir == QChar::DirAN) {
								eor = current; break;
							}
							[[fallthrough]];
						case QChar::DirBN:
						case QChar::DirB:
						case QChar::DirS:
						case QChar::DirWS:
						case QChar::DirON:
							if(status.eor == QChar::DirR) {
								// neutrals go to R
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirEN;
								dir = QChar::DirAN;
							}
							else if(status.eor == QChar::DirL ||
									 (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
								eor = current; status.eor = dirCurrent;
							} else {
								// numbers on both sides, neutrals get right to left direction
								if(dir != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									eor = current - 1;
									dir = QChar::DirR;
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirAN;
								} else {
									eor = current; status.eor = dirCurrent;
								}
							}
							[[fallthrough]];
						default:
							break;
						}
					break;
				}
				[[fallthrough]];
			case QChar::DirAN:
				hasBidi = true;
				dirCurrent = QChar::DirAN;
				if(dir == QChar::DirON) dir = QChar::DirAN;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirAN:
						eor = current; status.eor = QChar::DirAN;
						break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
						if (eor >= 0){
							eAppendItems(analysis, sor, eor, control, dir);
						} else {
							eor = current;
						}
						dir = QChar::DirAN; status.eor = QChar::DirAN;
						break;
					case QChar::DirCS:
						if(status.eor == QChar::DirAN) {
							eor = current; break;
						}
						[[fallthrough]];
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor == QChar::DirR) {
							// neutrals go to R
							eor = current - 1;
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirAN;
							dir = QChar::DirAN;
						} else if(status.eor == QChar::DirL ||
								   (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
							eor = current; status.eor = dirCurrent;
						} else {
							// numbers on both sides, neutrals get right to left direction
							if(dir != QChar::DirL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								eor = current - 1;
								dir = QChar::DirR;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirAN;
								dir = QChar::DirAN;
							} else {
								eor = current; status.eor = dirCurrent;
							}
						}
						[[fallthrough]];
					default:
						break;
					}
				break;
			case QChar::DirES:
			case QChar::DirCS:
				break;
			case QChar::DirET:
				if(status.last == QChar::DirEN) {
					dirCurrent = QChar::DirEN;
					eor = current; status.eor = dirCurrent;
				}
				break;

				// boundary neutrals should be ignored
			case QChar::DirBN:
				break;
				// neutrals
			case QChar::DirB:
				// ### what do we do with newline and paragraph separators that come to here?
				break;
			case QChar::DirS:
				// ### implement rule L1
				break;
			case QChar::DirWS:
			case QChar::DirON:
				break;
			default:
				break;
			}

			if(current >= (int)_parLength) break;

			// set status.last as needed.
			switch(dirCurrent) {
			case QChar::DirET:
			case QChar::DirES:
			case QChar::DirCS:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				switch(status.last)
				{
				case QChar::DirL:
				case QChar::DirR:
				case QChar::DirAL:
				case QChar::DirEN:
				case QChar::DirAN:
					status.last = dirCurrent;
					break;
				default:
					status.last = QChar::DirON;
				}
				break;
			case QChar::DirNSM:
			case QChar::DirBN:
				// ignore these
				break;
			case QChar::DirLRO:
			case QChar::DirLRE:
				status.last = QChar::DirL;
				break;
			case QChar::DirRLO:
			case QChar::DirRLE:
				status.last = QChar::DirR;
				break;
			case QChar::DirEN:
				if (status.last == QChar::DirL) {
					status.last = QChar::DirL;
					break;
				}
				[[fallthrough]];
			default:
				status.last = dirCurrent;
			}

			++current;
		}

		eor = current - 1; // remove dummy char

		if (sor <= eor)
			eAppendItems(analysis, sor, eor, control, dir);

		return hasBidi;
	}

private:
	void applyBlockProperties(AbstractBlock *block) {
		eSetFont(block);
		if (_p) {
			if (block->lnkIndex()) {
				_currentPen = &_textPalette->linkFg->p;
				_currentPenSelected = &_textPalette->selectLinkFg->p;
			} else if ((block->flags() & TextBlockFCode) || (block->flags() & TextBlockFPre)) {
				_currentPen = &_textPalette->monoFg->p;
				_currentPenSelected = &_textPalette->selectMonoFg->p;
			} else {
				_currentPen = &_originalPen;
				_currentPenSelected = &_originalPenSelected;
			}
		}
	}

	Painter *_p = nullptr;
	const style::TextPalette *_textPalette = nullptr;
	const String *_t = nullptr;
	bool _elideLast = false;
	bool _breakEverywhere = false;
	int _elideRemoveFromEnd = 0;
	style::align _align = style::al_topleft;
	const QPen _originalPen;
	QPen _originalPenSelected;
	const QPen *_currentPen = nullptr;
	const QPen *_currentPenSelected = nullptr;
	int _yFrom = 0;
	int _yTo = 0;
	int _yToElide = 0;
	TextSelection _selection = { 0, 0 };
	bool _fullWidthSelection = true;
	const QChar *_str = nullptr;

	// current paragraph data
	String::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection;
	int _parStart = 0;
	int _parLength = 0;
	bool _parHasBidi = false;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e = nullptr;
	style::font _f;
	QFixed _x, _w, _wLeft, _last_rPadding;
	int32 _y, _yDelta, _lineHeight, _fontHeight;

	// elided hack support
	int _blocksSize = 0;
	int _elideSavedIndex = 0;
	std::unique_ptr<AbstractBlock> _elideSavedBlock;

	int _lineStart = 0;
	int _localFrom = 0;
	int _lineStartBlock = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	StateRequest _lookupRequest;
	StateResult _lookupResult;

};

String::String(int32 minResizeWidth) : _minResizeWidth(minResizeWidth) {
}

String::String(const style::TextStyle &st, const QString &text, const TextParseOptions &options, int32 minResizeWidth, bool richText) : _minResizeWidth(minResizeWidth) {
	if (richText) {
		setRichText(st, text, options);
	} else {
		setText(st, text, options);
	}
}

String::String(const String &other)
: _minResizeWidth(other._minResizeWidth)
, _maxWidth(other._maxWidth)
, _minHeight(other._minHeight)
, _text(other._text)
, _st(other._st)
, _links(other._links)
, _startDir(other._startDir) {
	_blocks.reserve(other._blocks.size());
	for (auto &block : other._blocks) {
		_blocks.push_back(block->clone());
	}
}

String::String(String &&other)
: _minResizeWidth(other._minResizeWidth)
, _maxWidth(other._maxWidth)
, _minHeight(other._minHeight)
, _text(other._text)
, _st(other._st)
, _blocks(std::move(other._blocks))
, _links(other._links)
, _startDir(other._startDir) {
	other.clearFields();
}

String &String::operator=(const String &other) {
	_minResizeWidth = other._minResizeWidth;
	_maxWidth = other._maxWidth;
	_minHeight = other._minHeight;
	_text = other._text;
	_st = other._st;
	_blocks = TextBlocks(other._blocks.size());
	_links = other._links;
	_startDir = other._startDir;
	for (int32 i = 0, l = _blocks.size(); i < l; ++i) {
		_blocks[i] = other._blocks.at(i)->clone();
	}
	return *this;
}

String &String::operator=(String &&other) {
	_minResizeWidth = other._minResizeWidth;
	_maxWidth = other._maxWidth;
	_minHeight = other._minHeight;
	_text = other._text;
	_st = other._st;
	_blocks = std::move(other._blocks);
	_links = other._links;
	_startDir = other._startDir;
	other.clearFields();
	return *this;
}

void String::setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options) {
	_st = &st;
	clear();
	{
		Parser parser(this, text, options);
	}
	recountNaturalSize(true, options.dir);
}

void String::recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir) {
	NewlineBlock *lastNewline = 0;

	_maxWidth = _minHeight = 0;
	int32 lineHeight = 0;
	int32 result = 0, lastNewlineStart = 0;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (auto i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		auto b = i->get();
		auto _btype = b->type();
		auto blockHeight = countBlockHeight(b, _st);
		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			if (initial) {
				Qt::LayoutDirection dir = optionsDir;
				if (dir == Qt::LayoutDirectionAuto) {
					dir = StringDirection(_text, lastNewlineStart, b->from());
				}
				if (lastNewline) {
					lastNewline->_nextDir = dir;
				} else {
					_startDir = dir;
				}
			}
			lastNewlineStart = b->from();
			lastNewline = static_cast<NewlineBlock*>(b);

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();

			accumulate_max(_maxWidth, _width);
			_width = (b->f_width() - last_rBearing);
			continue;
		}

		auto b__f_rbearing = b->f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		accumulate_max(_maxWidth, _width);

		_width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (initial) {
		Qt::LayoutDirection dir = optionsDir;
		if (dir == Qt::LayoutDirectionAuto) {
			dir = StringDirection(_text, lastNewlineStart, _text.size());
		}
		if (lastNewline) {
			lastNewline->_nextDir = dir;
		} else {
			_startDir = dir;
		}
	}
	if (_width > 0) {
		if (!lineHeight) lineHeight = countBlockHeight(_blocks.back().get(), _st);
		_minHeight += lineHeight;
		accumulate_max(_maxWidth, _width);
	}
}

void String::setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options) {
	_st = &st;
	clear();
	{
		// utf codes of the text display for emoji extraction
//		auto text = textWithEntities.text;
//		auto newText = QString();
//		newText.reserve(8 * text.size());
//		newText.append("\t{ ");
//		for (const QChar *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
//			if (*ch == TextCommand) {
//				break;
//			} else if (chIsNewline(*ch)) {
//				newText.append("},").append(*ch).append("\t{ ");
//			} else {
//				if (ch->isHighSurrogate() || ch->isLowSurrogate()) {
//					if (ch->isHighSurrogate() && (ch + 1 != e) && ((ch + 1)->isLowSurrogate())) {
//						newText.append("0x").append(QString::number((uint32(ch->unicode()) << 16) | uint32((ch + 1)->unicode()), 16).toUpper()).append("U, ");
//						++ch;
//					} else {
//						newText.append("BADx").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//					}
//				} else {
//					newText.append("0x").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//				}
//			}
//		}
//		newText.append("},\n\n").append(text);
//		Parser parser(this, { newText, EntitiesInText() }, options);

		Parser parser(this, textWithEntities, options);
	}
	recountNaturalSize(true, options.dir);
}

void String::setRichText(const style::TextStyle &st, const QString &text, TextParseOptions options) {
	options.flags |= TextParseRichText;
	setText(st, text, options);
}

void String::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

bool String::hasLinks() const {
	return !_links.isEmpty();
}

bool String::hasSkipBlock() const {
	return _blocks.empty() ? false : _blocks.back()->type() == TextBlockTSkip;
}

bool String::updateSkipBlock(int width, int height) {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockTSkip) {
		const auto block = static_cast<SkipBlock*>(_blocks.back().get());
		if (block->width() == width && block->height() == height) {
			return false;
		}
		_text.resize(block->from());
		_blocks.pop_back();
	}
	_text.push_back('_');
	_blocks.push_back(std::make_unique<SkipBlock>(
		_st->font,
		_text,
		_text.size() - 1,
		width,
		height,
		0));
	recountNaturalSize(false);
	return true;
}

bool String::removeSkipBlock() {
	if (_blocks.empty() || _blocks.back()->type() != TextBlockTSkip) {
		return false;
	}
	_text.resize(_blocks.back()->from());
	_blocks.pop_back();
	recountNaturalSize(false);
	return true;
}

int String::countWidth(int width) const {
	if (QFixed(width) >= _maxWidth) {
		return _maxWidth.ceil().toInt();
	}

	QFixed maxLineWidth = 0;
	enumerateLines(width, [&maxLineWidth](QFixed lineWidth, int lineHeight) {
		if (lineWidth > maxLineWidth) {
			maxLineWidth = lineWidth;
		}
	});
	return maxLineWidth.ceil().toInt();
}

int String::countHeight(int width) const {
	if (QFixed(width) >= _maxWidth) {
		return _minHeight;
	}
	int result = 0;
	enumerateLines(width, [&result](QFixed lineWidth, int lineHeight) {
		result += lineHeight;
	});
	return result;
}

void String::countLineWidths(int width, QVector<int> *lineWidths) const {
	enumerateLines(width, [lineWidths](QFixed lineWidth, int lineHeight) {
		lineWidths->push_back(lineWidth.ceil().toInt());
	});
}

template <typename Callback>
void String::enumerateLines(int w, Callback callback) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;

	int lineHeight = 0;
	QFixed widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (auto &b : _blocks) {
		auto _btype = b->type();
		int blockHeight = countBlockHeight(b.get(), _st);

		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			callback(width - widthLeft, lineHeight);

			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		auto b__f_rbearing = b->f_rbearing();
		auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + b->f_width() - b__f_rbearing);
		if (newWidthLeft >= 0) {
			last_rBearing = b__f_rbearing;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			lineHeight = qMax(lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockTText) {
			auto t = static_cast<TextBlock*>(b.get());
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				last_rPadding += b->f_rpadding();

				lineHeight = qMax(lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			auto f_wLeft = widthLeft;
			int f_lineHeight = lineHeight;
			for (auto j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->f_width() >= 0);
				auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

				auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->f_rpadding();
					widthLeft = newWidthLeft;

					lineHeight = qMax(lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f_lineHeight = lineHeight;
						f = j + 1;
					}
					continue;
				}

				if (f != j) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
				}

				callback(width - widthLeft, lineHeight);

				lineHeight = qMax(0, blockHeight);
				last_rBearing = j->f_rbearing();
				last_rPadding = j->f_rpadding();
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = true;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		callback(width - widthLeft, lineHeight);

		lineHeight = qMax(0, blockHeight);
		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < width) {
		callback(width - widthLeft, lineHeight);
	}
}

void String::draw(Painter &painter, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection, bool fullWidthSelection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer p(&painter, this);
	p.draw(left, top, w, align, yFrom, yTo, selection, fullWidthSelection);
}

void String::drawElided(Painter &painter, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer p(&painter, this);
	p.drawElided(left, top, w, align, lines, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

StateResult String::getState(QPoint point, int width, StateRequest request) const {
	return Renderer(nullptr, this).getState(point, width, request);
}

StateResult String::getStateElided(QPoint point, int width, StateRequestElided request) const {
	return Renderer(nullptr, this).getStateElided(point, width, request);
}

TextSelection String::adjustSelection(TextSelection selection, TextSelectType selectType) const {
	uint16 from = selection.from, to = selection.to;
	if (from < _text.size() && from <= to) {
		if (to > _text.size()) to = _text.size();
		if (selectType == TextSelectType::Paragraphs) {
			if (!chIsParagraphSeparator(_text.at(from))) {
				while (from > 0 && !chIsParagraphSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsParagraphSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsParagraphSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		} else if (selectType == TextSelectType::Words) {
			if (!chIsWordSeparator(_text.at(from))) {
				while (from > 0 && !chIsWordSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsWordSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsWordSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		}
	}
	return { from, to };
}

bool String::isEmpty() const {
	return _blocks.empty() || _blocks[0]->type() == TextBlockTSkip;
}

uint16 String::countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const {
	return (i + 1 == e) ? _text.size() : (*(i + 1))->from();
}

uint16 String::countBlockLength(const String::TextBlocks::const_iterator &i, const String::TextBlocks::const_iterator &e) const {
	return countBlockEnd(i, e) - (*i)->from();
}

template <typename AppendPartCallback, typename ClickHandlerStartCallback, typename ClickHandlerFinishCallback, typename FlagsChangeCallback>
void String::enumerateText(TextSelection selection, AppendPartCallback appendPartCallback, ClickHandlerStartCallback clickHandlerStartCallback, ClickHandlerFinishCallback clickHandlerFinishCallback, FlagsChangeCallback flagsChangeCallback) const {
	if (isEmpty() || selection.empty()) {
		return;
	}

	int lnkIndex = 0;
	uint16 lnkFrom = 0;
	int32 flags = 0;
	for (auto i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		int blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		uint16 blockFrom = (i == e) ? _text.size() : (*i)->from();
		int32 blockFlags = (i == e) ? 0 : (*i)->flags();

		if (blockLnkIndex && !_links.at(blockLnkIndex - 1)) { // ignore empty links
			blockLnkIndex = 0;
		}
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) {
				auto rangeFrom = qMax(selection.from, lnkFrom);
				auto rangeTo = qMin(selection.to, blockFrom);
				if (rangeTo > rangeFrom) { // handle click handler
					QStringRef r = _text.midRef(rangeFrom, rangeTo - rangeFrom);
					if (lnkFrom != rangeFrom || blockFrom != rangeTo) {
						appendPartCallback(r);
					} else {
						clickHandlerFinishCallback(r, _links.at(lnkIndex - 1));
					}
				}
			}
			lnkIndex = blockLnkIndex;
			if (lnkIndex) {
				lnkFrom = blockFrom;
				clickHandlerStartCallback();
			}
		}

		const auto checkBlockFlags = (blockFrom >= selection.from)
			&& (blockFrom <= selection.to);
		if (checkBlockFlags && blockFlags != flags) {
			flagsChangeCallback(flags, blockFlags);
			flags = blockFlags;
		}
		if (i == e || blockFrom >= selection.to) {
			break;
		}

		if ((*i)->type() == TextBlockTSkip) continue;

		if (!blockLnkIndex) {
			auto rangeFrom = qMax(selection.from, blockFrom);
			auto rangeTo = qMin(selection.to, uint16(blockFrom + countBlockLength(i, e)));
			if (rangeTo > rangeFrom) {
				appendPartCallback(_text.midRef(rangeFrom, rangeTo - rangeFrom));
			}
		}
	}
}

QString String::toString(TextSelection selection) const {
	return toText(selection, false, false).rich.text;
}

TextWithEntities String::toTextWithEntities(TextSelection selection) const {
	return toText(selection, false, true).rich;
}

TextForMimeData String::toTextForMimeData(TextSelection selection) const {
	return toText(selection, true, true);
}

TextForMimeData String::toText(
		TextSelection selection,
		bool composeExpanded,
		bool composeEntities) const {
	struct MarkdownTagTracker {
		TextBlockFlags flag = TextBlockFlags();
		EntityType type = EntityType();
		int start = 0;
	};
	auto result = TextForMimeData();
	result.rich.text.reserve(_text.size());
	if (composeExpanded) {
		result.expanded.reserve(_text.size());
	}
	auto linkStart = 0;
	auto markdownTrackers = composeEntities
		? std::vector<MarkdownTagTracker>{
			{ TextBlockFItalic, EntityType::Italic },
			{ TextBlockFSemibold, EntityType::Bold },
			{ TextBlockFUnderline, EntityType::Underline },
			{ TextBlockFStrikeOut, EntityType::StrikeOut },
			{ TextBlockFCode, EntityType::Code }, // #TODO entities
			{ TextBlockFPre, EntityType::Pre }
		} : std::vector<MarkdownTagTracker>();
	const auto flagsChangeCallback = [&](int32 oldFlags, int32 newFlags) {
		if (!composeEntities) {
			return;
		}
		for (auto &tracker : markdownTrackers) {
			const auto flag = tracker.flag;
			if ((oldFlags & flag) && !(newFlags & flag)) {
				result.rich.entities.push_back({
					tracker.type,
					tracker.start,
					result.rich.text.size() - tracker.start });
			} else if ((newFlags & flag) && !(oldFlags & flag)) {
				tracker.start = result.rich.text.size();
			}
		}
	};
	const auto clickHandlerStartCallback = [&] {
		linkStart = result.rich.text.size();
	};
	const auto clickHandlerFinishCallback = [&](
			const QStringRef &part,
			const ClickHandlerPtr &handler) {
		const auto entity = handler->getTextEntity();
		const auto plainUrl = (entity.type == EntityType::Url)
			|| (entity.type == EntityType::Email);
		const auto full = plainUrl
			? entity.data.midRef(0, entity.data.size())
			: part;
		result.rich.text.append(full);
		if (!composeExpanded && !composeEntities) {
			return;
		}
		if (composeExpanded) {
			result.expanded.append(full);
			if (entity.type == EntityType::CustomUrl) {
				const auto &url = entity.data;
				result.expanded.append(qstr(" (")).append(url).append(')');
			}
		}
		if (composeEntities) {
			result.rich.entities.push_back({
				entity.type,
				linkStart,
				full.size(),
				plainUrl ? QString() : entity.data });
		}
	};
	const auto appendPartCallback = [&](const QStringRef &part) {
		result.rich.text += part;
		if (composeExpanded) {
			result.expanded += part;
		}
	};

	enumerateText(
		selection,
		appendPartCallback,
		clickHandlerStartCallback,
		clickHandlerFinishCallback,
		flagsChangeCallback);

	return result;
}

IsolatedEmoji String::toIsolatedEmoji() const {
	auto result = IsolatedEmoji();
	const auto skip = (_blocks.empty()
		|| _blocks.back()->type() != TextBlockTSkip) ? 0 : 1;
	if (_blocks.size() > kIsolatedEmojiLimit + skip) {
		return IsolatedEmoji();
	}
	auto index = 0;
	for (const auto &block : _blocks) {
		const auto type = block->type();
		if (block->lnkIndex()) {
			return IsolatedEmoji();
		} else if (type == TextBlockTEmoji) {
			result.items[index++] = static_cast<EmojiBlock*>(block.get())->emoji;
		} else if (type != TextBlockTSkip) {
			return IsolatedEmoji();
		}
	}
	return result;
}

void String::clear() {
	clearFields();
	_text.clear();
}

void String::clearFields() {
	_blocks.clear();
	_links.clear();
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

String::~String() = default;

} // namespace Text
} // namespace Ui
