/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_panel_controller.h"

#include "export/view/export_view_settings.h"
#include "export/view/export_view_progress.h"
#include "export/view/export_view_done.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
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
		_panel->showInner(base::make_unique_q<ProgressWidget>(
			_panel.get(),
			ContentFromState(_process->state())));
		_process->startExport(settings);
	}, settings->lifetime());

	settings->cancelClicks(
	) | rpl::start_with_next([=] {
		_panel->hideGetDuration();
	}, settings->lifetime());

	_panel->showInner(std::move(settings));
}

void PanelController::showError(const ApiErrorState &error) {
	showError("API Error happened :(\n"
		+ QString::number(error.data.code()) + ": " + error.data.type()
		+ "\n" + error.data.description());
}

void PanelController::showError(const OutputErrorState &error) {
	showError("Disk Error happened :(\n"
		"Could not write path:\n" + error.path);
}

void PanelController::showError(const QString &text) {
	auto container = base::make_unique_q<Ui::PaddingWrap<Ui::FlatLabel>>(
		_panel.get(),
		object_ptr<Ui::FlatLabel>(
			_panel.get(),
			text,
			Ui::FlatLabel::InitType::Simple,
			st::exportErrorLabel),
		style::margins(0, st::exportPanelSize.height() / 4, 0, 0));
	container->widthValue(
	) | rpl::start_with_next([label = container->entity()](int width) {
		label->resize(width, label->height());
	}, container->lifetime());

	_panel->showInner(std::move(container));
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
	if (const auto apiError = base::get_if<ApiErrorState>(&_state)) {
		showError(*apiError);
	} else if (const auto error = base::get_if<OutputErrorState>(&_state)) {
		showError(*error);
	} else if (const auto finished = base::get_if<FinishedState>(&_state)) {
		const auto path = finished->path;

		auto done = base::make_unique_q<DoneWidget>(_panel.get());

		done->showClicks(
		) | rpl::start_with_next([=] {
			File::ShowInFolder(path);
			_panel->hideGetDuration();
		}, done->lifetime());

		_panel->showInner(std::move(done));
	}
}

PanelController::~PanelController() = default;

} // namespace View
} // namespace Export
