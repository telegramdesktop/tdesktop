/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_block_widget.h"

#include "styles/style_settings.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"

namespace Settings {

BlockWidget::BlockWidget(QWidget *parent, UserData *self, const QString &title) : RpWidget(parent)
, _content(this)
, _self(self)
, _title(title) {
	_content->heightValue(
	) | rpl::start_with_next([this](int contentHeight) {
		resize(
			width(),
			contentTop()
				+ contentHeight
				+ st::settingsBlockMarginBottom);
	}, lifetime());
}

void BlockWidget::setContentLeft(int contentLeft) {
	_contentLeft = contentLeft;
}

int BlockWidget::contentTop() const {
	return emptyTitle() ? 0 : (st::settingsBlockMarginTop + st::settingsBlockTitleHeight);
}

int BlockWidget::resizeGetHeight(int newWidth) {
	int x = contentLeft(), result = contentTop();
	int availw = newWidth - x;

	auto margins = getMargins();

	_content->resizeToWidth(availw);
	_content->moveToLeft(margins.left() + x, margins.top() + result, newWidth);
	result += _content->heightNoMargins() + st::settingsBlockMarginBottom;

	return result;
}

QMargins BlockWidget::getMargins() const {
	auto result = _content->getMargins();
	return QMargins(
		result.left(),
		qMax(result.top() - contentTop(), 0),
		result.right(),
		qMax(result.bottom() - st::settingsBlockMarginBottom, 0));
}

void BlockWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintTitle(p);
	paintContents(p);
}

void BlockWidget::paintTitle(Painter &p) {
	if (emptyTitle()) return;

	p.setFont(st::settingsBlockTitleFont);
	p.setPen(st::settingsBlockTitleFg);
	auto margins = getMargins();
	auto titleTop = st::settingsBlockMarginTop + st::settingsBlockTitleTop;
	p.drawTextLeft(
		margins.left() + contentLeft(),
		margins.top() + titleTop,
		width(),
		_title);
}

Ui::RpWidget *BlockWidget::addCreatedRow(
		object_ptr<RpWidget> row,
		const style::margins &margin) {
	return _content->add(std::move(row), margin);
}

void BlockWidget::createChildWidget(
		object_ptr<Ui::Checkbox> &child,
		style::margins &margin,
		const QString &text,
		base::lambda<void(bool checked)> callback,
		bool checked) {
	child.create(this, text, checked, st::defaultBoxCheckbox);
	subscribe(child->checkedChanged, std::move(callback));
}

void BlockWidget::createChildWidget(
		object_ptr<Ui::LinkButton> &child,
		style::margins &margin,
		const QString &text,
		const char *slot,
		const style::LinkButton &st) {
	child.create(this, text, st);
	connect(child, SIGNAL(clicked()), this, slot);
}

} // namespace Settings
