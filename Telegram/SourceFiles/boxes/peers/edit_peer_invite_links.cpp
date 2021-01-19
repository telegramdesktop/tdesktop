/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_links.h"

#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "ui/boxes/edit_invite_link.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/abstract_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/controls/invite_link_label.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "history/view/history_view_group_call_tracker.h" // GenerateUs...
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/history.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "settings/settings_common.h" // AddDivider.
#include "apiwrap.h"
#include "mainwindow.h"
#include "boxes/share_box.h"
#include "base/weak_ptr.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "api/api_common.h"
#include "styles/style_info.h"
#include "styles/style_layers.h" // st::boxDividerLabel
#include "styles/style_settings.h" // st::settingsDividerLabelPadding

#include <xxhash.h>

#include <QtGui/QGuiApplication>

namespace {

constexpr auto kPreloadPages = 2;

enum class Color {
	Permanent,
	Expiring,
	ExpireSoon,
	Expired,
	Revoked,

	Count,
};

using InviteLinkData = Api::InviteLink;
using InviteLinksSlice = Api::PeerInviteLinks;

struct InviteLinkAction {
	enum class Type {
		Copy,
		Share,
		Edit,
		Revoke,
		Delete,
	};
	QString link;
	Type type = Type::Copy;
};

class Row;

class RowDelegate {
public:
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
	virtual void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		float64 progress,
		Color color) = 0;
};

class Row final : public PeerListRow {
public:
	Row(
		not_null<RowDelegate*> delegate,
		const InviteLinkData &data,
		TimeId now);

	void update(const InviteLinkData &data);

	[[nodiscard]] InviteLinkData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback() override;

	QSize actionSize() const override;
	QMargins actionMargins() const override;
	void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	const not_null<RowDelegate*> _delegate;
	InviteLinkData _data;
	QString _status;
	float64 _progressTillExpire = 0.;
	Color _color = Color::Permanent;

};

[[nodiscard]] uint64 ComputeRowId(const QString &link) {
	return XXH64(link.data(), link.size() * sizeof(ushort), 0);
}

[[nodiscard]] uint64 ComputeRowId(const InviteLinkData &data) {
	return ComputeRowId(data.link);
}

[[nodiscard]] float64 ComputeProgress(
		const InviteLinkData &link,
		TimeId now) {
	const auto startDate = link.startDate ? link.startDate : link.date;
	if (link.expireDate <= startDate && link.usageLimit <= 0) {
		return -1;
	}
	const auto expireProgress = (link.expireDate <= startDate
		|| now <= startDate)
		? 0.
		: (link.expireDate <= now)
		? 1.
		: (now - startDate) / float64(link.expireDate - startDate);
	const auto usageProgress = (link.usageLimit <= 0 || link.usage <= 0)
		? 0.
		: (link.usageLimit <= link.usage)
		? 1.
		: link.usage / float64(link.usageLimit);
	return std::max(expireProgress, usageProgress);
}

[[nodiscard]] Color ComputeColor(
		const InviteLinkData &link,
		float64 progress) {
	const auto startDate = link.startDate ? link.startDate : link.date;
	return link.revoked
		? Color::Revoked
		: (progress >= 1.)
		? Color::Expired
		: (progress >= 3 / 4.)
		? Color::ExpireSoon
		: (progress >= 0.)
		? Color::Expiring
		: Color::Permanent;
}

[[nodiscard]] QString ComputeStatus(const InviteLinkData &link) {
	return "nothing";
}

void CopyLink(const QString &link) {
	QGuiApplication::clipboard()->setText(link);
	Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
}

void ShareLinkBox(not_null<PeerData*> peer, const QString &link) {
	const auto session = &peer->session();
	const auto sending = std::make_shared<bool>();
	const auto box = std::make_shared<QPointer<ShareBox>>();

	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(link);
		Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
	};
	auto submitCallback = [=](
			std::vector<not_null<PeerData*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					{},
					comment);
				if (!error.isEmpty()) {
					return std::make_pair(error, peer);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->name)
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		*sending = true;
		if (!comment.text.isEmpty()) {
			comment.text = link + "\n" + comment.text;
			const auto add = link.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		}
		const auto owner = &peer->owner();
		auto &api = peer->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto peer : result) {
			const auto history = owner->history(peer);
			auto message = ApiWrap::MessageToSend(history);
			message.textWithTags = comment;
			message.action.options = options;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		Ui::Toast::Show(tr::lng_share_done(tr::now));
		if (*box) {
			(*box)->closeBox();
		}
	};
	auto filterCallback = [](PeerData *peer) {
		return peer->canWrite();
	};
	*box = Ui::show(Box<ShareBox>(
		App::wnd()->sessionController(),
		std::move(copyCallback),
		std::move(submitCallback),
		std::move(filterCallback)));
}

void EditLink(not_null<PeerData*> peer, const InviteLinkData &data) {
	const auto creating = data.link.isEmpty();
	const auto box = std::make_shared<QPointer<Ui::GenericBox>>();
	using Fields = Ui::InviteLinkFields;
	const auto done = [=](Fields result) {
		const auto finish = [=](Api::InviteLink finished) {
			if (*box) {
				(*box)->closeBox();
			}
		};
		if (creating) {
			peer->session().api().inviteLinks().create(
				peer,
				finish,
				result.expireDate,
				result.usageLimit);
		} else {
			peer->session().api().inviteLinks().edit(
				peer,
				result.link,
				result.expireDate,
				result.usageLimit,
				finish);
		}
	};
	*box = Ui::show(
		(creating
			? Box(Ui::CreateInviteLinkBox, done)
			: Box(
				Ui::EditInviteLinkBox,
				Fields{
					.link = data.link,
					.expireDate = data.expireDate,
					.usageLimit = data.usageLimit
				},
				done)),
		Ui::LayerOption::KeepOther);
}

not_null<Ui::SettingsButton*> AddCreateLinkButton(
		not_null<Ui::VerticalLayout*> container) {
	const auto result = container->add(
		object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_group_invite_add(),
			st::inviteLinkCreate),
		style::margins(0, st::inviteLinkCreateSkip, 0, 0));
	const auto icon = Ui::CreateChild<Ui::RpWidget>(result);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto size = st::inviteLinkCreateIconSize;
	icon->resize(size, size);
	result->heightValue(
	) | rpl::start_with_next([=](int height) {
		const auto &st = st::inviteLinkList.item;
		icon->move(
			st.photoPosition.x() + (st.photoSize - size) / 2,
			(height - size) / 2);
	}, icon->lifetime());
	icon->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto rect = icon->rect();
		auto hq = PainterHighQualityEnabler(p);
		p.drawEllipse(rect);
		st::inviteLinkCreateIcon.paintInCenter(p, rect);
	}, icon->lifetime());
	return result;
}

Row::Row(
	not_null<RowDelegate*> delegate,
	const InviteLinkData &data,
	TimeId now)
: PeerListRow(ComputeRowId(data))
, _delegate(delegate)
, _data(data)
, _progressTillExpire(ComputeProgress(data, now))
, _color(ComputeColor(data, _progressTillExpire)) {
	setCustomStatus(ComputeStatus(data));
}

void Row::update(const InviteLinkData &data) {
	_data = data;
	_progressTillExpire = ComputeProgress(data, base::unixtime::now());
	_color = ComputeColor(data, _progressTillExpire);
	setCustomStatus(ComputeStatus(data));
	_delegate->rowUpdateRow(this);
}

InviteLinkData Row::data() const {
	return _data;
}

QString Row::generateName() {
	auto result = _data.link;
	return result.replace(qstr("https://"), QString());
}

QString Row::generateShortName() {
	return generateName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback() {
	return [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		_delegate->rowPaintIcon(p, x, y, size, _progressTillExpire, _color);
	};
}

QSize Row::actionSize() const {
	return QSize(
		st::inviteLinkThreeDotsIcon.width(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins Row::actionMargins() const {
	return QMargins(
		0,
		(st::inviteLinkList.item.height - actionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void Row::paintAction(
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
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	Controller(not_null<PeerData*> peer, bool revoked);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void rowUpdateRow(not_null<Row*> row) override;
	void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		float64 progress,
		Color color) override;

private:
	void appendRow(const InviteLinkData &data, TimeId now);
	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);

	not_null<PeerData*> _peer;
	bool _revoked = false;
	base::unique_qptr<Ui::PopupMenu> _menu;

	std::array<QImage, int(Color::Count)> _icons;
	rpl::lifetime _lifetime;

};

Controller::Controller(not_null<PeerData*> peer, bool revoked)
: _peer(peer)
, _revoked(revoked) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &image : _icons) {
			image = QImage();
		}
	}, _lifetime);
}

void Controller::prepare() {
	const auto now = base::unixtime::now();
	const auto &links = _peer->session().api().inviteLinks().links(_peer);
	for (const auto &link : links.links) {
		if (!link.permanent || link.revoked) {
			appendRow(link, now);
		}
	}
	delegate()->peerListRefreshRows();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	// #TODO links show
}

void Controller::rowActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, nullptr);
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
	const auto real = static_cast<Row*>(row.get());
	const auto data = real->data();
	const auto link = data.link;
	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	if (data.revoked) {
		//result->addAction(tr::lng_group_invite_context_delete(tr::now), [=] {
		//	// #TODO links delete
		//});
	} else {
		result->addAction(tr::lng_group_invite_context_copy(tr::now), [=] {
			CopyLink(link);
		});
		result->addAction(tr::lng_group_invite_context_share(tr::now), [=] {
			ShareLinkBox(_peer, link);
		});
		result->addAction(tr::lng_group_invite_context_edit(tr::now), [=] {
			EditLink(_peer, data);
		});
		result->addAction(tr::lng_group_invite_context_revoke(tr::now), [=] {
			const auto box = std::make_shared<QPointer<ConfirmBox>>();
			const auto revoke = crl::guard(this, [=] {
				const auto done = crl::guard(this, [=](InviteLinkData data) {
					// #TODO links add to revoked, remove from list
					if (*box) {
						(*box)->closeBox();
					}
				});
				_peer->session().api().inviteLinks().revoke(
					_peer,
					link,
					done);
			});
			*box = Ui::show(
				Box<ConfirmBox>(
					tr::lng_group_invite_revoke_about(tr::now),
					revoke),
				Ui::LayerOption::KeepOther);
		});
	}
	return result;
}

Main::Session &Controller::session() const {
	return _peer->session();
}

void Controller::appendRow(const InviteLinkData &data, TimeId now) {
	delegate()->peerListAppendRow(std::make_unique<Row>(this, data, now));
}

void Controller::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void Controller::rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		float64 progress,
		Color color) {
	const auto skip = st::inviteLinkIconSkip;
	const auto inner = size - 2 * skip;
	const auto bg = [&] {
		switch (color) {
		case Color::Permanent: return &st::msgFile1Bg;
		case Color::Expiring: return &st::msgFile2Bg;
		case Color::ExpireSoon: return &st::msgFile4Bg;
		case Color::Expired: return &st::msgFile3Bg;
		case Color::Revoked: return &st::windowSubTextFg;
		}
		Unexpected("Color in Controller::rowPaintIcon.");
	}();
	auto &icon = _icons[int(color)];
	if (icon.isNull()) {
		icon = QImage(
			QSize(inner, inner) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		icon.fill(Qt::transparent);
		icon.setDevicePixelRatio(style::DevicePixelRatio());

		auto p = QPainter(&icon);
		p.setPen(Qt::NoPen);
		p.setBrush(*bg);
		auto hq = PainterHighQualityEnabler(p);
		p.drawEllipse(0, 0, inner, inner);
		st::inviteLinkIcon.paintInCenter(p, { 0, 0, inner, inner });
	}
	p.drawImage(x + skip, y + skip, icon);
	if (progress >= 0. && progress < 1.) {
		const auto kFullArcLength = 360 * 16;
		const auto stroke = st::inviteLinkIconStroke;
		auto hq = PainterHighQualityEnabler(p);
		auto pen = QPen((*bg)->c);
		pen.setWidth(stroke);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		const auto margins = 1.5 * stroke;
		p.drawArc(QRectF(x + skip, y + skip, inner, inner).marginsAdded({
			margins,
			margins,
			margins,
			margins,
		}), (kFullArcLength / 4), kFullArcLength * (1. - progress));
	}
}

} // namespace

void AddPermanentLinkBlock(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer) {
	const auto computePermanentLink = [=] {
		const auto &links = peer->session().api().inviteLinks().links(
			peer).links;
		const auto link = links.empty() ? nullptr : &links.front();
		return (link && link->permanent && !link->revoked) ? link : nullptr;
	};
	auto value = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::InviteLinks
	) | rpl::map([=] {
		const auto link = computePermanentLink();
		return link
			? std::make_tuple(link->link, link->usage)
			: std::make_tuple(QString(), 0);
	}) | rpl::distinct_until_changed(
	) | rpl::start_spawning(container->lifetime());

	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		if (const auto link = computePermanentLink()) {
			CopyLink(link->link);
		}
	});
	const auto shareLink = crl::guard(weak, [=] {
		if (const auto link = computePermanentLink()) {
			ShareLinkBox(peer, link->link);
		}
	});
	const auto revokeLink = crl::guard(weak, [=] {
		const auto box = std::make_shared<QPointer<ConfirmBox>>();
		const auto done = crl::guard(weak, [=] {
			const auto close = [=](auto&&) {
				if (*box) {
					(*box)->closeBox();
				}
			};
			peer->session().api().inviteLinks().revokePermanent(peer, close);
		});
		*box = Ui::show(
			Box<ConfirmBox>(tr::lng_group_invite_about_new(tr::now), done),
			Ui::LayerOption::KeepOther);
	});

	auto link = rpl::duplicate(
		value
	) | rpl::map([=](QString link, int usage) {
		const auto prefix = qstr("https://");
		return link.startsWith(prefix) ? link.mid(prefix.size()) : link;
	});
	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(container);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink);
		result->addAction(
			tr::lng_group_invite_context_revoke(tr::now),
			revokeLink);
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

	AddCopyShareLinkButtons(
		container,
		copyLink,
		shareLink);

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
				return !element.peer->hasUserpic() || element.view->image();
			});
		state->content = Ui::JoinedCountContent{
			.count = state->count,
			.userpics = state->cachedUserpics
		};
	};
	std::move(
		value
	) | rpl::map([=](QString link, int usage) {
		return peer->session().api().inviteLinks().joinedFirstSliceValue(
			peer,
			link,
			usage);
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
			} else if (!element.view->image()) {
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

not_null<Ui::RpWidget*> AddLinksList(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		bool revoked) {
	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<Controller>(
		peer,
		revoked);
	controller->setStyleOverrides(&st::inviteLinkList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	return content;
}

void ManageInviteLinksBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	box->setTitle(tr::lng_group_invite_title());

	const auto container = box->verticalLayout();
	AddPermanentLinkBlock(container, peer);
	Settings::AddDivider(container);

	const auto add = AddCreateLinkButton(container);
	add->setClickedCallback([=] {
		EditLink(peer, InviteLinkData{ .admin = peer->session().user() });
	});

	const auto list = AddLinksList(container, peer, false);
	const auto dividerAbout = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_group_invite_add_about(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding)));
	const auto divider = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::BoxContentDivider>(container)));
	const auto header = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_group_invite_revoked_title(),
			st::settingsSubsectionTitle),
		st::inviteLinkRevokedTitlePadding));
	const auto revoked = AddLinksList(container, peer, true);

	rpl::combine(
		list->heightValue(),
		revoked->heightValue()
	) | rpl::start_with_next([=](int list, int revoked) {
		dividerAbout->toggle(!list, anim::type::instant);
		divider->toggle(list > 0 && revoked > 0, anim::type::instant);
		header->toggle(revoked > 0, anim::type::instant);
	}, header->lifetime());
}
