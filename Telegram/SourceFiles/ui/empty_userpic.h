/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class EmptyUserpic {
public:
	struct BgColors {
		const style::color color1;
		const style::color color2;
	};

	[[nodiscard]] static int ColorIndex(uint64 id);
	[[nodiscard]] static EmptyUserpic::BgColors UserpicColor(int id);

	[[nodiscard]] static QString ExternalName();
	[[nodiscard]] static QString InaccessibleName();

	EmptyUserpic(const BgColors &colors, const QString &name);

	void paint(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) const;
	void paintRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		int radius = 0) const;
	void paintSquare(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size) const;
	[[nodiscard]] QPixmap generate(int size);
	[[nodiscard]] std::pair<uint64, uint64> uniqueKey() const;

	static void PaintSavedMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintSavedMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintSavedMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg);
	static void PaintSavedMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg);
	[[nodiscard]] static QPixmap GenerateSavedMessages(int size);
	[[nodiscard]] static QPixmap GenerateSavedMessagesRounded(int size);

	static void PaintRepliesMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintRepliesMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintRepliesMessages(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg);
	static void PaintRepliesMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		QBrush bg,
		const style::color &fg);
	[[nodiscard]] static QPixmap GenerateRepliesMessages(int size);
	[[nodiscard]] static QPixmap GenerateRepliesMessagesRounded(int size);

	~EmptyUserpic();

private:
	void paint(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		Fn<void()> paintBackground) const;

	void fillString(const QString &name);

	const BgColors _colors;
	QString _string;

};

} // namespace Ui
