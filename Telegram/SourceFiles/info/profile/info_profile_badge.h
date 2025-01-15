/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/object_ptr.h"

namespace style {
struct InfoPeerBadge;
} // namespace style

namespace Data {
enum class CustomEmojiSizeTag : uchar;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
class AbstractButton;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Info::Profile {

class EmojiStatusPanel;

enum class BadgeType {
	None = 0x00,
	Verified = 0x01,
	BotVerified = 0x02,
	Premium = 0x04,
	Scam = 0x08,
	Fake = 0x10,
};
inline constexpr bool is_flag_type(BadgeType) { return true; }

class Badge final {
public:
	struct Content {
		BadgeType badge = BadgeType::None;
		EmojiStatusId emojiStatusId;

		friend inline bool operator==(Content, Content) = default;
	};
	Badge(
		not_null<QWidget*> parent,
		const style::InfoPeerBadge &st,
		not_null<Main::Session*> session,
		rpl::producer<Content> content,
		EmojiStatusPanel *emojiStatusPanel,
		Fn<bool()> animationPaused,
		int customStatusLoopsLimit = 0,
		base::flags<BadgeType> allowed
			= base::flags<BadgeType>::from_raw(-1));

	~Badge();

	[[nodiscard]] Ui::RpWidget *widget() const;

	void setPremiumClickCallback(Fn<void()> callback);
	[[nodiscard]] rpl::producer<> updated() const;
	void move(int left, int top, int bottom);

	[[nodiscard]] Data::CustomEmojiSizeTag sizeTag() const;

private:
	void setContent(Content content);

	const not_null<QWidget*> _parent;
	const style::InfoPeerBadge &_st;
	const not_null<Main::Session*> _session;
	EmojiStatusPanel *_emojiStatusPanel = nullptr;
	const int _customStatusLoopsLimit = 0;
	std::unique_ptr<Ui::Text::CustomEmoji> _emojiStatus;
	base::flags<BadgeType> _allowed;
	Content _content;
	Fn<void()> _premiumClickCallback;
	Fn<bool()> _animationPaused;
	object_ptr<Ui::AbstractButton> _view = { nullptr };
	rpl::event_stream<> _updated;
	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<Badge::Content> BadgeContentForPeer(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<Badge::Content> VerifiedContentForPeer(
	not_null<PeerData*> peer);

} // namespace Info::Profile
