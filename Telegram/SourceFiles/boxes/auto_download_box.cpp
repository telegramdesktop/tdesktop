/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_download_box.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
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
constexpr auto kDefaultLimit = 10 * kMegabyte;

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
	using namespace Settings;
	using namespace Data::AutoDownload;
	using namespace rpl::mappers;
	using Type = Data::AutoDownload::Type;

	setTitle(tr::lng_media_auto_title());

	const auto settings = &_session->settings().autoDownload();
	const auto checked = [=](Source source, Type type) {
		return (settings->bytesLimit(source, type) > 0);
	};

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	static const auto kHidden = {
		Type::Video,
		Type::Music,
		Type::VoiceMessage
	};

	const auto values = Ui::CreateChild<base::flat_map<Type, int>>(content);
	const auto add = [&](Type type, rpl::producer<QString> label) {
		if (ranges::find(kHidden, type) != end(kHidden)) {
			return;
		}
		const auto value = settings->bytesLimit(_source, type);
		AddButton(
			content,
			std::move(label),
			st::settingsButton
		)->toggleOn(
			rpl::single(value > 0)
		)->toggledChanges(
		) | rpl::start_with_next([=](bool enabled) {
			(*values)[type] = enabled ? 1 : 0;
		}, content->lifetime());
		values->emplace(type, value);
	};
	add(Type::Photo, tr::lng_media_photo_title());
	add(Type::VoiceMessage, tr::lng_media_audio_title());
	add(Type::VideoMessage, tr::lng_media_video_messages_title());
	add(Type::Video, tr::lng_media_video_title());
	add(Type::File, tr::lng_media_file_title());
	add(Type::Music, tr::lng_media_music_title());
	add(Type::GIF, tr::lng_media_animation_title());

	const auto limits = Ui::CreateChild<rpl::event_stream<int>>(content);
	using Pair = base::flat_map<Type, int>::value_type;
	const auto settingsLimit = ranges::max_element(
		*values,
		std::less<>(),
		[](Pair pair) { return pair.second; })->second;
	const auto initialLimit = settingsLimit ? settingsLimit : kDefaultLimit;
	const auto limit = Ui::CreateChild<int>(content, initialLimit);
	AddButtonWithLabel(
		content,
		tr::lng_media_size_limit(),
		limits->events_starting_with_copy(
			initialLimit
		) | rpl::map([](int value) {
			return tr::lng_media_size_up_to(
				tr::now,
				lt_size,
				QString::number(value / kMegabyte) + " MB");
		}),
		st::autoDownloadLimitButton
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto slider = content->add(
		object_ptr<Ui::MediaSlider>(content, st::autoDownloadLimitSlider),
		st::autoDownloadLimitPadding);
	slider->resize(st::autoDownloadLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		Export::View::kSizeValueCount,
		Export::View::SizeLimitByIndex,
		*limit,
		[=](int value) {
			*limit = value;
			limits->fire_copy(value);
		});

	addButton(tr::lng_connection_save(), [=] {
		auto allowMore = ranges::view::all(
			*values
		) | ranges::view::filter([&](Pair pair) {
			const auto [type, enabled] = pair;
			const auto value = enabled ? *limit : 0;
			const auto old = settings->bytesLimit(_source, type);
			return (old < value);
		}) | ranges::view::transform([](Pair pair) {
			return pair.first;
		});
		const auto allowMoreTypes = base::flat_set<Type>(
			allowMore.begin(),
			allowMore.end());

		const auto changed = ranges::find_if(*values, [&](Pair pair) {
			const auto [type, enabled] = pair;
			const auto value = enabled ? *limit : 0;
			return settings->bytesLimit(_source, type) != value;
		}) != end(*values);

		const auto hiddenChanged = ranges::find_if(kHidden, [&](Type type) {
			const auto now = settings->bytesLimit(_source, type);
			return (now > 0) && (now != *limit);
		}) != end(kHidden);

		if (changed) {
			for (const auto [type, enabled] : *values) {
				const auto value = enabled ? *limit : 0;
				settings->setBytesLimit(_source, type, value);
			}
		}
		if (hiddenChanged) {
			for (const auto type : kHidden) {
				const auto now = settings->bytesLimit(_source, type);
				if (now > 0) {
					settings->setBytesLimit(_source, type, *limit);
				}
			}
		}
		if (changed || hiddenChanged) {
			Local::writeUserSettings();
		}
		if (allowMoreTypes.contains(Type::Photo)) {
			_session->data().photoLoadSettingsChanged();
		}
		if (ranges::find_if(allowMoreTypes, _1 != Type::Photo)
			!= allowMoreTypes.end()) {
			_session->data().documentLoadSettingsChanged();
		}
		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, content);
}
