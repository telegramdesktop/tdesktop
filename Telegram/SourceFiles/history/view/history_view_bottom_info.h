/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "ui/text/text.h"
#include "base/flags.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;

class Message;
struct TextState;

class BottomInfo final : public Object {
public:
	struct Data {
		enum class Flag : uchar {
			Edited         = 0x01,
			OutLayout      = 0x02,
			Sending        = 0x04,
			RepliesContext = 0x08,
			Sponsored      = 0x10,
			//Unread, // We don't want to pass and update it in Date for now.
		};
		friend inline constexpr bool is_flag_type(Flag) { return true; };
		using Flags = base::flags<Flag>;

		QDateTime date;
		QString author;
		base::flat_map<QString, int> reactions;
		QString chosenReaction;
		std::optional<int> views;
		std::optional<int> replies;
		Flags flags;
	};
	struct Context {
		ClickHandlerPtr reactions;
	};
	BottomInfo(Data &&data, Context &&context);

	void update(Data &&data, Context &&context, int availableWidth);

	[[nodiscard]] int firstLineWidth() const;
	[[nodiscard]] TextState textState(
		not_null<const HistoryItem*> item,
		QPoint position) const;
	[[nodiscard]] bool isSignedAuthorElided() const;

	void paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const;

private:
	void layout();
	void layoutDateText();
	void layoutViewsText();
	void layoutRepliesText();
	void layoutReactionsText();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	Data _data;
	Context _context;
	Ui::Text::String _authorEditedDate;
	Ui::Text::String _views;
	Ui::Text::String _replies;
	Ui::Text::String _reactions;
	int _dateWidth = 0;
	bool _authorElided = false;

};

[[nodiscard]] BottomInfo::Data BottomInfoDataFromMessage(
	not_null<Message*> message);

[[nodiscard]] BottomInfo::Context BottomInfoContextFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
