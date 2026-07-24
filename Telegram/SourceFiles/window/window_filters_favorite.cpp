/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_favorite.h"

#include "apiwrap.h"
#include "base/options.h"
#include "base/qthelp_url.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "data/data_user.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/basic_click_handlers.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_filter_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtGui/QtEvents>

namespace Window {
namespace {

constexpr auto kMaxLabelLines = 3;

base::options::option<QString> FavoriteLink({
	.id = kOptionFolderFavoriteLink,
	.name = "Favorite folder button",
	.description = "Favorite button at the bottom of the folders strip "
		"that opens a tg:// or t.me/ link.",
});

enum class Category {
	Generic,
	Peer,
	Invite,
	StickerSet,
	EmojiSet,
	ChatFolder,
	Settings,
};

struct ParsedLink {
	Category category = Category::Generic;
	QString value;
	bool phone = false;
};

[[nodiscard]] ParsedLink ParseFavoriteLink(const QString &link) {
	const auto local = Core::TryConvertUrlToLocal(link.trimmed());
	const auto prefix = u"tg://"_q;
	if (!local.startsWith(prefix, Qt::CaseInsensitive)) {
		return {};
	}
	const auto command = local.mid(prefix.size());
	const auto queryStart = command.indexOf('?');
	const auto name = (queryStart < 0)
		? command
		: command.mid(0, queryStart);
	const auto params = (queryStart < 0)
		? QMap<QString, QString>()
		: qthelp::url_parse_params(
			command.mid(queryStart + 1),
			qthelp::UrlParamNameTransform::ToLower);
	const auto starts = [&](const QString &n) {
		return name.startsWith(n, Qt::CaseInsensitive);
	};
	const auto with = [](Category category, const QString &value) {
		return value.isEmpty() ? ParsedLink() : ParsedLink{ category, value };
	};
	if (starts(u"resolve"_q)) {
		const auto phone = params.value(u"phone"_q);
		if (!phone.isEmpty()) {
			return { Category::Peer, phone, true };
		}
		const auto domain = params.value(u"domain"_q);
		return (domain.isEmpty() || domain == u"giftcode"_q)
			? ParsedLink()
			: ParsedLink{ Category::Peer, domain };
	} else if (starts(u"join"_q)) {
		return with(Category::Invite, params.value(u"invite"_q));
	} else if (starts(u"addstickers"_q)) {
		return with(Category::StickerSet, params.value(u"set"_q));
	} else if (starts(u"addemoji"_q)) {
		return with(Category::EmojiSet, params.value(u"set"_q));
	} else if (starts(u"addlist"_q)) {
		return with(Category::ChatFolder, params.value(u"slug"_q));
	} else if (starts(u"settings"_q)) {
		auto path = name.mid(u"settings"_q.size());
		while (path.startsWith('/')) {
			path = path.mid(1);
		}
		return { Category::Settings, path };
	}
	return {};
}

struct KnownContent {
	const style::icon *icon = nullptr;
	QString label;
};

[[nodiscard]] KnownContent SettingsContent(const QString &path) {
	const auto starts = [&](const char *prefix) {
		return path.startsWith(QString::fromLatin1(prefix), Qt::CaseInsensitive);
	};
	if (path.isEmpty()) {
		return { &st::foldersSectionSettings, tr::lng_menu_settings(tr::now) };
	} else if (starts("folders")) {
		return { &st::foldersSectionFolders, tr::lng_filters_title(tr::now) };
	} else if (starts("notifications")) {
		return {
			&st::foldersSectionNotifications,
			tr::lng_settings_section_notify(tr::now),
		};
	} else if (starts("privacy") || starts("phone_privacy")) {
		return {
			&st::foldersSectionPrivacy,
			tr::lng_settings_section_privacy(tr::now),
		};
	} else if (starts("appearance/stickers") || starts("stickers")) {
		return {
			&st::foldersSectionStickers,
			tr::lng_settings_stickers_emoji(tr::now),
		};
	} else if (starts("appearance") || starts("chat") || starts("themes")) {
		return {
			&st::foldersSectionChat,
			tr::lng_settings_section_chat_settings(tr::now),
		};
	} else if (starts("devices")) {
		return {
			&st::foldersSectionSettings,
			tr::lng_settings_sessions_title(tr::now),
		};
	} else if (starts("language")) {
		return {
			&st::foldersSectionLanguage,
			tr::lng_settings_language(tr::now),
		};
	} else if (starts("advanced")) {
		return {
			&st::foldersSectionSettings,
			tr::lng_settings_advanced(tr::now),
		};
	} else if (starts("experiment")) {
		return {
			&st::foldersSectionSettings,
			tr::lng_settings_experimental(tr::now),
		};
	} else if (starts("edit")
		|| starts("information")
		|| starts("my-profile")
		|| starts("edit_profile")) {
		return {
			&st::foldersSectionInfo,
			tr::lng_settings_section_info(tr::now),
		};
	}
	return { &st::foldersSectionSettings, tr::lng_menu_settings(tr::now) };
}

} // namespace

const char kOptionFolderFavoriteLink[] = "folder-favorite-link";

bool ValidFolderFavoriteLink(const QString &link) {
	return Core::TryConvertUrlToLocal(link.trimmed())
		.startsWith(u"tg://"_q, Qt::CaseInsensitive);
}

void EditFolderFavoriteLinkBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_menu_formatting_link_edit());
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_info_link_label(),
		FavoriteLink.value()));
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto submit = [=] {
		const auto link = field->getLastText().trimmed();
		if (link.isEmpty()) {
			FavoriteLink.set(QString());
			box->closeBox();
		} else if (!ValidFolderFavoriteLink(link)) {
			field->showError();
		} else {
			FavoriteLink.set(link);
			box->closeBox();
		}
	};
	field->submits(
	) | rpl::on_next([=](auto) { submit(); }, field->lifetime());
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

FolderFavoriteButton::FolderFavoriteButton(
	QWidget *parent,
	not_null<SessionController*> controller,
	const style::SideBarButton &st)
: RippleButton(parent, st.ripple)
, _controller(controller)
, _st(st)
, _api(&controller->session().mtp())
, _label(st.minTextWidth) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	setClickedCallback([=] { openLink(); });

	events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::ContextMenu);
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		showMenu();
		e->accept();
	}, lifetime());
}

FolderFavoriteButton::~FolderFavoriteButton() = default;

rpl::producer<bool> FolderFavoriteButton::shownValue() const {
	return _shown.value();
}

bool FolderFavoriteButton::shown() const {
	return _shown.current();
}

int FolderFavoriteButton::resizeGetHeight(int newWidth) {
	const auto result = _st.minHeight;
	const auto text = std::min(
		_label.countHeight(newWidth - _st.textSkip * 2),
		_st.style.font->height * kMaxLabelLines);
	const auto add = text - _st.style.font->height;
	return result + std::max(add, 0);
}

void FolderFavoriteButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), _st.textBg);

	RippleButton::paintRipple(p, 0, 0);

	if (_thumbnail) {
		const auto size = st::foldersFavorite.width();
		const auto x = (width() - size) / 2;
		p.drawImage(x, _st.iconPosition.y(), _thumbnail->image(size));
	}

	p.setPen(_st.textFg);
	_label.draw(p, {
		.position = { _st.textSkip, _st.textTop },
		.availableWidth = width() - 2 * _st.textSkip,
		.align = style::al_top,
		.elisionLines = kMaxLabelLines,
	});
}

void FolderFavoriteButton::setLink(const QString &link) {
	_resolveLifetime.destroy();
	_api.request(base::take(_requestId)).cancel();

	_link = link.trimmed();
	if (_link.isEmpty()) {
		return;
	}

	const auto parsed = ParseFavoriteLink(_link);
	switch (parsed.category) {
	case Category::Peer:
		resolvePeer(parsed.value, parsed.phone);
		break;
	case Category::Invite:
		resolveInvite(parsed.value);
		break;
	case Category::StickerSet:
	case Category::EmojiSet:
		resolveStickerSet(parsed.value);
		break;
	case Category::ChatFolder:
		apply({
			.thumbnail = Ui::MakeIconThumbnail(st::foldersCustom),
			.label = tr::lng_filters_title(tr::now),
		});
		break;
	case Category::Settings: {
		const auto content = SettingsContent(parsed.value);
		apply({
			.thumbnail = Ui::MakeIconThumbnail(*content.icon),
			.label = content.label,
		});
		break;
	}
	default:
		showGeneric();
		break;
	}
}

void FolderFavoriteButton::resolvePeer(const QString &value, bool phone) {
	const auto session = &_controller->session();
	if (const auto cached = phone
		? session->data().userByPhone(value)
		: session->data().peerByUsername(value)) {
		applyPeer(cached);
		return;
	}
	const auto done = [=](const MTPcontacts_ResolvedPeer &result) {
		_requestId = 0;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				applyPeer(session->data().peer(peerId));
			} else {
				showGeneric();
			}
		});
	};
	const auto fail = [=](const MTP::Error &) {
		_requestId = 0;
		showGeneric();
	};
	if (phone) {
		_requestId = _api.request(MTPcontacts_ResolvePhone(
			MTP_string(value)
		)).done(done).fail(fail).send();
	} else {
		_requestId = _api.request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(value),
			MTP_string(QString())
		)).done(done).fail(fail).send();
	}
}

void FolderFavoriteButton::resolveInvite(const QString &hash) {
	const auto session = &_controller->session();
	const auto done = [=](const MTPChatInvite &result) {
		_requestId = 0;
		result.match([&](const MTPDchatInvite &data) {
			const auto photo = session->data().processPhoto(data.vphoto());
			auto thumbnail = std::shared_ptr<Ui::DynamicImage>();
			if (!photo->isNull()) {
				thumbnail = Ui::MakePhotoThumbnail(photo, FullMsgId());
			} else {
				const auto icon = data.is_broadcast()
					? &st::foldersChannels
					: &st::foldersGroups;
				thumbnail = Ui::MakeIconThumbnail(*icon);
			}
			apply({
				.thumbnail = std::move(thumbnail),
				.label = qs(data.vtitle()),
			});
		}, [&](const MTPDchatInviteAlready &data) {
			applyPeer(session->data().processChat(data.vchat()));
		}, [&](const MTPDchatInvitePeek &data) {
			applyPeer(session->data().processChat(data.vchat()));
		});
	};
	const auto fail = [=](const MTP::Error &) {
		_requestId = 0;
		showGeneric();
	};
	_requestId = _api.request(MTPmessages_CheckChatInvite(
		MTP_string(hash)
	)).done(done).fail(fail).send();
}

void FolderFavoriteButton::resolveStickerSet(const QString &shortName) {
	const auto session = &_controller->session();
	_requestId = _api.request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(StickerSetIdentifier{ .shortName = shortName }),
		MTP_int(0)
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			const auto set = session->data().stickers().feedSetFull(data);
			const auto thumb = set->lookupThumbnailDocument();
			const auto document = thumb
				? thumb
				: set->stickers.isEmpty()
				? nullptr
				: set->stickers.front();
			if (!document) {
				showGeneric();
				return;
			}
			apply({
				.thumbnail = Ui::MakeDocumentThumbnail(
					document,
					Data::FileOrigin(Data::FileOriginStickerSet(
						set->id,
						set->accessHash))),
				.label = set->title,
			});
		}, [&](const MTPDmessages_stickerSetNotModified &) {
			showGeneric();
		});
	}).fail([=](const MTP::Error &) {
		_requestId = 0;
		showGeneric();
	}).send();
}

void FolderFavoriteButton::applyPeer(not_null<PeerData*> peer) {
	peer->loadUserpic();
	apply({
		.thumbnail = Ui::MakeUserpicThumbnail(peer),
		.label = peer->name(),
	});
	peer->session().changes().peerUpdates(
		peer,
		Data::PeerUpdate::Flag::Name
	) | rpl::on_next([=] {
		_label.setText(_st.style, peer->name());
		resizeToWidth(width());
		update();
	}, _resolveLifetime);
}

void FolderFavoriteButton::apply(Presentation presentation) {
	_thumbnail = std::move(presentation.thumbnail);
	_label.setText(_st.style, presentation.label);
	if (_thumbnail) {
		_thumbnail->subscribeToUpdates([=] { update(); });
	}
	_shown = true;
	resizeToWidth(width());
	update();
}

void FolderFavoriteButton::showGeneric() {
	apply({
		.thumbnail = Ui::MakeIconThumbnail(st::foldersFavorite),
		.label = tr::lng_info_link_label(tr::now),
	});
}

void FolderFavoriteButton::openLink() {
	if (_link.isEmpty()) {
		return;
	}
	UrlClickHandler::Open(
		Core::TryConvertUrlToLocal(_link),
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = base::make_weak(_controller.get()),
		}));
}

void FolderFavoriteButton::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	addAction(
		tr::lng_menu_formatting_link_edit(tr::now),
		[=] { editLink(); },
		&st::menuIconEdit);
	addAction({
		.text = tr::lng_filters_context_remove(tr::now),
		.handler = [=] { FavoriteLink.set(QString()); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	_menu->popup(QCursor::pos());
}

void FolderFavoriteButton::editLink() {
	_controller->show(Box(EditFolderFavoriteLinkBox));
}

} // namespace Window
