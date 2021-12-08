/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reactions.h"

#include "history/view/history_view_message.h"
#include "history/history_message.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"

namespace HistoryView {

Reactions::Reactions(Data &&data)
: _data(std::move(data))
, _reactions(st::msgMinWidth / 2) {
	layout();
}

void Reactions::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

void Reactions::updateSkipBlock(int width, int height) {
	_reactions.updateSkipBlock(width, height);
}

void Reactions::removeSkipBlock() {
	_reactions.removeSkipBlock();
}

void Reactions::layout() {
	layoutReactionsText();
	initDimensions();
}

void Reactions::layoutReactionsText() {
	if (_data.reactions.empty()) {
		_reactions.clear();
		return;
	}
	auto sorted = ranges::view::all(
		_data.reactions
	) | ranges::view::transform([](const auto &pair) {
		return std::make_pair(pair.first, pair.second);
	}) | ranges::to_vector;
	ranges::sort(sorted, std::greater<>(), &std::pair<QString, int>::second);

	auto text = TextWithEntities();
	for (const auto &[string, count] : sorted) {
		if (!text.text.isEmpty()) {
			text.append(" - ");
		}
		const auto chosen = (_data.chosenReaction == string);
		text.append(string);
		if (_data.chosenReaction == string) {
			text.append(Ui::Text::Bold(QString::number(count)));
		} else {
			text.append(QString::number(count));
		}
	}

	_reactions.setMarkedText(
		st::msgDateTextStyle,
		text,
		Ui::NameTextOptions());
}

QSize Reactions::countOptimalSize() {
	return QSize(_reactions.maxWidth(), _reactions.minHeight());
}

QSize Reactions::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth()) {
		return optimalSize();
	}
	return { newWidth, _reactions.countHeight(newWidth) };
}

void Reactions::paint(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const QRect &clip) const {
	_reactions.draw(p, 0, 0, outerWidth);
}

Reactions::Data ReactionsDataFromMessage(not_null<Message*> message) {
	auto result = Reactions::Data();

	const auto item = message->message();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
	return result;
}

} // namespace HistoryView
