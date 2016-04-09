cd ../../
while IFS='' read -r line || [[ -n "$line" ]]; do
  tx pull -f -l $line --minimum-perc=100
done < tdesktop/Telegram/Resources/LangList
cd translations/telegram-desktop.langstrings/
for file in *.strings; do
  iconv -f "UTF-16LE" -t "UTF-8" "$file" > "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp"
  awk '{ if (NR==1) sub(/^\xef\xbb\xbf/,""); sub(//,""); print }' "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp" > "../../tdesktop/Telegram/SourceFiles/langs/lang_$file"
  rm "../../tdesktop/Telegram/SourceFiles/langs/lang_$file.tmp"
done
cd ../../tdesktop/Telegram/
touch SourceFiles/telegram.qrc
