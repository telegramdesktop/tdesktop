/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "data/data_message_reaction_id.h"

namespace ChatHelpers {
class TabbedPanel;
class Show;
} // namespace ChatHelpers

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
	void showReactionsPicker(QPoint middleBottom);
	[[nodiscard]] auto reactionPicked() const
	-> rpl::producer<Data::ReactionId>;
	[[nodiscard]] rpl::producer<bool> panelShown() const;

	void setShowRequestChanges(rpl::producer<ShowRequest> &&showRequest);
	// Middle x and plain y position.
	void setMoveRequestChanges(rpl::producer<QPoint> &&moveRequest);

private:
	const not_null<Ui::RpWidget*> _panelContainer;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const base::unique_qptr<ChatHelpers::TabbedPanel> _stickersPanel;
	base::unique_qptr<ChatHelpers::TabbedPanel> _reactionsPanel;
	rpl::event_stream<Data::ReactionId> _reactionPicked;

};

} // namespace Editor
