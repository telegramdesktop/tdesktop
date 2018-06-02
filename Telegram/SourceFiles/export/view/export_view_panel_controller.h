/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"
#include "base/unique_qptr.h"

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Export {
namespace View {

class Panel;

class PanelController {
public:
	PanelController(not_null<ControllerWrap*> process);

	rpl::producer<> closed() const;

	~PanelController();

private:
	void createPanel();
	void updateState(State &&state);
	void showSettings();

	not_null<ControllerWrap*> _process;

	base::unique_qptr<Ui::SeparatePanel> _panel;

	State _state;
	rpl::event_stream<rpl::producer<>> _panelCloseEvents;
	rpl::lifetime _lifetime;

};

} // namespace View
} // namespace Export
