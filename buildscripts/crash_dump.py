#!/usr/bin/env python3 
import logging
import argparse
import re
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import yaml

import evergreen_client

def _evergreen_config_default_file():
    return os.path.expanduser("~/.evergreen.yml")

def _read_config(file=None):
    if file is None:
        file = _evergreen_config_default_file()

    with open(file, "rb") as sjh:
        contents = sjh.read().decode('utf-8')
        config = yaml.safe_load(contents)

    user = config['user']
    api_key = config['api_key']
    return user, api_key

class TaskInfoModel(object):

    def __init__(self, json):
        self.display_name = json['display_name']
        self.revision = json['revision']
        # Array of {id, status}
        self.depends_on = json['depends_on']

        # Array of {name, url}
        self.files = json["files"]


def get_url(list, name):
    for item in list:
        if name in item['name']:
            return item['url']
    return None

def get_task_id_from_url(url):

    if re.search("/\d$", url):
        url = re.sub("\/\d$", "", url)

    task_id = url.split('/')[-1]

    return task_id


def main():
    # type: () -> None
    """Main entry point."""

    parser = argparse.ArgumentParser(description='PyLinter frontend.')

    parser.add_argument('-v', "--verbose", action='store_true', help="Enable verbose logging")

    parser.add_argument('url', type=str, help="Failing Task URL to grab")

    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    task_id = get_task_id_from_url(args.url)
    print("Retrieving dumps for task: " + task_id)

    (user, api_key) = _read_config()
    evergreen = evergreen_client.EvergreenClient(user, api_key, "mongodb_mongo_master")

    task_json = evergreen.get_info_for_task(task_id)

    task_info = TaskInfoModel(task_json)

    core_dump_url = get_url(task_info.files, "Core Dumps")
    if not core_dump_url:
        print("No Core Dumps found")
        sys.exit(1)

    depends_dict = task_info.depends_on[0]

    if('compile' not in depends_dict['id']):
        # Recurse

        nested_task_id = depends_dict['id']

        nested_task_json = evergreen.get_info_for_task(nested_task_id)

        task_info = TaskInfoModel(nested_task_json)
        depends_dict = task_info.depends_on[0]
        

    assert "compile" in depends_dict['id']

    compile_task_id = depends_dict['id']

    compile_task_json = evergreen.get_info_for_task(compile_task_id)

    compile_task_info = TaskInfoModel(compile_task_json)

    debugsymbols_url = get_url(compile_task_info.files, "debugsymbols")
    if not debugsymbols_url:
        print("No Debug Symbols found")
        sys.exit(1)

    binaries_url = get_url(compile_task_info.files, "Binaries")
    if not binaries_url:
        print("No Binary found")
        sys.exit(1)


    DUMP_DIR = 'dump'

    # local_dumps = os.path.join(DUMP_DIR, "dumps.tgz")
    # local_symbols  = os.path.join(DUMP_DIR, "symbols.tgz")
    # local_binaries  = os.path.join(DUMP_DIR, "binaries.tgz")

    if os.path.exists(DUMP_DIR):
        shutil.rmtree(DUMP_DIR)
    
    os.mkdir(DUMP_DIR)
    os.chdir(DUMP_DIR)

    local_dumps = "dumps.tgz"
    local_symbols  = "symbols.tgz"
    local_binaries  = "binaries.tgz"

    logging.info("Downloading files")
    urllib.request.urlretrieve(core_dump_url, local_dumps)
    urllib.request.urlretrieve(debugsymbols_url, local_symbols)
    urllib.request.urlretrieve(binaries_url, local_binaries)

    logging.info("Untaring Files")
    subprocess.check_call(['tar', '-zxvf', '%s' % (local_dumps)])
    subprocess.check_call(['tar', '-zxvf', '%s' % (local_symbols)])
    subprocess.check_call(['tar', '-zxvf', '%s' % (local_binaries)])

    logging.info("Renaming Dirs")
    os.system('mv mongodb-* mongodb')
    os.system('mv mongodb/*.debug mongodb/bin')

    logging.info("Analyzing Dumps...")
    sym_tar = tarfile.open(local_dumps)

    dc = 1
    for name in sym_tar.getnames():
        print(" %d: %s" % (dc, name))
        dc += 1

    for name in sym_tar.getnames():
        print('/opt/mongodbtoolchain/gdb/bin/gdb %s %s' % ('mongodb/bin/mongod', name) )
        os.system('/opt/mongodbtoolchain/gdb/bin/gdb %s %s' % ('mongodb/bin/mongod', name) )


if __name__ == "__main__":
    main()