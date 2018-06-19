/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "export/view/export_view_content.h"

namespace Export {
namespace View {

class ProgressWidget : public Ui::RpWidget {
public:
	ProgressWidget(
		QWidget *parent,
		rpl::producer<Content> content);

	~ProgressWidget();

private:
	void updateState(Content &&content);

	Content _content;

	class Row;
	std::vector<base::unique_qptr<Row>> _rows;

};

} // namespace View
} // namespace Export
