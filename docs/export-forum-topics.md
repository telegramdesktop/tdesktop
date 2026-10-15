# Exporting forum topics

Forum groups and their topics can be selected in Telegram Desktop's **Export data** settings. For an account-wide export, open **Choose forum topics**, select a forum, then choose **All topics** or individual topics. In a forum-only export, use that forum's **Choose forum topics** control. Topic choices are scoped to each forum in the current export settings; they are not saved across separate exports. With no explicit selection, **All topics** is selected by default and includes topics discovered when the export starts. An explicit selection includes only the checked topic IDs, so topics created later are not automatically added to that subset. Select **All topics** again to clear the explicit subset.

Each selected topic is written to its own folder using its numeric root message ID, rather than its title. This keeps duplicate titles distinct and avoids using untrusted titles as directory names. The containing chat folder is the normal export chat folder in an account-wide export; it is omitted when exporting a single forum. For example:

```text
<export>/
├── lists/chats.html                         # HTML index (when HTML is enabled)
└── <chat-folder>/topic_<root-id>/            # <chat-folder>/ omitted for a single forum
    ├── messages.html                        # HTML topic page, when enabled
    ├── result.json                          # JSON topic data, when enabled
    ├── files/                               # downloaded topic media
    └── stickers/                            # topic custom emoji/stickers
```
The exact files depend on the selected export format and media settings. A selected topic with no exported messages still gets its own entry and page. In a full JSON export, topic records and paths are also represented in the main export index; a forum-only JSON export has a root `topics` index linking to each topic's `result.json`. Media references in topic files are relative to the topic folder; paths in the global JSON index remain relative to the export root.

The General topic (root ID `1`) retains its linked pre-migration group history. That older history is written before the current supergroup history, with migrated message IDs and same-peer reply IDs adjusted so references remain consistent. Other topics contain their current-channel history only. The selected export's date bounds and media choices still apply. **Only my messages** is an existing message-export option, not a topic-selection mode; topic selection does not change that option's behavior.

If topic metadata cannot be read for a forum included in a global export, the export fails rather than silently omitting its topics. This can happen for a known but inaccessible left forum, such as a private forum left by the account. There is no separate exclude-left-forums control; exclude the affected group category or export an accessible forum separately. The topic selector lists available active forums and may not let you exclude an inaccessible forum individually. Missing General metadata, an explicitly selected topic that is no longer available, or unusable/non-advancing topic pagination also fails the export. In the selector, a topic-list load error is shown rather than treated as an empty list; retry after the API/connection issue clears. Wait for the export to report completion before using its output: a cancelled or failed export may have written only part of the selected data.

## Developer verification

The `test_export_output_topics` CMake target is included when `DESKTOP_APP_TEST_APPS=ON`. In a normally configured Telegram Desktop build with its required dependencies installed, build and run it from the repository root:

```sh
cmake --build out --target test_export_output_topics
QT_QPA_PLATFORM=offscreen out/test_export_output_topics
```

The test uses synthetic data and exercises the real HTML/JSON writers; it requires no Telegram account or live API request. An optional first argument selects a persistent output directory, which must not already exist:

```sh
QT_QPA_PLATFORM=offscreen out/test_export_output_topics /path/to/new/export-smoke
```
