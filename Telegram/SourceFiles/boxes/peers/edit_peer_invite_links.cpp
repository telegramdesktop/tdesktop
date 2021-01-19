/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_links.h"

#include "data/data_peer.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "ui/boxes/edit_invite_link.h"
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
	auto result = link.usage
		? tr::lng_group_invite_joined(tr::now, lt_count_decimal, link.usage)
		: tr::lng_group_invite_no_joined(tr::now);
	const auto add = [&](const QString &text) {
		result += QString::fromUtf8(" \xE2\xB8\xB1 ") + text;
	};
	if (link.revoked) {
		add(tr::lng_group_invite_link_revoked(tr::now));
	} else if ((link.usageLimit > 0 && link.usage >= link.usageLimit)
		|| (link.expireDate > 0 && now >= link.expireDate)) {
		add(tr::lng_group_invite_link_expired(tr::now));
	}
	return result;
}

void EditLink(not_null<PeerData*> peer, const InviteLinkData &data) {
	const auto creating = data.link.isEmpty();
	const auto box = std::make_shared<QPointer<Ui::GenericBox>>();
	using Fields = Ui::InviteLinkFields;
	const auto done = [=](Fields result) {
		const auto finish = [=](Api::InviteLink finished) {
			if (creating) {
				ShowInviteLinkBox(peer, finished);
			}
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

void DeleteLink(not_null<PeerData*> peer, const QString &link) {
	const auto box = std::make_shared<QPointer<ConfirmBox>>();
	const auto sure = [=] {
		const auto finish = [=] {
			if (*box) {
				(*box)->closeBox();
			}
		};
		peer->session().api().inviteLinks().destroy(peer, link, finish);
	};
	*box = Ui::show(
		Box<ConfirmBox>(tr::lng_group_invite_delete_sure(tr::now), sure),
		Ui::LayerOption::KeepOther);
}

void DeleteAllRevoked(not_null<PeerData*> peer) {
	const auto box = std::make_shared<QPointer<ConfirmBox>>();
	const auto sure = [=] {
		const auto finish = [=] {
			if (*box) {
				(*box)->closeBox();
			}
		};
		peer->session().api().inviteLinks().destroyAllRevoked(peer, finish);
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
	const bool _revoked = false;
	base::unique_qptr<Ui::PopupMenu> _menu;

	QString _offsetLink;
	TimeId _offsetDate = 0;
	bool _requesting = false;
	bool _allLoaded = false;

	base::flat_set<not_null<Row*>> _expiringRows;
	base::Timer _updateExpiringTimer;

	std::array<QImage, int(Color::Count)> _icons;
	rpl::lifetime _lifetime;

};

Controller::Controller(not_null<PeerData*> peer, bool revoked)
: _peer(peer)
, _revoked(revoked)
, _updateExpiringTimer([=] { expiringProgressTimer(); }) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &image : _icons) {
			image = QImage();
		}
	}, _lifetime);

	peer->session().api().inviteLinks().updates(
		peer
	) | rpl::start_with_next([=](const Api::InviteLinkUpdate &update) {
		const auto now = base::unixtime::now();
		if (!update.now || update.now->revoked != _revoked) {
			if (removeRow(update.was)) {
				delegate()->peerListRefreshRows();
			}
		} else if (update.was.isEmpty()) {
			prependRow(*update.now, now);
			delegate()->peerListRefreshRows();
		} else {
			updateRow(*update.now, now);
		}
	}, _lifetime);

	if (_revoked) {
		peer->session().api().inviteLinks().allRevokedDestroyed(
			peer
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

void Controller::prepare() {
	if (!_revoked) {
		appendSlice(_peer->session().api().inviteLinks().links(_peer));
	}
	if (!delegate()->peerListFullRowsCount()) {
		loadMoreRows();
	}
}

void Controller::loadMoreRows() {
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
		_offsetDate,
		_offsetLink,
		_revoked,
		crl::guard(this, done));
}

void Controller::appendSlice(const InviteLinksSlice &slice) {
	const auto now = base::unixtime::now();
	for (const auto &link : slice.links) {
		if (!link.permanent || link.revoked) {
			appendRow(link, now);
		}
		_offsetLink = link.link;
		_offsetDate = link.date;
	}
	if (slice.links.size() >= slice.count) {
		_allLoaded = true;
	}
	delegate()->peerListRefreshRows();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	ShowInviteLinkBox(_peer, static_cast<Row*>(row.get())->data());
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
		result->addAction(tr::lng_group_invite_context_delete(tr::now), [=] {
			DeleteLink(_peer, link);
		});
	} else {
		result->addAction(tr::lng_group_invite_context_copy(tr::now), [=] {
			CopyInviteLink(link);
		});
		result->addAction(tr::lng_group_invite_context_share(tr::now), [=] {
			ShareInviteLinkBox(_peer, link);
		});
		result->addAction(tr::lng_group_invite_context_edit(tr::now), [=] {
			EditLink(_peer, data);
		});
		result->addAction(tr::lng_group_invite_context_revoke(tr::now), [=] {
			RevokeLink(_peer, link);
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

void Controller::prependRow(const InviteLinkData &data, TimeId now) {
	delegate()->peerListPrependRow(std::make_unique<Row>(this, data, now));
}

void Controller::updateRow(const InviteLinkData &data, TimeId now) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(data))) {
		const auto real = static_cast<Row*>(row);
		real->update(data, now);
		checkExpiringTimer(real);
		delegate()->peerListUpdateRow(row);
	}
}

bool Controller::removeRow(const QString &link) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(link))) {
		delegate()->peerListRemoveRow(row);
		return true;
	}
	return false;
}

void Controller::checkExpiringTimer(not_null<Row*> row) {
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

void Controller::expiringProgressTimer() {
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
	using namespace Settings;

	box->setTitle(tr::lng_group_invite_title());

	const auto container = box->verticalLayout();
	AddSubsectionTitle(container, tr::lng_create_permanent_link_title());
	AddPermanentLinkBlock(container, peer);
	AddDivider(container);

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
			st::settingsDividerLabelPadding)),
		style::margins(0, st::inviteLinkCreateSkip, 0, 0));
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

	const auto deleteAll = Ui::CreateChild<Ui::LinkButton>(
		container.get(),
		tr::lng_group_invite_context_delete_all(tr::now),
		st::boxLinkButton);
	rpl::combine(
		header->topValue(),
		container->widthValue()
	) | rpl::start_with_next([=](int top, int outerWidth) {
		deleteAll->moveToRight(
			st::inviteLinkRevokedTitlePadding.left(),
			top + st::inviteLinkRevokedTitlePadding.top(),
			outerWidth);
	}, deleteAll->lifetime());
	deleteAll->setClickedCallback([=] {
		DeleteAllRevoked(peer);
	});

	rpl::combine(
		list->heightValue(),
		revoked->heightValue()
	) | rpl::start_with_next([=](int list, int revoked) {
		dividerAbout->toggle(!list, anim::type::instant);
		divider->toggle(list > 0 && revoked > 0, anim::type::instant);
		header->toggle(revoked > 0, anim::type::instant);
		deleteAll->setVisible(revoked > 0);
	}, header->lifetime());

	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
}
