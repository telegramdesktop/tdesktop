/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_to_community_box.h"

#include "api/api_communities.h"
#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "boxes/peers/edit_peer_common.h"
#include "boxes/peer_list_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_add_contact_box.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

struct CommunityCreationState {
	QString title;
	QImage image;
	bool creating = false;
};

class Controller final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	Controller(
		not_null<PeerData*> peer,
		Fn<void(not_null<ChannelData*>)> callback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const;

private:
	void appendRow(not_null<ChannelData*> community);

	const not_null<PeerData*> _peer;
	const Fn<void(not_null<ChannelData*>)> _callback;
	rpl::variable<int> _count = 0;

};

class CommunityIdentityBox final : public Ui::BoxContent {
public:
	CommunityIdentityBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

protected:
	void prepare() override;
	void setInnerFocus() override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void submit();
	void createCommunity(bool visible);

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<PeerData*> _peer;
	const std::shared_ptr<CommunityCreationState> _state;
	object_ptr<Ui::UserpicButton> _photo = { nullptr };
	object_ptr<Ui::InputField> _title = { nullptr };
	Ui::CommunityUserpicEffect _effectCache = {};

};

void ChooseVisibilityBox(
	not_null<Ui::GenericBox*> box,
	rpl::producer<QString> primaryText,
	Fn<void(bool visible)> done);

Controller::Controller(
	not_null<PeerData*> peer,
	Fn<void(not_null<ChannelData*>)> callback)
: _peer(peer)
, _callback(std::move(callback)) {
}

rpl::producer<int> Controller::countValue() const {
	return _count.value();
}

Main::Session &Controller::session() const {
	return _peer->session();
}

void Controller::prepare() {
	session().api().communities().requestJoinedCommunities(crl::guard(
		this,
		[=](const std::vector<not_null<ChannelData*>> &list) {
			for (const auto &community : list) {
				appendRow(community);
			}
			delegate()->peerListRefreshRows();
			_count = delegate()->peerListFullRowsCount();
		}));
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	if (const auto community = row->peer()->asChannel()) {
		_callback(community);
	}
}

void Controller::appendRow(not_null<ChannelData*> community) {
	if (delegate()->peerListFindRow(community->id.value)) {
		return;
	}
	auto row = std::make_unique<PeerListRow>(community);
	delegate()->peerListAppendRow(std::move(row));
}

CommunityIdentityBox::CommunityIdentityBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer)
: _navigation(navigation)
, _peer(peer)
, _state(std::make_shared<CommunityCreationState>()) {
}

void CommunityIdentityBox::prepare() {
	setMouseTracking(true);
	setTitle(tr::lng_community_create_title());

	_photo.create(
		this,
		&_navigation->parentController()->window(),
		Ui::UserpicButton::Role::ChoosePhoto,
		st::defaultUserpicButton,
		Ui::PeerUserpicShape::Forum);
	_photo->showCustomOnChosen();
	_title.create(
		this,
		st::defaultInputField,
		tr::lng_community_create_name());
	_title->setMaxLength(Ui::EditPeer::kMaxGroupChannelTitle);
	_title->setInstantReplaces(Ui::InstantReplaces::Default());
	_title->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue(),
		Core::App().settings().systemTextReplaceValue());
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_title,
		&_navigation->session());

	_title->submits(
	) | rpl::on_next([=] { submit(); }, _title->lifetime());

	addButton(tr::lng_create_group_next(), [=] { submit(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	setDimensions(
		st::boxWideWidth,
		st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::defaultUserpicButton.size.height()
			+ st::boxPadding.bottom()
			+ st::newGroupInfoPadding.bottom());
}

void CommunityIdentityBox::setInnerFocus() {
	_title->setFocusFast();
}

void CommunityIdentityBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_photo->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left(),
		st::boxPadding.top() + st::newGroupInfoPadding.top());

	const auto nameLeft = st::defaultUserpicButton.size.width()
		+ st::newGroupNamePosition.x();
	_title->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupInfoPadding.left()
			- st::boxPadding.right()
			- nameLeft,
		_title->height());
	_title->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left() + nameLeft,
		st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::newGroupNamePosition.y());
}

void CommunityIdentityBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	auto p = Painter(this);
	const auto origin = _photo->mapTo(this, QPoint());
	Ui::PaintCommunityUserpicEffect(
		p,
		_effectCache,
		origin.x(),
		origin.y(),
		_photo->width(),
		st::windowSubTextFg->c);
}

void CommunityIdentityBox::submit() {
	const auto title = _title->getLastText().trimmed();
	if (title.isEmpty()) {
		_title->setFocus();
		_title->showError();
		return;
	}
	_state->title = title;
	if (auto image = _photo->takeResultImage(); !image.isNull()) {
		_state->image = std::move(image);
	}
	uiShow()->showBox(Box(
		ChooseVisibilityBox,
		tr::lng_create_group_create(),
		crl::guard(
			this,
			[=](bool visible) { createCommunity(visible); })));
}

void CommunityIdentityBox::createCommunity(bool visible) {
	if (_state->creating) {
		return;
	}
	const auto state = _state;
	const auto show = uiShow();
	state->creating = true;
	_navigation->session().api().communities().create(
		state->title,
		QString(),
		_peer,
		!visible,
		crl::guard(
			this,
			[state, show](not_null<ChannelData*> community) {
				if (!state->image.isNull()) {
					community->session().api().peerPhoto().upload(
						community,
						{ std::move(state->image) });
				}
				show->hideLayer();
				show->showToast(tr::lng_community_created(tr::now));
			}),
		crl::guard(
			this,
			[state, show](const QString &error) {
				state->creating = false;
				show->showToast(error.isEmpty()
					? Lang::Hard::ServerError()
					: error);
			}));
}

void ChooseVisibilityBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> primaryText,
		Fn<void(bool visible)> done) {
	box->setTitle(tr::lng_community_visibility_title());

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(1);
	const auto addOption = [&](
			int value,
			const QString &label,
			rpl::producer<QString> about) {
		box->addSkip(st::editPeerHistoryVisibilityTopSkip);
		const auto wrap = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		wrap->add(object_ptr<Ui::Radiobutton>(
			box,
			group,
			value,
			label,
			st::defaultBoxCheckbox));
		wrap->add(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::editPeerPrivacyLabel),
			st::editPeerPreHistoryLabelMargins);
		const auto button = Ui::CreateChild<Ui::AbstractButton>(wrap);
		wrap->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			button->resize(s);
		}, button->lifetime());
		button->setClickedCallback([=] { group->setValue(value); });
	};
	addOption(
		1,
		tr::lng_community_visibility_visible(tr::now),
		tr::lng_community_visibility_visible_about());
	addOption(
		0,
		tr::lng_community_visibility_hidden(tr::now),
		tr::lng_community_visibility_hidden_about());

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_community_visibility_note(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins);

	box->addButton(std::move(primaryText), [=] {
		done(group->current() == 1);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ShowAddPeerToCommunity(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		Fn<void()> completed) {
	struct State {
		base::weak_qptr<Ui::GenericBox> visibility;
		bool linking = false;
	};
	const auto state = completed ? std::make_shared<State>() : nullptr;
	const auto show = navigation->uiShow();
	const auto finish = [=] {
		if (state) {
			const auto visibility = base::take(state->visibility);
			if (visibility) {
				visibility->closeBox();
			}
			completed();
		} else {
			show->hideLayer();
		}
	};
	const auto add = [=](bool visible) {
		if (state) {
			if (state->linking) {
				return;
			}
			state->linking = true;
		}
		const auto sure = [=](Fn<void()> &&close) {
			close();
			peer->session().api().communities().addPeerLink(
				community,
				peer,
				visible,
				[=] {
					finish();
					show->showToast(peer->isUser()
						? tr::lng_community_add_done_bot(tr::now)
						: peer->isBroadcast()
						? tr::lng_community_add_done_channel(tr::now)
						: tr::lng_community_add_done(tr::now));
				},
				[=](const QString &error) {
					if (error == Api::kCommunityRequestCreated.utf16()) {
						finish();
						show->showToast(
							tr::lng_community_request_sent(tr::now));
						return;
					}
					if (state) {
						state->linking = false;
					}
					if (error == Api::kCommunityPeersTooMuch.utf16()) {
						show->showToast(
							Api::CommunityPeersLimitToast(peer));
					} else {
						show->showToast(error.isEmpty()
							? Lang::Hard::ServerError()
							: error);
					}
				});
		};
		// A community admin adds chats directly; everyone else can only
		// suggest a chat, which a community admin must then approve.
		if (community->canManageLinkedPeers()) {
			sure([] {});
		} else {
			auto args = Ui::ConfirmBoxArgs{
				.text = (peer->isUser()
					? tr::lng_community_add_confirm_bot()
					: peer->isBroadcast()
					? tr::lng_community_add_confirm_channel()
					: tr::lng_community_add_confirm()),
				.confirmed = sure,
				.confirmText = tr::lng_community_add_confirm_add(),
				.title = tr::lng_community_add_to(),
			};
			if (state) {
				args.cancelled = [=](Fn<void()> close) {
					state->linking = false;
					close();
				};
			}
			show->showBox(Ui::MakeConfirmBox(std::move(args)));
		}
	};
	auto box = Box(
		ChooseVisibilityBox,
		tr::lng_community_add_to(),
		add);
	if (state) {
		state->visibility = show->show(std::move(box));
	} else {
		show->showBox(std::move(box));
	}
}

void ShowAddToCommunityBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	const auto choose = [=](not_null<ChannelData*> community) {
		ShowAddPeerToCommunity(navigation, community, peer);
	};
	auto controller = std::make_unique<Controller>(peer, choose);
	const auto raw = controller.get();
	const auto init = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_community_title());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });

		auto above = object_ptr<Ui::VerticalLayout>(box);
		Ui::AddDividerText(above, peer->isUser()
			? tr::lng_community_add_about_bot()
			: peer->isBroadcast()
			? tr::lng_community_add_about_channel()
			: tr::lng_community_add_about());
		Ui::AddSkip(above);
		Settings::AddButtonWithIcon(
			above,
			tr::lng_community_create(),
			st::infoCreateDiscussionLinkButton,
			{ &st::menuBlueIconGroupCreate }
		)->addClickHandler([=] {
			navigation->uiShow()->showBox(
				Box<CommunityIdentityBox>(navigation, peer));
		});
		const auto wrap = above->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				above,
				object_ptr<Ui::VerticalLayout>(above)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddDivider(inner);
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, tr::lng_community_existing());
		wrap->toggleOn(
			raw->countValue() | rpl::map(rpl::mappers::_1 > 0),
			anim::type::instant);
		wrap->finishAnimating();
		box->peerListSetAboveWidget(std::move(above));
	};
	navigation->uiShow()->showBox(Box<PeerListBox>(
		std::move(controller),
		init));
}
