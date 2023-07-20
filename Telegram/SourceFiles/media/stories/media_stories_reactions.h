/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Data {
struct ReactionId;
} // namespace Data

namespace HistoryView::Reactions {
class Selector;
struct ChosenReaction;
} // namespace HistoryView::Reactions

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Controller;

class Reactions final {
public:
	explicit Reactions(not_null<Controller*> controller);
	~Reactions();

	using Chosen = HistoryView::Reactions::ChosenReaction;
	[[nodiscard]] rpl::producer<bool> expandedValue() const {
		return _expanded.value();
	}
	[[nodiscard]] rpl::producer<Chosen> chosen() const {
		return _chosen.events();
	}

	void show();
	void hide();
	void hideIfCollapsed();
	void collapse();

private:
	struct Hiding;

	void create();
	void updateShowState();
	void fadeOutSelector();

	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _parent;
	std::unique_ptr<HistoryView::Reactions::Selector> _selector;
	std::vector<std::unique_ptr<Hiding>> _hiding;
	rpl::event_stream<Chosen> _chosen;
	Ui::Animations::Simple _showing;
	rpl::variable<float64> _shownValue;
	rpl::variable<bool> _expanded;
	bool _shown = false;

};

} // namespace Media::Stories
