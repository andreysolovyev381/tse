import argparse
import ast
import difflib
import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WINDOWS = os.name == "nt"
EXAMPLE_DIR = re.compile(r"^(\d\d) ")
THIRD_PARTY = ("numpy", "xgboost")
PIP_HINT = "pip install numpy xgboost"


class Run:
    def __init__(self, status, code, seconds, stdout, stderr, note):
        self.status = status
        self.code = code
        self.seconds = seconds
        self.stdout = stdout
        self.stderr = stderr
        self.note = note


class Log:
    def __init__(self, directory):
        self.directory = directory
        if directory is not None:
            directory.mkdir(parents=True, exist_ok=True)

    def write(self, name, content):
        if self.directory is not None:
            (self.directory / name).write_text(content, encoding="utf-8", errors="replace")

    def where(self, name):
        if self.directory is None:
            return "rerun with --log-dir to keep " + name
        return "see " + str(self.directory / name)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build the C++ examples with CMake, then run every example in Python and in C++ "
                    "and compare the two outputs.")
    parser.add_argument("numbers", nargs="*", metavar="NN",
                        help="two-digit numbers of the examples to run; every example when none is given")
    parser.add_argument("--build-dir",
                        help="CMake build directory; build/ at the root of this repository when omitted")
    parser.add_argument("--jobs", type=int,
                        help="parallel build jobs; CMake decides when omitted")
    parser.add_argument("--timeout", type=float,
                        help="limit for a single run, in seconds; no limit when omitted")
    parser.add_argument("--log-dir",
                        help="directory that keeps the output of every run, the build logs and the "
                             "differences; only the summary is printed when omitted")
    return parser.parse_args()


def discover(numbers):
    found = {}
    for entry in sorted(ROOT.iterdir()):
        match = EXAMPLE_DIR.match(entry.name)
        if match and entry.is_dir():
            found[match.group(1)] = entry
    if not numbers:
        return list(found.values())
    unknown = [number for number in numbers if number not in found]
    if unknown:
        sys.exit("unknown example number(s): " + ", ".join(unknown))
    return [found[number] for number in sorted(set(numbers))]


def text(data):
    if not data:
        return ""
    return data.decode("utf-8", errors="replace")


def uses_xgboost(source):
    return "<xgboost/" in source.read_text(encoding="utf-8", errors="replace")


def missing_modules(script):
    tree = ast.parse(script.read_text(encoding="utf-8"))
    imported = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
            imported.add(node.module.split(".")[0])
    return [name for name in THIRD_PARTY if name in imported and importlib.util.find_spec(name) is None]


def step(command, env, log, name):
    try:
        done = subprocess.run(command, cwd=str(ROOT), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as error:
        log.write(name, " ".join(command) + "\n\n" + str(error) + "\n")
        return False
    log.write(name, " ".join(command) + "\n\n" + text(done.stdout))
    return done.returncode == 0


def build(build_dir, jobs, targets, xgboost_targets, env, log):
    configure = ["cmake", "-S", str(ROOT), "-B", str(build_dir)]
    if WINDOWS:
        configure += ["-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++"]
    parallel = [] if jobs is None else ["-j", str(jobs)]
    xgboost = step(configure + ["-DTSE_EXAMPLES_XGBOOST=ON"], env, log, "configure.log")
    configured = xgboost
    if xgboost and xgboost_targets:
        xgboost = step(["cmake", "--build", str(build_dir), "--target", "xgboost"] + parallel,
                       env, log, "build-xgboost.log")
    if not xgboost:
        configured = step(configure + ["-DTSE_EXAMPLES_XGBOOST=OFF"], env, log, "configure-without-xgboost.log")
    if not configured:
        return False, True
    command = ["cmake", "--build", str(build_dir)] + parallel
    if targets is not None:
        wanted = [target for target in targets if xgboost or target not in xgboost_targets]
        if not wanted:
            return True, xgboost
        command += ["--target"] + wanted
    return step(command, env, log, "build.log"), xgboost


def execute(command, env, timeout):
    start = time.monotonic()
    try:
        done = subprocess.run(command, cwd=str(ROOT), env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout)
    except subprocess.TimeoutExpired as expired:
        return Run("TIMEOUT", None, time.monotonic() - start, text(expired.stdout), text(expired.stderr), "")
    except OSError as error:
        return Run("FAIL", None, time.monotonic() - start, "", str(error), "")
    status = "PASS" if done.returncode == 0 else "FAIL"
    return Run(status, done.returncode, time.monotonic() - start, text(done.stdout), text(done.stderr), "")


def run_python(script, env, timeout):
    missing = missing_modules(script)
    if missing:
        return Run("SKIP", None, 0.0, "", "", "no " + ", ".join(missing) + "; " + PIP_HINT)
    return execute([sys.executable, str(script)], env, timeout)


def run_cpp(source, build_dir, env, timeout, xgboost):
    executable = build_dir / (source.stem + (".exe" if WINDOWS else ""))
    if not executable.is_file():
        if not xgboost and uses_xgboost(source):
            return Run("SKIP", None, 0.0, "", "", "XGBoost disabled")
        return Run("FAIL", None, 0.0, "", "", "not built")
    return execute([str(executable)], env, timeout)


def compare(python_run, cpp_run):
    if python_run.status != "PASS" or cpp_run.status != "PASS":
        return "-", ""
    left = python_run.stdout.replace("\r\n", "\n").splitlines(keepends=True)
    right = cpp_run.stdout.replace("\r\n", "\n").splitlines(keepends=True)
    if left == right:
        return "SAME", ""
    return "DIFF", "".join(difflib.unified_diff(left, right, "python", "c++"))


def last_line(content):
    lines = [line.strip() for line in content.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def detail(run):
    if run.note:
        return run.note
    if run.status == "PASS":
        return last_line(run.stdout)
    exit_text = "" if run.code is None else "exit {}: ".format(run.code)
    return exit_text + (last_line(run.stderr) or last_line(run.stdout))


def report(surface, run):
    print("    {:<7} {:<8} {:>7.1f}s  {}".format(surface, run.status, run.seconds, detail(run)), flush=True)


def main():
    args = parse_args()
    sdk_dir = Path(os.environ.get("TSE_SDK_DIR", str(ROOT / "sdk"))).resolve()
    data_dir = Path(os.environ.get("TSE_DATA_DIR", str(ROOT / "data"))).resolve()
    build_dir = Path(args.build_dir).resolve() if args.build_dir else ROOT / "build"
    log = Log(Path(args.log_dir).resolve() if args.log_dir else None)
    examples = discover(args.numbers)

    env = dict(os.environ)
    env["TSE_SDK_DIR"] = sdk_dir.as_posix()
    env["TSE_DATA_DIR"] = data_dir.as_posix()
    cpp_env = dict(env)
    if WINDOWS:
        cpp_env["PATH"] = env.get("PATH", "") + os.pathsep + str(sdk_dir / "lib")

    sources = [source for example in examples for source in sorted((example / "cpp").glob("*.cpp"))]
    xgboost_targets = [source.stem for source in sources if uses_xgboost(source)]
    targets = [source.stem for source in sources] if args.numbers else None
    built, xgboost = build(build_dir, args.jobs, targets, xgboost_targets, env, log)
    if built:
        print("build: OK" + ("" if xgboost or not xgboost_targets else " (XGBoost disabled)"), flush=True)
    else:
        print("build: FAILED (" + log.where("build.log") + ")", flush=True)

    counts = {"PASS": 0, "FAIL": 0, "TIMEOUT": 0, "SKIP": 0, "SAME": 0, "DIFF": 0}
    rows = ["number\tdirectory\tsurface\tstatus\texit\tseconds"]
    for example in examples:
        number = EXAMPLE_DIR.match(example.name).group(1)
        print(example.name, flush=True)
        runs = []
        for script in sorted((example / "python").glob("*.py")):
            runs.append(("python", script, run_python(script, env, args.timeout)))
            report("python", runs[-1][2])
        for source in sorted((example / "cpp").glob("*.cpp")):
            runs.append(("c++", source, run_cpp(source, build_dir, cpp_env, args.timeout, xgboost)))
            report("c++", runs[-1][2])
        for surface, path, run in runs:
            counts[run.status] += 1
            stem = "{}-{}-{}".format(number, "cpp" if surface == "c++" else surface, path.stem)
            log.write(stem + ".stdout", run.stdout)
            log.write(stem + ".stderr", run.stderr)
            code = "" if run.code is None else str(run.code)
            rows.append("\t".join([number, example.name, surface, run.status, code, "{:.1f}".format(run.seconds)]))
        python_runs = [run for surface, _, run in runs if surface == "python"]
        cpp_runs = [run for surface, _, run in runs if surface == "c++"]
        outcome = "-"
        if len(python_runs) == 1 and len(cpp_runs) == 1:
            outcome, difference = compare(python_runs[0], cpp_runs[0])
            if outcome == "DIFF":
                log.write(number + ".diff", difference)
            if outcome in counts:
                counts[outcome] += 1
        print("    {:<7} {}".format("output", outcome), flush=True)
        rows.append("\t".join([number, example.name, "output", outcome, "", ""]))

    log.write("summary.tsv", "\n".join(rows) + "\n")
    print("")
    print("  ".join("{}={}".format(key, value) for key, value in counts.items()), flush=True)
    return 0 if counts["FAIL"] == 0 and counts["TIMEOUT"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
