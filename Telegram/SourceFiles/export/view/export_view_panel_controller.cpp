/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_panel_controller.h"

#include "export/view/export_view_settings.h"
#include "export/view/export_view_progress.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "styles/style_export.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {

PanelController::PanelController(not_null<ControllerWrap*> process)
: _process(process) {
	_process->state(
	) | rpl::start_with_next([=](State &&state) {
		updateState(std::move(state));
	}, _lifetime);
}

void PanelController::activatePanel() {
	_panel->showAndActivate();
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
		showProgress();
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
	_panel->setHideOnDeactivate(false);
}

void PanelController::showProgress() {
	_panel->setTitle(Lang::Viewer(lng_export_progress_title));

	auto progress = base::make_unique_q<ProgressWidget>(
		_panel.get(),
		rpl::single(
			ContentFromState(ProcessingState())
		) | rpl::then(progressState()));

	progress->cancelClicks(
	) | rpl::start_with_next([=] {
		stopWithConfirmation();
	}, progress->lifetime());

	progress->doneClicks(
	) | rpl::start_with_next([=] {
		if (const auto finished = base::get_if<FinishedState>(&_state)) {
			File::ShowInFolder(finished->path);
			_panel->hideGetDuration();
		}
	}, progress->lifetime());

	_panel->showInner(std::move(progress));
	_panel->setHideOnDeactivate(true);
}

void PanelController::stopWithConfirmation(FnMut<void()> callback) {
	if (!_state.is<ProcessingState>()) {
		stopExport();
		callback();
		return;
	}
	auto stop = [=, callback = std::move(callback)]() mutable {
		auto saved = std::move(callback);
		stopExport();
		if (saved) {
			saved();
		}
	};
	const auto hidden = _panel->isHidden();
	const auto old = _confirmStopBox;
	auto box = Box<ConfirmBox>(
		lang(lng_export_sure_stop),
		lang(lng_export_stop),
		st::attentionBoxButton,
		std::move(stop));
	_confirmStopBox = box.data();
	_panel->showBox(
		std::move(box),
		LayerOption::CloseOther,
		hidden ? anim::type::instant : anim::type::normal);
	if (hidden) {
		_panel->showAndActivate();
	}
	if (old) {
		old->closeBox();
	}
}

void PanelController::stopExport() {
	_stopRequested = true;
	_panel->showAndActivate();
	_panel->hideGetDuration();
}

rpl::producer<> PanelController::closed() const {
	return _panelCloseEvents.events(
	) | rpl::flatten_latest(
	) | rpl::filter([=] {
		return !_state.is<ProcessingState>() || _stopRequested;
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
		_panel->setTitle(Lang::Viewer(lng_export_title));
		_panel->setHideOnDeactivate(false);
	}
}

PanelController::~PanelController() = default;

} // namespace View
} // namespace Export
