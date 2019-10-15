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


class Linter:
    """Simple C++ Linter."""

    def __init__(self, file_name, raw_lines):
        """Create new linter."""
        self.file_name = file_name
        self.raw_lines = raw_lines
        self.clean_lines = []
        self.nolint_supression = []
        self._error_count = 0

    def lint(self):
        """Run linter."""
        # 3 steps:
        # 1. Check for header
        # 2. Check for NOLINT and Strip multi line comments
        # 3. Run per line checks

        # The license header is 28 lines and we are 0 indexed unless the file does not have the correct header
        start_line = 27
        if "enterprise" not in self.file_name:
            if not self._check_for_server_side_public_license(3):
                start_line = 0

        self._check_and_strip_comments()

        for linenum in range(start_line, len(self.clean_lines)):
            if not self.clean_lines[linenum]:
                continue

            self._check_for_mongo_volatile(linenum)
            self._check_for_mongo_polyfill(linenum)
            self._check_for_mongo_atomic(linenum)
            self._check_for_mongo_mutex(linenum)
            self._check_for_nonmongo_assert(linenum)

        return self._error_count

    def _check_and_strip_comments(self):
        in_multi_line_comment = False

        for linenum in range(len(self.raw_lines)):
            clean_line = self.raw_lines[linenum]

            # Users can write NOLINT different ways
            # // NOLINT
            # // Some explanation NOLINT
            # so we need a regular expression
            if _RE_LINT.search(clean_line):
                self.nolint_supression.append(linenum)

            if not in_multi_line_comment:
                if "/*" in clean_line and not "*/" in clean_line:
                    in_multi_line_comment = True
                    clean_line = ""

                # Trim comments - approximately
                # Note, this does not understand if // is in a string
                # i.e. it will think URLs are also comments but this should be good enough to find
                # violators of the coding convention
                if "//" in clean_line:
                    clean_line = _RE_COMMENT_STRIP.sub("", clean_line)
            else:
                if "*/" in clean_line:
                    in_multi_line_comment = False

                clean_line = ""

            self.clean_lines.append(clean_line)

    def _check_for_mongo_volatile(self, linenum):
        line = self.clean_lines[linenum]
        if _RE_VOLATILE.search(line) and not "__asm__" in line:
            self._error(
                linenum, 'mongodb/volatile',
                'Illegal use of the volatile storage keyword, use AtomicWord instead '
                'from "mongo/platform/atomic_word.h"')

    def _check_for_mongo_polyfill(self, linenum):
        line = self.clean_lines[linenum]
        if _RE_PATTERN_MONGO_POLYFILL.search(line):
            self._error(
                linenum, 'mongodb/polyfill',
                'Illegal use of banned name from std::/boost::, use mongo::stdx:: variant instead')

    def _check_for_mongo_atomic(self, linenum):
        line = self.clean_lines[linenum]
        if 'std::atomic' in line:
            self._error(
                linenum, 'mongodb/stdatomic',
                'Illegal use of prohibited std::atomic<T>, use AtomicWord<T> or other types '
                'from "mongo/platform/atomic_word.h"')

    def _check_for_mongo_mutex(self, linenum):
        line = self.clean_lines[linenum]
        if _RE_MUTEX.search(line):
            self._error(
                linenum, 'mongodb/stdxmutex', 'Illegal use of prohibited stdx::mutex, '
                'use mongo::Mutex from mongo/platform/mutex.h instead.')

    def _check_for_nonmongo_assert(self, linenum):
        line = self.clean_lines[linenum]
        if _RE_ASSERT.search(line):
            self._error(
                linenum, 'mongodb/assert',
                'Illegal use of the bare assert function, use a function from assert_utils.h instead.'
            )

    def _check_for_server_side_public_license(self, copyright_offset):
        license_header = '''\
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */'''.splitlines()

        # We expect the first line of the license header to follow shortly after the
        # "Copyright" message.
        if r'This program is free software' in self.raw_lines[copyright_offset]:
            license_header_start_line = copyright_offset
            for i in range(len(license_header)):  # pylint: disable=consider-using-enumerate
                line = i + license_header_start_line
                # We don't trim the lines so we check length and then do strncmp
                if line >= len(self.raw_lines) or \
                    len(self.raw_lines[line]) != len(license_header[i]) + 1 or \
                     self.raw_lines[line][:len(license_header[i])] != license_header[i]:
                    self._error(
                        0, 'legal/license', 'Incorrect license header found.  '
                        'Expected "' + license_header[i] + '".  '
                        'See https://github.com/mongodb/mongo/wiki/Server-Code-Style')
                    # We break here to stop reporting legal/license errors for this file.
                    return False
        else:
            self._error(
                0, 'legal/license', 'No license header found.  '
                'See https://github.com/mongodb/mongo/wiki/Server-Code-Style')
            return False

        return True

    def _error(self, linenum, category, message):
        if linenum in self.nolint_supression:
            return

        if category == "legal/license":
            # The following files are in the src/mongo/ directory but technically belong
            # in src/third_party/ because their copyright does not belong to MongoDB.
            files_to_ignore = set([
                'src/mongo/scripting/mozjs/PosixNSPR.cpp',
                'src/mongo/shell/linenoise.cpp',
                'src/mongo/shell/linenoise.h',
                'src/mongo/shell/mk_wcwidth.cpp',
                'src/mongo/shell/mk_wcwidth.h',
                'src/mongo/util/md5.cpp',
                'src/mongo/util/md5.h',
                'src/mongo/util/md5main.cpp',
                'src/mongo/util/net/ssl_stream.cpp',
                'src/mongo/util/scopeguard.h',
            ])

            for file_to_ignore in files_to_ignore:
                if file_to_ignore in self.file_name:
                    return

        # We count internally from 0 but users count from 1 for line numbers
        print("Error: %s:%d - %s - %s" % (self.file_name, linenum + 1, category, message))
        self._error_count += 1


def lint_file(file_name):
    """Lint file and print errors to console."""
    with io.open(file_name, encoding='utf-8') as file_stream:
        raw_lines = file_stream.readlines()

    linter = Linter(file_name, raw_lines)
    return linter.lint()


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
