"""Compile focused production-code regressions against lightweight game-data fakes.

Run from a VS Developer Command Prompt (cl), or with g++ on PATH:
    python tests/playerbot_logic_test.py

This exercises the actual quest-chain functions, reward-spell block and class
config loop, extracted verbatim so the harness cannot test a stale reimplementation.
It is not a full worldserver integration test and needs no client data or database.
"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source_slice(path, start, end):
    text = (ROOT / path).read_text(encoding="utf-8")
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def run_cpp(test, harness, sources=(), includes=(), headers=None, defines=(), gcc_flags=(), standard=17):
    compiler = shutil.which("cl") if os.name == "nt" else shutil.which("g++")
    cmake = shutil.which("cmake") if os.name == "nt" else None
    if not compiler and not cmake:
        test.skipTest("Requires MSVC (or CMake + VS 2022 on Windows), or g++")

    # Stay out of tracked sources and keep MSVC's .obj/.pdb files in the temp dir.
    build = ROOT / "build/playerbot-tests"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as temporary:
        directory = Path(temporary)
        source = directory / "regressions.cpp"
        executable = directory / ("regressions.exe" if os.name == "nt" else "regressions")
        source.write_text(harness, encoding="utf-8")
        include_paths = [str(ROOT / path) for path in includes]
        if headers:
            shims = directory / "include"
            shims.mkdir()
            for name, content in headers.items():
                (shims / name).write_text(content, encoding="utf-8")
            include_paths.insert(0, str(shims))
        source_paths = [str(source), *(str(ROOT / path) for path in sources)]
        if not compiler:
            # CMake can find MSVC on CI even outside a Developer Command Prompt.
            def quoted(value):
                return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'
            project = (
                'cmake_minimum_required(VERSION 3.18)\n'
                'project(PlayerbotRegressions LANGUAGES CXX)\n'
                f'set(CMAKE_CXX_STANDARD {standard})\n'
                'set(CMAKE_CXX_STANDARD_REQUIRED ON)\n'
                'add_executable(regressions ' + ' '.join(map(quoted, source_paths)) + ')\n')
            if include_paths:
                project += 'target_include_directories(regressions PRIVATE ' + ' '.join(map(quoted, include_paths)) + ')\n'
            if defines:
                project += 'target_compile_definitions(regressions PRIVATE ' + ' '.join(defines) + ')\n'
            (directory / "CMakeLists.txt").write_text(project, encoding="utf-8")
            commands = [
                [cmake, "-S", str(directory), "-B", str(directory / "obj"),
                 "-G", "Visual Studio 17 2022", "-A", "x64", "-T", "host=x64",
                 f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG={directory}"],
                [cmake, "--build", str(directory / "obj"), "--config", "Debug"],
            ]
        elif os.name == "nt":
            commands = [[compiler, "/nologo", f"/std:c++{standard}", "/EHsc", "/W4", *source_paths,
                         *(f"/I{path}" for path in include_paths), *(f"/D{define}" for define in defines), f"/Fe:{executable}"]]
        else:
            commands = [[compiler, f"-std=c++{standard}", "-Wall", "-Wextra", "-Werror",
                         "-D_GLIBCXX_DEBUG", "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                         *gcc_flags, *source_paths, *(f"-I{path}" for path in include_paths),
                         *(f"-D{define}" for define in defines), "-o", str(executable)]]
        for command in commands:
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=120)
            test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=30)
        test.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class PlayerbotLogicTest(unittest.TestCase):
    def test_focused_cpp_regressions(self):
        harness = Path(__file__).with_name("playerbot_logic_harness.cpp.in").read_text(encoding="utf-8")
        harness = harness.replace("@QUEST_INIT@", source_slice(
            "src/plugins/playerbot/PlayerbotFactory.cpp", "static void AddQuestChain(",
            "void PlayerbotFactory::ClearInventory()"))
        harness = harness.replace("@REWARD_SPELLS@", source_slice(
            "src/server/game/Entities/Player/Player.cpp",
            "    // cast spells after mark quest complete", "    if (quest->GetZoneOrSort() > 0)"))
        harness = harness.replace("@SPEC_PROBABILITIES@", source_slice(
            "src/plugins/playerbot/PlayerbotAIConfig.cpp",
            "    for (uint32 cls = 0; cls < MAX_CLASSES; ++cls)", "    randomBotAccountPrefix ="))
        shared = (ROOT / "src/server/game/Miscellaneous/SharedDefines.h").read_text(encoding="utf-8")
        classes = re.search(r"enum Classes : uint8\s*\{.*?\};", shared, re.DOTALL).group()
        maximum = re.search(r"#define MAX_CLASSES\s+\d+", shared).group()
        harness = harness.replace("@CLASSES@", classes + "\n" + maximum)

        run_cpp(self, harness)


if __name__ == "__main__":
    unittest.main()
