/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_menu.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_settings.h"
#include "calls/group/calls_group_panel.h"
#include "calls/group/ui/calls_group_recording_box.h"
#include "data/data_peer.h"
#include "data/data_group_call.h"
#include "info/profile/info_profile_values.h" // Info::Profile::NameValue.
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Calls::Group {
namespace {

class JoinAsAction final : public Ui::Menu::ItemBase {
public:
	JoinAsAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		not_null<PeerData*> peer,
		Fn<void()> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const not_null<PeerData*> _peer;
	Ui::PeerUserpicView _userpicView;

	Ui::Text::String _text;
	Ui::Text::String _name;
	int _textWidth = 0;
	int _nameWidth = 0;
	const int _height = 0;

};

class RecordingAction final : public Ui::Menu::ItemBase {
public:
	RecordingAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		rpl::producer<QString> text,
		rpl::producer<TimeId> startAtValues,
		Fn<void()> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare(rpl::producer<QString> text);
	void refreshElapsedText();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	TimeId _startAt = 0;
	crl::time _startedAt = 0;
	base::Timer _refreshTimer;

	Ui::Text::String _text;
	int _textWidth = 0;
	QString _elapsedText;
	const int _smallHeight = 0;
	const int _bigHeight = 0;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

JoinAsAction::JoinAsAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	not_null<PeerData*> peer,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _peer(peer)
, _height(st::groupCallJoinAsPadding.top()
	+ st::groupCallJoinAsPhotoSize
	+ st::groupCallJoinAsPadding.bottom()) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void JoinAsAction::paint(Painter &p) {
	const auto selected = isSelected();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	const auto &padding = st::groupCallJoinAsPadding;
	_peer->paintUserpic(
		p,
		_userpicView,
		padding.left(),
		padding.top(),
		st::groupCallJoinAsPhotoSize);
	const auto textLeft = padding.left()
		+ st::groupCallJoinAsPhotoSize
		+ padding.left();
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		textLeft,
		st::groupCallJoinAsTextTop,
		_textWidth,
		width());
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	_name.drawLeftElided(
		p,
		textLeft,
		st::groupCallJoinAsNameTop,
		_nameWidth,
		width());
}

void JoinAsAction::prepare() {
	rpl::combine(
		tr::lng_group_call_display_as_header(),
		Info::Profile::NameValue(_peer)
	) | rpl::start_with_next([=](QString text, QString name) {
		const auto &padding = st::groupCallJoinAsPadding;
		_text.setMarkedText(_st.itemStyle, { text }, MenuTextOptions);
		_name.setMarkedText(_st.itemStyle, { name }, MenuTextOptions);
		const auto textWidth = _text.maxWidth();
		const auto nameWidth = _name.maxWidth();
		const auto textLeft = padding.left()
			+ st::groupCallJoinAsPhotoSize
			+ padding.left();
		const auto w = std::clamp(
			(textLeft
				+ std::max(textWidth, nameWidth)
				+ padding.right()),
			_st.widthMin,
			_st.widthMax);
		setMinWidth(w);
		_textWidth = w - textLeft - padding.right();
		_nameWidth = w - textLeft - padding.right();
		update();
	}, lifetime());
}

bool JoinAsAction::isEnabled() const {
	return true;
}

not_null<QAction*> JoinAsAction::action() const {
	return _dummyAction;
}

QPoint JoinAsAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage JoinAsAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int JoinAsAction::contentHeight() const {
	return _height;
}

void JoinAsAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

RecordingAction::RecordingAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<QString> text,
	rpl::producer<TimeId> startAtValues,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _refreshTimer([=] { refreshElapsedText(); })
, _smallHeight(st.itemPadding.top()
	+ _st.itemStyle.font->height
	+ st.itemPadding.bottom())
, _bigHeight(st::groupCallRecordingTimerPadding.top()
	+ _st.itemStyle.font->height
	+ st::groupCallRecordingTimerFont->height
	+ st::groupCallRecordingTimerPadding.bottom()) {
	std::move(
		startAtValues
	) | rpl::start_with_next([=](TimeId startAt) {
		_startAt = startAt;
		_startedAt = crl::now();
		_refreshTimer.cancel();
		refreshElapsedText();
		resize(width(), contentHeight());
	}, lifetime());

	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare(std::move(text));
}

void RecordingAction::paint(Painter &p) {
	const auto selected = isSelected();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}
	const auto smallTop = st::groupCallRecordingTimerPadding.top();
	const auto textTop = _startAt ? smallTop : _st.itemPadding.top();
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		_st.itemPadding.left(),
		textTop,
		_textWidth,
		width());
	if (_startAt) {
		p.setFont(st::groupCallRecordingTimerFont);
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		p.drawTextLeft(
			_st.itemPadding.left(),
			smallTop + _st.itemStyle.font->height,
			width(),
			_elapsedText);
	}
}

void RecordingAction::refreshElapsedText() {
	const auto now = base::unixtime::now();
	const auto elapsed = std::max(now - _startAt, 0);
	const auto text = !_startAt
		? QString()
		: (elapsed >= 3600)
		? QString("%1:%2:%3"
		).arg(elapsed / 3600
		).arg((elapsed % 3600) / 60, 2, 10, QChar('0')
		).arg(elapsed % 60, 2, 10, QChar('0'))
		: QString("%1:%2"
		).arg(elapsed / 60
		).arg(elapsed % 60, 2, 10, QChar('0'));
	if (_elapsedText != text) {
		_elapsedText = text;
		update();
	}

	const auto nextCall = crl::time(500) - ((crl::now() - _startedAt) % 500);
	_refreshTimer.callOnce(nextCall);
}

void RecordingAction::prepare(rpl::producer<QString> text) {
	refreshElapsedText();

	const auto &padding = _st.itemPadding;
	const auto textWidth1 = _st.itemStyle.font->width(
		tr::lng_group_call_recording_start(tr::now));
	const auto textWidth2 = _st.itemStyle.font->width(
		tr::lng_group_call_recording_stop(tr::now));
	const auto maxWidth = st::groupCallRecordingTimerFont->width("23:59:59");
	const auto w = std::clamp(
		(padding.left()
			+ std::max({ textWidth1, textWidth2, maxWidth })
			+ padding.right()),
		_st.widthMin,
		_st.widthMax);
	setMinWidth(w);

	std::move(text) | rpl::start_with_next([=](QString text) {
		const auto &padding = _st.itemPadding;
		_text.setMarkedText(_st.itemStyle, { text }, MenuTextOptions);
		_textWidth = w - padding.left() - padding.right();
		update();
	}, lifetime());
}

bool RecordingAction::isEnabled() const {
	return true;
}

not_null<QAction*> RecordingAction::action() const {
	return _dummyAction;
}

QPoint RecordingAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage RecordingAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int RecordingAction::contentHeight() const {
	return _startAt ? _bigHeight : _smallHeight;
}

void RecordingAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

base::unique_qptr<Ui::Menu::ItemBase> MakeJoinAsAction(
		not_null<Ui::Menu::Menu*> menu,
		not_null<PeerData*> peer,
		Fn<void()> callback) {
	return base::make_unique_q<JoinAsAction>(
		menu,
		menu->st(),
		peer,
		std::move(callback));
}

base::unique_qptr<Ui::Menu::ItemBase> MakeRecordingAction(
		not_null<Ui::Menu::Menu*> menu,
		rpl::producer<TimeId> startDate,
		Fn<void()> callback) {
	using namespace rpl::mappers;
	return base::make_unique_q<RecordingAction>(
		menu,
		menu->st(),
		rpl::conditional(
			rpl::duplicate(startDate) | rpl::map(!!_1),
			tr::lng_group_call_recording_stop(),
			tr::lng_group_call_recording_start()),
		rpl::duplicate(startDate),
		std::move(callback));
}

} // namespace

void LeaveBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call,
		bool discardChecked,
		BoxContext context) {
	const auto conference = call->conference();
	const auto livestream = call->peer()->isBroadcast();
	const auto scheduled = (call->scheduleDate() != 0);
	if (!scheduled) {
		box->setTitle(conference
			? tr::lng_group_call_leave_title_call()
			: livestream
			? tr::lng_group_call_leave_title_channel()
			: tr::lng_group_call_leave_title());
	}
	const auto inCall = (context == BoxContext::GroupCallPanel);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			(scheduled
				? (livestream
					? tr::lng_group_call_close_sure_channel()
					: tr::lng_group_call_close_sure())
				: (conference
					? tr::lng_group_call_leave_sure_call()
					: livestream
					? tr::lng_group_call_leave_sure_channel()
					: tr::lng_group_call_leave_sure())),
			(inCall ? st::groupCallBoxLabel : st::boxLabel)),
		scheduled ? st::boxPadding : st::boxRowPadding);
	const auto discard = call->canManage()
		? box->addRow(object_ptr<Ui::Checkbox>(
			box.get(),
			(scheduled
				? (livestream
					? tr::lng_group_call_also_cancel_channel()
					: tr::lng_group_call_also_cancel())
				: (livestream
					? tr::lng_group_call_also_end_channel()
					: tr::lng_group_call_also_end())),
			discardChecked,
			(inCall ? st::groupCallCheckbox : st::defaultBoxCheckbox),
			(inCall ? st::groupCallCheck : st::defaultCheck)),
			style::margins(
				st::boxRowPadding.left(),
				st::boxRowPadding.left(),
				st::boxRowPadding.right(),
				st::boxRowPadding.bottom()))
		: nullptr;
	const auto weak = base::make_weak(call);
	auto label = scheduled
		? tr::lng_group_call_close()
		: tr::lng_group_call_leave();
	box->addButton(std::move(label), [=] {
		const auto discardCall = (discard && discard->checked());
		box->closeBox();

		if (!weak) {
			return;
		} else if (discardCall) {
			call->discard();
		} else {
			call->hangup();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

object_ptr<Ui::GenericBox> ConfirmBox(Ui::ConfirmBoxArgs &&args) {
	auto copy = std::move(args);
	copy.labelStyle = &st::groupCallBoxLabel;
	return Ui::MakeConfirmBox(std::move(copy));
}

void FillMenu(
		not_null<Ui::DropdownMenu*> menu,
		not_null<PeerData*> peer,
		not_null<GroupCall*> call,
		bool wide,
		Fn<void()> chooseJoinAs,
		Fn<void()> chooseShareScreenSource,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox) {
	const auto weak = base::make_weak(call);
	const auto resolveReal = [=] {
		if (const auto strong = weak.get()) {
			if (const auto real = strong->lookupReal()) {
				return real;
			}
		}
		return (Data::GroupCall*)nullptr;
	};
	const auto real = resolveReal();
	if (!real) {
		return;
	}

	const auto conference = call->conference();
	const auto addEditJoinAs = call->showChooseJoinAs();
	const auto addEditTitle = !conference && call->canManage();
	const auto addEditRecording = !conference
		&& call->canManage()
		&& !real->scheduleDate();
	const auto addScreenCast = !wide
		&& call->videoIsWorking()
		&& !real->scheduleDate();
	if (addEditJoinAs) {
		menu->addAction(MakeJoinAsAction(
			menu->menu(),
			call->joinAs(),
			chooseJoinAs));
		menu->addSeparator();
	}
	if (addEditTitle) {
		const auto livestream = call->peer()->isBroadcast();
		const auto text = (livestream
			? tr::lng_group_call_edit_title_channel
			: tr::lng_group_call_edit_title)(tr::now);
		menu->addAction(text, [=] {
			const auto done = [=](const QString &title) {
				if (const auto strong = weak.get()) {
					strong->changeTitle(title);
				}
			};
			if (const auto real = resolveReal()) {
				showBox(Box(
					EditGroupCallTitleBox,
					peer->name(),
					real->title(),
					livestream,
					done));
			}
		});
	}
	if (addEditRecording) {
		const auto handler = [=] {
			const auto real = resolveReal();
			if (!real) {
				return;
			}
			const auto type = std::make_shared<RecordingType>();
			const auto recordStartDate = real->recordStartDate();
			const auto done = [=](QString title) {
				if (const auto strong = weak.get()) {
					strong->toggleRecording(
						!recordStartDate,
						title,
						(*type) != RecordingType::AudioOnly,
						(*type) == RecordingType::VideoPortrait);
				}
			};
			if (recordStartDate) {
				showBox(Box(
					StopGroupCallRecordingBox,
					done));
			} else {
				const auto typeDone = [=](RecordingType newType) {
					*type = newType;
					showBox(Box(
						AddTitleGroupCallRecordingBox,
						real->title(),
						done));
				};
				showBox(Box(StartGroupCallRecordingBox, typeDone));
			}
		};
		menu->addAction(MakeRecordingAction(
			menu->menu(),
			real->recordStartDateValue(),
			handler));
	}
	if (addScreenCast) {
		const auto sharing = call->isSharingScreen();
		const auto toggle = [=] {
			if (const auto strong = weak.get()) {
				if (sharing) {
					strong->toggleScreenSharing(std::nullopt);
				} else {
					chooseShareScreenSource();
				}
			}
		};
		menu->addAction(
			(call->isSharingScreen()
				? tr::lng_group_call_screen_share_stop(tr::now)
				: tr::lng_group_call_screen_share_start(tr::now)),
			toggle);
	}
	menu->addAction(tr::lng_group_call_settings(tr::now), [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(SettingsBox, strong));
		}
	});
	const auto finish = [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(
				LeaveBox,
				strong,
				true,
				BoxContext::GroupCallPanel));
		}
	};
	const auto livestream = real->peer()->isBroadcast();
	menu->addAction(MakeAttentionAction(
		menu->menu(),
		(!call->canManage()
			? tr::lng_group_call_leave
			: real->scheduleDate()
			? (livestream
				? tr::lng_group_call_cancel_channel
				: tr::lng_group_call_cancel)
			: (livestream
				? tr::lng_group_call_end_channel
				: tr::lng_group_call_end))(tr::now),
		finish));
}

base::unique_qptr<Ui::Menu::ItemBase> MakeAttentionAction(
		not_null<Ui::Menu::Menu*> menu,
		const QString &text,
		Fn<void()> callback) {
	return base::make_unique_q<Ui::Menu::Action>(
		menu,
		st::groupCallFinishMenu,
		Ui::Menu::CreateAction(
			menu,
			text,
			std::move(callback)),
		nullptr,
		nullptr);

}

} // namespace Calls::Group
