"""Build current tuning UI/backend objects without changing CMake or shared artifacts.

Windows: run this script through tools/Invoke-HalfCpu.ps1 in a dedicated
powershell.exe process. Requires an existing Release build with JUCE libraries.
Native fixtures enumerate displays and use GuiTestDisplay; PNGs are offscreen.
"""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
build = root / "source/build"
out = root / ".cache/tuning-ui-isolated"
out.mkdir(parents=True, exist_ok=True)
if not os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL"):
    raise SystemExit("Run through tools/Invoke-HalfCpu.ps1; refusing unrestricted build/test")
cache = (build / "CMakeCache.txt").read_text()
ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.M)[1]
compiler = Path(re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", cache, re.M)[1])
vcvars = compiler.parents[6] / "Auxiliary/Build/vcvars64.bat"
commands = subprocess.check_output([ninja, "-C", str(build), "-t", "commands", "VocalStackEditorTests"], text=True).splitlines()
template = next(c for c in commands if "EditorTests.cpp.obj" in c and " -c " in c)
compile_steps = []
objects = []
for source in ("tests/TuningUiTests.cpp", "src/PluginEditor.cpp", "src/PluginProcessor.cpp", "src/VocalEngine.cpp"):
    stem = Path(source).stem
    obj = out / (stem + ".obj")
    objects.append(obj)
    command = re.sub(r"/Fo\S+", '/Fo"' + obj.as_posix() + '"', template)
    command = re.sub(r"/Fd\S+", '/Fd"' + (out / (stem + ".pdb")).as_posix() + '"', command)
    command = command.replace("/showIncludes", "")
    command = command[:command.rfind(" -c ")] + ' -c "' + (root / "source" / source).as_posix() + '"'
    executable, arguments = command.split("  ", 1)
    response = out / (stem + ".rsp")
    response.write_text(arguments)
    compile_steps.extend([executable + ' @"' + response.as_posix() + '"', "if errorlevel 1 exit /b 1"])
link = next(c for c in commands if "vs_link_exe" in c and "VocalStackEditorTests.exe" in c)
link = link.split(" -- ", 1)[1].rsplit(" && cd .", 1)[0]
linker = link.split(" /nologo ", 1)[0]
libraries = link[link.index('"VocalStack_artefacts'):].rstrip('"')
exe = out / "TuningUiTests.exe"
link_command = linker + ' /nologo /machine:x64 /subsystem:console /out:"' + exe.as_posix() + '" '
link_command += " ".join('"' + obj.as_posix() + '"' for obj in objects) + " " + libraries
script = out / "build.cmd"
script.write_text('@echo off\ncall "' + str(vcvars) + '"\nif errorlevel 1 exit /b 1\n' + "\n".join(compile_steps) + "\n" + link_command + "\nexit /b %errorlevel%\n")
subprocess.run(["cmd.exe", "/c", str(script)], cwd=build, check=True)
subprocess.run([str(exe)], cwd=out, check=True, timeout=120)
