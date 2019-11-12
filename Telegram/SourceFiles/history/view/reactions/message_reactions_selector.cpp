/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/message_reactions_selector.h"

#include "ui/widgets/discrete_sliders.h"
#include "styles/style_layers.h"

namespace HistoryView {

not_null<Selector*> CreateReactionSelector(
		not_null<QWidget*> parent,
		const base::flat_map<QString, int> &items) {
	const auto sectionsCount = int(items.size() + 1);
	const auto result = Ui::CreateChild<Selector>(parent.get());
	using Entry = std::pair<int, QString>;
	auto sorted = std::vector<Entry>();
	for (const auto &[reaction, count] : items) {
		sorted.emplace_back(count, reaction);
	}
	ranges::sort(sorted, std::greater<>(), &Entry::first);
	const auto count = ranges::accumulate(
		sorted,
		0,
		std::plus<>(),
		&Entry::first);
	auto labels = QStringList() << ("ALL (" + QString::number(count) + ")");
	for (const auto &[count, reaction] : sorted) {
		labels.append(reaction + " (" + QString::number(count) + ")");
	}
	auto tabs = Ui::CreateChild<Ui::SettingsSlider>(
		parent.get(),
		st::defaultTabsSlider);
	tabs->setSections(labels | ranges::to_vector);
	tabs->setRippleTopRoundRadius(st::boxRadius);
	result->move = [=](int x, int y) {
		tabs->moveToLeft(x, y);
	};
	result->resizeToWidth = [=](int width) {
		tabs->resizeToWidth(std::min(
			width,
			sectionsCount * st::defaultTabsSlider.height * 2));
	};
	result->height = [=] {
		return tabs->height() - st::lineWidth;
	};
	result->changes = [=] {
		return tabs->sectionActivated() | rpl::map([=](int section) {
			return (section > 0) ? sorted[section - 1].second : QString();
		});
	};
	return result;
}

} // namespace HistoryView
