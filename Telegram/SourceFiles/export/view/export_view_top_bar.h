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
class FilledSlider;
class AbstractButton;
class PlainShadow;
} // namespace Ui

namespace Export {
namespace View {

struct Content;

class TopBar : public Ui::RpWidget {
public:
	TopBar(QWidget *parent, Content &&content);

	rpl::producer<> clicks() const;

	void updateData(Content &&content);

	void setShadowGeometryToLeft(int x, int y, int w, int h);
	void showShadow();
	void hideShadow();

	~TopBar();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _info;
	object_ptr<Ui::PlainShadow> _shadow = { nullptr };
	object_ptr<Ui::FilledSlider> _progress;
	object_ptr<Ui::AbstractButton> _button;

};

} // namespace View
} // namespace Export
