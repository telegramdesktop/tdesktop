/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "ui/widgets/shadow.h"

namespace Ui {
class RoundButton;
class IconButton;
} // namespace Ui

namespace Dialogs {

enum class RestoreWindowsChoice {
	Always,
	Once,
	Never,
	Dismiss,
	Detached,
};

class RestoreWindowsOffer final : public Ui::RpWidget {
public:
	explicit RestoreWindowsOffer(not_null<Ui::RpWidget*> parent);

	void setAvailableWidth(int available);

	[[nodiscard]] rpl::producer<RestoreWindowsChoice> chosen() const;

private:
	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] Ui::Text::GeometryDescriptor questionGeometry() const;
	void relayout();

	Ui::Text::String _question;
	not_null<Ui::RoundButton*> _always;
	not_null<Ui::RoundButton*> _restore;
	not_null<Ui::RoundButton*> _never;
	not_null<Ui::RoundButton*> _wideAlways;
	not_null<Ui::RoundButton*> _wideRestore;
	not_null<Ui::RoundButton*> _wideNever;
	not_null<Ui::IconButton*> _close;
	Ui::BoxShadow _shadow;
	rpl::event_stream<RestoreWindowsChoice> _chosen;
	QPoint _questionPosition;
	int _questionFirstLine = 0;
	int _questionOther = 0;
	int _maxWidth = 0;

};

} // namespace Dialogs
