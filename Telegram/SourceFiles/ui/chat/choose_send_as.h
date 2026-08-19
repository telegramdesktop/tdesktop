/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/layers/generic_box.h"

class PeerData;

namespace style {
struct ChooseSendAs;
} // namespace style

namespace Data {
class GroupCall;
} // namespace Data

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
struct SendAsPeer;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class SendAsButton;

void ChooseSendAsBox(
	not_null<GenericBox*> box,
	const style::ChooseSendAs &st,
	std::vector<Main::SendAsPeer> list,
	not_null<PeerData*> chosen,
	Fn<bool(not_null<PeerData*>)> done);

void SetupSendAsButton(
	not_null<SendAsButton*> button,
	const style::ChooseSendAs &st,
	rpl::producer<PeerData*> active,
	std::shared_ptr<ChatHelpers::Show> show);

void SetupSendAsButton(
	not_null<SendAsButton*> button,
	const style::ChooseSendAs &st,
	std::shared_ptr<Data::GroupCall> videoStream,
	std::shared_ptr<ChatHelpers::Show> show);

void SetupSendAsButton(
	not_null<SendAsButton*> button,
	const style::ChooseSendAs &st,
	not_null<Window::SessionController*> window);

} // namespace Ui
