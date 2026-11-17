/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_authorizations.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "core/core_settings.h"
#include "data/business/data_business_chatbots.h"
#include "data/business/data_business_info.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"

namespace Api {
namespace {

constexpr auto TestApiId = 17349;
constexpr auto SnapApiId = 611335;
constexpr auto DesktopApiId = 2040;

Authorizations::Entry ParseEntry(const MTPDauthorization &data) {
	auto result = Authorizations::Entry();

	result.hash = data.is_current() ? 0 : data.vhash().v;
	result.incomplete = data.is_password_pending();
	result.callsDisabled = data.is_call_requests_disabled();

	const auto apiId = result.apiId = data.vapi_id().v;
	const auto isTest = (apiId == TestApiId);
	const auto isDesktop = (apiId == DesktopApiId)
		|| (apiId == SnapApiId)
		|| isTest;

	const auto appName = isDesktop
		? u"Telegram Desktop%1"_q.arg(isTest ? " (GitHub)" : QString())
		: qs(data.vapp_name());// + u" for "_q + qs(d.vplatform());
	const auto appVer = [&] {
		const auto version = qs(data.vapp_version());
		if (isDesktop) {
			const auto verInt = version.toInt();
			if (version == QString::number(verInt)) {
				return Core::FormatVersionDisplay(verInt);
			}
		} else {
			if (const auto index = version.indexOf('('); index >= 0) {
				return version.mid(index);
			}
		}
		return version;
	}();

	result.name = result.hash
		? qs(data.vdevice_model())
		: Core::App().settings().deviceModel();

	const auto country = qs(data.vcountry());
	//const auto platform = qs(data.vplatform());
	//const auto &countries = countriesByISO2();
	//const auto j = countries.constFind(country);
	//if (j != countries.cend()) {
	//	country = QString::fromUtf8(j.value()->name);
	//}
	result.system = qs(data.vsystem_version());
	result.platform = qs(data.vplatform());
	result.activeTime = data.vdate_active().v
		? data.vdate_active().v
		: data.vdate_created().v;
	result.info = QString("%1%2").arg(
		appName,
		appVer.isEmpty() ? QString() : (' ' + appVer));
	result.ip = qs(data.vip());
	result.active = result.hash
		? Authorizations::ActiveDateString(result.activeTime)
		: tr::lng_status_online(tr::now);
	result.location = country;

	return result;
}

} // namespace

Authorizations::Authorizations(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance())
, _autoconfirmPeriod([=] {
	constexpr auto kFallbackCount = 604800;
	return api->session().appConfig().get<int>(
		u"authorization_autoconfirm_period"_q,
		kFallbackCount);
})
, _saveUnreviewed([=] {
	api->session().settings().setUnreviewed(_unreviewed);
	api->session().saveSettingsDelayed();
})
, _unreviewedTimer([=] {
	removeExpiredUnreviewed();
	_unreviewedChanges.fire({});
}) {
	_unreviewed = api->session().settings().unreviewed();
	crl::on_main(&api->session(), [=] {
		removeExpiredUnreviewed();
		if (!_unreviewed.empty()) {
			reload();
			_session->data().chatbots().preload();
		}
		_session->changes().peerUpdates(
			Data::PeerUpdate::Flag::Name | Data::PeerUpdate::Flag::Username
		) | rpl::on_next([=] {
			refreshBotNames();
		}, _lifetime);
	});
	_reviewTimer.setCallback([=] { reviewTimedOut(); });
	Core::App().settings().deviceModelChanges(
	) | rpl::on_next([=](const QString &model) {
		auto changed = false;
		for (auto &entry : _list) {
			if (!entry.hash) {
				entry.name = model;
				changed = true;
			}
		}
		if (changed) {
			_listChanges.fire({});
		}
	}, _lifetime);

	if (Core::App().settings().disableCallsLegacy()) {
		toggleCallsDisabledHere(true);
	}
}

void Authorizations::reload() {
	if (_requestId) {
		return;
	}

	const auto snapshot = _unreviewed;
	_requestId = _api.request(MTPaccount_GetAuthorizations(
	)).done([=](const MTPaccount_Authorizations &result) {
		_requestId = 0;
		_lastReceived = crl::now();
		const auto &data = result.data();
		_ttlDays = data.vauthorization_ttl_days().v;
		_list = ranges::views::all(
			data.vauthorizations().v
		) | ranges::views::transform([](const MTPAuthorization &auth) {
			return ParseEntry(auth.data());
		}) | ranges::to<List>;
		reconcileAuthorizations(result, snapshot);
		refreshCallsDisabledHereFromCloud();
		_listChanges.fire({});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void Authorizations::cancelCurrentRequest() {
	_api.request(base::take(_requestId)).cancel();
}

void Authorizations::refreshCallsDisabledHereFromCloud() {
	const auto that = ranges::find(_list, 0, &Entry::hash);
	if (that != end(_list)
		&& !_toggleCallsDisabledRequests.contains(0)) {
		_callsDisabledHere = that->callsDisabled;
	}
}

void Authorizations::requestTerminate(
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
		std::optional<uint64> hash) {
	const auto send = [&](auto request) {
		_api.request(
			std::move(request)
		).done([=, done = std::move(done)](const MTPBool &result) {
			done(result);
			if (mtpIsTrue(result)) {
				if (hash) {
					_list.erase(
						ranges::remove(_list, *hash, &Entry::hash),
						end(_list));
				} else {
					_list.clear();
				}
				_listChanges.fire({});
			}
		}).fail(
			std::move(fail)
		).send();
	};
	if (hash) {
		send(MTPaccount_ResetAuthorization(MTP_long(*hash)));
	} else {
		send(MTPauth_ResetAuthorizations());
	}
}

Authorizations::List Authorizations::list() const {
	return _list;
}

auto Authorizations::listValue() const
-> rpl::producer<Authorizations::List> {
	return rpl::single(
		list()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return list(); })
	);
}

rpl::producer<int> Authorizations::totalValue() const {
	return rpl::single(
		total()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return total(); })
	);
}

void Authorizations::updateTTL(int days) {
	_api.request(_ttlRequestId).cancel();
	_ttlRequestId = _api.request(MTPaccount_SetAuthorizationTTL(
		MTP_int(days)
	)).done([=] {
		_ttlRequestId = 0;
	}).fail([=] {
		_ttlRequestId = 0;
	}).send();
	_ttlDays = days;
}

rpl::producer<int> Authorizations::ttlDays() const {
	return _ttlDays.value() | rpl::filter(rpl::mappers::_1 != 0);
}

void Authorizations::toggleCallsDisabled(uint64 hash, bool disabled) {
	if (const auto sent = _toggleCallsDisabledRequests.take(hash)) {
		_api.request(*sent).cancel();
	}
	using Flag = MTPaccount_ChangeAuthorizationSettings::Flag;
	const auto id = _api.request(MTPaccount_ChangeAuthorizationSettings(
		MTP_flags(Flag::f_call_requests_disabled),
		MTP_long(hash),
		MTPBool(), // encrypted_requests_disabled
		MTP_bool(disabled)
	)).done([=] {
		_toggleCallsDisabledRequests.remove(hash);
	}).fail([=] {
		_toggleCallsDisabledRequests.remove(hash);
	}).send();
	_toggleCallsDisabledRequests.emplace(hash, id);
	if (!hash) {
		_callsDisabledHere = disabled;
	}
}

bool Authorizations::callsDisabledHere() const {
	return _callsDisabledHere.current();
}

rpl::producer<bool> Authorizations::callsDisabledHereValue() const {
	return _callsDisabledHere.value();
}

QString Authorizations::ActiveDateString(TimeId active) {
	const auto now = QDateTime::currentDateTime();
	const auto lastTime = base::unixtime::parse(active);
	const auto nowDate = now.date();
	const auto lastDate = lastTime.date();
	return (lastDate == nowDate)
		? QLocale().toString(lastTime.time(), QLocale::ShortFormat)
		: (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber())
		? langDayOfWeek(lastDate)
		: QLocale().toString(lastDate, QLocale::ShortFormat);
}

int Authorizations::total() const {
	return ranges::count_if(
		_list,
		ranges::not_fn(&Entry::incomplete));
}

crl::time Authorizations::lastReceivedTime() {
	return _lastReceived;
}

const std::vector<Data::UnreviewedAuth> &Authorizations::unreviewed() {
	removeExpiredUnreviewed();
	return _unreviewed;
}

void Authorizations::removeExpiredUnreviewed() {
	const auto now = base::unixtime::now();
	const auto period = _autoconfirmPeriod();

	for (auto i = begin(_reviewed); i != end(_reviewed);) {
		if (now - i->second >= period) {
			i = _reviewed.erase(i);
		} else {
			++i;
		}
	}
	const auto oldSize = _unreviewed.size();
	_unreviewed.erase(
		std::remove_if(_unreviewed.begin(), _unreviewed.end(),
			[=](const auto &auth) {
				return (now - auth.date) >= period;
			}),
		_unreviewed.end());

	if (_unreviewed.size() != oldSize) {
		_saveUnreviewed();
	}
	_unreviewedTimer.cancel();
	if (!_unreviewed.empty()) {
		const auto earliest = ranges::min(
			_unreviewed,
			{},
			&Data::UnreviewedAuth::date);
		_unreviewedTimer.callOnce(std::min(
			(earliest.date + period - now) * crl::time(1000),
			crl::time(std::numeric_limits<int>::max())));
	}
}

void Authorizations::review(
		const std::vector<Data::UnreviewedAuth> &list,
		bool confirm,
		Fn<void(std::vector<Data::UnreviewedAuth>)> done) {
	struct State {
		int remaining = 0;
		std::vector<Data::UnreviewedAuth> success;
		Fn<void(std::vector<Data::UnreviewedAuth>)> done;
	};
	const auto state = std::make_shared<State>();
	state->remaining = int(list.size());
	state->done = std::move(done);
	if (list.empty()) {
		state->done({});
		return;
	}
	for (const auto &auth : list) {
		const auto key = std::pair(auth.hash, auth.botId);
		const auto callback = [=](bool success) {
			if (success) {
				state->success.push_back(auth);
			}
			if (!--state->remaining) {
				state->done(std::move(state->success));
			}
		};
		const auto i = _reviewRequests.find(key);
		if (i != end(_reviewRequests)) {
			const auto same = (i->second.confirm == confirm)
				&& (!auth.botId || auth.date <= i->second.auth.date);
			i->second.callbacks.push_back([=](bool success) {
				callback(success && same);
			});
			continue;
		}
		_reviewRequests.emplace(key, ReviewRequest{
			.confirm = confirm,
			.auth = auth,
			.callbacks = { callback },
		});
		_reviewing = true;
		if (!_reviewTimer.isActive()) {
			_reviewTimer.callOnce(30000);
		}
		reviewOne(auth, confirm);
	}
}

void Authorizations::reviewTimedOut() {
	const auto pending = _reviewRequests;
	for (const auto &[key, request] : pending) {
		const auto i = _reviewRequests.find(key);
		if (i != end(_reviewRequests) && i->second.id == request.id) {
			_api.request(request.id).cancel();
			finishReview(request.auth, false);
		}
	}
	reload();
	_session->data().chatbots().reload();
}

rpl::producer<bool> Authorizations::reviewingValue() const {
	return _reviewing.value();
}

void Authorizations::finishReview(
		Data::UnreviewedAuth auth,
		bool success,
		bool resolved) {
	const auto request = _reviewRequests.take({ auth.hash, auth.botId });
	if (!request) {
		return;
	}
	if (success || resolved) {
		_reviewed[{ auth.hash, auth.botId }] = auth.date;
		const auto matches = [&](const Data::UnreviewedAuth &now) {
			return now.hash == auth.hash
				&& now.botId == auth.botId
				&& (!auth.botId || now.date <= auth.date);
		};
		_unreviewed.erase(
			ranges::remove_if(_unreviewed, matches),
			end(_unreviewed));
		_saveUnreviewed();
		_unreviewedChanges.fire({});
	}
	_reviewing = !_reviewRequests.empty();
	if (_reviewRequests.empty()) {
		_reviewTimer.cancel();
	}
	if (!success && !resolved) {
		reload();
		if (auth.botId) {
			_session->data().chatbots().reload();
		}
	}
	for (const auto &callback : request->callbacks) {
		callback(success);
	}
}

void Authorizations::reviewOne(Data::UnreviewedAuth auth, bool confirm) {
	const auto key = std::pair(auth.hash, auth.botId);
	const auto finish = [=](bool success) { finishReview(auth, success); };
	const auto fail = [=](const MTP::Error &error) {
		const auto gone = !auth.botId && error.type() == u"HASH_INVALID"_q;
		finishReview(auth, false, gone);
	};
	auto id = mtpRequestId();
	if (auth.botId) {
		const auto user = _session->data().userLoaded(auth.botId);
		if (!user || !user->accessHash()) {
			id = _api.request(MTPaccount_GetConnectedBots(
			)).done([=](const MTPaccount_ConnectedBots &result) {
				reviewBotLoaded(auth, confirm, result);
			}).fail(fail).send();
		} else if (confirm) {
			id = _api.request(MTPaccount_ConfirmBotConnection(
				user->inputUser()
			)).done([=](const MTPBool &result) {
				finish(mtpIsTrue(result));
			}).fail(fail).send();
		} else {
			using Flag = MTPaccount_UpdateConnectedBot::Flag;
			id = _api.request(MTPaccount_UpdateConnectedBot(
				MTP_flags(Flag::f_deleted),
				MTPBusinessBotRights(),
				user->inputUser(),
				Data::ForBotsToMTP({})
			)).done([=](const MTPUpdates &result) {
				_session->api().applyUpdates(result);
				_session->data().chatbots().reload();
				finish(true);
			}).fail(fail).send();
		}
	} else if (confirm) {
		using Flag = MTPaccount_ChangeAuthorizationSettings::Flag;
		id = _api.request(MTPaccount_ChangeAuthorizationSettings(
			MTP_flags(Flag::f_confirmed),
			MTP_long(auth.hash),
			MTPBool(), // encrypted_requests_disabled
			MTPBool() // call_requests_disabled
		)).done([=](const MTPBool &result) {
			finish(mtpIsTrue(result));
		}).fail(fail).send();
	} else {
		id = _api.request(MTPaccount_ResetAuthorization(
			MTP_long(auth.hash)
		)).done([=](const MTPBool &result) {
			if (mtpIsTrue(result)) {
				_list.erase(
					ranges::remove(_list, auth.hash, &Entry::hash),
					end(_list));
				_listChanges.fire({});
			}
			finish(mtpIsTrue(result));
		}).fail(fail).send();
	}
	_reviewRequests[key].id = id;
}

void Authorizations::reviewBotLoaded(
		Data::UnreviewedAuth auth,
		bool confirm,
		const MTPaccount_ConnectedBots &result) {
	_session->data().processUsers(result.data().vusers());
	refreshBotNames();
	const auto found = ranges::any_of(
		result.data().vconnected_bots().v,
		[=](const auto &bot) {
			return bot.data().vbot_id().v == auth.botId;
		});
	const auto loaded = _session->data().userLoaded(auth.botId);
	if (!found) {
		finishReview(auth, false, true);
	} else if (!loaded || !loaded->accessHash()) {
		finishReview(auth, false);
	} else {
		reviewOne(auth, confirm);
	}
}

void Authorizations::reconcileAuthorizations(
		const MTPaccount_Authorizations &result,
		const std::vector<Data::UnreviewedAuth> &snapshot) {
	const auto &authorizations = result.data().vauthorizations().v;
	const auto resolved = [&](const Data::UnreviewedAuth &auth) {
		const auto pending = [&](const MTPAuthorization &entry) {
			return entry.data().vhash().v == auth.hash
				&& entry.data().is_unconfirmed();
		};
		return !auth.botId
			&& ranges::contains(snapshot, auth)
			&& !ranges::any_of(authorizations, pending);
	};
	_unreviewed.erase(
		ranges::remove_if(_unreviewed, resolved),
		end(_unreviewed));
	_saveUnreviewed();
	removeExpiredUnreviewed();
	_unreviewedChanges.fire({});
}

void Authorizations::reconcileBots(
		const MTPaccount_ConnectedBots &result,
		const std::vector<Data::UnreviewedAuth> &snapshot) {
	const auto &bots = result.data().vconnected_bots().v;
	const auto resolved = [&](const Data::UnreviewedAuth &auth) {
		const auto matchesSnapshot = [&](const Data::UnreviewedAuth &before) {
			return before.botId == auth.botId && before.date == auth.date;
		};
		const auto connected = [&](const MTPConnectedBot &bot) {
			return bot.data().vbot_id().v == auth.botId;
		};
		return auth.botId
			&& ranges::any_of(snapshot, matchesSnapshot)
			&& !ranges::any_of(bots, connected);
	};
	_unreviewed.erase(
		ranges::remove_if(_unreviewed, resolved),
		end(_unreviewed));
	_saveUnreviewed();
	refreshBotNames();
	_unreviewedChanges.fire({});
}

void Authorizations::refreshBotNames() {
	auto changed = false;
	for (auto &auth : _unreviewed) {
		if (!auth.botId) {
			continue;
		}
		if (const auto user = _session->data().userLoaded(auth.botId)) {
			const auto name = user->username().isEmpty()
				? user->name()
				: '@' + user->username();
			if (!name.isEmpty() && auth.botName != name) {
				auth.botName = name;
				changed = true;
			}
		}
	}
	if (changed) {
		_saveUnreviewed();
		_unreviewedChanges.fire({});
	}
}

rpl::producer<> Authorizations::unreviewedChanges() const {
	return _unreviewedChanges.events();
}

void Authorizations::apply(const MTPUpdate &update) {
	removeExpiredUnreviewed();
	auto unreviewed = update.match([&](
			const MTPDupdateNewAuthorization &data) {
		return Data::UnreviewedAuth{
			.hash = data.vhash().v,
			.unconfirmed = data.is_unconfirmed(),
			.date = data.vdate().value_or_empty(),
			.device = qs(data.vdevice().value_or_empty()),
			.location = qs(data.vlocation().value_or_empty()),
		};
	}, [&](const MTPDupdateNewBotConnection &data) {
		const auto user = _session->data().user(data.vbot_id().v);
		const auto username = user->username();
		_session->data().chatbots().reload();
		return Data::UnreviewedAuth{
			.unconfirmed = !data.is_confirmed(),
			.date = data.vdate().value_or_empty(),
			.device = qs(data.vdevice().value_or_empty()),
			.location = qs(data.vlocation().value_or_empty()),
			.botId = data.vbot_id().v,
			.botAccessHash = user->accessHash(),
			.botName = username.isEmpty() ? user->name() : '@' + username,
		};
	}, [](auto&&) -> Data::UnreviewedAuth {
		Unexpected("Update in Authorizations::apply.");
	});
	const auto i = ranges::find_if(_unreviewed, [&](const auto &auth) {
		return auth.hash == unreviewed.hash && auth.botId == unreviewed.botId;
	});
	const auto key = std::pair(unreviewed.hash, unreviewed.botId);
	const auto reviewed = _reviewed.find(key);
	if (unreviewed.unconfirmed
		&& reviewed != end(_reviewed)
		&& unreviewed.date <= reviewed->second) {
		return;
	}
	if (!unreviewed.unconfirmed && i != end(_unreviewed)) {
		_reviewed[key] = i->date;
	}
	if (i == end(_unreviewed)) {
		if (unreviewed.unconfirmed) {
			_unreviewed.push_back(std::move(unreviewed));
		}
	} else if (!unreviewed.unconfirmed) {
		_unreviewed.erase(i);
	} else if (unreviewed.date >= i->date) {
		*i = std::move(unreviewed);
	}
	_saveUnreviewed();
	removeExpiredUnreviewed();
	_unreviewedChanges.fire({});
}

} // namespace Api
