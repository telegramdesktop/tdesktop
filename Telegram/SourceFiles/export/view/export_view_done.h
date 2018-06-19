/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Export {
namespace View {

class DoneWidget : public Ui::RpWidget {
public:
	DoneWidget(QWidget *parent);

	rpl::producer<> showClicks() const;
	rpl::producer<> closeClicks() const;

private:
	void initFooter();
	void setupContent();

	rpl::event_stream<> _showClicks;

	QPointer<Ui::RoundButton> _close;

};

} // namespace View
} // namespace Export
