# Customer Package Generation

Generate a customer-specific source archive from the mixed internal workspace:

```bash
python scripts/package_vendor.py
python scripts/package_vendor.py lhandpro
python scripts/package_vendor.py lumibot
```

When no vendor argument is provided, the script prompts for:

```text
1. LHandPro
2. LumiBot
```

The script copies the reusable ROS workspace into `dist/`, overlays only the
selected vendor's model and SDK assets, rewrites the default model and SDK
profile, scans for forbidden vendor strings as warnings, and creates a zip
archive.

Customer packages intentionally exclude `AGENTS.md`, `.git`, build outputs, and
the `packaging/` metadata so the generated source tree does not expose other
vendor profiles.

If a vendor `.so` still contains old source-file or symbol strings, run on a
Linux machine with binutils installed and add `--strip-libs`. If forbidden
strings remain after stripping, rebuild the vendor SDK with clean source names
and debug symbols removed.

Use `--strict-scan` when a delivery must fail on any forbidden vendor string:

```bash
python scripts/package_vendor.py lumibot --strict-scan
```
