/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/checkbox.h"
#include "base/timer.h"

namespace style {
struct InfoToggle;
} // namespace style

namespace Ui {
class UserpicButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info {
namespace Profile {

class SectionWithToggle : public Ui::FixedHeightWidget {
public:
	using FixedHeightWidget::FixedHeightWidget;

	SectionWithToggle *setToggleShown(rpl::producer<bool> &&shown);
	void toggle(bool toggled, anim::type animated);
	bool toggled() const;
	rpl::producer<bool> toggledValue() const;

protected:
	rpl::producer<bool> toggleShownValue() const;
	int toggleSkip() const;

private:
	object_ptr<Ui::Checkbox> _toggle = { nullptr };
	rpl::event_stream<bool> _toggleShown;

};

class Cover : public SectionWithToggle {
public:
	Cover(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	Cover *setOnlineCount(rpl::producer<int> &&count);

	Cover *setToggleShown(rpl::producer<bool> &&shown) {
		return static_cast<Cover*>(
			SectionWithToggle::setToggleShown(std::move(shown)));
	}

	~Cover();

private:
	void setupChildGeometry();
	void initViewers();
	void refreshNameText();
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);
	void refreshUploadPhotoOverlay();
	void setVerified(bool verified);

	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	int _onlineCount = 0;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::RpWidget> _verifiedCheck = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };
	base::Timer _refreshStatusTimer;

};

class SharedMediaCover : public SectionWithToggle {
public:
	SharedMediaCover(QWidget *parent);

	SharedMediaCover *setToggleShown(rpl::producer<bool> &&shown);

	QMargins getMargins() const override;

private:
	void createLabel();

};

} // namespace Profile
} // namespace Info
