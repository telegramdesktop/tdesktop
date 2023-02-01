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
#include "ui/toasts/common_toasts.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kSlowmodeValues = 7;
constexpr auto kSuggestGigagroupThreshold = 199000;

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

[[nodiscard]] std::vector<ChatRestrictions> MediaRestrictions() {
	return std::vector<ChatRestrictions>{
		ChatRestriction::SendPhotos,
		ChatRestriction::SendVideos,
		ChatRestriction::SendVideoMessages,
		ChatRestriction::SendMusic,
		ChatRestriction::SendVoiceMessages,
		ChatRestriction::SendFiles,
		ChatRestriction::SendStickers
			| ChatRestriction::SendGifs
			| ChatRestriction::SendGames
			| ChatRestriction::SendInline,
		ChatRestriction::EmbedLinks,
		ChatRestriction::SendPolls,
	};
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

not_null<Ui::SettingsButton*> SendMediaToggle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<int> checkedValue,
		int total,
		not_null<Ui::SlideWrap<>*> wrap,
		Fn<void(bool)> toggleMedia,
		std::optional<QString> locked) {
	const auto &stButton = st::rightsButton;
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(QString()),
		stButton));
	const auto toggleButton = Ui::CreateChild<Ui::SettingsButton>(
		container.get(),
		rpl::single(QString()),
		stButton);
	{
		const auto separator = Ui::CreateChild<Ui::RpWidget>(container.get());
		separator->paintRequest(
		) | rpl::start_with_next([=, bg = stButton.textBgOver] {
			auto p = QPainter(separator);
			p.fillRect(separator->rect(), bg);
		}, separator->lifetime());
		const auto separatorHeight = 2 * stButton.toggle.border
			+ stButton.toggle.diameter;
		button->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			const auto w = st::rightsButtonToggleWidth;
			constexpr auto kLineWidth = int(1);
			toggleButton->setGeometry(
				r.x() + r.width() - w,
				r.y(),
				w,
				r.height());
			separator->setGeometry(
				toggleButton->x() - kLineWidth,
				r.y() + (r.height() - separatorHeight) / 2,
				kLineWidth,
				separatorHeight);
		}, toggleButton->lifetime());
	}
	using namespace rpl::mappers;
	button->toggleOn(rpl::duplicate(checkedValue) | rpl::map(_1 > 0), true);
	toggleButton->toggleOn(button->toggledValue(), true);
	button->setToggleLocked(locked.has_value());
	toggleButton->setToggleLocked(locked.has_value());
	struct State final {
		Ui::Animations::Simple animation;
		rpl::lifetime finishAnimatingLifetime;
	};
	const auto state = button->lifetime().make_state<State>();
	rpl::duplicate(
		checkedValue
	) | rpl::start_with_next([=] {
		button->finishAnimating();
		toggleButton->finishAnimating();
	}, state->finishAnimatingLifetime);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		rpl::combine(
			tr::lng_rights_chat_send_media(),
			rpl::duplicate(checkedValue)
		) | rpl::map([total](const QString &t, int checked) {
			auto count = Ui::Text::Bold("  "
				+ QString::number(checked)
				+ '/'
				+ QString::number(total));
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
	) | rpl::start_with_next([=](const QSize &s) {
		const auto labelLeft = st::rightsButton.padding.left();
		const auto labelRight = s.width() - toggleButton->width();

		label->resizeToWidth(labelRight - labelLeft - arrow->width());
		label->moveToLeft(
			labelLeft,
			(s.height() - label->height()) / 2);
		arrow->moveToLeft(
			std::min(
				labelLeft + label->naturalWidth(),
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
			Ui::ShowMultilineToast({
				.parentOverride = container,
				.text = { *locked },
			});
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
			toggleMedia(!button->toggled());
			state->finishAnimatingLifetime.destroy();
		}
	}, toggleButton->lifetime());

	return button;
}

not_null<Ui::SettingsButton*> AddInnerCheckbox(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		bool toggled,
		rpl::producer<> toggledChanges) {
	class Button final : public Ui::SettingsButton {
	public:
		using Ui::SettingsButton::SettingsButton;

	protected:
		void paintEvent(QPaintEvent *e) override {
			Painter p(this);

			const auto paintOver = (isOver() || isDown()) && !isDisabled();
			Ui::SettingsButton::paintBg(p, e->rect(), paintOver);
			Ui::SettingsButton::paintRipple(p, 0, 0);
		}

	};

	const auto checkbox = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			text,
			toggled,
			st::settingsCheckbox),
		st::rightsButton.padding);
	const auto button = Ui::CreateChild<Button>(
		container.get(),
		rpl::single(QString()));
	button->stackUnder(checkbox);
	rpl::combine(
		container->widthValue(),
		checkbox->geometryValue()
	) | rpl::start_with_next([=](int w, const QRect &r) {
		button->setGeometry(0, r.y(), w, r.height());
	}, button->lifetime());
	checkbox->setAttribute(Qt::WA_TransparentForMouseEvents);
	std::move(
		toggledChanges
	) | rpl::start_with_next([=] {
		checkbox->setChecked(button->toggled());
	}, checkbox->lifetime());
	return button;
};

not_null<Ui::SettingsButton*> AddDefaultCheckbox(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		bool toggled) {
	const auto button = Settings::AddButton(
		container,
		rpl::single(text),
		st::rightsButton);
	return button;
};

template <
	typename Flags,
	typename DisabledMessagePairs,
	typename FlagLabelPairs,
	typename CheckboxFactory>
[[nodiscard]] EditFlagsControl<Flags, Ui::RpWidget> CreateEditFlags(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> header,
		Flags checked,
		const DisabledMessagePairs &disabledMessagePairs,
		const FlagLabelPairs &flagLabelPairs,
		CheckboxFactory checkboxFactory) {
	struct FlagCheck final {
		QPointer<Ui::SettingsButton> widget;
		rpl::event_stream<bool> checkChanges;
	};
	struct State final {
		std::map<Flags, FlagCheck> checkboxes;
		rpl::event_stream<> anyChanges;
	};
	const auto state = container->lifetime().make_state<State>();

	const auto value = [=] {
		auto result = Flags(0);
		for (const auto &[flags, checkbox] : state->checkboxes) {
			if (checkbox.widget->toggled()) {
				result |= flags;
			} else {
				result &= ~flags;
			}
		}
		return result;
	};

	const auto applyDependencies = [=](Ui::SettingsButton *changed) {
		static const auto dependencies = Dependencies(Flags());

		const auto checkAndApply = [&](
				auto &current,
				auto dependency,
				bool isChecked) {
			for (const auto &checkbox : state->checkboxes) {
				if ((checkbox.first & dependency)
					&& (checkbox.second.widget->toggled() == isChecked)) {
					current.checkChanges.fire_copy(isChecked);
					return true;
				}
			}
			return false;
		};
		const auto applySomeDependency = [&] {
			auto result = false;
			for (auto &entry : state->checkboxes) {
				if (entry.second.widget.data() == changed) {
					continue;
				}
				auto isChecked = entry.second.widget->toggled();
				for (const auto &dependency : dependencies) {
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

		const auto maxFixesCount = int(state->checkboxes.size());
		for (auto i = 0; i != maxFixesCount; ++i) {
			if (!applySomeDependency()) {
				break;
			}
		};
	};

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(header),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);

	auto addCheckbox = [&](Flags flags, const QString &text) {
		const auto lockedIt = ranges::find_if(
			disabledMessagePairs,
			[&](const auto &pair) { return (pair.first & flags) != 0; });
		const auto locked = (lockedIt != end(disabledMessagePairs))
			? std::make_optional(lockedIt->second)
			: std::nullopt;
		const auto toggled = ((checked & flags) != 0);
		auto flagCheck = state->checkboxes.emplace(flags, FlagCheck()).first;
		const auto control = checkboxFactory(
			container,
			flags,
			text,
			toggled,
			locked,
			[=](bool v) { flagCheck->second.checkChanges.fire_copy(v); });
		flagCheck->second.widget = Ui::MakeWeak(control);
		control->toggleOn(flagCheck->second.checkChanges.events());
		control->setToggleLocked(locked.has_value());
		control->toggledChanges(
		) | rpl::start_with_next([=](bool checked) {
			if (locked.has_value()) {
				if (checked != toggled) {
					Ui::ShowMultilineToast({
						.parentOverride = container,
						.text = { *locked },
					});
					flagCheck->second.checkChanges.fire_copy(toggled);
				}
			} else {
				InvokeQueued(control, [=] {
					applyDependencies(control);
					state->anyChanges.fire({});
				});
			}
		}, control->lifetime());
		flagCheck->second.checkChanges.fire_copy(toggled);
	};
	for (const auto &[flags, label] : flagLabelPairs) {
		addCheckbox(flags, label);
	}

	applyDependencies(nullptr);
	for (const auto &[flags, checkbox] : state->checkboxes) {
		checkbox.widget->finishAnimating();
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
		{ &st::settingsIconAskQuestion, Settings::kIconGreen }));

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
		{ &st::settingsIconKey, Settings::kIconLightOrange }));
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
			{ &st::settingsIconMinus, Settings::kIconRed }));
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
		auto result = std::map<Flags, QString>();
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
			if (const auto strongController = weak.get()) {
				strongController->window().hideSettingsAndLayer();
				Ui::ShowMultilineToast({
					.parentOverride = strongController->widget(),
					.text = { tr::lng_gigagroup_done(tr::now) },
				});
			}
		}).fail([=] {
			*converting = false;
		}).send();
	};
	const auto convertWarn = [=] {
		const auto strongController = weak.get();
		if (*converting || !strongController) {
			return;
		}
		strongController->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(tr::lng_gigagroup_warning_title());
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_gigagroup_warning(
					) | Ui::Text::ToRichLangValue(),
					st::infoAboutGigagroup));
			box->addButton(tr::lng_gigagroup_convert_sure(), convertSure);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}), Ui::LayerOption::KeepOther);
	};
	return [=] {
		const auto strongController = weak.get();
		if (*converting || !strongController) {
			return;
		}
		strongController->show(Box([=](not_null<Ui::GenericBox*> box) {
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
		}), Ui::LayerOption::KeepOther);
	};
}

std::vector<RestrictionLabel> RestrictionLabels(
		Data::RestrictionsSetOptions options) {
	using Flag = ChatRestriction;

	auto result = std::vector<RestrictionLabel>{
		{ Flag::SendOther, tr::lng_rights_chat_send_text(tr::now) },
		// { Flag::SendMedia, tr::lng_rights_chat_send_media(tr::now) },
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
		{ Flag::AddParticipants, tr::lng_rights_chat_add_members(tr::now) },
		{ Flag::CreateTopics, tr::lng_rights_group_add_topics(tr::now) },
		{ Flag::PinMessages, tr::lng_rights_group_pin(tr::now) },
		{ Flag::ChangeInfo, tr::lng_rights_group_info(tr::now) },
	};
	if (!options.isForum) {
		result.erase(
			ranges::remove(
				result,
				Flag::CreateTopics,
				&RestrictionLabel::flags),
			end(result));
	}
	return result;
}

std::vector<AdminRightLabel> AdminRightLabels(
		Data::AdminRightsSetOptions options) {
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
					Flag::ManageTopics,
					&AdminRightLabel::flags),
				end(result));
		}
		return result;
	} else {
		return {
			{ Flag::ChangeInfo, tr::lng_rights_channel_info(tr::now) },
			{ Flag::PostMessages, tr::lng_rights_channel_post(tr::now) },
			{ Flag::EditMessages, tr::lng_rights_channel_edit(tr::now) },
			{ Flag::DeleteMessages, tr::lng_rights_channel_delete(tr::now) },
			{ Flag::InviteByLinkOrAdd, tr::lng_rights_group_invite(tr::now) },
			{ Flag::ManageCall, tr::lng_rights_channel_manage_calls(tr::now) },
			{ Flag::AddAdmins, tr::lng_rights_add_admins(tr::now) }
		};
	}
}

EditFlagsControl<ChatRestrictions, Ui::RpWidget> CreateEditRestrictions(
		QWidget *parent,
		rpl::producer<QString> header,
		ChatRestrictions restrictions,
		std::map<ChatRestrictions, QString> disabledMessages,
		Data::RestrictionsSetOptions options) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	struct State {
		Ui::SlideWrap<Ui::VerticalLayout> *inner = nullptr;
		rpl::event_stream<ChatRestrictions> restrictions;
		std::vector<Fn<void(bool)>> mediaToggleCallbacks;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto mediaRestrictions = MediaRestrictions();
	const auto checkboxFactory = [&](
			not_null<Ui::VerticalLayout*> container,
			ChatRestrictions flags,
			const QString &text,
			bool toggled,
			std::optional<QString> locked,
			Fn<void(bool)> toggleCallback) {
		const auto isMedia = ranges::any_of(
			mediaRestrictions,
			[&](auto f) { return (flags & f); });
		if (isMedia) {
			state->mediaToggleCallbacks.push_back(toggleCallback);
			if (!state->inner) {
				auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container));
				wrap->hide(anim::type::instant);
				SendMediaToggle(
					container,
					state->restrictions.events_starting_with(
						ChatRestrictions(0)
					) | rpl::map([=](ChatRestrictions r) -> int {
						return (r == ChatRestrictions(0))
							? 0
							: ranges::count_if(
								mediaRestrictions,
								[&](auto f) { return !(r & f); });
					}),
					mediaRestrictions.size(),
					wrap.data(),
					[=](bool toggled) {
						for (auto &callback : state->mediaToggleCallbacks) {
							callback(toggled);
						}
					},
					locked);
				state->inner = container->add(std::move(wrap));
			}
			const auto checkbox = AddInnerCheckbox(
				state->inner->entity(),
				text,
				toggled,
				state->restrictions.events() | rpl::to_empty);
			return checkbox;
		} else {
			return AddDefaultCheckbox(container, text, toggled);
		}
	};
	auto result = CreateEditFlags(
		widget.data(),
		header,
		NegateRestrictions(restrictions),
		disabledMessages,
		RestrictionLabels(options),
		checkboxFactory);
	result.widget = std::move(widget);
	result.value = [original = std::move(result.value)]{
		return NegateRestrictions(original());
	};
	result.changes = std::move(
		result.changes
	) | rpl::map(NegateRestrictions);
	rpl::duplicate(
		result.changes
	) | rpl::start_to_stream(state->restrictions, state->inner->lifetime());
	result.widget->widthValue(
	) | rpl::start_with_next([=](int w) {
		state->inner->resizeToWidth(w);
	}, state->inner->lifetime());

	return result;
}

EditFlagsControl<ChatAdminRights, Ui::RpWidget> CreateEditAdminRights(
		QWidget *parent,
		rpl::producer<QString> header,
		ChatAdminRights rights,
		std::map<ChatAdminRights, QString> disabledMessages,
		Data::AdminRightsSetOptions options) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	const auto checkboxFactory = [&](
			not_null<Ui::VerticalLayout*> container,
			ChatAdminRights flags,
			const QString &text,
			bool toggled,
			std::optional<QString>,
			auto&&) {
		return AddDefaultCheckbox(container, text, toggled);
	};
	auto result = CreateEditFlags(
		widget.data(),
		header,
		rights,
		disabledMessages,
		AdminRightLabels(options),
		checkboxFactory);
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
	for (const auto &[flag, label] : AdminRightLabels(options)) {
		if (!(flag & ChatAdminRight::Anonymous)) {
			result |= flag;
		}
	}
	return result;
}
