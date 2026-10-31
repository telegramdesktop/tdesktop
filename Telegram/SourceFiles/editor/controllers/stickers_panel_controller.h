/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "data/data_message_reaction_id.h"
#include "ui/effects/animations.h"

namespace ChatHelpers {
class TabbedPanel;
class Show;
} // namespace ChatHelpers

namespace HistoryView::Reactions {
class Selector;
} // namespace HistoryView::Reactions

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class StickersPanelController final {
public:
	enum class ShowRequest {
		ToggleAnimated,
		ShowAnimated,
		HideAnimated,
		HideFast,
	};

	StickersPanelController(
		not_null<Ui::RpWidget*> panelContainer,
		std::shared_ptr<ChatHelpers::Show> show,
		bool withAudio,
		bool storyWidgets = false);
	~StickersPanelController();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] auto stickerChosen() const
	-> rpl::producer<not_null<DocumentData*>>;
	[[nodiscard]] rpl::producer<> photoRequests() const;
	[[nodiscard]] rpl::producer<> audioRequests() const;
	[[nodiscard]] rpl::producer<> linkRequests() const;
	[[nodiscard]] rpl::producer<> locationRequests() const;
	[[nodiscard]] rpl::producer<> weatherRequests() const;
	[[nodiscard]] auto reactionRequests() const
	-> rpl::producer<Data::ReactionId>;
	void showReactionsSelector(QRect globalItemRect);
	void hideReactionsSelector();
	[[nodiscard]] auto reactionPicked() const
	-> rpl::producer<Data::ReactionId>;
	[[nodiscard]] rpl::producer<bool> panelShown() const;

	void setShowRequestChanges(rpl::producer<ShowRequest> &&showRequest);
	// Middle x and plain y position.
	void setMoveRequestChanges(rpl::producer<QPoint> &&moveRequest);

private:
	struct ReactionsHiding;

	void placeReactionsSelector(QRect itemRect);
	void updateReactionsShowState();
	void fadeOutReactionsSelector();

	const not_null<Ui::RpWidget*> _panelContainer;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const base::unique_qptr<ChatHelpers::TabbedPanel> _stickersPanel;
	std::unique_ptr<HistoryView::Reactions::Selector> _reactionsSelector;
	int _reactionsWidth = 0;
	int _reactionsCategoriesTop = 0;
	std::vector<std::unique_ptr<ReactionsHiding>> _hiding;
	Ui::Animations::Simple _reactionsShowing;
	rpl::event_stream<Data::ReactionId> _reactionPicked;

};

} // namespace Editor
