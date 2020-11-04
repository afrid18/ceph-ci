import datetime

import pytest

from ceph.utils import datetime_now, datetime_to_str, str_to_datetime


def test_datetime_to_str_1():
    dt = datetime.datetime.now()
    assert type(datetime_to_str(dt)) is str


def test_datetime_to_str_2():
    dt = datetime.datetime.strptime('2019-04-24T17:06:53.039991',
                                    '%Y-%m-%dT%H:%M:%S.%f')
    assert datetime_to_str(dt) == '2019-04-24T17:06:53.039991Z'


def test_datetime_to_str_3():
    dt = datetime.datetime.strptime('2020-11-02T04:40:12.748172-0800',
                                    '%Y-%m-%dT%H:%M:%S.%f%z')
    assert datetime_to_str(dt) == '2020-11-02T12:40:12.748172Z'


def test_str_to_datetime_1():
    dt = str_to_datetime('2020-03-03T09:21:43.636153304Z')
    assert type(dt) is datetime.datetime


def test_str_to_datetime_2():
    dt = str_to_datetime('2020-03-03T15:52:30.136257504-0600')
    assert type(dt) is datetime.datetime


def test_str_to_datetime_3():
    dt = str_to_datetime('2020-03-03T15:52:30.136257504')
    assert type(dt) is datetime.datetime


def test_str_to_datetime_4():
    with pytest.raises(ValueError):
        str_to_datetime('2020-03-03 15:52:30.136257504')


def test_str_to_datetime_5():
    with pytest.raises(ValueError):
        str_to_datetime('2020-03-03')


def test_datetime_now_1():
    dt = str_to_datetime('2020-03-03T09:21:43.636153304Z')
    assert dt < datetime_now()
