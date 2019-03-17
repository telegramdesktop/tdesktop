/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "ui/widgets/checkbox.h"

namespace style {
struct InfoProfileCountButton;
} // namespace style

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

enum class HistoryVisibility {
	Visible,
	Hidden,
};

class EditPeerHistoryVisibilityBox : public BoxContent {
public:

	EditPeerHistoryVisibilityBox(
		QWidget*,
		not_null<PeerData*> peer,
		FnMut<void(HistoryVisibility)> savedCallback,
		std::optional<HistoryVisibility> historyVisibilitySavedValue = std::nullopt);

protected:
	void prepare() override;

private:
	void setupContent();

	not_null<PeerData*> _peer;
	FnMut<void(HistoryVisibility)> _savedCallback;

	std::optional<HistoryVisibility> _historyVisibilitySavedValue;
	std::shared_ptr<Ui::RadioenumGroup<HistoryVisibility>> _historyVisibility = nullptr;

};
