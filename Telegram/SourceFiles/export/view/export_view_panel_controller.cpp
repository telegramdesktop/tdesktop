/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_panel_controller.h"

#include "export/view/export_view_settings.h"
#include "export/view/export_view_progress.h"
#include "export/export_manager.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/wrap/padding_wrap.h"
#include "mtproto/mtproto_config.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "storage/storage_account.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "base/platform/base_platform_info.h"
#include "base/unixtime.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"

namespace Export {
namespace View {
namespace {

constexpr auto kSaveSettingsTimeout = crl::time(1000);

class SuggestBox : public Ui::BoxContent {
public:
	SuggestBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;

private:
	const not_null<Main::Session*> _session;

};

SuggestBox::SuggestBox(QWidget*, not_null<Main::Session*> session)
: _session(session) {
}

void SuggestBox::prepare() {
	setTitle(tr::lng_export_suggest_title());

	addButton(tr::lng_box_ok(), [=] {
		const auto session = _session;
		closeBox();
		Core::App().exportManager().start(
			session,
			session->local().readExportSettings().singlePeer);
	});
	addButton(tr::lng_export_suggest_cancel(), [=] { closeBox(); });
	setCloseByOutsideClick(false);

	const auto content = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_export_suggest_text(tr::now),
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

} // namespace

Environment PrepareEnvironment(not_null<Main::Session*> session) {
	auto result = Environment();
	result.internalLinksDomain = session->serverConfig().internalLinksDomain;
	result.aboutTelegram = tr::lng_export_about_telegram(tr::now).toUtf8();
	result.aboutContacts = tr::lng_export_about_contacts(tr::now).toUtf8();
	result.aboutFrequent = tr::lng_export_about_frequent(tr::now).toUtf8();
	result.aboutSessions = tr::lng_export_about_sessions(tr::now).toUtf8();
	result.aboutWebSessions = tr::lng_export_about_web_sessions(tr::now).toUtf8();
	result.aboutChats = tr::lng_export_about_chats(tr::now).toUtf8();
	result.aboutLeftChats = tr::lng_export_about_left_chats(tr::now).toUtf8();
	return result;
}

QPointer<Ui::BoxContent> SuggestStart(not_null<Main::Session*> session) {
	ClearSuggestStart(session);
	return Ui::show(
		Box<SuggestBox>(session),
		Ui::LayerOption::KeepOther).data();
}

void ClearSuggestStart(not_null<Main::Session*> session) {
	session->data().clearExportSuggestion();

	auto settings = session->local().readExportSettings();
	if (settings.availableAt) {
		settings.availableAt = 0;
		session->local().writeExportSettings(settings);
	}
}

bool IsDefaultPath(not_null<Main::Session*> session, const QString &path) {
	const auto check = [](const QString &value) {
		const auto result = value.endsWith('/')
			? value.mid(0, value.size() - 1)
			: value;
		return Platform::IsWindows() ? result.toLower() : result;
	};
	return (check(path) == check(File::DefaultDownloadPath(session)));
}

void ResolveSettings(not_null<Main::Session*> session, Settings &settings) {
	if (settings.path.isEmpty()) {
		settings.path = File::DefaultDownloadPath(session);
		settings.forceSubPath = true;
	} else {
		settings.forceSubPath = IsDefaultPath(session, settings.path);
	}
	if (!settings.onlySinglePeer()) {
		settings.singlePeerFrom = settings.singlePeerTill = 0;
	}
}

PanelController::PanelController(
	not_null<Main::Session*> session,
	not_null<Controller*> process)
: _session(session)
, _process(process)
, _settings(
	std::make_unique<Settings>(_session->local().readExportSettings()))
, _saveSettingsTimer([=] { saveSettings(); }) {
	ResolveSettings(session, *_settings);

	_process->state(
	) | rpl::start_with_next([=](State &&state) {
		updateState(std::move(state));
	}, _lifetime);
}

PanelController::~PanelController() {
	if (_saveSettingsTimer.isActive()) {
		saveSettings();
	}
	_panel->destroyLayer();
}

void PanelController::activatePanel() {
	_panel->showAndActivate();
}

void PanelController::createPanel() {
	const auto singlePeer = _settings->onlySinglePeer();
	_panel = base::make_unique_q<Ui::SeparatePanel>();
	_panel->setTitle((singlePeer
		? tr::lng_export_header_chats
		: tr::lng_export_title)());
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
		_session,
		*_settings);
	settings->setShowBoxCallback([=](object_ptr<Ui::BoxContent> box) {
		_panel->showBox(
			std::move(box),
			Ui::LayerOption::KeepOther,
			anim::type::normal);
	});

	settings->startClicks(
	) | rpl::start_with_next([=]() {
		showProgress();
		_process->startExport(*_settings, PrepareEnvironment(_session));
	}, settings->lifetime());

	settings->cancelClicks(
	) | rpl::start_with_next([=] {
		LOG(("Export Info: Panel Hide By Cancel."));
		_panel->hideGetDuration();
	}, settings->lifetime());

	settings->changes(
	) | rpl::start_with_next([=](Settings &&settings) {
		*_settings = std::move(settings);
	}, settings->lifetime());

	_panel->showInner(std::move(settings));
}

void PanelController::showError(const ApiErrorState &error) {
	LOG(("Export Info: API Error '%1'.").arg(error.data.type()));

	if (error.data.type() == qstr("TAKEOUT_INVALID")) {
		showError(tr::lng_export_invalid(tr::now));
	} else if (error.data.type().startsWith(qstr("TAKEOUT_INIT_DELAY_"))) {
		const auto seconds = std::max(error.data.type().midRef(
			qstr("TAKEOUT_INIT_DELAY_").size()).toInt(), 1);
		const auto now = QDateTime::currentDateTime();
		const auto when = now.addSecs(seconds);
		const auto hours = seconds / 3600;
		const auto hoursText = [&] {
			if (hours <= 0) {
				return tr::lng_export_delay_less_than_hour(tr::now);
			}
			return tr::lng_export_delay_hours(tr::now, lt_count, hours);
		}();
		showError(tr::lng_export_delay(
			tr::now,
			lt_hours,
			hoursText,
			lt_date,
			langDateTimeFull(when)));

		_settings->availableAt = base::unixtime::now() + seconds;
		_saveSettingsTimer.callOnce(kSaveSettingsTimeout);

		_session->data().suggestStartExport(_settings->availableAt);
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
	const auto weak = Ui::MakeWeak(box.data());
	const auto hidden = _panel->isHidden();
	_panel->showBox(
		std::move(box),
		Ui::LayerOption::CloseOther,
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
	ClearSuggestStart(_session);

	_panel->setTitle(tr::lng_export_progress_title());

	auto progress = base::make_unique_q<ProgressWidget>(
		_panel.get(),
		rpl::single(
			ContentFromState(_settings.get(), ProcessingState())
		) | rpl::then(progressState()));

	progress->skipFileClicks(
	) | rpl::start_with_next([=](uint64 randomId) {
		_process->skipFile(randomId);
	}, progress->lifetime());

	progress->cancelClicks(
	) | rpl::start_with_next([=] {
		stopWithConfirmation();
	}, progress->lifetime());

	progress->doneClicks(
	) | rpl::start_with_next([=] {
		if (const auto finished = std::get_if<FinishedState>(&_state)) {
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
	if (!v::is<ProcessingState>(_state)) {
		LOG(("Export Info: Stop Panel Without Confirmation."));
		stopExport();
		if (callback) {
			callback();
		}
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
		tr::lng_export_sure_stop(tr::now),
		tr::lng_export_stop(tr::now),
		st::attentionBoxButton,
		std::move(stop));
	_confirmStopBox = box.data();
	_panel->showBox(
		std::move(box),
		Ui::LayerOption::CloseOther,
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
		return !v::is<ProcessingState>(_state) || _stopRequested;
	});
}

void PanelController::fillParams(const PasswordCheckState &state) {
	_settings->singlePeer = state.singlePeer;
}

void PanelController::updateState(State &&state) {
	if (const auto start = std::get_if<PasswordCheckState>(&state)) {
		fillParams(*start);
	}
	if (!_panel) {
		createPanel();
	}
	_state = std::move(state);
	if (const auto apiError = std::get_if<ApiErrorState>(&_state)) {
		showError(*apiError);
	} else if (const auto error = std::get_if<OutputErrorState>(&_state)) {
		showError(*error);
	} else if (v::is<FinishedState>(_state)) {
		_panel->setTitle(tr::lng_export_title());
		_panel->setHideOnDeactivate(false);
	} else if (v::is<CancelledState>(_state)) {
		LOG(("Export Info: Stop Panel After Cancel."));
		stopExport();
	}
}

void PanelController::saveSettings() const {
	const auto check = [](const QString &value) {
		const auto result = value.endsWith('/')
			? value.mid(0, value.size() - 1)
			: value;
		return Platform::IsWindows() ? result.toLower() : result;
	};
	auto settings = *_settings;
	if (check(settings.path) == check(File::DefaultDownloadPath(_session))) {
		settings.path = QString();
	}
	_session->local().writeExportSettings(settings);
}

} // namespace View
} // namespace Export
