/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "base/flags.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class Message;

class BottomInfo {
public:
	struct Data {
		enum class Flag {
			Edited         = 0x01,
			OutLayout      = 0x02,
			Sending        = 0x04,
			RepliesContext = 0x08,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		QDateTime date;
		QString author;
		base::flat_map<QString, int> reactions;
		std::optional<int> views;
		std::optional<int> replies;
		Flags flags;
	};
	explicit BottomInfo(Data &&data);

	void update(Data &&data, int availableWidth);

	[[nodiscard]] QSize optimalSize() const;
	[[nodiscard]] QSize size() const;
	[[nodiscard]] int firstLineWidth() const;
	[[nodiscard]] bool pointInTime(QPoint position) const;
	[[nodiscard]] bool isSignedAuthorElided() const;

	void paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const;

	int resizeToWidth(int newWidth);

private:
	void layout();
	void layoutDateText();
	void layoutViewsText();
	void layoutRepliesText();
	void layoutReactionsText();
	void countOptimalSize();

	Data _data;
	QSize _optimalSize;
	QSize _size;
	Ui::Text::String _authorEditedDate;
	Ui::Text::String _views;
	Ui::Text::String _replies;
	Ui::Text::String _reactions;
	int _dateWidth = 0;
	bool _authorElided = false;

};

[[nodiscard]] BottomInfo::Data BottomInfoDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
