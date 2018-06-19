/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "export/view/export_view_content.h"

namespace Ui {
class VerticalLayout;
class RoundButton;
} // namespace Ui

namespace Export {
namespace View {

class ProgressWidget : public Ui::RpWidget {
public:
	ProgressWidget(
		QWidget *parent,
		rpl::producer<Content> content);

	rpl::producer<> cancelClicks() const;

	~ProgressWidget();

private:
	void initFooter();
	void updateState(Content &&content);

	Content _content;

	class Row;
	object_ptr<Ui::VerticalLayout> _body;
	std::vector<not_null<Row*>> _rows;

	QPointer<Ui::RoundButton> _cancel;

};

} // namespace View
} // namespace Export
