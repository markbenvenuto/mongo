import unittest
import sys

loader = unittest.defaultTestLoader

all_tests = unittest.defaultTestLoader.discover(start_dir="tests")

from xmlrunner import XMLTestRunner


with open("results.xml", "wb") as output:

    runner = XMLTestRunner(verbosity=2, failfast=False,
                           output=output)
    result = runner.run(all_tests)
sys.exit(not result.wasSuccessful())

