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

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct InfoToggle;
} // namespace style

namespace Ui {
class AbstractButton;
class UserpicButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
class Controller;
class Section;
} // namespace Info

namespace Info {
namespace Profile {

enum class Badge;

class Cover : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller);
	Cover(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title);

	Cover *setOnlineCount(rpl::producer<int> &&count);

	rpl::producer<Section> showSection() const {
		return _showSection.events();
	}

	~Cover();

private:
	void setupChildGeometry();
	void initViewers(rpl::producer<QString> title);
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);
	void refreshUploadPhotoOverlay();
	void setBadge(Badge badge);

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	int _onlineCount = 0;
	Badge _badge = Badge();

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::AbstractButton> _verifiedCheck = { nullptr };
	object_ptr<Ui::RpWidget> _scamFakeBadge = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };
	base::Timer _refreshStatusTimer;

	rpl::event_stream<Section> _showSection;

};

} // namespace Profile
} // namespace Info
