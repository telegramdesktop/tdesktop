/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_settings.h"

#include "export/output/export_output_abstract.h"
#include "export/view/export_view_panel_controller.h"
#include "export/data/export_data_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "mtproto/sender.h"
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
#include "ui/boxes/choose_time.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "base/unixtime.h"
#include "base/flat_set.h"
#include "main/main_session.h"
#include "styles/style_export.h"
#include "styles/style_layers.h"

namespace Export {
namespace View {
namespace {

constexpr auto kMegabyte = int64(1024) * 1024;

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

[[nodiscard]] bool SizeLimitFitsOptionLine(
		int outerWidth,
		int optionWidth,
		int labelWidth) {
	const auto available = outerWidth
		- st::exportSettingPadding.left()
		- st::exportFileSizePadding.right();
	return (optionWidth + labelWidth <= available);
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
	addFormatOption(
		tr::lng_export_option_html_and_json(tr::now),
		Format::HtmlAndJson);
	box->addButton(tr::lng_settings_save(), [=] { done(group->current()); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

int64 SizeLimitByIndex(int index) {
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
		} else if (index <= 90) {
			return 1000 + (index - 80) * 100;
		} else {
			return 2000 + (index - 90) * 200;
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
	) | rpl::on_next([=](QSize size) {
		scroll->resize(size.width(), size.height() - buttons->height());
		wrap->resizeToWidth(size.width());
		content->resizeToWidth(size.width());
	}, lifetime());
}

void SettingsWidget::setupOptions(not_null<Ui::VerticalLayout*> container) {
	if (!_singlePeerId) {
		setupFullExportOptions(container);
	} else if (readData().splitTopics) {
		addHeader(container, tr::lng_export_header_topic(tr::now));
	}
	if (!_singlePeerId || readData().splitTopics) {
		const auto button = container->add(
			object_ptr<Ui::RoundButton>(
				container,
				tr::lng_export_choose_topics(),
				st::defaultBoxButton),
			st::exportSettingPadding);
		button->setClickedCallback([=] {
			if (_singlePeerId) {
				auto chat = Data::DialogInfo();
				chat.input = readData().singlePeer;
				chat.peerId = _singlePeerId;
				chat.name = _session->data().peer(_singlePeerId)->name().toUtf8();
				chooseForumTopics(chat);
			} else {
				chooseForums();
			}
		});
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
	addOptionWithAbout(
		container,
		tr::lng_export_option_stories(tr::now),
		Type::Stories,
		tr::lng_export_option_stories_about(tr::now));
	addOptionWithAbout(
		container,
		tr::lng_export_option_profile_music(tr::now),
		Type::ProfileMusic,
		tr::lng_export_option_profile_music_about(tr::now));
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
	) | rpl::on_next([=](Settings::Types types) {
		mediaWrap->toggle((types & (Type::PersonalChats
			| Type::BotChats
			| Type::PrivateGroups
			| Type::PrivateChannels
			| Type::PublicGroups
			| Type::PublicChannels
			| Type::ProfileMusic)) != 0, anim::type::normal);
	}, mediaWrap->lifetime());

	widthValue(
	) | rpl::on_next([=](int width) {
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
	addFormatOption(tr::lng_export_option_html_and_json(tr::now), Format::HtmlAndJson);
}

void SettingsWidget::addLocationLabel(
		not_null<Ui::VerticalLayout*> container) {
#ifndef OS_MAC_STORE
	auto pathLink = value() | rpl::map([](const Settings &data) {
		return data.path;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const QString &path) {
		const auto text = IsDefaultPath(_session, path)
			? Core::App().canReadDefaultDownloadPath()
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: tr::lng_download_path_temp(tr::now)
			: path;
		return tr::link(
			QDir::toNativeSeparators(text),
			QString("internal:edit_export_path"));
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_location(
				lt_path,
				std::move(pathLink),
				tr::marked),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=] {
		chooseFolder();
	});
#endif // OS_MAC_STORE
}

void SettingsWidget::chooseFormat() {
	const auto shared = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
	const auto callback = [=](Format format) {
		changeData([&](Settings &data) {
			data.format = format;
		});
		if (const auto strong = shared->get()) {
			strong->closeBox();
		}
	};
	auto box = Box(
		ChooseFormatBox,
		readData().format,
		callback);
	*shared = base::make_weak(box.data());
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
			? Core::App().canReadDefaultDownloadPath()
			? u"Downloads/"_q + File::DefaultDownloadPathFolder(_session)
			: tr::lng_download_path_temp(tr::now)
			: path;
		return tr::link(
			QDir::toNativeSeparators(text),
			u"internal:edit_export_path"_q);
	});
	auto formatLink = value() | rpl::map([](const Settings &data) {
		return data.format;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](Format format) {
		const auto text = (format == Format::Html)
			? "HTML"
			: (format == Format::Json)
			? "JSON"
			: tr::lng_export_option_html_and_json(tr::now);
		return tr::link(text, u"internal:edit_format"_q);
	});
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_export_option_format_location(
				lt_format,
				std::move(formatLink),
				lt_path,
				std::move(pathLink),
				tr::marked),
			st::exportLocationLabel),
		st::exportLocationPadding);
	label->overrideLinkClickHandler([=](const QString &url) {
		if (url == u"internal:edit_export_path"_q) {
			chooseFolder();
		} else if (url == u"internal:edit_format"_q) {
			chooseFormat();
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
	});
#endif // OS_MAC_STORE
}

void SettingsWidget::addLimitsLabel(
		not_null<Ui::VerticalLayout*> container) {
	auto fromDateLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerFrom;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](TimeId from) {
		return (from
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(from).date()))
			: tr::lng_export_beginning()
		) | rpl::map(tr::url(u"internal:edit_from"_q));
	}) | rpl::flatten_latest();

	const auto mapToTime = [](TimeId id, const QString &link) {
		return rpl::single(id
			? QLocale().toString(
				base::unixtime::parse(id).time(),
				QLocale::ShortFormat)
			: QString()
		) | rpl::map(tr::url(link));
	};

	const auto concat = [](TextWithEntities date, TextWithEntities link) {
		return link.text.isEmpty()
			? date
			: date.append(u", "_q).append(std::move(link));
	};

	auto fromTimeLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerFrom;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId from) {
		return mapToTime(from, u"internal:edit_from_time"_q);
	}) | rpl::flatten_latest();

	auto fromLink = rpl::combine(
		std::move(fromDateLink),
		std::move(fromTimeLink)
	) | rpl::map(concat);

	auto tillDateLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerTill;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](TimeId till) {
		return (till
			? rpl::single(langDayOfMonthFull(
				base::unixtime::parse(till).date()))
			: tr::lng_export_end()
		) | rpl::map(tr::url(u"internal:edit_till"_q));
	}) | rpl::flatten_latest();

	auto tillTimeLink = value() | rpl::map([](const Settings &data) {
		return data.singlePeerTill;
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](TimeId till) {
		return mapToTime(till, u"internal:edit_till_time"_q);
	}) | rpl::flatten_latest();

	auto tillLink = rpl::combine(
		std::move(tillDateLink),
		std::move(tillTimeLink)
	) | rpl::map(concat);

	auto datesText = tr::lng_export_limits(
		lt_from,
		std::move(fromLink),
		lt_till,
		std::move(tillLink),
		tr::marked
	) | rpl::after_next([=] {
		container->resizeToWidth(container->width());
	});

	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(datesText),
			st::boxLabel),
		st::exportLimitsPadding);

	const auto removeTime = [](TimeId dateTime) {
		return base::unixtime::serialize(
			QDateTime(
				base::unixtime::parse(dateTime).date(),
				QTime()));
	};

	const auto editTimeLimit = [=](Fn<TimeId()> now, Fn<void(TimeId)> done) {
		_showBoxCallback(Box([=](not_null<Ui::GenericBox*> box) {
			auto result = Ui::ChooseTimeWidget(
				box->verticalLayout(),
				[&] {
					const auto time = base::unixtime::parse(now()).time();
					return time.hour() * 3600
						+ time.minute() * 60
						+ time.second();
				}(),
				true);
			const auto widget = box->addRow(std::move(result.widget));
			const auto toSave = widget->lifetime().make_state<TimeId>(0);
			std::move(
				result.secondsValue
			) | rpl::on_next([=](TimeId t) {
				*toSave = t;
			}, box->lifetime());
			box->addButton(tr::lng_settings_save(), [=] {
				done(*toSave);
				box->closeBox();
			});
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
			box->setTitle(tr::lng_settings_ttl_after_custom());
		}));
	};

	constexpr auto kOffset = 600;

	label->overrideLinkClickHandler([=](const QString &url) {
		if (url == u"internal:edit_from"_q) {
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
		} else if (url == u"internal:edit_from_time"_q) {
			const auto now = [=] {
				auto result = TimeId(0);
				changeData([&](Settings &settings) {
					result = settings.singlePeerFrom;
				});
				return result;
			};
			const auto done = [=](TimeId time) {
				changeData([&](Settings &settings) {
					const auto result = time
						+ removeTime(settings.singlePeerFrom);
					if (result >= settings.singlePeerTill
							&& settings.singlePeerTill) {
						settings.singlePeerFrom = settings.singlePeerTill
							- kOffset;
					} else {
						settings.singlePeerFrom = result;
					}
				});
			};
			editTimeLimit(now, done);
		} else if (url == u"internal:edit_till"_q) {
			const auto done = [=](TimeId limit) {
				changeData([&](Settings &settings) {
					if (limit <= settings.singlePeerFrom
							&& settings.singlePeerFrom) {
						settings.singlePeerTill = settings.singlePeerFrom
							+ kOffset;
					} else {
						settings.singlePeerTill = limit;
					}
				});
			};
			editDateLimit(
				readData().singlePeerTill,
				readData().singlePeerFrom,
				0,
				tr::lng_export_till_end(),
				done);
		} else if (url == u"internal:edit_till_time"_q) {
			const auto now = [=] {
				auto result = TimeId(0);
				changeData([&](Settings &settings) {
					result = settings.singlePeerTill;
				});
				return result;
			};
			const auto done = [=](TimeId time) {
				changeData([&](Settings &settings) {
					const auto result = time
						+ removeTime(settings.singlePeerTill);
					if (result <= settings.singlePeerFrom
							&& settings.singlePeerFrom) {
						settings.singlePeerTill = settings.singlePeerFrom
							+ kOffset;
					} else {
						settings.singlePeerTill = result;
					}
				});
			};
			editTimeLimit(now, done);
		} else {
			Unexpected("Click handler URL in export limits edit.");
		}
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
	const auto shared = std::make_shared<base::weak_qptr<Ui::CalendarBox>>();
	const auto finalize = [=](not_null<Ui::CalendarBox*> box) {
		box->addLeftButton(std::move(resetLabel), crl::guard(this, [=] {
			done(0);
			if (const auto weak = shared->get()) {
				weak->closeBox();
			}
		}));
	};
	const auto callback = crl::guard(this, [=](
			const QDate &date,
			Fn<void()> close) {
		done(base::unixtime::serialize(date.startOfDay()));
		close();
	});
	auto box = Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = month,
		.highlighted = highlighted,
		.callback = callback,
		.finalize = finalize,
		.st = st::exportCalendarSizes,
		.minDate = (min
			? base::unixtime::parse(min).date()
			: QDate(2013, 8, 1)), // Telegram was launched in August 2013 :)
		.maxDate = (max
			? base::unixtime::parse(max).date()
			: QDate::currentDate()),
	});
	*shared = base::make_weak(box.data());
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

	value() | rpl::map([=](const Settings &data) {
		const auto selected = data.topicSelection.find(_singlePeerId.value);
		return ((data.types != Types(0)) || data.onlySinglePeer())
			&& (!data.splitTopics
				|| selected == end(data.topicSelection)
				|| !selected->second.empty());
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool canStart) {
		refreshButtons(buttons, canStart);
		topShadow->raise();
		bottomShadow->raise();
	}, buttons->lifetime());

	sizeValue(
	) | rpl::on_next([=](QSize size) {
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
	) | rpl::on_next([=](bool checked) {
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
	) | rpl::on_next([=](bool checked) {
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
	const auto files = addMediaOption(
		container,
		tr::lng_export_option_files(tr::now),
		MediaType::File);
	addSizeSlider(container, files);
}

not_null<Ui::Checkbox*> SettingsWidget::addMediaOption(
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
	) | rpl::on_next([=](bool checked) {
		changeData([&](Settings &data) {
			if (checked) {
				data.media.types |= type;
			} else {
				data.media.types &= ~type;
			}
		});
	}, checkbox->lifetime());
	return checkbox;
}

void SettingsWidget::addSizeSlider(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::Checkbox*> above) {
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(container));
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::exportFileSizeSlider),
		st::exportFileSizePadding);
	slider->resize(st::exportFileSizeSlider.seekSize);
	slider->setPseudoDiscrete(
		kSizeValueCount,
		SizeLimitByIndex,
		readData().media.sizeLimit,
		[=](int64 limit) {
			changeData([&](Settings &data) {
				data.media.sizeLimit = limit;
			});
		});

	const auto label = Ui::CreateChild<Ui::LabelSimple>(
		container.get(),
		st::exportFileSizeLabel);
	value() | rpl::map([](const Settings &data) {
		return data.media.sizeLimit;
	}) | rpl::on_next([=](int64 sizeLimit) {
		const auto limit = sizeLimit / kMegabyte;
		const auto size = QString::number(limit) + " MB";
		const auto text = tr::lng_export_option_size_limit(
			tr::now,
			lt_size,
			size);
		label->setText(text);
	}, slider->lifetime());

	_sizeLimitExtraHeight = SizeLimitFitsOptionLine(
		st::exportPanelSize.width(),
		above->naturalWidth(),
		label->width())
		? 0
		: label->height();

	rpl::combine(
		container->widthValue(),
		label->widthValue(),
		slider->geometryValue()
	) | rpl::on_next([=](int outerWidth, int labelWidth, QRect geometry) {
		const auto sameLine = SizeLimitFitsOptionLine(
			outerWidth,
			above->naturalWidth(),
			labelWidth);
		wrap->resize(wrap->width(), sameLine ? 0 : label->height());
		label->moveToRight(
			st::exportFileSizePadding.right(),
			(sameLine
				? (geometry.y()
					- label->height()
					- st::exportFileSizeLabelBottom)
				: wrap->y()),
			outerWidth);
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
		) | rpl::on_next([=](QSize size) {
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
	) | rpl::on_next([=](QSize size, int width) {
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

int SettingsWidget::sizeLimitExtraHeight() const {
	return _sizeLimitExtraHeight;
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

void SettingsWidget::chooseForums() {
	_showBoxCallback(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_export_choose_topics());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		const auto api = box->lifetime().make_state<MTP::Sender>(
			&_session->mtp());
		struct State {
			TimeId offsetDate = 0;
			int32 offsetId = 0;
			MTPInputPeer offsetPeer = MTP_inputPeerEmpty();
			base::flat_set<uint64> seen;
			int forums = 0;
			Fn<void()> load;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_export_forums_loading(tr::now),
			st::exportAboutOptionLabel));
		state->load = [=] {
			api->request(MTPmessages_GetDialogs(
				MTP_flags(state->offsetId
					? MTPmessages_GetDialogs::Flag::f_exclude_pinned
					: MTPmessages_GetDialogs::Flags(0)),
				MTPint(),
				MTP_int(state->offsetDate),
				MTP_int(state->offsetId),
				state->offsetPeer,
				MTP_int(100),
				MTP_long(0)
			)).done(crl::guard(this, [=](const MTPmessages_Dialogs &result) {
				if (result.type() == mtpc_messages_dialogsNotModified) {
					status->setText(tr::lng_export_topics_failed(tr::now));
					return;
				}
				const auto parsed = Data::ParseDialogsInfo(result);
				for (const auto &chat : parsed.chats) {
					if (!chat.isForum
						|| !state->seen.emplace(chat.peerId.value).second) {
						continue;
					}
					const auto type = (chat.type
							== Data::DialogInfo::Type::PublicSupergroup)
						? Type::PublicGroups
						: Type::PrivateGroups;
					if (!(readData().types & type)) {
						continue;
					}
					++state->forums;
					const auto button = box->addRow(
						object_ptr<Ui::RoundButton>(
							box,
							rpl::single(QString::fromUtf8(chat.name)),
							st::defaultBoxButton));
					button->setClickedCallback(crl::guard(this, [=] {
						chooseForumTopics(chat);
					}));
				}
				if (parsed.chats.empty()
					|| result.type() == mtpc_messages_dialogs) {
					if (state->forums) {
						status->hide();
					} else {
						status->setText(tr::lng_export_forums_empty(tr::now));
					}
					return;
				}
				const auto &last = parsed.chats.back();
				if (last.topMessageId == state->offsetId
					&& last.peerId == ReadPeerId(_session, state->offsetPeer)) {
					status->setText(tr::lng_export_topics_failed(tr::now));
					return;
				}
				state->offsetDate = last.topMessageDate;
				state->offsetId = last.topMessageId;
				state->offsetPeer = last.input;
				state->load();
			})).fail([=](const MTP::Error &) {
				status->setText(tr::lng_export_topics_failed(tr::now));
			}).send();
		};
		state->load();
	}));
}

void SettingsWidget::chooseForumTopics(const Data::DialogInfo &chat) {
	_showBoxCallback(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8(chat.name)));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		const auto api = box->lifetime().make_state<MTP::Sender>(
			&_session->mtp());
		struct State {
			TimeId offsetDate = 0;
			int32 offsetId = 0;
			int32 offsetTopicId = 0;
			bool updating = false;
			bool defaultChecked = false;
			base::flat_map<int32, not_null<Ui::Checkbox*>> rows;
			Fn<void()> load;
			Fn<void(const MTPmessages_ForumTopics&)> append;
			Fn<void()> finish;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto selected = readData().topicSelection.find(chat.peerId.value);
		const auto ids = (selected != end(readData().topicSelection))
			? selected->second
			: std::vector<int32>();
		state->defaultChecked = selected == end(readData().topicSelection);
		const auto all = box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_export_all_topics(tr::now),
			state->defaultChecked,
			st::defaultBoxCheckbox),
			st::exportSettingPadding);
		all->setDisabled(true);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_export_topics_about(tr::now),
			st::exportAboutOptionLabel),
			st::exportAboutOptionPadding);
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_export_topics_loading(tr::now),
			st::exportAboutOptionLabel));
		all->checkedChanges() | rpl::on_next([=](bool checked) {
			if (state->updating) {
				return;
			}
			state->defaultChecked = checked;
			state->updating = true;
			for (const auto &[id, row] : state->rows) {
				row->setChecked(checked);
			}
			state->updating = false;
		}, all->lifetime());
		state->append = [=](const MTPmessages_ForumTopics &result) {
			const auto slice = Data::ParseForumTopicsSlice(result);
			for (const auto &topic : slice.list) {
				if (state->rows.contains(topic.rootId)) {
					continue;
				}
				const auto row = box->addRow(object_ptr<Ui::Checkbox>(
					box,
					QString::fromUtf8(topic.title),
					state->defaultChecked || ranges::contains(ids, topic.rootId),
					st::defaultBoxCheckbox),
					st::exportSettingPadding);
				state->rows.emplace(topic.rootId, row);
				row->setDisabled(true);
				row->checkedChanges() | rpl::on_next([=](bool checked) {
					if (!state->updating && !checked && all->checked()) {
						state->updating = true;
						all->setChecked(false);
						state->updating = false;
					}
				}, row->lifetime());
			}
		};
		state->finish = crl::guard(this, [=] {
			status->hide();
			all->setDisabled(false);
			for (const auto &[id, row] : state->rows) {
				row->setDisabled(false);
			}
			box->addButton(tr::lng_settings_save(), crl::guard(this, [=] {
				changeData([&](Settings &settings) {
					if (all->checked()) {
						settings.topicSelection.erase(chat.peerId.value);
					} else {
						auto &ids = settings.topicSelection[chat.peerId.value];
						ids.clear();
						for (const auto &[id, row] : state->rows) {
							if (row->checked()) {
								ids.push_back(id);
							}
						}
					}
				});
				box->closeBox();
			}));
		});
		state->load = [=] {
			api->request(MTPmessages_GetForumTopics(
				MTP_flags(0),
				chat.input,
				MTPstring(),
				MTP_int(state->offsetDate),
				MTP_int(state->offsetId),
				MTP_int(state->offsetTopicId),
				MTP_int(100)
			)).done([=](const MTPmessages_ForumTopics &result) {
				const auto slice = Data::ParseForumTopicsSlice(result);
				if (slice.list.empty() && !result.data().vtopics().v.isEmpty()) {
					status->setText(tr::lng_export_topics_failed(tr::now));
					return;
				}
				state->append(result);
				if (!slice.list.empty()
					&& std::tie(slice.offsetDate, slice.offsetId, slice.offsetTopicId)
						== std::tie(state->offsetDate,
							state->offsetId,
							state->offsetTopicId)) {
					status->setText(tr::lng_export_topics_failed(tr::now));
					return;
				}
				if (slice.list.empty()) {
					if (state->rows.contains(1)) {
						state->finish();
					} else {
						api->request(MTPmessages_GetForumTopicsByID(
							chat.input,
							MTP_vector<MTPint>(1, MTP_int(1))
						)).done([=](const MTPmessages_ForumTopics &result) {
							state->append(result);
							if (!state->rows.contains(1)) {
								status->setText(tr::lng_export_topics_failed(tr::now));
								return;
							}
							state->finish();
						}).fail([=](const MTP::Error &) {
							status->setText(tr::lng_export_topics_failed(tr::now));
						}).send();
					}
					return;
				}
				state->offsetDate = slice.offsetDate;
				state->offsetId = slice.offsetId;
				state->offsetTopicId = slice.offsetTopicId;
				state->load();
			}).fail([=](const MTP::Error &) {
				status->setText(tr::lng_export_topics_failed(tr::now));
			}).send();
		};
		state->load();
	}));
}

} // namespace View
} // namespace Export
