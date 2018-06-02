/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_controller.h"

#include "export/export_settings.h"
#include "mtproto/rpc_sender.h"
#include "mtproto/concurrent_sender.h"

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
	void setState(State &&state);
	void apiError(const RPCError &error);
	void apiError(const QString &error);
	void ioError(const QString &path);
	void setFinishedState();

	void requestPasswordState();
	void passwordStateDone(const MTPaccount_Password &password);

	MTP::ConcurrentSender _mtp;
	Settings _settings;

	// rpl::variable<State> fails to compile in MSVC :(
	State _state;
	rpl::event_stream<State> _stateChanges;

	mtpRequestId _passwordRequestId = 0;

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
	setState(ProcessingState());

	_mtp.request(MTPusers_GetFullUser(
		MTP_inputUserSelf()
	)).done([=](const MTPUserFull &result) {
		Expects(result.type() == mtpc_userFull);

		const auto &full = result.c_userFull();
		if (full.vuser.type() != mtpc_user) {
			apiError("Bad user type.");
			return;
		}
		const auto &user = full.vuser.c_user();

		QFile f(_settings.path + "personal.txt");
		if (!f.open(QIODevice::WriteOnly)) {
			ioError(f.fileName());
			return;
		}
		QTextStream stream(&f);
		stream.setCodec("UTF-8");
		if (user.has_first_name()) {
			stream << "First name: " << qs(user.vfirst_name) << "\n";
		}
		if (user.has_last_name()) {
			stream << "Last name: " << qs(user.vlast_name) << "\n";
		}
		if (user.has_phone()) {
			stream << "Phone number: " << qs(user.vphone) << "\n";
		}
		if (user.has_username()) {
			stream << "Username: @" << qs(user.vusername) << "\n";
		}
		setFinishedState();
	}).fail([=](const RPCError &error) {
		apiError(error);
	}).send();
}

void Controller::setFinishedState() {
	setState(FinishedState{ _settings.path });
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
