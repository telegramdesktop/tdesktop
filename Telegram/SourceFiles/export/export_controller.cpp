/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_controller.h"

#include "export/export_settings.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_abstract.h"
#include "mtproto/rpc_sender.h"
#include "mtproto/concurrent_sender.h"

namespace Export {
namespace {

constexpr auto kUserpicsSliceLimit = 100;

} // namespace

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
	void apiError(const RPCError &error);
	void apiError(const QString &error);
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

	void exportUserpicsSlice(const MTPphotos_Photos &result);

	bool normalizePath();

	MTP::ConcurrentSender _mtp;
	Settings _settings;

	// rpl::variable<State> fails to compile in MSVC :(
	State _state;
	rpl::event_stream<State> _stateChanges;

	mtpRequestId _passwordRequestId = 0;

	std::unique_ptr<Output::AbstractWriter> _writer;
	std::vector<ProcessingState::Step> _steps;
	int _stepIndex = -1;

	MTPInputUser _user = MTP_inputUserSelf();

};

Controller::Controller(crl::weak_on_queue<Controller> weak)
: _mtp(weak)
, _state(PasswordCheckState{}) {
	requestPasswordState();
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

void Controller::apiError(const RPCError &error) {
	setState(ErrorState{ ErrorState::Type::API, error });
}

void Controller::apiError(const QString &error) {
	apiError(MTP_rpc_error(MTP_int(0), MTP_string("API_ERROR: " + error)));
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
	_mtp.request(base::take(_passwordRequestId)).cancel();
	requestPasswordState();
}

void Controller::requestPasswordState() {
	if (_passwordRequestId) {
		return;
	}
	_passwordRequestId = _mtp.request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		passwordStateDone(result);
	}).fail([=](const RPCError &error) {
		apiError(error);
	}).send();
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
	const auto base = QString("DataExport.%1.%2.%3"
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
	if (!(_settings.types & Settings::Type::PersonalInfo)) {
		exportUserpics();
		return;
	}
	_mtp.request(MTPusers_GetFullUser(
		_user
	)).done([=](const MTPUserFull &result) {
		Expects(result.type() == mtpc_userFull);

		const auto &full = result.c_userFull();
		if (full.vuser.type() == mtpc_user) {
			_writer->writePersonal(Data::ParsePersonalInfo(result));
			exportNext();
		} else {
			apiError("Bad user type.");
		}
	}).fail([=](const RPCError &error) {
		apiError(error);
	}).send();
}

void Controller::exportUserpics() {
	_mtp.request(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0),
		MTP_long(0),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) {
		_writer->writeUserpicsStart([&] {
			auto info = Data::UserpicsInfo();
			switch (result.type()) {
			case mtpc_photos_photos: {
				const auto &data = result.c_photos_photos();
				info.count = data.vphotos.v.size();
			} break;

			case mtpc_photos_photosSlice: {
				const auto &data = result.c_photos_photosSlice();
				info.count = data.vcount.v;
			} break;

			default: Unexpected("Photos type in Controller::exportUserpics.");
			}
			return info;
		}());

		exportUserpicsSlice(result);
	}).fail([=](const RPCError &error) {
		apiError(error);
	}).send();
}

void Controller::exportUserpicsSlice(const MTPphotos_Photos &result) {
	const auto finish = [&] {
		_writer->writeUserpicsEnd();
		exportNext();
	};
	switch (result.type()) {
	case mtpc_photos_photos: {
		const auto &data = result.c_photos_photos();

		_writer->writeUserpicsSlice(
			Data::ParseUserpicsSlice(data.vphotos));

		finish();
	} break;

	case mtpc_photos_photosSlice: {
		const auto &data = result.c_photos_photosSlice();

		const auto slice = Data::ParseUserpicsSlice(data.vphotos);
		_writer->writeUserpicsSlice(slice);

		if (slice.list.empty()) {
			finish();
		} else {
			_mtp.request(MTPphotos_GetUserPhotos(
				_user,
				MTP_int(0),
				MTP_long(slice.list.back().id),
				MTP_int(kUserpicsSliceLimit)
			)).done([=](const MTPphotos_Photos &result) {
				exportUserpicsSlice(result);
			}).fail([=](const RPCError &error) {
				apiError(error);
			}).send();
		}
	} break;

	default: Unexpected("Photos type in Controller::exportUserpicsSlice.");
	}
}

void Controller::exportContacts() {
	exportNext();
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
