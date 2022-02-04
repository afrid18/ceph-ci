#!/usr/bin/env python3

import argparse
import logging
import requests
import sys

from pathlib import Path
from urllib.parse import urlparse

log = logging.getLogger(__name__)

SHAMAN_SEARCH_URL = 'https://shaman.ceph.com/api/search'

PROJECT = 'ceph'
DISTRO = 'ubuntu'
RELEASE = 'focal'
ARCH='x86_64'
BRANCH = 'master'
SHA1 = 'latest'
FLAVOR = 'default'
FILENAME = 'cephadm'


def search(*args, **kwargs):
    '''
    Query shaman for a build result
    '''
    resp = requests.get(SHAMAN_SEARCH_URL, params=kwargs)
    resp.raise_for_status()
    return resp

def _get_distros(distro, release, arch=None):
    ret = f'{distro}/{release}'
    if arch:
        ret = f'{ret}/{arch}'
    return ret

def _get_binary_url(host, project, ref, sha1,
                    distro, release, arch, flavor,
                    filename):
    base_url = f'https://{host}/binaries'
    bin_path = f'{project}/{ref}/{sha1}/{distro}/{release}/{arch}/flavors/{flavor}'
    return f'{base_url}/{bin_path}/{filename}'

def get_binary_url(
    filename,
    project=None,
    distro=None,
    release=None,
    arch=None,
    flavor=None,
    branch=None,
    sha1=None
):
    '''
    Return the chacra url for a build result
    '''
    # query shaman for the built binary
    s = {}
    if project:
        s['project'] = project
    if distro:
        s['distros'] = _get_distros(distro, release, arch)
    if flavor:
        s['flavor'] = flavor
    if branch:
        s['ref'] = branch
    if sha1:
        s['sha1'] = sha1

    resp = search(**s)
    result = resp.json()

    if len(result) == 0:
        raise RuntimeError(f'no results found at {resp.url}')

    # TODO: filter the result down to the correct arch etc.?
    result = result[0]

    status = result['status']
    if status != 'ready':
        raise RuntimeError(f'cannot pull file with status: {status}')

    # build the chacra url
    chacra_host = urlparse(result['url']).netloc
    chacra_ref = result['ref']
    chacra_sha1 = result['sha1']
    log.info(f'got chacra host {chacra_host}, ref {chacra_ref}, sha1 {chacra_sha1} from {resp.url}')

    url = _get_binary_url(
        chacra_host,
        result['project'],
        chacra_ref,
        chacra_sha1,
        result['distro'],
        result['distro_codename'] if result['distro_codename'] else result['distro_version'],
        result['archs'][0],
        result['flavor'],
        filename,
    )

    return url

def pull(
    filename,
    project=None,
    distro=None,
    release=None,
    arch=None,
    flavor=None,
    branch=None,
    sha1=None
):
    '''
    Pull a build result from chacra
    '''
    url = get_binary_url(
            filename,
            project=project,
            distro=distro,
            release=release,
            arch=arch,
            flavor=flavor,
            branch=branch,
            sha1=sha1
    )
    resp = requests.get(url, stream=True)
    resp.raise_for_status()
    log.info(f'got file from {resp.url}')

    return resp

def main():
    handler = logging.StreamHandler(sys.stdout)
    log.addHandler(handler)
    log.setLevel(logging.INFO)

    parser = argparse.ArgumentParser()
    parser.add_argument('--project', default=PROJECT)
    parser.add_argument('--distro', default=DISTRO)
    parser.add_argument('--release', default=RELEASE)
    parser.add_argument('--arch', default=ARCH)
    parser.add_argument('--branch', default=BRANCH)
    parser.add_argument('--sha1', default=SHA1)
    parser.add_argument('--flavor', default=FLAVOR)
    parser.add_argument('--src', default=FILENAME)
    parser.add_argument('--dest', default=FILENAME)
    args = parser.parse_args()

    resp = pull(
        args.src,
        project=args.project,
        distro=args.distro,
        release=args.release,
        arch=args.arch,
        flavor=args.flavor,
        branch=args.branch,
        sha1=args.sha1
    )

    dest = Path(args.dest).absolute()
    with open(dest, 'wb') as f:
        for chunk in resp.iter_content(chunk_size=None, decode_unicode=True):
            log.info('.',)
            f.write(chunk)
    log.info(f'wrote binary file: {dest}')

    return 0


if __name__ == '__main__':
   sys.exit(main())
