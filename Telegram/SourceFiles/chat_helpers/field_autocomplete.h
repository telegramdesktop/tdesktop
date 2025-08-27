/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "ui/effects/animations.h"
#include "ui/effects/message_sending_animation_common.h"
#include "ui/rp_widget.h"
#include "base/timer.h"
#include "base/object_ptr.h"

namespace style {
struct EmojiPan;
} // namespace style

namespace Ui {
class PopupMenu;
class ScrollArea;
class InputField;
} // namespace Ui

namespace Lottie {
class SinglePlayer;
class FrameRenderer;
} // namespace Lottie;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class DocumentMedia;
} // namespace Data

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace ChatHelpers {

struct ComposeFeatures;
struct FileChosen;
class Show;

enum class FieldAutocompleteChooseMethod {
	ByEnter,
	ByTab,
	ByClick,
};

class FieldAutocomplete final : public Ui::RpWidget {
public:
	FieldAutocomplete(
		QWidget *parent,
		std::shared_ptr<Show> show,
		const style::EmojiPan *stOverride = nullptr);
	~FieldAutocomplete();

	[[nodiscard]] std::shared_ptr<Show> uiShow() const;

	bool clearFilteredBotCommands();
	void showFiltered(
		not_null<PeerData*> peer,
		QString query,
		bool addInlineBots);

	void showStickers(EmojiPtr emoji);
	[[nodiscard]] EmojiPtr stickersEmoji() const;

	void setBoundings(QRect boundings);

	[[nodiscard]] const QString &filter() const;
	[[nodiscard]] ChatData *chat() const;
	[[nodiscard]] ChannelData *channel() const;
	[[nodiscard]] UserData *user() const;

	[[nodiscard]] int32 innerTop();
	[[nodiscard]] int32 innerBottom();

	bool eventFilter(QObject *obj, QEvent *e) override;

	using ChooseMethod = FieldAutocompleteChooseMethod;
	struct MentionChosen {
		not_null<UserData*> user;
		QString mention;
		ChooseMethod method = ChooseMethod::ByEnter;
	};
	struct HashtagChosen {
		QString hashtag;
		ChooseMethod method = ChooseMethod::ByEnter;
	};
	struct BotCommandChosen {
		not_null<UserData*> user;
		QString command;
		ChooseMethod method = ChooseMethod::ByEnter;
	};
	using StickerChosen = FileChosen;
	enum class Type {
		Mentions,
		Hashtags,
		BotCommands,
		Stickers,
	};

	bool chooseSelected(ChooseMethod method) const;

	[[nodiscard]] bool stickersShown() const {
		return !_srows.empty();
	}

	[[nodiscard]] bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) {
			return false;
		}
		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void setModerateKeyActivateCallback(Fn<bool(int)> callback) {
		_moderateKeyActivateCallback = std::move(callback);
	}
	void setSendMenuDetails(Fn<SendMenu::Details()> &&callback);

	void hideFast();
	void showAnimated();
	void hideAnimated();

	void requestRefresh();
	[[nodiscard]] rpl::producer<> refreshRequests() const;
	void requestStickersUpdate();
	[[nodiscard]] rpl::producer<> stickersUpdateRequests() const;

	[[nodiscard]] rpl::producer<MentionChosen> mentionChosen() const;
	[[nodiscard]] rpl::producer<HashtagChosen> hashtagChosen() const;
	[[nodiscard]] rpl::producer<BotCommandChosen> botCommandChosen() const;
	[[nodiscard]] rpl::producer<StickerChosen> stickerChosen() const;
	[[nodiscard]] rpl::producer<Type> choosingProcesses() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	class Inner;
	friend class Inner;
	struct StickerSuggestion;
	struct MentionRow;
	struct BotCommandRow;

	using HashtagRows = std::vector<QString>;
	using BotCommandRows = std::vector<BotCommandRow>;
	using StickerRows = std::vector<StickerSuggestion>;
	using MentionRows = std::vector<MentionRow>;

	void animationCallback();
	void hideFinish();

	void updateFiltered(bool resetScroll = false);
	void recount(bool resetScroll = false);
	StickerRows getStickerSuggestions();

	const std::shared_ptr<Show> _show;
	const not_null<Main::Session*> _session;
	const style::EmojiPan &_st;
	QPixmap _cache;
	MentionRows _mrows;
	HashtagRows _hrows;
	BotCommandRows _brows;
	StickerRows _srows;

	void rowsUpdated(
		MentionRows &&mrows,
		HashtagRows &&hrows,
		BotCommandRows &&brows,
		StickerRows &&srows,
		bool resetScroll);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Inner> _inner;

	ChatData *_chat = nullptr;
	UserData *_user = nullptr;
	ChannelData *_channel = nullptr;
	EmojiPtr _emoji;
	uint64 _stickersSeed = 0;
	Type _type = Type::Mentions;
	QString _filter;
	QRect _boundings;
	bool _addInlineBots;

	bool _hiding = false;

	Ui::Animations::Simple _a_opacity;
	rpl::event_stream<> _refreshRequests;
	rpl::event_stream<> _stickersUpdateRequests;

	Fn<bool(int)> _moderateKeyActivateCallback;

};

struct FieldAutocompleteDescriptor {
	not_null<QWidget*> parent;
	std::shared_ptr<Show> show;
	not_null<Ui::InputField*> field;
	const style::EmojiPan *stOverride = nullptr;
	not_null<PeerData*> peer;
	Fn<ComposeFeatures()> features;
	Fn<SendMenu::Details()> sendMenuDetails;
	Fn<void()> stickerChoosing;
	Fn<void(FileChosen&&)> stickerChosen;
	Fn<void(TextWithTags)> setText;
	Fn<void(QString)> sendBotCommand;
	Fn<void(QString)> processShortcut;
	Fn<bool(int)> moderateKeyActivateCallback;
};
void InitFieldAutocomplete(
	std::unique_ptr<FieldAutocomplete> &autocomplete,
	FieldAutocompleteDescriptor &&descriptor);

} // namespace ChatHelpers
