#!/bin/sh
# Entry point of the hev-socks5-server test suite.
#
#   tests/run.sh func            functional tests (fast, pass/fail)
#   tests/run.sh bench           benchmarks (report numbers only)
#   tests/run.sh all             both, as two separate sessions
#   tests/run.sh list            list tests
#
# Options: -b BIN  -k NAME (repeatable)  -d SECONDS  -x  -v
# See tests/README.md.

set -e
HERE=$(cd "$(dirname "$0")" && pwd)

# Network namespaces need root.  Build as the calling user first, so bin/
# does not end up owned by root.
if [ "$(id -u)" != 0 ]; then
    case " $* " in
    *" -b "*|*" --bin"*|*" --no-build "*) ;;
    *)
        make -C "$HERE/.." -j"$(nproc 2>/dev/null || echo 2)" >&2 || exit 1
        set -- "$@" --no-build
        ;;
    esac
    exec sudo env "PATH=$PATH" ${PYTHON:+"PYTHON=$PYTHON"} \
        ${BENCH_DURATION:+"BENCH_DURATION=$BENCH_DURATION"} \
        ${GITHUB_STEP_SUMMARY:+"GITHUB_STEP_SUMMARY=$GITHUB_STEP_SUMMARY"} \
        "$0" "$@"
fi

PY=${PYTHON:-python3}
if ! "$PY" -c 'import os,sys; sys.exit(0 if sys.version_info>=(3,12) and hasattr(os,"setns") else 1)' 2>/dev/null; then
    echo "run.sh: Python >= 3.12 is required (set PYTHON=...)" >&2
    exit 2
fi

cd "$HERE/.."
PYTHONPATH="$HERE${PYTHONPATH:+:$PYTHONPATH}" exec "$PY" -m hevtest.main "$@"
