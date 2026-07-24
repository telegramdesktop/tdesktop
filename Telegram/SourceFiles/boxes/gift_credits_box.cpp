/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_credits_box.h"

#include "api/api_credits.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/star_gift_box.h" // CollectGiftFrequentUsers.
#include "core/ui_integration.h" // TextContext.
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "settings/settings_credits_graphics.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui {
namespace {

[[nodiscard]] object_ptr<RpWidget> MakeFrequentRecipientsList(
		not_null<Main::Session*> session,
		std::vector<not_null<UserData*>> users,
		Fn<void(not_null<PeerData*>)> chosen) {
	class FrequentController final : public PeerListController {
	public:
		FrequentController(
			not_null<Main::Session*> session,
			std::vector<not_null<UserData*>> users,
			Fn<void(not_null<PeerData*>)> chosen)
		: _session(session)
		, _users(std::move(users))
		, _chosen(std::move(chosen)) {
		}

		void prepare() override {
			for (const auto &user : _users) {
				delegate()->peerListAppendRow(
					std::make_unique<PeerListRow>(user));
			}
			delegate()->peerListRefreshRows();
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_chosen(row->peer());
		}
		Main::Session &session() const override {
			return *_session;
		}

	private:
		const not_null<Main::Session*> _session;
		const std::vector<not_null<UserData*>> _users;
		const Fn<void(not_null<PeerData*>)> _chosen;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_settings_top_peers_title()));
	Ui::AddSkip(container, st::defaultVerticalListSkip / 2);

	const auto delegate
		= container->lifetime().make_state<PeerListContentDelegateSimple>();
	const auto controller
		= container->lifetime().make_state<FrequentController>(
			session,
			std::move(users),
			std::move(chosen));
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_contacts_header()));

	return result;
}

} // namespace

void GiftCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		Fn<void()> gifted) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::creditsGiftBox);
	box->setNoContentMargin(true);
	box->addButton(tr::lng_create_group_back(), [=] { box->closeBox(); });

	const auto content = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	const auto &stUser = st::premiumGiftsUserpicButton;
	const auto userpicWrap = content->add(
		object_ptr<Ui::UserpicButton>(content, peer, stUser),
		style::al_top);
	userpicWrap->setAttribute(Qt::WA_TransparentForMouseEvents);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	Settings::AddMiniStars(
		content,
		Ui::CreateChild<Ui::RpWidget>(content),
		stUser.photoSize,
		box->width(),
		2.);
	{
		Ui::AddSkip(content);
		auto link = tr::lng_credits_box_history_entry_gift_about_link(
			lt_emoji,
			rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
			tr::rich
		) | rpl::map([](TextWithEntities text) {
			return tr::link(
				std::move(text),
				u"internal:stars_examples"_q);
		});
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_credits_box_history_entry_gift_out_about(
					lt_user,
					rpl::single(TextWithEntities{ peer->shortName() }),
					lt_link,
					std::move(link),
					tr::rich),
				st::creditsBoxAbout),
			st::boxRowPadding,
			style::al_top);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(box->verticalLayout());

	Settings::FillCreditOptions(
		Main::MakeSessionShow(box->uiShow(), &peer->session()),
		box->verticalLayout(),
		peer,
		CreditsAmount(),
		[=] { gifted(); box->uiShow()->hideLayer(); },
		box->showFinishes(),
		tr::lng_credits_summary_options_subtitle(),
		{});

	box->setPinnedToBottomContent(
		object_ptr<Ui::VerticalLayout>(box));
}

void ShowGiftCreditsBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> gifted) {

	class Controller final : public ContactsBoxController {
	public:
		Controller(
			not_null<Main::Session*> session,
			Fn<void(not_null<PeerData*>)> choose)
		: ContactsBoxController(session)
		, _choose(std::move(choose))
		, _frequentUsers(CollectGiftFrequentUsers(session)) {
		}

	protected:
		std::unique_ptr<PeerListRow> createRow(
				not_null<UserData*> user) override {
			if (user->isSelf()
				|| user->isBot()
				|| user->isServiceUser()
				|| user->isInaccessible()) {
				return nullptr;
			}
			if (ranges::contains(_frequentUsers, user)) {
				return nullptr;
			}
			return ContactsBoxController::createRow(user);
		}

		void prepareViewHook() override {
			if (_frequentUsers.empty()) {
				return;
			}
			delegate()->peerListSetAboveWidget(MakeFrequentRecipientsList(
				&session(),
				_frequentUsers,
				_choose));
		}

		void rowClicked(not_null<PeerListRow*> row) override {
			_choose(row->peer());
		}

	private:
		const Fn<void(not_null<PeerData*>)> _choose;
		const std::vector<not_null<UserData*>> _frequentUsers;

	};
	auto initBox = [=](not_null<PeerListBox*> peersBox) {
		peersBox->setTitle(tr::lng_credits_gift_title());
		peersBox->addButton(tr::lng_cancel(), [=] { peersBox->closeBox(); });
	};

	const auto show = controller->uiShow();
	auto listController = std::make_unique<Controller>(
		&controller->session(),
		[=](not_null<PeerData*> peer) {
			show->showBox(Box(GiftCreditsBox, peer, gifted));
		});
	show->showBox(
		Box<PeerListBox>(std::move(listController), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

} // namespace Ui
