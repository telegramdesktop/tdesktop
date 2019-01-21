/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "private/qfontengine_p.h"

#include "core/click_handler.h"
#include "ui/text/text_entity.h"
#include "base/flags.h"

static const QChar TextCommand(0x0010);
enum TextCommands {
	TextCommandBold        = 0x01,
	TextCommandNoBold      = 0x02,
	TextCommandItalic      = 0x03,
	TextCommandNoItalic    = 0x04,
	TextCommandUnderline   = 0x05,
	TextCommandNoUnderline = 0x06,
	TextCommandSemibold    = 0x07,
	TextCommandNoSemibold  = 0x08,
	TextCommandLinkIndex   = 0x09, // 0 - NoLink
	TextCommandLinkText    = 0x0A,
	TextCommandSkipBlock   = 0x0D,

	TextCommandLangTag     = 0x20,
};

struct TextParseOptions {
	int32 flags;
	int32 maxw;
	int32 maxh;
	Qt::LayoutDirection dir;
};
extern const TextParseOptions _defaultOptions, _textPlainOptions;

enum class TextSelectType {
	Letters    = 0x01,
	Words      = 0x02,
	Paragraphs = 0x03,
};

struct TextSelection {
	constexpr TextSelection() : from(0), to(0) {
	}
	constexpr TextSelection(uint16 from, uint16 to) : from(from), to(to) {
	}
	constexpr bool empty() const {
		return from == to;
	}
	uint16 from;
	uint16 to;
};
inline bool operator==(TextSelection a, TextSelection b) {
	return a.from == b.from && a.to == b.to;
}
inline bool operator!=(TextSelection a, TextSelection b) {
	return !(a == b);
}

static constexpr TextSelection AllTextSelection = { 0, 0xFFFF };

typedef QPair<QString, QString> TextCustomTag; // open str and close str
typedef QMap<QChar, TextCustomTag> TextCustomTagsMap;

class ITextBlock;
class Text {
public:
	Text(int32 minResizeWidth = QFIXED_MAX);
	Text(const style::TextStyle &st, const QString &text, const TextParseOptions &options = _defaultOptions, int32 minResizeWidth = QFIXED_MAX, bool richText = false);
	Text(const Text &other);
	Text(Text &&other);
	Text &operator=(const Text &other);
	Text &operator=(Text &&other);

	int countWidth(int width) const;
	int countHeight(int width) const;
	void countLineWidths(int width, QVector<int> *lineWidths) const;
	void setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options = _defaultOptions);
	void setRichText(const style::TextStyle &st, const QString &text, TextParseOptions options = _defaultOptions, const TextCustomTagsMap &custom = TextCustomTagsMap());
	void setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options = _defaultOptions);

	void setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk);
	bool hasLinks() const;

	bool hasSkipBlock() const;
	bool updateSkipBlock(int width, int height);
	bool removeSkipBlock();

	int32 maxWidth() const {
		return _maxWidth.ceil().toInt();
	}
	int32 minHeight() const {
		return _minHeight;
	}

	void draw(Painter &p, int32 left, int32 top, int32 width, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }, bool fullWidthSelection = true) const;
	void drawElided(Painter &p, int32 left, int32 top, int32 width, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const;
	void drawLeft(Painter &p, int32 left, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }) const {
		draw(p, rtl() ? (outerw - left - width) : left, top, width, align, yFrom, yTo, selection);
	}
	void drawLeftElided(Painter &p, int32 left, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const {
		drawElided(p, rtl() ? (outerw - left - width) : left, top, width, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
	}
	void drawRight(Painter &p, int32 right, int32 top, int32 width, int32 outerw, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, TextSelection selection = { 0, 0 }) const {
		draw(p, rtl() ? right : (outerw - right - width), top, width, align, yFrom, yTo, selection);
	}
	void drawRightElided(Painter &p, int32 right, int32 top, int32 width, int32 outerw, int32 lines = 1, style::align align = style::al_left, int32 yFrom = 0, int32 yTo = -1, int32 removeFromEnd = 0, bool breakEverywhere = false, TextSelection selection = { 0, 0 }) const {
		drawElided(p, rtl() ? right : (outerw - right - width), top, width, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
	}

	struct StateRequest {
		enum class Flag {
			BreakEverywhere     = (1 << 0),
			LookupSymbol        = (1 << 1),
			LookupLink          = (1 << 2),
			LookupCustomTooltip = (1 << 3),
		};
		using Flags = base::flags<Flag>;
		friend inline constexpr auto is_flag_type(Flag) { return true; };

		StateRequest() {
		}

		style::align align = style::al_left;
		Flags flags = Flag::LookupLink;
	};
	struct StateResult {
		ClickHandlerPtr link;
		bool uponSymbol = false;
		bool afterSymbol = false;
		uint16 symbol = 0;
	};
	StateResult getState(QPoint point, int width, StateRequest request = StateRequest()) const;
	StateResult getStateLeft(QPoint point, int width, int outerw, StateRequest request = StateRequest()) const {
		return getState(rtlpoint(point, outerw), width, request);
	}
	struct StateRequestElided : public StateRequest {
		StateRequestElided() {
		}
		StateRequestElided(const StateRequest &other) : StateRequest(other) {
		}
		int lines = 1;
		int removeFromEnd = 0;
    };
	StateResult getStateElided(QPoint point, int width, StateRequestElided request = StateRequestElided()) const;
	StateResult getStateElidedLeft(QPoint point, int width, int outerw, StateRequestElided request = StateRequestElided()) const {
		return getStateElided(rtlpoint(point, outerw), width, request);
	}

	[[nodiscard]] TextSelection adjustSelection(TextSelection selection, TextSelectType selectType) const;
	bool isFullSelection(TextSelection selection) const {
		return (selection.from == 0) && (selection.to >= _text.size());
	}

	bool isEmpty() const;
	bool isNull() const {
		return !_st;
	}
	int length() const {
		return _text.size();
	}

	TextWithEntities originalTextWithEntities(TextSelection selection = AllTextSelection, ExpandLinksMode mode = ExpandLinksShortened) const;
	QString originalText(TextSelection selection = AllTextSelection, ExpandLinksMode mode = ExpandLinksShortened) const;

	bool lastDots(int32 dots, int32 maxdots = 3) { // hack for typing animation
		if (_text.size() < maxdots) return false;

		int32 nowDots = 0, from = _text.size() - maxdots, to = _text.size();
		for (int32 i = from; i < to; ++i) {
			if (_text.at(i) == QChar('.')) {
				++nowDots;
			}
		}
		if (nowDots == dots) return false;
		for (int32 j = from; j < from + dots; ++j) {
			_text[j] = QChar('.');
		}
		for (int32 j = from + dots; j < to; ++j) {
			_text[j] = QChar(' ');
		}
		return true;
	}

	const style::TextStyle *style() const {
		return _st;
	}

	void clear();
	~Text();

private:
	using TextBlocks = std::vector<std::unique_ptr<ITextBlock>>;
	using TextLinks = QVector<ClickHandlerPtr>;

	uint16 countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const;
	uint16 countBlockLength(const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) const;

	// Template method for originalText(), originalTextWithEntities().
	template <typename AppendPartCallback, typename ClickHandlerStartCallback, typename ClickHandlerFinishCallback, typename FlagsChangeCallback>
	void enumerateText(TextSelection selection, AppendPartCallback appendPartCallback, ClickHandlerStartCallback clickHandlerStartCallback, ClickHandlerFinishCallback clickHandlerFinishCallback, FlagsChangeCallback flagsChangeCallback) const;

	// Template method for countWidth(), countHeight(), countLineWidths().
	// callback(lineWidth, lineHeight) will be called for all lines with:
	// QFixed lineWidth, int lineHeight
	template <typename Callback>
	void enumerateLines(int w, Callback callback) const;

	void recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir = Qt::LayoutDirectionAuto);

	// clear() deletes all blocks and calls this method
	// it is also called from move constructor / assignment operator
	void clearFields();

	QFixed _minResizeWidth;
	QFixed _maxWidth = 0;
	int32 _minHeight = 0;

	QString _text;
	const style::TextStyle *_st = nullptr;

	TextBlocks _blocks;
	TextLinks _links;

	Qt::LayoutDirection _startDir = Qt::LayoutDirectionAuto;

	friend class TextParser;
	friend class TextPainter;

};
inline TextSelection snapSelection(int from, int to) {
	return { static_cast<uint16>(snap(from, 0, 0xFFFF)), static_cast<uint16>(snap(to, 0, 0xFFFF)) };
}
inline TextSelection shiftSelection(TextSelection selection, uint16 byLength) {
	return snapSelection(int(selection.from) + byLength, int(selection.to) + byLength);
}
inline TextSelection unshiftSelection(TextSelection selection, uint16 byLength) {
	return snapSelection(int(selection.from) - int(byLength), int(selection.to) - int(byLength));
}
inline TextSelection shiftSelection(TextSelection selection, const Text &byText) {
	return shiftSelection(selection, byText.length());
}
inline TextSelection unshiftSelection(TextSelection selection, const Text &byText) {
	return unshiftSelection(selection, byText.length());
}

// textcmd
QString textcmdSkipBlock(ushort w, ushort h);
QString textcmdStartLink(ushort lnkIndex);
QString textcmdStartLink(const QString &url);
QString textcmdStopLink();
QString textcmdLink(ushort lnkIndex, const QString &text);
QString textcmdLink(const QString &url, const QString &text);
QString textcmdStartSemibold();
QString textcmdStopSemibold();
const QChar *textSkipCommand(const QChar *from, const QChar *end, bool canLink = true);

inline bool chIsSpace(QChar ch, bool rich = false) {
	return ch.isSpace() || (ch < 32 && !(rich && ch == TextCommand)) || (ch == QChar::ParagraphSeparator) || (ch == QChar::LineSeparator) || (ch == QChar::ObjectReplacementCharacter) || (ch == QChar::CarriageReturn) || (ch == QChar::Tabulation);
}
inline bool chIsDiac(QChar ch) { // diac and variation selectors
	return (ch.category() == QChar::Mark_NonSpacing) || (ch == 1652) || (ch >= 64606 && ch <= 64611);
}

bool chIsBad(QChar ch);

inline bool chIsTrimmed(QChar ch, bool rich = false) {
	return (!rich || ch != TextCommand) && (chIsSpace(ch) || chIsBad(ch));
}
inline bool chReplacedBySpace(QChar ch) {
	// \xe2\x80[\xa8 - \xac\xad] // 8232 - 8237
	// QString from1 = QString::fromUtf8("\xe2\x80\xa8"), to1 = QString::fromUtf8("\xe2\x80\xad");
	// \xcc[\xb3\xbf\x8a] // 819, 831, 778
	// QString bad1 = QString::fromUtf8("\xcc\xb3"), bad2 = QString::fromUtf8("\xcc\xbf"), bad3 = QString::fromUtf8("\xcc\x8a");
	// [\x00\x01\x02\x07\x08\x0b-\x1f] // '\t' = 0x09
	return (/*code >= 0x00 && */ch <= 0x02) || (ch >= 0x07 && ch <= 0x09) || (ch >= 0x0b && ch <= 0x1f) ||
		(ch == 819) || (ch == 831) || (ch == 778) || (ch >= 8232 && ch <= 8237);
}
inline int32 chMaxDiacAfterSymbol() {
	return 2;
}
inline bool chIsNewline(QChar ch) {
	return (ch == QChar::LineFeed || ch == 156);
}
inline bool chIsLinkEnd(QChar ch) {
	return ch == TextCommand || chIsBad(ch) || chIsSpace(ch) || chIsNewline(ch) || ch.isLowSurrogate() || ch.isHighSurrogate();
}
inline bool chIsAlmostLinkEnd(QChar ch) {
	switch (ch.unicode()) {
	case '?':
	case ',':
	case '.':
	case '"':
	case ':':
	case '!':
	case '\'':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsWordSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::Space:
	case QChar::LineFeed:
	case '.':
	case ',':
	case '?':
	case '!':
	case '@':
	case '#':
	case '$':
	case ':':
	case ';':
	case '-':
	case '<':
	case '>':
	case '[':
	case ']':
	case '(':
	case ')':
	case '{':
	case '}':
	case '=':
	case '/':
	case '+':
	case '%':
	case '&':
	case '^':
	case '*':
	case '\'':
	case '"':
	case '`':
	case '~':
	case '|':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsSentenceEnd(QChar ch) {
	switch (ch.unicode()) {
	case '.':
	case '?':
	case '!':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsSentencePartEnd(QChar ch) {
	switch (ch.unicode()) {
	case ',':
	case ':':
	case ';':
		return true;
	default:
		break;
	}
	return false;
}
inline bool chIsParagraphSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::LineFeed:
		return true;
	default:
		break;
	}
	return false;
}
