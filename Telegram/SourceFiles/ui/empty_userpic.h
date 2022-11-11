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
	[[nodiscard]] static QString ExternalName();
	[[nodiscard]] static QString InaccessibleName();

	EmptyUserpic(const style::color &color, const QString &name);

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
	QPixmap generate(int size);
	InMemoryKey uniqueKey() const;

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
		const style::color &bg,
		const style::color &fg);
	static void PaintSavedMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg);
	static QPixmap GenerateSavedMessages(int size);
	static QPixmap GenerateSavedMessagesRounded(int size);

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
		const style::color &bg,
		const style::color &fg);
	static void PaintRepliesMessagesRounded(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg);
	static QPixmap GenerateRepliesMessages(int size);
	static QPixmap GenerateRepliesMessagesRounded(int size);

	~EmptyUserpic();

private:
	template <typename Callback>
	void paint(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		Callback paintBackground) const;

	void fillString(const QString &name);

	style::color _color;
	QString _string;

};

} // namespace Ui
