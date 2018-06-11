/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_controller.h"

#include "export/export_api_wrap.h"
#include "export/export_settings.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_abstract.h"

namespace Export {

class Controller {
public:
	Controller(crl::weak_on_queue<Controller> weak);

	rpl::producer<State> state() const;

	// Password step.
	void submitPassword(const QString &password);
	void requestPasswordRecover();
	rpl::producer<PasswordUpdate> passwordUpdate() const;
	void reloadPasswordState();
	void cancelUnconfirmedPassword();

	// Processing step.
	void startExport(const Settings &settings);

private:
	using Step = ProcessingState::Step;

	void setState(State &&state);
	void ioError(const QString &path);
	void setFinishedState();

	void requestPasswordState();
	void passwordStateDone(const MTPaccount_Password &password);

	void fillExportSteps();
	void exportNext();
	void exportPersonalInfo();
	void exportUserpics();
	void exportContacts();
	void exportSessions();
	void exportChats();

	bool normalizePath();

	ApiWrap _api;
	Settings _settings;

	// rpl::variable<State> fails to compile in MSVC :(
	State _state;
	rpl::event_stream<State> _stateChanges;

	mtpRequestId _passwordRequestId = 0;

	std::unique_ptr<Output::AbstractWriter> _writer;
	std::vector<ProcessingState::Step> _steps;
	int _stepIndex = -1;

	rpl::lifetime _lifetime;

};

Controller::Controller(crl::weak_on_queue<Controller> weak)
: _api(weak.runner())
, _state(PasswordCheckState{}) {
	_api.errors(
	) | rpl::start_with_next([=](RPCError &&error) {
		setState(ErrorState{ ErrorState::Type::API, std::move(error) });
	}, _lifetime);

	//requestPasswordState();
	auto state = PasswordCheckState();
	state.checked = false;
	state.requesting = false;
	setState(std::move(state));
}

rpl::producer<State> Controller::state() const {
	return rpl::single(
		_state
	) | rpl::then(
		_stateChanges.events()
	) | rpl::filter([](const State &state) {
		const auto password = base::get_if<PasswordCheckState>(&state);
		return !password || !password->requesting;
	});
}

void Controller::setState(State &&state) {
	_state = std::move(state);
	_stateChanges.fire_copy(_state);
}

void Controller::ioError(const QString &path) {
	setState(ErrorState{ ErrorState::Type::IO, base::none, path });
}

void Controller::submitPassword(const QString &password) {

}

void Controller::requestPasswordRecover() {

}

rpl::producer<PasswordUpdate> Controller::passwordUpdate() const {
	return rpl::never<PasswordUpdate>();
}

void Controller::reloadPasswordState() {
	//_mtp.request(base::take(_passwordRequestId)).cancel();
	requestPasswordState();
}

void Controller::requestPasswordState() {
	if (_passwordRequestId) {
		return;
	}
	//_passwordRequestId = _mtp.request(MTPaccount_GetPassword(
	//)).done([=](const MTPaccount_Password &result) {
	//	_passwordRequestId = 0;
	//	passwordStateDone(result);
	//}).fail([=](const RPCError &error) {
	//	apiError(error);
	//}).send();
}

void Controller::passwordStateDone(const MTPaccount_Password &result) {
	auto state = PasswordCheckState();
	state.checked = false;
	state.requesting = false;
	state.hasPassword;
	state.hint;
	state.unconfirmedPattern;
	setState(std::move(state));
}

void Controller::cancelUnconfirmedPassword() {

}

void Controller::startExport(const Settings &settings) {
	if (!_settings.path.isEmpty()) {
		return;
	}
	_settings = base::duplicate(settings);

	if (!normalizePath()) {
		ioError(_settings.path);
		return;
	}
	_writer = Output::CreateWriter(_settings.format);
	_api.setFilesBaseFolder(_settings.path);
	fillExportSteps();
	exportNext();
}

bool Controller::normalizePath() {
	const auto check = [&] {
		return QDir().mkpath(_settings.path);
	};
	QDir folder(_settings.path);
	const auto path = folder.absolutePath();
	_settings.path = path + '/';
	if (!folder.exists()) {
		return check();
	}
	const auto list = folder.entryInfoList();
	if (list.isEmpty()) {
		return true;
	}
	const auto date = QDate::currentDate();
	const auto base = QString("DataExport_%1_%2_%3"
	).arg(date.day(), 2, 10, QChar('0')
	).arg(date.month(), 2, 10, QChar('0')
	).arg(date.year());
	const auto add = [&](int i) {
		return base + (i ? " (" + QString::number(i) + ')' : QString());
	};
	auto index = 0;
	while (QDir(_settings.path + add(index)).exists()) {
		++index;
	}
	_settings.path += add(index) + '/';
	return check();
}

void Controller::fillExportSteps() {
	using Type = Settings::Type;
	if (_settings.types & Type::PersonalInfo) {
		_steps.push_back(Step::PersonalInfo);
	}
	if (_settings.types & Type::Userpics) {
		_steps.push_back(Step::Userpics);
	}
	if (_settings.types & Type::Contacts) {
		_steps.push_back(Step::Contacts);
	}
	if (_settings.types & Type::Sessions) {
		_steps.push_back(Step::Sessions);
	}
	const auto chatTypes = Type::PersonalChats
		| Type::PrivateGroups
		| Type::PublicGroups
		| Type::MyChannels;
	if (_settings.types & chatTypes) {
		_steps.push_back(Step::Chats);
	}
}

void Controller::exportNext() {
	if (!++_stepIndex) {
		_writer->start(_settings.path);
	}
	if (_stepIndex >= _steps.size()) {
		_writer->finish();
		setFinishedState();
		return;
	}
	const auto step = _steps[_stepIndex];
	switch (step) {
	case Step::PersonalInfo: return exportPersonalInfo();
	case Step::Userpics: return exportUserpics();
	case Step::Contacts: return exportContacts();
	case Step::Sessions: return exportSessions();
	case Step::Chats: return exportChats();
	}
	Unexpected("Step in Controller::exportNext.");
}

void Controller::exportPersonalInfo() {
	_api.requestPersonalInfo([=](Data::PersonalInfo &&result) {
		_writer->writePersonal(result);
		exportNext();
	});
}

void Controller::exportUserpics() {
	_api.requestUserpics([=](Data::UserpicsInfo &&start) {
		_writer->writeUserpicsStart(start);
	}, [=](Data::UserpicsSlice &&slice) {
		_writer->writeUserpicsSlice(slice);
	}, [=] {
		_writer->writeUserpicsEnd();
		exportNext();
	});
}

void Controller::exportContacts() {
	_api.requestContacts([=](Data::ContactsList &&result) {
		_writer->writeContactsList(result);
		exportNext();
	});
}

void Controller::exportSessions() {
	exportNext();
}

void Controller::exportChats() {
	exportNext();
}

void Controller::setFinishedState() {
	setState(FinishedState{ _writer->mainFilePath() });
}

ControllerWrap::ControllerWrap() {
}

rpl::producer<State> ControllerWrap::state() const {
	return _wrapped.producer_on_main([=](const Controller &controller) {
		return controller.state();
	});
}

void ControllerWrap::submitPassword(const QString &password) {
	_wrapped.with([=](Controller &controller) {
		controller.submitPassword(password);
	});
}

void ControllerWrap::requestPasswordRecover() {
	_wrapped.with([=](Controller &controller) {
		controller.requestPasswordRecover();
	});
}

rpl::producer<PasswordUpdate> ControllerWrap::passwordUpdate() const {
	return _wrapped.producer_on_main([=](const Controller &controller) {
		return controller.passwordUpdate();
	});
}

void ControllerWrap::reloadPasswordState() {
	_wrapped.with([=](Controller &controller) {
		controller.reloadPasswordState();
	});
}

void ControllerWrap::cancelUnconfirmedPassword() {
	_wrapped.with([=](Controller &controller) {
		controller.cancelUnconfirmedPassword();
	});
}

void ControllerWrap::startExport(const Settings &settings) {
	LOG(("Export Info: Started export to '%1'.").arg(settings.path));

	_wrapped.with([=](Controller &controller) {
		controller.startExport(settings);
	});
}

rpl::lifetime &ControllerWrap::lifetime() {
	return _lifetime;
}

ControllerWrap::~ControllerWrap() = default;

} // namespace Export
