/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_settings.h"

#include "export/output/export_output_abstract.h"
#include "export/view/export_view_panel_controller.h"
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
#include "auth_session.h"
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

PeerId ReadPeerId(const MTPInputPeer &data) {
	return data.match([](const MTPDinputPeerUser &data) {
		return peerFromUser(data.vuser_id.v);
	}, [](const MTPDinputPeerChat &data) {
		return peerFromChat(data.vchat_id.v);
	}, [](const MTPDinputPeerChannel &data) {
		return peerFromChannel(data.vchannel_id.v);
	}, [](const MTPDinputPeerSelf &data) {
		return Auth().userPeerId();
	}, [](const MTPDinputPeerEmpty &data) {
		return PeerId(0);
	});
}

} // namespace

SettingsWidget::SettingsWidget(QWidget *parent, Settings data)
: RpWidget(parent)
, _singlePeerId(ReadPeerId(data.singlePeer))
, _internal_data(std::move(data)) {
	ResolveSettings(_internal_data);
	setupContent();
}

const Settings &SettingsWidget::readData() const {
	return _internal_data;
}

template <typename Callback>
void SettingsWidget::changeData(Callback &&callback) {
	callback(_internal_data);
	_changes.fire_copy(_internal_data);
}

void SettingsWidget::setupContent() {
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::boxLayerScroll);
	const auto wrap = scroll->setOwnedWidget(
		object_ptr<Ui::OverrideMargins>(
			scroll,
			object_ptr<Ui::VerticalLayout>(scroll)));
	const auto content = static_cast<Ui::VerticalLayout*>(wrap->entity());

	const auto buttons = setupButtons(scroll, wrap);
	setupOptions(content);
	setupPathAndFormat(content);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		scroll->resize(size.width(), size.height() - buttons->height());
		wrap->resizeToWidth(size.width());
		content->resizeToWidth(size.width());
	}, lifetime());
}

void SettingsWidget::setupOptions(not_null<Ui::VerticalLayout*> container) {
	if (!_singlePeerId) {
		setupFullExportOptions(container);
	}
	setupMediaOptions(container);
	if (!_singlePeerId) {
		setupOtherOptions(container);
	}
}

void SettingsWidget::setupFullExportOptions(
		not_null<Ui::VerticalLayout*> container) {
	addOptionWithAbout(
		container,
		lng_export_option_info,
		Type::PersonalInfo | Type::Userpics,
		lng_export_option_info_about);
	addOptionWithAbout(
		container,
		lng_export_option_contacts,
		Type::Contacts,
		lng_export_option_contacts_about);
	addHeader(container, lng_export_header_chats);
	addOption(
		container,
		lng_export_option_personal_chats,
		Type::PersonalChats);
	addOption(container, lng_export_option_bot_chats, Type::BotChats);
	addChatOption(
		container,
		lng_export_option_private_groups,
		Type::PrivateGroups);
	addChatOption(
		container,
		lng_export_option_private_channels,
		Type::PrivateChannels);
	addChatOption(
		container,
		lng_export_option_public_groups,
		Type::PublicGroups);
	addChatOption(
		container,
		lng_export_option_public_channels,
		Type::PublicChannels);
}

void SettingsWidget::setupMediaOptions(
		not_null<Ui::VerticalLayout*> container) {
	if (_singlePeerId != 0) {
		addMediaOptions(container);
		return;
	}
	const auto mediaWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto media = mediaWrap->entity();
	addHeader(media, lng_export_header_media);
	addMediaOptions(media);

	value() | rpl::map([](const Settings &data) {
		return data.types;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](Settings::Types types) {
		mediaWrap->toggle((types & (Type::PersonalChats
			| Type::BotChats
			| Type::PrivateGroups
			| Type::PrivateChannels
			| Type::PublicGroups
			| Type::PublicChannels)) != 0, anim::type::normal);
	}, mediaWrap->lifetime());

	widthValue(
	) | rpl::start_with_next([=](int width) {
		mediaWrap->resizeToWidth(width);
	}, mediaWrap->lifetime());
}

void SettingsWidget::setupOtherOptions(
		not_null<Ui::VerticalLayout*> container) {
	addHeader(container, lng_export_header_other);
	addOptionWithAbout(
		container,
		lng_export_option_sessions,
		Type::Sessions,
		lng_export_option_sessions_about);
	addOptionWithAbout(
		container,
		lng_export_option_other,
		Type::OtherData,
		lng_export_option_other_about);
}

void SettingsWidget::setupPathAndFormat(
		not_null<Ui::VerticalLayout*> container) {
	if (_singlePeerId != 0) {
		addLocationLabel(container);
		return;
	}
	const auto formatGroup = std::make_shared<Ui::RadioenumGroup<Format>>(
		readData().format);
	formatGroup->setChangedCallback([=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
	});
	const auto addFormatOption = [&](LangKey key, Format format) {
		const auto radio = container->add(
			object_ptr<Ui::Radioenum<Format>>(
				container,
				formatGroup,
				format,
				lang(key),
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
	};
	addHeader(container, lng_export_header_format);
	addLocationLabel(container);
	addFormatOption(lng_export_option_html, Format::Html);
	addFormatOption(lng_export_option_json, Format::Json);
}

void SettingsWidget::addLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLabel = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](const QString &path) {
		const auto text = IsDefaultPath(path)
			? QString("Downloads/Telegram Desktop")
			: path;
		auto pathLink = TextWithEntities{
			QDir::toNativeSeparators(text),
			EntitiesInText()
		};
		pathLink.entities.push_back(EntityInText(
			EntityInTextCustomUrl,
			0,
			text.size(),
			QString("internal:edit_export_path")));
		return lng_export_option_location__generic<TextWithEntities>(
			lt_path,
			pathLink);
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(pathLabel),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->setClickHandlerFilter([=](auto&&...) {
		chooseFolder();
		return false;
	});
#endif // OS_MAC_STORE
}

not_null<Ui::RpWidget*> SettingsWidget::setupButtons(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> wrap) {
	using namespace rpl::mappers;

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

	value() | rpl::map([](const Settings &data) {
		return (data.types != Types(0)) || data.onlySinglePeer();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool canStart) {
		refreshButtons(buttons, canStart);
		topShadow->raise();
		bottomShadow->raise();
	}, buttons->lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		buttons->resizeToWidth(size.width());
		buttons->moveToLeft(0, size.height() - buttons->height());
		topShadow->resizeToWidth(size.width());
		topShadow->moveToLeft(0, 0);
		bottomShadow->resizeToWidth(size.width());
		bottomShadow->moveToLeft(0, buttons->y() - st::lineWidth);
	}, buttons->lifetime());

	return buttons;
}

void SettingsWidget::addHeader(
		not_null<Ui::VerticalLayout*> container,
		LangKey key) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			lang(key),
			Ui::FlatLabel::InitType::Simple,
			st::exportHeaderLabel),
		st::exportHeaderPadding);
}

not_null<Ui::Checkbox*> SettingsWidget::addOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			lang(key),
			((readData().types & types) == types),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	base::ObservableViewer(
		checkbox->checkedChanged
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.types |= types;
			} else {
				data.types &= ~types;
			}
		});
	}, lifetime());
	return checkbox;
}

not_null<Ui::Checkbox*> SettingsWidget::addOptionWithAbout(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types,
		LangKey about) {
	const auto result = addOption(container, key, types);
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			lang(about),
			Ui::FlatLabel::InitType::Simple,
			st::exportAboutOptionLabel),
		st::exportAboutOptionPadding);
	return result;
}

void SettingsWidget::addChatOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		Types types) {
	const auto checkbox = addOption(container, key, types);
	const auto onlyMy = container->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				lang(lng_export_option_only_my),
				((readData().fullChats & types) != types),
				st::defaultBoxCheckbox),
			st::exportSubSettingPadding));

	base::ObservableViewer(
		onlyMy->entity()->checkedChanged
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.fullChats &= ~types;
			} else {
				data.fullChats |= types;
			}
		});
	}, checkbox->lifetime());

	onlyMy->toggleOn(base::ObservableViewer(
		checkbox->checkedChanged
	));

	onlyMy->toggle(checkbox->checked(), anim::type::instant);

	if (types & (Type::PublicGroups | Type::PublicChannels)) {
		onlyMy->entity()->setChecked(true);
		onlyMy->entity()->setDisabled(true);
	}
}

void SettingsWidget::addMediaOptions(
		not_null<Ui::VerticalLayout*> container) {
	addMediaOption(container, lng_export_option_photos, MediaType::Photo);
	addMediaOption(
		container,
		lng_export_option_video_files,
		MediaType::Video);
	addMediaOption(
		container,
		lng_export_option_voice_messages,
		MediaType::VoiceMessage);
	addMediaOption(
		container,
		lng_export_option_video_messages,
		MediaType::VideoMessage);
	addMediaOption(
		container,
		lng_export_option_stickers,
		MediaType::Sticker);
	addMediaOption(container, lng_export_option_gifs, MediaType::GIF);
	addMediaOption(container, lng_export_option_files, MediaType::File);
	addSizeSlider(container);
}

void SettingsWidget::addMediaOption(
		not_null<Ui::VerticalLayout*> container,
		LangKey key,
		MediaType type) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			lang(key),
			((readData().media.types & type) == type),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	base::ObservableViewer(
		checkbox->checkedChanged
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.media.types |= type;
			} else {
				data.media.types &= ~type;
			}
		});
	}, lifetime());
}

void SettingsWidget::addSizeSlider(
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::exportFileSizeSlider),
		st::exportFileSizePadding);
	slider->resize(st::exportFileSizeSlider.seekSize);
	slider->setPseudoDiscrete(
		kSizeValueCount + 1,
		SizeLimitByIndex,
		readData().media.sizeLimit,
		[=](int limit) {
			changeData([&](Settings &data) {
				data.media.sizeLimit = limit;
			});
		});

	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		container.get(),
		st::exportFileSizeLabel);
	value() | rpl::map([](const Settings &data) {
		return data.media.sizeLimit;
	}) | rpl::start_with_next([=](int sizeLimit) {
		const auto limit = sizeLimit / kMegabyte;
		const auto size = ((limit > 0)
			? QString::number(limit)
			: QString::number(float64(sizeLimit) / kMegabyte))
			+ " MB";
		const auto text = lng_export_option_size_limit(lt_size, size);
		label->setText(text);
	}, slider->lifetime());

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

void SettingsWidget::refreshButtons(
		not_null<Ui::RpWidget*> container,
		bool canStart) {
	container->hideChildren();
	const auto children = container->children();
	for (const auto child : children) {
		if (child->isWidgetType()) {
			child->deleteLater();
		}
	}
	const auto start = canStart
		? Ui::CreateChild<Ui::RoundButton>(
			container.get(),
			langFactory(lng_export_start),
			st::defaultBoxButton)
		: nullptr;
	if (start) {
		start->show();
		_startClicks = start->clicks(
		) | rpl::map([] {
			return rpl::empty_value();
		});

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
	_cancelClicks = cancel->clicks(
	) | rpl::map([] {
		return rpl::empty_value();
	});

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
	const auto callback = [=](QString &&result) {
		changeData([&](Settings &data) {
			data.path = std::move(result);
			data.forceSubPath = IsDefaultPath(data.path);
		});
	};
	FileDialog::GetFolder(
		this,
		lang(lng_export_folder),
		readData().path,
		callback);
}

rpl::producer<Settings> SettingsWidget::changes() const {
	return _changes.events();
}

rpl::producer<Settings> SettingsWidget::value() const {
	return rpl::single(readData()) | rpl::then(changes());
}

rpl::producer<> SettingsWidget::startClicks() const {
	return _startClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

rpl::producer<> SettingsWidget::cancelClicks() const {
	return _cancelClicks.value(
	) | rpl::map([](Wrap &&wrap) {
		return std::move(wrap.value);
	}) | rpl::flatten_latest();
}

} // namespace View
} // namespace Export
