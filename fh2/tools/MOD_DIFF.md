Extract mod_diff.py and Mod Diff.cmd together. Requires Python 3.10+; no packages are needed.

Drop files onto Mod Diff.cmd:
  * Two .carbin files, or two car ZIPs: produces the original carbin JSON diff.
  * Two SQLite databases (.slt/.db/etc.): prompts for the database diff options.
  * Two .str tables: exports added/changed strings to strings.json.
  * One .str table: exports the full table for editing/reference.
For pairs, the first file is vanilla and the second is edited. Confirm or swap this order
when prompted (Windows may change the order when dragging several files).

Carbin output is written beside the edited file using the vanilla filename with
.json appended. Rename it to the game's actual .carbin.json filename if your
vanilla input has a temporary name. Database output defaults to database.json.
Existing files are replaced only after confirmation. Source files stay unchanged.

Database choices: handling changed/deleted rows, override value conflicts,
nearest-free-ID reassignment, table filters, explicit keys, target database path,
output path and validation report. All native database CLI switches remain
available through: python mod_diff.py database --help
Carbin commands: python mod_diff.py carbin --help
String commands: python mod_diff.py strings --help

Strings prompt for the original game table name (e.g. Data_Car), language code
(EN, BR, FR, etc.) and output location. Two-file diffs export changed/new IDs;
missing IDs are not deleted. Use a two-file diff for a deployable strings.json.
A full export overrides every exported string when loaded as a mod.

Examples:
python mod_diff.py strings export Data_Car.str full.json --table Data_Car --language EN
python mod_diff.py strings diff Data_Car.str custom.str strings.json --table Data_Car --language EN
python mod_diff.py strings languages "PATH TO STRINGTABLES" reference.json --table Data_Car

Filtered database views are supported as export-time row snapshots. Choose
Export deletions to include excluded existing rows. Other mods' later rows
are unaffected; no runtime filters are installed.

This is a diff/export tool, not a mod installer. Put generated files into the
appropriate mod folder yourself. Carbin upgrade entries must already exist in
the edited carbin; automatic generation was deliberately removed.

String-table editor compatibility:
The strings exporter recognizes an editor layout where newly added strings are
present but the first section size omits the six-byte index records. Recovery
requires complete UTF-16 strings and a matching following section. It prints a
NOTE and exports those strings without modifying the source STR or ZIP. Other
invalid tables remain errors. This does not repair the original edited archive;
use the generated strings.json with the mod loader instead.
