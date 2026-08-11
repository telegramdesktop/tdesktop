/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_codes.h"

#include "ui/toast/toast.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "core/application.h"
#include "mtproto/web_proxy/web_proxy_transport.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/window_session_controller.h"
#include "media/audio/media_audio_track.h"
#include "settings/sections/settings_folders.h"
#include "storage/storage_account.h"
#include "wallet/wallet_session.h"
#include "gram/ton/gram_address.h"
#include "gram/api/gram_api_history.h"
#include "ui/controls/ton_common.h"
#include "api/api_updates.h"
#include "base/qt/qt_common_adapters.h"
#include "base/unixtime.h"
#include "base/custom_app_icon.h"
#include "base/options.h"
#include "boxes/abstract_box.h" // Ui::show().

#ifdef _DEBUG
#include "data/data_file_origin.h"
#include "gram/api/gram_api_nft.h"
#include "gram/api/gram_api_request.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_response.h"
#include "storage/file_download.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "ui/image/image_location.h"
#include "wallet/wallet_api.h"
#endif // _DEBUG

#include <zlib.h>

namespace Settings {
namespace {

using SessionController = Window::SessionController;

[[nodiscard]] QByteArray UnpackRawGzip(const QByteArray &bytes) {
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, -MAX_WBITS);
	if (res != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(1024 * 1024, char(0));
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return QByteArray();
		} else if (!stream.avail_out) {
			return QByteArray();
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

[[nodiscard]] std::vector<QString> TestWalletPhrase() {
	const auto phrase = u"hospital stove relief fringe tongue always "
		u"charge angry urge sentence again match nerve inquiry senior "
		u"coconut label tumble carry category beauty bean road solution"_q;
	auto result = std::vector<QString>();
	for (const auto &word : phrase.split(u' ')) {
		result.push_back(word);
	}
	return result;
}

#ifdef _DEBUG
const auto kAcc1Raw = u"0:9DA971AF38D2F03ABDF308D5F91636A97E5A2B07A66C39D71D7CBAE3B032EDDC"_q;

struct ToncenterProbe {
	QString name;
	QString endpoint;
	QString query;
};

[[nodiscard]] std::vector<ToncenterProbe> ToncenterProbes() {
	const auto owner2 = u"0:4E3664FAEE814FBDB6C1CB36D72BAC52D15E186C11ECD664FE1B3F1A7CFD801D"_q;
	const auto owner3 = u"0:8B704249E018FAA59BDC356F463A7A34FC0201DBBFA7441A387BE83E9FFFBFC4"_q;
	const auto gift = u"0:CA0CFD519F763102B5BEC9D9E3AF43592EA362FB773FA319EA09C4F162C171E0"_q;
	const auto numbers = u"EQAOQdwdw8kGftJCSFgOErM1mBjYPe4DBPq8-AhF6vr9si5N"_q;
	const auto usernames = u"EQCA14o1-VWhS2efqoh_9M1b_A9DtKTuoqfmkn83AbJzwnPi"_q;
	const auto jetton = u"0:B113A994B5024A16719F69139328EB759596C38A25F59028B146FECDC3621DFE"_q;
	const auto gifts = u"0:4C71F300665314AF55B75FC91D130DDF24C5006961F8F9772613947945F14863"_q;
	const auto encoded = [](const QString &address) {
		return Gram::ApiDetails::PercentEncoded(address);
	};
	const auto items = u"/api/v3/nft/items"_q;
	const auto transfers = u"/api/v3/nft/transfers"_q;
	const auto records = u"/api/v3/dns/records"_q;
	const auto traces = u"/api/v3/traces"_q;
	const auto transactions = u"/api/v3/transactions"_q;
	return {
		{
			u"api-nft-items-owner1"_q,
			items,
			u"owner_address="_q + encoded(kAcc1Raw),
		},
		{
			u"api-nft-items-owner2"_q,
			items,
			u"owner_address="_q + encoded(owner2),
		},
		{
			u"api-nft-items-owner1-paged"_q,
			items,
			u"owner_address="_q + encoded(kAcc1Raw) + u"&limit=50&offset=0"_q,
		},
		{
			u"api-nft-items-address-gift"_q,
			items,
			u"address="_q + encoded(gift),
		},
		{
			u"api-nft-items-collection-numbers"_q,
			items,
			u"collection_address="_q + encoded(numbers) + u"&limit=1"_q,
		},
		{
			u"api-nft-items-collection-usernames"_q,
			items,
			u"collection_address="_q + encoded(usernames) + u"&limit=1"_q,
		},
		{
			u"api-nft-items-camel"_q,
			u"/api/v3/nftItems"_q,
			u"owner_address="_q + encoded(kAcc1Raw),
		},
		{
			u"api-nft-transfers-owner1"_q,
			transfers,
			u"owner_address="_q + encoded(kAcc1Raw),
		},
		{
			u"api-nft-transfers-owner1-paged"_q,
			transfers,
			u"owner_address="_q
				+ encoded(kAcc1Raw)
				+ u"&direction=both&limit=50&offset=0"_q,
		},
		{
			u"api-nft-transfers-item-gift"_q,
			transfers,
			u"item_address="_q + encoded(gift),
		},
		{
			u"api-nft-transfers-camel"_q,
			u"/api/v3/nftTransfers"_q,
			u"owner_address="_q + encoded(kAcc1Raw),
		},
		{
			u"api-dns-records-domain-tolya"_q,
			records,
			u"domain=tolya.ton"_q,
		},
		{
			u"api-dns-records-wallet-owner1"_q,
			records,
			u"wallet="_q + encoded(kAcc1Raw),
		},
		{
			u"api-dns-records-domain-saint"_q,
			records,
			u"domain=saint.ton"_q,
		},
		{
			u"api-dns-records-domain-pumpanddump"_q,
			records,
			u"domain=pumpanddump.ton"_q,
		},
		{
			u"api-dns-records-wallet-owner3"_q,
			records,
			u"wallet="_q + encoded(owner3),
		},
		{
			u"api-dns-records-camel"_q,
			u"/api/v3/dnsRecords"_q,
			u"wallet="_q + encoded(kAcc1Raw),
		},
		{
			u"api-traces-owner1"_q,
			traces,
			u"account="_q + encoded(kAcc1Raw) + u"&limit=20&offset=0"_q,
		},
		{
			u"api-traces-owner2"_q,
			traces,
			u"account="_q + encoded(owner2) + u"&limit=20&offset=0"_q,
		},
		{
			u"api-transactions-owner1"_q,
			transactions,
			u"account="_q + encoded(kAcc1Raw) + u"&limit=20&offset=0"_q,
		},
		{
			u"api-nft-items-owner1-limit5"_q,
			items,
			u"owner_address="_q + encoded(kAcc1Raw) + u"&limit=5"_q,
		},
		{
			u"api-nft-items-owner1-metadata-flag"_q,
			items,
			u"owner_address="_q
				+ encoded(kAcc1Raw)
				+ u"&limit=5&include_metadata=true"_q,
		},
		{
			u"api-jetton-wallets-owner1"_q,
			u"/api/v3/jetton/wallets"_q,
			u"owner_address="_q
				+ encoded(kAcc1Raw)
				+ u"&limit=20&offset=0"_q,
		},
		{
			u"api-jetton-masters-usdt"_q,
			u"/api/v3/jetton/masters"_q,
			u"address="_q + encoded(jetton) + u"&limit=1&offset=0"_q,
		},
		{
			u"api-nft-collections-gifts"_q,
			u"/api/v3/nft/collections"_q,
			u"collection_address="_q + encoded(gifts),
		},
		{
			u"api-metadata-address-gift"_q,
			u"/api/v3/metadata"_q,
			u"address="_q + encoded(gift),
		},
		{
			u"api-nft-transfers-owner1-in"_q,
			transfers,
			u"owner_address="_q + encoded(kAcc1Raw) + u"&direction=in"_q,
		},
		{
			u"api-transactions-latest"_q,
			transactions,
			u"limit=1&offset=0"_q,
		},
		{
			u"api-masterchain-info"_q,
			u"/api/v3/masterchainInfo"_q,
			QString(),
		},
		{
			u"api-nft-transfers-owner2"_q,
			transfers,
			u"owner_address="_q + encoded(owner2),
		},
		{
			u"api-traces-item-gift"_q,
			traces,
			u"account="_q + encoded(gift) + u"&limit=20&offset=0"_q,
		},
		{
			u"api-transactions-item-gift"_q,
			transactions,
			u"account="_q + encoded(gift) + u"&limit=20&offset=0"_q,
		},
		{
			u"api-traces-owner1-limit5"_q,
			traces,
			u"account="_q + encoded(kAcc1Raw) + u"&limit=5&offset=0"_q,
		},
	};
}

[[nodiscard]] bool WriteProbeBody(
		const QString &fileName,
		const QByteArray &bytes) {
	auto file = QFile(Test::EvidenceDir() + fileName);
	if (!file.open(QIODevice::WriteOnly)) {
		Test::Note(u"PROBE_WRITE_FAILED %1"_q.arg(fileName));
		return false;
	}
	return (file.write(bytes) == bytes.size());
}

void RunToncenterProbes(not_null<SessionController*> window) {
	struct State {
		int done = 0;
		int total = 0;
		int written = 0;
	};
	const auto state = std::make_shared<State>();
	const auto probes = ToncenterProbes();
	state->total = int(probes.size());
	const auto session = &window->session();
	const auto wallet = &session->wallet();
	const auto webFileDcId = session->serverConfig().webFileDcId;
	Test::Note(u"PROBE_START count=%1 dir=%2"_q
		.arg(state->total)
		.arg(Test::EvidenceDir()));
	Test::Note(u"PROBE_CONFIG env=%1 webFileDcId=%2 shifted=%3"_q
		.arg(session->mtp().environment() == MTP::Environment::Test
			? u"Test"_q
			: u"Production"_q)
		.arg(webFileDcId)
		.arg(MTP::ShiftDcId(webFileDcId, MTP::kToncenterDcShift)));
	for (const auto &probe : probes) {
		const auto name = probe.name;
		const auto endpoint = probe.endpoint;
		const auto query = probe.query;
		const auto record = [=](
				const QString &suffix,
				const QByteArray &bytes,
				bool body) {
			const auto saved = WriteProbeBody(name + suffix, bytes);
			if (saved && body) {
				++state->written;
			}
			Test::Note(u"PROBE %1 endpoint=%2 query=%3 bytes=%4"_q.arg(
				name,
				endpoint,
				query,
				QString::number(bytes.size())));
			if (++state->done < state->total) {
				return;
			}
			Test::Note(u"PROBE_COMPLETE bodies=%1/%2"_q
				.arg(state->written)
				.arg(state->total));
			Test::Fire(u"toncenter_probe_complete"_q);
			Ui::Toast::Show(u"Probe: %1/%2 bodies in %3"_q
				.arg(state->written)
				.arg(state->total)
				.arg(Test::EvidenceDir()));
		};
		wallet->debugRawRequest(
			Gram::HttpRequest{
				.endpoint = endpoint,
				.query = query,
			},
			[=](const QByteArray &bytes) {
				record(u".json"_q, bytes, true);
			},
			[=](const Gram::ApiError &error) {
				const auto text = u"code=%1 type=%2"_q
					.arg(error.code)
					.arg(error.message);
				record(u".mtp-error.txt"_q, text.toUtf8(), false);
			});
	}
}

struct FragmentProbe {
	QString name;
	QString extension;
	QString url;
};

[[nodiscard]] std::vector<FragmentProbe> FragmentProbes() {
	const auto gift = u"https://nft.fragment.com/gift/deskcalendar-45754"_q;
	return {
		{
			u"fragment-gift-deskcalendar-45754"_q,
			u".json"_q,
			gift + u".json"_q,
		},
		{
			u"fragment-collection-deskcalendar"_q,
			u".json"_q,
			u"https://nft.fragment.com/collection/deskcalendar.json"_q,
		},
		{
			u"fragment-gift-deskcalendar-45754-image"_q,
			u".webp"_q,
			gift + u".webp"_q,
		},
		{
			u"fragment-imgproxy-deskcalendar-small"_q,
			u".bin"_q,
			u"https://imgproxy.toncenter.com/"
				u"CLisjZYndcPa0HF7lUeL2LkBthE4L7OJ04Gp8GEDknE/pr:small/"
				u"aHR0cHM6Ly9uZnQuZnJhZ21lbnQuY29tL2dpZnQvZGVza2NhbGVuZGFyLTQ1NzU0LndlYnA"_q,
		},
		{
			u"fragment-missing-control"_q,
			u".json"_q,
			gift + u"-notanitem.json"_q,
		},
	};
}

[[nodiscard]] QString FailureDetail(FileLoader::Error error) {
	using Reason = FileLoader::FailureReason;
	const auto reason = (error.failureReason == Reason::FileWriteFailure)
		? u"file-write-failure"_q
		: (error.failureReason == Reason::OtherFailure)
		? u"other-failure"_q
		: u"no-failure"_q;
	return reason + (error.started ? u" started"_q : u" not-started"_q);
}

void RunFragmentProbes(not_null<SessionController*> window) {
	struct Entry {
		crl::time started = 0;
		int progress = 0;
		std::unique_ptr<FileLoader> loader;
	};
	struct State {
		int done = 0;
		int total = 0;
		int written = 0;
		std::vector<Entry> entries;
	};
	const auto session = &window->session();
	const auto state = session->lifetime().make_state<State>();
	const auto probes = FragmentProbes();
	state->total = int(probes.size());
	state->entries.resize(state->total);
	Test::Note(u"PROBE_LOAD_START count=%1 dir=%2"_q
		.arg(state->total)
		.arg(Test::EvidenceDir()));
	for (auto i = 0; i != state->total; ++i) {
		const auto name = probes[i].name;
		const auto extension = probes[i].extension;
		const auto url = probes[i].url;
		const auto entry = &state->entries[i];
		const auto record = [=](
				const QString &outcome,
				const QString &detail,
				const QByteArray &bytes) {
			const auto saved = !bytes.isEmpty()
				&& WriteProbeBody(name + extension, bytes);
			if (saved) {
				++state->written;
			}
			const auto text = u"name=%1\nurl=%2\nmechanism=%3\noutcome=%4\n"
				u"detail=%5\nbytes=%6\nprogressUpdates=%7\nelapsedMs=%8\n"
				u"bodyFile=%9\n"_q.arg(
					name,
					url,
					u"DownloadLocation{PlainUrlLocation} -> CreateFileLoader"
						u" -> webFileLoader"_q,
					outcome,
					detail,
					QString::number(bytes.size()),
					QString::number(entry->progress),
					QString::number(crl::now() - entry->started),
					saved ? (name + extension) : QString());
			const auto recorded = WriteProbeBody(
				name + u".load.txt"_q,
				text.toUtf8());
			Test::Note(u"PROBE_LOAD %1 outcome=%2 bytes=%3 recorded=%4"_q.arg(
				name,
				outcome,
				QString::number(bytes.size()),
				recorded ? u"1"_q : u"0"_q));
			if (++state->done < state->total) {
				return;
			}
			Test::Note(u"PROBE_LOAD_COMPLETE bodies=%1/%2"_q
				.arg(state->written)
				.arg(state->total));
			Test::Fire(u"fragment_probe_complete"_q);
			Ui::Toast::Show(u"Fragment: %1/%2 bodies in %3"_q
				.arg(state->written)
				.arg(state->total)
				.arg(Test::EvidenceDir()));
		};
		entry->started = crl::now();
		entry->loader = CreateFileLoader(
			session,
			DownloadLocation{ PlainUrlLocation{ url } },
			Data::FileOrigin(),
			QString(),
			0,
			0,
			UnknownFileLocation,
			LoadToCacheAsWell,
			LoadFromCloudOrLocal,
			false,
			0);
		const auto raw = entry->loader.get();
		raw->updates() | rpl::on_next_error_done([=] {
			++entry->progress;
		}, [=](FileLoader::Error error) {
			record(u"failed"_q, FailureDetail(error), QByteArray());
		}, [=] {
			const auto cancelled = raw->cancelled();
			record(
				cancelled ? u"cancelled"_q : u"loaded"_q,
				cancelled ? u"destroyed-in-flight"_q : QString(),
				raw->bytes());
		}, raw->lifetime());
		raw->start();
	}
}

constexpr auto kWalletBurstCount = 24;
constexpr auto kWalletBurstSlowFloor = crl::time(2500);
constexpr auto kWalletStallReleaseMargin = 5 * crl::time(1000);
constexpr auto kWalletStallArmTimeout = 20 * crl::time(1000);

void RunWalletStall(not_null<SessionController*> window) {
	const auto session = &window->session();
	const auto wallet = &session->wallet();
	if (wallet->keyState() == Wallet::KeyState::None) {
		Ui::Toast::Show(u"No wallet."_q);
		return;
	}
	struct State {
		base::Timer release;
		base::Timer netreset;
		crl::time armed = 0;
		crl::time swallowed = 0;
		int completions = 0;
		int round = 0;
		bool failed = false;
	};
	const auto state = session->lifetime().make_state<State>();
	const auto bound = Wallet::Api::DebugRequestTimeout();
	const auto items = [=] {
		return int(wallet->history().size());
	};
	const auto elapsed = [=] {
		const auto from = state->swallowed ? state->swallowed : state->armed;
		return crl::now() - from;
	};
	const auto onSwallowed = [=] {
		state->swallowed = crl::now();
		state->completions = 0;
		Test::Note(u"WALLET_STALL_SWALLOWED round=%1 armElapsedMs=%2"
			u" pending=%3"_q
			.arg(state->round)
			.arg(state->swallowed - state->armed)
			.arg(wallet->debugPendingCount()));
		wallet->refreshHistory([=] {
			Test::Note(u"WALLET_STALL_HISTORY_DONE round=%1 n=%2"
				u" elapsedMs=%3 items=%4 pending=%5"_q
				.arg(state->round)
				.arg(++state->completions)
				.arg(elapsed())
				.arg(items())
				.arg(wallet->debugPendingCount()));
		});
		if (state->round == 2) {
			const auto now = state->swallowed;
			const auto at = state->armed + bound / 2;
			state->netreset.callOnce((at > now) ? (at - now) : crl::time(0));
		}
		state->release.callOnce(bound + kWalletStallReleaseMargin);
	};
	const auto startRound = [=](int round) {
		state->round = round;
		state->armed = crl::now();
		state->swallowed = 0;
		Test::Note(u"WALLET_STALL_ARM round=%1 boundMs=%2 pending=%3"_q
			.arg(round)
			.arg(bound)
			.arg(wallet->debugPendingCount()));
		wallet->debugStallNextRequest(
			Gram::TracesRequest(QString(), 0, 0).endpoint,
			onSwallowed);
		if (round == 1) {
			wallet->startPolling();
		}
		wallet->refreshHistory();
		state->release.callOnce(kWalletStallArmTimeout);
	};
	const auto finish = [=] {
		wallet->debugStallNextRequest(QString(), nullptr);
		wallet->debugRestoreNetworkState();
		wallet->stopPolling();
		if (state->failed) {
			Test::Note(u"WALLET_STALL_INCOMPLETE"_q);
			Ui::Toast::Show(u"Wallet stall: incomplete, see the test log."_q);
			return;
		}
		Test::Fire(u"wallet_stall_complete"_q);
		Ui::Toast::Show(u"Wallet stall: done, see the test log."_q);
	};
	state->netreset.setCallback([=] {
		Test::Note(u"WALLET_STALL_NETRESET pending=%1"_q
			.arg(wallet->debugPendingCount()));
		wallet->debugClearNetworkState();
	});
	state->release.setCallback([=] {
		const auto round = state->round;
		if (!state->swallowed) {
			state->failed = true;
			Test::Note(u"WALLET_STALL_NO_SWALLOW round=%1 waitedMs=%2"_q
				.arg(round)
				.arg(elapsed()));
			finish();
			return;
		}
		const auto pendingBefore = wallet->debugPendingCount();
		Test::Note(u"WALLET_STALL_RELEASE round=%1 elapsedMs=%2 items=%3"
			u" pending=%4"_q
			.arg(round)
			.arg(elapsed())
			.arg(items())
			.arg(pendingBefore));
		wallet->debugReleaseStalledAnswer();
		const auto pendingAfter = wallet->debugPendingCount();
		Test::Note(u"WALLET_STALL_AFTER_RELEASE round=%1 items=%2 pending=%3"_q
			.arg(round)
			.arg(items())
			.arg(pendingAfter));
		const auto recovered = (pendingBefore == pendingAfter)
			&& ((round > 1) || (state->completions > 0));
		if (!recovered) {
			state->failed = true;
			Test::Note(u"WALLET_STALL_NO_RECOVERY round=%1 completions=%2"_q
				.arg(round)
				.arg(state->completions));
		}
		if (round < 2) {
			startRound(2);
			return;
		}
		finish();
	});
	startRound(1);
}

void RunWalletBurst(not_null<SessionController*> window) {
	const auto session = &window->session();
	const auto wallet = &session->wallet();
	if (wallet->keyState() == Wallet::KeyState::None) {
		Ui::Toast::Show(u"No wallet."_q);
		return;
	}
	struct State {
		crl::time started = 0;
		int done = 0;
		int failed = 0;
		int flood = 0;
		int slow = 0;
	};
	const auto state = session->lifetime().make_state<State>();
	state->started = crl::now();
	Test::Note(u"WALLET_BURST_START count=%1"_q.arg(kWalletBurstCount));
	for (auto i = 0; i != kWalletBurstCount; ++i) {
		const auto index = i;
		const auto started = crl::now();
		const auto record = [=](
				const QString &outcome,
				const QString &detail,
				int bytes) {
			const auto elapsed = crl::now() - started;
			if (elapsed >= kWalletBurstSlowFloor) {
				++state->slow;
			}
			Test::Note(u"WALLET_BURST index=%1 outcome=%2 detail=%3"
				u" bytes=%4 elapsedMs=%5"_q
				.arg(index)
				.arg(outcome)
				.arg(detail)
				.arg(bytes)
				.arg(elapsed));
			if (++state->done < kWalletBurstCount) {
				return;
			}
			Test::Note(u"WALLET_BURST_COMPLETE count=%1 failed=%2 flood=%3"
				u" slow=%4 elapsedMs=%5 pending=%6"_q
				.arg(kWalletBurstCount)
				.arg(state->failed)
				.arg(state->flood)
				.arg(state->slow)
				.arg(crl::now() - state->started)
				.arg(wallet->debugPendingCount()));
			Test::Fire(u"wallet_burst_complete"_q);
			Ui::Toast::Show(u"Burst: %1 sent, %2 failed, %3 flood."_q
				.arg(kWalletBurstCount)
				.arg(state->failed)
				.arg(state->flood));
		};
		wallet->debugProductRequest(
			Gram::HttpRequest{ .endpoint = u"/api/v3/masterchainInfo"_q },
			[=](const QByteArray &bytes) {
				record(u"done"_q, QString(), int(bytes.size()));
			},
			[=](const Gram::ApiError &error) {
				++state->failed;
				if (MTP::IsFloodError(error.message)) {
					++state->flood;
				}
				const auto detail = u"code=%1 type=%2"_q
					.arg(error.code)
					.arg(error.message);
				record(u"failed"_q, detail, 0);
			});
	}
}
#endif // _DEBUG

auto GenerateCodes() {
	auto codes = std::map<QString, Fn<void(SessionController*)>>();
	codes.emplace(u"debugmode"_q, [](SessionController *window) {
		QString text = Logs::DebugEnabled()
			? u"Do you want to disable DEBUG logs?"_q
			: u"Do you want to enable DEBUG logs?\n\nAll network events will be logged."_q;
		Ui::show(Ui::MakeConfirmBox({ text, [] {
			Core::App().switchDebugMode();
		} }));
	});
	codes.emplace(u"viewlogs"_q, [](SessionController *window) {
		File::ShowInFolder(cWorkingDir() + "log.txt");
	});
	if (!Core::UpdaterDisabled()) {
		codes.emplace(u"testupdate"_q, [](SessionController *window) {
			Core::UpdateChecker().test();
		});
	}
	codes.emplace(u"loadlang"_q, [](SessionController *window) {
		Lang::CurrentCloudManager().switchToLanguage({ u"#custom"_q });
	});
	codes.emplace(u"crashplease"_q, [](SessionController *window) {
		Unexpected("Crashed in Settings!");
	});
	codes.emplace(u"moderate"_q, [](SessionController *window) {
		auto text = Core::App().settings().moderateModeEnabled() ? u"Disable moderate mode?"_q : u"Enable moderate mode?"_q;
		Ui::show(Ui::MakeConfirmBox({ text, [=] {
			Core::App().settings().setModerateModeEnabled(!Core::App().settings().moderateModeEnabled());
			Core::App().saveSettingsDelayed();
			Ui::hideLayer();
		} }));
	});
	codes.emplace(u"getdifference"_q, [](SessionController *window) {
		if (window) {
			window->session().updates().getDifference();
		}
	});
	codes.emplace(u"walletcreate"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		if (wallet.keyState() != Wallet::KeyState::None) {
			Ui::Toast::Show(u"Wallet already exists."_q);
			return;
		}
		if (wallet.create()) {
			Ui::Toast::Show(
				u"Wallet created: %1"_q.arg(wallet.addressFriendly(false)));
		} else {
			Ui::Toast::Show(u"Wallet create failed."_q);
		}
	});
	codes.emplace(u"walletimport"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		if (wallet.keyState() != Wallet::KeyState::None) {
			Ui::Toast::Show(u"Wallet already exists, walletdelete first."_q);
			return;
		}
		if (!wallet.import(TestWalletPhrase())) {
			Ui::Toast::Show(u"Wallet import failed."_q);
			return;
		}
		const auto address = wallet.addressFriendly(true);
		const auto match = (address
			== u"EQDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw3Pg53kAk"_q);
		Ui::Toast::Show(u"Wallet imported: %1 (%2)"_q
			.arg(address)
			.arg(match ? u"PASS"_q : u"FAIL"_q));
	});
	codes.emplace(u"walletaddress"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		const auto address = wallet.address();
		if (!address) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		LOG(("Wallet: address UQ: %1, EQ: %2, raw: %3"
			).arg(wallet.addressFriendly(false)
			).arg(wallet.addressFriendly(true)
			).arg(Gram::FormatRaw(*address)));
		Ui::Toast::Show(u"Address: %1"_q.arg(wallet.addressFriendly(false)));
	});
	codes.emplace(u"walletbalance"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		window->session().wallet().refreshState([](
				const Gram::AccountState &state) {
			Ui::Toast::Show(u"Balance: %1 TON (status %2)"_q
				.arg(Ui::FormatTonAmount(state.balanceNano).full)
				.arg(int(state.status)));
		}, [](const Gram::ApiError &error) {
			Ui::Toast::Show(u"Balance error: %1"_q.arg(error.message));
		});
	});
	codes.emplace(u"wallethistory"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		const auto wallet = &window->session().wallet();
		wallet->refreshHistory([=] {
			const auto &list = wallet->history();
			Ui::Toast::Show(
				u"History: %1 items (see log)."_q.arg(list.size()));
			for (const auto &item : list) {
				LOG(("Wallet: %1 %2 TON fee %3 to %4 date %5 lt %6 status %7"
					).arg(item.incoming ? u"in"_q : u"out"_q
					).arg(Ui::FormatTonAmount(item.amountNano).full
					).arg(Ui::FormatTonAmount(item.feeNano).full
					).arg(Gram::FormatFriendly(item.counterparty, false)
					).arg(item.date
					).arg(item.lt
					).arg(int(item.status)));
				if (!item.comment.isEmpty()) {
					LOG(("Wallet:   comment: %1").arg(item.comment));
				}
			}
		});
	});
	codes.emplace(u"walletpoll"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		if (wallet.keyState() == Wallet::KeyState::None) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		if (wallet.pollingRequested()) {
			wallet.stopPolling();
		} else {
			wallet.startPolling();
		}
		Ui::Toast::Show(u"Wallet polling: %1"_q
			.arg(wallet.pollingRequested() ? u"on"_q : u"off"_q));
	});
	codes.emplace(u"walletsend"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		const auto address = wallet.address();
		if (!address) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		auto args = Wallet::SendArgs{
			.destination = *address,
			.amountNano = int64(10'000'000),
			.comment = u"tdesktop test"_q,
		};
		wallet.estimateFee(args, [](Wallet::FeeResult result) {
			Ui::Toast::Show(result.error.isEmpty()
				? u"Fee estimate: ~%1 TON"_q
					.arg(Ui::FormatTonAmount(result.feeNano).full)
				: u"Fee error: %1"_q.arg(result.error));
		});
		wallet.send(args, [](QString error) {
			Ui::Toast::Show(error.isEmpty()
				? u"Sent, pending confirmation."_q
				: error);
		});
	});
	codes.emplace(u"walletsendstale"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		const auto address = wallet.address();
		if (!address) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		auto args = Wallet::SendArgs{
			.destination = *address,
			.amountNano = int64(10'000'000),
			.comment = u"tdesktop test"_q,
			.simulateStaleSeqno = true,
		};
		wallet.send(std::move(args), [](QString error) {
			Ui::Toast::Show(error.isEmpty()
				? u"Sent, pending confirmation."_q
				: error);
		});
	});
	codes.emplace(u"walletviewed"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		const auto wallet = window->session().local().readWallet();
		if (!wallet) {
			Ui::Toast::Show(u"No wallet stored."_q);
			return;
		}
		window->session().wallet().markPhraseViewed();
		Ui::Toast::Show(u"Wallet phrase marked as viewed."_q);
	});
	codes.emplace(u"walletdelete"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		if (wallet.keyState() == Wallet::KeyState::None) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		wallet.remove();
		Ui::Toast::Show(u"Wallet deleted."_q);
	});
#ifdef _DEBUG
	codes.emplace(u"wallethistoryfixture"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		const auto weak = base::make_weak(window);
		FileDialog::GetOpenPath(
			Core::App().getFileDialogParent(),
			"Open traces fixture",
			"Traces JSON (*.json)",
			[weak](const FileDialog::OpenResult &result) {
				const auto strong = weak.get();
				if (!strong || result.paths.isEmpty()) {
					return;
				}
				auto file = QFile(result.paths.front());
				if (!file.open(QIODevice::ReadOnly)) {
					Ui::Toast::Show(u"Could not open fixture."_q);
					return;
				}
				const auto json = file.readAll();
				auto &wallet = strong->session().wallet();
				auto candidates = std::vector<Gram::Address>();
				const auto acc2 = u"0:BC1B748F5D26B74D857798FF4DD4252A2B79CF51B232AE41BE1F19E8CD9547B7"_q;
				for (const auto &raw : { kAcc1Raw, acc2 }) {
					if (const auto parsed = Gram::ParseAddress(raw)) {
						candidates.push_back(parsed->address);
					}
				}
				if (const auto own = wallet.address()) {
					candidates.push_back(*own);
				}
				auto items = std::vector<Gram::TransferItem>();
				for (const auto &self : candidates) {
					if (auto page = Gram::ParseTraces(json, self, 20)) {
						if (!page->list.empty()) {
							items = std::move(page->list);
							break;
						}
					}
				}
				if (items.empty()) {
					Ui::Toast::Show(u"No items parsed from fixture."_q);
					return;
				}
				auto demo = items.front();
				demo.date = base::unixtime::now();
				demo.lt = demo.lt + 1;
				demo.traceId = demo.traceId + "-demo-now";
				auto demoPending = items.front();
				demoPending.status = Gram::TransferItem::Status::Pending;
				demoPending.date = base::unixtime::now();
				demoPending.lt = demoPending.lt + 2;
				demoPending.traceId = demoPending.traceId + "-demo-pending";
				items.push_back(std::move(demo));
				items.push_back(std::move(demoPending));
				const auto count = int(items.size());
				wallet.injectDebugHistory(std::move(items));
				Ui::Toast::Show(u"Injected %1 history items."_q.arg(count));
			});
	});
	codes.emplace(u"walletcollectiblesfixture"_q, [](
			SessionController *window) {
		if (!window) {
			return;
		}
		const auto weak = base::make_weak(window);
		FileDialog::GetOpenPath(
			Core::App().getFileDialogParent(),
			"Open NFT items fixture",
			"NFT items JSON (*.json)",
			[weak](const FileDialog::OpenResult &result) {
				const auto strong = weak.get();
				if (!strong || result.paths.isEmpty()) {
					return;
				}
				auto file = QFile(result.paths.front());
				if (!file.open(QIODevice::ReadOnly)) {
					Ui::Toast::Show(u"Could not open fixture."_q);
					return;
				}
				auto page = Gram::ParseNftItems(file.readAll(), 0);
				if (!page) {
					Ui::Toast::Show(u"No items parsed from fixture."_q);
					return;
				}
				const auto count = int(page->list.size());
				strong->session().wallet().injectDebugCollectibles(
					std::move(page->list));
				Ui::Toast::Show(u"Injected %1 collectibles."_q.arg(count));
			});
	});
	codes.emplace(u"walletprobe"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		RunToncenterProbes(window);
		RunFragmentProbes(window);
	});
	codes.emplace(u"walletstall"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		RunWalletStall(window);
	});
	codes.emplace(u"walletburst"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		RunWalletBurst(window);
	});
	codes.emplace(u"walletagerefresh"_q, [](SessionController *window) {
		if (!window) {
			return;
		}
		auto &wallet = window->session().wallet();
		if (wallet.keyState() == Wallet::KeyState::None) {
			Ui::Toast::Show(u"No wallet."_q);
			return;
		}
		wallet.debugSetRefreshAges(61 * crl::time(1000));
		Ui::Toast::Show(u"Wallet refresh stamps aged to 61s."_q);
	});
#endif
	codes.emplace(u"loadcolors"_q, [](SessionController *window) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open palette file", "Palette (*.tdesktop-palette)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				Window::Theme::Apply(result.paths.front());
			}
		});
	});
	codes.emplace(u"endpoints"_q, [](SessionController *window) {
		if (!Core::App().domain().started()) {
			return;
		}
		const auto weak = window
			? base::make_weak(&window->session().account())
			: nullptr;
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open DC endpoints", "DC Endpoints (*.tdesktop-endpoints)", [weak](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				const auto loadFor = [&](not_null<Main::Account*> account) {
					if (!account->mtp().dcOptions().loadFromFile(result.paths.front())) {
						Ui::show(Ui::MakeInformBox("Could not load endpoints"
							" :( Errors in 'log.txt'."));
					}
				};
				if (const auto strong = weak.get()) {
					loadFor(strong);
				} else {
					for (const auto &pair : Core::App().domain().accounts()) {
						loadFor(pair.account.get());
					}
				}
			}
		});
	});
	codes.emplace(u"testmode"_q, [](SessionController *window) {
		auto &domain = Core::App().domain();
		if (domain.started()
			&& (domain.accounts().size() == 1)
			&& !domain.active().sessionExists()) {
			const auto environment = domain.active().mtp().environment();
			domain.addActivated([&] {
				return (environment == MTP::Environment::Production)
					? MTP::Environment::Test
					: MTP::Environment::Production;
			}());
			Ui::Toast::Show((environment == MTP::Environment::Production)
				? "Switched to the test environment."
				: "Switched to the production environment.");
		}
	});
	codes.emplace(u"folders"_q, [](SessionController *window) {
		if (window) {
			window->showSettings(Settings::FoldersId());
		}
	});
	codes.emplace(u"registertg"_q, [](SessionController *window) {
		Core::Application::RegisterUrlScheme();
		Ui::Toast::Show("Forced custom scheme register.");
	});
	codes.emplace(u"numberbuttons"_q, [](SessionController *window) {
		using namespace base::options;
		auto &option = lookup<bool>(kOptionFastButtonsMode);
		const auto now = !option.value();
		option.set(now);
		Ui::Toast::Show(now
			? u"Fast buttons mode enabled."_q
			: u"Fast buttons mode disabled."_q);
	});
	codes.emplace(u"externalweb"_q, [](SessionController *window) {
		const auto disabled = MTP::WebProxy::Transport::ToggleWebviewDisabled();
		Ui::Toast::Show(disabled
			? u"WebView transport blocked."_q
			: u"WebView transport unblocked."_q);
	});

	auto audioFilters = u"Audio files (*.wav *.mp3);;"_q + FileDialog::AllFilesFilter();
	auto audioKeys = {
		u"msg_incoming"_q,
		u"call_incoming"_q,
		u"call_outgoing"_q,
		u"call_busy"_q,
		u"call_connect"_q,
		u"call_end"_q,
	};
	for (auto &key : audioKeys) {
		codes.emplace(key, [=](SessionController *window) {
			FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open audio file", audioFilters, [=](const FileDialog::OpenResult &result) {
				if (!result.paths.isEmpty()) {
					auto track = Media::Audio::Current().createTrack();
					track->fillFromFile(result.paths.front());
					if (track->failed()) {
						Ui::show(Ui::MakeInformBox(
							"Could not audio :( Errors in 'log.txt'."));
					} else {
						Core::App().settings().setSoundOverride(
							key,
							result.paths.front());
						Core::App().saveSettingsDelayed();
					}
				}
			});
		});
	}
	codes.emplace(u"sounds_reset"_q, [](SessionController *window) {
		Core::App().settings().clearSoundOverrides();
		Core::App().saveSettingsDelayed();
		Ui::show(Ui::MakeInformBox("All sound overrides were reset."));
	});
	codes.emplace(u"unpacklog"_q, [](SessionController *window) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open crash log file", "Crash dump (*.txt)", [=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto f = QFile(result.paths.front());
			if (!f.open(QIODevice::ReadOnly)) {
				Ui::Toast::Show("Could not open log :(");
				return;
			}
			const auto all = f.readAll();
			const auto log = all.indexOf("Log: ");
			if (log < 0) {
				Ui::Toast::Show("Could not find log :(");
				return;
			}
			const auto base = all.mid(log + 5);
			const auto end = base.indexOf('\n');
			if (end <= 0) {
				Ui::Toast::Show("Could not find log end :(");
				return;
			}
			const auto based = QByteArray::fromBase64(base.mid(0, end));
			const auto uncompressed = UnpackRawGzip(based);
			if (uncompressed.isEmpty()) {
				Ui::Toast::Show("Could not unpack log :(");
				return;
			}
			FileDialog::GetWritePath(Core::App().getFileDialogParent(), "Save detailed log", "Crash dump (*.txt)", QString(), [=](QString &&result) {
				if (result.isEmpty()) {
					return;
				}
				auto f = QFile(result);
				if (!f.open(QIODevice::WriteOnly)) {
					Ui::Toast::Show("Could not open details :(");
				} else if (f.write(uncompressed) != uncompressed.size()) {
					Ui::Toast::Show("Could not write details :(");
				} else {
					f.close();
					Ui::Toast::Show("Done!");
				}
			});
		});
	});
	codes.emplace(u"testchatcolors"_q, [](SessionController *window) {
		const auto now = !Data::CloudThemes::TestingColors();
		Data::CloudThemes::SetTestingColors(now);
		Ui::Toast::Show(now ? "Testing chat theme colors!" : "Not testing..");
	});

#ifdef Q_OS_MAC
	codes.emplace(u"customicon"_q, [](SessionController *window) {
		const auto iconFilters = u"Icon files (*.icns *.png);;"_q + FileDialog::AllFilesFilter();
		const auto change = [](const QString &path) {
			const auto success = path.isEmpty()
				? base::ClearCustomAppIcon()
				: base::SetCustomAppIcon(path);
			Ui::Toast::Show(success
				? (path.isEmpty()
					? "Icon cleared. Restarting the Dock."
					: "Icon updated. Restarting the Dock.")
				: (path.isEmpty()
					? "Icon clear failed. See log.txt for details."
					: "Icon update failed. See log.txt for details."));
		};
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Choose custom icon", iconFilters, [=](const FileDialog::OpenResult &result) {
			change(result.paths.isEmpty() ? QString() : result.paths.front());
		}, [=] {
			change(QString());
		});
	});
#endif // Q_OS_MAC

	return codes;
}

} // namespace

void CodesFeedString(SessionController *window, const QString &text) {
	static const auto codes = GenerateCodes();
	static auto secret = QString();

	secret += text.toLower();
	int size = secret.size(), from = 0;
	while (size > from) {
		auto piece = base::StringViewMid(secret,from);
		auto found = false;
		for (const auto &[key, method] : codes) {
			if (piece == key) {
				method(window);
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		found = ranges::any_of(codes, [&](const auto &pair) {
			return pair.first.startsWith(piece);
		});
		if (found) break;

		++from;
	}
	secret = (size > from) ? secret.mid(from) : QString();
}

} // namespace Settings
