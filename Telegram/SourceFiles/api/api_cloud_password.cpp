/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_cloud_password.h"

#include "apiwrap.h"
#include "base/random.h"
#include "core/core_cloud_password.h"
#include "passport/passport_encryption.h"

#include "base/unixtime.h"
#include "base/call_delayed.h"

namespace Api {
namespace {

[[nodiscard]] Core::CloudPasswordState ProcessMtpState(
		const MTPaccount_password &state) {
	return state.match([&](const MTPDaccount_password &data) {
		base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
		return Core::ParseCloudPasswordState(data);
	});
}

} // namespace

CloudPassword::CloudPassword(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

void CloudPassword::apply(Core::CloudPasswordState state) {
	if (_state) {
		*_state = std::move(state);
	} else {
		_state = std::make_unique<Core::CloudPasswordState>(std::move(state));
	}
	_stateChanges.fire_copy(*_state);
}

void CloudPassword::reload() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_requestId = 0;
		apply(ProcessMtpState(result));
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void CloudPassword::clearUnconfirmedPassword() {
	_requestId = _api.request(MTPaccount_CancelPasswordEmail(
	)).done([=] {
		_requestId = 0;
		reload();
	}).fail([=] {
		_requestId = 0;
		reload();
	}).send();
}

rpl::producer<Core::CloudPasswordState> CloudPassword::state() const {
	return _state
		? _stateChanges.events_starting_with_copy(*_state)
		: (_stateChanges.events() | rpl::type_erased());
}

auto CloudPassword::stateCurrent() const
-> std::optional<Core::CloudPasswordState> {
	return _state
		? base::make_optional(*_state)
		: std::nullopt;
}

auto CloudPassword::resetPassword()
-> rpl::producer<CloudPassword::ResetRetryDate, QString> {
	return [=](auto consumer) {
		_api.request(MTPaccount_ResetPassword(
		)).done([=](const MTPaccount_ResetPasswordResult &result) {
			result.match([&](const MTPDaccount_resetPasswordOk &data) {
				reload();
			}, [&](const MTPDaccount_resetPasswordRequestedWait &data) {
				if (!_state) {
					reload();
					return;
				}
				const auto until = data.vuntil_date().v;
				if (_state->pendingResetDate != until) {
					_state->pendingResetDate = until;
					_stateChanges.fire_copy(*_state);
				}
			}, [&](const MTPDaccount_resetPasswordFailedWait &data) {
				consumer.put_next_copy(data.vretry_date().v);
			});
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return rpl::lifetime();
	};
}

auto CloudPassword::cancelResetPassword()
-> rpl::producer<rpl::no_value, QString> {
	return [=](auto consumer) {
		_api.request(MTPaccount_DeclinePasswordReset(
		)).done([=] {
			reload();
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return rpl::lifetime();
	};
}

rpl::producer<CloudPassword::SetOk, QString> CloudPassword::set(
		const QString &oldPassword,
		const QString &newPassword,
		const QString &hint,
		bool hasRecoveryEmail,
		const QString &recoveryEmail) {

	const auto generatePasswordCheck = [=](
			const Core::CloudPasswordState &latestState) {
		if (oldPassword.isEmpty() || !latestState.hasPassword) {
			return Core::CloudPasswordResult{
				MTP_inputCheckPasswordEmpty()
			};
		}
		const auto hash = Core::ComputeCloudPasswordHash(
			latestState.mtp.request.algo,
			bytes::make_span(oldPassword.toUtf8()));
		return Core::ComputeCloudPasswordCheck(
			latestState.mtp.request,
			hash);
	};

	const auto finish = [=](auto consumer, int unconfirmedEmailLengthCode) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			apply(ProcessMtpState(result));
			if (unconfirmedEmailLengthCode) {
				consumer.put_next(SetOk{ unconfirmedEmailLengthCode });
			} else {
				consumer.put_done();
			}
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();
	};

	const auto sendMTPaccountUpdatePasswordSettings = [=](
			const Core::CloudPasswordState &latestState,
			const QByteArray &secureSecret,
			auto consumer) {
		const auto newPasswordBytes = newPassword.toUtf8();
		const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
			latestState.mtp.newPassword,
			bytes::make_span(newPasswordBytes));
		if (!newPassword.isEmpty() && newPasswordHash.modpow.empty()) {
			consumer.put_error("INTERNAL_SERVER_ERROR");
			return;
		}
		using Flag = MTPDaccount_passwordInputSettings::Flag;
		const auto flags = Flag::f_new_algo
			| Flag::f_new_password_hash
			| Flag::f_hint
			| (secureSecret.isEmpty() ? Flag(0) : Flag::f_new_secure_settings)
			| ((!hasRecoveryEmail) ? Flag(0) : Flag::f_email);

		auto newSecureSecret = bytes::vector();
		auto newSecureSecretId = 0ULL;
		if (!secureSecret.isEmpty()) {
			newSecureSecretId = Passport::CountSecureSecretId(
				bytes::make_span(secureSecret));
			newSecureSecret = Passport::EncryptSecureSecret(
				bytes::make_span(secureSecret),
				Core::ComputeSecureSecretHash(
					latestState.mtp.newSecureSecret,
					bytes::make_span(newPasswordBytes)));
		}
		const auto settings = MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(newPassword.isEmpty()
				? v::null
				: latestState.mtp.newPassword),
			newPassword.isEmpty()
				? MTP_bytes()
				: MTP_bytes(newPasswordHash.modpow),
			MTP_string(hint),
			MTP_string(recoveryEmail),
			MTP_secureSecretSettings(
				Core::PrepareSecureSecretAlgo(
					latestState.mtp.newSecureSecret),
				MTP_bytes(newSecureSecret),
				MTP_long(newSecureSecretId)));
		_api.request(MTPaccount_UpdatePasswordSettings(
			generatePasswordCheck(latestState).result,
			settings
		)).done([=] {
			finish(consumer, 0);
		}).fail([=](const MTP::Error &error) {
			const auto &type = error.type();
			const auto prefix = u"EMAIL_UNCONFIRMED_"_q;
			if (type.startsWith(prefix)) {
				const auto codeLength = base::StringViewMid(
					type,
					prefix.size()).toInt();

				finish(consumer, codeLength);
			} else {
				consumer.put_error_copy(type);
			}
		}).handleFloodErrors().send();
	};

	return [=](auto consumer) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			const auto latestState = ProcessMtpState(result);

			if (latestState.hasPassword
					&& !oldPassword.isEmpty()
					&& !newPassword.isEmpty()) {

				_api.request(MTPaccount_GetPasswordSettings(
					generatePasswordCheck(latestState).result
				)).done([=](const MTPaccount_PasswordSettings &result) {
					using Settings = MTPDaccount_passwordSettings;
					const auto &data = result.match([&](
							const Settings &data) -> const Settings & {
						return data;
					});
					auto secureSecret = QByteArray();
					if (const auto wrapped = data.vsecure_settings()) {
						using Secure = MTPDsecureSecretSettings;
						const auto &settings = wrapped->match([](
								const Secure &data) -> const Secure & {
							return data;
						});
						const auto passwordUtf = oldPassword.toUtf8();
						const auto secret = Passport::DecryptSecureSecret(
							bytes::make_span(settings.vsecure_secret().v),
							Core::ComputeSecureSecretHash(
								Core::ParseSecureSecretAlgo(
									settings.vsecure_algo()),
								bytes::make_span(passwordUtf)));
						if (secret.empty()) {
							LOG(("API Error: "
								"Failed to decrypt secure secret."));
							consumer.put_error("SUGGEST_SECRET_RESET");
							return;
						} else if (Passport::CountSecureSecretId(secret)
								!= settings.vsecure_secret_id().v) {
							LOG(("API Error: Wrong secure secret id."));
							consumer.put_error("SUGGEST_SECRET_RESET");
							return;
						} else {
							secureSecret = QByteArray(
								reinterpret_cast<const char*>(secret.data()),
								secret.size());
						}
					}
					_api.request(MTPaccount_GetPassword(
					)).done([=](const MTPaccount_Password &result) {
						const auto latestState = ProcessMtpState(result);
						sendMTPaccountUpdatePasswordSettings(
							latestState,
							secureSecret,
							consumer);
					}).fail([=](const MTP::Error &error) {
						consumer.put_error_copy(error.type());
					}).send();
				}).fail([=](const MTP::Error &error) {
					consumer.put_error_copy(error.type());
				}).send();
			} else {
				sendMTPaccountUpdatePasswordSettings(
					latestState,
					QByteArray(),
					consumer);
			}
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();
		return rpl::lifetime();
	};
}

rpl::producer<rpl::no_value, QString> CloudPassword::check(
		const QString &password) {
	return [=](auto consumer) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			const auto latestState = ProcessMtpState(result);
			const auto input = [&] {
				if (password.isEmpty()) {
					return Core::CloudPasswordResult{
						MTP_inputCheckPasswordEmpty()
					};
				}
				const auto hash = Core::ComputeCloudPasswordHash(
					latestState.mtp.request.algo,
					bytes::make_span(password.toUtf8()));
				return Core::ComputeCloudPasswordCheck(
					latestState.mtp.request,
					hash);
			}();

			_api.request(MTPaccount_GetPasswordSettings(
				input.result
			)).done([=](const MTPaccount_PasswordSettings &result) {
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return rpl::lifetime();
	};
}

rpl::producer<rpl::no_value, QString> CloudPassword::confirmEmail(
		const QString &code) {
	return [=](auto consumer) {
		_api.request(MTPaccount_ConfirmPasswordEmail(
			MTP_string(code)
		)).done([=] {
			_api.request(MTPaccount_GetPassword(
			)).done([=](const MTPaccount_Password &result) {
				apply(ProcessMtpState(result));
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();

		return rpl::lifetime();
	};
}

rpl::producer<rpl::no_value, QString> CloudPassword::resendEmailCode() {
	return [=](auto consumer) {
		_api.request(MTPaccount_ResendPasswordEmail(
		)).done([=] {
			_api.request(MTPaccount_GetPassword(
			)).done([=](const MTPaccount_Password &result) {
				apply(ProcessMtpState(result));
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();

		return rpl::lifetime();
	};
}

rpl::producer<CloudPassword::SetOk, QString> CloudPassword::setEmail(
		const QString &oldPassword,
		const QString &recoveryEmail) {
	const auto generatePasswordCheck = [=](
			const Core::CloudPasswordState &latestState) {
		if (oldPassword.isEmpty() || !latestState.hasPassword) {
			return Core::CloudPasswordResult{
				MTP_inputCheckPasswordEmpty()
			};
		}
		const auto hash = Core::ComputeCloudPasswordHash(
			latestState.mtp.request.algo,
			bytes::make_span(oldPassword.toUtf8()));
		return Core::ComputeCloudPasswordCheck(
			latestState.mtp.request,
			hash);
	};

	const auto finish = [=](auto consumer, int unconfirmedEmailLengthCode) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			apply(ProcessMtpState(result));
			if (unconfirmedEmailLengthCode) {
				consumer.put_next(SetOk{ unconfirmedEmailLengthCode });
			} else {
				consumer.put_done();
			}
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();
	};

	const auto sendMTPaccountUpdatePasswordSettings = [=](
			const Core::CloudPasswordState &latestState,
			auto consumer) {
		const auto settings = MTP_account_passwordInputSettings(
			MTP_flags(MTPDaccount_passwordInputSettings::Flag::f_email),
			MTP_passwordKdfAlgoUnknown(),
			MTP_bytes(),
			MTP_string(),
			MTP_string(recoveryEmail),
			MTPSecureSecretSettings());
		_api.request(MTPaccount_UpdatePasswordSettings(
			generatePasswordCheck(latestState).result,
			settings
		)).done([=] {
			finish(consumer, 0);
		}).fail([=](const MTP::Error &error) {
			const auto &type = error.type();
			const auto prefix = u"EMAIL_UNCONFIRMED_"_q;
			if (type.startsWith(prefix)) {
				const auto codeLength = base::StringViewMid(
					type,
					prefix.size()).toInt();

				finish(consumer, codeLength);
			} else {
				consumer.put_error_copy(type);
			}
		}).handleFloodErrors().send();
	};

	return [=](auto consumer) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			const auto latestState = ProcessMtpState(result);
			sendMTPaccountUpdatePasswordSettings(latestState, consumer);
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();
		return rpl::lifetime();
	};
}

rpl::producer<rpl::no_value, QString> CloudPassword::recoverPassword(
		const QString &code,
		const QString &newPassword,
		const QString &newHint) {

	const auto finish = [=](auto consumer) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			apply(ProcessMtpState(result));
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();
	};

	const auto sendMTPaccountUpdatePasswordSettings = [=](
			const Core::CloudPasswordState &latestState,
			auto consumer) {
		const auto newPasswordBytes = newPassword.toUtf8();
		const auto newPasswordHash = Core::ComputeCloudPasswordDigest(
			latestState.mtp.newPassword,
			bytes::make_span(newPasswordBytes));
		if (!newPassword.isEmpty() && newPasswordHash.modpow.empty()) {
			consumer.put_error("INTERNAL_SERVER_ERROR");
			return;
		}
		using Flag = MTPDaccount_passwordInputSettings::Flag;
		const auto flags = Flag::f_new_algo
			| Flag::f_new_password_hash
			| Flag::f_hint;

		const auto settings = MTP_account_passwordInputSettings(
			MTP_flags(flags),
			Core::PrepareCloudPasswordAlgo(newPassword.isEmpty()
				? v::null
				: latestState.mtp.newPassword),
			newPassword.isEmpty()
				? MTP_bytes()
				: MTP_bytes(newPasswordHash.modpow),
			MTP_string(newHint),
			MTP_string(),
			MTPSecureSecretSettings());

		_api.request(MTPauth_RecoverPassword(
			MTP_flags(newPassword.isEmpty()
				? MTPauth_RecoverPassword::Flags(0)
				: MTPauth_RecoverPassword::Flag::f_new_settings),
			MTP_string(code),
			settings
		)).done([=](const MTPauth_Authorization &result) {
			finish(consumer);
		}).fail([=](const MTP::Error &error) {
			const auto &type = error.type();
			consumer.put_error_copy(type);
		}).handleFloodErrors().send();
	};

	return [=](auto consumer) {
		_api.request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			const auto latestState = ProcessMtpState(result);
			sendMTPaccountUpdatePasswordSettings(latestState, consumer);
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();
		return rpl::lifetime();
	};
}

rpl::producer<QString, QString> CloudPassword::requestPasswordRecovery() {
	return [=](auto consumer) {
		_api.request(MTPauth_RequestPasswordRecovery(
		)).done([=](const MTPauth_PasswordRecovery &result) {
			result.match([&](const MTPDauth_passwordRecovery &data) {
				consumer.put_next(qs(data.vemail_pattern().v));
			});
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();
		return rpl::lifetime();
	};
}

auto CloudPassword::checkRecoveryEmailAddressCode(const QString &code)
-> rpl::producer<rpl::no_value, QString> {
	return [=](auto consumer) {
		_api.request(MTPauth_CheckRecoveryPassword(
			MTP_string(code)
		)).done([=] {
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).handleFloodErrors().send();

		return rpl::lifetime();
	};
}

} // namespace Api
