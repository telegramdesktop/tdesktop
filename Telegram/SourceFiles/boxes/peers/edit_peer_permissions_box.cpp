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
#include "ui/wrap/vertical_layout.h"
#include "ui/layers/generic_box.h"
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
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

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

template <typename CheckboxesMap, typename DependenciesMap>
void ApplyDependencies(
		const CheckboxesMap &checkboxes,
		const DependenciesMap &dependencies,
		QPointer<Ui::Checkbox> changed) {
	const auto checkAndApply = [&](
			auto &&current,
			auto dependency,
			bool isChecked) {
		for (auto &&checkbox : checkboxes) {
			if ((checkbox.first & dependency)
				&& (checkbox.second->checked() == isChecked)) {
				current->setChecked(isChecked);
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

std::vector<std::pair<ChatRestrictions, QString>> RestrictionLabels() {
	const auto langKeys = {
		tr::lng_rights_chat_send_text,
		tr::lng_rights_chat_send_media,
		tr::lng_rights_chat_send_stickers,
		tr::lng_rights_chat_send_links,
		tr::lng_rights_chat_send_polls,
		tr::lng_rights_chat_add_members,
		tr::lng_rights_group_pin,
		tr::lng_rights_group_info,
	};

	std::vector<std::pair<ChatRestrictions, QString>> vector;
	const auto restrictions = Data::ListOfRestrictions();
	auto i = 0;
	for (const auto &key : langKeys) {
		vector.emplace_back(restrictions[i++], key(tr::now));
	}
	return vector;
}

std::vector<std::pair<ChatAdminRights, QString>> AdminRightLabels(
		bool isGroup,
		bool anyoneCanAddMembers) {
	using Flag = ChatAdminRight;

	if (isGroup) {
		return {
			{ Flag::f_change_info, tr::lng_rights_group_info(tr::now) },
			{ Flag::f_delete_messages, tr::lng_rights_group_delete(tr::now) },
			{ Flag::f_ban_users, tr::lng_rights_group_ban(tr::now) },
			{ Flag::f_invite_users, anyoneCanAddMembers
				? tr::lng_rights_group_invite_link(tr::now)
				: tr::lng_rights_group_invite(tr::now) },
			{ Flag::f_pin_messages, tr::lng_rights_group_pin(tr::now) },
			{ Flag::f_manage_call, tr::lng_rights_group_manage_calls(tr::now) },
			{ Flag::f_anonymous, tr::lng_rights_group_anonymous(tr::now) },
			{ Flag::f_add_admins, tr::lng_rights_add_admins(tr::now) },
		};
	} else {
		return {
			{ Flag::f_change_info, tr::lng_rights_channel_info(tr::now) },
			{ Flag::f_post_messages, tr::lng_rights_channel_post(tr::now) },
			{ Flag::f_edit_messages, tr::lng_rights_channel_edit(tr::now) },
			{ Flag::f_delete_messages, tr::lng_rights_channel_delete(tr::now) },
			{ Flag::f_invite_users, tr::lng_rights_group_invite(tr::now) },
			{ Flag::f_manage_call, tr::lng_rights_group_manage_calls(tr::now) },
			{ Flag::f_add_admins, tr::lng_rights_add_admins(tr::now) }
		};
	}
}

auto Dependencies(ChatRestrictions)
-> std::vector<std::pair<ChatRestriction, ChatRestriction>> {
	using Flag = ChatRestriction;

	return {
		// stickers <-> gifs
		{ Flag::f_send_gifs, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_gifs },

		// stickers <-> games
		{ Flag::f_send_games, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_games },

		// stickers <-> inline
		{ Flag::f_send_inline, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_inline },

		// stickers -> send_media
		{ Flag::f_send_stickers, Flag::f_send_messages },

		// embed_links -> send_media
		{ Flag::f_embed_links, Flag::f_send_messages },

		// send_media -> send_messages
		{ Flag::f_send_media, Flag::f_send_messages },

		// send_polls -> send_messages
		{ Flag::f_send_polls, Flag::f_send_messages },

		// send_messages -> view_messages
		{ Flag::f_send_messages, Flag::f_view_messages },
	};
}

ChatRestrictions NegateRestrictions(ChatRestrictions value) {
	using Flag = ChatRestriction;

	return (~value) & (Flag(0)
		// view_messages is always allowed, so it is never in restrictions.
		//| Flag::f_view_messages
		| Flag::f_change_info
		| Flag::f_embed_links
		| Flag::f_invite_users
		| Flag::f_pin_messages
		| Flag::f_send_games
		| Flag::f_send_gifs
		| Flag::f_send_inline
		| Flag::f_send_media
		| Flag::f_send_messages
		| Flag::f_send_polls
		| Flag::f_send_stickers);
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
		| ((adminRights & Admin::f_pin_messages)
			? Flag(0)
			: Flag::f_pin_messages)
		| ((adminRights & Admin::f_invite_users)
			? Flag(0)
			: Flag::f_invite_users)
		| ((adminRights & Admin::f_change_info)
			? Flag(0)
			: Flag::f_change_info);
}

} // namespace

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
		| ((restrictions & Restriction::f_pin_messages)
			? Flag(0)
			: Flag::f_pin_messages)
		//
		// We allow to edit 'invite_users' admin right no matter what
		// is chosen in default permissions for 'invite_users', because
		// if everyone can 'invite_users' it handles invite link for admins.
		//
		//| ((restrictions & Restriction::f_invite_users)
		//	? Flag(0)
		//	: Flag::f_invite_users)
		//
		| ((restrictions & Restriction::f_change_info)
			? Flag(0)
			: Flag::f_change_info);
}

ChatRestrictions FixDependentRestrictions(ChatRestrictions restrictions) {
	const auto &dependencies = Dependencies(restrictions);

	// Fix iOS bug of saving send_inline like embed_links.
	// We copy send_stickers to send_inline.
	if (restrictions & ChatRestriction::f_send_stickers) {
		restrictions |= ChatRestriction::f_send_inline;
	} else {
		restrictions &= ~ChatRestriction::f_send_inline;
	}

	// Apply the strictest.
	const auto fixOne = [&] {
		for (const auto [first, second] : dependencies) {
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

ChatAdminRights AdminRightsForOwnershipTransfer(bool isGroup) {
	auto result = ChatAdminRights();
	for (const auto &[flag, label] : AdminRightLabels(isGroup, true)) {
		if (!(flag & ChatAdminRight::f_anonymous)) {
			result |= flag;
		}
	}
	return result;
}

Fn<void()> AboutGigagroupCallback(not_null<ChannelData*> channel) {
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
			Ui::hideSettingsAndLayer();
			Ui::Toast::Show(tr::lng_gigagroup_done(tr::now));
		}).fail([=](const MTP::Error &error) {
			*converting = false;
		}).send();
	};
	const auto convertWarn = [=] {
		if (*converting) {
			return;
		}
		Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
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
		if (*converting) {
			return;
		}
		Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
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

EditPeerPermissionsBox::EditPeerPermissionsBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: _navigation(navigation)
, _peer(peer->migrateToOrMe()) {
}

auto EditPeerPermissionsBox::saveEvents() const -> rpl::producer<Result> {
	Expects(_save != nullptr);

	return _save->clicks() | rpl::map(_value);
}

void EditPeerPermissionsBox::prepare() {
	setTitle(tr::lng_manage_peer_permissions());

	const auto inner = setInnerWidget(object_ptr<Ui::VerticalLayout>(this));

	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	const auto disabledByAdminRights = DisabledByAdminRights(_peer);
	const auto restrictions = FixDependentRestrictions([&] {
		if (const auto chat = _peer->asChat()) {
			return chat->defaultRestrictions()
				| disabledByAdminRights;
		} else if (const auto channel = _peer->asChannel()) {
			return channel->defaultRestrictions()
				| (channel->isPublic()
					? (Flag::f_change_info | Flag::f_pin_messages)
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
		if (const auto channel = _peer->asChannel()) {
			if (channel->isPublic()) {
				result.emplace(
					Flag::f_change_info | Flag::f_pin_messages,
					tr::lng_rights_permission_unavailable(tr::now));
			}
		}
		return result;
	}();

	auto [checkboxes, getRestrictions, changes] = CreateEditRestrictions(
		this,
		tr::lng_rights_default_restrictions_header(),
		restrictions,
		disabledMessages);

	inner->add(std::move(checkboxes));

	const auto getSlowmodeSeconds = addSlowmodeSlider(inner);

	if (const auto channel = _peer->asChannel()) {
		if (channel->amCreator()
			&& channel->membersCount() >= kSuggestGigagroupThreshold) {
			addSuggestGigagroup(inner);
		}
	}

	addBannedButtons(inner);

	_value = [=, rights = getRestrictions]() -> Result {
		return { rights(), getSlowmodeSeconds() };
	};
	_save = addButton(tr::lng_settings_save());
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, inner);
}

Fn<int()> EditPeerPermissionsBox::addSlowmodeSlider(
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	if (const auto chat = _peer->asChat()) {
		if (!chat->amCreator()) {
			return [] { return 0; };
		}
	}
	const auto channel = _peer->asChannel();
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

	addSlowmodeLabels(container);

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

	const auto about = container->add(
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

void EditPeerPermissionsBox::addSlowmodeLabels(
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
				? tr::lng_rights_slowmode_seconds(
					tr::now,
					lt_count,
					seconds)
				: (seconds < 3600)
				? tr::lng_rights_slowmode_minutes(
					tr::now,
					lt_count,
					seconds / 60)
				: tr::lng_rights_slowmode_hours(
					tr::now,
					lt_count,
					seconds / 3600)));
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

void EditPeerPermissionsBox::addSuggestGigagroup(
		not_null<Ui::VerticalLayout*> container) {
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
		AboutGigagroupCallback(_peer->asChannel()),
		st::peerPermissionsButton));

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

void EditPeerPermissionsBox::addBannedButtons(
		not_null<Ui::VerticalLayout*> container) {
	if (const auto chat = _peer->asChat()) {
		if (!chat->amCreator()) {
			return;
		}
	}
	const auto channel = _peer->asChannel();
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_manage_peer_exceptions(),
		(channel
			? Info::Profile::RestrictedCountValue(channel)
			: rpl::single(0)) | ToPositiveNumberString(),
		[=] {
			ParticipantsBoxController::Start(
				_navigation,
				_peer,
				ParticipantsBoxController::Role::Restricted);
		},
		st::peerPermissionsButton));
	if (channel) {
		container->add(EditPeerInfoBox::CreateButton(
			container,
			tr::lng_manage_peer_removed_users(),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					_navigation,
					_peer,
					ParticipantsBoxController::Role::Kicked);
			},
			st::peerPermissionsButton));
	}
}

template <
	typename Flags,
	typename DisabledMessagePairs,
	typename FlagLabelPairs>
EditFlagsControl<Flags> CreateEditFlags(
		QWidget *parent,
		rpl::producer<QString> header,
		Flags checked,
		const DisabledMessagePairs &disabledMessagePairs,
		const FlagLabelPairs &flagLabelPairs) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	const auto container = widget.data();

	const auto checkboxes = container->lifetime(
	).make_state<std::map<Flags, QPointer<Ui::Checkbox>>>();

	const auto value = [=] {
		auto result = Flags(0);
		for (const auto &[flags, checkbox] : *checkboxes) {
			if (checkbox->checked()) {
				result |= flags;
			} else {
				result &= ~flags;
			}
		}
		return result;
	};

	const auto changes = container->lifetime(
	).make_state<rpl::event_stream<>>();

	const auto applyDependencies = [=](Ui::Checkbox *control) {
		static const auto dependencies = Dependencies(Flags());
		ApplyDependencies(*checkboxes, dependencies, control);
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
		auto toggle = std::make_unique<Ui::ToggleView>(
			st::rightsToggle,
			toggled);
		toggle->setLocked(locked.has_value());
		const auto control = container->add(
			object_ptr<Ui::Checkbox>(
				container,
				text,
				st::rightsCheckbox,
				std::move(toggle)),
			st::rightsToggleMargin);
		control->checkedChanges(
		) | rpl::start_with_next([=](bool checked) {
			if (locked.has_value()) {
				if (checked != toggled) {
					Ui::Toast::Show(*locked);
					control->setChecked(toggled);
				}
			} else {
				InvokeQueued(control, [=] {
					applyDependencies(control);
					changes->fire({});
				});
			}
		}, control->lifetime());
		checkboxes->emplace(flags, control);
	};
	for (const auto &[flags, label] : flagLabelPairs) {
		addCheckbox(flags, label);
	}

	applyDependencies(nullptr);
	for (const auto &[flags, checkbox] : *checkboxes) {
		checkbox->finishAnimating();
	}

	return {
		std::move(widget),
		value,
		changes->events() | rpl::map(value)
	};
}

EditFlagsControl<MTPDchatBannedRights::Flags> CreateEditRestrictions(
		QWidget *parent,
		rpl::producer<QString> header,
		MTPDchatBannedRights::Flags restrictions,
		std::map<MTPDchatBannedRights::Flags, QString> disabledMessages) {
	auto result = CreateEditFlags(
		parent,
		header,
		NegateRestrictions(restrictions),
		disabledMessages,
		RestrictionLabels());
	result.value = [original = std::move(result.value)]{
		return NegateRestrictions(original());
	};
	result.changes = std::move(
		result.changes
	) | rpl::map(NegateRestrictions);

	return result;
}

EditFlagsControl<MTPDchatAdminRights::Flags> CreateEditAdminRights(
		QWidget *parent,
		rpl::producer<QString> header,
		MTPDchatAdminRights::Flags rights,
		std::map<MTPDchatAdminRights::Flags, QString> disabledMessages,
		bool isGroup,
		bool anyoneCanAddMembers) {
	return CreateEditFlags(
		parent,
		header,
		rights,
		disabledMessages,
		AdminRightLabels(isGroup, anyoneCanAddMembers));
}
