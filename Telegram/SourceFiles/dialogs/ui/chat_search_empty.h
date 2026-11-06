/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Dialogs {

enum class SearchEmptyIcon {
	Search,
	NoResults,
};

class SearchEmpty final : public Ui::RpWidget {
public:
	using Icon = SearchEmptyIcon;

	SearchEmpty(
		QWidget *parent,
		Icon icon,
		rpl::producer<TextWithEntities> text);

	void setMinimalHeight(int minimalHeight);

	void animate();

	[[nodiscard]] rpl::producer<ClickHandlerPtr> handlerActivated() const {
		return _handlerActivated.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setup(Icon icon, rpl::producer<TextWithEntities> text);
	[[nodiscard]] int fullHeight() const;

	Ui::FlatLabel *_label = nullptr;
	Ui::RpWidget *_animated = nullptr;
	int _minimalHeight = 0;
	bool _measuring = false;
	Fn<void()> _animate;
	rpl::event_stream<ClickHandlerPtr> _handlerActivated;

};

} // namespace Dialogs
