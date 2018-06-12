/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_block.h"

#include "core/crash_reports.h"

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace {

struct ScriptLine {
	ScriptLine() : length(0), textWidth(0) {
	}

	int32 length;
	QFixed textWidth;
};

// All members finished with "_" are internal.
struct LineBreakHelper
{
	LineBreakHelper()
		: glyphCount(0), maxGlyphs(INT_MAX), currentPosition(0), fontEngine(0), logClusters(0)
	{
	}


	ScriptLine tmpData;
	ScriptLine spaceData;

	QGlyphLayout glyphs;

	int glyphCount;
	int maxGlyphs;
	int currentPosition;

	glyph_t previousGlyph_ = 0;
	QFontEngine *previousFontEngine_ = nullptr;

	QFixed rightBearing;

	QFontEngine *fontEngine;
	const unsigned short *logClusters;

	inline glyph_t currentGlyph() const
	{
		Q_ASSERT(currentPosition > 0);
		Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

		return glyphs.glyphs[logClusters[currentPosition - 1]];
	}

	inline void saveCurrentGlyph()
	{
		if (currentPosition > 0 &&
			logClusters[currentPosition - 1] < glyphs.numGlyphs) {
			previousGlyph_ = currentGlyph(); // needed to calculate right bearing later
			previousFontEngine_ = fontEngine;
		} else {
			previousGlyph_ = 0;
			previousFontEngine_ = nullptr;
		}
	}

	inline void calculateRightBearing(QFontEngine *engine, glyph_t glyph)
	{
		qreal rb;
		engine->getGlyphBearings(glyph, 0, &rb);

		// We only care about negative right bearings, so we limit the range
		// of the bearing here so that we can assume it's negative in the rest
		// of the code, as well ase use QFixed(1) as a sentinel to represent
		// the state where we have yet to compute the right bearing.
		rightBearing = qMin(QFixed::fromReal(rb), QFixed(0));
	}

	inline void calculateRightBearing()
	{
		if (currentPosition > 0 &&
			logClusters[currentPosition - 1] < glyphs.numGlyphs) {
			calculateRightBearing(fontEngine, currentGlyph());
		} else {
			rightBearing = 0;
		}
	}

	inline void calculateRightBearingForPreviousGlyph()
	{
		if (previousGlyph_ > 0) {
			calculateRightBearing(previousFontEngine_, previousGlyph_);
		} else {
			rightBearing = 0;
		}
	}

	// We always calculate the right bearing right before it is needed.
	// So we don't need caching / optimizations referred to delayed right bearing calculations.

	//static const QFixed RightBearingNotCalculated;

	//inline void resetRightBearing()
	//{
	//	rightBearing = RightBearingNotCalculated;
	//}

	// We express the negative right bearing as an absolute number
	// so that it can be applied to the width using addition.
	inline QFixed negativeRightBearing() const
	{
		//if (rightBearing == RightBearingNotCalculated)
		//	return QFixed(0);

		return qAbs(rightBearing);
	}

};

//const QFixed LineBreakHelper::RightBearingNotCalculated = QFixed(1);

static inline void addNextCluster(int &pos, int end, ScriptLine &line, int &glyphCount,
	const QScriptItem &current, const unsigned short *logClusters,
	const QGlyphLayout &glyphs)
{
	int glyphPosition = logClusters[pos];
	do { // got to the first next cluster
		++pos;
		++line.length;
	} while (pos < end && logClusters[pos] == glyphPosition);
	do { // calculate the textWidth for the rest of the current cluster.
		if (!glyphs.attributes[glyphPosition].dontPrint)
			line.textWidth += glyphs.advances[glyphPosition];
		++glyphPosition;
	} while (glyphPosition < current.num_glyphs && !glyphs.attributes[glyphPosition].clusterStart);

	Q_ASSERT((pos == end && glyphPosition == current.num_glyphs) || logClusters[pos] == glyphPosition);

	++glyphCount;
}

} // anonymous namespace

class BlockParser {
public:

	BlockParser(QTextEngine *e, TextBlock *b, QFixed minResizeWidth, int32 blockFrom, const QString &str)
		: block(b), eng(e), str(str) {
		parseWords(minResizeWidth, blockFrom);
	}

	void parseWords(QFixed minResizeWidth, int32 blockFrom) {
		LineBreakHelper lbh;

// Helper for debugging crashes in text processing.
//
//		auto debugChars = QString();
//		debugChars.reserve(str.size() * 7);
//		for (const auto ch : str) {
//			debugChars.append(
//				"0x").append(
//				QString::number(ch.unicode(), 16).toUpper()).append(
//				' ');
//		}
//		LOG(("Text: %1, chars: %2").arg(str).arg(debugChars));

		int item = -1;
		int newItem = eng->findItem(0);

		style::align alignment = eng->option.alignment();

		const QCharAttributes *attributes = eng->attributes();
		if (!attributes)
			return;
		int end = 0;
		lbh.logClusters = eng->layoutData->logClustersPtr;

		block->_words.clear();

		int wordStart = lbh.currentPosition;

		bool addingEachGrapheme = false;
		int lastGraphemeBoundaryPosition = -1;
		ScriptLine lastGraphemeBoundaryLine;

		while (newItem < eng->layoutData->items.size()) {
			if (newItem != item) {
				item = newItem;
				const QScriptItem &current = eng->layoutData->items[item];
				if (!current.num_glyphs) {
					eng->shape(item);
					attributes = eng->attributes();
					if (!attributes)
						return;
					lbh.logClusters = eng->layoutData->logClustersPtr;
				}
				lbh.currentPosition = current.position;
				end = current.position + eng->length(item);
				lbh.glyphs = eng->shapedGlyphs(&current);
				QFontEngine *fontEngine = eng->fontEngine(current);
				if (lbh.fontEngine != fontEngine) {
					lbh.fontEngine = fontEngine;
				}
			}
			const QScriptItem &current = eng->layoutData->items[item];

			if (attributes[lbh.currentPosition].whiteSpace) {
				while (lbh.currentPosition < end && attributes[lbh.currentPosition].whiteSpace)
					addNextCluster(lbh.currentPosition, end, lbh.spaceData, lbh.glyphCount,
						current, lbh.logClusters, lbh.glyphs);

				if (block->_words.isEmpty()) {
					block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
				}
				block->_words.back().add_rpadding(lbh.spaceData.textWidth);
				block->_width += lbh.spaceData.textWidth;
				lbh.spaceData.length = 0;
				lbh.spaceData.textWidth = 0;

				wordStart = lbh.currentPosition;

				addingEachGrapheme = false;
				lastGraphemeBoundaryPosition = -1;
				lastGraphemeBoundaryLine = ScriptLine();
			} else {
				do {
					addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
						current, lbh.logClusters, lbh.glyphs);

					if (lbh.currentPosition >= eng->layoutData->string.length()
						|| attributes[lbh.currentPosition].whiteSpace
						|| isLineBreak(attributes, lbh.currentPosition)) {
						lbh.calculateRightBearing();
						block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
						block->_width += lbh.tmpData.textWidth;
						lbh.tmpData.textWidth = 0;
						lbh.tmpData.length = 0;
						wordStart = lbh.currentPosition;
						break;
					} else if (attributes[lbh.currentPosition].graphemeBoundary) {
						if (!addingEachGrapheme && lbh.tmpData.textWidth > minResizeWidth) {
							if (lastGraphemeBoundaryPosition >= 0) {
								lbh.calculateRightBearingForPreviousGlyph();
								block->_words.push_back(TextWord(wordStart + blockFrom, -lastGraphemeBoundaryLine.textWidth, -lbh.negativeRightBearing()));
								block->_width += lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
								wordStart = lastGraphemeBoundaryPosition;
							}
							addingEachGrapheme = true;
						}
						if (addingEachGrapheme) {
							lbh.calculateRightBearing();
							block->_words.push_back(TextWord(wordStart + blockFrom, -lbh.tmpData.textWidth, -lbh.negativeRightBearing()));
							block->_width += lbh.tmpData.textWidth;
							lbh.tmpData.textWidth = 0;
							lbh.tmpData.length = 0;
							wordStart = lbh.currentPosition;
						} else {
							lastGraphemeBoundaryPosition = lbh.currentPosition;
							lastGraphemeBoundaryLine = lbh.tmpData;
							lbh.saveCurrentGlyph();
						}
					}
				} while (lbh.currentPosition < end);
			}
			if (lbh.currentPosition == end)
				newItem = item + 1;
		}
		if (!block->_words.isEmpty()) {
			block->_rpadding = block->_words.back().f_rpadding();
			block->_width -= block->_rpadding;
			block->_words.squeeze();
		}
	}

	bool isLineBreak(const QCharAttributes *attributes, int32 index) {
		bool lineBreak = attributes[index].lineBreak;
		if (lineBreak && block->lnkIndex() > 0 && index > 0 && str.at(index - 1) == '/') {
			return false; // don't break after / in links
		}
		return lineBreak;
	}

private:

	TextBlock *block;
	QTextEngine *eng;
	const QString &str;

};

QFixed ITextBlock::f_rbearing() const {
	return (type() == TextBlockTText) ? static_cast<const TextBlock*>(this)->real_f_rbearing() : 0;
}

TextBlock::TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, uint16 lnkIndex) : ITextBlock(font, str, from, length, flags, lnkIndex) {
	_flags |= ((TextBlockTText & 0x0F) << 8);
	if (length) {
		style::font blockFont = font;
		if (!flags && lnkIndex) {
			// should use TextStyle lnkFlags somehow... not supported
		}

		if ((flags & TextBlockFPre) || (flags & TextBlockFCode)) {
			blockFont = App::monofont();
			if (blockFont->size() != font->size() || blockFont->flags() != font->flags()) {
				blockFont = style::font(font->size(), font->flags(), blockFont->family());
			}
		} else {
			if (flags & TextBlockFBold) {
				blockFont = blockFont->bold();
			} else if (flags & TextBlockFSemibold) {
				blockFont = st::semiboldFont;
				if (blockFont->size() != font->size() || blockFont->flags() != font->flags()) {
					blockFont = style::font(font->size(), font->flags(), blockFont->family());
				}
			}
			if (flags & TextBlockFItalic) blockFont = blockFont->italic();
			if (flags & TextBlockFUnderline) blockFont = blockFont->underline();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				blockFont = st::semiboldFont;
			}
		}

		const auto part = str.mid(_from, length);

		// Attempt to catch a crash in text processing
		CrashReports::SetAnnotationRef("CrashString", &part);

		QStackTextEngine engine(part, blockFont->f);
		BlockParser parser(&engine, this, minResizeWidth, _from, part);

		CrashReports::ClearAnnotationRef("CrashString");
	}
}

EmojiBlock::EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, uint16 lnkIndex, EmojiPtr emoji) : ITextBlock(font, str, from, length, flags, lnkIndex)
, emoji(emoji) {
	_flags |= ((TextBlockTEmoji & 0x0F) << 8);
	_width = int(st::emojiSize + 2 * st::emojiPadding);

	_rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = str[_from + (--i)];
		if (ch.unicode() == QChar::Space) {
			_rpadding += font->spacew;
		} else {
			break;
		}
	}
}

SkipBlock::SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex) : ITextBlock(font, str, from, 1, 0, lnkIndex), _height(h) {
	_flags |= ((TextBlockTSkip & 0x0F) << 8);
	_width = w;
}
