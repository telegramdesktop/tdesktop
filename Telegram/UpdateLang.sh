cd ../../
tx pull -fa
cd translations/telegram-desktop.langstrings/
for file in *.strings; do
  if [ "$file" != "es_419.strings" ]; then
    iconv -f "UTF-16LE" -t "UTF-8" "$file" > "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp"
    awk '{ if (NR==1) sub(/^\xef\xbb\xbf/,""); sub(//,""); print }' "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp" > "../../tdesktop/Telegram/SourceFiles/langs/lang_$file"
    rm "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp"
  fi
done
cd ../../tdesktop/Telegram/
touch SourceFiles/telegram.qrc
touch SourceFiles/telegram_linux.qrc
