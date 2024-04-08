/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu_helpers.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_info.h" // infoTopBarMenu
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace Window {
namespace {

class VersionLabel final
	: public Ui::FlatLabel
	, public Ui::AbstractTooltipShower {
public:
	using Ui::FlatLabel::FlatLabel;

	void clickHandlerActiveChanged(
			const ClickHandlerPtr &action,
			bool active) override {
		update();
		if (active && action && !action->dragText().isEmpty()) {
			Ui::Tooltip::Show(1000, this);
		} else {
			Ui::Tooltip::Hide();
		}
	}

	QString tooltipText() const override {
		return u"Build date: %1."_q.arg(__DATE__);
	}

	QPoint tooltipPos() const override {
		return QCursor::pos();
	}

	bool tooltipWindowActive() const override {
		return Ui::AppInFocus() && Ui::InFocusChain(window());
	}

};

} // namespace

[[nodiscard]] not_null<Ui::FlatLabel*> AddVersionLabel(
		not_null<Ui::RpWidget*> parent) {
	return (Platform::IsMacStoreBuild() || Platform::IsWindowsStoreBuild())
		? Ui::CreateChild<Ui::FlatLabel>(
			parent.get(),
			st::mainMenuVersionLabel)
		: Ui::CreateChild<VersionLabel>(
			parent.get(),
			st::mainMenuVersionLabel);
}

not_null<Ui::SettingsButton*> AddMyChannelsBox(
		not_null<Ui::SettingsButton*> button,
		not_null<SessionController*> controller,
		bool chats) {
	button->setAcceptBoth(true);

	const auto requestIcon = [=, session = &controller->session()](
			not_null<Ui::GenericBox*> box,
			Fn<void(not_null<DocumentData*>)> done) {
		const auto api = box->lifetime().make_state<MTP::Sender>(
			&session->mtp());
		api->request(MTPmessages_GetStickerSet(
			Data::InputStickerSet({
				.shortName = u"tg_placeholders_android"_q,
			}),
			MTP_int(0)
		)).done([=](const MTPmessages_StickerSet &result) {
			result.match([&](const MTPDmessages_stickerSet &data) {
				const auto &v = data.vdocuments().v;
				if (v.size() > 1) {
					done(session->data().processDocument(v[1]));
				}
			}, [](const MTPDmessages_stickerSetNotModified &) {
			});
		}).send();
	};
	const auto addIcon = [=](not_null<Ui::GenericBox*> box) {
		const auto widget = box->addRow(object_ptr<Ui::RpWidget>(box));
		widget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(widget);
			p.setFont(st::boxTextFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				widget->rect(),
				tr::lng_contacts_loading(tr::now),
				style::al_center);
		}, widget->lifetime());
		widget->resize(Size(st::maxStickerSize));
		widget->show();
		box->verticalLayout()->resizeToWidth(box->width());
		requestIcon(box, [=](not_null<DocumentData*> document) {
			const auto view = document->createMediaView();
			const auto origin = document->stickerSetOrigin();
			controller->session().downloaderTaskFinished(
			) | rpl::take_while([=] {
				if (view->bytes().isEmpty()) {
					return true;
				}
				auto owned = Lottie::MakeIcon({
					.json = Images::UnpackGzip(view->bytes()),
					.sizeOverride = Size(st::maxStickerSize),
				});
				const auto icon = owned.get();
				widget->lifetime().add([kept = std::move(owned)]{});
				widget->paintRequest(
				) | rpl::start_with_next([=] {
					auto p = QPainter(widget);
					icon->paint(p, (widget->width() - icon->width()) / 2, 0);
				}, widget->lifetime());
				icon->animate(
					[=] { widget->update(); },
					0,
					icon->framesCount());
				return false;
			}) | rpl::start(widget->lifetime());
			view->automaticLoad(origin, nullptr);
			view->videoThumbnailWanted(origin);
		});
	};

	const auto myChannelsBox = [=](not_null<Ui::GenericBox*> box) {
		box->setTitle(chats
			? tr::lng_notification_groups()
			: tr::lng_notification_channels());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });

		const auto st = box->lifetime().make_state<style::UserpicButton>(
			st::defaultUserpicButton);
		st->photoSize = st::defaultPeerListItem.photoSize;
		st->size = QSize(st->photoSize, st->photoSize);

		class Button final : public Ui::SettingsButton {
		public:
			using Ui::SettingsButton::SettingsButton;

			void setPeer(not_null<PeerData*> p) {
				const auto c = p->asChannel();
				const auto g = p->asChat();
				_text.setText(
					st::defaultPeerListItem.nameStyle,
					((c && c->isMegagroup()) ? u"[s] "_q : QString())
						+ p->name());
				const auto count = c ? c->membersCount() : g->count;
				_status.setText(
					st::defaultTextStyle,
					!p->username().isEmpty()
						? ('@' + p->username())
						: (count > 0)
						? ((c && !c->isMegagroup())
							? tr::lng_chat_status_subscribers
							: tr::lng_chat_status_members)(
								tr::now,
								lt_count,
								count)
						: QString());
			}

			int resizeGetHeight(int) override {
				return st::defaultPeerListItem.height;
			}

			void paintEvent(QPaintEvent *e) override {
				Ui::SettingsButton::paintEvent(e);
				auto p = Painter(this);
				const auto &st = st::defaultPeerListItem;
				const auto availableWidth = width()
					- st::boxRowPadding.right()
					- st.namePosition.x();
				p.setPen(st.nameFg);
				auto context = Ui::Text::PaintContext{
					.position = st.namePosition,
					.outerWidth = availableWidth,
					.availableWidth = availableWidth,
					.elisionLines = 1,
				};
				_text.draw(p, context);
				p.setPen(st.statusFg);
				context.position = st.statusPosition;
				_status.draw(p, context);
			}

		private:
			Ui::Text::String _text;
			Ui::Text::String _status;

		};

		const auto add = [&](not_null<PeerData*> peer) {
			const auto row = box->addRow(
				object_ptr<Button>(box, rpl::single(QString())),
				{});
			row->setPeer(peer);
			row->setClickedCallback([=] {
				controller->showPeerHistory(peer);
			});
			using Button = Ui::UserpicButton;
			const auto userpic = Ui::CreateChild<Button>(row, peer, *st);
			userpic->move(st::defaultPeerListItem.photoPosition);
			userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		};

		const auto &data = controller->session().data();
		auto ids = std::vector<PeerId>();
		if (chats) {
			data.enumerateGroups([&](not_null<PeerData*> peer) {
				peer = peer->migrateToOrMe();
				if (ranges::contains(ids, peer->id)) {
					return;
				}
				const auto c = peer->asChannel();
				const auto g = peer->asChat();
				if ((c && c->amCreator()) || (g && g->amCreator())) {
					ids.push_back(peer->id);
					add(peer);
				}
			});
		} else {
			data.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
				if (channel->amCreator()
					&& !ranges::contains(ids, channel->id)) {
					ids.push_back(channel->id);
					add(channel);
				}
			});
		}
		if (ids.empty()) {
			addIcon(box);
		}
	};

	using Menu = base::unique_qptr<Ui::PopupMenu>;
	const auto menu = button->lifetime().make_state<Menu>();
	button->addClickHandler([=](Qt::MouseButton which) {
		if (which != Qt::RightButton) {
			return;
		}

		(*menu) = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(
			(chats ? tr::lng_menu_my_groups : tr::lng_menu_my_channels)(
				tr::now),
			[=] { controller->uiShow()->showBox(Box(myChannelsBox)); },
			chats ? &st::menuIconGroups : &st::menuIconChannel);
		(*menu)->popup(QCursor::pos());
	});

	return button;
}

void SetupMenuBots(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto wrap = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto bots = &controller->session().attachWebView();
	const auto iconLoadLifetime = wrap->lifetime().make_state<
		rpl::lifetime
	>();

	rpl::single(
		rpl::empty
	) | rpl::then(
		bots->attachBotsUpdates()
	) | rpl::start_with_next([=] {
		const auto width = container->widthNoMargins();
		wrap->clear();
		for (const auto &bot : bots->attachBots()) {
			const auto user = bot.user;
			if (!bot.inMainMenu || !bot.media) {
				continue;
			} else if (const auto media = bot.media; !media->loaded()) {
				if (!*iconLoadLifetime) {
					auto &session = user->session();
					*iconLoadLifetime = session.downloaderTaskFinished(
					) | rpl::start_with_next([=] {
						if (media->loaded()) {
							iconLoadLifetime->destroy();
							bots->notifyBotIconLoaded();
						}
					});
				}
				continue;
			}
			const auto button = wrap->add(object_ptr<Ui::SettingsButton>(
				wrap,
				rpl::single(bot.name),
				st::mainMenuButton));
			const auto menu = button->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>
			>();
			const auto icon = Ui::CreateChild<InlineBots::MenuBotIcon>(
				button,
				bot.media);
			button->heightValue(
			) | rpl::start_with_next([=](int height) {
				icon->move(
					st::mainMenuButton.iconLeft,
					(height - icon->height()) / 2);
			}, button->lifetime());
			const auto weak = Ui::MakeWeak(container);
			button->setAcceptBoth(true);
			button->clicks(
			) | rpl::start_with_next([=](Qt::MouseButton which) {
				if (which == Qt::LeftButton) {
					bots->requestSimple(controller, user, {
						.fromMainMenu = true,
					});
					if (weak) {
						controller->window().hideSettingsAndLayer();
					}
				} else {
					(*menu) = nullptr;
					(*menu) = base::make_unique_q<Ui::PopupMenu>(
						button,
						st::popupMenuWithIcons);
					(*menu)->addAction(
						tr::lng_bot_remove_from_menu(tr::now),
						[=] { bots->removeFromMenu(user); },
						&st::menuIconDelete);
					(*menu)->popup(QCursor::pos());
				}
			}, button->lifetime());

			if (bots->showMainMenuNewBadge(bot)) {
				Ui::NewBadge::AddToRight(button);
			}
		}
		wrap->resizeToWidth(width);
	}, wrap->lifetime());
}

} // namespace Window
