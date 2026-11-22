using Factory = Fn<TextWithEntities()>;
TextWithEntities other();

void run() {
	TextWithEntities current;
	auto empty = tr::marked();
	auto custom = other::TextWithEntities();
	auto text = TextWithEntities(value);
	auto commented = TextWithEntities(/* keep */);
	auto object = new TextWithEntities();
}
