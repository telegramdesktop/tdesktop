/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/calls_panel.h"

namespace Ui::GL {
enum class Backend;
struct ChosenRenderer;
} // namespace Ui::GL

namespace Calls {

class Panel::Incoming final {
public:
	Incoming(
		not_null<QWidget*> parent,
		not_null<Webrtc::VideoTrack*> track,
		Ui::GL::Backend backend);

	[[nodiscard]] not_null<QWidget*> widget() const;
	[[nodiscard]] not_null<Ui::RpWidgetWrap*> rp() const;

	void setControlsAlignment(style::align align);

private:
	class RendererGL;
	class RendererSW;

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Backend backend);

	const std::unique_ptr<Ui::RpWidgetWrap> _surface;
	const not_null<Webrtc::VideoTrack*> _track;
	style::align _topControlsAlignment = style::al_left;
	bool _opengl = false;

};

} // namespace Calls
