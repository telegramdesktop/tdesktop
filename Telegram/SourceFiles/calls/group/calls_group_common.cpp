/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_common.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "base/random.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsWithMoreBadge
#include "boxes/share_box.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "tde2e/tde2e_api.h"
#include "tde2e/tde2e_integration.h"
#include "ui/boxes/boost_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Calls::Group {

object_ptr<Ui::GenericBox> ScreenSharingPrivacyRequestBox() {
#ifdef Q_OS_MAC
	if (!Platform::IsMac10_15OrGreater()) {
		return { nullptr };
	}
	return Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				rpl::combine(
					tr::lng_group_call_mac_screencast_access(),
					tr::lng_group_call_mac_recording()
				) | rpl::map([](QString a, QString b) {
					auto result = Ui::Text::RichLangValue(a);
					result.append("\n\n").append(Ui::Text::RichLangValue(b));
					return result;
				}),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_group_call_mac_settings(), [=] {
			Platform::OpenDesktopCapturePrivacySettings();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	});
#else // Q_OS_MAC
	return { nullptr };
#endif // Q_OS_MAC
}

object_ptr<Ui::RpWidget> MakeJoinCallLogo(not_null<QWidget*> parent) {
	const auto logoSize = st::confcallJoinLogo.size();
	const auto logoOuter = logoSize.grownBy(st::confcallJoinLogoPadding);
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto logo = result.data();
	logo->resize(logo->width(), logoOuter.height());
	logo->paintRequest() | rpl::start_with_next([=] {
		if (logo->width() < logoOuter.width()) {
			return;
		}
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto x = (logo->width() - logoOuter.width()) / 2;
		const auto outer = QRect(QPoint(x, 0), logoOuter);
		p.setBrush(st::windowBgActive);
		p.setPen(Qt::NoPen);
		p.drawEllipse(outer);
		st::confcallJoinLogo.paintInCenter(p, outer);
	}, logo->lifetime());
	return result;
}

void ConferenceCallJoinConfirm(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Data::GroupCall> call,
		UserData *maybeInviter,
		Fn<void(Fn<void()> close)> join) {
	box->setStyle(st::confcallJoinBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	box->addRow(
		MakeJoinCallLogo(box),
		st::boxRowPadding + st::confcallLinkHeaderIconPadding);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_confcall_join_title(),
			st::boxTitle),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top);
	const auto wrapName = [&](not_null<PeerData*> peer) {
		return rpl::single(Ui::Text::Bold(peer->shortName()));
	};
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(maybeInviter
				? tr::lng_confcall_join_text_inviter(
					lt_user,
					wrapName(maybeInviter),
					Ui::Text::RichLangValue)
				: tr::lng_confcall_join_text(Ui::Text::RichLangValue)),
			st::confcallLinkCenteredText),
		st::boxRowPadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	const auto &participants = call->participants();
	const auto known = int(participants.size());
	if (known) {
		const auto sep = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding + st::confcallJoinSepPadding);
		sep->resize(sep->width(), st::normalFont->height);
		sep->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(sep);
			const auto line = st::lineWidth;
			const auto top = st::confcallLinkFooterOrLineTop;
			const auto fg = st::windowSubTextFg->b;
			p.setOpacity(0.2);
			p.fillRect(0, top, sep->width(), line, fg);
		}, sep->lifetime());

		auto peers = std::vector<not_null<PeerData*>>();
		for (const auto &participant : participants) {
			peers.push_back(participant.peer);
			if (peers.size() == 3) {
				break;
			}
		}
		box->addRow(
			CreateUserpicsWithMoreBadge(
				box,
				rpl::single(peers),
				st::confcallJoinUserpics,
				known),
			st::boxRowPadding + st::confcallJoinUserpicsPadding);

		const auto wrapByIndex = [&](int index) {
			Expects(index >= 0 && index < known);

			return wrapName(participants[index].peer);
		};
		auto text = (known == 1)
			? tr::lng_confcall_already_joined_one(
				lt_user,
				wrapByIndex(0),
				Ui::Text::RichLangValue)
			: (known == 2)
			? tr::lng_confcall_already_joined_two(
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				Ui::Text::RichLangValue)
			: (known == 3)
			? tr::lng_confcall_already_joined_three(
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				lt_third,
				wrapByIndex(2),
				Ui::Text::RichLangValue)
			: tr::lng_confcall_already_joined_many(
				lt_count,
				rpl::single(1. * (std::max(known, call->fullCount()) - 2)),
				lt_user,
				wrapByIndex(0),
				lt_other,
				wrapByIndex(1),
				Ui::Text::RichLangValue);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(text),
				st::confcallLinkCenteredText),
			st::boxRowPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);
	}
	const auto joinAndClose = [=] {
		join([weak = base::make_weak(box)] {
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		});
	};
	Info::BotStarRef::AddFullWidthButton(
		box,
		tr::lng_confcall_join_button(),
		joinAndClose,
		&st::confcallLinkButton);
}

ConferenceCallLinkStyleOverrides DarkConferenceCallLinkStyle() {
	return {
		.box = &st::groupCallLinkBox,
		.menuToggle = &st::groupCallLinkMenu,
		.menu = &st::groupCallPopupMenuWithIcons,
		.close = &st::storiesStealthBoxClose,
		.centerLabel = &st::groupCallLinkCenteredText,
		.linkPreview = &st::groupCallLinkPreview,
		.contextRevoke = &st::mediaMenuIconRemove,
		.shareBox = std::make_shared<ShareBoxStyleOverrides>(
			DarkShareBoxStyle()),
	};
}

void ShowConferenceCallLinkBox(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<Data::GroupCall> call,
		const ConferenceCallLinkArgs &args) {
	const auto st = args.st;
	const auto initial = args.initial;
	const auto link = call->conferenceInviteLink();
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
			bool resetting = false;
		};
		const auto state = box->lifetime().make_state<State>();

		box->setStyle(st.box
			? *st.box
			: initial
			? st::confcallLinkBoxInitial
			: st::confcallLinkBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		const auto close = box->addTopButton(
			st.close ? *st.close : st::boxTitleClose,
			[=] { box->closeBox(); });

		if (!args.initial && call->canManage()) {
			const auto toggle = Ui::CreateChild<Ui::IconButton>(
				close->parentWidget(),
				st.menuToggle ? *st.menuToggle : st::confcallLinkMenu);
			const auto handler = [=] {
				if (state->resetting) {
					return;
				}
				state->resetting = true;
				using Flag = MTPphone_ToggleGroupCallSettings::Flag;
				const auto weak = base::make_weak(box);
				call->session().api().request(
					MTPphone_ToggleGroupCallSettings(
						MTP_flags(Flag::f_reset_invite_hash),
						call->input(),
						MTPBool(), // join_muted
						MTPBool()) // messages_enabled
				).done([=](const MTPUpdates &result) {
					call->session().api().applyUpdates(result);
					ShowConferenceCallLinkBox(show, call, args);
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
					show->showToast({
						.title = tr::lng_confcall_link_revoked_title(
							tr::now),
						.text = {
							tr::lng_confcall_link_revoked_text(tr::now),
						},
					});
				}).send();
			};
			toggle->setClickedCallback([=] {
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					toggle,
					st.menu ? *st.menu : st::popupMenuWithIcons);
				state->menu->addAction(
					tr::lng_confcall_link_revoke(tr::now),
					handler,
					(st.contextRevoke
						? st.contextRevoke
						: &st::menuIconRemove));
				state->menu->popup(QCursor::pos());
			});

			close->geometryValue(
			) | rpl::start_with_next([=](QRect geometry) {
				toggle->moveToLeft(
					geometry.x() - toggle->width(),
					geometry.y());
			}, close->lifetime());
		}

		box->addRow(
			Info::BotStarRef::CreateLinkHeaderIcon(box, &call->session()),
			st::boxRowPadding + st::confcallLinkHeaderIconPadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_confcall_link_title(),
				st.box ? st.box->title : st::boxTitle),
			st::boxRowPadding + st::confcallLinkTitlePadding,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_confcall_link_about(),
				(st.centerLabel
					? *st.centerLabel
					: st::confcallLinkCenteredText)),
			st::boxRowPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 2);
		const auto preview = box->addRow(
			Info::BotStarRef::MakeLinkLabel(box, link, st.linkPreview));
		Ui::AddSkip(box->verticalLayout());

		const auto copyCallback = [=] {
			QApplication::clipboard()->setText(link);
			show->showToast(tr::lng_username_copied(tr::now));
		};
		const auto shareCallback = [=] {
			FastShareLink(
				show,
				link,
				st.shareBox ? *st.shareBox : ShareBoxStyleOverrides());
		};
		preview->setClickedCallback(copyCallback);
		const auto share = box->addButton(
			tr::lng_group_invite_share(),
			shareCallback,
			st::confcallLinkShareButton);
		const auto copy = box->addButton(
			tr::lng_group_invite_copy(),
			copyCallback,
			st::confcallLinkCopyButton);

		rpl::combine(
			box->widthValue(),
			copy->widthValue(),
			share->widthValue()
		) | rpl::start_with_next([=] {
			const auto width = st::boxWideWidth;
			const auto padding = st::confcallLinkBox.buttonPadding;
			const auto available = width - 2 * padding.right();
			const auto buttonWidth = (available - padding.left()) / 2;
			copy->resizeToWidth(buttonWidth);
			share->resizeToWidth(buttonWidth);
			copy->moveToLeft(padding.right(), copy->y(), width);
			share->moveToRight(padding.right(), share->y(), width);
		}, box->lifetime());

		if (!initial) {
			return;
		}

		const auto sep = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_or(),
			st::confcallLinkFooterOr);
		sep->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(sep);
			const auto text = sep->textMaxWidth();
			const auto white = (sep->width() - 2 * text) / 2;
			const auto line = st::lineWidth;
			const auto top = st::confcallLinkFooterOrLineTop;
			const auto fg = st::windowSubTextFg->b;
			p.setOpacity(0.4);
			p.fillRect(0, top, white, line, fg);
			p.fillRect(sep->width() - white, top, white, line, fg);
		}, sep->lifetime());

		const auto footer = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_join(
				lt_link,
				tr::lng_confcall_link_join_link(
					lt_arrow,
					rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
					[](QString v) { return Ui::Text::Link(v); }),
				Ui::Text::WithEntities),
			(st.centerLabel
				? *st.centerLabel
				: st::confcallLinkCenteredText));
		footer->setTryMakeSimilarLines(true);
		footer->setClickHandlerFilter([=](const auto &...) {
			if (auto slug = ExtractConferenceSlug(link); !slug.isEmpty()) {
				Core::App().calls().startOrJoinConferenceCall({
					.call = call,
					.linkSlug = std::move(slug),
				});
			}
			return false;
		});
		copy->geometryValue() | rpl::start_with_next([=](QRect geometry) {
			const auto width = st::boxWideWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right();
			footer->resizeToWidth(width);
			const auto top = geometry.y()
				+ geometry.height()
				+ st::confcallLinkFooterOrTop;
			sep->resizeToWidth(width / 2);
			sep->move(
				st::boxRowPadding.left() + (width - sep->width()) / 2,
				top);
			footer->moveToLeft(
				st::boxRowPadding.left(),
				top + sep->height() + st::confcallLinkFooterOrSkip);
		}, footer->lifetime());
	}));
}

void MakeConferenceCall(ConferenceFactoryArgs &&args) {
	const auto show = std::move(args.show);
	const auto finished = std::move(args.finished);
	const auto session = &show->session();
	const auto fail = [=](QString error) {
		show->showToast(error);
		if (const auto onstack = finished) {
			onstack(false);
		}
	};
	session->api().request(MTPphone_CreateConferenceCall(
		MTP_flags(0),
		MTP_int(base::RandomValue<int32>()),
		MTPint256(), // public_key
		MTPbytes(), // block
		MTPDataJSON() // params
	)).done([=](const MTPUpdates &result) {
		auto call = session->data().sharedConferenceCallFind(result);
		if (!call) {
			fail(u"Call not found!"_q);
			return;
		}
		session->api().applyUpdates(result);

		const auto link = call ? call->conferenceInviteLink() : QString();
		if (link.isEmpty()) {
			fail(u"Call link not found!"_q);
			return;
		}
		Calls::Group::ShowConferenceCallLinkBox(
			show,
			call,
			{ .initial = true });
		if (const auto onstack = finished) {
			finished(true);
		}
	}).fail([=](const MTP::Error &error) {
		fail(error.type());
	}).send();
}

QString ExtractConferenceSlug(const QString &link) {
	const auto local = Core::TryConvertUrlToLocal(link);
	const auto parts1 = QStringView(local).split('#');
	if (!parts1.isEmpty()) {
		const auto parts2 = parts1.front().split('&');
		if (!parts2.isEmpty()) {
			const auto parts3 = parts2.front().split(u"slug="_q);
			if (parts3.size() > 1) {
				return parts3.back().toString();
			}
		}
	}
	return QString();
}

} // namespace Calls::Group
