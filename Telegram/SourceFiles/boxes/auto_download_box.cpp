/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_download_box.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "data/data_session.h"
#include "data/data_auto_download.h"
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

not_null<int*> AddSizeLimitSlider(
		not_null<Ui::VerticalLayout*> container,
		const base::flat_map<Type, int> &values,
		int defaultValue) {
	using namespace Settings;
	using Pair = base::flat_map<Type, int>::value_type;

	const auto limits = Ui::CreateChild<rpl::event_stream<int>>(
		container.get());
	const auto currentLimit = ranges::max_element(
		values,
		std::less<>(),
		[](Pair pair) { return pair.second; })->second;
	const auto initialLimit = currentLimit ? currentLimit : defaultValue;
	const auto result = Ui::CreateChild<int>(container.get(), initialLimit);
	AddButtonWithLabel(
		container,
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
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::autoDownloadLimitSlider),
		st::autoDownloadLimitPadding);
	slider->resize(st::autoDownloadLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		Export::View::kSizeValueCount,
		Export::View::SizeLimitByIndex,
		*result,
		[=](int value) {
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
	using Pair = base::flat_map<Type, int>::value_type;

	setTitle(tr::lng_profile_settings_section());

	const auto settings = &_session->settings().autoDownload();

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto add = [&](
			not_null<base::flat_map<Type, int>*> values,
			Type type,
			rpl::producer<QString> label) {
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

	AddSubsectionTitle(content, tr::lng_media_auto_title());

	const auto downloadValues = Ui::CreateChild<base::flat_map<Type, int>>(
		content);
	add(downloadValues, Type::Photo, tr::lng_media_photo_title());
	add(downloadValues, Type::File, tr::lng_media_file_title());

	const auto downloadLimit = AddSizeLimitSlider(
		content,
		*downloadValues,
		kDefaultDownloadLimit);

	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_media_auto_play());

	const auto autoPlayValues = Ui::CreateChild<base::flat_map<Type, int>>(
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

	addButton(tr::lng_connection_save(), [=] {
		auto &&values = ranges::views::concat(
			*downloadValues,
			*autoPlayValues);
		auto allowMore = values | ranges::views::filter([&](Pair pair) {
			const auto [type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			const auto old = settings->bytesLimit(_source, type);
			return (old < value);
		}) | ranges::views::transform([](Pair pair) {
			return pair.first;
		});
		const auto less = ranges::any_of(*autoPlayValues, [&](Pair pair) {
			const auto [type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			return value < settings->bytesLimit(_source, type);
		});
		const auto allowMoreTypes = base::flat_set<Type>(
			allowMore.begin(),
			allowMore.end());

		const auto changed = ranges::any_of(values, [&](Pair pair) {
			const auto [type, enabled] = pair;
			const auto value = enabled ? limitByType(type) : 0;
			return value != settings->bytesLimit(_source, type);
		});

		const auto &kHidden = kStreamedTypes;
		const auto hiddenChanged = ranges::any_of(kHidden, [&](Type type) {
			const auto now = settings->bytesLimit(_source, type);
			return (now > 0) && (now != limitByType(type));
		});

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
		if (changed || hiddenChanged) {
			_session->saveSettingsDelayed();
		}
		if (allowMoreTypes.contains(Type::Photo)) {
			_session->data().photoLoadSettingsChanged();
		}
		if (ranges::any_of(allowMoreTypes, _1 != Type::Photo)) {
			_session->data().documentLoadSettingsChanged();
		}
		if (less) {
			_session->data().checkPlayingAnimations();
		}
		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, content);
}
