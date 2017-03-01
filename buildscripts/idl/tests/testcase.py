from __future__ import absolute_import, print_function, unicode_literals

import unittest

#print("Name:" + str(__name__))
#print("Name:" + str(__package__))
# import package so that it works regardless of whether we run as a module or file
if __name__ == 'testcase':
    import sys
    from os import path
    sys.path.append(path.dirname(path.dirname(path.abspath(__file__))))
    from context import idl
else:
    from .context import idl


def errors_to_str(errors):
    # type: (idl.errors.ParserErrorCollection) -> unicode
    """Dump the list of errors as a multiline text string."""
    if errors is not None:
        return "\n".join(errors.to_list())
    return "<empty>"


class IDLTestcase(unittest.TestCase):
    def _parse(self, doc_str):
        # type: (unicode) -> idl.syntax.IDLParsedSpec

        try:
            return idl.parser.parse(doc_str)
        except:
            self.fail("Failed to parse document:\n%s" % (doc_str))

    def _assert_parse(self, doc_str, parsed_doc):
        # type: (unicode, idl.syntax.IDLParsedSpec) -> None
        self.assertIsNone(parsed_doc.errors,
                          "Expected no parser errors\nFor document:\n%s\nReceived errors:\n\n%s" %
                          (doc_str, errors_to_str(parsed_doc.errors)))
        self.assertIsNotNone(parsed_doc.spec, "Expected a parsed doc")

    def assert_parse(self, doc_str):
        # type: (unicode) -> None
        parsed_doc = self._parse(doc_str)
        self._assert_parse(doc_str, parsed_doc)

    def assert_parse_fail(self, doc_str, id, multiple=False):
        # type: (unicode, unicode, bool) -> None
        parsed_doc = self._parse(doc_str)

        self.assertIsNone(parsed_doc.spec, "Expected no parsed doc")
        self.assertIsNotNone(parsed_doc.errors, "Expected parser errors")

        # Assert that negative test cases are only testing one fault in a test.
        # This is impossible to assert for all tests though.
        self.assertTrue(
            multiple or parsed_doc.errors.count() == 1,
            "For document:\n%s\nExpected only error message '%s' but received multiple errors:\n\n%s"
            % (doc_str, id, errors_to_str(parsed_doc.errors)))

        self.assertTrue(
            parsed_doc.errors.contains(id),
            "For document:\n%s\nExpected error message '%s' but received only errors:\n %s" %
            (doc_str, id, errors_to_str(parsed_doc.errors)))

    def assert_bind(self, doc_str):
        # type: (unicode) -> idl.ast.IDLBoundSpec
        parsed_doc = self._parse(doc_str)
        self._assert_parse(doc_str, parsed_doc)

        bound_doc = idl.binder.bind(parsed_doc.spec)

        self.assertIsNone(bound_doc.errors,
                          "Expected no binder errors\nFor document:\n%s\nReceived errors:\n\n%s" %
                          (doc_str, errors_to_str(bound_doc.errors)))
        self.assertIsNotNone(bound_doc.spec, "Expected a bound doc")

        return bound_doc.spec

    def assert_bind_fail(self, doc_str, id):
        # type: (unicode, unicode) -> None
        parsed_doc = self._parse(doc_str)
        self._assert_parse(doc_str, parsed_doc)

        bound_doc = idl.binder.bind(parsed_doc.spec)

        self.assertIsNone(bound_doc.spec, "Expected no bound doc\nFor document:\n%s\n" % (doc_str))
        self.assertIsNotNone(bound_doc.errors, "Expected binder errors")

        # Assert that negative test cases are only testing one fault in a test.
        self.assertTrue(
            bound_doc.errors.count() == 1,
            "For document:\n%s\nExpected only error message '%s' but received multiple errors:\n\n%s"
            % (doc_str, id, errors_to_str(bound_doc.errors)))

        self.assertTrue(
            bound_doc.errors.contains(id),
            "For document:\n%s\nExpected error message '%s' but received only errors:\n %s" %
            (doc_str, id, errors_to_str(bound_doc.errors)))
