/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_progress.h"

#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {

ProgressWidget::ProgressWidget(
	QWidget *parent,
	rpl::producer<Content> content)
: RpWidget(parent) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(this, st::boxLabel);
	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		label->setGeometry(QRect(QPoint(), size));
	}, label->lifetime());
	std::move(
		content
	) | rpl::start_with_next([=](Content &&content) {
		auto text = QString();
		for (const auto &row : content.rows) {
			text += row.id + ' ' + row.info + ' ' + row.label + '\n';
		}
		label->setText(text);
		updateState(std::move(content));
	}, lifetime());
}

void ProgressWidget::updateState(Content &&content) {
	_content = std::move(content);


}

ProgressWidget::~ProgressWidget() = default;

} // namespace View
} // namespace Export
