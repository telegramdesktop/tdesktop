/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace style {
struct InfoProfileCountButton;
} // namespace style

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
namespace Profile {
class Button;
} // namespace Profile
} // namespace Info

class ManagePeerBox : public BoxContent {
public:
	ManagePeerBox(QWidget*, not_null<PeerData*> peer);

	static bool Available(not_null<PeerData*> peer);

	static Info::Profile::Button *CreateButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::InfoProfileCountButton &st,
		const style::icon *icon = nullptr);

protected:
	void prepare() override;

private:
	void setupContent();

	not_null<PeerData*> _peer;

};
