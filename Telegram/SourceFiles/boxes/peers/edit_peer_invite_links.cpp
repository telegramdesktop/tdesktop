/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_links.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_peer_invite_link.h"
#include "settings/settings_common.h" // AddDivider.
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "base/unixtime.h"
#include "styles/style_info.h"
#include "styles/style_layers.h" // st::boxDividerLabel
#include "styles/style_settings.h" // st::settingsDividerLabelPadding

#include <xxhash.h>

namespace {

constexpr auto kPreloadPages = 2;
constexpr auto kFullArcLength = 360 * 16;

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

	void update(const InviteLinkData &data, TimeId now);
	void updateExpireProgress(TimeId now);

	[[nodiscard]] InviteLinkData data() const;
	[[nodiscard]] crl::time updateExpireIn() const;

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

[[nodiscard]] QString ComputeStatus(const InviteLinkData &link, TimeId now) {
	const auto expired = IsExpiredLink(link, now);
	const auto revoked = link.revoked;
	auto result = link.usage
		? tr::lng_group_invite_joined(tr::now, lt_count_decimal, link.usage)
		: (!expired && !revoked && link.usageLimit > 0)
		? tr::lng_group_invite_can_join(
			tr::now,
			lt_count_decimal,
			link.usageLimit)
		: tr::lng_group_invite_no_joined(tr::now);
	const auto add = [&](const QString &text) {
		result += QString::fromUtf8(" \xE2\x80\xA2 ") + text;
	};
	if (revoked) {
		return result;
	} else if (expired) {
		add(tr::lng_group_invite_link_expired(tr::now));
		return result;
	}
	if (link.usage > 0 && link.usageLimit > link.usage) {
		result += ", " + tr::lng_group_invite_remaining(
			tr::now,
			lt_count_decimal,
			link.usageLimit - link.usage);
	}
	if (link.expireDate > now) {
		const auto left = (link.expireDate - now);
		if (left >= 86400) {
			add(tr::lng_group_invite_days_left(
				tr::now,
				lt_count,
				left / 86400));
		} else {
			const auto time = base::unixtime::parse(link.expireDate).time();
			add(QLocale::system().toString(time, QLocale::LongFormat));
		}
	}
	return result;
}

void DeleteAllRevoked(
		not_null<PeerData*> peer,
		not_null<UserData*> admin) {
	const auto box = std::make_shared<QPointer<ConfirmBox>>();
	const auto sure = [=] {
		const auto finish = [=] {
			if (*box) {
				(*box)->closeBox();
			}
		};
		peer->session().api().inviteLinks().destroyAllRevoked(
			peer,
			admin,
			finish);
	};
	*box = Ui::show(
		Box<ConfirmBox>(tr::lng_group_invite_delete_all_sure(tr::now), sure),
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
	setCustomStatus(ComputeStatus(data, now));
}

void Row::update(const InviteLinkData &data, TimeId now) {
	_data = data;
	_progressTillExpire = ComputeProgress(data, now);
	_color = ComputeColor(data, _progressTillExpire);
	setCustomStatus(ComputeStatus(data, now));
	_delegate->rowUpdateRow(this);
}

void Row::updateExpireProgress(TimeId now) {
	const auto updated = ComputeProgress(_data, now);
	if (std::round(_progressTillExpire * 360) != std::round(updated * 360)) {
		_progressTillExpire = updated;
		const auto color = ComputeColor(_data, _progressTillExpire);
		if (_color != color) {
			_color = color;
			setCustomStatus(ComputeStatus(_data, now));
		}
		_delegate->rowUpdateRow(this);
	}
}

InviteLinkData Row::data() const {
	return _data;
}

crl::time Row::updateExpireIn() const {
	if (_color != Color::Expiring && _color != Color::ExpireSoon) {
		return 0;
	}
	const auto start = _data.startDate ? _data.startDate : _data.date;
	if (_data.expireDate <= start) {
		return 0;
	}
	return std::round((_data.expireDate - start) * crl::time(1000) / 720.);
}

QString Row::generateName() {
	auto result = _data.link;
	return result.replace(
		qstr("https://"),
		QString()
	).replace(
		qstr("t.me/+"),
		QString()
	).replace(
		qstr("t.me/joinchat/"),
		QString()
	);
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

class LinksController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	LinksController(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		int count,
		bool revoked);

	[[nodiscard]] rpl::producer<int> fullCountValue() const {
		return _count.value();
	}

	void prepare() override;
	void loadMoreRows() override;
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

	[[nodiscard]] rpl::producer<InviteLinkData> permanentFound() const {
		return _permanentFound.events();
	}

private:
	void appendRow(const InviteLinkData &data, TimeId now);
	void prependRow(const InviteLinkData &data, TimeId now);
	void updateRow(const InviteLinkData &data, TimeId now);
	bool removeRow(const QString &link);

	void appendSlice(const InviteLinksSlice &slice);
	void checkExpiringTimer(not_null<Row*> row);
	void expiringProgressTimer();

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);

	const not_null<PeerData*> _peer;
	const not_null<UserData*> _admin;
	const bool _revoked = false;
	rpl::variable<int> _count;
	base::unique_qptr<Ui::PopupMenu> _menu;

	QString _offsetLink;
	TimeId _offsetDate = 0;
	bool _requesting = false;
	bool _allLoaded = false;

	rpl::event_stream<InviteLinkData> _permanentFound;
	base::flat_set<not_null<Row*>> _expiringRows;
	base::Timer _updateExpiringTimer;

	std::array<QImage, int(Color::Count)> _icons;
	rpl::lifetime _lifetime;

};

LinksController::LinksController(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	int count,
	bool revoked)
: _peer(peer)
, _admin(admin)
, _revoked(revoked)
, _count(count)
, _updateExpiringTimer([=] { expiringProgressTimer(); }) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &image : _icons) {
			image = QImage();
		}
	}, _lifetime);

	peer->session().api().inviteLinks().updates(
		peer,
		admin
	) | rpl::start_with_next([=](const Api::InviteLinkUpdate &update) {
		const auto now = base::unixtime::now();
		if (!update.now || update.now->revoked != _revoked) {
			if (removeRow(update.was)) {
				delegate()->peerListRefreshRows();
			}
		} else if (update.was.isEmpty()) {
			if (update.now->permanent && !update.now->revoked) {
				_permanentFound.fire_copy(*update.now);
			} else {
				prependRow(*update.now, now);
				delegate()->peerListRefreshRows();
			}
		} else {
			updateRow(*update.now, now);
		}
	}, _lifetime);

	if (_revoked) {
		peer->session().api().inviteLinks().allRevokedDestroyed(
			peer,
			admin
		) | rpl::start_with_next([=] {
			_requesting = false;
			_allLoaded = true;
			while (delegate()->peerListFullRowsCount()) {
				delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
			}
			delegate()->peerListRefreshRows();
		}, _lifetime);
	}
}

void LinksController::prepare() {
	if (!_revoked && _admin->isSelf()) {
		appendSlice(_peer->session().api().inviteLinks().myLinks(_peer));
	}
	if (!delegate()->peerListFullRowsCount()) {
		loadMoreRows();
	}
}

void LinksController::loadMoreRows() {
	if (_requesting || _allLoaded) {
		return;
	}
	_requesting = true;
	const auto done = [=](const InviteLinksSlice &slice) {
		if (!_requesting) {
			return;
		}
		_requesting = false;
		if (slice.links.empty()) {
			_allLoaded = true;
			return;
		}
		appendSlice(slice);
	};
	_peer->session().api().inviteLinks().requestMoreLinks(
		_peer,
		_admin,
		_offsetDate,
		_offsetLink,
		_revoked,
		crl::guard(this, done));
}

void LinksController::appendSlice(const InviteLinksSlice &slice) {
	const auto now = base::unixtime::now();
	for (const auto &link : slice.links) {
		if (link.permanent && !link.revoked) {
			_permanentFound.fire_copy(link);
		} else {
			appendRow(link, now);
		}
		_offsetLink = link.link;
		_offsetDate = link.date;
	}
	if (slice.links.size() >= slice.count) {
		_allLoaded = true;
	}
	const auto rowsCount = delegate()->peerListFullRowsCount();
	const auto minimalCount = _revoked ? rowsCount : (rowsCount + 1);
	_count = _allLoaded ? minimalCount : std::max(slice.count, minimalCount);
	delegate()->peerListRefreshRows();
}

void LinksController::rowClicked(not_null<PeerListRow*> row) {
	ShowInviteLinkBox(_peer, static_cast<Row*>(row.get())->data());
}

void LinksController::rowActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, nullptr);
}

base::unique_qptr<Ui::PopupMenu> LinksController::rowContextMenu(
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

base::unique_qptr<Ui::PopupMenu> LinksController::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto real = static_cast<Row*>(row.get());
	const auto data = real->data();
	const auto link = data.link;
	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	if (data.revoked) {
		result->addAction(tr::lng_group_invite_context_delete(tr::now), [=] {
			DeleteLink(_peer, _admin, link);
		});
	} else {
		result->addAction(tr::lng_group_invite_context_copy(tr::now), [=] {
			CopyInviteLink(link);
		});
		result->addAction(tr::lng_group_invite_context_share(tr::now), [=] {
			ShareInviteLinkBox(_peer, link);
		});
		result->addAction(tr::lng_group_invite_context_qr(tr::now), [=] {
			InviteLinkQrBox(link);
		});
		result->addAction(tr::lng_group_invite_context_edit(tr::now), [=] {
			EditLink(_peer, data);
		});
		result->addAction(tr::lng_group_invite_context_revoke(tr::now), [=] {
			RevokeLink(_peer, _admin, link);
		});
	}
	return result;
}

Main::Session &LinksController::session() const {
	return _peer->session();
}

void LinksController::appendRow(const InviteLinkData &data, TimeId now) {
	delegate()->peerListAppendRow(std::make_unique<Row>(this, data, now));
}

void LinksController::prependRow(const InviteLinkData &data, TimeId now) {
	delegate()->peerListPrependRow(std::make_unique<Row>(this, data, now));
}

void LinksController::updateRow(const InviteLinkData &data, TimeId now) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(data))) {
		const auto real = static_cast<Row*>(row);
		real->update(data, now);
		checkExpiringTimer(real);
		delegate()->peerListUpdateRow(row);
	} else if (_revoked) {
		prependRow(data, now);
		delegate()->peerListRefreshRows();
	}
}

bool LinksController::removeRow(const QString &link) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(link))) {
		delegate()->peerListRemoveRow(row);
		return true;
	}
	return false;
}

void LinksController::checkExpiringTimer(not_null<Row*> row) {
	const auto updateIn = row->updateExpireIn();
	if (updateIn > 0) {
		_expiringRows.emplace(row);
		if (!_updateExpiringTimer.isActive()
			|| updateIn < _updateExpiringTimer.remainingTime()) {
			_updateExpiringTimer.callOnce(updateIn);
		}
	} else {
		_expiringRows.remove(row);
	}
}

void LinksController::expiringProgressTimer() {
	const auto now = base::unixtime::now();
	auto minimalIn = 0;
	for (auto i = begin(_expiringRows); i != end(_expiringRows);) {
		(*i)->updateExpireProgress(now);
		const auto updateIn = (*i)->updateExpireIn();
		if (!updateIn) {
			i = _expiringRows.erase(i);
		} else {
			++i;
			if (!minimalIn || minimalIn > updateIn) {
				minimalIn = updateIn;
			}
		}
	}
	if (minimalIn) {
		_updateExpiringTimer.callOnce(minimalIn);
	}
}

void LinksController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void LinksController::rowPaintIcon(
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
		Unexpected("Color in LinksController::rowPaintIcon.");
	}();
	const auto stroke = st::inviteLinkIconStroke;
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
		auto rect = QRect(0, 0, inner, inner);
		if (color == Color::Expiring || color == Color::ExpireSoon) {
			rect = rect.marginsRemoved({ stroke, stroke, stroke, stroke });
		}
		p.drawEllipse(rect);
		(color == Color::Revoked
			? st::inviteLinkRevokedIcon
			: st::inviteLinkIcon).paintInCenter(p, { 0, 0, inner, inner });
	}
	p.drawImage(x + skip, y + skip, icon);
	if (progress >= 0. && progress < 1.) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = QPen((*bg)->c);
		pen.setWidth(stroke);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		const auto margins = .5 * stroke;
		p.drawArc(QRectF(x + skip, y + skip, inner, inner).marginsAdded({
			margins,
			margins,
			margins,
			margins,
		}), (kFullArcLength / 4), kFullArcLength * (1. - progress));
	}
}

class AdminsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	AdminsController(not_null<PeerData*> peer, not_null<UserData*> admin);
	~AdminsController();

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	void appendRow(not_null<UserData*> user, int count);

	const not_null<PeerData*> _peer;
	const not_null<UserData*> _admin;
	mtpRequestId _requestId = 0;

};

AdminsController::AdminsController(
	not_null<PeerData*> peer,
	not_null<UserData*> admin)
: _peer(peer)
, _admin(admin) {
}

AdminsController::~AdminsController() {
	session().api().request(base::take(_requestId)).cancel();
}

void AdminsController::prepare() {
	if (const auto chat = _peer->asChat()) {
		if (!chat->amCreator()) {
			return;
		}
	} else if (const auto channel = _peer->asChannel()) {
		if (!channel->amCreator()) {
			return;
		}
	}
	if (!_admin->isSelf()) {
		return;
	}
	_requestId = session().api().request(MTPmessages_GetAdminsWithInvites(
		_peer->input
	)).done([=](const MTPmessages_ChatAdminsWithInvites &result) {
		result.match([&](const MTPDmessages_chatAdminsWithInvites &data) {
			auto &owner = _peer->owner();
			owner.processUsers(data.vusers());
			for (const auto &admin : data.vadmins().v) {
				admin.match([&](const MTPDchatAdminWithInvites &data) {
					const auto adminId = data.vadmin_id().v;
					if (const auto user = owner.userLoaded(adminId)) {
						if (!user->isSelf()) {
							appendRow(user, data.vinvites_count().v);
						}
					}
				});
			}
			delegate()->peerListRefreshRows();
		});
	}).send();
}

void AdminsController::loadMoreRows() {
}

void AdminsController::rowClicked(not_null<PeerListRow*> row) {
	Ui::show(
		Box(ManageInviteLinksBox, _peer, row->peer()->asUser(), 0, 0),
		Ui::LayerOption::KeepOther);
}

Main::Session &AdminsController::session() const {
	return _peer->session();
}

void AdminsController::appendRow(not_null<UserData*> user, int count) {
	auto row = std::make_unique<PeerListRow>(user);
	row->setCustomStatus(
		tr::lng_group_invite_other_count(tr::now, lt_count, count));
	delegate()->peerListAppendRow(std::move(row));
}

} // namespace

struct LinksList {
	not_null<Ui::RpWidget*> widget;
	not_null<LinksController*> controller;
};

LinksList AddLinksList(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		int count,
		bool revoked) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<LinksController>(
		peer,
		admin,
		count,
		revoked);
	controller->setStyleOverrides(&st::inviteLinkList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	return { content, controller };
}

not_null<Ui::RpWidget*> AddAdminsList(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		not_null<UserData*> admin) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<AdminsController>(
		peer,
		admin);
	controller->setStyleOverrides(&st::inviteLinkAdminsList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	return content;
}

void ManageInviteLinksBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		int count,
		int revokedCount) {
	using namespace Settings;

	box->setTitle(tr::lng_group_invite_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	const auto permanentFromList = box->lifetime().make_state<
		rpl::event_stream<InviteLinkData>
	>();
	const auto countValue = box->lifetime().make_state<rpl::variable<int>>(
		count);

	if (!admin->isSelf()) {
		auto status = tr::lng_group_invite_links_count(
			lt_count,
			countValue->value() | tr::to_count());
		AddSinglePeerRow(
			container,
			admin,
			std::move(status));
	}

	AddSubsectionTitle(container, tr::lng_create_permanent_link_title());
	AddPermanentLinkBlock(
		container,
		peer,
		admin,
		permanentFromList->events());
	AddDivider(container);

	auto otherHeader = (Ui::SlideWrap<>*)nullptr;
	if (admin->isSelf()) {
		const auto add = AddCreateLinkButton(container);
		add->setClickedCallback([=] {
			EditLink(peer, InviteLinkData{ .admin = admin });
		});
	} else {
		otherHeader = container->add(object_ptr<Ui::SlideWrap<>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_group_invite_other_list(),
				st::settingsSubsectionTitle),
			st::inviteLinkRevokedTitlePadding));
	}

	auto [list, controller] = AddLinksList(
		container,
		peer,
		admin,
		count,
		false);
	*countValue = controller->fullCountValue();

	controller->permanentFound(
	) | rpl::start_with_next([=](InviteLinkData &&data) {
		permanentFromList->fire(std::move(data));
	}, container->lifetime());

	const auto dividerAbout = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_group_invite_add_about(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding)),
		style::margins(0, st::inviteLinkCreateSkip, 0, 0));

	const auto adminsDivider = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::BoxContentDivider>(container)));
	const auto adminsHeader = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_group_invite_other_title(),
			st::settingsSubsectionTitle),
		st::inviteLinkRevokedTitlePadding));
	const auto admins = AddAdminsList(container, peer, admin);

	const auto revokedDivider = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::BoxContentDivider>(container)));
	const auto revokedHeader = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_group_invite_revoked_title(),
			st::settingsSubsectionTitle),
		st::inviteLinkRevokedTitlePadding));
	const auto revoked = AddLinksList(
		container,
		peer,
		admin,
		revokedCount,
		true).widget;

	const auto deleteAll = Ui::CreateChild<Ui::LinkButton>(
		container.get(),
		tr::lng_group_invite_context_delete_all(tr::now),
		st::defaultLinkButton);
	rpl::combine(
		revokedHeader->topValue(),
		container->widthValue()
	) | rpl::start_with_next([=](int top, int outerWidth) {
		deleteAll->moveToRight(
			st::inviteLinkRevokedTitlePadding.left(),
			top + st::inviteLinkRevokedTitlePadding.top(),
			outerWidth);
	}, deleteAll->lifetime());
	deleteAll->setClickedCallback([=] {
		DeleteAllRevoked(peer, admin);
	});

	rpl::combine(
		list->heightValue(),
		admins->heightValue(),
		revoked->heightValue()
	) | rpl::start_with_next([=](int list, int admins, int revoked) {
		if (otherHeader) {
			otherHeader->toggle(list > 0, anim::type::instant);
		}
		dividerAbout->toggle(!list && !otherHeader, anim::type::instant);
		adminsDivider->toggle(admins > 0 && list > 0, anim::type::instant);
		adminsHeader->toggle(admins > 0, anim::type::instant);
		revokedDivider->toggle(revoked > 0 && (list > 0 || admins > 0), anim::type::instant);
		revokedHeader->toggle(revoked > 0, anim::type::instant);
		deleteAll->setVisible(revoked > 0);
	}, revokedHeader->lifetime());

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
}
