"""Compile only UtilityPanelTests against read-only existing Release libraries.

Run from any directory with Python on Windows after the normal build has supplied
JUCE/processor libraries. Never invokes a shared build or writes packaged plugins.
"""
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
build = root / "source/build"
out = root / ".cache/utility-panel-isolated"
out.mkdir(parents=True, exist_ok=True)
commands = subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "VocalStackEditorTests"], text=True
).splitlines()
compile_command = next(c for c in commands if "EditorTests.cpp.obj" in c and " -c " in c)
compile_command = re.sub(r"/Fo\S+", '/Fo"' + (out / "UtilityPanelTests.obj").as_posix() + '"', compile_command)
compile_command = re.sub(r"/Fd\S+", '/Fd"' + (out / "UtilityPanelTests.pdb").as_posix() + '"', compile_command)
compile_command = compile_command.replace("/showIncludes", "")
compile_command = compile_command[:compile_command.rfind(" -c ")] + ' -c "' + (root / "source/tests/UtilityPanelTests.cpp").as_posix() + '"'
compiler, arguments = compile_command.split("  ", 1)
response = out / "compile.rsp"
response.write_text(arguments)
compile_command = compiler + ' @"' + response.as_posix() + '"'
link = next(c for c in commands if "vs_link_exe" in c and "VocalStackEditorTests.exe" in c)
link = link.split(" -- ", 1)[1].rsplit(" && cd .", 1)[0]
linker = link.split(" /nologo ", 1)[0]
libraries = link[link.index('"VocalStack_artefacts'):].rstrip('"')
# The final quote belongs to cmd /C, not to an individual library path.
link_command = linker + ' /nologo /machine:x64 /subsystem:console /out:"' + (out / "UtilityPanelTests.exe").as_posix() + '" "' + (out / "UtilityPanelTests.obj").as_posix() + '" ' + libraries
vcvars = pathlib.Path("C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
script = out / "build.cmd"
script.write_text('@echo off\ncall "' + str(vcvars) + '"\nif errorlevel 1 exit /b 1\n' + compile_command + '\nif errorlevel 1 exit /b 1\n' + link_command + '\nexit /b %errorlevel%\n')
subprocess.run(["cmd.exe", "/c", str(script)], cwd=build, check=True)
subprocess.run([str(out / "UtilityPanelTests.exe")], cwd=out, check=True, timeout=60)
