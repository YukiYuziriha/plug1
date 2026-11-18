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
    parser.add_argument("--version", default="1.11.0")
    parser.add_argument("--revision", default="12534e87440ca33a41f12bde53956319bd06ebab")
    parser.add_argument("--working-tree", action="store_true",
                        help="Package an explicitly uncommitted source snapshot with BUILD-PROVENANCE.json")
    parser.add_argument("--skip-history", action="store_true",
                        help="Do not regenerate the separately published historical audit/archive")
    parser.add_argument("--output-directory", type=Path, default=ROOT / "source/build/publication")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        raise RuntimeError("Expected a numeric major.minor.patch version")
    if args.audit_only and args.skip_history:
        parser.error("--audit-only cannot be combined with --skip-history")
    cmake = ((ROOT / "source/CMakeLists.txt").read_text(encoding="utf-8") if args.working_tree
             else git("show", args.revision + ":source/CMakeLists.txt").decode())
    if not re.search(r"project\(VocalStack VERSION " + re.escape(args.version) + r"(?:\s|\))", cmake):
        raise RuntimeError("Selected source version differs from requested release")
    provenance = None
    if args.working_tree:
        provenance = json.loads((ROOT / "Windows-x64/BUILD-PROVENANCE.json").read_text(encoding="utf-8"))
        if provenance["version"] != args.version:
            raise RuntimeError("Build provenance version differs from requested release")
        for name, expected in provenance["source_inputs_sha256"].items():
            if file_digest(ROOT / name) != expected:
                raise RuntimeError(f"Source changed since the observed build: {name}")
        for name, expected in provenance["packaged_binaries_sha256"].items():
            if file_digest(ROOT / name) != expected:
                raise RuntimeError(f"Packaged binary differs from observed build: {name}")
    manifest_path = ROOT / "docs/Historical-Source-Manifest.json"
    if not args.skip_history:
        manifest = audit()
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Audited {manifest['commit_count']} commits; {manifest['unique_binary_blobs']} unique binary blobs; all dependency declarations identical", flush=True)
    if args.audit_only:
        return
    output = args.output_directory.resolve()
    output.mkdir(parents=True, exist_ok=True)
    names = [f"HEAVEN-{args.version}-corresponding-source.zip",
             "VocalStack-source-history.zip", f"HEAVEN-{args.version}-Windows-x64.zip"]
    if any((output / name).exists() for name in names):
        raise RuntimeError("Output assets already exist; use a new output directory")
    subprocess.run([sys.executable, str(ROOT / "tools/Bundle-Corresponding-Source.py"),
                    *(["--working-tree"] if args.working_tree else ["--revision", args.revision]),
                    "--output", str(output / names[0])], check=True, cwd=ROOT)
    if not args.skip_history:
        bundle_history(output / names[1], manifest, manifest_path)
    files = {}
    for path in sorted((ROOT / "Windows-x64").rglob("*")):
        if path.is_file():
            relative = path.relative_to(ROOT).as_posix()
            if path.suffix in (".clap", ".vst3"):
                if provenance is not None:
                    if provenance["packaged_binaries_sha256"].get(relative) != file_digest(path):
                        raise RuntimeError(f"Unverified binary: {relative}")
                elif path.read_bytes() != git("show", args.revision + ":" + relative):
                    raise RuntimeError(f"Binary changed: {relative}")
            files[f"HEAVEN-{args.version}/" + relative] = path
    documents = ["LICENSE", "COPYING.AGPLv3", "README.md", "docs/Building.md", "docs/Licensing.md",
                 "docs/Historical-Source.md", "docs/Historical-Source-Manifest.json", "docs/Getting-Started.md",
                 "docs/Windows-Resource-Limits.md"]
    if args.working_tree:
        documents += ["docs/Tuning.md", "docs/UiArchitecture.md", f"docs/Release-{args.version}-Validation.md"]
    for name in documents:
        files[f"HEAVEN-{args.version}/" + name] = ROOT / name
    write_zip(output / names[2], files)
    checksums = []
    for name in (names if not args.skip_history else [names[0], names[2]]):
        path = output / name
        with zipfile.ZipFile(path) as archive:
            if archive.testzip() is not None:
                raise RuntimeError(f"ZIP integrity error: {name}")
        checksum = file_digest(path) + "  " + name + "\n"
        path.with_suffix(path.suffix + ".sha256").write_text(checksum, encoding="ascii")
        checksums.append(checksum)
        print(f"{path.stat().st_size} bytes  {checksum.strip()}", flush=True)
    (output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="ascii")


def bundle_history(output, manifest, manifest_path):
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
        write_zip(output, {
            "VocalStack-history/VocalStack.gitbundle": bundle,
            "VocalStack-history/Historical-Source-Manifest.json": manifest_path,
            "VocalStack-history/README.md": ROOT / "docs/Historical-Source.md",
            "VocalStack-history/LICENSE": ROOT / "LICENSE",
            "VocalStack-history/COPYING.AGPLv3": ROOT / "COPYING.AGPLv3",
        })



if __name__ == "__main__":
    main()
