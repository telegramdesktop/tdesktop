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
	EmptyUserpic(const style::color &color, const QString &name);

	void paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) const;
	void paintRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) const;
	void paintSquare(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) const;
	QPixmap generate(int size);
	InMemoryKey uniqueKey() const;

	static void PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintSavedMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size);
	static void PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg);
	static void PaintSavedMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg);
	static QPixmap GenerateSavedMessages(int size);
	static QPixmap GenerateSavedMessagesRounded(int size);

	~EmptyUserpic();

private:
	template <typename Callback>
	void paint(
		Painter &p,
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
