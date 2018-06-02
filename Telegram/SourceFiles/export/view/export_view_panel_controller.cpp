/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_panel_controller.h"

#include "export/view/export_view_settings.h"
#include "export/view/export_view_done.h"
#include "ui/widgets/separate_panel.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "styles/style_export.h"

namespace Export {
namespace View {

PanelController::PanelController(not_null<ControllerWrap*> process)
: _process(process) {
	_process->state(
	) | rpl::start_with_next([=](State &&state) {
		updateState(std::move(state));
	}, _lifetime);
}

void PanelController::createPanel() {
	_panel = base::make_unique_q<Ui::SeparatePanel>();
	_panel->setTitle(Lang::Viewer(lng_export_title));
	_panel->setInnerSize(st::exportPanelSize);
	_panel->closeRequests(
	) | rpl::start_with_next([=] {
		_panel->hideGetDuration();
	}, _panel->lifetime());
	_panelCloseEvents.fire(_panel->closeEvents());

	showSettings();
}

void PanelController::showSettings() {
	auto settings = base::make_unique_q<SettingsWidget>(_panel);

	settings->startClicks(
	) | rpl::start_with_next([=](const Settings &settings) {
		_process->startExport(settings);
	}, settings->lifetime());

	settings->cancelClicks(
	) | rpl::start_with_next([=] {
		_panel->hideGetDuration();
	}, settings->lifetime());

	_panel->showInner(std::move(settings));
}

rpl::producer<> PanelController::closed() const {
	return _panelCloseEvents.events(
	) | rpl::flatten_latest(
	) | rpl::filter([=] {
		return !_state.is<ProcessingState>();
	});
}

void PanelController::updateState(State &&state) {
	if (!_panel) {
		createPanel();
	}
	_state = std::move(state);
	if (const auto finished = base::get_if<FinishedState>(&_state)) {
		const auto path = finished->path;

		auto done = base::make_unique_q<DoneWidget>(_panel.get());

		done->showClicks(
		) | rpl::start_with_next([=] {
			File::ShowInFolder(path + "personal.txt");
			_panel->hideGetDuration();
		}, done->lifetime());

		_panel->showInner(std::move(done));
	}
}

PanelController::~PanelController() = default;

} // namespace View
} // namespace Export
