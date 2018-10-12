import logging

import requests


class EvergreenClient(object):  # pragma: no cover
    """Class for querying Evergreen.

    Attributes:
        host_url: the base Evergreen url.
        headers: the authentication headers needed to query protected Evergreen resources.
        project: the name of the Evergreen project.
    """

    def __init__(self, username, api_key, project):
        """
        Args:
            username: the username of the Evergreen user to be used for authentication.
            api_key: the Evergreen api key to be used for authentication.
            project: the name of the Evergreen project.
        """
        self.host_url = "https://evergreen.mongodb.com"
        self.headers = {
            "Auth-Username": username,
            "Api-Key": api_key
        }

        self.project = project

    def _build_url(self, path):
        return self.host_url + path

    def get_request(self, url, params=None):
        """Performs a GET request at the specified path.

        Args:
            url: the full url to be queried.
            params: (optional) a dict of params to be passed along with the request.

        Returns:
            The raw text returned by Evergreen or None if the request could not be completed.

        Raises:
            EvergreenLoginException: If auth failed and request is redirected.
        """
        response = requests.get(url, headers=self.headers, params=params, stream=False)

        if response.status_code != 200:
            logging.error("Evergreen returned status code %d: %s. URL = %s" %
                          (response.status_code, response.text, url))
            return None
        elif response.url.startswith(self.host_url + "login"):
            logging.error("Evergreen redirected request to login page. "
                          "Please double check auth info.")
            raise EvergreenLoginException("Evergreen requested login.")

        return response

    def get_request_text(self, path, params=None):
        response = self.get_request(path, params)
        if response:
            return response.text

    def get_request_json(self, path, params=None):
        response = self.get_request(path, params)
        if response:
            return response.json()

    def get_patch_diff(self, version):
        """Queries Evergreen for the raw diff of the given patch.

        Args:
            version: the patch's version.

        Returns:
            The json of the git diff or None if the request could not be completed.
        """
        version = version.split("_")[0]

        path = "/rest/v1/patches/%s" % version
        return self.get_request_json(self._build_url(path))

    def get_data_for_revision(self, revision):
        """Queries Evergreen for metadata about the given revision's build.

        Args:
            revision: the git hash of the revision.

        Returns:
            The json returned by Evergreen (a dict)
            or None if the request could not be completed.
        """
        path = "/rest/v1/projects/%s/revisions/%s" % (self.project, revision)
        return self.get_request_json(self._build_url(path))

    def get_patches(self, end_date):
        """Queries Evergreen for patches that ran before end_date.

        Args:
            end_date: a datetime object.

        Returns:
            The json returned by Evergreen (an array of dicts)
            or None if the request could not be completed.
        """

        path = "/rest/v2/projects/%s/patches" % self.project
        params = {
            "start_at": "\"%s\"" % end_date.strftime("%Y-%m-%dT%H:%M:%S.000Z")
        }
        return self.get_request_json(self._build_url(path), params)

    def get_task_ids_in_version(self, version):
        """Queries Evergreen for data about which tasks ran in the given version.

        Args:
            version: the version string.

        Returns:
            The json returned by Evergreen (a dict)
            or None if the request could not be completed.
        """
        path = "/rest/v1/versions/%s/status" % version
        params = {
            "groupby": "builds"
        }
        return self.get_request_json(self._build_url(path), params)

    def get_info_for_task(self, task_id):
        """Queries Evergreen for data about the given task.

        Args:
            task_id: the task_id of the task.

        Returns:
            The json returned by Evergreen (a dict)
            or None if the request could not be completed.
        """
        path = "/rest/v1/tasks/%s" % task_id
        return self.get_request_json(self._build_url(path))

    def get_test_history(self, after_revision, before_revision, tasks):
        """Queries Evergreen for test history.

        Args:
            after_revision: the beginning revision cutoff for test results (exclusive).
            before_revision: the end revision cutoff for test results (inclusive).
            tasks: a list of tasks to get test history for.

        Returns:
            A list of test units.
            For example:
            [{
                "revision": "...",
                "test_file": "src/...",
                "task_name": "auth",
                "variant": "enterprise-rhel-62-64-bit",
                "test_status": "pass"
            }]
        """
        path = "/rest/v1/projects/%s/test_history" % self.project

        params = {
            "afterRevision": after_revision,
            "beforeRevision": before_revision,
            "taskStatuses": "failed,timeout,success,sysfail",
            "testStatuses": "pass,fail",
            "tasks": ",".join(tasks)
        }

        return self.get_request_json(self._build_url(path), params)


class EvergreenLoginException(Exception):
    pass
