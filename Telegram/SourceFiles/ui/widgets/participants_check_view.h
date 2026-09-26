/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"

namespace Ui {

class ExpanderCheckView final : public Ui::AbstractCheckView {
public:
	ExpanderCheckView(
		TextWithEntities text,
		int duration,
		bool checked,
		Fn<void()> updateCallback);

	[[nodiscard]] static QSize ComputeSize(const TextWithEntities &text);

	void setText(TextWithEntities text);

	QSize getSize() const override;

	void paint(QPainter &p, int left, int top, int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

	~ExpanderCheckView();

private:
	void checkedChangedHook(anim::type animated) override;

	Ui::Text::String _text;
	QSize _size;

};

class ExpanderButton final : public Ui::RippleButton {
public:
	ExpanderButton(
		not_null<QWidget*> parent,
		TextWithEntities text);

	[[nodiscard]] static QSize ComputeSize(const TextWithEntities &text);

	void setText(TextWithEntities text);
	[[nodiscard]] not_null<Ui::AbstractCheckView*> checkView() const;

private:
	void paintEvent(QPaintEvent *event) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	std::unique_ptr<Ui::ExpanderCheckView> _view;

};

} // namespace Ui
