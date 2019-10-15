#!/usr/bin/env python3
"""Simple C++ Linter."""

import argparse
import io
import logging
import re
import sys

# try:
#     import regex as re
# except ImportError:
#     print("*** Run 'pip3 install --user regex' to speed up error code checking")
#     import re  # type: ignore


def _make_polyfill_regex():
    polyfill_required_names = [
        '_',
        'adopt_lock',
        'async',
        'chrono',
        'condition_variable',
        'condition_variable_any',
        'cv_status',
        'defer_lock',
        'future',
        'future_status',
        'get_terminate',
        'launch',
        'lock_guard',
        'mutex',
        'notify_all_at_thread_exit',
        'packaged_task',
        'promise',
        'recursive_mutex',
        'set_terminate',
        'shared_lock',
        'shared_mutex',
        'shared_timed_mutex',
        'this_thread(?!::at_thread_exit)',
        'thread',
        'timed_mutex',
        'try_to_lock',
        'unique_lock',
        'unordered_map',
        'unordered_multimap',
        'unordered_multiset',
        'unordered_set',
    ]

    qualified_names = ['boost::' + name + "\\b" for name in polyfill_required_names]
    qualified_names.extend('std::' + name + "\\b" for name in polyfill_required_names)
    qualified_names_regex = '|'.join(qualified_names)
    return re.compile(qualified_names_regex)


_RE_LINT = re.compile("//.*NOLINT")
_RE_COMMENT_STRIP = re.compile("//.*")

_RE_PATTERN_MONGO_POLYFILL = _make_polyfill_regex()
_RE_VOLATILE = re.compile('[^_]volatile')
_RE_MUTEX = re.compile('[ ({,]stdx?::mutex[ ({]')
_RE_ASSERT = re.compile(r'\bassert\s*\(')


def lint_file(file_name):
    """Lint file and print errors to console."""
    with io.open(file_name, encoding='utf-8') as file_stream:
        raw_lines = file_stream.readlines()

    #linter = Linter(file_name, raw_lines)
    #return linter.lint()


def main():
    # type: () -> None
    """Execute Main Entry point."""
    parser = argparse.ArgumentParser(description='MongoDB Simple C++ Linter.')

    parser.add_argument('file', type=str, help="C++ input file")

    parser.add_argument('-v', '--verbose', action='count', help="Enable verbose tracing")

    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    lint_file(args.file)

    success = True

    if not success:
        sys.exit(1)


if __name__ == '__main__':
    main()
