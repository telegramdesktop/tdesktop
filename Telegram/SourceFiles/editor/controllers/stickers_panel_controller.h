/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

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
		not_null<Window::SessionController*> controller);

	[[nodiscard]] auto stickerChosen() const
	-> rpl::producer<not_null<DocumentData*>>;
	[[nodiscard]] rpl::producer<bool> panelShown() const;

	void setShowRequestChanges(rpl::producer<ShowRequest> &&showRequest);
	// Middle x and plain y position.
	void setMoveRequestChanges(rpl::producer<QPoint> &&moveRequest);

private:
	const base::unique_qptr<ChatHelpers::TabbedPanel> _stickersPanel;

};

} // namespace Editor
