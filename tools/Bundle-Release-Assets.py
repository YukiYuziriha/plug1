"""Audit all reachable history and prepare local release assets without compiling."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def audit():
    rows = []
    binary_hashes = {}
    expected = None
    for commit in git("rev-list", "--all", "--reverse").decode().splitlines():
        entries = {}
        for line in git("ls-tree", "-r", commit).decode().splitlines():
            metadata, path = line.split("\t", 1)
            entries[path] = metadata.split()[2]
        cmake = git("show", commit + ":source/CMakeLists.txt").decode()
        dependencies = {name: " ".join(value.split()) for name, value in
                        re.findall(r"FetchContent_Declare\(([^\s]+)(.*?)\)", cmake, re.S)}
        if expected is None:
            expected = dependencies
        if dependencies != expected:
            raise RuntimeError(f"Historical dependencies differ at {commit}; extend dependency package before release")
        binaries = {}
        for path, blob in entries.items():
            if path.endswith((".clap", ".vst3")):
                if blob not in binary_hashes:
                    binary_hashes[blob] = digest(git("cat-file", "blob", blob))
                binaries[path] = {"git_blob": blob, "sha256": binary_hashes[blob]}
        rows.append({"commit": commit,
                     "version": re.search(r"project\(VocalStack VERSION (\S+)", cmake).group(1),
                     "cmake_blob": entries["source/CMakeLists.txt"],
                     "source_tree": git("rev-parse", commit + ":source").decode().strip(),
                     "dependencies": dependencies, "co_delivered_binaries": binaries})
    return {"scope": "All commits reachable from local refs at packaging time",
            "history_tip": git("rev-parse", "HEAD").decode().strip(),
            "provenance": "Binary hashes identify bytes co-delivered with each commit's source; no historical rebuild or exact producing-commit claim",
            "dependency_result": "Identical FetchContent declarations in every audited commit; use the external directory from the v1.11.0 corresponding-source ZIP",
            "commit_count": len(rows), "unique_binary_blobs": len(binary_hashes), "commits": rows}


def write_zip(path, files):
    if path.exists():
        raise RuntimeError(f"Refusing to overwrite {path}")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, source in sorted(files.items()):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--output-directory", type=Path, default=ROOT / "source/build/publication")
    args = parser.parse_args()
    manifest_path = ROOT / "docs/Historical-Source-Manifest.json"
    manifest = audit()
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Audited {manifest['commit_count']} commits; {manifest['unique_binary_blobs']} unique binary blobs; all dependency declarations identical", flush=True)
    if args.audit_only:
        return
    output = args.output_directory.resolve()
    output.mkdir(parents=True, exist_ok=True)
    names = ["VocalStack-1.11.0-corresponding-source.zip", "VocalStack-source-history.zip", "VocalStack-1.11.0-Windows-x64.zip"]
    if any((output / name).exists() for name in names):
        raise RuntimeError("Output assets already exist; use a new output directory")
    subprocess.run([sys.executable, str(ROOT / "tools/Bundle-Corresponding-Source.py"),
                    "--revision", "12534e87440ca33a41f12bde53956319bd06ebab",
                    "--output", str(output / names[0])], check=True, cwd=ROOT)
    with tempfile.TemporaryDirectory(prefix="vocalstack-history-") as temporary:
        bundle = Path(temporary) / "VocalStack.gitbundle"
        subprocess.run(["git", "-C", str(ROOT), "bundle", "create", str(bundle), "--all"], check=True)
        subprocess.run(["git", "-C", str(ROOT), "bundle", "verify", str(bundle)], check=True)
        clone = Path(temporary) / "restored"
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", str(bundle), str(clone)], check=True)
        restored = subprocess.check_output(["git", "-C", str(clone), "rev-list", "--all"]).decode().splitlines()
        if set(restored) != {row["commit"] for row in manifest["commits"]}:
            raise RuntimeError("Restored bundle history does not match audit")
        for row in manifest["commits"]:
            actual = subprocess.check_output(["git", "-C", str(clone), "rev-parse", row["commit"] + ":source"]).decode().strip()
            if actual != row["source_tree"]:
                raise RuntimeError("Restored source tree mismatch")
        write_zip(output / names[1], {
            "VocalStack-history/VocalStack.gitbundle": bundle,
            "VocalStack-history/Historical-Source-Manifest.json": manifest_path,
            "VocalStack-history/README.md": ROOT / "docs/Historical-Source.md",
            "VocalStack-history/LICENSE": ROOT / "LICENSE",
            "VocalStack-history/COPYING.AGPLv3": ROOT / "COPYING.AGPLv3",
        })
    files = {}
    for path in sorted((ROOT / "Windows-x64").rglob("*")):
        if path.is_file():
            relative = path.relative_to(ROOT).as_posix()
            if path.suffix in (".clap", ".vst3"):
                if path.read_bytes() != git("show", "12534e8:" + relative):
                    raise RuntimeError(f"Binary changed: {relative}")
            files["VocalStack-1.11.0/" + relative] = path
    for name in ("LICENSE", "COPYING.AGPLv3", "README.md", "docs/Building.md", "docs/Licensing.md", "docs/Historical-Source.md", "docs/Historical-Source-Manifest.json", "docs/Getting-Started.md", "docs/Windows-Resource-Limits.md"):
        files["VocalStack-1.11.0/" + name] = ROOT / name
    write_zip(output / names[2], files)
    checksums = []
    for name in names:
        path = output / name
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:
                raise RuntimeError(f"ZIP integrity error: {name}")
        checksum = file_digest(path) + "  " + name + "\n"
        path.with_suffix(path.suffix + ".sha256").write_text(checksum, encoding="ascii")
        checksums.append(checksum)
        print(f"{path.stat().st_size} bytes  {checksum.strip()}", flush=True)
    (output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="ascii")


if __name__ == "__main__":
    main()
