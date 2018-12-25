/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_helper.h"

#include "dialogs/dialogs_key.h"
#include "data/data_drafts.h"
#include "history/history.h"
#include "boxes/abstract_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/text_entity.h"
#include "ui/text_options.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"

namespace Support {
namespace {

constexpr auto kOccupyFor = TimeId(60);
constexpr auto kReoccupyEach = 30 * TimeMs(1000);
constexpr auto kMaxSupportInfoLength = MaxMessageSize * 4;

class EditInfoBox : public BoxContent {
public:
	EditInfoBox(
		QWidget*,
		const TextWithTags &text,
		Fn<void(TextWithTags, Fn<void(bool success)>)> submit);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	object_ptr<Ui::InputField> _field = { nullptr };
	Fn<void(TextWithTags, Fn<void(bool success)>)> _submit;

};

EditInfoBox::EditInfoBox(
	QWidget*,
	const TextWithTags &text,
	Fn<void(TextWithTags, Fn<void(bool success)>)> submit)
: _field(
	this,
	st::supportInfoField,
	Ui::InputField::Mode::MultiLine,
	[] { return QString("Support information"); },
	text)
, _submit(std::move(submit)) {
	_field->setMaxLength(kMaxSupportInfoLength);
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	_field->setInstantReplaces(Ui::InstantReplaces::Default());
	_field->setInstantReplacesEnabled(Global::ReplaceEmojiValue());
	_field->setMarkdownReplacesEnabled(rpl::single(true));
	_field->setEditLinkCallback(DefaultEditLinkCallback(_field));
}

void EditInfoBox::prepare() {
	setTitle([] { return QString("Edit support information"); });

	const auto save = [=] {
		const auto done = crl::guard(this, [=](bool success) {
			if (success) {
				closeBox();
			} else {
				_field->showError();
			}
		});
		_submit(_field->getTextWithAppliedMarkdown(), done);
	};
	addButton(langFactory(lng_settings_save), save);
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	connect(_field, &Ui::InputField::submitted, save);
	connect(_field, &Ui::InputField::cancelled, [=] { closeBox(); });
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_field);

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);

	widthValue(
	) | rpl::start_with_next([=](int width) {
		_field->resizeToWidth(
			width - st::boxPadding.left() - st::boxPadding.right());
		_field->moveToLeft(st::boxPadding.left(), st::boxPadding.bottom());
	}, _field->lifetime());

	_field->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			st::boxPadding.bottom() + height + st::boxPadding.bottom());
	}, _field->lifetime());
}

void EditInfoBox::setInnerFocus() {
	_field->setFocusFast();
}

QString FormatDateTime(TimeId value) {
	const auto now = QDateTime::currentDateTime();
	const auto date = ParseDateTime(value);
	if (date.date() == now.date()) {
		return lng_mediaview_today(
			lt_time,
			date.time().toString(cTimeFormat()));
	} else if (date.date().addDays(1) == now.date()) {
		return lng_mediaview_yesterday(
			lt_time,
			date.time().toString(cTimeFormat()));
	} else {
		return lng_mediaview_date_time(
			lt_date,
			date.date().toString(qsl("dd.MM.yy")),
			lt_time,
			date.time().toString(cTimeFormat()));
	}
}

uint32 OccupationTag() {
	return uint32(Sandbox::UserTag() & 0xFFFFFFFFU);
}

QString NormalizeName(QString name) {
	return name.replace(':', '_').replace(';', '_');
}

Data::Draft OccupiedDraft(const QString &normalizedName) {
	const auto now = unixtime(), till = now + kOccupyFor;
	return {
		TextWithTags{ "t:"
			+ QString::number(till)
			+ ";u:"
			+ QString::number(OccupationTag())
			+ ";n:"
			+ normalizedName },
		MsgId(0),
		MessageCursor(),
		false
	};
}

[[nodiscard]] bool TrackHistoryOccupation(History *history) {
	if (!history) {
		return false;
	} else if (const auto user = history->peer->asUser()) {
		return !user->botInfo;
	}
	return false;
}

uint32 ParseOccupationTag(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = uint32();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= unixtime()) {
				valid = true;
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			result = part.mid(2).toUInt();
		}
	}
	return valid ? result : 0;
}

QString ParseOccupationName(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return QString();
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return QString();
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = QString();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= unixtime()) {
				valid = true;
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("n:"))) {
#ifndef OS_MAC_OLD
			result = part.mid(2).toString();
#else // OS_MAC_OLD
			result = part.mid(2);
#endif // OS_MAC_OLD
		}
	}
	return valid ? result : QString();
}

TimeId OccupiedBySomeoneTill(History *history) {
	if (!TrackHistoryOccupation(history)) {
		return 0;
	}
	const auto draft = history->cloudDraft();
	if (!draft) {
		return 0;
	}
	const auto &text = draft->textWithTags.text;
#ifndef OS_MAC_OLD
	const auto parts = text.splitRef(';');
#else // OS_MAC_OLD
	const auto parts = text.split(';');
#endif // OS_MAC_OLD
	auto valid = false;
	auto result = TimeId();
	for (const auto &part : parts) {
		if (part.startsWith(qstr("t:"))) {
			if (part.mid(2).toInt() >= unixtime()) {
				result = part.mid(2).toInt();
			} else {
				return 0;
			}
		} else if (part.startsWith(qstr("u:"))) {
			if (part.mid(2).toUInt() != OccupationTag()) {
				valid = true;
			} else {
				return 0;
			}
		}
	}
	return valid ? result : 0;
}

} // namespace

Helper::Helper(not_null<AuthSession*> session)
: _session(session)
, _templates(_session)
, _reoccupyTimer([=] { reoccupy(); })
, _checkOccupiedTimer([=] { checkOccupiedChats(); }) {
	request(MTPhelp_GetSupportName(
	)).done([=](const MTPhelp_SupportName &result) {
		result.match([&](const MTPDhelp_supportName &data) {
			setSupportName(qs(data.vname));
		});
	}).fail([=](const RPCError &error) {
		setSupportName(
			qsl("[rand^") + QString::number(Sandbox::UserTag()) + ']');
	}).send();
}

void Helper::registerWindow(not_null<Window::Controller*> controller) {
	controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto history = key.history();
		return TrackHistoryOccupation(history) ? history : nullptr;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](History *history) {
		updateOccupiedHistory(controller, history);
	}, controller->lifetime());
}

void Helper::cloudDraftChanged(not_null<History*> history) {
	chatOccupiedUpdated(history);
	if (history != _occupiedHistory) {
		return;
	}
	occupyIfNotYet();
}

void Helper::chatOccupiedUpdated(not_null<History*> history) {
	if (const auto till = OccupiedBySomeoneTill(history)) {
		_occupiedChats[history] = till + 2;
		Notify::peerUpdatedDelayed(
			history->peer,
			Notify::PeerUpdate::Flag::UserOccupiedChanged);
		checkOccupiedChats();
	} else if (_occupiedChats.take(history)) {
		Notify::peerUpdatedDelayed(
			history->peer,
			Notify::PeerUpdate::Flag::UserOccupiedChanged);
	}
}

void Helper::checkOccupiedChats() {
	const auto now = unixtime();
	while (!_occupiedChats.empty()) {
		const auto nearest = ranges::min_element(
			_occupiedChats,
			std::less<>(),
			[](const auto &pair) { return pair.second; });
		if (nearest->second <= now) {
			const auto history = nearest->first;
			_occupiedChats.erase(nearest);
			Notify::peerUpdatedDelayed(
				history->peer,
				Notify::PeerUpdate::Flag::UserOccupiedChanged);
		} else {
			_checkOccupiedTimer.callOnce(
				(nearest->second - now) * TimeMs(1000));
			return;
		}
	}
	_checkOccupiedTimer.cancel();
}

void Helper::updateOccupiedHistory(
		not_null<Window::Controller*> controller,
		History *history) {
	if (isOccupiedByMe(_occupiedHistory)) {
		_occupiedHistory->clearCloudDraft();
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
	_occupiedHistory = history;
	occupyInDraft();
}

void Helper::setSupportName(const QString &name) {
	_supportName = name;
	_supportNameNormalized = NormalizeName(name);
	occupyIfNotYet();
}

void Helper::occupyIfNotYet() {
	if (!isOccupiedByMe(_occupiedHistory)) {
		occupyInDraft();
	}
}

void Helper::occupyInDraft() {
	if (_occupiedHistory
		&& !isOccupiedBySomeone(_occupiedHistory)
		&& !_supportName.isEmpty()) {
		const auto draft = OccupiedDraft(_supportNameNormalized);
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
		_reoccupyTimer.callEach(kReoccupyEach);
	}
}

void Helper::reoccupy() {
	if (isOccupiedByMe(_occupiedHistory)) {
		const auto draft = OccupiedDraft(_supportNameNormalized);
		_occupiedHistory->createCloudDraft(&draft);
		_session->api().saveDraftToCloudDelayed(_occupiedHistory);
	}
}

bool Helper::isOccupiedByMe(History *history) const {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag == OccupationTag());
	}
	return false;
}

bool Helper::isOccupiedBySomeone(History *history) const {
	if (const auto tag = ParseOccupationTag(history)) {
		return (tag != OccupationTag());
	}
	return false;
}

void Helper::refreshInfo(not_null<UserData*> user) {
	request(MTPhelp_GetUserInfo(
		user->inputUser
	)).done([=](const MTPhelp_UserInfo &result) {
		applyInfo(user, result);
		if (_userInfoEditPending.contains(user)) {
			_userInfoEditPending.erase(user);
			showEditInfoBox(user);
		}
	}).send();
}

void Helper::applyInfo(
		not_null<UserData*> user,
		const MTPhelp_UserInfo &result) {
	const auto notify = [&] {
		Notify::peerUpdatedDelayed(
			user,
			Notify::PeerUpdate::Flag::UserSupportInfoChanged);
	};
	const auto remove = [&] {
		if (_userInformation.take(user)) {
			notify();
		}
	};
	result.match([&](const MTPDhelp_userInfo &data) {
		auto info = UserInfo();
		info.author = qs(data.vauthor);
		info.date = data.vdate.v;
		info.text = TextWithEntities{
			qs(data.vmessage),
			TextUtilities::EntitiesFromMTP(data.ventities.v) };
		if (info.text.empty()) {
			remove();
		} else if (_userInformation[user] != info) {
			_userInformation[user] = info;
			notify();
		}
	}, [&](const MTPDhelp_userInfoEmpty &) {
		remove();
	});
}

rpl::producer<UserInfo> Helper::infoValue(not_null<UserData*> user) const {
	return Notify::PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserSupportInfoChanged
	) | rpl::map([=] {
		return infoCurrent(user);
	});
}

rpl::producer<QString> Helper::infoLabelValue(
		not_null<UserData*> user) const {
	return infoValue(
		user
	) | rpl::map([](const Support::UserInfo &info) {
		return info.author + ", " + FormatDateTime(info.date);
	});
}

rpl::producer<TextWithEntities> Helper::infoTextValue(
		not_null<UserData*> user) const {
	return infoValue(
		user
	) | rpl::map([](const Support::UserInfo &info) {
		return info.text;
	});
}

UserInfo Helper::infoCurrent(not_null<UserData*> user) const {
	const auto i = _userInformation.find(user);
	return (i != end(_userInformation)) ? i->second : UserInfo();
}

void Helper::editInfo(not_null<UserData*> user) {
	if (!_userInfoEditPending.contains(user)) {
		_userInfoEditPending.emplace(user);
		refreshInfo(user);
	}
}

void Helper::showEditInfoBox(not_null<UserData*> user) {
	const auto info = infoCurrent(user);
	const auto editData = TextWithTags{
		info.text.text,
		ConvertEntitiesToTextTags(info.text.entities)
	};

	const auto save = [=](TextWithTags result, Fn<void(bool)> done) {
		saveInfo(user, TextWithEntities{
			result.text,
			ConvertTextTagsToEntities(result.tags)
		}, done);
	};
	Ui::show(Box<EditInfoBox>(editData, save), LayerOption::KeepOther);
}

void Helper::saveInfo(
		not_null<UserData*> user,
		TextWithEntities text,
		Fn<void(bool success)> done) {
	const auto i = _userInfoSaving.find(user);
	if (i != end(_userInfoSaving)) {
		if (i->second.data == text) {
			return;
		} else {
			i->second.data = text;
			request(base::take(i->second.requestId)).cancel();
		}
	} else {
		_userInfoSaving.emplace(user, SavingInfo{ text });
	}

	TextUtilities::PrepareForSending(
		text,
		Ui::ItemTextDefaultOptions().flags);
	TextUtilities::Trim(text);

	const auto entities = TextUtilities::EntitiesToMTP(
		text.entities,
		TextUtilities::ConvertOption::SkipLocal);
	_userInfoSaving[user].requestId = request(MTPhelp_EditUserInfo(
		user->inputUser,
		MTP_string(text.text),
		entities
	)).done([=](const MTPhelp_UserInfo &result) {
		applyInfo(user, result);
		done(true);
	}).fail([=](const RPCError &error) {
		done(false);
	}).send();
}

Templates &Helper::templates() {
	return _templates;
}

QString ChatOccupiedString(not_null<History*> history) {
	const auto hand = QString::fromUtf8("\xe2\x9c\x8b\xef\xb8\x8f");
	const auto name = ParseOccupationName(history);
	return (name.isEmpty() || name.startsWith(qstr("[rand^")))
		? hand + " chat taken"
		: hand + ' ' + name + " is here";
}

QString InterpretSendPath(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return "App Error: Could not open interpret file: " + path;
	}
	const auto content = QString::fromUtf8(f.readAll());
	f.close();
	const auto lines = content.split('\n');
	auto toId = PeerId(0);
	auto filePath = QString();
	auto caption = QString();
	for (const auto &line : lines) {
		if (line.startsWith(qstr("from: "))) {
			if (Auth().userId() != line.mid(qstr("from: ").size()).toInt()) {
				return "App Error: Wrong current user.";
			}
		} else if (line.startsWith(qstr("channel: "))) {
			const auto channelId = line.mid(qstr("channel: ").size()).toInt();
			toId = peerFromChannel(channelId);
		} else if (line.startsWith(qstr("file: "))) {
			const auto path = line.mid(qstr("file: ").size());
			if (!QFile(path).exists()) {
				return "App Error: Could not find file with path: " + path;
			}
			filePath = path;
		} else if (line.startsWith(qstr("caption: "))) {
			caption = line.mid(qstr("caption: ").size());
		} else if (!caption.isEmpty()) {
			caption += '\n' + line;
		} else {
			return "App Error: Invalid command: " + line;
		}
	}
	const auto history = App::historyLoaded(toId);
	if (!history) {
		return "App Error: Could not find channel with id: " + QString::number(peerToChannel(toId));
	}
	Ui::showPeerHistory(history, ShowAtUnreadMsgId);
	Auth().api().sendFiles(
		Storage::PrepareMediaList(QStringList(filePath), st::sendMediaPreviewSize),
		SendMediaType::File,
		{ caption },
		nullptr,
		ApiWrap::SendOptions(history));
	return QString();
}

} // namespace Support
