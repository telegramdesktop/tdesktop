/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/text/text.h"

#include <private/qharfbuzz_p.h>

#include "core/click_handler_types.h"
#include "ui/text/text_block.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "boxes/confirm_box.h"
#include "mainwindow.h"

namespace {

inline int32 countBlockHeight(const ITextBlock *b, const style::TextStyle *st) {
	return (b->type() == TextBlockTSkip) ? static_cast<const SkipBlock*>(b)->height() : (st->lineHeight > st->font->height) ? st->lineHeight : st->font->height;
}

} // namespace

bool chIsBad(QChar ch) {
#ifdef OS_MAC_OLD
	if (cIsSnowLeopard() && (ch == 8207 || ch == 8206)) {
		return true;
	}
#endif // OS_MAC_OLD
	return (ch == 0) || (ch >= 8232 && ch < 8237) || (ch >= 65024 && ch < 65040 && ch != 65039) || (ch >= 127 && ch < 160 && ch != 156) || (cPlatform() == dbipMac && ch >= 0x0B00 && ch <= 0x0B7F && chIsDiac(ch) && cIsElCapitan()); // tmp hack see https://bugreports.qt.io/browse/QTBUG-48910
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

class TextParser {
public:
	static Qt::LayoutDirection stringDirection(const QString &str, int32 from, int32 to) {
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

	void blockCreated() {
		sumWidth += _t->_blocks.back()->f_width();
		if (sumWidth.floor().toInt() > stopAfterWidth) {
			sumFinished = true;
		}
	}

	void createBlock(int32 skipBack = 0) {
		if (lnkIndex < 0x8000 && lnkIndex > maxLnkIndex) maxLnkIndex = lnkIndex;
		int32 len = int32(_t->_text.size()) + skipBack - blockStart;
		if (len > 0) {
			bool newline = !emoji && (len == 1 && _t->_text.at(blockStart) == QChar::LineFeed);
			if (newlineAwaited) {
				newlineAwaited = false;
				if (!newline) {
					_t->_text.insert(blockStart, QChar::LineFeed);
					createBlock(skipBack - len);
				}
			}
			lastSkipped = false;
			if (emoji) {
				_t->_blocks.push_back(std::make_unique<EmojiBlock>(_t->_st->font, _t->_text, blockStart, len, flags, lnkIndex, emoji));
				emoji = 0;
				lastSkipped = true;
			} else if (newline) {
				_t->_blocks.push_back(std::make_unique<NewlineBlock>(_t->_st->font, _t->_text, blockStart, len, flags, lnkIndex));
			} else {
				_t->_blocks.push_back(std::make_unique<TextBlock>(_t->_st->font, _t->_text, _t->_minResizeWidth, blockStart, len, flags, lnkIndex));
			}
			blockStart += len;
			blockCreated();
		}
	}

	void createSkipBlock(int32 w, int32 h) {
		createBlock();
		_t->_text.push_back('_');
		_t->_blocks.push_back(std::make_unique<SkipBlock>(_t->_st->font, _t->_text, blockStart++, w, h, lnkIndex));
		blockCreated();
	}

	void createNewlineBlock() {
		createBlock();
		_t->_text.push_back(QChar::LineFeed);
		createBlock();
	}

	bool checkCommand() {
		bool result = false;
		for (QChar c = ((ptr < end) ? *ptr : 0); c == TextCommand; c = ((ptr < end) ? *ptr : 0)) {
			if (!readCommand()) {
				break;
			}
			result = true;
		}
		return result;
	}

	// Returns true if at least one entity was parsed in the current position.
	bool checkEntities() {
		while (!removeFlags.isEmpty() && (ptr >= removeFlags.firstKey() || ptr >= end)) {
			const QList<int32> &removing(removeFlags.first());
			for (int32 i = removing.size(); i > 0;) {
				int32 flag = removing.at(--i);
				if (flags & flag) {
					createBlock();
					flags &= ~flag;
					if (flag == TextBlockFPre) {
						newlineAwaited = true;
					}
				}
			}
			removeFlags.erase(removeFlags.begin());
		}
		while (waitingEntity != entitiesEnd && start + waitingEntity->offset() + waitingEntity->length() <= ptr) {
			++waitingEntity;
		}
		if (waitingEntity == entitiesEnd || ptr < start + waitingEntity->offset()) {
			return false;
		}

		int32 startFlags = 0;
		QString linkData, linkText;
		auto type = waitingEntity->type(), linkType = EntityInTextInvalid;
		LinkDisplayStatus linkDisplayStatus = LinkDisplayedFull;
		if (type == EntityInTextBold) {
			startFlags = TextBlockFSemibold;
		} else if (type == EntityInTextItalic) {
			startFlags = TextBlockFItalic;
		} else if (type == EntityInTextCode) {
			startFlags = TextBlockFCode;
		} else if (type == EntityInTextPre) {
			startFlags = TextBlockFPre;
			createBlock();
			if (!_t->_blocks.empty() && _t->_blocks.back()->type() != TextBlockTNewline) {
				createNewlineBlock();
			}
		} else if (type == EntityInTextUrl
		        || type == EntityInTextEmail
		        || type == EntityInTextMention
		        || type == EntityInTextHashtag
		        || type == EntityInTextBotCommand) {
			linkType = type;
			linkData = QString(start + waitingEntity->offset(), waitingEntity->length());
			if (linkType == EntityInTextUrl) {
				computeLinkText(linkData, &linkText, &linkDisplayStatus);
			} else {
				linkText = linkData;
			}
		} else if (type == EntityInTextCustomUrl || type == EntityInTextMentionName) {
			linkType = type;
			linkData = waitingEntity->data();
			linkText = QString(start + waitingEntity->offset(), waitingEntity->length());
		}

		if (linkType != EntityInTextInvalid) {
			createBlock();

			links.push_back(TextLinkData(linkType, linkText, linkData, linkDisplayStatus));
			lnkIndex = 0x8000 + links.size();

			for (auto entityEnd = start + waitingEntity->offset() + waitingEntity->length(); ptr < entityEnd; ++ptr) {
				parseCurrentChar();
				parseEmojiFromCurrent();

				if (sumFinished || _t->_text.size() >= 0x8000) break; // 32k max
			}
			createBlock();

			lnkIndex = 0;
		} else if (startFlags) {
			if (!(flags & startFlags)) {
				createBlock();
				flags |= startFlags;
				removeFlags[start + waitingEntity->offset() + waitingEntity->length()].push_front(startFlags);
			}
		}

		++waitingEntity;
		if (links.size() >= 0x7FFF) {
			while (waitingEntity != entitiesEnd && (
				waitingEntity->type() == EntityInTextUrl ||
				waitingEntity->type() == EntityInTextCustomUrl ||
				waitingEntity->type() == EntityInTextEmail ||
				waitingEntity->type() == EntityInTextHashtag ||
				waitingEntity->type() == EntityInTextMention ||
				waitingEntity->type() == EntityInTextMentionName ||
				waitingEntity->type() == EntityInTextBotCommand ||
				waitingEntity->length() <= 0)) {
				++waitingEntity;
			}
		} else {
			while (waitingEntity != entitiesEnd && waitingEntity->length() <= 0) ++waitingEntity;
		}
		return true;
	}

	bool readSkipBlockCommand() {
		const QChar *afterCmd = textSkipCommand(ptr, end, links.size() < 0x7FFF);
		if (afterCmd == ptr) {
			return false;
		}

		ushort cmd = (++ptr)->unicode();
		++ptr;

		switch (cmd) {
		case TextCommandSkipBlock:
			createSkipBlock(ptr->unicode(), (ptr + 1)->unicode());
		break;
		}

		ptr = afterCmd;
		return true;
	}

	bool readCommand() {
		const QChar *afterCmd = textSkipCommand(ptr, end, links.size() < 0x7FFF);
		if (afterCmd == ptr) {
			return false;
		}

		ushort cmd = (++ptr)->unicode();
		++ptr;

		switch (cmd) {
		case TextCommandBold:
			if (!(flags & TextBlockFBold)) {
				createBlock();
				flags |= TextBlockFBold;
			}
		break;

		case TextCommandNoBold:
			if (flags & TextBlockFBold) {
				createBlock();
				flags &= ~TextBlockFBold;
			}
		break;

		case TextCommandSemibold:
		if (!(flags & TextBlockFSemibold)) {
			createBlock();
			flags |= TextBlockFSemibold;
		}
		break;

		case TextCommandNoSemibold:
		if (flags & TextBlockFSemibold) {
			createBlock();
			flags &= ~TextBlockFSemibold;
		}
		break;

		case TextCommandItalic:
			if (!(flags & TextBlockFItalic)) {
				createBlock();
				flags |= TextBlockFItalic;
			}
		break;

		case TextCommandNoItalic:
			if (flags & TextBlockFItalic) {
				createBlock();
				flags &= ~TextBlockFItalic;
			}
		break;

		case TextCommandUnderline:
			if (!(flags & TextBlockFUnderline)) {
				createBlock();
				flags |= TextBlockFUnderline;
			}
		break;

		case TextCommandNoUnderline:
			if (flags & TextBlockFUnderline) {
				createBlock();
				flags &= ~TextBlockFUnderline;
			}
		break;

		case TextCommandLinkIndex:
			if (ptr->unicode() != lnkIndex) {
				createBlock();
				lnkIndex = ptr->unicode();
			}
		break;

		case TextCommandLinkText: {
			createBlock();
			int32 len = ptr->unicode();
			links.push_back(TextLinkData(EntityInTextCustomUrl, QString(), QString(++ptr, len), LinkDisplayedFull));
			lnkIndex = 0x8000 + links.size();
		} break;

		case TextCommandSkipBlock:
			createSkipBlock(ptr->unicode(), (ptr + 1)->unicode());
		break;
		}

		ptr = afterCmd;
		return true;
	}

	void parseCurrentChar() {
		int skipBack = 0;
		ch = ((ptr < end) ? *ptr : 0);
		emojiLookback = 0;
		bool skip = false, isNewLine = multiline && chIsNewline(ch), isSpace = chIsSpace(ch), isDiac = chIsDiac(ch), isTilde = checkTilde && (ch == '~');
		if (chIsBad(ch) || ch.isLowSurrogate()) {
			skip = true;
		} else if (ch == 0xFE0F && (cPlatform() == dbipMac || cPlatform() == dbipMacOld)) {
			// Some sequences like 0x0E53 0xFE0F crash OS X harfbuzz text processing :(
			skip = true;
		} else if (isDiac) {
			if (lastSkipped || emoji || ++diacs > chMaxDiacAfterSymbol()) {
				skip = true;
			}
		} else if (ch.isHighSurrogate()) {
			if (ptr + 1 >= end || !(ptr + 1)->isLowSurrogate()) {
				skip = true;
			} else {
				_t->_text.push_back(ch);
				skipBack = -1;
				++ptr;
				ch = *ptr;
				emojiLookback = 1;
			}
		}

		lastSkipped = skip;
		if (skip) {
			ch = 0;
		} else {
			if (isTilde) { // tilde fix in OpenSans
				if (!(flags & TextBlockFTilde)) {
					createBlock(skipBack);
					flags |= TextBlockFTilde;
				}
			} else {
				if (flags & TextBlockFTilde) {
					createBlock(skipBack);
					flags &= ~TextBlockFTilde;
				}
			}
			if (isNewLine) {
				createNewlineBlock();
			} else if (isSpace) {
				_t->_text.push_back(QChar::Space);
			} else {
				if (emoji) createBlock(skipBack);
				_t->_text.push_back(ch);
			}
			if (!isDiac) diacs = 0;
		}
	}

	void parseEmojiFromCurrent() {
		int len = 0;
		auto e = Ui::Emoji::Find(ptr - emojiLookback, end, &len);
		if (!e) return;

		for (int l = len - emojiLookback - 1; l > 0; --l) {
			_t->_text.push_back(*++ptr);
		}
		if (e->hasPostfix() && _t->_text.at(_t->_text.size() - 1).unicode() != Ui::Emoji::kPostfix) {
			_t->_text.push_back(QChar(Ui::Emoji::kPostfix));
			++len;
		}

		createBlock(-len);
		emoji = e;
	}

	TextParser(Text *t, const QString &text, const TextParseOptions &options) : _t(t),
		source { text },
		rich(options.flags & TextParseRichText),
		multiline(options.flags & TextParseMultiline),
		stopAfterWidth(QFIXED_MAX) {
		if (options.flags & TextParseLinks) {
			TextUtilities::ParseEntities(source, options.flags, rich);
		}
		parse(options);
	}

	TextParser(Text *t, const TextWithEntities &textWithEntities, const TextParseOptions &options) : _t(t),
		source(textWithEntities),
		rich(options.flags & TextParseRichText),
		multiline(options.flags & TextParseMultiline),
		stopAfterWidth(QFIXED_MAX) {
		auto &preparsed = textWithEntities.entities;
		if ((options.flags & TextParseLinks) && !preparsed.isEmpty()) {
			bool parseMentions = (options.flags & TextParseMentions);
			bool parseHashtags = (options.flags & TextParseHashtags);
			bool parseBotCommands = (options.flags & TextParseBotCommands);
			bool parseMarkdown = (options.flags & TextParseMarkdown);
			if (!parseMentions || !parseHashtags || !parseBotCommands || !parseMarkdown) {
				int32 i = 0, l = preparsed.size();
				source.entities.clear();
				source.entities.reserve(l);
				const QChar s = source.text.size();
				for (; i < l; ++i) {
					auto type = preparsed.at(i).type();
					if (((type == EntityInTextMention || type == EntityInTextMentionName) && !parseMentions) ||
						(type == EntityInTextHashtag && !parseHashtags) ||
						(type == EntityInTextBotCommand && !parseBotCommands) ||
						((type == EntityInTextBold || type == EntityInTextItalic || type == EntityInTextCode || type == EntityInTextPre) && !parseMarkdown)) {
						continue;
					}
					source.entities.push_back(preparsed.at(i));
				}
			}
		}
		parse(options);
	}

	void parse(const TextParseOptions &options) {
		if (options.maxw > 0 && options.maxh > 0) {
			stopAfterWidth = ((options.maxh / _t->_st->font->height) + 1) * options.maxw;
		}

		start = source.text.constData();
		end = start + source.text.size();

		entitiesEnd = source.entities.cend();
		waitingEntity = source.entities.cbegin();
		while (waitingEntity != entitiesEnd && waitingEntity->length() <= 0) ++waitingEntity;
		int firstMonospaceOffset = EntityInText::firstMonospaceOffset(source.entities, end - start);

		ptr = start;
		while (ptr != end && chIsTrimmed(*ptr, rich) && ptr != start + firstMonospaceOffset) {
			++ptr;
		}
		while (ptr != end && chIsTrimmed(*(end - 1), rich)) {
			--end;
		}

		_t->_text.resize(0);
		_t->_text.reserve(end - ptr);

		diacs = 0;
		sumWidth = 0;
		sumFinished = newlineAwaited = false;
		blockStart = 0;
		emoji = 0;

		ch = emojiLookback = 0;
		lastSkipped = false;
		checkTilde = !cRetina() && (_t->_st->font->size() == 13) && (_t->_st->font->flags() == 0) && (_t->_st->font->f.family() == qstr("Open Sans")); // tilde Open Sans fix
		for (; ptr <= end; ++ptr) {
			while (checkEntities() || (rich && checkCommand())) {
			}
			parseCurrentChar();
			parseEmojiFromCurrent();

			if (sumFinished || _t->_text.size() >= 0x8000) break; // 32k max
		}
		createBlock();
		if (sumFinished && rich) { // we could've skipped the final skip block command
			for (; ptr < end; ++ptr) {
				if (*ptr == TextCommand && readSkipBlockCommand()) {
					break;
				}
			}
		}
		removeFlags.clear();

		_t->_links.resize(maxLnkIndex);
		for (auto i = _t->_blocks.cbegin(), e = _t->_blocks.cend(); i != e; ++i) {
			auto b = i->get();
			if (b->lnkIndex() > 0x8000) {
				lnkIndex = maxLnkIndex + (b->lnkIndex() - 0x8000);
				if (_t->_links.size() < lnkIndex) {
					_t->_links.resize(lnkIndex);
					auto &link = links[lnkIndex - maxLnkIndex - 1];
					auto handler = ClickHandlerPtr();
					switch (link.type) {
					case EntityInTextCustomUrl: {
						if (!link.data.isEmpty()) {
							handler = std::make_shared<HiddenUrlClickHandler>(link.data);
						}
					} break;
					case EntityInTextEmail:
					case EntityInTextUrl: handler = std::make_shared<UrlClickHandler>(link.data, link.displayStatus == LinkDisplayedFull); break;
					case EntityInTextBotCommand: handler = std::make_shared<BotCommandClickHandler>(link.data); break;
					case EntityInTextHashtag:
						if (options.flags & TextTwitterMentions) {
							handler = std::make_shared<UrlClickHandler>(qsl("https://twitter.com/hashtag/") + link.data.mid(1) + qsl("?src=hash"), true);
						} else if (options.flags & TextInstagramMentions) {
							handler = std::make_shared<UrlClickHandler>(qsl("https://instagram.com/explore/tags/") + link.data.mid(1) + '/', true);
						} else {
							handler = std::make_shared<HashtagClickHandler>(link.data);
						}
					break;
					case EntityInTextMention:
						if (options.flags & TextTwitterMentions) {
							handler = std::make_shared<UrlClickHandler>(qsl("https://twitter.com/") + link.data.mid(1), true);
						} else if (options.flags & TextInstagramMentions) {
							handler = std::make_shared<UrlClickHandler>(qsl("https://instagram.com/") + link.data.mid(1) + '/', true);
						} else {
							handler = std::make_shared<MentionClickHandler>(link.data);
						}
					break;
					case EntityInTextMentionName: {
						auto fields = TextUtilities::MentionNameDataToFields(link.data);
						if (fields.userId) {
							handler = std::make_shared<MentionNameClickHandler>(link.text, fields.userId, fields.accessHash);
						} else {
							LOG(("Bad mention name: %1").arg(link.data));
						}
					} break;
					}

					if (handler) {
						_t->setLink(lnkIndex, handler);
					}
				}
				b->setLnkIndex(lnkIndex);
			}
		}
		_t->_links.squeeze();
		_t->_blocks.shrink_to_fit();
		_t->_text.squeeze();
	}

private:
	enum LinkDisplayStatus {
		LinkDisplayedFull,
		LinkDisplayedElided,
	};

	struct TextLinkData {
		TextLinkData() = default;
		TextLinkData(EntityInTextType type, const QString &text, const QString &data, LinkDisplayStatus displayStatus)
			: type(type)
			, text(text)
			, data(data)
			, displayStatus(displayStatus) {
		}
		EntityInTextType type = EntityInTextInvalid;
		QString text, data;
		LinkDisplayStatus displayStatus = LinkDisplayedFull;
	};

	void computeLinkText(const QString &linkData, QString *outLinkText, LinkDisplayStatus *outDisplayStatus) {
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

	Text *_t;
	TextWithEntities source;
	const QChar *start, *end, *ptr;
	bool rich, multiline;
	EntitiesInText::const_iterator waitingEntity, entitiesEnd;

	typedef QVector<TextLinkData> TextLinks;
	TextLinks links;

	typedef QMap<const QChar*, QList<int32> > RemoveFlagsMap;
	RemoveFlagsMap removeFlags;

	uint16 maxLnkIndex = 0;

	// current state
	int32 flags = 0;
	uint16 lnkIndex = 0;
	EmojiPtr emoji = nullptr; // current emoji, if current word is an emoji, or zero
	int32 blockStart = 0; // offset in result, from which current parsed block is started
	int32 diacs = 0; // diac chars skipped without good char
	QFixed sumWidth, stopAfterWidth; // summary width of all added words
	bool sumFinished = false;
	bool newlineAwaited = false;

	// current char data
	QChar ch; // current char (low surrogate, if current char is surrogate pair)
	int32 emojiLookback; // how far behind the current ptr to look for current emoji
	bool lastSkipped; // did we skip current char
	bool checkTilde; // do we need a special text block for tilde symbol
};

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

} // namespace

class TextPainter {
public:
	TextPainter(Painter *p, const Text *t) : _p(p), _t(t) {
	}

	~TextPainter() {
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
			_originalPen = _p->pen();
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

	Text::StateResult getState(QPoint point, int w, Text::StateRequest request) {
		if (!_t->isNull() && point.y() >= 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & Text::StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & Text::StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & Text::StateRequest::Flag::LookupLink);
			if (_lookupSymbol || (_lookupX >= 0 && _lookupX < w)) {
				draw(0, 0, w, _lookupRequest.align, _lookupY, _lookupY + 1);
			}
		}
		return _lookupResult;
	}

	Text::StateResult getStateElided(QPoint point, int w, Text::StateRequestElided request) {
		if (!_t->isNull() && point.y() >= 0 && request.lines > 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & Text::StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & Text::StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & Text::StateRequest::Flag::LookupLink);
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
	void initNextParagraph(Text::TextBlocks::const_iterator i) {
		_parStartBlock = i;
		Text::TextBlocks::const_iterator e = _t->_blocks.cend();
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

		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

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

	bool drawLine(uint16 _lineEnd, const Text::TextBlocks::const_iterator &_endBlockIter, const Text::TextBlocks::const_iterator &_end) {
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

		int32 delta = (currentBlock->from() < _lineStart ? qMin(_lineStart - currentBlock->from(), 2) : 0);
		_localFrom = _lineStart - delta;
		int32 lineEnd = (_endBlock && _endBlock->from() < trimmedLineEnd && !elidedLine) ? qMin(uint16(trimmedLineEnd + 2), _t->countBlockEnd(_endBlockIter, _end)) : trimmedLineEnd;

		auto lineText = _t->_text.mid(_localFrom, lineEnd - _localFrom);
		auto lineStart = delta;
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
			bool selectFromStart = (_selection.to > _lineStart) && (_lineStart > 0) && (_selection.from <= _lineStart);
			bool selectTillEnd = (_selection.to >= _lineEnd) && (_lineEnd < _t->_text.size()) && (_selection.from < _lineEnd) && (!_endBlock || _endBlock->type() != TextBlockTSkip);

			if ((selectFromStart && _parDirection == Qt::LeftToRight) || (selectTillEnd && _parDirection == Qt::RightToLeft)) {
				if (x > _x) {
					fillSelectRange(_x, x);
				}
			}
			if ((selectTillEnd && _parDirection == Qt::LeftToRight) || (selectFromStart && _parDirection == Qt::RightToLeft)) {
				if (x < _x + _wLeft) {
					fillSelectRange(x + _w - _wLeft, _x + _w);
				}
			}
		}
		if (trimmedLineEnd == _lineStart && !elidedLine) return true;

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
					emojiDraw(*_p, static_cast<EmojiBlock*>(currentBlock)->emoji, (glyphX + st::emojiPadding).toInt(), _y + _yDelta + emojiY);
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

	void elideSaveBlock(int32 blockIndex, ITextBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
		if (_elideSavedBlock) {
			restoreAfterElided();
		}

		_elideSavedIndex = blockIndex;
		auto mutableText = const_cast<Text*>(_t);
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

	void prepareElidedLine(QString &lineText, int32 lineStart, int32 &lineLength, ITextBlock *&_endBlock, int repeat = 0) {
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
			const_cast<Text*>(_t)->_blocks[_elideSavedIndex] = std::move(_elideSavedBlock);
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
		style::font result = f;
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
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				result = st::semiboldFont;
			}
		}
		return result;
	}

	void eSetFont(ITextBlock *block) {
		style::font newFont = _t->_st->font;
		int flags = block->flags();
		if (flags) {
			newFont = applyFlags(flags, _t->_st->font);
		}
		if (block->lnkIndex()) {
			if (ClickHandler::showAsActive(_t->_links.at(block->lnkIndex() - 1))) {
				if (_t->_st->font != _t->_st->linkFontOver) {
					newFont = _t->_st->linkFontOver;
				}
			} else {
				if (_t->_st->font != _t->_st->linkFont) {
					newFont = _t->_st->linkFont;
				}
			}
		}
		if (newFont != _f) {
			if (newFont->family() == _t->_st->font->family()) {
				newFont = applyFlags(flags | newFont->flags(), _t->_st->font);
			}
			_f = newFont;
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
											Text::TextBlocks::const_iterator i) {
		Text::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

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

		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

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
	void applyBlockProperties(ITextBlock *block) {
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
	const Text *_t = nullptr;
	bool _elideLast = false;
	bool _breakEverywhere = false;
	int _elideRemoveFromEnd = 0;
	style::align _align = style::al_topleft;
	QPen _originalPen;
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
	Text::TextBlocks::const_iterator _parStartBlock;
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
	std::unique_ptr<ITextBlock> _elideSavedBlock;

	int _lineStart = 0;
	int _localFrom = 0;
	int _lineStartBlock = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	Text::StateRequest _lookupRequest;
	Text::StateResult _lookupResult;

};

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

Text::Text(int32 minResizeWidth) : _minResizeWidth(minResizeWidth) {
}

Text::Text(const style::TextStyle &st, const QString &text, const TextParseOptions &options, int32 minResizeWidth, bool richText) : _minResizeWidth(minResizeWidth) {
	if (richText) {
		setRichText(st, text, options);
	} else {
		setText(st, text, options);
	}
}

Text::Text(const Text &other)
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

Text::Text(Text &&other)
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

Text &Text::operator=(const Text &other) {
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

Text &Text::operator=(Text &&other) {
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

void Text::setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options) {
	_st = &st;
	clear();
	{
		TextParser parser(this, text, options);
	}
	recountNaturalSize(true, options.dir);
}

void Text::recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir) {
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
					dir = TextParser::stringDirection(_text, lastNewlineStart, b->from());
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
			dir = TextParser::stringDirection(_text, lastNewlineStart, _text.size());
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

void Text::setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options) {
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
//		TextParser parser(this, { newText, EntitiesInText() }, options);

		TextParser parser(this, textWithEntities, options);
	}
	recountNaturalSize(true, options.dir);
}

void Text::setRichText(const style::TextStyle &st, const QString &text, TextParseOptions options, const TextCustomTagsMap &custom) {
	QString parsed;
	parsed.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
	for (const QChar *b = s, *e = b + text.size(); ch != e; ++ch) {
		if (ch->unicode() == '\\') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch + 1;

			if (s < e) ++ch;
			continue;
		}
		if (ch->unicode() == '[') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch;

			const QChar *tag = ch + 1;
			if (tag >= e) continue;

			bool closing = false, other = false;
			if (tag->unicode() == '/') {
				closing = true;
				if (++tag >= e) continue;
			}

			TextCommands cmd;
			switch (tag->unicode()) {
			case 'b': cmd = closing ? TextCommandNoBold : TextCommandBold; break;
			case 'i': cmd = closing ? TextCommandNoItalic : TextCommandItalic; break;
			case 'u': cmd = closing ? TextCommandNoUnderline : TextCommandUnderline; break;
			default : other = true; break;
			}

			if (!other) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(TextCommand).append(QChar(cmd)).append(TextCommand);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (tag->unicode() != 'a') {
				TextCustomTagsMap::const_iterator i = custom.constFind(*tag);
				if (++tag >= e || tag->unicode() != ']' || i == custom.cend()) continue;
				parsed.append(closing ? i->second : i->first);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (closing) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(textcmdStopLink());
				ch = tag;
				s = ch + 1;
				continue;
			}
			if (++tag >= e || tag->unicode() != ' ') continue;
			while (tag < e && tag->unicode() == ' ') ++tag;
			if (tag + 5 < e && text.midRef(tag - b, 6) == qsl("href=\"")) {
				tag += 6;
				const QChar *tagend = tag;
				while (tagend < e && tagend->unicode() != '"') ++tagend;
				if (++tagend >= e || tagend->unicode() != ']') continue;
				parsed.append(textcmdStartLink(QString(tag, tagend - 1 - tag)));
				ch = tagend;
				s = ch + 1;
				continue;
			}
		}
	}
	if (ch > s) parsed.append(s, ch - s);
	s = ch;

	options.flags |= TextParseRichText;
	setText(st, parsed, options);
}

void Text::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

bool Text::hasLinks() const {
	return !_links.isEmpty();
}

bool Text::hasSkipBlock() const {
	return _blocks.empty() ? false : _blocks.back()->type() == TextBlockTSkip;
}

void Text::setSkipBlock(int32 width, int32 height) {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockTSkip) {
		auto block = static_cast<SkipBlock*>(_blocks.back().get());
		if (block->width() == width && block->height() == height) return;
		_text.resize(block->from());
		_blocks.pop_back();
	}
	_text.push_back('_');
	_blocks.push_back(std::make_unique<SkipBlock>(_st->font, _text, _text.size() - 1, width, height, 0));
	recountNaturalSize(false);
}

void Text::removeSkipBlock() {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockTSkip) {
		_text.resize(_blocks.back()->from());
		_blocks.pop_back();
		recountNaturalSize(false);
	}
}

int Text::countWidth(int width) const {
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

int Text::countHeight(int width) const {
	if (QFixed(width) >= _maxWidth) {
		return _minHeight;
	}
	int result = 0;
	enumerateLines(width, [&result](QFixed lineWidth, int lineHeight) {
		result += lineHeight;
	});
	return result;
}

void Text::countLineWidths(int width, QVector<int> *lineWidths) const {
	enumerateLines(width, [lineWidths](QFixed lineWidth, int lineHeight) {
		lineWidths->push_back(lineWidth.ceil().toInt());
	});
}

template <typename Callback>
void Text::enumerateLines(int w, Callback callback) const {
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

void Text::draw(Painter &painter, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection, bool fullWidthSelection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.draw(left, top, w, align, yFrom, yTo, selection, fullWidthSelection);
}

void Text::drawElided(Painter &painter, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.drawElided(left, top, w, align, lines, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

Text::StateResult Text::getState(QPoint point, int width, StateRequest request) const {
	TextPainter p(0, this);
	return p.getState(point, width, request);
}

Text::StateResult Text::getStateElided(QPoint point, int width, StateRequestElided request) const {
	TextPainter p(0, this);
	return p.getStateElided(point, width, request);
}

TextSelection Text::adjustSelection(TextSelection selection, TextSelectType selectType) const {
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

bool Text::isEmpty() const {
	return _blocks.empty() || _blocks[0]->type() == TextBlockTSkip;
}

uint16 Text::countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const {
	return (i + 1 == e) ? _text.size() : (*(i + 1))->from();
}

uint16 Text::countBlockLength(const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) const {
	return countBlockEnd(i, e) - (*i)->from();
}

template <typename AppendPartCallback, typename ClickHandlerStartCallback, typename ClickHandlerFinishCallback, typename FlagsChangeCallback>
void Text::enumerateText(TextSelection selection, AppendPartCallback appendPartCallback, ClickHandlerStartCallback clickHandlerStartCallback, ClickHandlerFinishCallback clickHandlerFinishCallback, FlagsChangeCallback flagsChangeCallback) const {
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

		bool checkBlockFlags = (blockFrom >= selection.from) && (blockFrom <= selection.to);
		if (checkBlockFlags && blockFlags != flags) {
			flagsChangeCallback(flags, blockFlags);
			flags = blockFlags;
		}
		if (blockLnkIndex && !_links.at(blockLnkIndex - 1)) { // ignore empty links
			blockLnkIndex = 0;
		}
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) {
				auto rangeFrom = qMax(selection.from, lnkFrom);
				auto rangeTo = qMin(blockFrom, selection.to);
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
		if (i == e || blockFrom >= selection.to) break;

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

TextWithEntities Text::originalTextWithEntities(TextSelection selection, ExpandLinksMode mode) const {
	TextWithEntities result;
	result.text.reserve(_text.size());

	int lnkStart = 0, italicStart = 0, boldStart = 0, codeStart = 0, preStart = 0;
	auto flagsChangeCallback = [&result, &italicStart, &boldStart, &codeStart, &preStart](int32 oldFlags, int32 newFlags) {
		if ((oldFlags & TextBlockFItalic) && !(newFlags & TextBlockFItalic)) { // write italic
			result.entities.push_back(EntityInText(EntityInTextItalic, italicStart, result.text.size() - italicStart));
		} else if ((newFlags & TextBlockFItalic) && !(oldFlags & TextBlockFItalic)) {
			italicStart = result.text.size();
		}
		if ((oldFlags & TextBlockFSemibold) && !(newFlags & TextBlockFSemibold)) {
			result.entities.push_back(EntityInText(EntityInTextBold, boldStart, result.text.size() - boldStart));
		} else if ((newFlags & TextBlockFSemibold) && !(oldFlags & TextBlockFSemibold)) {
			boldStart = result.text.size();
		}
		if ((oldFlags & TextBlockFCode) && !(newFlags & TextBlockFCode)) {
			result.entities.push_back(EntityInText(EntityInTextCode, codeStart, result.text.size() - codeStart));
		} else if ((newFlags & TextBlockFCode) && !(oldFlags & TextBlockFCode)) {
			codeStart = result.text.size();
		}
		if ((oldFlags & TextBlockFPre) && !(newFlags & TextBlockFPre)) {
			result.entities.push_back(EntityInText(EntityInTextPre, preStart, result.text.size() - preStart));
		} else if ((newFlags & TextBlockFPre) && !(oldFlags & TextBlockFPre)) {
			preStart = result.text.size();
		}
	};
	auto clickHandlerStartCallback = [&result, &lnkStart]() {
		lnkStart = result.text.size();
	};
	auto clickHandlerFinishCallback = [mode, &result, &lnkStart](const QStringRef &r, const ClickHandlerPtr &handler) {
		auto expanded = handler->getExpandedLinkTextWithEntities(mode, lnkStart, r);
		if (expanded.text.isEmpty()) {
			result.text += r;
		} else {
			result.text += expanded.text;
		}
		if (!expanded.entities.isEmpty()) {
			result.entities.append(expanded.entities);
		}
	};
	auto appendPartCallback = [&result](const QStringRef &r) {
		result.text += r;
	};

	enumerateText(selection, appendPartCallback, clickHandlerStartCallback, clickHandlerFinishCallback, flagsChangeCallback);

	return result;
}

QString Text::originalText(TextSelection selection, ExpandLinksMode mode) const {
	QString result;
	result.reserve(_text.size());

	auto appendPartCallback = [&result](const QStringRef &r) {
		result += r;
	};
	auto clickHandlerStartCallback = []() {
	};
	auto clickHandlerFinishCallback = [mode, &result](const QStringRef &r, const ClickHandlerPtr &handler) {
		auto expanded = handler->getExpandedLinkText(mode, r);
		if (expanded.isEmpty()) {
			result += r;
		} else {
			result += expanded;
		}
	};
	auto flagsChangeCallback = [](int32 oldFlags, int32 newFlags) {
	};

	enumerateText(selection, appendPartCallback, clickHandlerStartCallback, clickHandlerFinishCallback, flagsChangeCallback);

	return result;
}

void Text::clear() {
	clearFields();
	_text.clear();
}

void Text::clearFields() {
	_blocks.clear();
	_links.clear();
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

Text::~Text() = default;

void emojiDraw(QPainter &p, EmojiPtr e, int x, int y) {
	auto size = Ui::Emoji::Size();
	p.drawPixmap(QPoint(x, y), App::emoji(), QRect(e->x() * size, e->y() * size, size, size));
}
