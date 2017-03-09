#!/usr/bin/env python2
"""
IDL Unit Test runner

Generates a file called results.xml in the XUnit format.
"""

import sys
import unittest
from xmlrunner import XMLTestRunner

if __name__ == '__main__':

    all_tests = unittest.defaultTestLoader.discover(start_dir="tests")

    with open("results.xml", "wb") as output:

        runner = XMLTestRunner(verbosity=2, failfast=False, output=output)
        result = runner.run(all_tests)

    sys.exit(not result.wasSuccessful())
