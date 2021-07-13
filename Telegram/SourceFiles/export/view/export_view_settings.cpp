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
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/calendar_box.h"
#include "platform/platform_specific.h"
#include "core/file_utilities.h"
#include "base/unixtime.h"
#include "base/qt_adapters.h"
#include "main/main_session.h"
#include "styles/style_widgets.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"

namespace Export {
namespace View {
namespace {

constexpr auto kMegabyte = 1024 * 1024;

[[nodiscard]] PeerId ReadPeerId(
		not_null<Main::Session*> session,
		const MTPInputPeer &data) {
	return data.match([](const MTPDinputPeerUser &data) {
		return peerFromUser(data.vuser_id().v);
	}, [](const MTPDinputPeerUserFromMessage &data) {
		return peerFromUser(data.vuser_id().v);
	}, [](const MTPDinputPeerChat &data) {
		return peerFromChat(data.vchat_id().v);
	}, [](const MTPDinputPeerChannel &data) {
		return peerFromChannel(data.vchannel_id().v);
	}, [](const MTPDinputPeerChannelFromMessage &data) {
		return peerFromChannel(data.vchannel_id().v);
	}, [&](const MTPDinputPeerSelf &data) {
		return session->userPeerId();
	}, [](const MTPDinputPeerEmpty &data) {
		return PeerId(0);
	});
}

void ChooseFormatBox(
		not_null<Ui::GenericBox*> box,
		Output::Format format,
		Fn<void(Output::Format)> done) {
	using Format = Output::Format;
	const auto group = std::make_shared<Ui::RadioenumGroup<Format>>(format);
	const auto addFormatOption = [&](QString label, Format format) {
		box->addRow(
			object_ptr<Ui::Radioenum<Format>>(
				box,
				group,
				format,
				label,
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
	};
	box->setTitle(tr::lng_export_option_choose_format());
	addFormatOption(tr::lng_export_option_html(tr::now), Format::Html);
	addFormatOption(tr::lng_export_option_json(tr::now), Format::Json);
	box->addButton(tr::lng_settings_save(), [=] { done(group->value()); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

int SizeLimitByIndex(int index) {
	Expects(index >= 0 && index < kSizeValueCount);

	index += 1;
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
		} else if (index <= 80) {
			return 500 + (index - 70) * 50;
		} else {
			return 1000 + (index - 80) * 100;
		}
	}();
	return megabytes * kMegabyte;
}

SettingsWidget::SettingsWidget(
	QWidget *parent,
	not_null<Main::Session*> session,
	Settings data)
: RpWidget(parent)
, _session(session)
, _singlePeerId(ReadPeerId(session, data.singlePeer))
, _internal_data(std::move(data)) {
	ResolveSettings(session, _internal_data);
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
		st::boxScroll);
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
		tr::lng_export_option_info(tr::now),
		Type::PersonalInfo | Type::Userpics,
		tr::lng_export_option_info_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_contacts(tr::now),
		Type::Contacts,
		tr::lng_export_option_contacts_about(tr::now));
	addHeader(container, tr::lng_export_header_chats(tr::now));
	addOption(
		container,
		tr::lng_export_option_personal_chats(tr::now),
		Type::PersonalChats);
	addOption(
		container,
		tr::lng_export_option_bot_chats(tr::now),
		Type::BotChats);
	addChatOption(
		container,
		tr::lng_export_option_private_groups(tr::now),
		Type::PrivateGroups);
	addChatOption(
		container,
		tr::lng_export_option_private_channels(tr::now),
		Type::PrivateChannels);
	addChatOption(
		container,
		tr::lng_export_option_public_groups(tr::now),
		Type::PublicGroups);
	addChatOption(
		container,
		tr::lng_export_option_public_channels(tr::now),
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
	addHeader(media, tr::lng_export_header_media(tr::now));
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
	addHeader(container, tr::lng_export_header_other(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_sessions(tr::now),
		Type::Sessions,
		tr::lng_export_option_sessions_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_other(tr::now),
		Type::OtherData,
		tr::lng_export_option_other_about(tr::now));
}

void SettingsWidget::setupPathAndFormat(
		not_null<Ui::VerticalLayout*> container) {
	if (_singlePeerId != 0) {
		addFormatAndLocationLabel(container);
		addLimitsLabel(container);
		return;
	}
	const auto formatGroup = std::make_shared<Ui::RadioenumGroup<Format>>(
		readData().format);
	formatGroup->setChangedCallback([=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
	});
	const auto addFormatOption = [&](QString label, Format format) {
		container->add(
			object_ptr<Ui::Radioenum<Format>>(
				container,
				formatGroup,
				format,
				label,
				st::defaultBoxCheckbox),
			st::exportSettingPadding);
	};
	addHeader(container, tr::lng_export_header_format(tr::now));
	addLocationLabel(container);
	addFormatOption(tr::lng_export_option_html(tr::now), Format::Html);
	addFormatOption(tr::lng_export_option_json(tr::now), Format::Json);
}

void SettingsWidget::addLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLink = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto text = IsDefaultPath(_session, path)
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: path;
		return Ui::Text::Link(
			QDir::toNativeSeparators(text),
			QString("internal:edit_export_path"));
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_location(
				lt_path,
				std::move(pathLink),
				Ui::Text::WithEntities),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->setClickHandlerFilter([=](auto&&...) {
		chooseFolder();
		return false;
	});
#endif // OS_MAC_STORE
}

void SettingsWidget::chooseFormat() {
	const auto shared = std::make_shared<QPointer<Ui::GenericBox>>();
	const auto callback = [=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
		if (const auto weak = shared->data()) {
			weak->closeBox();
		}
	};
	auto box = Box(
		ChooseFormatBox,
		readData().format,
		callback);
	*shared = Ui::MakeWeak(box.data());
	_showBoxCallback(std::move(box));
}

void SettingsWidget::addFormatAndLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLink = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto text = IsDefaultPath(_session, path)
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: path;
		return Ui::Text::Link(
			QDir::toNativeSeparators(text),
			u"internal:edit_export_path"_q);
	});
	auto formatLink = value() | rpl::map([](const Settings &data) {
		return data.format;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](Format format) {
		const auto text = (format == Format::Html) ? "HTML" : "JSON";
		return Ui::Text::Link(text, u"internal:edit_format"_q);
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_format_location(
				lt_format,
				std::move(formatLink),
				lt_path,
				std::move(pathLink),
				Ui::Text::WithEntities),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->setClickHandlerFilter([=](
		const ClickHandlerPtr &handler,
		Qt::MouseButton) {
		const auto url = handler->dragText();
		if (url == qstr("internal:edit_export_path")) {
			chooseFolder();
		} else if (url == qstr("internal:edit_format")) {
			chooseFormat();
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
		return false;
	});
#endif // OS_MAC_STORE
}

void SettingsWidget::addLimitsLabel(
		not_null<Ui::VerticalLayout*> container) {
	auto fromLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerFrom;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](TimeId from) {
		return (from
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(from).date()))
			: tr::lng_export_beginning()
		) | Ui::Text::ToLink(qsl("internal:edit_from"));
	}) | rpl::flatten_latest();

	auto tillLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerTill;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](TimeId till) {
		return (till
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(till).date()))
			: tr::lng_export_end()
		) | Ui::Text::ToLink(qsl("internal:edit_till"));
	}) | rpl::flatten_latest();

	auto datesText = tr::lng_export_limits(
		lt_from,
		std::move(fromLink),
		lt_till,
		std::move(tillLink),
		Ui::Text::WithEntities
	) | rpl::after_next([=] {
		container->resizeToWidth(container->width());
	});

	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(datesText),
			st::exportLocationLabel),
		st::exportLimitsPadding);
	label->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		const auto url = handler->dragText();
		if (url == qstr("internal:edit_from")) {
			const auto done = [=](TimeId limit) {
				changeData([&](Settings &settings) {
					settings.singlePeerFrom = limit;
				});
			};
			editDateLimit(
				readData().singlePeerFrom,
				0,
				readData().singlePeerTill,
				tr::lng_export_from_beginning(),
				done);
		} else if (url == qstr("internal:edit_till")) {
			const auto done = [=](TimeId limit) {
				changeData([&](Settings &settings) {
					settings.singlePeerTill = limit;
				});
			};
			editDateLimit(
				readData().singlePeerTill,
				readData().singlePeerFrom,
				0,
				tr::lng_export_till_end(),
				done);
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
		return false;
	});
}

void SettingsWidget::editDateLimit(
		TimeId current,
		TimeId min,
		TimeId max,
		rpl::producer<QString> resetLabel,
		Fn<void(TimeId)> done) {
	Expects(_showBoxCallback != nullptr);

	const auto highlighted = current
		? base::unixtime::parse(current).date()
		: max
		? base::unixtime::parse(max).date()
		: min
		? base::unixtime::parse(min).date()
		: QDate::currentDate();
	const auto month = highlighted;
	const auto shared = std::make_shared<QPointer<Ui::CalendarBox>>();
	const auto finalize = [=](not_null<Ui::CalendarBox*> box) {
		box->setMaxDate(max
			? base::unixtime::parse(max).date()
			: QDate::currentDate());
		box->setMinDate(min
			? base::unixtime::parse(min).date()
			: QDate(2013, 8, 1)); // Telegram was launched in August 2013 :)
		box->addLeftButton(std::move(resetLabel), crl::guard(this, [=] {
			done(0);
			if (const auto weak = shared->data()) {
				weak->closeBox();
			}
		}));
	};
	const auto callback = crl::guard(this, [=](const QDate &date) {
		done(base::unixtime::serialize(base::QDateToDateTime(date)));
		if (const auto weak = shared->data()) {
			weak->closeBox();
		}
	});
	auto box = Box<Ui::CalendarBox>(
		month,
		highlighted,
		callback,
		finalize,
		st::exportCalendarSizes);
	*shared = Ui::MakeWeak(box.data());
	_showBoxCallback(std::move(box));
}

not_null<Ui::RpWidget*> SettingsWidget::setupButtons(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> wrap) {
	using namespace rpl::mappers;

	const auto buttonsPadding = st::defaultBox.buttonPadding;
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
		const QString &text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			text,
			st::exportHeaderLabel),
		st::exportHeaderPadding);
}

not_null<Ui::Checkbox*> SettingsWidget::addOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			((readData().types & types) == types),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	checkbox->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.types |= types;
			} else {
				data.types &= ~types;
			}
		});
	}, checkbox->lifetime());
	return checkbox;
}

not_null<Ui::Checkbox*> SettingsWidget::addOptionWithAbout(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types,
		const QString &about) {
	const auto result = addOption(container, text, types);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			about,
			st::exportAboutOptionLabel),
		st::exportAboutOptionPadding);
	return result;
}

void SettingsWidget::addChatOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		Types types) {
	const auto checkbox = addOption(container, text, types);
	const auto onlyMy = container->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_export_option_only_my(tr::now),
				((readData().fullChats & types) != types),
				st::defaultBoxCheckbox),
			st::exportSubSettingPadding));

	onlyMy->entity()->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.fullChats &= ~types;
			} else {
				data.fullChats |= types;
			}
		});
	}, onlyMy->lifetime());

	onlyMy->toggleOn(checkbox->checkedValue());

	if (types & (Type::PublicGroups | Type::PublicChannels)) {
		onlyMy->entity()->setChecked(true);
		onlyMy->entity()->setDisabled(true);
	}
}

void SettingsWidget::addMediaOptions(
		not_null<Ui::VerticalLayout*> container) {
	addMediaOption(
		container,
		tr::lng_export_option_photos(tr::now),
		MediaType::Photo);
	addMediaOption(
		container,
		tr::lng_export_option_video_files(tr::now),
		MediaType::Video);
	addMediaOption(
		container,
		tr::lng_export_option_voice_messages(tr::now),
		MediaType::VoiceMessage);
	addMediaOption(
		container,
		tr::lng_export_option_video_messages(tr::now),
		MediaType::VideoMessage);
	addMediaOption(
		container,
		tr::lng_export_option_stickers(tr::now),
		MediaType::Sticker);
	addMediaOption(
		container,
		tr::lng_export_option_gifs(tr::now),
		MediaType::GIF);
	addMediaOption(
		container,
		tr::lng_export_option_files(tr::now),
		MediaType::File);
	addSizeSlider(container);
}

void SettingsWidget::addMediaOption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		MediaType type) {
	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			((readData().media.types & type) == type),
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	checkbox->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.media.types |= type;
			} else {
				data.media.types &= ~type;
			}
		});
	}, checkbox->lifetime());
}

void SettingsWidget::addSizeSlider(
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::exportFileSizeSlider),
		st::exportFileSizePadding);
	slider->resize(st::exportFileSizeSlider.seekSize);
	slider->setPseudoDiscrete(
		kSizeValueCount,
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
		const auto size = QString::number(limit) + " MB";
		const auto text = tr::lng_export_option_size_limit(tr::now, lt_size, size);
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
			tr::lng_export_start(),
			st::defaultBoxButton)
		: nullptr;
	if (start) {
		start->show();
		_startClicks = start->clicks() | rpl::to_empty;

		container->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			const auto right = st::defaultBox.buttonPadding.right();
			const auto top = st::defaultBox.buttonPadding.top();
			start->moveToRight(right, top);
		}, start->lifetime());
	}

	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		container.get(),
		tr::lng_cancel(),
		st::defaultBoxButton);
	cancel->show();
	_cancelClicks = cancel->clicks() | rpl::to_empty;

	rpl::combine(
		container->sizeValue(),
		start ? start->widthValue() : rpl::single(0)
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto right = st::defaultBox.buttonPadding.right()
			+ (width ? width + st::defaultBox.buttonPadding.left() : 0);
		const auto top = st::defaultBox.buttonPadding.top();
		cancel->moveToRight(right, top);
	}, cancel->lifetime());
}

void SettingsWidget::chooseFolder() {
	const auto callback = [=](QString &&result) {
		changeData([&](Settings &data) {
			data.path = std::move(result);
			data.forceSubPath = IsDefaultPath(_session, data.path);
		});
	};
	FileDialog::GetFolder(
		this,
		tr::lng_export_folder(tr::now),
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
