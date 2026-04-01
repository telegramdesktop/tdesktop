#pragma once

#include <array>

#include <QString>

namespace TeleForge::OfficialResources {

enum class Type {
	Channel,
	Chat,
	Website,
};

struct Entry {
	Type type = Type::Website;
	const char *id = "";
	const char *label = "";
	const char *url = "";
	const char *usernameOrId = "";
};

inline constexpr std::array<Entry, 3> kEntries = { {
	{
		.type = Type::Channel,
		.id = "teleforge/channel",
		.label = "@teleforge_official",
		.url = "https://t.me/teleforge_official",
		.usernameOrId = "teleforge_official",
	},
	{
		.type = Type::Chat,
		.id = "teleforge/chat",
		.label = "@teleforgechat",
		.url = "https://t.me/teleforgechat",
		.usernameOrId = "teleforgechat",
	},
	{
		.type = Type::Website,
		.id = "teleforge/website",
		.label = "tele-forge.ru",
		.url = "https://tele-forge.ru",
		.usernameOrId = "",
	},
} };

[[nodiscard]] inline bool IsOfficialUsername(const QString &username) {
	for (const auto &entry : kEntries) {
		if (!entry.usernameOrId || !*entry.usernameOrId) {
			continue;
		}
		if (username == QString::fromLatin1(entry.usernameOrId)) {
			return true;
		}
	}
	return false;
}

} // namespace TeleForge::OfficialResources
