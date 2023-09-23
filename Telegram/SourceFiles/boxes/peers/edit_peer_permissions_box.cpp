/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_permissions_box.h"

#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "settings/settings_power_saving.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kSlowmodeValues = 7;
constexpr auto kSuggestGigagroupThreshold = 199000;
constexpr auto kForceDisableTooltipDuration = 3 * crl::time(1000);

[[nodiscard]] auto Dependencies(PowerSaving::Flags)
-> std::vector<std::pair<PowerSaving::Flag, PowerSaving::Flag>> {
	return {};
}

[[nodiscard]] auto NestedRestrictionLabelsList(
	Data::RestrictionsSetOptions options)
-> std::vector<NestedEditFlagsLabels<ChatRestrictions>> {
	using Flag = ChatRestriction;

	auto first = std::vector<RestrictionLabel>{
		{ Flag::SendOther, tr::lng_rights_chat_send_text(tr::now) },
	};
	auto media = std::vector<RestrictionLabel>{
		{ Flag::SendPhotos, tr::lng_rights_chat_photos(tr::now) },
		{ Flag::SendVideos, tr::lng_rights_chat_videos(tr::now) },
		{ Flag::SendVideoMessages, tr::lng_rights_chat_video_messages(tr::now) },
		{ Flag::SendMusic, tr::lng_rights_chat_music(tr::now) },
		{ Flag::SendVoiceMessages, tr::lng_rights_chat_voice_messages(tr::now) },
		{ Flag::SendFiles, tr::lng_rights_chat_files(tr::now) },
		{ Flag::SendStickers
			| Flag::SendGifs
			| Flag::SendGames
			| Flag::SendInline, tr::lng_rights_chat_stickers(tr::now) },
		{ Flag::EmbedLinks, tr::lng_rights_chat_send_links(tr::now) },
		{ Flag::SendPolls, tr::lng_rights_chat_send_polls(tr::now) },
	};
	auto second = std::vector<RestrictionLabel>{
		{ Flag::AddParticipants, tr::lng_rights_chat_add_members(tr::now) },
		{ Flag::CreateTopics, tr::lng_rights_group_add_topics(tr::now) },
		{ Flag::PinMessages, tr::lng_rights_group_pin(tr::now) },
		{ Flag::ChangeInfo, tr::lng_rights_group_info(tr::now) },
	};
	if (!options.isForum) {
		second.erase(
			ranges::remove(
				second,
				Flag::CreateTopics | Flag(),
				&RestrictionLabel::flags),
			end(second));
	}
	return {
		{ std::nullopt, std::move(first) },
		{ tr::lng_rights_chat_send_media(), std::move(media) },
		{ std::nullopt, std::move(second) },
	};
}

[[nodiscard]] auto NestedAdminRightLabels(
	Data::AdminRightsSetOptions options)
-> std::vector<NestedEditFlagsLabels<ChatAdminRights>> {
	using Flag = ChatAdminRight;

	if (options.isGroup) {
		auto result = std::vector<AdminRightLabel>{
			{ Flag::ChangeInfo, tr::lng_rights_group_info(tr::now) },
			{ Flag::DeleteMessages, tr::lng_rights_group_delete(tr::now) },
			{ Flag::BanUsers, tr::lng_rights_group_ban(tr::now) },
			{ Flag::InviteByLinkOrAdd, options.anyoneCanAddMembers
				? tr::lng_rights_group_invite_link(tr::now)
				: tr::lng_rights_group_invite(tr::now) },
			{ Flag::ManageTopics, tr::lng_rights_group_topics(tr::now) },
			{ Flag::PinMessages, tr::lng_rights_group_pin(tr::now) },
			{ Flag::ManageCall, tr::lng_rights_group_manage_calls(tr::now) },
			{ Flag::Anonymous, tr::lng_rights_group_anonymous(tr::now) },
			{ Flag::AddAdmins, tr::lng_rights_add_admins(tr::now) },
		};
		if (!options.isForum) {
			result.erase(
				ranges::remove(
					result,
					Flag::ManageTopics | Flag(),
					&AdminRightLabel::flags),
				end(result));
		}
		return { { std::nullopt, std::move(result) } };
	}
	auto first = std::vector<AdminRightLabel>{
		{ Flag::ChangeInfo, tr::lng_rights_channel_info(tr::now) },
	};
	auto messages = std::vector<AdminRightLabel>{
		{ Flag::PostMessages, tr::lng_rights_channel_post(tr::now) },
		{ Flag::EditMessages, tr::lng_rights_channel_edit(tr::now) },
		{ Flag::DeleteMessages, tr::lng_rights_channel_delete(tr::now) },
	};
	auto stories = std::vector<AdminRightLabel>{
		{ Flag::PostStories, tr::lng_rights_channel_post_stories(tr::now) },
		{ Flag::EditStories, tr::lng_rights_channel_edit_stories(tr::now) },
		{ Flag::DeleteStories, tr::lng_rights_channel_delete_stories(tr::now) },
	};
	auto second = std::vector<AdminRightLabel>{
		{ Flag::InviteByLinkOrAdd, tr::lng_rights_group_invite(tr::now) },
		{ Flag::ManageCall, tr::lng_rights_channel_manage_calls(tr::now) },
		{ Flag::AddAdmins, tr::lng_rights_add_admins(tr::now) },
	};
	return {
		{ std::nullopt, std::move(first) },
		{ tr::lng_rights_channel_manage(), std::move(messages) },
		{ tr::lng_rights_channel_manage_stories(), std::move(stories) },
		{ std::nullopt, std::move(second) },
	};
}

int SlowmodeDelayByIndex(int index) {
	Expects(index >= 0 && index < kSlowmodeValues);

	switch (index) {
	case 0: return 0;
	case 1: return 10;
	case 2: return 30;
	case 3: return 60;
	case 4: return 5 * 60;
	case 5: return 15 * 60;
	case 6: return 60 * 60;
	}
	Unexpected("Index in SlowmodeDelayByIndex.");
}

template <typename CheckboxesMap, typename DependenciesMap>
void ApplyDependencies(
		const CheckboxesMap &checkboxes,
		const DependenciesMap &dependencies,
		Ui::AbstractCheckView *changed) {
	const auto checkAndApply = [&](
			auto &&current,
			auto dependency,
			bool isChecked) {
		for (auto &&checkbox : checkboxes) {
			if ((checkbox.first & dependency)
				&& (checkbox.second->checked() == isChecked)) {
				current->setChecked(isChecked, anim::type::normal);
				return true;
			}
		}
		return false;
	};
	const auto applySomeDependency = [&] {
		auto result = false;
		for (auto &&entry : checkboxes) {
			if (entry.second == changed) {
				continue;
			}
			auto isChecked = entry.second->checked();
			for (auto &&dependency : dependencies) {
				const auto check = isChecked
					? dependency.first
					: dependency.second;
				if (entry.first & check) {
					if (checkAndApply(
							entry.second,
							(isChecked
								? dependency.second
								: dependency.first),
							!isChecked)) {
						result = true;
						break;
					}
				}
			}
		}
		return result;
	};

	const auto maxFixesCount = int(checkboxes.size());
	for (auto i = 0; i != maxFixesCount; ++i) {
		if (!applySomeDependency()) {
			break;
		}
	};
}

auto Dependencies(ChatRestrictions)
-> std::vector<std::pair<ChatRestriction, ChatRestriction>> {
	using Flag = ChatRestriction;

	return {
		// stickers <-> gifs
		{ Flag::SendGifs, Flag::SendStickers },
		{ Flag::SendStickers, Flag::SendGifs },

		// stickers <-> games
		{ Flag::SendGames, Flag::SendStickers },
		{ Flag::SendStickers, Flag::SendGames },

		// stickers <-> inline
		{ Flag::SendInline, Flag::SendStickers },
		{ Flag::SendStickers, Flag::SendInline },

		// embed_links -> send_plain
		{ Flag::EmbedLinks, Flag::SendOther },

		// send_* -> view_messages
		{ Flag::SendStickers, Flag::ViewMessages },
		{ Flag::SendGifs, Flag::ViewMessages },
		{ Flag::SendGames, Flag::ViewMessages },
		{ Flag::SendInline, Flag::ViewMessages },
		{ Flag::SendPolls, Flag::ViewMessages },
		{ Flag::SendPhotos, Flag::ViewMessages },
		{ Flag::SendVideos, Flag::ViewMessages },
		{ Flag::SendVideoMessages, Flag::ViewMessages },
		{ Flag::SendMusic, Flag::ViewMessages },
		{ Flag::SendVoiceMessages, Flag::ViewMessages },
		{ Flag::SendFiles, Flag::ViewMessages },
		{ Flag::SendOther, Flag::ViewMessages },
	};
}

ChatRestrictions NegateRestrictions(ChatRestrictions value) {
	using Flag = ChatRestriction;

	return (~value) & (Flag(0)
		// view_messages is always allowed, so it is never in restrictions.
		//| Flag::ViewMessages
		| Flag::ChangeInfo
		| Flag::EmbedLinks
		| Flag::AddParticipants
		| Flag::CreateTopics
		| Flag::PinMessages
		| Flag::SendGames
		| Flag::SendGifs
		| Flag::SendInline
		| Flag::SendPolls
		| Flag::SendStickers
		| Flag::SendPhotos
		| Flag::SendVideos
		| Flag::SendVideoMessages
		| Flag::SendMusic
		| Flag::SendVoiceMessages
		| Flag::SendFiles
		| Flag::SendOther);
}

auto Dependencies(ChatAdminRights)
-> std::vector<std::pair<ChatAdminRight, ChatAdminRight>> {
	return {};
}

auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

ChatRestrictions DisabledByAdminRights(not_null<PeerData*> peer) {
	using Flag = ChatRestriction;
	using Admin = ChatAdminRight;
	using Admins = ChatAdminRights;

	const auto adminRights = [&] {
		const auto full = ~Admins(0);
		if (const auto chat = peer->asChat()) {
			return chat->amCreator() ? full : chat->adminRights();
		} else if (const auto channel = peer->asChannel()) {
			return channel->amCreator() ? full : channel->adminRights();
		}
		Unexpected("User in DisabledByAdminRights.");
	}();
	return Flag(0)
		| ((adminRights & Admin::ManageTopics)
			? Flag(0)
			: Flag::CreateTopics)
		| ((adminRights & Admin::PinMessages)
			? Flag(0)
			: Flag::PinMessages)
		| ((adminRights & Admin::InviteByLinkOrAdd)
			? Flag(0)
			: Flag::AddParticipants)
		| ((adminRights & Admin::ChangeInfo)
			? Flag(0)
			: Flag::ChangeInfo);
}

not_null<Ui::RpWidget*> AddInnerToggle(
		not_null<Ui::VerticalLayout*> container,
		const style::SettingsButton &st,
		std::vector<not_null<Ui::AbstractCheckView*>> innerCheckViews,
		not_null<Ui::SlideWrap<>*> wrap,
		rpl::producer<QString> buttonLabel,
		std::optional<QString> locked,
		Settings::IconDescriptor &&icon) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		nullptr,
		st));
	if (icon) {
		Settings::AddButtonIcon(button, st, std::move(icon));
	}

	const auto toggleButton = Ui::CreateChild<Ui::SettingsButton>(
		container.get(),
		nullptr,
		st);

	struct State final {
		State(const style::Toggle &st, Fn<void()> c)
		: checkView(st, false, c) {
		}
		Ui::ToggleView checkView;
		Ui::Animations::Simple animation;
		rpl::event_stream<> anyChanges;
		std::vector<not_null<Ui::AbstractCheckView*>> innerChecks;
	};
	const auto state = button->lifetime().make_state<State>(
		st.toggle,
		[=] { toggleButton->update(); });
	state->innerChecks = std::move(innerCheckViews);
	const auto countChecked = [=] {
		return ranges::count_if(
			state->innerChecks,
			[](const auto &v) { return v->checked(); });
	};
	for (const auto &innerCheck : state->innerChecks) {
		innerCheck->checkedChanges(
		) | rpl::to_empty | rpl::start_to_stream(
			state->anyChanges,
			button->lifetime());
	}
	const auto checkView = &state->checkView;
	{
		const auto separator = Ui::CreateChild<Ui::RpWidget>(container.get());
		separator->paintRequest(
		) | rpl::start_with_next([=, bg = st.textBgOver] {
			auto p = QPainter(separator);
			p.fillRect(separator->rect(), bg);
		}, separator->lifetime());
		const auto separatorHeight = 2 * st.toggle.border
			+ st.toggle.diameter;
		button->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			const auto w = st::rightsButtonToggleWidth;
			toggleButton->setGeometry(
				r.x() + r.width() - w,
				r.y(),
				w,
				r.height());
			separator->setGeometry(
				toggleButton->x() - st::lineWidth,
				r.y() + (r.height() - separatorHeight) / 2,
				st::lineWidth,
				separatorHeight);
		}, toggleButton->lifetime());

		const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggleButton);
		checkWidget->resize(checkView->getSize());
		checkWidget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(checkWidget);
			checkView->paint(p, 0, 0, checkWidget->width());
		}, checkWidget->lifetime());
		toggleButton->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			checkWidget->moveToRight(
				st.toggleSkip,
				(s.height() - checkWidget->height()) / 2);
		}, toggleButton->lifetime());
	}
	state->anyChanges.events_starting_with(
		rpl::empty_value()
	) | rpl::map(countChecked) | rpl::start_with_next([=](int count) {
		checkView->setChecked(count > 0, anim::type::normal);
	}, toggleButton->lifetime());
	checkView->setLocked(locked.has_value());
	checkView->finishAnimating();

	const auto totalInnerChecks = state->innerChecks.size();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		rpl::combine(
			std::move(buttonLabel),
			state->anyChanges.events_starting_with(
				rpl::empty_value()
			) | rpl::map(countChecked)
		) | rpl::map([=](const QString &t, int checked) {
			auto count = Ui::Text::Bold("  "
				+ QString::number(checked)
				+ '/'
				+ QString::number(totalInnerChecks));
			return TextWithEntities::Simple(t).append(std::move(count));
		}));
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto arrow = Ui::CreateChild<Ui::RpWidget>(button);
	{
		const auto &icon = st::permissionsExpandIcon;
		arrow->resize(icon.size());
		arrow->paintRequest(
		) | rpl::start_with_next([=, &icon] {
			auto p = QPainter(arrow);
			const auto center = QPointF(
				icon.width() / 2.,
				icon.height() / 2.);
			const auto progress = state->animation.value(
				wrap->toggled() ? 1. : 0.);
			auto hq = std::optional<PainterHighQualityEnabler>();
			if (progress > 0.) {
				hq.emplace(p);
				p.translate(center);
				p.rotate(progress * 180.);
				p.translate(-center);
			}
			icon.paint(p, 0, 0, arrow->width());
		}, arrow->lifetime());
	}
	button->sizeValue(
	) | rpl::start_with_next([=, &st](const QSize &s) {
		const auto labelLeft = st.padding.left();
		const auto labelRight = s.width() - toggleButton->width();

		label->resizeToWidth(labelRight - labelLeft - arrow->width());
		label->moveToLeft(
			labelLeft,
			(s.height() - label->height()) / 2);
		arrow->moveToLeft(
			std::min(
				labelLeft + label->textMaxWidth(),
				labelRight - arrow->width()),
			(s.height() - arrow->height()) / 2);
	}, button->lifetime());
	wrap->toggledValue(
	) | rpl::skip(1) | rpl::start_with_next([=](bool toggled) {
		state->animation.start(
			[=] { arrow->update(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::slideWrapDuration);
	}, button->lifetime());

	const auto handleLocked = [=] {
		if (locked.has_value()) {
			Ui::Toast::Show(container, *locked);
			return true;
		}
		return false;
	};

	button->clicks(
	) | rpl::start_with_next([=] {
		if (!handleLocked()) {
			wrap->toggle(!wrap->toggled(), anim::type::normal);
		}
	}, button->lifetime());

	toggleButton->clicks(
	) | rpl::start_with_next([=] {
		if (!handleLocked()) {
			const auto checked = !checkView->checked();
			for (const auto &innerCheck : state->innerChecks) {
				innerCheck->setChecked(checked, anim::type::normal);
			}
		}
	}, toggleButton->lifetime());

	return button;
}

template <typename Flags>
[[nodiscard]] EditFlagsControl<Flags> CreateEditFlags(
		not_null<Ui::VerticalLayout*> container,
		Flags checked,
		EditFlagsDescriptor<Flags> &&descriptor) {
	struct State final {
		std::map<Flags, not_null<Ui::AbstractCheckView*>> checkViews;
		rpl::event_stream<> anyChanges;
		rpl::variable<QString> forceDisabledMessage;
		rpl::variable<bool> forceDisabled;
		base::flat_map<Flags, bool> realCheckedValues;
		base::weak_ptr<Ui::Toast::Instance> toast;
	};
	const auto state = container->lifetime().make_state<State>();
	if (descriptor.forceDisabledMessage) {
		state->forceDisabledMessage = std::move(
			descriptor.forceDisabledMessage);
		state->forceDisabled = state->forceDisabledMessage.value(
		) | rpl::map([=](const QString &message) {
			return !message.isEmpty();
		});

		state->forceDisabled.value(
		) | rpl::start_with_next([=](bool disabled) {
			if (disabled) {
				for (const auto &[flags, checkView] : state->checkViews) {
					checkView->setChecked(false, anim::type::normal);
				}
			} else {
				for (const auto &[flags, checkView] : state->checkViews) {
					if (const auto i = state->realCheckedValues.find(flags)
						; i != state->realCheckedValues.end()) {
						checkView->setChecked(
							i->second,
							anim::type::normal);
					}
				}
			}
		}, container->lifetime());
	}

	const auto &st = descriptor.st ? *descriptor.st : st::rightsButton;
	const auto value = [=] {
		auto result = Flags(0);
		for (const auto &[flags, checkView] : state->checkViews) {
			if (checkView->checked()) {
				result |= flags;
			} else {
				result &= ~flags;
			}
		}
		return result;
	};
	const auto applyDependencies = [=](Ui::AbstractCheckView *view) {
		static const auto dependencies = Dependencies(Flags());
		ApplyDependencies(state->checkViews, dependencies, view);
	};

	if (descriptor.header) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(descriptor.header),
				st::rightsHeaderLabel),
			st::rightsHeaderMargin);
	}
	const auto addCheckbox = [&](
			not_null<Ui::VerticalLayout*> verticalLayout,
			bool isInner,
			const EditFlagsLabel<Flags> &entry) {
		const auto flags = entry.flags;
		const auto lockedIt = ranges::find_if(
			descriptor.disabledMessages,
			[&](const auto &pair) { return (pair.first & flags) != 0; });
		const auto locked = (lockedIt != end(descriptor.disabledMessages))
			? std::make_optional(lockedIt->second)
			: std::nullopt;
		const auto realChecked = (checked & flags) != 0;
		state->realCheckedValues.emplace(flags, realChecked);
		const auto toggled = realChecked && !state->forceDisabled.current();

		const auto checkView = [&]() -> not_null<Ui::AbstractCheckView*> {
			if (isInner) {
				const auto checkbox = verticalLayout->add(
					object_ptr<Ui::Checkbox>(
						verticalLayout,
						entry.label,
						toggled,
						st::settingsCheckbox),
					st.padding);
				const auto button = Ui::CreateChild<Ui::RippleButton>(
					verticalLayout.get(),
					st::defaultRippleAnimation);
				button->stackUnder(checkbox);
				rpl::combine(
					verticalLayout->widthValue(),
					checkbox->geometryValue()
				) | rpl::start_with_next([=](int w, const QRect &r) {
					button->setGeometry(0, r.y(), w, r.height());
				}, button->lifetime());
				checkbox->setAttribute(Qt::WA_TransparentForMouseEvents);
				const auto checkView = checkbox->checkView();
				button->setClickedCallback([=] {
					checkView->setChecked(
						!checkView->checked(),
						anim::type::normal);
				});

				return checkView;
			} else {
				const auto button = Settings::AddButton(
					verticalLayout,
					rpl::single(entry.label),
					st,
					{ entry.icon });
				const auto toggle = Ui::CreateChild<Ui::RpWidget>(
					button.get());

				// Looks like a bug in Clang, fails to compile with 'auto&' below.
				rpl::lifetime &lifetime = toggle->lifetime();

				const auto checkView = lifetime.make_state<Ui::ToggleView>(
					st.toggle,
					toggled,
					[=] { toggle->update(); });
				toggle->resize(checkView->getSize());
				toggle->paintRequest(
				) | rpl::start_with_next([=] {
					auto p = QPainter(toggle);
					checkView->paint(p, 0, 0, toggle->width());
				}, toggle->lifetime());
				button->sizeValue(
				) | rpl::start_with_next([=](const QSize &s) {
					toggle->moveToRight(
						st.toggleSkip,
						(s.height() - toggle->height()) / 2);
				}, toggle->lifetime());
				button->setClickedCallback([=] {
					checkView->setChecked(
						!checkView->checked(),
						anim::type::normal);
				});
				checkView->setLocked(locked.has_value());
				return checkView;
			}
		}();
		state->checkViews.emplace(flags, checkView);
		checkView->checkedChanges(
		) | rpl::start_with_next([=](bool checked) {
			if (checked && state->forceDisabled.current()) {
				if (!state->toast) {
					state->toast = Ui::Toast::Show(container, {
						.text = { state->forceDisabledMessage.current() },
						.duration = kForceDisableTooltipDuration,
					});
				}
				checkView->setChecked(false, anim::type::instant);
			} else if (locked.has_value()) {
				if (checked != toggled) {
					if (!state->toast) {
						state->toast = Ui::Toast::Show(container, {
							.text = { *locked },
							.duration = kForceDisableTooltipDuration,
						});
					}
					checkView->setChecked(toggled, anim::type::instant);
				}
			} else {
				if (!state->forceDisabled.current()) {
					state->realCheckedValues[flags] = checked;
				}
				InvokeQueued(container, [=] {
					applyDependencies(checkView);
					state->anyChanges.fire({});
				});
			}
		}, verticalLayout->lifetime());

		return checkView;
	};
	for (const auto &nestedWithLabel : descriptor.labels) {
		Assert(!nestedWithLabel.nested.empty());

		const auto isInner = nestedWithLabel.nestingLabel.has_value();
		auto wrap = isInner
			? object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container))
			: object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>{ nullptr };
		const auto verticalLayout = wrap ? wrap->entity() : container.get();
		auto innerChecks = std::vector<not_null<Ui::AbstractCheckView*>>();
		for (const auto &entry : nestedWithLabel.nested) {
			const auto c = addCheckbox(verticalLayout, isInner, entry);
			if (isInner) {
				innerChecks.push_back(c);
			}
		}
		if (wrap) {
			const auto raw = wrap.data();
			raw->hide(anim::type::instant);
			AddInnerToggle(
				container,
				st,
				innerChecks,
				raw,
				*nestedWithLabel.nestingLabel,
				std::nullopt,
				{ nestedWithLabel.nested.front().icon });
			container->add(std::move(wrap));
			container->widthValue(
			) | rpl::start_with_next([=](int w) {
				raw->resizeToWidth(w);
			}, raw->lifetime());
		}
	}

	applyDependencies(nullptr);
	for (const auto &[flags, checkView] : state->checkViews) {
		checkView->finishAnimating();
	}

	return {
		nullptr,
		value,
		state->anyChanges.events() | rpl::map(value)
	};
}

void AddSlowmodeLabels(
		not_null<Ui::VerticalLayout*> container) {
	const auto labels = container->add(
		object_ptr<Ui::FixedHeightWidget>(container, st::normalFont->height),
		st::slowmodeLabelsMargin);
	for (auto i = 0; i != kSlowmodeValues; ++i) {
		const auto seconds = SlowmodeDelayByIndex(i);
		const auto label = Ui::CreateChild<Ui::LabelSimple>(
			labels,
			st::slowmodeLabel,
			(!seconds
				? tr::lng_rights_slowmode_off(tr::now)
				: (seconds < 60)
				? tr::lng_seconds_tiny(tr::now, lt_count, seconds)
				: (seconds < 3600)
				? tr::lng_minutes_tiny(tr::now, lt_count, seconds / 60)
				: tr::lng_hours_tiny(tr::now, lt_count,seconds / 3600)));
		rpl::combine(
			labels->widthValue(),
			label->widthValue()
		) | rpl::start_with_next([=](int outer, int inner) {
			const auto skip = st::localStorageLimitMargin;
			const auto size = st::localStorageLimitSlider.seekSize;
			const auto available = outer
				- skip.left()
				- skip.right()
				- size.width();
			const auto shift = (i == 0)
				? -(size.width() / 2)
				: (i + 1 == kSlowmodeValues)
				? (size.width() - (size.width() / 2) - inner)
				: (-inner / 2);
			const auto left = skip.left()
				+ (size.width() / 2)
				+ (i * available) / (kSlowmodeValues - 1)
				+ shift;
			label->moveToLeft(left, 0, outer);
		}, label->lifetime());
	}
}

Fn<int()> AddSlowmodeSlider(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	if (const auto chat = peer->asChat()) {
		if (!chat->amCreator()) {
			return [] { return 0; };
		}
	}
	const auto channel = peer->asChannel();
	auto &lifetime = container->lifetime();
	const auto secondsCount = lifetime.make_state<rpl::variable<int>>(
		channel ? channel->slowmodeSeconds() : 0);

	container->add(
		object_ptr<Ui::BoxContentDivider>(container),
		{ 0, st::infoProfileSkip, 0, st::infoProfileSkip });

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_rights_slowmode_header(),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);

	AddSlowmodeLabels(container);

	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::localStorageLimitSlider),
		st::localStorageLimitMargin);
	slider->resize(st::localStorageLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		kSlowmodeValues,
		SlowmodeDelayByIndex,
		secondsCount->current(),
		[=](int seconds) {
			(*secondsCount) = seconds;
		});

	auto hasSlowMode = secondsCount->value(
	) | rpl::map(
		_1 != 0
	) | rpl::distinct_until_changed();

	auto useSeconds = secondsCount->value(
	) | rpl::map(
		_1 < 60
	) | rpl::distinct_until_changed();

	auto interval = rpl::combine(
		std::move(useSeconds),
		tr::lng_rights_slowmode_interval_seconds(
			lt_count,
			secondsCount->value() | tr::to_count()),
		tr::lng_rights_slowmode_interval_minutes(
			lt_count,
			secondsCount->value() | rpl::map(_1 / 60.))
	) | rpl::map([](
			bool use,
			const QString &seconds,
			const QString &minutes) {
		return use ? seconds : minutes;
	});

	auto aboutText = rpl::combine(
		std::move(hasSlowMode),
		tr::lng_rights_slowmode_about(),
		tr::lng_rights_slowmode_about_interval(
			lt_interval,
			std::move(interval))
	) | rpl::map([](
			bool has,
			const QString &about,
			const QString &aboutInterval) {
		return has ? aboutInterval : about;
	});

	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(aboutText),
				st::boxDividerLabel),
			st::proxyAboutPadding),
		style::margins(0, st::infoProfileSkip, 0, st::infoProfileSkip));

	return [=] { return secondsCount->current(); };
}

void AddSuggestGigagroup(
		not_null<Ui::VerticalLayout*> container,
		Fn<void()> callback) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_rights_gigagroup_title(),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_rights_gigagroup_convert(),
		rpl::single(QString()),
		std::move(callback),
		st::manageGroupTopicsButton,
		{ &st::menuIconChatDiscuss }));

	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_rights_gigagroup_about(),
				st::boxDividerLabel),
			st::proxyAboutPadding),
		style::margins(0, st::infoProfileSkip, 0, st::infoProfileSkip));
}

void AddBannedButtons(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (!chat->amCreator()) {
			return;
		}
	}
	const auto channel = peer->asChannel();
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_manage_peer_exceptions(),
		(channel
			? Info::Profile::RestrictedCountValue(channel)
			: rpl::single(0)) | ToPositiveNumberString(),
		[=] {
			ParticipantsBoxController::Start(
				navigation,
				peer,
				ParticipantsBoxController::Role::Restricted);
		},
		st::manageGroupTopicsButton,
		{ &st::menuIconPermissions }));
	if (channel) {
		container->add(EditPeerInfoBox::CreateButton(
			container,
			tr::lng_manage_peer_removed_users(),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					peer,
					ParticipantsBoxController::Role::Kicked);
			},
			st::manageGroupTopicsButton,
			{ &st::menuIconRemove }));
	}
}

} // namespace

void ShowEditPeerPermissionsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> channelOrGroup,
		Fn<void(EditPeerPermissionsBoxResult)> done) {
	const auto peer = channelOrGroup->migrateToOrMe();

	box->setTitle(tr::lng_manage_peer_permissions());

	const auto inner = box->verticalLayout();

	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	const auto disabledByAdminRights = DisabledByAdminRights(peer);
	const auto restrictions = FixDependentRestrictions([&] {
		if (const auto chat = peer->asChat()) {
			return chat->defaultRestrictions()
				| disabledByAdminRights;
		} else if (const auto channel = peer->asChannel()) {
			return channel->defaultRestrictions()
				| (channel->isPublic()
					? (Flag::ChangeInfo | Flag::PinMessages)
					: Flags(0))
				| disabledByAdminRights;
		}
		Unexpected("User in EditPeerPermissionsBox.");
	}());
	const auto disabledMessages = [&] {
		auto result = base::flat_map<Flags, QString>();
		result.emplace(
			disabledByAdminRights,
			tr::lng_rights_permission_cant_edit(tr::now));
		if (const auto channel = peer->asChannel()) {
			if (channel->isPublic()
				|| (channel->isMegagroup() && channel->linkedChat())) {
				result.emplace(
					Flag::ChangeInfo | Flag::PinMessages,
					tr::lng_rights_permission_unavailable(tr::now));
			}
		}
		return result;
	}();

	auto [checkboxes, getRestrictions, changes] = CreateEditRestrictions(
		inner,
		tr::lng_rights_default_restrictions_header(),
		restrictions,
		disabledMessages,
		{ .isForum = peer->isForum() });

	inner->add(std::move(checkboxes));

	const auto getSlowmodeSeconds = AddSlowmodeSlider(inner, peer);

	if (const auto channel = peer->asChannel()) {
		if (channel->amCreator()
			&& channel->membersCount() >= kSuggestGigagroupThreshold) {
			AddSuggestGigagroup(
				inner,
				AboutGigagroupCallback(
					peer->asChannel(),
					navigation->parentController()));
		}
	}

	AddBannedButtons(inner, navigation, peer);

	box->addButton(tr::lng_settings_save(), [=, rights = getRestrictions] {
		done({ rights(), getSlowmodeSeconds() });
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	box->setWidth(st::boxWideWidth);
}

Fn<void()> AboutGigagroupCallback(
		not_null<ChannelData*> channel,
		not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);

	const auto converting = std::make_shared<bool>();
	const auto convertSure = [=] {
		if (*converting) {
			return;
		}
		*converting = true;
		channel->session().api().request(MTPchannels_ConvertToGigagroup(
			channel->inputChannel
		)).done([=](const MTPUpdates &result) {
			channel->session().api().applyUpdates(result);
			if (const auto strong = weak.get()) {
				strong->window().hideSettingsAndLayer();
				strong->showToast(tr::lng_gigagroup_done(tr::now));
			}
		}).fail([=] {
			*converting = false;
		}).send();
	};
	const auto convertWarn = [=] {
		const auto strong = weak.get();
		if (*converting || !strong) {
			return;
		}
		strong->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::lng_gigagroup_warning_title());
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_gigagroup_warning(
					) | Ui::Text::ToRichLangValue(),
					st::infoAboutGigagroup));
			box->addButton(tr::lng_gigagroup_convert_sure(), convertSure);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	};
	return [=] {
		const auto strong = weak.get();
		if (*converting || !strong) {
			return;
		}
		strong->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::lng_gigagroup_convert_title());
			const auto addFeature = [&](rpl::producer<QString> text) {
				using namespace rpl::mappers;
				const auto prefix = QString::fromUtf8("\xE2\x80\xA2 ");
				box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						std::move(text) | rpl::map(prefix + _1),
						st::infoAboutGigagroup),
					style::margins(
						st::boxRowPadding.left(),
						st::boxLittleSkip,
						st::boxRowPadding.right(),
						st::boxLittleSkip));
			};
			addFeature(tr::lng_gigagroup_convert_feature1());
			addFeature(tr::lng_gigagroup_convert_feature2());
			addFeature(tr::lng_gigagroup_convert_feature3());
			box->addButton(tr::lng_gigagroup_convert_sure(), convertWarn);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	};
}

std::vector<RestrictionLabel> RestrictionLabels(
		Data::RestrictionsSetOptions options) {
	auto result = std::vector<RestrictionLabel>();
	for (const auto &[_, r] : NestedRestrictionLabelsList(options)) {
		result.insert(result.end(), r.begin(), r.end());
	}
	return result;
}

std::vector<AdminRightLabel> AdminRightLabels(
		Data::AdminRightsSetOptions options) {
	auto result = std::vector<AdminRightLabel>();
	for (const auto &[_, r] : NestedAdminRightLabels(options)) {
		result.insert(result.end(), r.begin(), r.end());
	}
	return result;
}

EditFlagsControl<ChatRestrictions> CreateEditRestrictions(
		QWidget *parent,
		rpl::producer<QString> header,
		ChatRestrictions restrictions,
		base::flat_map<ChatRestrictions, QString> disabledMessages,
		Data::RestrictionsSetOptions options) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	auto result = CreateEditFlags(
		widget.data(),
		NegateRestrictions(restrictions),
		{
			.header = std::move(header),
			.labels = NestedRestrictionLabelsList(options),
			.disabledMessages = std::move(disabledMessages),
		});
	result.widget = std::move(widget);
	result.value = [original = std::move(result.value)]{
		return NegateRestrictions(original());
	};
	result.changes = std::move(
		result.changes
	) | rpl::map(NegateRestrictions);

	return result;
}

EditFlagsControl<ChatAdminRights> CreateEditAdminRights(
		QWidget *parent,
		rpl::producer<QString> header,
		ChatAdminRights rights,
		base::flat_map<ChatAdminRights, QString> disabledMessages,
		Data::AdminRightsSetOptions options) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	auto result = CreateEditFlags(
		widget.data(),
		rights,
		{
			.header = std::move(header),
			.labels = NestedAdminRightLabels(options),
			.disabledMessages = std::move(disabledMessages),
		});
	result.widget = std::move(widget);

	return result;
}

ChatAdminRights DisabledByDefaultRestrictions(not_null<PeerData*> peer) {
	using Flag = ChatAdminRight;
	using Restriction = ChatRestriction;

	const auto restrictions = FixDependentRestrictions([&] {
		if (const auto chat = peer->asChat()) {
			return chat->defaultRestrictions();
		} else if (const auto channel = peer->asChannel()) {
			return channel->defaultRestrictions();
		}
		Unexpected("User in DisabledByDefaultRestrictions.");
	}());
	return Flag(0)
		| ((restrictions & Restriction::PinMessages)
			? Flag(0)
			: Flag::PinMessages)
		//
		// We allow to edit 'invite_users' admin right no matter what
		// is chosen in default permissions for 'invite_users', because
		// if everyone can 'invite_users' it handles invite link for admins.
		//
		//| ((restrictions & Restriction::AddParticipants)
		//	? Flag(0)
		//	: Flag::InviteByLinkOrAdd)
		//
		| ((restrictions & Restriction::ChangeInfo)
			? Flag(0)
			: Flag::ChangeInfo);
}

ChatRestrictions FixDependentRestrictions(ChatRestrictions restrictions) {
	const auto &dependencies = Dependencies(restrictions);

	// Fix iOS bug of saving send_inline like embed_links.
	// We copy send_stickers to send_inline.
	if (restrictions & ChatRestriction::SendStickers) {
		restrictions |= ChatRestriction::SendInline;
	} else {
		restrictions &= ~ChatRestriction::SendInline;
	}

	// Apply the strictest.
	const auto fixOne = [&] {
		for (const auto &[first, second] : dependencies) {
			if ((restrictions & second) && !(restrictions & first)) {
				restrictions |= first;
				return true;
			}
		}
		return false;
	};
	while (fixOne()) {
	}
	return restrictions;
}

ChatAdminRights AdminRightsForOwnershipTransfer(
		Data::AdminRightsSetOptions options) {
	auto result = ChatAdminRights();
	for (const auto &entry : AdminRightLabels(options)) {
		if (!(entry.flags & ChatAdminRight::Anonymous)) {
			result |= entry.flags;
		}
	}
	return result;
}

EditFlagsControl<PowerSaving::Flags> CreateEditPowerSaving(
		QWidget *parent,
		PowerSaving::Flags flags,
		rpl::producer<QString> forceDisabledMessage) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	auto descriptor = Settings::PowerSavingLabels();
	descriptor.forceDisabledMessage = std::move(forceDisabledMessage);
	auto result = CreateEditFlags(
		widget.data(),
		flags,
		std::move(descriptor));
	result.widget = std::move(widget);

	return result;
}
