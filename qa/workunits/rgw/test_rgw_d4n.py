#!/usr/bin/python3

'''
This workunits tests the functionality of the D4N read workflow on a small object of size 4.
''' 

import logging as log
from configobj import ConfigObj
import boto3
import redis
import subprocess
import json
import os

log.basicConfig(level=log.DEBUG)

""" Constants """
ACCESS_KEY = 'test3'
SECRET_KEY = 'test3'

def exec_cmd(cmd):
    log.debug("exec_cmd(%s)", cmd)
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        out, err = proc.communicate()
        if proc.returncode == 0:
            log.info('command succeeded')
            if out is not None: log.info(out)
            return out
        else:
            raise Exception("error: %s \nreturncode: %s" % (err, proc.returncode))
    except Exception as e:
        log.error('command failed')
        log.error(e)
        return False

def get_radosgw_endpoint():
    out = exec_cmd('sudo netstat -nltp | egrep "rados|valgr"')  # short for radosgw/valgrind
    x = out.decode('utf8').split(" ")
    port = [i for i in x if ':' in i][0].split(':')[1]
    log.info('radosgw port: %s' % port)
    proto = "http"
    hostname = '127.0.0.1'

    if port == '443':
        proto = "https"

    endpoint = "%s://%s:%s" % (proto, hostname, port)

    log.info("radosgw endpoint is: %s", endpoint)
    return endpoint, proto

def create_s3cmd_config(path, proto):
    """
    Creates a minimal config file for s3cmd
    """
    log.info("Creating s3cmd config...")

    use_https_config = "False"
    log.info("proto for s3cmd config is %s", proto)
    if proto == "https":
        use_https_config = "True"

    s3cmd_config = ConfigObj(
        indent_type='',
        infile={
            'default':
                {
                'host_bucket': 'no.way.in.hell',
                'use_https': use_https_config,
                },
            }
    )

    f = open(path, 'wb')
    s3cmd_config.write(f)
    f.close()
    log.info("s3cmd config written")

def get_cmd_output(cmd_out):
    out = cmd_out.decode('utf8')
    out = out.strip('\n')
    return out

def test_directory_methods(r, client, obj):
    test_txt = 'test'

    response_put = obj.put(Body=test_txt)

    assert(response_put.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    # first get call
    response_get = obj.get()

    assert(response_get.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    data = r.hgetall('bkt_test.txt_0_4')
    output = subprocess.check_output(['radosgw-admin', 'object', 'stat', '--bucket=bkt', '--object=test.txt'])
    attrs = json.loads(output.decode('latin-1'))

    assert(data.get('blockID'), '0')
    assert(data.get('version'), attrs.get('tag'))
    assert(data.get('size'), '4')
    assert(data.get('globalWeight'), '0')
    assert(data.get('blockHosts'), '127.0.0.1:6379')
    assert(data.get('objName'), 'test.txt')
    assert(data.get('bucketName'), 'bkt')
    assert(data.get('creationTime'), attrs.get('mtime'))
    assert(data.get('dirty'), '0')
    assert(data.get('objHosts'), '127.0.0.1:6379')

    # second get call
    response_get = obj.get()

    assert(response_get.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    data = r.hgetall('bkt_test.txt_0_4')
    output = subprocess.check_output(['radosgw-admin', 'object', 'stat', '--bucket=bkt', '--object=test.txt'])
    attrs = json.loads(output.decode('latin-1'))

    assert(data.get('blockID'), '0')
    assert(data.get('version'), attrs.get('tag'))
    assert(data.get('size'), '4')
    assert(data.get('globalWeight'), '0')
    assert(data.get('blockHosts'), '127.0.0.1:6379')
    assert(data.get('objName'), 'test.txt')
    assert(data.get('bucketName'), 'bkt')
    assert(data.get('creationTime'), attrs.get('mtime'))
    assert(data.get('dirty'), '0')
    assert(data.get('objHosts'), '127.0.0.1:6379')

    r.flushall()

def test_cache_methods(r, client, obj):
    test_txt = 'test'

    response_put = obj.put(Body=test_txt)

    assert(response_put.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    # first get call
    response_get = obj.get()

    assert(response_get.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    print(subprocess.check_output(['ls', '%s/build/out' % os.environ['CEPH_ROOT']]))

    # check logs to ensure object was retrieved from storage backend
    res = subprocess.call(['grep', '"D4NFilterObject::iterate:: iterate(): Fetching object from backend store"', '%s/build/out/radosgw.8000.log' % os.environ['CEPH_ROOT']])

    assert(res >= 1)

    # retrieve and compare cache contents
    data = subprocess.check_output(['ls', '/tmp/rgw_d4n_datacache/'])
    data = data.decode('latin-1').strip()

    output = subprocess.check_output(['md5sum', '/tmp/rgw_d4n_datacache/' + data]).decode('latin-1')
    assert(output.splitlines()[0].split()[0] == hashlib.md5("test".encode('utf-8')).hexdigest())

    # second get call
    response_get = obj.get()

    assert(response_get.get('ResponseMetadata').get('HTTPStatusCode') == 200)

    # check logs to ensure object was retrieved from cache
    res = subprocess.call(['grep', '"SSDCache: get_async(): ::aio_read(), ret=0"', '%s/build/out/radosgw.8000.log' % os.environ['CEPH_ROOT']])
    assert(res >= 1)

    # retrieve and compare cache contents
    data = subprocess.check_output(['ls', '/tmp/rgw_d4n_datacache/'])
    data = data.decode('latin-1').strip()

    output = subprocess.check_output(['md5sum', '/tmp/rgw_d4n_datacache/' + data]).decode('latin-1')
    assert(output.splitlines()[0].split()[0] == hashlib.md5("test".encode('utf-8')).hexdigest())

    r.flushall()

def main():
    """
    execute the d4n test
    """

    # Setup for test
    log.info("D4NFilterTest setup.")

    out = exec_cmd('pwd')
    pwd = get_cmd_output(out)
    log.debug("pwd is: %s", pwd)

    endpoint, proto = get_radosgw_endpoint()

    client = boto3.client(service_name='s3',
                aws_access_key_id=ACCESS_KEY,
                aws_secret_access_key=SECRET_KEY,
                endpoint_url=endpoint,
                use_ssl=False,
                verify=False)

    s3 = boto3.resource('s3', 
                aws_access_key_id=ACCESS_KEY,
                aws_secret_access_key=SECRET_KEY,
                endpoint_url=endpoint, 
                use_ssl=False,
                verify=False)

    bucket = s3.Bucket('bkt')
    bucket.create()
    obj = s3.Object(bucket_name='bkt', key='test.txt')

    # Check for Redis instance
    try:
        connection = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)
        connection.ping() 
    except:
        log.debug("ERROR: Redis instance not running.")
        raise

    # Create s3cmd config
    s3cmd_config_path = pwd + '/s3cfg'
    create_s3cmd_config(s3cmd_config_path, proto)

    r = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)

    test_directory_methods(r, client, obj)

    # Responses should not be decoded
    r = redis.Redis(host='localhost', port=6379, db=0)

    test_cache_methods(r, client, obj)

    log.info("D4NFilterTest successfully completed.")

main()
log.info("Completed D4N tests")
