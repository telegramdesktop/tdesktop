/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"

namespace Data {
class Reactions;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
class Message;
struct TextState;
} // namespace HistoryView

namespace HistoryView::Reactions {

struct InlineListData {
	enum class Flag : uchar {
		InBubble  = 0x01,
		OutLayout = 0x02,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	base::flat_map<QString, int> reactions;
	QString chosenReaction;
	Flags flags = {};
};

class InlineList final : public Object {
public:
	using Data = InlineListData;
	InlineList(
		not_null<::Data::Reactions*> owner,
		Fn<ClickHandlerPtr(QString)> handlerFactory,
		Data &&data);

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] int placeAndResizeGetHeight(QRect available);
	void flipToRight();

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	void paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const;
	[[nodiscard]] bool getState(
		QPoint point,
		not_null<TextState*> outResult) const;

private:
	struct Button {
		QRect geometry;
		mutable QImage image;
		mutable ClickHandlerPtr link;
		QString emoji;
		QString countText;
		int count = 0;
		int countTextWidth = 0;
	};

	void layout();
	void layoutButtons();

	void setButtonCount(Button &button, int count);
	[[nodiscard]] Button prepareButtonWithEmoji(const QString &emoji);

	QSize countOptimalSize() override;

	const not_null<::Data::Reactions*> _owner;
	const Fn<ClickHandlerPtr(QString)> _handlerFactory;
	Data _data;
	std::vector<Button> _buttons;
	QSize _skipBlock;

};

[[nodiscard]] InlineListData InlineListDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
