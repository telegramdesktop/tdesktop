/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/checkbox.h"

namespace style {
struct InfoToggle;
} // namespace style

namespace Profile {
class UserpicButton;
} // namespace Profile

namespace Ui {
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
namespace Profile {

class Cover : public Ui::FixedHeightWidget {
public:
	Cover(QWidget *parent, not_null<PeerData*> peer);

	Cover *setOnlineCount(rpl::producer<int> &&count);
	Cover *setToggleShown(rpl::producer<bool> &&shown);
	rpl::producer<bool> toggledValue() const;

private:
	void setupChildGeometry();
	void initViewers();
	void initUserpicButton();
	void refreshUserpicLink();
	void refreshNameText();
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);

	not_null<PeerData*> _peer;
	int _onlineCount = 0;

	object_ptr<::Profile::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	object_ptr<Ui::Checkbox> _toggle = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };

};

class SharedMediaCover : public Ui::FixedHeightWidget {
public:
	SharedMediaCover(QWidget *parent);

	SharedMediaCover *setToggleShown(rpl::producer<bool> &&shown);
	rpl::producer<bool> toggledValue() const;

	QMargins getMargins() const override;

private:
	void createLabel();

	object_ptr<Ui::Checkbox> _toggle = { nullptr };

};

class SectionToggle : public Ui::AbstractCheckView {
public:
	SectionToggle(
		const style::InfoToggle &st,
		bool checked,
		base::lambda<void()> updateCallback);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	QSize rippleSize() const;

	const style::InfoToggle &_st;

};

} // namespace Profile
} // namespace Info
