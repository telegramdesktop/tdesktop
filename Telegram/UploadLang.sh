cd ../../
while IFS='' read -r line || [[ -n "$line" ]]; do
  tx pull -f -l $line
done < tdesktop/Telegram/Resources/LangList
tx push -s
cd tdesktop/Telegram/
