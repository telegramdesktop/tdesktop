/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <private/qfixed_p.h>

namespace Ui {
namespace Text {

enum TextBlockType {
	TextBlockTNewline = 0x01,
	TextBlockTText = 0x02,
	TextBlockTEmoji = 0x03,
	TextBlockTSkip = 0x04,
};

enum TextBlockFlags {
	TextBlockFBold = 0x01,
	TextBlockFItalic = 0x02,
	TextBlockFUnderline = 0x04,
	TextBlockFStrikeOut = 0x08,
	TextBlockFTilde = 0x10, // tilde fix in OpenSans
	TextBlockFSemibold = 0x20,
	TextBlockFCode = 0x40,
	TextBlockFPre = 0x80,
};

class AbstractBlock {
public:
	AbstractBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, uint16 lnkIndex) : _from(from), _flags((flags & 0xFF) | ((lnkIndex & 0xFFFF) << 12)) {
	}

	uint16 from() const {
		return _from;
	}
	int32 width() const {
		return _width.toInt();
	}
	int32 rpadding() const {
		return _rpadding.toInt();
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}

	// Should be virtual, but optimized throught type() call.
	QFixed f_rbearing() const;

	uint16 lnkIndex() const {
		return (_flags >> 12) & 0xFFFF;
	}
	void setLnkIndex(uint16 lnkIndex) {
		_flags = (_flags & ~(0xFFFF << 12)) | (lnkIndex << 12);
	}

	TextBlockType type() const {
		return TextBlockType((_flags >> 8) & 0x0F);
	}
	int32 flags() const {
		return (_flags & 0xFF);
	}

	virtual std::unique_ptr<AbstractBlock> clone() const = 0;
	virtual ~AbstractBlock() {
	}

protected:
	uint16 _from = 0;

	uint32 _flags = 0; // 4 bits empty, 16 bits lnkIndex, 4 bits type, 8 bits flags

	QFixed _width = 0;

	// Right padding: spaces after the last content of the block (like a word).
	// This holds spaces after the end of the block, for example a text ending
	// with a space before a link has started. If text block has a leading spaces
	// (for example a text block after a link block) it is prepended with an empty
	// word that holds those spaces as a right padding.
	QFixed _rpadding = 0;

};

class NewlineBlock : public AbstractBlock {
public:
	NewlineBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, uint16 lnkIndex) : AbstractBlock(font, str, from, length, flags, lnkIndex), _nextDir(Qt::LayoutDirectionAuto) {
		_flags |= ((TextBlockTNewline & 0x0F) << 8);
	}

	Qt::LayoutDirection nextDirection() const {
		return _nextDir;
	}

	std::unique_ptr<AbstractBlock> clone() const override {
		return std::make_unique<NewlineBlock>(*this);
	}

private:
	Qt::LayoutDirection _nextDir;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class TextWord {
public:
	TextWord() = default;
	TextWord(uint16 from, QFixed width, QFixed rbearing, QFixed rpadding = 0)
		: _from(from)
		, _width(width)
		, _rpadding(rpadding)
		, _rbearing(rbearing.value() > 0x7FFF ? 0x7FFF : (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value())) {
	}
	uint16 from() const {
		return _from;
	}
	QFixed f_rbearing() const {
		return QFixed::fromFixed(_rbearing);
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}
	void add_rpadding(QFixed padding) {
		_rpadding += padding;
	}

private:
	uint16 _from = 0;
	QFixed _width, _rpadding;
	int16 _rbearing = 0;

};

class TextBlock : public AbstractBlock {
public:
	TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, uint16 lnkIndex);

	std::unique_ptr<AbstractBlock> clone() const override {
		return std::make_unique<TextBlock>(*this);
	}

private:
	friend class AbstractBlock;
	QFixed real_f_rbearing() const {
		return _words.isEmpty() ? 0 : _words.back().f_rbearing();
	}

	using TextWords = QVector<TextWord>;
	TextWords _words;

	friend class String;
	friend class Parser;
	friend class Renderer;
	friend class BlockParser;

};

class EmojiBlock : public AbstractBlock {
public:
	EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, uint16 lnkIndex, EmojiPtr emoji);

	std::unique_ptr<AbstractBlock> clone() const override {
		return std::make_unique<EmojiBlock>(*this);
	}

private:
	EmojiPtr emoji = nullptr;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class SkipBlock : public AbstractBlock {
public:
	SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex);

	int32 height() const {
		return _height;
	}

	std::unique_ptr<AbstractBlock> clone() const override {
		return std::make_unique<SkipBlock>(*this);
	}

private:
	int32 _height;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

} // namespace Text
} // namespace Ui
