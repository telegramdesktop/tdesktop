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
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "platform/platform_specific.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "styles/style_export.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {
namespace {

constexpr auto kSaveSettingsTimeout = TimeMs(1000);

class SuggestBox : public BoxContent {
public:
	SuggestBox(QWidget*);

protected:
	void prepare() override;

};

SuggestBox::SuggestBox(QWidget*) {
}

void SuggestBox::prepare() {
	setTitle(langFactory(lng_export_suggest_title));

	addButton(langFactory(lng_box_ok), [=] {
		closeBox();
		Auth().data().startExport();
	});
	addButton(langFactory(lng_export_suggest_cancel), [=] { closeBox(); });
	setCloseByOutsideClick(false);

	const auto content = Ui::CreateChild<Ui::FlatLabel>(
		this,
		lang(lng_export_suggest_text),
		Ui::FlatLabel::InitType::Simple,
		st::boxLabel);
	widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto contentWidth = width
			- st::boxPadding.left()
			- st::boxPadding.right();
		content->resizeToWidth(contentWidth);
		content->moveToLeft(st::boxPadding.left(), 0);
	}, content->lifetime());
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height + st::boxPadding.bottom());
	}, content->lifetime());
}

Environment PrepareEnvironment() {
	auto result = Environment();
	const auto utfLang = [](LangKey key) {
		return lang(key).toUtf8();
	};
	result.internalLinksDomain = Global::InternalLinksDomain();
	result.aboutTelegram = utfLang(lng_export_about_telegram);
	result.aboutContacts = utfLang(lng_export_about_contacts);
	result.aboutFrequent = utfLang(lng_export_about_frequent);
	result.aboutSessions = utfLang(lng_export_about_sessions);
	result.aboutWebSessions = utfLang(lng_export_about_web_sessions);
	result.aboutChats = utfLang(lng_export_about_chats);
	result.aboutLeftChats = utfLang(lng_export_about_left_chats);
	return result;
}

} // namespace

QPointer<BoxContent> SuggestStart() {
	ClearSuggestStart();
	return Ui::show(Box<SuggestBox>(), LayerOption::KeepOther).data();
}

void ClearSuggestStart() {
	Auth().data().clearExportSuggestion();

	auto settings = Local::ReadExportSettings();
	if (settings.availableAt) {
		settings.availableAt = 0;
		Local::WriteExportSettings(settings);
	}
}

PanelController::PanelController(not_null<ControllerWrap*> process)
: _process(process)
, _settings(std::make_unique<Settings>(Local::ReadExportSettings()))
, _saveSettingsTimer([=] { saveSettings(); }) {
	if (_settings->path.isEmpty()) {
		_settings->path = psDownloadPath();
	}

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
		LOG(("Export Info: Panel Hide By Close."));
		_panel->hideGetDuration();
	}, _panel->lifetime());
	_panelCloseEvents.fire(_panel->closeEvents());

	showSettings();
}

void PanelController::showSettings() {
	auto settings = base::make_unique_q<SettingsWidget>(
		_panel,
		*_settings);

	settings->startClicks(
	) | rpl::start_with_next([=]() {
		showProgress();
		_process->startExport(*_settings, PrepareEnvironment());
	}, settings->lifetime());

	settings->cancelClicks(
	) | rpl::start_with_next([=] {
		LOG(("Export Info: Panel Hide By Cancel."));
		_panel->hideGetDuration();
	}, settings->lifetime());

	settings->changes(
	) | rpl::start_with_next([=](Settings &&settings) {
		*_settings = std::move(settings);
		_saveSettingsTimer.callOnce(kSaveSettingsTimeout);
	}, settings->lifetime());

	_panel->showInner(std::move(settings));
}

void PanelController::showError(const ApiErrorState &error) {
	LOG(("Export Info: API Error '%1'.").arg(error.data.type()));

	if (error.data.type() == qstr("TAKEOUT_INVALID")) {
		showError(lang(lng_export_invalid));
	} else if (error.data.type().startsWith(qstr("TAKEOUT_INIT_DELAY_"))) {
		const auto seconds = std::max(error.data.type().mid(
			qstr("TAKEOUT_INIT_DELAY_").size()).toInt(), 1);
		const auto now = QDateTime::currentDateTime();
		const auto when = now.addSecs(seconds);
		const auto hours = seconds / 3600;
		const auto hoursText = [&] {
			if (hours <= 0) {
				return lang(lng_export_delay_less_than_hour);
			}
			return lng_export_delay_hours(lt_count, hours);
		}();
		showError(lng_export_delay(
			lt_hours,
			hoursText,
			lt_date,
			langDateTimeFull(when)));

		_settings->availableAt = unixtime() + seconds;
		_saveSettingsTimer.callOnce(kSaveSettingsTimeout);

		Auth().data().suggestStartExport(_settings->availableAt);
	} else {
		showCriticalError("API Error happened :(\n"
			+ QString::number(error.data.code()) + ": " + error.data.type()
			+ "\n" + error.data.description());
	}
}

void PanelController::showError(const OutputErrorState &error) {
	showCriticalError("Disk Error happened :(\n"
		"Could not write path:\n" + error.path);
}

void PanelController::showCriticalError(const QString &text) {
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

void PanelController::showError(const QString &text) {
	auto box = Box<InformBox>(text);
	const auto weak = make_weak(box.data());
	const auto hidden = _panel->isHidden();
	_panel->showBox(
		std::move(box),
		LayerOption::CloseOther,
		hidden ? anim::type::instant : anim::type::normal);
	weak->setCloseByEscape(false);
	weak->setCloseByOutsideClick(false);
	weak->boxClosing(
	) | rpl::start_with_next([=] {
		LOG(("Export Info: Panel Hide By Error: %1.").arg(text));
		_panel->hideGetDuration();
	}, weak->lifetime());
	if (hidden) {
		_panel->showAndActivate();
	}
	_panel->setHideOnDeactivate(false);
}

void PanelController::showProgress() {
	_settings->availableAt = 0;
	ClearSuggestStart();

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
			LOG(("Export Info: Panel Hide By Done: %1."
				).arg(finished->path));
			_panel->hideGetDuration();
		}
	}, progress->lifetime());

	_panel->showInner(std::move(progress));
	_panel->setHideOnDeactivate(true);
}

void PanelController::stopWithConfirmation(FnMut<void()> callback) {
	if (!_state.is<ProcessingState>()) {
		LOG(("Export Info: Stop Panel Without Confirmation."));
		stopExport();
		callback();
		return;
	}
	auto stop = [=, callback = std::move(callback)]() mutable {
		if (auto saved = std::move(callback)) {
			LOG(("Export Info: Stop Panel With Confirmation."));
			stopExport();
			saved();
		} else {
			_process->cancelExportFast();
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
	LOG(("Export Info: Panel Hide By Stop"));
	_panel->hideGetDuration();
}

rpl::producer<> PanelController::stopRequests() const {
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
	} else if (_state.is<FinishedState>()) {
		_panel->setTitle(Lang::Viewer(lng_export_title));
		_panel->setHideOnDeactivate(false);
	} else if (_state.is<CancelledState>()) {
		LOG(("Export Info: Stop Panel After Cancel."));
		stopExport();
	}
}

void PanelController::saveSettings() const {
	const auto check = [](const QString &value) {
		const auto result = value.endsWith('/')
			? value.mid(0, value.size() - 1)
			: value;
		return (cPlatform() == dbipWindows) ? result.toLower() : result;
	};
	auto settings = *_settings;
	if (check(settings.path) == check(psDownloadPath())) {
		settings.path = QString();
	}
	Local::WriteExportSettings(settings);
}

PanelController::~PanelController() {
	if (_saveSettingsTimer.isActive()) {
		saveSettings();
	}
}

} // namespace View
} // namespace Export
