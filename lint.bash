FILES=$(find . \( -name '*.h' -or -name '*.cpp' -or -name '*.cc' \) -not -path '*/Telegram/*' -not -path '*/cmake/*' -not -path '*/lib/*' -not -path "*/build/*")
echo "Files to be linted:"
echo "$FILES"
for FILE in $FILES; do
  clang-tidy -p="$1" $FILE
done