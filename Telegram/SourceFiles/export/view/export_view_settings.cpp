/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_settings.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "platform/platform_specific.h"
#include "core/file_utilities.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_boxes.h"

namespace Export {
namespace View {
namespace {

constexpr auto kSizeValueCount = 80;
constexpr auto kMegabyte = 1024 * 1024;

int SizeLimitByIndex(int index) {
	Expects(index >= 0 && index <= kSizeValueCount);

	const auto megabytes = [&] {
		if (index <= 10) {
			return index;
		} else if (index <= 30) {
			return 10 + (index - 10) * 2;
		} else if (index <= 40) {
			return 50 + (index - 30) * 5;
		} else if (index <= 60) {
			return 100 + (index - 40) * 10;
		} else if (index <= 70) {
			return 300 + (index - 60) * 20;
		} else {
			return 500 + (index - 70) * 100;
		}
	};
	if (!index) {
		return kMegabyte / 2;
	}
	return megabytes() * kMegabyte;
}

} // namespace

SettingsWidget::SettingsWidget(QWidget *parent)
: RpWidget(parent) {
	if (Global::DownloadPath().isEmpty()) {
		_data.path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		_data.path = cTempDir();
	} else {
		_data.path = Global::DownloadPath();
	}
	_data.internalLinksDomain = Global::InternalLinksDomain();

	setupContent();
}

void SettingsWidget::setupContent() {
	using namespace rpl::mappers;

	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::boxLayerScroll);
	const auto wrap = scroll->setOwnedWidget(object_ptr<Ui::IgnoreMargins>(
		scroll,
		object_ptr<Ui::VerticalLayout>(scroll)));
	const auto content = static_cast<Ui::VerticalLayout*>(wrap->entity());

	const auto buttonsPadding = st::boxButtonPadding;
	const auto buttonsHeight = buttonsPadding.top()
		+ st::defaultBoxButton.height
		+ buttonsPadding.bottom();
	const auto buttons = Ui::CreateChild<Ui::FixedHeightWidget>(
		this,
		buttonsHeight);
	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(this);
	topShadow->toggleOn(scroll->scrollTopValue(
	) | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		scroll->heightValue(),
		scroll->scrollTopValue(),
		wrap->heightValue(),
		_2
	) | rpl::map([=](int top) {
		return top < scroll->scrollTopMax();
	}));
	const auto refreshButtonsCallback = [=] {
		refreshButtons(buttons);
	};
	const auto addHeader = [&](
			not_null<Ui::VerticalLayout*> container,
			LangKey key) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				lang(key),
				Ui::FlatLabel::InitType::Simple,
				st::exportHeaderLabel),
			st::exportHeaderPadding);
	};

	const auto addOption = [&](LangKey key, Types types) {
		const auto checkbox = content->add(
			object_ptr<Ui::Checkbox>(
				content,
				lang(key),
				((_data.types & types) == types),
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
		base::ObservableViewer(
			checkbox->checkedChanged
		) | rpl::start_with_next([=](bool checked) {
			if (checked) {
				_data.types |= types;
			} else {
				_data.types &= ~types;
			}
			_dataTypesChanges.fire_copy(_data.types);
			refreshButtonsCallback();
		}, lifetime());
		return checkbox;
	};
	const auto addBigOption = [&](LangKey key, Types types) {
		const auto checkbox = addOption(key, types);
		const auto onlyMy = content->add(
			object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
				content,
				object_ptr<Ui::Checkbox>(
					content,
					lang(lng_export_option_only_my),
					((_data.fullChats & types) != types),
					st::defaultBoxCheckbox),
				st::exportSubSettingPadding));

		base::ObservableViewer(
			onlyMy->entity()->checkedChanged
		) | rpl::start_with_next([=](bool checked) {
			if (checked) {
				_data.fullChats &= ~types;
			} else {
				_data.fullChats |= types;
			}
		}, checkbox->lifetime());

		onlyMy->toggleOn(base::ObservableViewer(
			checkbox->checkedChanged
		));

		onlyMy->toggle(checkbox->checked(), anim::type::instant);

		if (types & (Type::PublicGroups | Type::PublicChannels)) {
			onlyMy->entity()->setChecked(true);
			onlyMy->entity()->setDisabled(true);
		}
	};
	addOption(lng_export_option_info, Type::PersonalInfo | Type::Userpics);
	addOption(lng_export_option_contacts, Type::Contacts);
	addOption(lng_export_option_sessions, Type::Sessions);
	addHeader(content, lng_export_header_chats);
	addOption(lng_export_option_personal_chats, Type::PersonalChats);
	addOption(lng_export_option_bot_chats, Type::BotChats);
	addBigOption(lng_export_option_private_groups, Type::PrivateGroups);
	addBigOption(lng_export_option_private_channels, Type::PrivateChannels);
	addBigOption(lng_export_option_public_groups, Type::PublicGroups);
	addBigOption(lng_export_option_public_channels, Type::PublicChannels);
	const auto mediaWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto media = mediaWrap->entity();
	const auto addSubOption = [&](LangKey key, MediaType type) {
		const auto checkbox = media->add(
			object_ptr<Ui::Checkbox>(
				media,
				lang(key),
				((_data.media.types & type) == type),
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
		base::ObservableViewer(
			checkbox->checkedChanged
		) | rpl::start_with_next([=](bool checked) {
			if (checked) {
				_data.media.types |= type;
			} else {
				_data.media.types &= ~type;
			}
			refreshButtonsCallback();
		}, lifetime());
	};
	addHeader(media, lng_export_header_media);
	addSubOption(lng_export_option_photos, MediaType::Photo);
	addSubOption(lng_export_option_video_files, MediaType::Video);
	addSubOption(lng_export_option_voice_messages, MediaType::VoiceMessage);
	addSubOption(lng_export_option_video_messages, MediaType::VideoMessage);
	addSubOption(lng_export_option_stickers, MediaType::Sticker);
	addSubOption(lng_export_option_gifs, MediaType::GIF);
	addSubOption(lng_export_option_files, MediaType::File);
	createSizeSlider(media);

	_dataTypesChanges.events_starting_with_copy(
		_data.types
	) | rpl::start_with_next([=](Settings::Types types) {
		mediaWrap->toggle((types & (Type::PersonalChats
			| Type::BotChats
			| Type::PrivateGroups
			| Type::PrivateChannels
			| Type::PublicGroups
			| Type::PublicChannels)) != 0, anim::type::normal);
	}, mediaWrap->lifetime());

	refreshButtonsCallback();

	topShadow->raise();
	bottomShadow->raise();

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		scroll->resize(size.width(), size.height() - buttons->height());
		wrap->resizeToWidth(size.width());
		content->resizeToWidth(size.width());
		buttons->resizeToWidth(size.width());
		topShadow->resizeToWidth(size.width());
		mediaWrap->resizeToWidth(size.width());
		topShadow->moveToLeft(0, 0);
		bottomShadow->resizeToWidth(size.width());
		bottomShadow->moveToLeft(0, scroll->height() - st::lineWidth);
		buttons->moveToLeft(0, size.height() - buttons->height());
	}, lifetime());
}

void SettingsWidget::createSizeSlider(
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::exportFileSizeSlider),
		st::exportFileSizePadding);
	slider->resize(st::exportFileSizeSlider.seekSize);
	slider->setAlwaysDisplayMarker(true);
	slider->setMoveByWheel(true);
	slider->setDirection(Ui::ContinuousSlider::Direction::Horizontal);
	for (auto i = 0; i != kSizeValueCount + 1; ++i) {
		if (_data.media.sizeLimit <= SizeLimitByIndex(i)) {
			slider->setValue(i / float64(kSizeValueCount));
			break;
		}
	}

	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		container.get(),
		st::exportFileSizeLabel);
	const auto refreshSizeLimit = [=] {
		const auto limit = _data.media.sizeLimit / kMegabyte;
		const auto size = ((limit > 0)
			? QString::number(limit)
			: QString::number(float64(_data.media.sizeLimit) / kMegabyte))
			+ " MB";
		const auto text = lng_export_option_size_limit(lt_size, size);
		label->setText(text);
	};
	slider->setAdjustCallback([=](float64 value) {
		return std::round(value * kSizeValueCount) / kSizeValueCount;
	});
	slider->setChangeProgressCallback([=](float64 value) {
		const auto index = int(std::round(value * kSizeValueCount));
		_data.media.sizeLimit = SizeLimitByIndex(index);
		refreshSizeLimit();
	});
	refreshSizeLimit();

	rpl::combine(
		label->widthValue(),
		slider->geometryValue(),
		_2
	) | rpl::start_with_next([=](QRect geometry) {
		label->moveToRight(
			st::exportFileSizePadding.right(),
			geometry.y() - label->height() - st::exportFileSizeLabelBottom);
	}, label->lifetime());

}

void SettingsWidget::refreshButtons(not_null<Ui::RpWidget*> container) {
	container->hideChildren();
	const auto children = container->children();
	for (const auto child : children) {
		if (child->isWidgetType()) {
			child->deleteLater();
		}
	}
	const auto start = _data.types
		? Ui::CreateChild<Ui::RoundButton>(
			container.get(),
			langFactory(lng_export_start),
			st::defaultBoxButton)
		: nullptr;
	if (start) {
		start->show();
		start->addClickHandler([=] { chooseFolder(); });

		container->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			const auto right = st::boxButtonPadding.right();
			const auto top = st::boxButtonPadding.top();
			start->moveToRight(right, top);
		}, start->lifetime());
	}

	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		container.get(),
		langFactory(lng_cancel),
		st::defaultBoxButton);
	cancel->show();
	_cancelClicks = cancel->clicks();

	rpl::combine(
		container->sizeValue(),
		start ? start->widthValue() : rpl::single(0)
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto right = st::boxButtonPadding.right()
			+ (width ? width + st::boxButtonPadding.left() : 0);
		const auto top = st::boxButtonPadding.top();
		cancel->moveToRight(right, top);
	}, cancel->lifetime());
}

void SettingsWidget::chooseFolder() {
	const auto ready = [=](QString &&result) {
		_data.path = result;
		_startClicks.fire(base::duplicate(_data));
	};
	FileDialog::GetFolder(this, lang(lng_export_folder), _data.path, ready);
}

rpl::producer<Settings> SettingsWidget::startClicks() const {
	return _startClicks.events();
}

rpl::producer<> SettingsWidget::cancelClicks() const {
	return _cancelClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

} // namespace View
} // namespace Export
