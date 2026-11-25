"""Bundle pinned source + dependency caches, never compiled output. Python 3.12+."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

from release_fonts import require_oxanium_files

ROOT = Path(__file__).resolve().parents[1]
LEGAL_OVERLAYS = (
    "COPYING.AGPLv3", "README.md", "docs/Building.md", "docs/Licensing.md",
    "Windows-x64/SOURCE.md", "tools/Bundle-Corresponding-Source.py",
    "tools/Bundle-Release-Assets.py", "tools/Refresh-Windows-Package.py",
    "tools/Verify-Release-Assets.py", "tools/Test-Release-Bundles.py", "tools/release_fonts.py",
)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export_git(repo, revision, destination, include_submodules=True):
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(git(repo, "archive", "--format=zip", revision))) as archive:
        archive.extractall(destination)
    if not include_submodules:
        return
    tree = git(repo, "ls-tree", "-r", revision).decode()
    for line in tree.splitlines():
        metadata, path = line.split("\t", 1)
        mode, _, commit = metadata.split()
        if mode == "160000":
            child = repo / path
            if not child.exists():
                raise RuntimeError(f"Missing submodule cache: {child}; initialize recursively first")
            export_git(child, commit, destination / path)


def source_path(name):
    path = Path(name)
    return (name.startswith(("source/", "ThirdParty/", "tools/", "docs/"))
            or name in ("LICENSE", "COPYING.AGPLv3", "README.md", "AGENTS.md", ".gitignore", ".gitattributes")) \
        and not any(part in ("build", "_deps", "__pycache__", "CMakeFiles")
                    or part.startswith("build-") for part in path.parts) \
        and path.suffix.lower() not in (".exe", ".dll", ".clap", ".vst3", ".obj", ".o",
                                       ".lib", ".a", ".pdb", ".pyc", ".zip", ".gitbundle")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--revision", help="Exact binary-producing Git commit")
    selection.add_argument("--working-tree", action="store_true",
                           help="Explicit uncommitted snapshot; base commit is not a producing revision")
    parser.add_argument("--deps-cache", type=Path, default=ROOT / "source/build/_deps")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    revision = git(ROOT, "rev-parse", (args.revision or "HEAD") + "^{commit}").decode().strip()
    # Packaging overlays must not disguise code/configuration changes.
    if not args.working_tree:
        for command in (("diff", revision, "--", "source", "ThirdParty"),
                        ("ls-files", "--others", "--exclude-standard", "source", "ThirdParty")):
            if git(ROOT, *command).strip():
                raise RuntimeError("Source/ThirdParty differs from the binary revision; review before bundling")
    cmake = ((ROOT / "source/CMakeLists.txt").read_text(encoding="utf-8") if args.working_tree
             else git(ROOT, "show", revision + ":source/CMakeLists.txt").decode())
    version_match = re.search(r"project\(VocalStack VERSION ([0-9]+\.[0-9]+\.[0-9]+)(?:\s|\))", cmake)
    if version_match is None:
        raise RuntimeError("Missing numeric source project version")
    archive_root = f"HEAVEN-{version_match.group(1)}"
    declarations = dict(re.findall(r"FetchContent_Declare\(([^\s]+)(.*?)\)" , cmake, re.S))
    manifest = {"binary_source_commit": None if args.working_tree else revision,
                "source_mode": "working-tree-snapshot" if args.working_tree else "git-revision",
                "base_commit": revision, "packaging_overlays": {}, "dependencies": {}}
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise RuntimeError(f"Refusing to overwrite {output}")
    with tempfile.TemporaryDirectory(prefix="vocalstack-source-") as temporary:
        stage = Path(temporary) / archive_root
        stage.mkdir()
        tracked = (git(ROOT, "ls-files", "-z", "--cached", "--others", "--exclude-standard").decode().split("\0")
                   if args.working_tree else git(ROOT, "ls-tree", "-r", "--name-only", revision).decode().splitlines())
        paths = sorted({p for p in tracked if source_path(p)})
        snapshot = {}
        if args.working_tree:
            for name in paths:
                source = ROOT / name
                if not source.exists():  # Respect working-tree deletions.
                    continue
                if source.is_symlink() or not source.is_file():
                    raise RuntimeError(f"Unsupported snapshot input: {name}")
                data = source.read_bytes()
                target = stage / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                snapshot[name] = hashlib.sha256(data).hexdigest()
            manifest["working_tree_files_sha256"] = snapshot
        else:
            with zipfile.ZipFile(io.BytesIO(git(ROOT, "archive", "--format=zip", revision, "--", *paths))) as archive:
                archive.extractall(stage)
        require_oxanium_files(cmake, lambda name: (stage / name).is_file())
        overlays = LEGAL_OVERLAYS
        if args.working_tree and (ROOT / "Windows-x64/BUILD-PROVENANCE.json").is_file():
            overlays += ("Windows-x64/BUILD-PROVENANCE.json",)
        for name in overlays:
            # Working snapshots already contain the current tooling/docs. Revision
            # exports overlay delivery guidance only, never source configuration.
            data = (ROOT / name).read_bytes()
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            manifest["packaging_overlays"][name] = hashlib.sha256(data).hexdigest()
        for name, declaration in declarations.items():
            destination = stage / "external" / name
            url_match = re.search(r"\bGIT_REPOSITORY\s+(\S+)", declaration)
            if url_match:
                url = url_match.group(1)
                commit = re.search(r"\bGIT_TAG\s+(\S+)", declaration).group(1)
                if not re.fullmatch(r"[0-9a-f]{40}", commit):
                    raise RuntimeError(f"Unpinned dependency: {name}")
                repo = args.deps_cache / (name.lower() + "-src")
                include_submodules = not re.search(r'\bGIT_SUBMODULES\s+""', declaration)
                export_git(repo, commit, destination, include_submodules)
                submodules = git(repo, "ls-tree", "-r", commit).decode().splitlines()
                manifest["dependencies"][name] = {"url": url, "commit": commit,
                    "submodules_enabled": include_submodules,
                    "submodule_gitlinks": [line for line in submodules if line.startswith("160000 ")]}
            else:
                url = re.search(r"\bURL\s+(\S+)", declaration).group(1)
                digest = re.search(r"URL_HASH SHA256=([0-9a-f]{64})", declaration).group(1)
                download = Path(temporary) / (name + ".tar.gz")
                print(f"Downloading verified source: {url}", flush=True)
                urllib.request.urlretrieve(url, download)
                if sha256(download) != digest:
                    raise RuntimeError(f"Archive hash mismatch: {name}")
                unpacked = Path(temporary) / (name + "-unpacked")
                with tarfile.open(download) as archive:
                    archive.extractall(unpacked, filter="data")
                roots = list(unpacked.iterdir())
                if len(roots) != 1 or not roots[0].is_dir():
                    raise RuntimeError(f"Unexpected source archive layout: {name}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                roots[0].rename(destination)
                manifest["dependencies"][name] = {"url": url, "sha256": digest}
        if args.working_tree:
            current = set(git(ROOT, "ls-files", "-z", "--cached", "--others", "--exclude-standard").decode().split("\0"))
            current = {p for p in current if source_path(p) and (ROOT / p).exists()}
            if current != set(snapshot) or any(sha256(ROOT / p) != h for p, h in snapshot.items()):
                raise RuntimeError("Working tree changed during source export; wait for edits to finish")
        if any(sha256(ROOT / name) != checksum for name, checksum in manifest["packaging_overlays"].items()):
            raise RuntimeError("Packaging guidance/provenance changed during source export")
        for required in ("LICENSE", "COPYING.AGPLv3", "ThirdParty/PixelifySans-OFL.txt",
                         "ThirdParty/JUCE.md", "ThirdParty/CLAP.txt",
                         "ThirdParty/CLAP-Helpers.txt", "ThirdParty/CLAP-JUCE-Extensions.txt",
                         "ThirdParty/VST3-SDK.txt", "ThirdParty/Signalsmith-Linear.txt",
                         "ThirdParty/Signalsmith-Stretch.txt", "ThirdParty/Vital/LICENSE"):
            if not (stage / required).is_file():
                raise RuntimeError(f"Missing corresponding-source notice: {required}")
        manifest["files_sha256"] = {
            p.relative_to(stage).as_posix(): sha256(p)
            for p in sorted(stage.rglob("*")) if p.is_file()
        }
        (stage / "SOURCE-MANIFEST.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        partial = output.with_suffix(output.suffix + ".partial")
        try:
            with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        info = zipfile.ZipInfo(archive_root + "/" + path.relative_to(stage).as_posix())
                        info.compress_type = zipfile.ZIP_DEFLATED
                        info.external_attr = 0o100644 << 16
                        archive.writestr(info, path.read_bytes())
            partial.rename(output)
        finally:
            partial.unlink(missing_ok=True)
    digest = sha256(output)
    output.with_suffix(output.suffix + ".sha256").write_text(
        digest + "  " + output.name + "\n", encoding="ascii")
    print(f"{digest}  {output}")


if __name__ == "__main__":
    main()
