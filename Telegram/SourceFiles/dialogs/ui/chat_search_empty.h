/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

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

private:
	void setup(Icon icon, rpl::producer<TextWithEntities> text);

	Fn<void()> _animate;

};

} // namespace Dialogs
