/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"

namespace Ui {
class ChatStyle;
} // namespace Ui

namespace HistoryView {

class Message;

class Reactions final : public Object {
public:
	struct Data {
		base::flat_map<QString, int> reactions;
		QString chosenReaction;
	};

	explicit Reactions(Data &&data);

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	void paint(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const QRect &clip) const;

private:
	void layout();
	void layoutReactionsText();

	QSize countOptimalSize() override;

	Data _data;
	Ui::Text::String _reactions;

};

[[nodiscard]] Reactions::Data ReactionsDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
