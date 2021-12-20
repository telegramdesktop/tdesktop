/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"

class DocumentData;

namespace Data {
class Session;
class DocumentMedia;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
class Message;
} // namespace HistoryView

namespace HistoryView::Reactions {

struct InlineListData {
	enum class Flag : uchar {
		InBubble  = 0x01,
		OutLayout = 0x02,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	not_null<Data::Session*> owner;
	base::flat_map<QString, int> reactions;
	QString chosenReaction;
	Flags flags = {};
};

class InlineList final : public Object {
public:
	using Data = InlineListData;
	explicit InlineList(Data &&data);

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	void paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const;

private:
	struct Button {
		QRect geometry;
		QImage image;
		QString emoji;
		std::shared_ptr<::Data::DocumentMedia> media;
		ClickHandlerPtr link;
		QString countText;
		int count = 0;
		int countTextWidth = 0;
	};

	void layout();
	void layoutButtons();

	void setButtonCount(Button &button, int count);
	void loadButtonImage(Button &button, not_null<DocumentData*> document);
	void setButtonImage(Button &button, QImage large);
	[[nodiscard]] Button prepareButtonWithEmoji(const QString &emoji);

	void reactionsListLoaded();
	void downloadTaskFinished();
	[[nodiscard]] bool assetsLoaded() const;

	QSize countOptimalSize() override;

	Data _data;
	std::vector<Button> _buttons;
	QSize _skipBlock;

	rpl::lifetime _assetsLoadLifetime;
	bool _waitingForReactionsList = false;
	bool _waitingForDownloadTask = false;

};

[[nodiscard]] InlineListData InlineListDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
