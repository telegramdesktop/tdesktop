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
#include "export/output/export_output_stats.h"

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
	void startExport(
		const Settings &settings,
		const Environment &environment);
	void cancelExportFast();

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
	void initialized(const ApiWrap::StartInfo &info);
	void collectLeftChannels();
	void collectDialogsList();
	void exportPersonalInfo();
	void exportUserpics();
	void exportContacts();
	void exportSessions();
	void exportOtherData();
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
	ProcessingState stateUserpics(const DownloadProgress &progress) const;
	ProcessingState stateContacts() const;
	ProcessingState stateSessions() const;
	ProcessingState stateOtherData() const;
	ProcessingState stateLeftChannels(
		const DownloadProgress &progress) const;
	ProcessingState stateDialogs(const DownloadProgress &progress) const;
	void fillMessagesState(
		ProcessingState &result,
		const Data::DialogsInfo &info,
		int index,
		const DownloadProgress &progress,
		int addIndex,
		int addCount) const;

	int substepsInStep(Step step) const;

	bool normalizePath();

	ApiWrap _api;
	Settings _settings;
	Environment _environment;

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

	Output::Stats _stats;

	std::vector<int> _substepsInStep;
	int _substepsTotal = 0;
	mutable int _substepsPassed = 0;
	mutable Step _lastProcessingStep = Step::Initializing;

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
	if (_state.is<CancelledState>()) {
		return;
	}
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

void Controller::startExport(
		const Settings &settings,
		const Environment &environment) {
	if (!_settings.path.isEmpty()) {
		return;
	}
	_settings = base::duplicate(settings);
	_environment = environment;

	_settings.path = Output::NormalizePath(_settings.path);
	_writer = Output::CreateWriter(_settings.format);
	fillExportSteps();
	exportNext();
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
	if (_settings.types & Type::OtherData) {
		_steps.push_back(Step::OtherData);
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
		push(Step::Userpics, 1);
	}
	if (_settings.types & Settings::Type::Contacts) {
		push(Step::Contacts, 1);
	}
	if (_settings.types & Settings::Type::Sessions) {
		push(Step::Sessions, 1);
	}
	if (_settings.types & Settings::Type::OtherData) {
		push(Step::OtherData, 1);
	}
	if (_settings.types & Settings::Type::GroupsChannelsMask) {
		push(Step::LeftChannels, info.leftChannelsCount);
	}
	if (_settings.types & Settings::Type::AnyChatsMask) {
		push(Step::Dialogs, info.dialogsCount);
	}
	_substepsInStep = std::move(result);
	_substepsTotal = ranges::accumulate(_substepsInStep, 0);
}

void Controller::cancelExportFast() {
	_api.cancelExportFast();
	setState(CancelledState());
}

void Controller::exportNext() {
	if (++_stepIndex >= _steps.size()) {
		if (ioCatchError(_writer->finish())) {
			return;
		}
		_api.finishExport([=] {
			setFinishedState();
		});
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
	case Step::OtherData: return exportOtherData();
	case Step::LeftChannels: return exportLeftChannels();
	case Step::Dialogs: return exportDialogs();
	}
	Unexpected("Step in Controller::exportNext.");
}

void Controller::initialize() {
	setState(stateInitializing());
	_api.startExport(_settings, &_stats, [=](ApiWrap::StartInfo info) {
		initialized(info);
	});
}

void Controller::initialized(const ApiWrap::StartInfo &info) {
	if (ioCatchError(_writer->start(_settings, _environment, &_stats))) {
		return;
	}
	fillSubstepsInSteps(info);
	exportNext();
}

void Controller::collectLeftChannels() {
	setState(stateLeftChannelsList(0));
	_api.requestLeftChannelsList([=](int count) {
		setState(stateLeftChannelsList(count));
		return true;
	}, [=](Data::DialogsInfo &&result) {
		_leftChannelsInfo = std::move(result);
		exportNext();
	});
}

void Controller::collectDialogsList() {
	setState(stateDialogsList(0));
	_api.requestDialogsList([=](int count) {
		setState(stateDialogsList(count));
		return true;
	}, [=](Data::DialogsInfo &&result) {
		_dialogsInfo = std::move(result);
		exportNext();
	});
}

void Controller::exportPersonalInfo() {
	setState(statePersonalInfo());
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
	setState(stateContacts());
	_api.requestContacts([=](Data::ContactsList &&result) {
		if (ioCatchError(_writer->writeContactsList(result))) {
			return;
		}
		exportNext();
	});
}

void Controller::exportSessions() {
	setState(stateSessions());
	_api.requestSessions([=](Data::SessionsList &&result) {
		if (ioCatchError(_writer->writeSessionsList(result))) {
			return;
		}
		exportNext();
	});
}

void Controller::exportOtherData() {
	setState(stateOtherData());
	const auto relativePath = "lists/other_data.json";
	_api.requestOtherData(relativePath, [=](Data::File &&result) {
		if (ioCatchError(_writer->writeOtherData(result))) {
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
			_messagesCount = ranges::accumulate(
				info.messagesCountPerSplit,
				0);
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
			_messagesCount = ranges::accumulate(
				info.messagesCountPerSplit,
				0);
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
	if (step != _lastProcessingStep) {
		_substepsPassed += substepsInStep(_lastProcessingStep);
		_lastProcessingStep = step;
	}

	auto result = ProcessingState();
	callback(result);
	result.step = step;
	result.substepsPassed = _substepsPassed;
	result.substepsNow = substepsInStep(_lastProcessingStep);
	result.substepsTotal = _substepsTotal;
	return result;
}

ProcessingState Controller::stateInitializing() const {
	return ProcessingState();
}

ProcessingState Controller::stateLeftChannelsList(int processed) const {
	return prepareState(Step::LeftChannelsList, [&](
			ProcessingState &result) {
		result.entityIndex = processed;
		result.entityCount = std::max(
			processed,
			substepsInStep(Step::LeftChannels))
			+ substepsInStep(Step::Dialogs);
	});
}

ProcessingState Controller::stateDialogsList(int processed) const {
	const auto step = Step::DialogsList;
	return prepareState(step, [&](ProcessingState &result) {
		result.entityIndex = substepsInStep(Step::LeftChannels) + processed;
		result.entityCount = substepsInStep(Step::LeftChannels) + std::max(
			processed,
			substepsInStep(Step::Dialogs));
	});
}
ProcessingState Controller::statePersonalInfo() const {
	return prepareState(Step::PersonalInfo);
}

ProcessingState Controller::stateUserpics(
		const DownloadProgress &progress) const {
	return prepareState(Step::Userpics, [&](ProcessingState &result) {
		result.entityIndex = _userpicsWritten + progress.itemIndex;
		result.entityCount = std::max(_userpicsCount, result.entityIndex);
		result.bytesType = ProcessingState::FileType::Photo;
		if (!progress.path.isEmpty()) {
			const auto last = progress.path.lastIndexOf('/');
			result.bytesName = progress.path.mid(last + 1);
		}
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

ProcessingState Controller::stateOtherData() const {
	return prepareState(Step::OtherData);
}

ProcessingState Controller::stateLeftChannels(
		const DownloadProgress & progress) const {
	const auto step = Step::LeftChannels;
	return prepareState(step, [&](ProcessingState &result) {
		const auto addIndex = _dialogsInfo.list.size();
		const auto addCount = addIndex;
		fillMessagesState(
			result,
			_leftChannelsInfo,
			_leftChannelIndex,
			progress,
			addIndex,
			addCount);
	});
}

ProcessingState Controller::stateDialogs(
		const DownloadProgress &progress) const {
	const auto step = Step::Dialogs;
	return prepareState(step, [&](ProcessingState &result) {
		const auto addIndex = 0;
		const auto addCount = _leftChannelsInfo.list.size();
		fillMessagesState(
			result,
			_dialogsInfo,
			_dialogIndex,
			progress,
			addIndex,
			addCount);
	});
}

void Controller::fillMessagesState(
		ProcessingState &result,
		const Data::DialogsInfo &info,
		int index,
		const DownloadProgress &progress,
		int addIndex,
		int addCount) const {
	const auto &dialog = info.list[index];
	result.entityIndex = index + addIndex;
	result.entityCount = info.list.size() + addCount;
	result.entityName = dialog.name;
	result.itemIndex = _messagesWritten + progress.itemIndex;
	result.itemCount = std::max(_messagesCount, result.itemIndex);
	result.bytesType = ProcessingState::FileType::File; // TODO
	if (!progress.path.isEmpty()) {
		const auto last = progress.path.lastIndexOf('/');
		result.bytesName = progress.path.mid(last + 1);
	}
	result.bytesLoaded = progress.ready;
	result.bytesCount = progress.total;
}

int Controller::substepsInStep(Step step) const {
	Expects(_substepsInStep.size() > static_cast<int>(step));

	return _substepsInStep[static_cast<int>(step)];
}

void Controller::setFinishedState() {
	setState(FinishedState{
		_writer->mainFilePath(),
		_stats.filesCount(),
		_stats.bytesCount() });
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

void ControllerWrap::startExport(
		const Settings &settings,
		const Environment &environment) {
	LOG(("Export Info: Started export to '%1'.").arg(settings.path));

	_wrapped.with([=](Controller &controller) {
		controller.startExport(settings, environment);
	});
}

void ControllerWrap::cancelExportFast() {
	LOG(("Export Info: Cancelled export."));

	_wrapped.with([=](Controller &controller) {
		controller.cancelExportFast();
	});
}

rpl::lifetime &ControllerWrap::lifetime() {
	return _lifetime;
}

ControllerWrap::~ControllerWrap() {
	LOG(("Export Info: Controller destroyed."));
}

} // namespace Export
