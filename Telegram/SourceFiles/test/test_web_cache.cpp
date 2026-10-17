/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_web_cache.h"

#ifdef _DEBUG

#include "base/random.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/image/image_location.h"

namespace Test {
namespace {

using Storage::Cache::Error;
using Storage::Cache::Key;

// Fabricated under the reserved .invalid TLD: no loader is ever handed
// these, and no resolver could answer them if one were.
constexpr auto kSubjectUrl
	= "https://web-cache-selftest.example.invalid/subject.bin";
constexpr auto kControlUrl
	= "https://web-cache-selftest.example.invalid/control.bin";

// Any non-zero access hash: the premise stage proves it never enters the key.
constexpr auto kOtherAccessHash = uint64(0x5EEDull);

[[nodiscard]] QString ErrorText(const Error &error) {
	const auto type = [&] {
		switch (error.type) {
		case Error::Type::None: return u"none"_q;
		case Error::Type::IO: return u"io"_q;
		case Error::Type::WrongKey: return u"wrong-key"_q;
		case Error::Type::LockFailed: return u"lock-failed"_q;
		}
		return u"unknown"_q;
	}();
	return error.path.isEmpty()
		? type
		: u"%1:%2"_q.arg(type, error.path);
}

[[nodiscard]] QString HalfText(bool done, const Error &error) {
	if (!done) {
		return u"pending"_q;
	} else if (error.type == Error::Type::None) {
		return u"ok"_q;
	}
	return ErrorText(error);
}

[[nodiscard]] QString KeyText(const Key &key) {
	return u"%1:%2"_q.arg(
		QString::number(key.high, 16).rightJustified(16, QChar('0')),
		QString::number(key.low, 16).rightJustified(16, QChar('0')));
}

[[nodiscard]] Key UrlKey(const QByteArray &url, uint64 accessHash = 0) {
	return Data::WebDocumentCacheKey(WebFileLocation(url, accessHash));
}

[[nodiscard]] QString ReadText(
		const std::optional<QByteArray> &read,
		const QByteArray &expected) {
	return u"expectedSize=%1 readSize=%2 equal=%3"_q.arg(
		QString::number(expected.size()),
		read ? QString::number(read->size()) : u"none"_q,
		(read && (*read == expected)) ? u"1"_q : u"0"_q);
}

} // namespace

bool WebCacheRemoval::finished() const {
	return smallDone && bigDone;
}

bool WebCacheRemoval::ok() const {
	return finished()
		&& (smallError.type == Error::Type::None)
		&& (bigError.type == Error::Type::None);
}

QString WebCacheRemovalText(const WebCacheRemoval &removal) {
	return u"key=%1 small=%2 big=%3 session=%4 finished=%5"_q.arg(
		KeyText(removal.key),
		HalfText(removal.smallDone, removal.smallError),
		HalfText(removal.bigDone, removal.bigError),
		removal.session ? u"alive"_q : u"gone"_q,
		removal.finished() ? u"1"_q : u"0"_q);
}

std::shared_ptr<const WebCacheRemoval> RemoveWebDocumentCache(
		not_null<Main::Session*> session,
		const QByteArray &url) {
	const auto state = std::make_shared<WebCacheRemoval>();
	state->key = UrlKey(url);
	state->session = base::make_weak(session);

	// Both callbacks run on their database's queue thread: they capture no
	// session reference and write the reading only after the guarded hop.
	const auto weak = state->session;
	session->data().cache().remove(state->key, [=](Error error) {
		crl::on_main(weak, [=] {
			state->smallDone = true;
			state->smallError = error;
		});
	});
	session->data().cacheBigFile().remove(state->key, [=](Error error) {
		crl::on_main(weak, [=] {
			state->bigDone = true;
			state->bigError = error;
		});
	});
	return state;
}

void AppendWebDocumentCacheSelfTest(not_null<Runner*> runner) {
	struct Reads {
		std::optional<QByteArray> subjectSmall;
		std::optional<QByteArray> subjectBig;
		std::optional<QByteArray> controlSmall;
		std::optional<QByteArray> controlBig;

		[[nodiscard]] bool complete() const {
			return subjectSmall && subjectBig && controlSmall && controlBig;
		}
	};
	struct State {
		base::weak_ptr<Main::Session> weak;
		bool sessionLost = false;
		Key subjectKey;
		Key controlKey;
		QByteArray subjectBytes;
		QByteArray controlBytes;
		int putsAnswered = 0;
		std::array<Error, 4> putErrors;
		Reads before;
		Reads after;
		std::shared_ptr<const WebCacheRemoval> removal;
		bool finishedInIssuingTurn = false;
		std::shared_ptr<const WebCacheRemoval> teardownSubject;
		std::shared_ptr<const WebCacheRemoval> teardownControl;
	};
	// Kept alive by the stages that capture it; every database callback
	// also holds it until its main hop has run or been dropped.
	const auto state = std::make_shared<State>();

	const auto sessionLostText = [=] {
		return state->sessionLost ? u" session=gone"_q : QString();
	};

	// Issues the four gets into |reads|, each answered on its database's
	// queue thread and written on main only while the session lives.
	const auto issueReads = [=](Reads State::*member) {
		const auto session = state->weak.get();
		if (!session) {
			state->sessionLost = true;
			return;
		}
		state.get()->*member = Reads();
		const auto weak = state->weak;
		const auto read = [=](
				Storage::Cache::Database &database,
				const Key &key,
				std::optional<QByteArray> Reads::*slot) {
			database.get(key, [=](QByteArray &&value) {
				crl::on_main(weak, [=, bytes = std::move(value)] {
					(state.get()->*member).*slot = bytes;
				});
			});
		};
		auto &small = session->data().cache();
		auto &big = session->data().cacheBigFile();
		read(small, state->subjectKey, &Reads::subjectSmall);
		read(big, state->subjectKey, &Reads::subjectBig);
		read(small, state->controlKey, &Reads::controlSmall);
		read(big, state->controlKey, &Reads::controlBig);
	};
	const auto readsDetails = [=](Reads State::*member) {
		const auto &reads = state.get()->*member;
		return u"answered=%1/4%2"_q.arg(
			QString::number(int(reads.subjectSmall.has_value())
				+ int(reads.subjectBig.has_value())
				+ int(reads.controlSmall.has_value())
				+ int(reads.controlBig.has_value())),
			sessionLostText());
	};

	runner->waitForSessionReady();

	runner->add({
		.name = u"web cache self-test: the cache key derives from the URL "
			"alone"_q,
		.then = [=] {
			const auto plain = UrlKey(kSubjectUrl);
			const auto hashed = UrlKey(kSubjectUrl, kOtherAccessHash);
			const auto control = UrlKey(kControlUrl);
			Check(
				(plain == hashed) && (plain != control),
				u"web cache self-test: the cache key is derived from the URL "
				"alone, not the access hash"_q,
				u"subject=%1 subjectWithHash=%2 control=%3"_q.arg(
					KeyText(plain),
					KeyText(hashed),
					KeyText(control)));
		},
	});

	runner->add({
		.name = u"web cache self-test: put synthetic bytes under both keys "
			"in both caches"_q,
		.run = [=] {
			const auto session = &Core::App().domain().active().session();
			state->weak = base::make_weak(session);
			state->subjectKey = UrlKey(kSubjectUrl);
			state->controlKey = UrlKey(kControlUrl);

			// A fresh nonce per run: a leftover entry from an aborted earlier
			// run cannot satisfy the positive control, and put overwrites it.
			const auto nonce = QString::number(
				base::RandomValue<uint64>(),
				16).toLatin1();
			state->subjectBytes = "web-cache-selftest subject nonce=" + nonce;
			state->controlBytes = "web-cache-selftest control nonce=" + nonce;

			const auto weak = state->weak;
			const auto put = [=](
					Storage::Cache::Database &database,
					const Key &key,
					const QByteArray &bytes,
					int index) {
				database.put(key, QByteArray(bytes), [=](Error error) {
					crl::on_main(weak, [=] {
						state->putErrors[index] = error;
						++state->putsAnswered;
					});
				});
			};
			auto &small = session->data().cache();
			auto &big = session->data().cacheBigFile();
			put(small, state->subjectKey, state->subjectBytes, 0);
			put(big, state->subjectKey, state->subjectBytes, 1);
			put(small, state->controlKey, state->controlBytes, 2);
			put(big, state->controlKey, state->controlBytes, 3);
		},
		.until = [=] {
			return (state->putsAnswered == 4);
		},
		.then = [=] {
			auto ok = true;
			for (const auto &error : state->putErrors) {
				ok = ok && (error.type == Error::Type::None);
			}
			Check(
				ok,
				u"web cache self-test: synthetic bytes were stored under the "
				"subject and control keys in both caches"_q,
				u"subjectKey=%1 controlKey=%2 subjectSmall=%3 subjectBig=%4 "
				"controlSmall=%5 controlBig=%6"_q.arg(
					KeyText(state->subjectKey),
					KeyText(state->controlKey),
					ErrorText(state->putErrors[0]),
					ErrorText(state->putErrors[1]),
					ErrorText(state->putErrors[2]),
					ErrorText(state->putErrors[3])));
		},
		.timeoutDetails = [=] {
			return u"answered=%1/4 session=%2"_q.arg(
				QString::number(state->putsAnswered),
				state->weak ? u"alive"_q : u"gone"_q);
		},
	});

	runner->add({
		.name = u"web cache self-test: positive control before the "
			"removal"_q,
		.run = [=] {
			issueReads(&State::before);
		},
		.until = [=] {
			return state->sessionLost || state->before.complete();
		},
		.then = [=] {
			const auto &reads = state->before;
			Check(
				reads.subjectSmall
					&& (*reads.subjectSmall == state->subjectBytes),
				u"web cache self-test: positive control: the subject's bytes "
				"read back from the small-file cache"_q,
				ReadText(reads.subjectSmall, state->subjectBytes)
					+ sessionLostText());
			Check(
				reads.subjectBig && (*reads.subjectBig == state->subjectBytes),
				u"web cache self-test: positive control: the subject's bytes "
				"read back from the big-file cache"_q,
				ReadText(reads.subjectBig, state->subjectBytes)
					+ sessionLostText());
			Check(
				reads.controlSmall
					&& (*reads.controlSmall == state->controlBytes)
					&& reads.controlBig
					&& (*reads.controlBig == state->controlBytes),
				u"web cache self-test: the unrelated key's bytes read back "
				"from both caches before the removal"_q,
				u"small: %1; big: %2%3"_q.arg(
					ReadText(reads.controlSmall, state->controlBytes),
					ReadText(reads.controlBig, state->controlBytes),
					sessionLostText()));
		},
		.timeoutDetails = [=] {
			return readsDetails(&State::before);
		},
	});

	runner->add({
		.name = u"web cache self-test: remove the subject through the "
			"helper"_q,
		.run = [=] {
			const auto session = state->weak.get();
			if (!session) {
				state->sessionLost = true;
				return;
			}
			state->removal = RemoveWebDocumentCache(session, kSubjectUrl);
			state->finishedInIssuingTurn = state->removal->finished();
		},
		.until = [=] {
			return state->sessionLost || state->removal->finished();
		},
		.then = [=] {
			const auto removal = state->removal;
			Check(
				removal
					&& removal->ok()
					&& (removal->key == state->subjectKey)
					&& !state->finishedInIssuingTurn,
				u"web cache self-test: the helper reported completion after "
				"both caches answered, with no error"_q,
				u"%1 finishedInIssuingTurn=%2%3"_q.arg(
					removal ? WebCacheRemovalText(*removal) : u"none"_q,
					state->finishedInIssuingTurn ? u"1"_q : u"0"_q,
					sessionLostText()));
		},
		.timeoutDetails = [=] {
			return state->removal
				? WebCacheRemovalText(*state->removal)
				: u"none"_q;
		},
	});

	runner->add({
		.name = u"web cache self-test: read back after the removal"_q,
		.run = [=] {
			issueReads(&State::after);
		},
		.until = [=] {
			return state->sessionLost || state->after.complete();
		},
		.then = [=] {
			const auto &reads = state->after;
			Check(
				reads.subjectSmall && reads.subjectSmall->isEmpty(),
				u"web cache self-test: after completion the subject's key "
				"reads empty from the small-file cache"_q,
				ReadText(reads.subjectSmall, QByteArray()) + sessionLostText());
			Check(
				reads.subjectBig && reads.subjectBig->isEmpty(),
				u"web cache self-test: after completion the subject's key "
				"reads empty from the big-file cache"_q,
				ReadText(reads.subjectBig, QByteArray()) + sessionLostText());
			Check(
				reads.controlSmall
					&& (*reads.controlSmall == state->controlBytes)
					&& reads.controlBig
					&& (*reads.controlBig == state->controlBytes),
				u"web cache self-test: the unrelated key written beside it "
				"still returns its bytes from both caches"_q,
				u"small: %1; big: %2%3"_q.arg(
					ReadText(reads.controlSmall, state->controlBytes),
					ReadText(reads.controlBig, state->controlBytes),
					sessionLostText()));
		},
		.timeoutDetails = [=] {
			return readsDetails(&State::after);
		},
	});

	runner->add({
		.name = u"web cache self-test: teardown"_q,
		.run = [=] {
			const auto session = state->weak.get();
			if (!session) {
				state->sessionLost = true;
				return;
			}
			state->teardownSubject = RemoveWebDocumentCache(
				session,
				kSubjectUrl);
			state->teardownControl = RemoveWebDocumentCache(
				session,
				kControlUrl);
		},
		.until = [=] {
			return state->sessionLost
				|| (state->teardownSubject->finished()
					&& state->teardownControl->finished());
		},
		.then = [=] {
			const auto subject = state->teardownSubject;
			const auto control = state->teardownControl;
			Check(
				subject && control && subject->ok() && control->ok(),
				u"web cache self-test: teardown: removing an absent key and "
				"a present key both complete without error"_q,
				u"absent: %1; present: %2%3"_q.arg(
					subject ? WebCacheRemovalText(*subject) : u"none"_q,
					control ? WebCacheRemovalText(*control) : u"none"_q,
					sessionLostText()));
		},
		.timeoutDetails = [=] {
			return u"absent: %1; present: %2"_q.arg(
				(state->teardownSubject
					? WebCacheRemovalText(*state->teardownSubject)
					: u"none"_q),
				(state->teardownControl
					? WebCacheRemovalText(*state->teardownControl)
					: u"none"_q));
		},
	});
}

} // namespace Test

#endif // _DEBUG
