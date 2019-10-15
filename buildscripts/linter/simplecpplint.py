#!/usr/bin/env python3
#
# Copyright (C) 2019-present MongoDB, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the Server Side Public License, version 1,
# as published by MongoDB, Inc.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# Server Side Public License for more details.
#
# You should have received a copy of the Server Side Public License
# along with this program. If not, see
# <http://www.mongodb.com/licensing/server-side-public-license>.
#
# As a special exception, the copyright holders give permission to link the
# code of portions of this program with the OpenSSL library under certain
# conditions as described in each individual source file and distribute
# linked combinations including the program with the OpenSSL library. You
# must comply with the Server Side Public License in all respects for
# all of the code used other than as permitted herein. If you modify file(s)
# with this exception, you may extend this exception to your version of the
# file(s), but you are not obligated to do so. If you do not wish to do so,
# delete this exception statement from your version. If you delete this
# exception statement from all source files in the program, then also delete
# it in the license file.
#
"""Simple C++ Linter"""

import argparse
import logging
import sys
import io
import re
import textwrap

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
  qualified_names.extend('std::' + name  + "\\b" for name in polyfill_required_names)
  qualified_names_regex = '|'.join(qualified_names)
  return re.compile(qualified_names_regex)

_RE_PATTERN_MONGO_POLYFILL = _make_polyfill_regex()


class Linter:

    def __init__(self, file_name, raw_lines):
        self.file_name = file_name
        self.raw_lines = raw_lines
        self.nolint_supression = []
        self.error_count = 0

    def lint(self):
        # 3 steps:
        # 1. Check for header
        # 2. Check for NOLINT and Strip multi line comments
        # 3. Run per line checks

        if "enterprise" not in self.file_name:
            self.CheckForServerSidePublicLicense(3)

        self._check_and_strip_comments()

        # print("self.nolint_supression: %s" %(self.nolint_supression))

        for linenum in range(len(self.clean_lines)):
            self.CheckForMongoVolatile(linenum)
            self.CheckForMongoPolyfill(linenum)
            self.CheckForMongoAtomic(linenum)
            self.CheckForMongoMutex(linenum)
            self.CheckForNonMongoAssert(linenum)

        return self.error_count

    def _check_and_strip_comments(self):
        self.clean_lines = []
        in_multi_line_comment = False

        # Users can write NOLINT different ways
        # // NOLINT
        # // Some explanation NOLINT
        # so we need a regular expression
        re_nolint = re.compile("//.*NOLINT")
        re_comment_strip = re.compile("//.*")

        for linenum in range(len(self.raw_lines)):
            clean_line = self.raw_lines[linenum]

            if re_nolint.search(clean_line):
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
                    clean_line = re_comment_strip.sub("", clean_line)
            else:
                if "*/" in clean_line:
                    in_multi_line_comment = False
    
                clean_line = ""
                
            self.clean_lines.append(clean_line)


    def CheckForMongoVolatile(self,linenum):
        line = self.clean_lines[linenum]
        if re.search('[^_]volatile', line) and not "__asm__" in line:
            self.error(linenum, 'mongodb/volatile',
                'Illegal use of the volatile storage keyword, use AtomicWord instead '
                'from "mongo/platform/atomic_word.h"')

    def CheckForMongoPolyfill(self, linenum):
        line = self.clean_lines[linenum]
        if re.search(_RE_PATTERN_MONGO_POLYFILL, line):
            self.error(linenum, 'mongodb/polyfill',
                'Illegal use of banned name from std::/boost::, use mongo::stdx:: variant instead')

    def CheckForMongoAtomic(self, linenum):
        line = self.clean_lines[linenum]
        if 'std::atomic' in line:
            self.error(linenum, 'mongodb/stdatomic',
                'Illegal use of prohibited std::atomic<T>, use AtomicWord<T> or other types '
                'from "mongo/platform/atomic_word.h"')

    def CheckForMongoMutex(self, linenum):
        line = self.clean_lines[linenum] 
        if re.search('[ ({,]stdx?::mutex[ ({]', line):
            self.error(linenum, 'mongodb/stdxmutex',
                    'Illegal use of prohibited stdx::mutex, '
                    'use mongo::Mutex from mongo/platform/mutex.h instead.')

    def CheckForNonMongoAssert(self, linenum):
        line = self.clean_lines[linenum]
        if re.search(r'\bassert\s*\(', line):
            self.error(linenum, 'mongodb/assert',
                    'Illegal use of the bare assert function, use a function from assert_utils.h instead.')

    def CheckForServerSidePublicLicense(self, copyright_offset):
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
        for line in range(copyright_offset, min(len(self.raw_lines), copyright_offset + 3)):
            if re.search(r'This program is free software', self.raw_lines[line]):
                license_header_start_line = line
                for i in range(len(license_header)):
                    line = i + license_header_start_line
                    if line >= len(self.raw_lines) or self.raw_lines[line] != license_header[i]:
                        #print("foo: |%s|" % (self.raw_lines[line]))
                        self.error(0, 'legal/license',
                                'Incorrect license header found.  '
                                'Expected "' + license_header[i] + '".  '
                                'See https://github.com/mongodb/mongo/wiki/Server-Code-Style')
                        # We break here to stop reporting legal/license errors for this file.
                        break

                # We break here to indicate that we found some license header.
                break
        else:
          self.error(0, 'legal/license',
                'No license header found.  '
                'See https://github.com/mongodb/mongo/wiki/Server-Code-Style')

    def error(self, linenum, category, message):
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
        self.error_count += 1


def lint_file(file_name):
    with io.open(file_name, encoding='utf-8') as file_stream:
        raw_lines = file_stream.readlines()

    # Strip trailing '\n'
    raw_lines = [line.rstrip() for line in raw_lines]

    # print(raw_lines)

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
