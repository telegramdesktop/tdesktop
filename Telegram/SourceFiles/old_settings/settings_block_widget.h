/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "ui/rp_widget.h"
#include "styles/style_boxes.h"

namespace Ui {
class Checkbox;
class RadiobuttonGroup;
class Radiobutton;
class LinkButton;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
template <typename Widget>
class SlideWrap;
class VerticalLayout;
} // namespace Ui

namespace OldSettings {

class BlockWidget : public Ui::RpWidget, protected base::Subscriber {
public:
	BlockWidget(QWidget *parent, UserData *self, const QString &title);

	void setContentLeft(int contentLeft);

	QMargins getMargins() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContents(Painter &p) {
	}

	// Where does the block content start (after the title).
	int contentLeft() const {
		return _contentLeft;
	}
	int contentTop() const;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	UserData *self() const {
		return _self;
	}

	bool emptyTitle() const {
		return _title.isEmpty();
	}

	template <typename Widget, typename ...Args>
	void createChildRow(
			Widget *&child,
			style::margins margin,
			Args&&... args) {
		auto row = object_ptr<Widget>{ nullptr };
		createChildWidget(row, margin, std::forward<Args>(args)...);
		child = static_cast<Widget*>(addCreatedRow(
			std::move(row),
			margin));
	}

private:
	template <typename Widget, typename ...Args>
	void createChildWidget(
			object_ptr<Ui::SlideWrap<Widget>> &child,
			style::margins &margin,
			const style::margins &padding,
			Args&&... args) {
		object_ptr<Widget> entity = { nullptr };
		createChildWidget(entity, margin, std::forward<Args>(args)...);
		child.create(this, std::move(entity), padding);
		margin.setLeft(margin.left() - padding.left());
		margin.setTop(margin.top() - padding.top());
		margin.setRight(margin.right() - padding.right());
		margin.setBottom(margin.bottom() - padding.bottom());
	}
	void createChildWidget(
		object_ptr<Ui::Checkbox> &child,
		style::margins &margin,
		const QString &text, Fn<void(bool checked)> callback, bool checked);
	void createChildWidget(
		object_ptr<Ui::LinkButton> &child,
		style::margins &margin,
		const QString &text,
		const char *slot,
		const style::LinkButton &st = st::boxLinkButton);

	template <typename Enum>
	void createChildWidget(
			object_ptr<Ui::Radioenum<Enum>> &child,
			style::margins &margin,
			const std::shared_ptr<Ui::RadioenumGroup<Enum>> &group,
			Enum value,
			const QString &text) {
		child.create(
			this,
			group,
			value,
			text,
			st::defaultBoxCheckbox);
	}

	RpWidget *addCreatedRow(
		object_ptr<RpWidget> row,
		const style::margins &margin);

	template <typename Widget>
	struct IsSlideWrap : std::false_type {
	};
	template <typename Widget>
	struct IsSlideWrap<Ui::SlideWrap<Widget>> : std::true_type {
	};
	template <typename Widget>
	struct IsRadioenum : std::false_type {
	};
	template <typename Enum>
	struct IsRadioenum<Ui::Radioenum<Enum>> : std::true_type {
	};

	template <typename Widget>
	using NotImplementedYet = std::enable_if_t<
		!IsSlideWrap<Widget>::value &&
		!IsRadioenum<Widget>::value &&
		!std::is_same<Ui::Checkbox, Widget>::value &&
		!std::is_same<Ui::LinkButton, Widget>::value>;

	template <typename Widget, typename... Args, typename = NotImplementedYet<Widget>>
	void createChildWidget(object_ptr<Widget> &child, style::margins &margin, Args&&... args) {
		child.create(this, std::forward<Args>(args)...);
	}

	void paintTitle(Painter &p);

	object_ptr<Ui::VerticalLayout> _content;

	int _contentLeft = 0;
	UserData *_self;
	QString _title;

};

} // namespace Settings
