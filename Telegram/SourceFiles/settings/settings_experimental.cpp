/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"

#include "settings/settings_common.h"
#include "data/components/passkeys.h"
#include "ui/layers/generic_box.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/search_field_controller.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "ui/gl/gl_detection.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "base/options.h"
#include "boxes/moderate_messages_box.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/sandbox.h"
#include "chat_helpers/tabbed_panel.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "history/history_item_components.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "media/player/media_player_instance.h"
#include "mtproto/session_private.h"
#include "webview/webview_embed.h"
#include "window/main_window.h"
#include "window/window_filters_favorite.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "info/info_flexible_scroll.h"
#include "chat_helpers/stickers_list_widget.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

const auto kOptionsClipboardPrefix = u"tdesktop-flags:"_q;

struct DecodeOptionsResult {
	bool ok = false;
	QString json;
};

[[nodiscard]] QString EncodeOptionsToText(const QString &json) {
	const auto flags = QByteArray::Base64UrlEncoding
		| QByteArray::OmitTrailingEquals;
	return kOptionsClipboardPrefix
		+ qs(qCompress(json.toLatin1(), 9).toBase64(flags));
}

[[nodiscard]] DecodeOptionsResult DecodeOptionsFromText(const QString &text) {
	auto result = DecodeOptionsResult();
	if (!text.startsWith(kOptionsClipboardPrefix)) {
		return result;
	}
	auto encoded = QStringView(text).mid(
		kOptionsClipboardPrefix.size()).toLatin1();
	const auto compressed = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!compressed || (*compressed).isEmpty()) {
		return result;
	}
	const auto decoded = qUncompress(*compressed);
	if (decoded.isEmpty()) {
		return result;
	}

	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(decoded, &error);
	if ((error.error != QJsonParseError::NoError) || !parsed.isObject()) {
		return result;
	}
	result.ok = true;
	result.json = QString::fromUtf8(decoded);
	return result;
}

void SetupCopyDeepLink(
		not_null<Window::Controller*> window,
		not_null<Button*> button,
		const QString &id) {
	const auto link = u"tg://settings/experimental/"_q + id;
	const auto menu
		= button->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(u"Copy deep link"_q, [=] {
			TextUtilities::SetClipboardText({ link });
			window->showToast({
				.text = { u"Deep link copied to clipboard."_q },
				.iconLottie = u"toast/voip_invite"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		}, &st::menuIconCopy);
		(*menu)->popup(QCursor::pos());
		e->accept();
	}, button->lifetime());
}

[[nodiscard]] not_null<Button*> AddOptionRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &name,
		const QString &description,
		const style::SettingsButton &st) {
	if (description.isEmpty()) {
		return container->add(object_ptr<Button>(
			container,
			rpl::single(name),
			st));
	}
	const auto &titlePadding = st::settingsExperimentalTitlePadding;
	const auto &aboutPadding = st::settingsExperimentalAboutPadding;
	const auto button = Ui::CreateChild<Button>(
		container.get(),
		rpl::single(QString()),
		st);
	const auto title = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			name,
			st::settingsExperimentalTitle),
		titlePadding);
	const auto about = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			description,
			st::settingsExperimentalAbout),
		aboutPadding);
	title->setAttribute(Qt::WA_TransparentForMouseEvents);
	about->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		container->widthValue(),
		title->heightValue(),
		about->heightValue()
	) | rpl::on_next([=](int width, int titleHeight, int aboutHeight) {
		button->resize(width, titlePadding.top()
			+ titleHeight
			+ titlePadding.bottom()
			+ aboutPadding.top()
			+ aboutHeight
			+ aboutPadding.bottom());
	}, button->lifetime());
	title->topValue(
	) | rpl::on_next([=](int top) {
		button->moveToLeft(0, top - titlePadding.top());
	}, button->lifetime());
	button->show();
	return button;
}

QString AddOption(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		base::options::option<bool> &option,
		rpl::producer<> resetClicks,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto name = option.name().isEmpty() ? option.id() : option.name();
	const auto &description = option.description();

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();

	auto &lifetime = inner->lifetime();
	const auto toggles = lifetime.make_state<rpl::event_stream<bool>>();
	std::move(
		resetClicks
	) | rpl::map_to(
		option.defaultValue()
	) | rpl::start_to_stream(*toggles, lifetime);
	std::move(reloadOptionsRequests) | rpl::on_next([=, &option] {
		toggles->fire_copy(option.value());
	}, lifetime);

	const auto button = AddOptionRow(
		inner,
		name,
		description,
		(option.relevant()
			? st::settingsButtonNoIcon
			: st::settingsOptionDisabled)
	)->toggleOn(toggles->events_starting_with(option.value()));

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option.id(), button);
	}

	SetupCopyDeepLink(window, button, option.id());

	const auto restarter = (option.relevant() && option.restartRequired())
		? button->lifetime().make_state<base::Timer>()
		: nullptr;
	if (restarter) {
		restarter->setCallback([=] {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		});
	}
	button->toggledChanges(
	) | rpl::on_next([=, &option](bool toggled) {
		if (!option.relevant() && toggled != option.defaultValue()) {
			toggles->fire_copy(option.defaultValue());
			window->showToast(
				tr::lng_settings_experimental_irrelevant(tr::now));
			return;
		}
		option.set(toggled);
		if (restarter) {
			restarter->callOnce(st::settingsButtonNoIcon.toggle.duration);
		}
	}, inner->lifetime());

	const auto searchable = name + ' ' + description;
	std::move(
		query
	) | rpl::on_next([=](const QString &text) {
		const auto trimmed = text.trimmed();
		const auto matches = trimmed.isEmpty()
			|| searchable.contains(trimmed, Qt::CaseInsensitive);
		wrap->toggle(matches, anim::type::instant);
	}, wrap->lifetime());

	return searchable;
}

QString AddFavoriteLinkButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto option = &base::options::lookup<QString>(
		Window::kOptionFolderFavoriteLink);
	const auto name = option->name().isEmpty()
		? option->id()
		: option->name();
	const auto &description = option->description();

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)),
		style::margins(),
		style::al_justify);
	const auto inner = wrap->entity();

	auto label = rpl::single(
		rpl::empty
	) | rpl::then(
		option->changes()
	) | rpl::map([option] {
		return option->value();
	});
	const auto button = AddButtonWithLabel(
		inner,
		rpl::single(name),
		std::move(label),
		st::settingsButtonNoIcon);
	button->setClickedCallback([=] {
		window->show(Box(Window::EditFolderFavoriteLinkBox));
	});

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option->id(), button);
	}

	SetupCopyDeepLink(window, button, option->id());

	const auto searchable = name + ' ' + description;
	std::move(
		query
	) | rpl::on_next([=](const QString &text) {
		const auto trimmed = text.trimmed();
		const auto matches = trimmed.isEmpty()
			|| searchable.contains(trimmed, Qt::CaseInsensitive);
		wrap->toggle(matches, anim::type::instant);
	}, wrap->lifetime());

	return searchable;
}

void SetupExperimental(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto headerWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto header = headerWrap->entity();

	Ui::AddSkip(header, st::settingsCheckboxesSkip);

	header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::defaultBoxDividerLabelPadding);

	auto reset = (Button*)nullptr;
	if (base::options::changed()) {
		const auto wrap = header->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				header,
				object_ptr<Ui::VerticalLayout>(header)));
		const auto inner = wrap->entity();
		Ui::AddDivider(inner);
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
		reset = inner->add(object_ptr<Button>(
			inner,
			tr::lng_settings_experimental_restore(),
			st::settingsButtonNoIcon));
		reset->addClickHandler([=] {
			base::options::reset();
			wrap->hide(anim::type::normal);
		});
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
	}

	Ui::AddDivider(header);

	rpl::duplicate(
		query
	) | rpl::on_next([=](const QString &text) {
		headerWrap->toggle(text.trimmed().isEmpty(), anim::type::instant);
	}, headerWrap->lifetime());

	struct Category {
		QString title;
		std::vector<const char*> options;
	};
	const auto categories = std::vector<Category>{
		{
			u"Chats"_q,
			{
				Dialogs::kOptionForumHideChatsList,
				Dialogs::kOptionDialogsUnreadOnTop,
				Dialogs::Ui::kOptionDialogsMuteIcon,
				kOptionAutoScrollInactiveChat,
				kModerateCommonGroups,
				Info::kClassicProfileScroll,
			}
		},
		{
			u"Messages"_q,
			{
				Ui::kOptionUseSmallMsgBubbleRadius,
				HistoryView::kOptionUnlimitedMessageWidth,
				HistoryView::Controls::kOptionMacCmdReplyImmediately,
				Ui::kOptionHideAiButton,
				kForceComposeSearchOneColumn,
			}
		},
		{
			u"Profile"_q,
			{
				Window::kOptionViewProfileInChatsListContextMenu,
				Info::Profile::kOptionShowPeerIdBelowAbout,
				Info::Profile::kOptionShowChannelJoinedBelowAbout,
				Info::Profile::kOptionProfileMediaTabs,
				Info::Profile::kOptionProfileMediaTabsExpanded,
			}
		},
		{
			u"Stickers and emoji"_q,
			{
				ChatHelpers::kOptionTabbedPanelShowOnClick,
				ChatHelpers::kOptionUnlimitedRecentStickers,
			}
		},
		{
			u"Media"_q,
			{
				Media::Player::kOptionDisableAutoplayNext,
				Window::kOptionExternalMediaViewer,
				FFmpeg::kOptionFFmpegMultiThread,
			}
		},
		{
			u"Notifications"_q,
			{
				Window::Notifications::kOptionHideReplyButton,
				Window::Notifications::kOptionCustomNotification,
				Window::Notifications::kOptionGNotification,
				Window::Notifications::kOptionMacModernNotifications,
			}
		},
		{
			u"Interface"_q,
			{
				Core::kOptionFractionalScalingEnabled,
				Core::kOptionHighDpiDownscale,
				Ui::GL::kOptionUseQtRhi,
				Ui::GL::kOptionEnableVulkanRhi,
				Core::kOptionFreeType,
				Ui::kOptionQScroller,
				Window::kOptionDisableTouchbar,
				Window::kOptionNewWindowsSizeAsFirst,
			}
		},
		{
			u"System"_q,
			{
				MTP::details::kOptionPreferIPv6,
				Core::kOptionSkipUrlSchemeRegister,
				Core::kOptionDeadlockDetector,
				Webview::kOptionWebviewDebugEnabled,
				Webview::kOptionWebviewLegacyEdge,
			}
		},
	};

	const auto addOption = [&](
			not_null<Ui::VerticalLayout*> inner,
			const char name[]) {
		return AddOption(
			window,
			inner,
			base::options::lookup<bool>(name),
			(reset
				? (reset->clicks() | rpl::to_empty)
				: rpl::producer<>()),
			rpl::duplicate(reloadOptionsRequests),
			rpl::duplicate(query),
			registerHighlight);
	};
	const auto addCategory = [&](
			const QString &title,
			auto &&fill) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, rpl::single(title));
		auto searchable = std::vector<QString>();
		fill(inner, searchable);
		Ui::AddSkip(inner);
		Ui::AddDivider(inner);

		rpl::duplicate(
			query
		) | rpl::on_next([=](const QString &text) {
			const auto trimmed = text.trimmed();
			const auto matches = trimmed.isEmpty()
				|| ranges::any_of(searchable, [&](const QString &entry) {
					return entry.contains(trimmed, Qt::CaseInsensitive);
				});
			wrap->toggle(matches, anim::type::instant);
		}, wrap->lifetime());
	};

	for (const auto &category : categories) {
		addCategory(category.title, [&](
				not_null<Ui::VerticalLayout*> inner,
				std::vector<QString> &searchable) {
			for (const auto name : category.options) {
				searchable.push_back(addOption(inner, name));
			}
		});
	}

	addCategory(u"Other"_q, [&](
			not_null<Ui::VerticalLayout*> inner,
			std::vector<QString> &searchable) {
		if (base::options::lookup<bool>(kOptionFastButtonsMode).value()) {
			searchable.push_back(addOption(inner, kOptionFastButtonsMode));
		}
		searchable.push_back(AddFavoriteLinkButton(
			window,
			inner,
			rpl::duplicate(query),
			registerHighlight));
	});
}

} // namespace

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

Experimental::~Experimental() = default;

rpl::producer<QString> Experimental::title() {
	return tr::lng_settings_experimental();
}

void Experimental::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto window = &controller()->window();
	addAction(
		u"Export"_q,
		[=] {
			TextUtilities::SetClipboardText(
				{ EncodeOptionsToText(base::options::serialize()) });
			window->showToast({
				.text = { u"Experimental settings code copied to clipboard."_q },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		},
		&st::menuIconCopy);
	if (!DecodeOptionsFromText(QGuiApplication::clipboard()->text()).ok) {
		return;
	}
	addAction(
		u"Import"_q,
		[=] {
			const auto decoded = DecodeOptionsFromText(
				QGuiApplication::clipboard()->text());
			if (!decoded.ok) {
				window->showToast(u"Clipboard does not contain "
					"a valid experimental settings code."_q);
				return;
			}
			if (!base::options::deserialize(decoded.json)) {
				window->showToast(u"Experimental settings code is valid"
					", but data format is not supported."_q);
				return;
			}
			_reloadOptionsRequests.fire({});
			window->showToast(u"Experimental settings imported "
				"from code in clipboard."_q);
		},
		&st::menuIconImportTheme);
}

void Experimental::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	} else {
		setFocus();
	}
}

void Experimental::showFinished() {
	AbstractSection::showFinished();
	for (const auto &[id, widget] : _highlights) {
		if (widget) {
			controller()->checkHighlightControl(id, widget);
		}
	}
}

base::weak_qptr<Ui::RpWidget> Experimental::createPinnedToTop(
		not_null<QWidget*> parent) {
	_searchController = std::make_unique<Ui::SearchFieldController>(
		_query.current());
	auto rowView = _searchController->createRowView(
		parent,
		st::infoLayerMediaSearch);
	_searchField = rowView.field;

	const auto searchContainer = Ui::CreateChild<Ui::FixedHeightWidget>(
		parent.get(),
		st::infoLayerMediaSearch.height);
	const auto wrap = rowView.wrap.release();
	wrap->setParent(searchContainer);
	wrap->show();

	searchContainer->widthValue(
	) | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
		wrap->moveToLeft(0, 0);
	}, searchContainer->lifetime());

	_searchController->queryValue(
	) | rpl::on_next([=](QString text) {
		_query = std::move(text);
	}, searchContainer->lifetime());

	return base::make_weak(not_null<Ui::RpWidget*>{ searchContainer });
}

void Experimental::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupExperimental(
		&controller()->window(),
		content,
		_reloadOptionsRequests.events(),
		_query.value(),
		[this](const QString &id, not_null<QWidget*> widget) {
			_highlights.push_back({ id, widget.get() });
		});

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
