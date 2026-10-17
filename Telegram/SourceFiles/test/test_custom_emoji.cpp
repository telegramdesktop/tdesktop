/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_custom_emoji.h"

#include "test/test_agent.h"
#include "test/test_log.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/emoji_config.h"

namespace Test {
namespace {

auto HandedOut = 0;

[[nodiscard]] base::flat_set<DocumentId> &Registered() {
	static auto result = base::flat_set<DocumentId>();
	return result;
}

class StubEmoji final : public Ui::Text::CustomEmoji {
public:
	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	[[nodiscard]] static int Side();

};

int StubEmoji::width() {
	return Side();
}

QString StubEmoji::entityData() {
	return QString();
}

void StubEmoji::paint(QPainter &p, const Context &context) {
	const auto side = Side();
	p.fillRect(
		QRect(context.position, QSize(side, side)),
		*context.textColor);
}

void StubEmoji::unload() {
}

bool StubEmoji::ready() {
	return true;
}

bool StubEmoji::readyInDefaultState() {
	return true;
}

int StubEmoji::Side() {
	return Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio();
}

} // namespace

void RegisterStubEmojiDocument(DocumentId documentId) {
	if (!Registered().emplace(documentId).second) {
		return;
	}
	Note(u"stub emoji: registered document %1 (registered=%2)"_q
		.arg(qulonglong(documentId))
		.arg(int(Registered().size())));
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeStubEmoji(DocumentId documentId) {
	if (!Active() || !Registered().contains(documentId)) {
		return nullptr;
	}
	++HandedOut;
	return std::make_unique<StubEmoji>();
}

int StubEmojiHandedOutCount() {
	return HandedOut;
}

} // namespace Test

#endif // _DEBUG
