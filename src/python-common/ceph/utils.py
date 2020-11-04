import datetime
import re


def datetime_now() -> datetime.datetime:
    """
    Return the current local date and time.
    :return: Returns an aware datetime object of the current date
        and time.
    """
    return datetime.datetime.now(tz=datetime.timezone.utc)


def datetime_to_str(dt: datetime.datetime) -> str:
    """
    Convert a datetime object into a ISO 8601 string, e.g.
    '2019-04-24T17:06:53.039991Z'.
    :param dt: The datetime object to process.
    :return: Return a string representing the date in
        ISO 8601 (timezone=UTC).
    """
    return dt.astimezone(tz=datetime.timezone.utc).strftime(
        '%Y-%m-%dT%H:%M:%S.%fZ')


def str_to_datetime(string: str) -> datetime.datetime:
    """
    Convert an ISO 8601 string into a datetime object.
    The following formats are supported:

    - 2020-03-03T09:21:43.636153304Z
    - 2020-03-03T15:52:30.136257504-0600
    - 2020-03-03T15:52:30.136257504

    :param string: The string to parse.
    :return: Returns an aware datetime object of the given date
        and time string.
    :raises: :exc:`~exceptions.ValueError` for an unknown
        datetime string.
    """
    fmts = [
        '%Y-%m-%dT%H:%M:%S.%f',
        '%Y-%m-%dT%H:%M:%S.%f%z'
    ]

    # In *all* cases, the 9 digit second precision is too much for
    # Python's strptime. Shorten it to 6 digits.
    p = re.compile(r'(\.[\d]{6})[\d]*')
    string = p.sub(r'\1', string)

    # Replace trailing Z with -0000, since (on Python 3.6.8) it
    # won't parse.
    if string and string[-1] == 'Z':
        string = string[:-1] + '-0000'

    for fmt in fmts:
        try:
            return datetime.datetime.strptime(string, fmt)
        except ValueError:
            pass

    raise ValueError("Time data {} does not match one of the formats {}".format(
        string, str(fmts)))
