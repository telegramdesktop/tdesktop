/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_link.h"

#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/controls/invite_link_label.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/popup_menu.h"
#include "ui/abstract_button.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/edit_invite_link.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "boxes/share_box.h"
#include "history/view/history_view_group_call_bar.h" // GenerateUserpics...
#include "history/history_item_helpers.h" // GetErrorTextForSending.
#include "history/history.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "mtproto/sender.h"
#include "qr/qr_generate.h"
#include "intro/intro_qr.h" // TelegramLogoImage
#include "styles/style_boxes.h"
#include "styles/style_layers.h" // st::boxDividerLabel.
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtCore/QMimeData>

namespace {

constexpr auto kFirstPage = 20;
constexpr auto kPerPage = 100;
constexpr auto kShareQrSize = 768;
constexpr auto kShareQrPadding = 16;

using LinkData = Api::InviteLink;

void ShowPeerInfoSync(not_null<PeerData*> peer) {
	// While a peer info is demanded by the left click
	// we can safely use activeWindow.
	if (const auto window = Core::App().activeWindow()) {
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				controller->showPeerInfo(peer);
			}
		}
	}
}

class RequestedRow final : public PeerListRow {
public:
	RequestedRow(not_null<PeerData*> peer, TimeId date);

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

};

RequestedRow::RequestedRow(not_null<PeerData*> peer, TimeId date)
: PeerListRow(peer) {
	setCustomStatus(PrepareRequestedRowStatus(date));
}

QSize RequestedRow::rightActionSize() const {
	return QSize(
		st::inviteLinkThreeDotsIcon.width(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins RequestedRow::rightActionMargins() const {
	return QMargins(
		0,
		(st::peerListBoxItem.height - rightActionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void RequestedRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	(actionSelected
		? st::inviteLinkThreeDotsIconOver
		: st::inviteLinkThreeDotsIcon).paint(p, x, y, outerWidth);
}

class Controller final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	enum class Role {
		Requested,
		Joined,
	};
	Controller(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		rpl::producer<LinkData> data,
		Role role);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	rpl::producer<int> boxHeightValue() const override;
	int descriptionTopSkipMin() const override;

	struct Processed {
		not_null<UserData*> user;
		bool approved = false;
	};
	[[nodiscard]] rpl::producer<Processed> processed() const {
		return _processed.events();
	}

private:
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void setupAboveJoinedWidget();
	void appendSlice(const Api::JoinedByLinkSlice &slice);
	void addHeaderBlock(not_null<Ui::VerticalLayout*> container);
	not_null<Ui::SlideWrap<>*> addRequestedListBlock(
		not_null<Ui::VerticalLayout*> container);
	void updateWithProcessed(Processed processed);

	[[nodiscard]] rpl::producer<LinkData> dataValue() const;

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);
	void processRequest(not_null<UserData*> user, bool approved);

	const not_null<PeerData*> _peer;
	const Role _role = Role::Joined;
	rpl::variable<LinkData> _data;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<Processed> _processed;

	QString _link;
	bool _revoked = false;

	mtpRequestId _requestId = 0;
	std::optional<Api::JoinedByLinkUser> _lastUser;
	bool _allLoaded = false;

	Ui::RpWidget *_headerWidget = nullptr;
	rpl::variable<int> _addedHeight;

	MTP::Sender _api;
	rpl::lifetime _lifetime;

};

class SingleRowController final : public PeerListController {
public:
	SingleRowController(
		not_null<PeerData*> peer,
		rpl::producer<QString> status);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const not_null<PeerData*> _peer;
	rpl::producer<QString> _status;
	rpl::lifetime _lifetime;

};

[[nodiscard]] bool ClosingLinkBox(const LinkData &updated, bool revoked) {
	return updated.link.isEmpty() || (!revoked && updated.revoked);
}

QImage QrExact(const Qr::Data &data, int pixel, QColor color) {
	const auto image = [](int size) {
		auto result = QImage(
			size,
			size,
			QImage::Format_ARGB32_Premultiplied);
		result.fill(Qt::transparent);
		{
			QPainter p(&result);
			const auto skip = size / 12;
			const auto logoSize = size - 2 * skip;
			p.drawImage(
				skip,
				skip,
				Window::LogoNoMargin().scaled(
					logoSize,
					logoSize,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation));
		}
		return result;
	};
	return Qr::ReplaceCenter(
		Qr::Generate(data, pixel, color),
		image(Qr::ReplaceSize(data, pixel)));
}

QImage Qr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	return QrExact(data, pixel * style::DevicePixelRatio(), st::windowFg->c);
}

QImage Qr(const QString &text, int pixel, int max) {
	return Qr(Qr::Encode(text), pixel, max);
}

QImage QrForShare(const QString &text) {
	const auto data = Qr::Encode(text);
	const auto size = (kShareQrSize - 2 * kShareQrPadding);
	const auto image = QrExact(data, size / data.size, Qt::black);
	auto result = QImage(
		kShareQrPadding * 2 + image.width(),
		kShareQrPadding * 2 + image.height(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	{
		auto p = QPainter(&result);
		p.drawImage(kShareQrPadding, kShareQrPadding, image);
	}
	return result;
}

void QrBox(
		not_null<Ui::GenericBox*> box,
		const QString &link,
		rpl::producer<QString> title,
		rpl::producer<QString> about,
		Fn<void(QImage, std::shared_ptr<Ui::Show>)> share) {
	box->setTitle(std::move(title));

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });

	const auto copyCallback = [=, show = box->uiShow()] {
		share(QrForShare(link), show);
	};

	const auto qr = Qr(
		link,
		st::inviteLinkQrPixel,
		st::boxWidth - st::boxRowPadding.left() - st::boxRowPadding.right());
	const auto size = qr.width() / style::DevicePixelRatio();
	const auto height = st::inviteLinkQrSkip * 2 + size;
	const auto container = box->addRow(
		object_ptr<Ui::BoxContentDivider>(box, height),
		st::inviteLinkQrMargin);
	const auto button = Ui::CreateChild<Ui::AbstractButton>(container);
	button->resize(size, size);
	button->paintRequest(
	) | rpl::start_with_next([=] {
		QPainter(button).drawImage(QRect(0, 0, size, size), qr);
	}, button->lifetime());
	container->widthValue(
	) | rpl::start_with_next([=](int width) {
		button->move((width - size) / 2, st::inviteLinkQrSkip);
	}, button->lifetime());
	button->setClickedCallback(copyCallback);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(about),
			st::boxLabel),
		st::inviteLinkQrValuePadding);

	box->addLeftButton(tr::lng_group_invite_context_copy(), copyCallback);
}

Controller::Controller(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	rpl::producer<LinkData> data,
	Role role)
: _peer(peer)
, _role(role)
, _data(LinkData{ .admin = admin })
, _api(&session().api().instance()) {
	_data = std::move(data);
	const auto current = _data.current();
	_link = current.link;
	_revoked = current.revoked;
}

rpl::producer<LinkData> Controller::dataValue() const {
	return _data.value(
	) | rpl::filter([=](const LinkData &data) {
		return !ClosingLinkBox(data, _revoked);
	});
}

void Controller::addHeaderBlock(not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto current = _data.current();
	const auto revoked = current.revoked;
	const auto link = current.link;
	const auto admin = current.admin;
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		CopyInviteLink(delegate()->peerListUiShow(), link);
	});
	const auto shareLink = crl::guard(weak, [=, peer = _peer] {
		delegate()->peerListUiShow()->showBox(ShareInviteLinkBox(peer, link));
	});
	const auto getLinkQr = crl::guard(weak, [=] {
		delegate()->peerListUiShow()->showBox(InviteLinkQrBox(
			link,
			tr::lng_group_invite_qr_title(),
			tr::lng_group_invite_qr_about()));
	});
	const auto revokeLink = crl::guard(weak, [=] {
		delegate()->peerListUiShow()->showBox(
			RevokeLinkBox(_peer, admin, link));
	});
	const auto editLink = crl::guard(weak, [=] {
		delegate()->peerListUiShow()->showBox(
			EditLinkBox(_peer, _data.current()));
	});
	const auto deleteLink = crl::guard(weak, [=] {
		delegate()->peerListUiShow()->showBox(
			DeleteLinkBox(_peer, admin, link));
	});

	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(
			container,
			st::popupMenuWithIcons);
		if (revoked) {
			result->addAction(
				tr::lng_group_invite_context_delete(tr::now),
				deleteLink,
				&st::menuIconDelete);
		} else {
			result->addAction(
				tr::lng_group_invite_context_copy(tr::now),
				copyLink,
				&st::menuIconCopy);
			result->addAction(
				tr::lng_group_invite_context_share(tr::now),
				shareLink,
				&st::menuIconShare);
			result->addAction(
				tr::lng_group_invite_context_qr(tr::now),
				getLinkQr,
				&st::menuIconQrCode);
			if (!admin->isBot()) {
				result->addAction(
					tr::lng_group_invite_context_edit(tr::now),
					editLink,
					&st::menuIconEdit);
				result->addAction(
					tr::lng_group_invite_context_revoke(tr::now),
					revokeLink,
					&st::menuIconRemove);
			}
		}
		return result;
	};

	const auto prefix = u"https://"_q;
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		rpl::single(link.startsWith(prefix)
			? link.mid(prefix.size())
			: link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	const auto reactivateWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(
				container)));
	const auto copyShareWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(
				container)));

	AddReactivateLinkButton(reactivateWrap->entity(), editLink);
	AddCopyShareLinkButtons(copyShareWrap->entity(), copyLink, shareLink);
	if (revoked) {
		AddDeleteLinkButton(container, deleteLink);
	}

	Ui::AddSkip(container, st::inviteLinkJoinedRowPadding.bottom() * 2);

	auto grayLabelText = dataValue(
	) | rpl::map([=](const LinkData &data) {
		const auto usageExpired = (data.usageLimit > 0)
			&& (data.usageLimit <= data.usage);
		return usageExpired
			? tr::lng_group_invite_used_about()
			: tr::lng_group_invite_expires_at(
				lt_when,
				rpl::single(langDateTime(
					base::unixtime::parse(data.expireDate))));
	}) | rpl::flatten_latest();

	const auto redLabelWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::DividerLabel>>(
			container,
			object_ptr<Ui::DividerLabel>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_group_invite_expired_about(),
					st::boxAttentionDividerLabel),
				st::defaultBoxDividerLabelPadding)));
	const auto grayLabelWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::DividerLabel>>(
			container,
			object_ptr<Ui::DividerLabel>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					std::move(grayLabelText),
					st::boxDividerLabel),
				st::defaultBoxDividerLabelPadding)));
	const auto justDividerWrap = container->add(
		object_ptr<Ui::SlideWrap<>>(
			container,
			object_ptr<Ui::BoxContentDivider>(container)));
	Ui::AddSkip(container);

	dataValue(
	) | rpl::start_with_next([=](const LinkData &data) {
		const auto now = base::unixtime::now();
		const auto expired = IsExpiredLink(data, now);
		reactivateWrap->toggle(
			!revoked && expired && !admin->isBot(),
			anim::type::instant);
		copyShareWrap->toggle(!revoked && !expired, anim::type::instant);

		const auto timeExpired = (data.expireDate > 0)
			&& (data.expireDate <= now);
		const auto usageExpired = (data.usageLimit > 0)
			&& (data.usageLimit <= data.usage);
		redLabelWrap->toggle(!revoked && timeExpired, anim::type::instant);
		grayLabelWrap->toggle(
			!revoked && !timeExpired && (data.expireDate > 0 || usageExpired),
			anim::type::instant);
		justDividerWrap->toggle(
			revoked || (!data.expireDate && !expired),
			anim::type::instant);
	}, lifetime());
}

not_null<Ui::SlideWrap<>*> Controller::addRequestedListBlock(
		not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	auto result = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(
				container)));
	const auto wrap = result->entity();
	// Make this container occupy full width.
	wrap->add(object_ptr<Ui::RpWidget>(wrap));
	Ui::AddDivider(wrap);
	Ui::AddSkip(wrap);
	auto requestedCount = dataValue(
	) | rpl::filter([](const LinkData &data) {
		return data.requested > 0;
	}) | rpl::map([=](const LinkData &data) {
		return float64(data.requested);
	});
	Ui::AddSubsectionTitle(
		wrap,
		tr::lng_group_invite_requested_full(
			lt_count_decimal,
			std::move(requestedCount)));

	class Delegate final : public PeerListContentDelegateSimple {
	public:
		explicit Delegate(std::shared_ptr<Main::SessionShow> show)
		: _show(std::move(show)) {
		}

		std::shared_ptr<Main::SessionShow> peerListUiShow() override {
			return _show;
		}

	private:
		const std::shared_ptr<Main::SessionShow> _show;

	};
	const auto delegate = container->lifetime().make_state<Delegate>(
		this->delegate()->peerListUiShow());

	const auto controller = container->lifetime().make_state<
		Controller
	>(_peer, _data.current().admin, _data.value(), Role::Requested);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	controller->processed(
	) | rpl::start_with_next([=](Processed processed) {
		updateWithProcessed(processed);
	}, lifetime());

	return result;
}

void Controller::prepare() {
	if (_role == Role::Joined) {
		setupAboveJoinedWidget();

		_allLoaded = (_data.current().usage == 0);

		const auto &inviteLinks = session().api().inviteLinks();
		const auto slice = inviteLinks.joinedFirstSliceLoaded(_peer, _link);
		if (slice) {
			appendSlice(*slice);
		}
	} else {
		_allLoaded = (_data.current().requested == 0);
	}
	loadMoreRows();
}

void Controller::updateWithProcessed(Processed processed) {
	const auto user = processed.user;
	auto updated = _data.current();
	if (processed.approved) {
		++updated.usage;
		if (!delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListPrependRow(
				std::make_unique<PeerListRow>(user));
			delegate()->peerListRefreshRows();
		}
	}
	if (updated.requested > 0) {
		--updated.requested;
	}
	session().api().inviteLinks().applyExternalUpdate(_peer, updated);
}

void Controller::setupAboveJoinedWidget() {
	using namespace Settings;

	auto header = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = header.data();

	const auto current = _data.current();
	const auto revoked = current.revoked;
	if (revoked || !current.permanent) {
		addHeaderBlock(container);
	}
	Ui::AddSubsectionTitle(
		container,
		tr::lng_group_invite_created_by());
	AddSinglePeerRow(
		container,
		current.admin,
		rpl::single(langDateTime(base::unixtime::parse(current.date))));
	Ui::AddSkip(container, st::membersMarginBottom);

	auto requestedWrap = addRequestedListBlock(container);

	const auto listHeaderWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(
				container)));
	const auto listHeader = listHeaderWrap->entity();

	// Make this container occupy full width.
	listHeader->add(object_ptr<Ui::RpWidget>(listHeader));

	Ui::AddDivider(listHeader);
	Ui::AddSkip(listHeader);

	auto listHeaderText = dataValue(
	) | rpl::map([=](const LinkData &data) {
		const auto now = base::unixtime::now();
		const auto timeExpired = (data.expireDate > 0)
			&& (data.expireDate <= now);
		if (!revoked && !data.usage && data.usageLimit > 0 && !timeExpired) {
			auto description = object_ptr<Ui::FlatLabel>(
				nullptr,
				tr::lng_group_invite_can_join_via_link(
					tr::now,
					lt_count,
					data.usageLimit),
				computeListSt().about);
			if (!delegate()->peerListFullRowsCount()) {
				using namespace rpl::mappers;
				_addedHeight = description->heightValue(
				) | rpl::map(_1
					+ st::membersAboutLimitPadding.top()
					+ st::membersAboutLimitPadding.bottom());
			}
			delegate()->peerListSetDescription(std::move(description));
		} else {
			_addedHeight = std::max(
				data.usage,
				delegate()->peerListFullRowsCount()
			) * computeListSt().item.height;
			delegate()->peerListSetDescription(nullptr);
		}
		listHeaderWrap->toggle(
			!revoked && (data.usage || (data.usageLimit > 0 && !timeExpired)),
			anim::type::instant);
		delegate()->peerListRefreshRows();
		return data.usage
			? tr::lng_group_invite_joined(
				lt_count,
				rpl::single(float64(data.usage)))
			: tr::lng_group_invite_no_joined();
	}) | rpl::flatten_latest();
	const auto listTitle = AddSubsectionTitle(
		listHeader,
		std::move(listHeaderText));
	auto remainingText = dataValue(
	) | rpl::map([=](const LinkData &data) {
		return !data.usageLimit
			? QString()
			: tr::lng_group_invite_remaining(
				tr::now,
				lt_count_decimal,
				std::max(data.usageLimit - data.usage, 0));
	});
	const auto remaining = Ui::CreateChild<Ui::FlatLabel>(
		listHeader,
		std::move(remainingText),
		st::inviteLinkTitleRight);
	dataValue(
	) | rpl::start_with_next([=](const LinkData &data) {
		remaining->setTextColorOverride(
			(data.usageLimit && (data.usageLimit <= data.usage)
				? std::make_optional(st::boxTextFgError->c)
				: std::nullopt));
		if (revoked || (!data.usage && data.usageLimit > 0)) {
			remaining->hide();
		} else {
			remaining->show();
		}

		requestedWrap->toggle(data.requested > 0, anim::type::instant);
	}, remaining->lifetime());

	rpl::combine(
		listTitle->positionValue(),
		remaining->widthValue(),
		listHeader->widthValue()
	) | rpl::start_with_next([=](
			QPoint position,
			int width,
			int outerWidth) {
		remaining->moveToRight(position.x(), position.y(), outerWidth);
	}, remaining->lifetime());

	_headerWidget = header.data();

	delegate()->peerListSetAboveWidget(std::move(header));
}

void Controller::loadMoreRows() {
	if (_requestId || _allLoaded) {
		return;
	}
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	_requestId = _api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(Flag::f_link
			| (_role == Role::Requested ? Flag::f_requested : Flag(0))),
		_peer->input,
		MTP_string(_link),
		MTPstring(), // q
		MTP_int(_lastUser ? _lastUser->date : 0),
		_lastUser ? _lastUser->user->inputUser : MTP_inputUserEmpty(),
		MTP_int(_lastUser ? kPerPage : kFirstPage)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		_requestId = 0;
		auto slice = Api::ParseJoinedByLinkSlice(_peer, result);
		_allLoaded = slice.users.empty();
		appendSlice(slice);
	}).fail([=] {
		_requestId = 0;
		_allLoaded = true;
	}).send();
}

void Controller::appendSlice(const Api::JoinedByLinkSlice &slice) {
	for (const auto &user : slice.users) {
		_lastUser = user;
		auto row = (_role == Role::Requested)
			? std::make_unique<RequestedRow>(user.user, user.date)
			: std::make_unique<PeerListRow>(user.user);
		if (_role != Role::Requested && user.viaFilterLink) {
			row->setCustomStatus(
				tr::lng_group_invite_joined_via_filter(tr::now));
		}
		delegate()->peerListAppendRow(std::move(row));
	}
	delegate()->peerListRefreshRows();
	if (delegate()->peerListFullRowsCount() > 0) {
		_addedHeight = std::max(
			_data.current().usage,
			delegate()->peerListFullRowsCount()
		) * computeListSt().item.height;
	}
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	ShowPeerInfoSync(row->peer());
}

void Controller::rowRightActionClicked(not_null<PeerListRow*> row) {
	if (_role != Role::Requested) {
		return;
	}
	delegate()->peerListShowRowMenu(row, true);
}

base::unique_qptr<Ui::PopupMenu> Controller::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = createRowContextMenu(parent, row);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

base::unique_qptr<Ui::PopupMenu> Controller::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto user = row->peer()->asUser();
	Assert(user != nullptr);

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto add = _peer->isBroadcast()
		? tr::lng_group_requests_add_channel(tr::now)
		: tr::lng_group_requests_add(tr::now);
	result->addAction(add, [=] {
		processRequest(user, true);
	}, &st::menuIconInvite);
	result->addAction(tr::lng_group_requests_dismiss(tr::now), [=] {
		processRequest(user, false);
	}, &st::menuIconRemove);
	return result;
}

void Controller::processRequest(
		not_null<UserData*> user,
		bool approved) {
	const auto done = crl::guard(this, [=] {
		_processed.fire({ user, approved });
		if (const auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
			delegate()->peerListRefreshRows();
		}
		if (approved) {
			delegate()->peerListUiShow()->showToast((_peer->isBroadcast()
				? tr::lng_group_requests_was_added_channel
				: tr::lng_group_requests_was_added)(
					tr::now,
					lt_user,
					Ui::Text::Bold(user->name()),
					Ui::Text::WithEntities));
		}
	});
	const auto fail = crl::guard(this, [=] {
		_processed.fire({ user, false });
	});
	session().api().inviteLinks().processRequest(
		_peer,
		_data.current().link,
		user,
		approved,
		done,
		fail);
}

Main::Session &Controller::session() const {
	return _peer->session();
}

rpl::producer<int> Controller::boxHeightValue() const {
	Expects(_headerWidget != nullptr);

	return rpl::combine(
		_headerWidget->heightValue(),
		_addedHeight.value()
	) | rpl::map([=](int header, int description) {
		const auto wrapped = description
			? (computeListSt().padding.top()
				+ description
				+ computeListSt().padding.bottom())
			: 0;
		return std::min(header + wrapped, st::boxMaxListHeight);
	});
}

int Controller::descriptionTopSkipMin() const {
	return 0;
}

SingleRowController::SingleRowController(
	not_null<PeerData*> peer,
	rpl::producer<QString> status)
: _peer(peer)
, _status(std::move(status)) {
}

void SingleRowController::prepare() {
	auto row = std::make_unique<PeerListRow>(_peer);

	const auto raw = row.get();
	std::move(
		_status
	) | rpl::start_with_next([=](const QString &status) {
		raw->setCustomStatus(status);
		delegate()->peerListUpdateRow(raw);
	}, _lifetime);

	delegate()->peerListAppendRow(std::move(row));
	delegate()->peerListRefreshRows();
}

void SingleRowController::loadMoreRows() {
}

void SingleRowController::rowClicked(not_null<PeerListRow*> row) {
	ShowPeerInfoSync(row->peer());
}

Main::Session &SingleRowController::session() const {
	return _peer->session();
}

} // namespace

bool IsExpiredLink(const Api::InviteLink &data, TimeId now) {
	return (data.expireDate > 0 && data.expireDate <= now)
		|| (data.usageLimit > 0 && data.usageLimit <= data.usage);
}

void AddSinglePeerRow(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		rpl::producer<QString> status) {
	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		SingleRowController
	>(peer, std::move(status));
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);
}

void AddPermanentLinkBlock(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		rpl::producer<Api::InviteLink> fromList) {
	struct LinkData {
		QString link;
		int usage = 0;
	};
	const auto value = container->lifetime().make_state<
		rpl::variable<LinkData>
	>();
	const auto currentLinkFields = container->lifetime().make_state<
		Api::InviteLink
	>(Api::InviteLink{ .admin = admin });
	if (admin->isSelf()) {
		*value = peer->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::InviteLinks
		) | rpl::map([=] {
			const auto &links = peer->session().api().inviteLinks().myLinks(
				peer).links;
			const auto link = links.empty() ? nullptr : &links.front();
			if (link && link->permanent && !link->revoked) {
				*currentLinkFields = *link;
				return LinkData{ link->link, link->usage };
			}
			return LinkData();
		});
	} else {
		rpl::duplicate(
			fromList
		) | rpl::start_with_next([=](const Api::InviteLink &link) {
			*currentLinkFields = link;
		}, container->lifetime());

		*value = std::move(
			fromList
		) | rpl::map([](const Api::InviteLink &link) {
			return LinkData{ link.link, link.usage };
		});
	}
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			CopyInviteLink(show, current.link);
		}
	});
	const auto shareLink = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			show->showBox(ShareInviteLinkBox(peer, current.link));
		}
	});
	const auto getLinkQr = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			show->showBox(InviteLinkQrBox(
				current.link,
				tr::lng_group_invite_qr_title(),
				tr::lng_group_invite_qr_about()));
		}
	});
	const auto revokeLink = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			show->showBox(RevokeLinkBox(peer, admin, current.link, true));
		}
	});

	auto link = value->value(
	) | rpl::map([=](const LinkData &data) {
		const auto prefix = u"https://"_q;
		return data.link.startsWith(prefix)
			? data.link.mid(prefix.size())
			: data.link;
	});
	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(
			container,
			st::popupMenuWithIcons);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink,
			&st::menuIconCopy);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink,
			&st::menuIconShare);
		result->addAction(
			tr::lng_group_invite_context_qr(tr::now),
			getLinkQr,
			&st::menuIconQrCode);
		if (!admin->isBot()) {
			result->addAction(
				tr::lng_group_invite_context_revoke(tr::now),
				revokeLink,
				&st::menuIconRemove);
		}
		return result;
	};
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		std::move(link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	AddCopyShareLinkButtons(container, copyLink, shareLink);

	struct JoinedState {
		QImage cachedUserpics;
		std::vector<HistoryView::UserpicInRow> list;
		int count = 0;
		bool allUserpicsLoaded = false;
		rpl::variable<Ui::JoinedCountContent> content;
		rpl::lifetime lifetime;
	};
	const auto state = container->lifetime().make_state<JoinedState>();
	const auto push = [=] {
		HistoryView::GenerateUserpicsInRow(
			state->cachedUserpics,
			state->list,
			st::inviteLinkUserpics,
			0);
		state->allUserpicsLoaded = ranges::all_of(
			state->list,
			[](const HistoryView::UserpicInRow &element) {
				return !element.peer->hasUserpic()
					|| !Ui::PeerUserpicLoading(element.view);
			});
		state->content = Ui::JoinedCountContent{
			.count = state->count,
			.userpics = state->cachedUserpics,
		};
	};
	value->value(
	) | rpl::map([=](const LinkData &data) {
		return peer->session().api().inviteLinks().joinedFirstSliceValue(
			peer,
			data.link,
			data.usage);
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](const Api::JoinedByLinkSlice &slice) {
		auto list = std::vector<HistoryView::UserpicInRow>();
		list.reserve(slice.users.size());
		for (const auto &item : slice.users) {
			const auto i = ranges::find(
				state->list,
				item.user,
				&HistoryView::UserpicInRow::peer);
			if (i != end(state->list)) {
				list.push_back(std::move(*i));
			} else {
				list.push_back({ item.user });
			}
		}
		state->count = slice.count;
		state->list = std::move(list);
		push();
	}, state->lifetime);

	peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !state->allUserpicsLoaded;
	}) | rpl::start_with_next([=] {
		auto pushing = false;
		state->allUserpicsLoaded = true;
		for (const auto &element : state->list) {
			if (!element.peer->hasUserpic()) {
				continue;
			} else if (element.peer->userpicUniqueKey(element.view)
				!= element.uniqueKey) {
				pushing = true;
			} else if (Ui::PeerUserpicLoading(element.view)) {
				state->allUserpicsLoaded = false;
			}
		}
		if (pushing) {
			push();
		}
	}, state->lifetime);

	Ui::AddJoinedCountButton(
		container,
		state->content.value(),
		st::inviteLinkJoinedRowPadding
	)->setClickedCallback([=] {
		if (!currentLinkFields->link.isEmpty()) {
			show->showBox(ShowInviteLinkBox(peer, *currentLinkFields));
		}
	});

	container->add(object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
		container,
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::inviteLinkJoinedRowPadding.bottom()))
	)->setDuration(0)->toggleOn(state->content.value(
	) | rpl::map([=](const Ui::JoinedCountContent &content) {
		return (content.count <= 0);
	}));
}

void CopyInviteLink(std::shared_ptr<Ui::Show> show, const QString &link) {
	QGuiApplication::clipboard()->setText(link);
	show->showToast(tr::lng_group_invite_copied(tr::now));
}

object_ptr<Ui::BoxContent> ShareInviteLinkBox(
		not_null<PeerData*> peer,
		const QString &link,
		const QString &copied) {
	return ShareInviteLinkBox(&peer->session(), link, copied);
}

object_ptr<Ui::BoxContent> ShareInviteLinkBox(
		not_null<Main::Session*> session,
		const QString &link,
		const QString &copied) {
	const auto sending = std::make_shared<bool>();
	const auto box = std::make_shared<QPointer<ShareBox>>();

	const auto showToast = [=](const QString &text) {
		if (*box) {
			(*box)->showToast(text);
		}
	};

	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(link);
		showToast(copied.isEmpty()
			? tr::lng_group_invite_copied(tr::now)
			: copied);
	};
	auto submitCallback = [=](
			std::vector<not_null<Data::Thread*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardOptions) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto thread : result) {
				const auto error = GetErrorTextForSending(
					thread,
					{ .text = &comment });
				if (!error.isEmpty()) {
					return std::make_pair(error, thread);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->chatListName())
				).append("\n\n");
			}
			text.append(error.first);
			if (*box) {
				(*box)->uiShow()->showBox(Ui::MakeInformBox(text));
			}
			return;
		}

		*sending = true;
		if (!comment.text.isEmpty()) {
			comment.text = link + "\n" + comment.text;
			const auto add = link.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		} else {
			comment.text = link;
		}
		auto &api = session->api();
		for (const auto thread : result) {
			auto message = Api::MessageToSend(
				Api::SendAction(thread, options));
			message.textWithTags = comment;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		if (*box) {
			showToast(tr::lng_share_done(tr::now));
			(*box)->closeBox();
		}
	};
	auto filterCallback = [](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreRequirePremium()) {
				return true;
			}
		}
		return Data::CanSendTexts(thread);
	};
	auto object = Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyCallback),
		.submitCallback = std::move(submitCallback),
		.filterCallback = std::move(filterCallback),
		.premiumRequiredError = SharePremiumRequiredError(),
	});
	*box = Ui::MakeWeak(object.data());
	return object;
}

object_ptr<Ui::BoxContent> InviteLinkQrBox(
		const QString &link,
		rpl::producer<QString> title,
		rpl::producer<QString> about) {
	return Box(QrBox, link, std::move(title), std::move(about), [=](
			const QImage &image,
			std::shared_ptr<Ui::Show> show) {
		auto mime = std::make_unique<QMimeData>();
		mime->setImageData(image);
		QGuiApplication::clipboard()->setMimeData(mime.release());
		show->showToast(tr::lng_group_invite_qr_copied(tr::now));
	});
}

object_ptr<Ui::BoxContent> EditLinkBox(
		not_null<PeerData*> peer,
		const Api::InviteLink &data) {
	const auto creating = data.link.isEmpty();
	const auto box = std::make_shared<QPointer<Ui::GenericBox>>();
	using Fields = Ui::InviteLinkFields;
	const auto done = [=](Fields result) {
		const auto finish = [=](Api::InviteLink finished) {
			if (*box) {
				if (creating) {
					(*box)->getDelegate()->show(
						ShowInviteLinkBox(peer, finished));
				}
				(*box)->closeBox();
			}
		};
		if (creating) {
			Assert(data.admin->isSelf());
			peer->session().api().inviteLinks().create(
				peer,
				finish,
				result.label,
				result.expireDate,
				result.usageLimit,
				result.requestApproval);
		} else {
			peer->session().api().inviteLinks().edit(
				peer,
				data.admin,
				result.link,
				result.label,
				result.expireDate,
				result.usageLimit,
				result.requestApproval,
				finish);
		}
	};
	const auto isGroup = !peer->isBroadcast();
	const auto isPublic = peer->isChannel() && peer->asChannel()->isPublic();
	if (creating) {
		auto object = Box(Ui::CreateInviteLinkBox, isGroup, isPublic, done);
		*box = Ui::MakeWeak(object.data());
		return object;
	} else {
		auto object = Box(
			Ui::EditInviteLinkBox,
			Fields{
				.link = data.link,
				.label = data.label,
				.expireDate = data.expireDate,
				.usageLimit = data.usageLimit,
				.requestApproval = data.requestApproval,
				.isGroup = isGroup,
				.isPublic = isPublic,
			},
			done);
		*box = Ui::MakeWeak(object.data());
		return object;
	}
}

object_ptr<Ui::BoxContent> RevokeLinkBox(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		bool permanent) {
	const auto revoke = [=](Fn<void()> &&close) {
		auto &l = peer->session().api().inviteLinks();
		if (permanent) {
			l.revokePermanent(peer, admin, link, std::move(close));
		} else {
			auto done = [c = std::move(close)](const LinkData &) { c(); };
			l.revoke(peer, admin, link, std::move(done));
		}
	};
	return Ui::MakeConfirmBox({
		permanent
			? tr::lng_group_invite_about_new()
			: tr::lng_group_invite_revoke_about(),
		revoke
	});
}

object_ptr<Ui::BoxContent> DeleteLinkBox(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link) {
	const auto sure = [=](Fn<void()> &&close) {
		peer->session().api().inviteLinks().destroy(
			peer,
			admin,
			link,
			std::move(close));
	};
	return Ui::MakeConfirmBox({ tr::lng_group_invite_delete_sure(), sure });
}

object_ptr<Ui::BoxContent> ShowInviteLinkBox(
		not_null<PeerData*> peer,
		const Api::InviteLink &link) {
	const auto admin = link.admin;
	const auto linkText = link.link;
	const auto revoked = link.revoked;

	auto updates = peer->session().api().inviteLinks().updates(
		peer,
		admin
	) | rpl::filter([=](const Api::InviteLinkUpdate &update) {
		return (update.was == linkText);
	}) | rpl::map([=](const Api::InviteLinkUpdate &update) {
		return update.now ? *update.now : LinkData{ .admin = admin };
	});
	auto data = rpl::single(link) | rpl::then(std::move(updates));

	auto initBox = [=, data = rpl::duplicate(data)](
		not_null<Ui::BoxContent*> box) {
		rpl::duplicate(
			data
		) | rpl::start_with_next([=](const LinkData &link) {
			if (ClosingLinkBox(link, revoked)) {
				box->closeBox();
				return;
			}
			const auto now = base::unixtime::now();
			box->setTitle(!link.label.isEmpty()
				? rpl::single(link.label)
				: link.revoked
				? tr::lng_manage_peer_link_invite()
				: IsExpiredLink(link, now)
				? tr::lng_manage_peer_link_expired()
				: link.permanent
				? tr::lng_manage_peer_link_permanent()
				: tr::lng_manage_peer_link_invite());
		}, box->lifetime());

		box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	};
	return Box<PeerListBox>(
		std::make_unique<Controller>(
			peer,
			link.admin,
			std::move(data),
			Controller::Role::Joined),
		std::move(initBox));
}

QString PrepareRequestedRowStatus(TimeId date) {
	const auto now = QDateTime::currentDateTime();
	const auto parsed = base::unixtime::parse(date);
	const auto parsedDate = parsed.date();
	const auto time = QLocale().toString(parsed.time(), QLocale::ShortFormat);
	const auto generic = [&] {
		return tr::lng_group_requests_status_date_time(
			tr::now,
			lt_date,
			langDayOfMonth(parsedDate),
			lt_time,
			time);
	};
	return (parsedDate.addDays(1) < now.date())
		? generic()
		: (parsedDate.addDays(1) == now.date())
		? tr::lng_group_requests_status_yesterday(tr::now, lt_time, time)
		: (now.date() == parsedDate)
		? tr::lng_group_requests_status_today(tr::now, lt_time, time)
		: generic();
}
