/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_download_box.h"

#include "lang/lang_keys.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "info/profile/info_profile_button.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/wrap.h"
#include "storage/localstorage.h"
#include "settings/settings_common.h"
#include "export/view/export_view_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kMegabyte = 1024 * 1024;
constexpr auto kDefaultLimit = 10 * kMegabyte;

} // namespace

AutoDownloadBox::AutoDownloadBox(
	QWidget*,
	Data::AutoDownload::Source source)
: _source(source) {
}

void AutoDownloadBox::prepare() {
	setupContent();
}

void AutoDownloadBox::setupContent() {
	using namespace Settings;
	using namespace Data::AutoDownload;
	using namespace rpl::mappers;
	using Type = Data::AutoDownload::Type;

	setTitle(langFactory(lng_media_auto_title));

	const auto settings = &Auth().settings().autoDownload();
	const auto checked = [=](Source source, Type type) {
		return (settings->bytesLimit(source, type) > 0);
	};

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto values = Ui::CreateChild<base::flat_map<Type, int>>(content);
	const auto add = [&](Type type, LangKey label) {
		const auto value = settings->bytesLimit(_source, type);
		AddButton(
			content,
			label,
			st::settingsButton
		)->toggleOn(
			rpl::single(value > 0)
		)->toggledChanges(
		) | rpl::start_with_next([=](bool enabled) {
			(*values)[type] = enabled ? 1 : 0;
		}, content->lifetime());
		values->emplace(type, value);
	};
	add(Type::Photo, lng_media_photo_title);
	add(Type::VoiceMessage, lng_media_audio_title);
	add(Type::VideoMessage, lng_media_video_messages_title);
	add(Type::Video, lng_media_video_title);
	add(Type::File, lng_media_file_title);
	add(Type::Music, lng_media_music_title);
	add(Type::GIF, lng_media_animation_title);

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
		lng_media_size_limit,
		limits->events_starting_with_copy(
			initialLimit
		) | rpl::map([](int value) {
			return lng_media_size_up_to(
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
	const auto save = [=](
			Type type,
			std::pair<bool, bool> pair) {
		const auto limit = [](bool checked) {
			return checked ? kMaxBytesLimit : 0;
		};
		settings->setBytesLimit(Source::User, type, limit(pair.first));
		settings->setBytesLimit(Source::Group, type, limit(pair.second));
		settings->setBytesLimit(Source::Channel, type, limit(pair.second));
	};

	addButton(langFactory(lng_connection_save), [=] {
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

		if (changed) {
			for (const auto [type, enabled] : *values) {
				const auto value = enabled ? *limit : 0;
				settings->setBytesLimit(_source, type, value);
			}
			Local::writeUserSettings();
		}
		if (allowMoreTypes.contains(Type::Photo)) {
			Auth().data().photoLoadSettingsChanged();
		}
		if (ranges::find_if(allowMoreTypes, _1 != Type::Photo)
			!= allowMoreTypes.end()) {
			Auth().data().documentLoadSettingsChanged();
		}
		closeBox();
	});
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, content);
}
