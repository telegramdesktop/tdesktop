/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_filter.h"

#include "ui/widgets/checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "base/unixtime.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

namespace AdminLog {
namespace {

class UserCheckbox : public Ui::RippleButton {
public:
	UserCheckbox(QWidget *parent, not_null<UserData*> user, bool checked);

	bool checked() const {
		return _check->checked();
	}
	rpl::producer<bool> checkedChanges() const;

	enum class NotifyAboutChange {
		Notify,
		DontNotify,
	};
	void setChecked(
		bool checked,
		NotifyAboutChange notify = NotifyAboutChange::Notify);

	QMargins getMargins() const override {
		return _st.margin;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::Checkbox &_st;
	std::unique_ptr<Ui::AbstractCheckView> _check;
	rpl::event_stream<bool> _checkedChanges;

	QRect _checkRect;

	const not_null<UserData*> _user;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;
	QString _statusText;
	bool _statusOnline = false;

};

UserCheckbox::UserCheckbox(QWidget *parent, not_null<UserData*> user, bool checked) : Ui::RippleButton(parent, st::defaultBoxCheckbox.ripple)
, _st(st::adminLogFilterUserCheckbox)
, _check(std::make_unique<Ui::CheckView>(st::defaultCheck, checked, [this] { rtlupdate(_checkRect); }))
, _user(user) {
	setCursor(style::cur_pointer);
	setClickedCallback([this] {
		if (isDisabled()) return;
		setChecked(!this->checked());
	});
	auto now = base::unixtime::now();
	_statusText = Data::OnlineText(_user, now);
	_statusOnline = Data::OnlineTextActive(_user, now);
	auto checkSize = _check->getSize();
	_checkRect = { QPoint(_st.margin.left(), (st::contactsPhotoSize - checkSize.height()) / 2), checkSize };
}

rpl::producer<bool> UserCheckbox::checkedChanges() const {
	return _checkedChanges.events();
}

void UserCheckbox::setChecked(bool checked, NotifyAboutChange notify) {
	if (_check->checked() != checked) {
		_check->setChecked(checked, anim::type::normal);
		if (notify == NotifyAboutChange::Notify) {
			_checkedChanges.fire_copy(checked);
		}
	}
}

void UserCheckbox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto active = _check->currentAnimationValue();
	auto color = anim::color(_st.rippleBg, _st.rippleBgActive, active);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y() + (_checkRect.y() - st::defaultBoxCheckbox.margin.top()), &color);

	auto realCheckRect = myrtlrect(_checkRect);
	if (realCheckRect.intersects(e->rect())) {
		_check->paint(p, _checkRect.left(), _checkRect.top(), width());
	}
	if (realCheckRect.contains(e->rect())) return;

	auto userpicLeft = _checkRect.x() + _checkRect.width() + st::adminLogFilterUserpicLeft;
	auto userpicTop = 0;
	_user->paintUserpicLeft(p, _userpic, userpicLeft, userpicTop, width(), st::contactsPhotoSize);

	if (_name.isEmpty()) {
		_name.setText(st::msgNameStyle, _user->name(), Ui::NameTextOptions());
	}
	auto nameLeft = userpicLeft + st::contactsPhotoSize + st::contactsPadding.left();
	auto nameTop = userpicTop + st::contactsNameTop;
	auto nameWidth = width() - nameLeft - st::contactsPadding.right();
	p.setPen(st::contactsNameFg);
	_name.drawLeftElided(p, nameLeft, nameTop, nameWidth, width());

	auto statusLeft = nameLeft;
	auto statusTop = userpicTop + st::contactsStatusTop;
	p.setFont(st::contactsStatusFont);
	p.setPen(_statusOnline ? st::contactsStatusFgOnline : st::contactsStatusFg);
	p.drawTextLeft(statusLeft, statusTop, width(), _statusText);
}

int UserCheckbox::resizeGetHeight(int newWidth) {
	return st::contactsPhotoSize;
}

QImage UserCheckbox::prepareRippleMask() const {
	return _check->prepareRippleMask();
}

QPoint UserCheckbox::prepareRippleStartPosition() const {
	auto position = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition - QPoint(0, _checkRect.y() - st::defaultBoxCheckbox.margin.top());
	return _check->checkRippleStartPosition(position) ? position : DisabledRippleStartPosition();
}

} // namespace

class FilterBox::Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<ChannelData*> channel,
		const std::vector<not_null<UserData*>> &admins,
		const FilterValue &filter,
		Fn<void()> changedCallback);

	template <typename Widget>
	QPointer<Widget> addRow(object_ptr<Widget> widget, int marginTop) {
		widget->setParent(this);
		widget->show();
		auto row = Row();
		row.widget = std::move(widget);
		row.marginTop = marginTop;
		_rows.push_back(std::move(row));
		return static_cast<Widget*>(_rows.back().widget.data());
	}

	bool canSave() const;
	FilterValue filter() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void createControls(
		const std::vector<not_null<UserData*>> &admins,
		const FilterValue &filter);
	void createAllActionsCheckbox(const FilterValue &filter);
	void createActionsCheckboxes(const FilterValue &filter);
	void createAllUsersCheckbox(const FilterValue &filter);
	void createAdminsCheckboxes(
		const std::vector<not_null<UserData*>> &admins,
		const FilterValue &filter);

	not_null<ChannelData*> _channel;

	QPointer<Ui::Checkbox> _allFlags;
	base::flat_map<
		FilterValue::Flags,
		QPointer<Ui::Checkbox>> _filterFlags;

	QPointer<Ui::Checkbox> _allUsers;
	base::flat_map<not_null<UserData*>, QPointer<UserCheckbox>> _admins;
	bool _restoringInvariant = false;

	struct Row {
		object_ptr<TWidget> widget = { nullptr };
		int marginTop = 0;
	};
	std::vector<Row> _rows;

	Fn<void()> _changedCallback;

};

FilterBox::Inner::Inner(
	QWidget *parent,
	not_null<ChannelData*> channel,
	const std::vector<not_null<UserData*>> &admins,
	const FilterValue &filter,
	Fn<void()> changedCallback)
: RpWidget(parent)
, _channel(channel)
, _changedCallback(std::move(changedCallback)) {
	createControls(admins, filter);
}

void FilterBox::Inner::createControls(const std::vector<not_null<UserData*>> &admins, const FilterValue &filter) {
	createAllActionsCheckbox(filter);
	createActionsCheckboxes(filter);
	createAllUsersCheckbox(filter);
	createAdminsCheckboxes(admins, filter);
}

void FilterBox::Inner::createAllActionsCheckbox(const FilterValue &filter) {
	auto checked = (filter.flags == 0);
	_allFlags = addRow(object_ptr<Ui::Checkbox>(this, tr::lng_admin_log_filter_all_actions(tr::now), checked, st::adminLogFilterCheckbox), st::adminLogFilterCheckbox.margin.top());
	_allFlags->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (!std::exchange(_restoringInvariant, true)) {
			auto allChecked = _allFlags->checked();
			for (const auto &[flag, checkbox] : _filterFlags) {
				checkbox->setChecked(allChecked);
			}
			_restoringInvariant = false;
			if (_changedCallback) {
				_changedCallback();
			}
		}
	}, _allFlags->lifetime());
}

void FilterBox::Inner::createActionsCheckboxes(const FilterValue &filter) {
	using Flag = FilterValue::Flag;
	using Flags = FilterValue::Flags;
	auto addFlag = [this, &filter](Flags flag, QString &&text) {
		auto checked = (filter.flags == 0) || (filter.flags & flag);
		auto checkbox = addRow(object_ptr<Ui::Checkbox>(this, std::move(text), checked, st::defaultBoxCheckbox), st::adminLogFilterLittleSkip);
		_filterFlags[flag] = checkbox;
		checkbox->checkedChanges(
		) | rpl::start_with_next([=](bool checked) {
			if (!std::exchange(_restoringInvariant, true)) {
				auto allChecked = true;
				for (const auto &[flag, checkbox] : _filterFlags) {
					if (!checkbox->checked()) {
						allChecked = false;
						break;
					}
				}
				_allFlags->setChecked(allChecked);
				_restoringInvariant = false;
				if (_changedCallback) {
					_changedCallback();
				}
			}
		}, checkbox->lifetime());
	};
	auto isGroup = _channel->isMegagroup();
	if (isGroup) {
		addFlag(
			Flag::Ban
			| Flag::Unban
			| Flag::Kick
			| Flag::Unkick,
			tr::lng_admin_log_filter_restrictions(tr::now));
	}
	addFlag(
		Flag::Promote | Flag::Demote,
		tr::lng_admin_log_filter_admins_new(tr::now));
	addFlag(
		Flag::Join | Flag::Invite,
		tr::lng_admin_log_filter_members_new(tr::now));
	addFlag(
		Flag::Info | Flag::Settings,
		_channel->isMegagroup()
			? tr::lng_admin_log_filter_info_group(tr::now)
			: tr::lng_admin_log_filter_info_channel(tr::now));
	addFlag(Flag::Delete, tr::lng_admin_log_filter_messages_deleted(tr::now));
	addFlag(Flag::Edit, tr::lng_admin_log_filter_messages_edited(tr::now));
	if (isGroup) {
		addFlag(
			Flag::Pinned,
			tr::lng_admin_log_filter_messages_pinned(tr::now));
		addFlag(
			Flag::GroupCall,
			tr::lng_admin_log_filter_voice_chats(tr::now));
	} else {
		addFlag(
			Flag::GroupCall,
			tr::lng_admin_log_filter_voice_chats_channel(tr::now));
	}
	addFlag(Flag::Invites, tr::lng_admin_log_filter_invite_links(tr::now));
	addFlag(Flag::Leave, tr::lng_admin_log_filter_members_removed(tr::now));
	if (isGroup) {
		addFlag(Flag::Topics, tr::lng_admin_log_filter_topics(tr::now));
	}
}

void FilterBox::Inner::createAllUsersCheckbox(const FilterValue &filter) {
	_allUsers = addRow(object_ptr<Ui::Checkbox>(this, tr::lng_admin_log_filter_all_admins(tr::now), filter.allUsers, st::adminLogFilterCheckbox), st::adminLogFilterSkip);
	_allUsers->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (!std::exchange(_restoringInvariant, true)) {
			auto allChecked = _allUsers->checked();
			for (const auto &[user, checkbox] : _admins) {
				checkbox->setChecked(allChecked);
			}
			_restoringInvariant = false;
			if (_changedCallback) {
				_changedCallback();
			}
		}
	}, _allUsers->lifetime());
}

void FilterBox::Inner::createAdminsCheckboxes(const std::vector<not_null<UserData*>> &admins, const FilterValue &filter) {
	for (const auto &user : admins) {
		const auto checked = filter.allUsers || base::contains(filter.admins, user);
		const auto checkbox = addRow(
			object_ptr<UserCheckbox>(this, user, checked),
			st::adminLogFilterLittleSkip);
		checkbox->checkedChanges(
		) | rpl::start_with_next([=](bool checked) {
			if (!std::exchange(_restoringInvariant, true)) {
				auto allChecked = true;
				for (const auto &[user, checkbox] : _admins) {
					if (!checkbox->checked()) {
						allChecked = false;
						break;
					}
				}
				if (!allChecked) {
					_allUsers->setChecked(allChecked);
				}
				_restoringInvariant = false;
				if (_changedCallback) {
					_changedCallback();
				}
			}
		}, checkbox->lifetime());
		_admins[user] = checkbox;
	}
}

bool FilterBox::Inner::canSave() const {
	for (const auto &[flag, checkbox] : _filterFlags) {
		if (checkbox->checked()) {
			return true;
		}
	}
	return false;
}

FilterValue FilterBox::Inner::filter() const {
	auto result = FilterValue();
	auto allChecked = true;
	for (const auto &[flag, checkbox] : _filterFlags) {
		if (checkbox->checked()) {
			result.flags |= flag;
		} else {
			allChecked = false;
		}
	}
	if (allChecked) {
		result.flags = 0;
	}
	result.allUsers = _allUsers->checked();
	if (!result.allUsers) {
		result.admins.reserve(_admins.size());
		for (const auto &[user, checkbox] : _admins) {
			if (checkbox->checked()) {
				result.admins.push_back(user);
			}
		}
	}
	return result;
}

int FilterBox::Inner::resizeGetHeight(int newWidth) {
	auto newHeight = 0;
	auto rowWidth = newWidth - st::boxPadding.left() - st::boxPadding.right();
	for (auto &&row : _rows) {
		newHeight += row.marginTop;
		row.widget->resizeToNaturalWidth(rowWidth);
		newHeight += row.widget->heightNoMargins();
	}
	return newHeight;
}

void FilterBox::Inner::resizeEvent(QResizeEvent *e) {
	auto top = 0;
	for (auto &&row : _rows) {
		top += row.marginTop;
		row.widget->moveToLeft(st::boxPadding.left(), top);
		top += row.widget->heightNoMargins();
	}
}

FilterBox::FilterBox(QWidget*, not_null<ChannelData*> channel, const std::vector<not_null<UserData*>> &admins, const FilterValue &filter, Fn<void(FilterValue &&filter)> saveCallback) : BoxContent()
, _channel(channel)
, _admins(admins)
, _initialFilter(filter)
, _saveCallback(std::move(saveCallback)) {
}

void FilterBox::prepare() {
	setTitle(tr::lng_admin_log_filter_title());

	_inner = setInnerWidget(object_ptr<Inner>(this, _channel, _admins, _initialFilter, [this] { refreshButtons(); }));
	_inner->resizeToWidth(st::boxWideWidth);

	refreshButtons();
	setDimensions(st::boxWideWidth, qMin(_inner->height(), st::boxMaxListHeight));
}

void FilterBox::refreshButtons() {
	clearButtons();
	if (_inner->canSave()) {
		addButton(tr::lng_settings_save(), [this] {
			if (_saveCallback) {
				_saveCallback(_inner->filter());
			}
		});
	}
	addButton(tr::lng_cancel(), [this] { closeBox(); });
}

void FilterBox::resizeToContent() {
	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(_inner->width(), _inner->height());
}

} // namespace AdminLog
