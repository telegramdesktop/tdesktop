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
#include "export/output/export_output_result.h"

namespace Export {

auto kNullStateCallback = [](ProcessingState&) {};

class Controller {
public:
	Controller(crl::weak_on_queue<Controller> weak);

	rpl::producer<State> state() const;

	// Password step.
	//void submitPassword(const QString &password);
	//void requestPasswordRecover();
	//rpl::producer<PasswordUpdate> passwordUpdate() const;
	//void reloadPasswordState();
	//void cancelUnconfirmedPassword();

	// Processing step.
	void startExport(const Settings &settings);

private:
	using Step = ProcessingState::Step;
	using DownloadProgress = ApiWrap::DownloadProgress;

	void setState(State &&state);
	void ioError(const QString &path);
	bool ioCatchError(Output::Result result);
	void setFinishedState();

	//void requestPasswordState();
	//void passwordStateDone(const MTPaccount_Password &password);

	void fillExportSteps();
	void fillSubstepsInSteps(const ApiWrap::StartInfo &info);
	void exportNext();
	void initialize();
	void collectLeftChannels();
	void collectDialogsList();
	void exportPersonalInfo();
	void exportUserpics();
	void exportContacts();
	void exportSessions();
	void exportDialogs();
	void exportNextDialog();
	void exportLeftChannels();
	void exportNextLeftChannel();

	template <typename Callback = const decltype(kNullStateCallback) &>
	ProcessingState prepareState(
		Step step,
		Callback &&callback = kNullStateCallback) const;
	ProcessingState stateInitializing() const;
	ProcessingState stateLeftChannelsList(int processed) const;
	ProcessingState stateDialogsList(int processed) const;
	ProcessingState statePersonalInfo() const;
	ProcessingState stateUserpics(DownloadProgress progress) const;
	ProcessingState stateContacts() const;
	ProcessingState stateSessions() const;
	ProcessingState stateLeftChannels(DownloadProgress progress) const;
	ProcessingState stateDialogs(DownloadProgress progress) const;

	int substepsInStep(Step step) const;

	bool normalizePath();

	ApiWrap _api;
	Settings _settings;

	Data::DialogsInfo _leftChannelsInfo;
	int _leftChannelIndex = -1;

	Data::DialogsInfo _dialogsInfo;
	int _dialogIndex = -1;

	int _messagesWritten = 0;
	int _messagesCount = 0;

	int _userpicsWritten = 0;
	int _userpicsCount = 0;

	// rpl::variable<State> fails to compile in MSVC :(
	State _state;
	rpl::event_stream<State> _stateChanges;
	std::shared_ptr<const std::vector<int>> _substepsInStep;

	std::unique_ptr<Output::AbstractWriter> _writer;
	std::vector<Step> _steps;
	int _stepIndex = -1;

	rpl::lifetime _lifetime;

};

Controller::Controller(crl::weak_on_queue<Controller> weak)
: _api(weak.runner())
, _state(PasswordCheckState{}) {
	_api.errors(
	) | rpl::start_with_next([=](RPCError &&error) {
		setState(ApiErrorState{ std::move(error) });
	}, _lifetime);

	_api.ioErrors(
	) | rpl::start_with_next([=](const Output::Result &result) {
		ioCatchError(result);
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
	setState(OutputErrorState{ path });
}

bool Controller::ioCatchError(Output::Result result) {
	if (!result) {
		ioError(result.path);
		return true;
	}
	return false;
}

//void Controller::submitPassword(const QString &password) {
//
//}
//
//void Controller::requestPasswordRecover() {
//
//}
//
//rpl::producer<PasswordUpdate> Controller::passwordUpdate() const {
//	return rpl::never<PasswordUpdate>();
//}
//
//void Controller::reloadPasswordState() {
//	//_mtp.request(base::take(_passwordRequestId)).cancel();
//	requestPasswordState();
//}
//
//void Controller::requestPasswordState() {
//	if (_passwordRequestId) {
//		return;
//	}
//	//_passwordRequestId = _mtp.request(MTPaccount_GetPassword(
//	//)).done([=](const MTPaccount_Password &result) {
//	//	_passwordRequestId = 0;
//	//	passwordStateDone(result);
//	//}).fail([=](const RPCError &error) {
//	//	apiError(error);
//	//}).send();
//}
//
//void Controller::passwordStateDone(const MTPaccount_Password &result) {
//	auto state = PasswordCheckState();
//	state.checked = false;
//	state.requesting = false;
//	state.hasPassword;
//	state.hint;
//	state.unconfirmedPattern;
//	setState(std::move(state));
//}
//
//void Controller::cancelUnconfirmedPassword() {
//
//}

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
	_steps.push_back(Step::Initializing);
	if (_settings.types & Type::GroupsChannelsMask) {
		_steps.push_back(Step::LeftChannelsList);
	}
	if (_settings.types & Type::AnyChatsMask) {
		_steps.push_back(Step::DialogsList);
	}
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
	if (_settings.types & Type::AnyChatsMask) {
		_steps.push_back(Step::Dialogs);
	}
	if (_settings.types & Type::GroupsChannelsMask) {
		_steps.push_back(Step::LeftChannels);
	}
}

void Controller::fillSubstepsInSteps(const ApiWrap::StartInfo &info) {
	auto result = std::vector<int>();
	const auto push = [&](Step step, int count) {
		const auto index = static_cast<int>(step);
		if (index >= result.size()) {
			result.resize(index + 1, 0);
		}
		result[index] = count;
	};
	push(Step::Initializing, 1);
	if (_settings.types & Settings::Type::GroupsChannelsMask) {
		push(Step::LeftChannelsList, 1);
	}
	if (_settings.types & Settings::Type::AnyChatsMask) {
		push(Step::DialogsList, 1);
	}
	if (_settings.types & Settings::Type::PersonalInfo) {
		push(Step::PersonalInfo, 1);
	}
	if (_settings.types & Settings::Type::Userpics) {
		push(Step::Userpics, info.userpicsCount);
	}
	if (_settings.types & Settings::Type::Contacts) {
		push(Step::Contacts, 1);
	}
	if (_settings.types & Settings::Type::Sessions) {
		push(Step::Sessions, 1);
	}
	if (_settings.types & Settings::Type::GroupsChannelsMask) {
		push(Step::LeftChannels, info.leftChannelsCount);
	}
	if (_settings.types & Settings::Type::AnyChatsMask) {
		push(Step::Dialogs, info.dialogsCount);
	}
	_substepsInStep = std::make_shared<const std::vector<int>>(
		std::move(result));
}

void Controller::exportNext() {
	if (!++_stepIndex) {
		if (ioCatchError(_writer->start(_settings))) {
			return;
		}
	}
	if (_stepIndex >= _steps.size()) {
		if (ioCatchError(_writer->finish())) {
			return;
		}
		setFinishedState();
		return;
	}
	const auto step = _steps[_stepIndex];
	switch (step) {
	case Step::Initializing: return initialize();
	case Step::LeftChannelsList: return collectLeftChannels();
	case Step::DialogsList: return collectDialogsList();
	case Step::PersonalInfo: return exportPersonalInfo();
	case Step::Userpics: return exportUserpics();
	case Step::Contacts: return exportContacts();
	case Step::Sessions: return exportSessions();
	case Step::LeftChannels: return exportLeftChannels();
	case Step::Dialogs: return exportDialogs();
	}
	Unexpected("Step in Controller::exportNext.");
}

void Controller::initialize() {
	setState(stateInitializing());

	_api.startExport(_settings, [=](ApiWrap::StartInfo info) {
		fillSubstepsInSteps(info);
		exportNext();
	});
}

void Controller::collectLeftChannels() {
	_api.requestLeftChannelsList([=](int count) {
		setState(stateLeftChannelsList(count));
		return true;
	}, [=](Data::DialogsInfo &&result) {
		_leftChannelsInfo = std::move(result);
		exportNext();
	});
}

void Controller::collectDialogsList() {
	_api.requestDialogsList([=](int count) {
		setState(stateDialogsList(count));
		return true;
	}, [=](Data::DialogsInfo &&result) {
		_dialogsInfo = std::move(result);
		exportNext();
	});
}

void Controller::exportPersonalInfo() {
	_api.requestPersonalInfo([=](Data::PersonalInfo &&result) {
		if (ioCatchError(_writer->writePersonal(result))) {
			return;
		}
		exportNext();
	});
}

void Controller::exportUserpics() {
	_api.requestUserpics([=](Data::UserpicsInfo &&start) {
		if (ioCatchError(_writer->writeUserpicsStart(start))) {
			return false;
		}
		_userpicsWritten = 0;
		_userpicsCount = start.count;
		return true;
	}, [=](DownloadProgress progress) {
		setState(stateUserpics(progress));
		return true;
	}, [=](Data::UserpicsSlice &&slice) {
		if (ioCatchError(_writer->writeUserpicsSlice(slice))) {
			return false;
		}
		_userpicsWritten += slice.list.size();
		setState(stateUserpics(DownloadProgress()));
		return true;
	}, [=] {
		if (ioCatchError(_writer->writeUserpicsEnd())) {
			return;
		}
		exportNext();
	});
}

void Controller::exportContacts() {
	_api.requestContacts([=](Data::ContactsList &&result) {
		if (ioCatchError(_writer->writeContactsList(result))) {
			return;
		}
		exportNext();
	});
}

void Controller::exportSessions() {
	_api.requestSessions([=](Data::SessionsList &&result) {
		if (ioCatchError(_writer->writeSessionsList(result))) {
			return;
		}
		exportNext();
	});
}

void Controller::exportDialogs() {
	if (ioCatchError(_writer->writeDialogsStart(_dialogsInfo))) {
		return;
	}

	exportNextDialog();
}

void Controller::exportNextDialog() {
	const auto index = ++_dialogIndex;
	if (index < _dialogsInfo.list.size()) {
		const auto &info = _dialogsInfo.list[index];
		_api.requestMessages(info, [=](const Data::DialogInfo &info) {
			if (ioCatchError(_writer->writeDialogStart(info))) {
				return false;
			}
			_messagesWritten = 0;
			_messagesCount = info.messagesCount;
			setState(stateDialogs(DownloadProgress()));
			return true;
		}, [=](DownloadProgress progress) {
			setState(stateDialogs(progress));
			return true;
		}, [=](Data::MessagesSlice &&result) {
			if (ioCatchError(_writer->writeDialogSlice(result))) {
				return false;
			}
			_messagesWritten += result.list.size();
			setState(stateDialogs(DownloadProgress()));
			return true;
		}, [=] {
			if (ioCatchError(_writer->writeDialogEnd())) {
				return;
			}
			exportNextDialog();
		});
		return;
	}
	if (ioCatchError(_writer->writeDialogsEnd())) {
		return;
	}
	exportNext();
}

void Controller::exportLeftChannels() {
	if (ioCatchError(_writer->writeLeftChannelsStart(_leftChannelsInfo))) {
		return;
	}

	exportNextLeftChannel();
}

void Controller::exportNextLeftChannel() {
	const auto index = ++_leftChannelIndex;
	if (index < _leftChannelsInfo.list.size()) {
		const auto &info = _leftChannelsInfo.list[index];
		_api.requestMessages(info, [=](const Data::DialogInfo &info) {
			if (ioCatchError(_writer->writeLeftChannelStart(info))) {
				return false;
			}
			_messagesWritten = 0;
			_messagesCount = info.messagesCount;
			setState(stateLeftChannels(DownloadProgress()));
			return true;
		}, [=](DownloadProgress progress) {
			setState(stateLeftChannels(progress));
			return true;
		}, [=](Data::MessagesSlice &&result) {
			if (ioCatchError(_writer->writeLeftChannelSlice(result))) {
				return false;
			}
			_messagesWritten += result.list.size();
			setState(stateLeftChannels(DownloadProgress()));
			return true;
		}, [=] {
			if (ioCatchError(_writer->writeLeftChannelEnd())) {
				return;
			}
			exportNextLeftChannel();
		});
		return;
	}
	if (ioCatchError(_writer->writeLeftChannelsEnd())) {
		return;
	}
	exportNext();
}

template <typename Callback>
ProcessingState Controller::prepareState(
		Step step,
		Callback &&callback) const {
	auto result = ProcessingState();
	callback(result);
	result.step = step;
	result.substepsInStep = _substepsInStep;
	return result;
}

ProcessingState Controller::stateInitializing() const {
	return ProcessingState();
}

ProcessingState Controller::stateLeftChannelsList(int processed) const {
	const auto step = Step::LeftChannelsList;
	return prepareState(step, [&](ProcessingState &result) {
		result.entityIndex = processed;
		result.entityCount = std::max(processed, substepsInStep(step));
	});
}

ProcessingState Controller::stateDialogsList(int processed) const {
	const auto step = Step::DialogsList;
	return prepareState(step, [&](ProcessingState &result) {
		result.entityIndex = processed;
		result.entityCount = std::max(processed, substepsInStep(step));
	});
}
ProcessingState Controller::statePersonalInfo() const {
	return prepareState(Step::PersonalInfo);
}

ProcessingState Controller::stateUserpics(DownloadProgress progress) const {
	return prepareState(Step::Userpics, [&](ProcessingState &result) {
		result.entityIndex = _userpicsWritten + progress.itemIndex;
		result.entityCount = std::max(_userpicsCount, result.entityIndex);
		result.bytesLoaded = progress.ready;
		result.bytesCount = progress.total;
	});
}

ProcessingState Controller::stateContacts() const {
	return prepareState(Step::Contacts);
}

ProcessingState Controller::stateSessions() const {
	return prepareState(Step::Sessions);
}

ProcessingState Controller::stateLeftChannels(
		DownloadProgress progress) const {
	const auto step = Step::LeftChannels;
	return prepareState(step, [&](ProcessingState &result) {
		result.entityIndex = _leftChannelIndex;
		result.entityCount = _leftChannelsInfo.list.size();
		result.itemIndex = _messagesWritten + progress.itemIndex;
		result.itemCount = std::max(_messagesCount, result.entityIndex);
		result.bytesLoaded = progress.ready;
		result.bytesCount = progress.total;
	});
}

ProcessingState Controller::stateDialogs(DownloadProgress progress) const {
	const auto step = Step::Dialogs;
	return prepareState(step, [&](ProcessingState &result) {
		result.entityIndex = _dialogIndex;
		result.entityCount = _dialogsInfo.list.size();
		result.itemIndex = _messagesWritten + progress.itemIndex;
		result.itemCount = std::max(_messagesCount, result.entityIndex);
		result.bytesLoaded = progress.ready;
		result.bytesCount = progress.total;
	});
}

int Controller::substepsInStep(Step step) const {
	Expects(_substepsInStep != 0);
	Expects(_substepsInStep->size() > static_cast<int>(step));

	return (*_substepsInStep)[static_cast<int>(step)];
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

//void ControllerWrap::submitPassword(const QString &password) {
//	_wrapped.with([=](Controller &controller) {
//		controller.submitPassword(password);
//	});
//}
//
//void ControllerWrap::requestPasswordRecover() {
//	_wrapped.with([=](Controller &controller) {
//		controller.requestPasswordRecover();
//	});
//}
//
//rpl::producer<PasswordUpdate> ControllerWrap::passwordUpdate() const {
//	return _wrapped.producer_on_main([=](const Controller &controller) {
//		return controller.passwordUpdate();
//	});
//}
//
//void ControllerWrap::reloadPasswordState() {
//	_wrapped.with([=](Controller &controller) {
//		controller.reloadPasswordState();
//	});
//}
//
//void ControllerWrap::cancelUnconfirmedPassword() {
//	_wrapped.with([=](Controller &controller) {
//		controller.cancelUnconfirmedPassword();
//	});
//}

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
