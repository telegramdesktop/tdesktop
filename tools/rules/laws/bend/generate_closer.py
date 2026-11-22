import argparse
from itertools import product
import json
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


HERE = Path(__file__).resolve().parent
CATALOG = HERE.parent / "catalog.json"
KEY = "closing_parenthesis_decisions"


def decisions(bend):
    with TemporaryDirectory(prefix="tdesktop-closer-") as folder:
        root = Path(folder)
        shutil.copyfile(HERE / "closer.bend", root / "closer.bend")
        path = root / "table.bend"
        rows = [f"Closer.encode(Closer.decide({', '.join(facts)}))"
            for facts in product(("False{}", "True{}"), repeat=4)]
        path.write_text("import Base\nimport closer.bend as Closer\n"
            "def main() -> List<&2, U32>:\n  [" + ", ".join(rows) + "]\n",
            encoding="utf-8")
        result = subprocess.check_output([bend, path], text=True)
    table = json.loads(result)
    if len(table) != len(rows) or any(row not in (0, 1, 2) for row in table):
        raise ValueError(f"invalid decisions: {table}")
    return "".join(map(str, table))


def main():
    parser = argparse.ArgumentParser(description="Check the closing-parenthesis decision table.")
    parser.add_argument("--write", action="store_true", help="update the table in catalog.json")
    args = parser.parse_args()
    bend = shutil.which("bend")
    if not bend:
        parser.error("Bend 2 is required to evaluate closer.bend")
    table = decisions(bend)
    catalog = json.loads(CATALOG.read_bytes())
    if args.write:
        catalog["layout"][KEY] = table
        CATALOG.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    elif catalog["layout"].get(KEY) != table:
        parser.error("the decision table differs from closer.bend; regenerate it with --write")
    print(f"Closing-parenthesis decisions match Bend for all {len(table)} inputs.")


if __name__ == "__main__":
    main()
