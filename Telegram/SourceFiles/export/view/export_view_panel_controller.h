/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"
#include "export/view/export_view_content.h"
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

	void activatePanel();
	void stopWithConfirmation();

	rpl::producer<> closed() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	auto progressState() const {
		return ContentFromState(_process->state());
	}

	~PanelController();

private:
	void stopExport();
	void createPanel();
	void updateState(State &&state);
	void showSettings();
	void showProgress();
	void showError(const ApiErrorState &error);
	void showError(const OutputErrorState &error);
	void showError(const QString &text);

	not_null<ControllerWrap*> _process;

	base::unique_qptr<Ui::SeparatePanel> _panel;

	State _state;
	rpl::event_stream<rpl::producer<>> _panelCloseEvents;
	bool _stopRequested = false;
	rpl::lifetime _lifetime;

};

} // namespace View
} // namespace Export
