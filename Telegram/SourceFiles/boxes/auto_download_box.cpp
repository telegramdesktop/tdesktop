/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_download_box.h"

#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_auto_download.h"
#include "ui/vertical_list.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/wrap.h"
#include "storage/localstorage.h"
#include "settings/settings_common.h"
#include "export/view/export_view_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kMegabyte = 1024 * 1024;
constexpr auto kDefaultDownloadLimit = 10 * kMegabyte;
constexpr auto kDefaultAutoPlayLimit = 50 * kMegabyte;

using Type = Data::AutoDownload::Type;
using Source = Data::AutoDownload::Source;
using Override = Data::AutoDownload::Override;

[[nodiscard]] bool PeerMatchesSource(
		not_null<PeerData*> peer,
		Source source) {
	if (peer->isSelf() || peer->isServiceUser() || peer->isRepliesChat()) {
		return false;
	}
	switch (source) {
	case Source::User: return peer->isUser();
	case Source::Group: return peer->isChat() || peer->isMegagroup();
	case Source::Channel:
		return peer->isChannel() && !peer->isMegagroup();
	}
	Unexpected("Source in PeerMatchesSource.");
}

struct PeerBuckets {
	base::flat_set<PeerId> always;
	base::flat_set<PeerId> never;
};

[[nodiscard]] PeerBuckets BucketsForSource(
		const Data::AutoDownload::Full &settings,
		not_null<Data::Session*> owner,
		Source source) {
	auto result = PeerBuckets();
	settings.enumeratePeerOverrides([&](PeerId peerId, Override value) {
		const auto peer = owner->peer(peerId);
		if (!PeerMatchesSource(peer, source)) {
			return;
		} else if (value == Override::ForceAllow) {
			result.always.emplace(peerId);
		} else if (value == Override::ForceDeny) {
			result.never.emplace(peerId);
		}
	});
	return result;
}

class ExceptionsBoxController final : public ChatsListBoxController {
public:
	ExceptionsBoxController(
		not_null<Main::Session*> session,
		Source source,
		base::flat_set<PeerId> selected);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	const not_null<Main::Session*> _session;
	const Source _source;
	base::flat_set<PeerId> _selected;

};

ExceptionsBoxController::ExceptionsBoxController(
	not_null<Main::Session*> session,
	Source source,
	base::flat_set<PeerId> selected)
: ChatsListBoxController(session)
, _session(session)
, _source(source)
, _selected(std::move(selected)) {
}

Main::Session &ExceptionsBoxController::session() const {
	return *_session;
}

void ExceptionsBoxController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

void ExceptionsBoxController::prepareViewHook() {
	auto &owner = _session->data();
	auto peers = std::vector<not_null<PeerData*>>();
	peers.reserve(_selected.size());
	for (const auto &peerId : _selected) {
		peers.push_back(owner.peer(peerId));
	}
	delegate()->peerListAddSelectedPeers(peers);
}

auto ExceptionsBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto peer = history->peer;
	return PeerMatchesSource(peer, _source)
		? std::make_unique<Row>(history)
		: nullptr;
}

not_null<int64*> AddSizeLimitSlider(
		not_null<Ui::VerticalLayout*> container,
		const base::flat_map<Type, int64> &values,
		int64 defaultValue) {
	using Pair = base::flat_map<Type, int64>::value_type;

	const auto limits = Ui::CreateChild<rpl::event_stream<int64>>(
		container.get());
	const auto currentLimit = ranges::max_element(
		values,
		std::less<>(),
		[](Pair pair) { return pair.second; })->second;
	const auto startLimit = currentLimit ? currentLimit : defaultValue;
	const auto result = Ui::CreateChild<int64>(container.get(), startLimit);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_media_size_limit(),
		limits->events_starting_with_copy(
			startLimit
		) | rpl::map([](int64 value) {
			return tr::lng_media_size_up_to(
				tr::now,
				lt_size,
				QString::number(value / kMegabyte) + " MB");
		}),
		st::autoDownloadLimitButton
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::autoDownloadLimitSlider),
		st::autoDownloadLimitPadding);
	slider->resize(st::autoDownloadLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		Export::View::kSizeValueCount,
		Export::View::SizeLimitByIndex,
		*result,
		[=](int64 value) {
			*result = value;
			limits->fire_copy(value);
		});
	return result;
}
} // namespace

AutoDownloadBox::AutoDownloadBox(
	QWidget*,
	not_null<Main::Session*> session,
	Data::AutoDownload::Source source)
: _session(session)
, _source(source) {
}

void AutoDownloadBox::prepare() {
	setupContent();
}

void AutoDownloadBox::setupContent() {
	using namespace rpl::mappers;
	using namespace Settings;
	using namespace Data::AutoDownload;
	using Type = Data::AutoDownload::Type;
	using Pair = base::flat_map<Type, int64>::value_type;

	setTitle(tr::lng_profile_settings_section());

	const auto settings = &_session->settings().autoDownload();

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto add = [&](
			not_null<base::flat_map<Type, int64>*> values,
			Type type,
			rpl::producer<QString> label) {
		const auto value = settings->bytesLimit(_source, type);
		content->add(object_ptr<Ui::SettingsButton>(
			content,
			std::move(label),
			st::settingsButtonNoIcon
		))->toggleOn(
			rpl::single(value > 0)
		)->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			(*values)[type] = enabled ? 1 : 0;
		}, content->lifetime());
		values->emplace(type, value);
	};

	AddSubsectionTitle(content, tr::lng_media_auto_title());

	const auto downloadValues = Ui::CreateChild<base::flat_map<Type, int64>>(
		content);
	add(downloadValues, Type::Photo, tr::lng_media_photo_title());
	add(downloadValues, Type::File, tr::lng_media_file_title());

	const auto downloadLimit = AddSizeLimitSlider(
		content,
		*downloadValues,
		kDefaultDownloadLimit);

	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_media_auto_play());

	const auto autoPlayValues = Ui::CreateChild<base::flat_map<Type, int64>>(
		content);
	add(
		autoPlayValues,
		Type::AutoPlayVideoMessage,
		tr::lng_media_video_messages_title());
	add(autoPlayValues, Type::AutoPlayVideo, tr::lng_media_video_title());
	add(autoPlayValues, Type::AutoPlayGIF, tr::lng_media_animation_title());

	const auto autoPlayLimit = AddSizeLimitSlider(
		content,
		*autoPlayValues,
		kDefaultAutoPlayLimit);

	const auto limitByType = [=](Type type) {
		return (ranges::find(kAutoPlayTypes, type) != end(kAutoPlayTypes))
			? *autoPlayLimit
			: *downloadLimit;
	};

	const auto staged = content->lifetime().make_state<PeerBuckets>(
		BucketsForSource(*settings, &_session->data(), _source));

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_media_auto_exceptions());

	const auto countText = [source = _source](int count) {
		switch (source) {
		case Source::User:
			return tr::lng_media_auto_exceptions_users(
				tr::now,
				lt_count,
				count);
		case Source::Group:
			return tr::lng_media_auto_exceptions_groups(
				tr::now,
				lt_count,
				count);
		case Source::Channel:
			return tr::lng_media_auto_exceptions_channels(
				tr::now,
				lt_count,
				count);
		}
		Unexpected("Source in countText.");
	};
	const auto addText = [source = _source] {
		switch (source) {
		case Source::User:
			return tr::lng_media_auto_exceptions_add_users(tr::now);
		case Source::Group:
			return tr::lng_media_auto_exceptions_add_groups(tr::now);
		case Source::Channel:
			return tr::lng_media_auto_exceptions_add_channels(tr::now);
		}
		Unexpected("Source in addText.");
	};

	const auto updates = content->lifetime().make_state<rpl::event_stream<>>();
	const auto addExceptionButton = [&](
			Override kind,
			rpl::producer<QString> text,
			rpl::producer<QString> title) {
		auto label = updates->events_starting_with(
			{}
		) | rpl::map([=] {
			const auto count = int((kind == Override::ForceAllow)
				? staged->always.size()
				: staged->never.size());
			return count ? countText(count) : addText();
		});
		const auto button = AddButtonWithLabel(
			content,
			std::move(text),
			std::move(label),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=, title = std::move(title)]() mutable {
			const auto &selected = (kind == Override::ForceAllow)
				? staged->always
				: staged->never;
			auto controller = std::make_unique<ExceptionsBoxController>(
				_session,
				_source,
				selected);
			auto initBox = [=, this, title = std::move(title)](
					not_null<PeerListBox*> box) mutable {
				box->setTitle(std::move(title));
				box->addButton(tr::lng_settings_save(), crl::guard(this, [=] {
					auto &dest = (kind == Override::ForceAllow)
						? staged->always
						: staged->never;
					auto &other = (kind == Override::ForceAllow)
						? staged->never
						: staged->always;
					dest.clear();
					for (const auto &peer : box->collectSelectedRows()) {
						dest.emplace(peer->id);
						other.remove(peer->id);
					}
					updates->fire({});
					box->closeBox();
				}));
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			};
			getDelegate()->show(Box<PeerListBox>(
				std::move(controller),
				std::move(initBox)));
		});
	};
	addExceptionButton(
		Override::ForceAllow,
		tr::lng_media_auto_always(),
		tr::lng_media_auto_always_title());
	addExceptionButton(
		Override::ForceDeny,
		tr::lng_media_auto_never(),
		tr::lng_media_auto_never_title());

	addButton(tr::lng_connection_save(), [=] {
		auto &&values = ranges::views::concat(
			*downloadValues,
			*autoPlayValues);
		auto allowMore = values | ranges::views::filter([&](Pair pair) {
			const auto &[type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			const auto old = settings->bytesLimit(_source, type);
			return (old < value);
		}) | ranges::views::transform([](Pair pair) {
			return pair.first;
		});
		const auto less = ranges::any_of(*autoPlayValues, [&](Pair pair) {
			const auto &[type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			return value < settings->bytesLimit(_source, type);
		});
		const auto allowMoreTypes = base::flat_set<Type>(
			allowMore.begin(),
			allowMore.end());

		const auto changed = ranges::any_of(values, [&](Pair pair) {
			const auto &[type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			return value != settings->bytesLimit(_source, type);
		});

		const auto &kHidden = kStreamedTypes;
		const auto hiddenChanged = ranges::any_of(kHidden, [&](Type type) {
			const auto now = settings->bytesLimit(_source, type);
			return (now > 0) && (now != limitByType(type));
		});

		const auto current = BucketsForSource(
			*settings,
			&_session->data(),
			_source);
		const auto exceptionsChanged = (current.always != staged->always)
			|| (current.never != staged->never);
		const auto exceptionsAllowMore = ranges::any_of(
			staged->always,
			[&](PeerId id) { return !current.always.contains(id); }
		) || ranges::any_of(
			current.never,
			[&](PeerId id) { return !staged->never.contains(id); });
		const auto exceptionsAllowLess = ranges::any_of(
			staged->never,
			[&](PeerId id) { return !current.never.contains(id); }
		) || ranges::any_of(
			current.always,
			[&](PeerId id) { return !staged->always.contains(id); });

		if (changed) {
			for (const auto &[type, enabled] : values) {
				const auto value = enabled ? limitByType(type) : 0;
				settings->setBytesLimit(_source, type, value);
			}
		}
		if (hiddenChanged) {
			for (const auto type : kHidden) {
				const auto now = settings->bytesLimit(_source, type);
				if (now > 0) {
					settings->setBytesLimit(
						_source,
						type,
						limitByType(type));
				}
			}
		}
		if (exceptionsChanged) {
			for (const auto &peerId : current.always) {
				if (!staged->always.contains(peerId)) {
					settings->setPeerOverride(peerId, Override::Default);
				}
			}
			for (const auto &peerId : current.never) {
				if (!staged->never.contains(peerId)) {
					settings->setPeerOverride(peerId, Override::Default);
				}
			}
			for (const auto &peerId : staged->always) {
				settings->setPeerOverride(peerId, Override::ForceAllow);
			}
			for (const auto &peerId : staged->never) {
				settings->setPeerOverride(peerId, Override::ForceDeny);
			}
		}
		if (changed || hiddenChanged || exceptionsChanged) {
			_session->saveSettingsDelayed();
		}
		if (allowMoreTypes.contains(Type::Photo) || exceptionsAllowMore) {
			_session->data().photoLoadSettingsChanged();
		}
		if (ranges::any_of(allowMoreTypes, _1 != Type::Photo)
			|| exceptionsAllowMore) {
			_session->data().documentLoadSettingsChanged();
		}
		if (less || exceptionsAllowLess) {
			_session->data().checkPlayingAnimations();
		}
		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, content);
}
