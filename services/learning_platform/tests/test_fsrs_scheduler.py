import unittest
from datetime import datetime, timedelta, timezone

from learning_platform.fsrs_scheduler import (
    PARAMETERS,
    ReviewEvent,
    replay,
)


class FsrsSchedulerTest(unittest.TestCase):
    def test_pinned_fsrs_6_default_parameters(self):
        self.assertEqual(len(PARAMETERS), 21)
        self.assertEqual(PARAMETERS[:4], (0.212, 1.2931, 2.3065, 8.2956))
        self.assertEqual(PARAMETERS[-1], 0.1542)

    def test_identical_histories_produce_identical_state(self):
        start = datetime(2026, 1, 1, 9, tzinfo=timezone.utc)
        for ratings in ([1], [3, 3], [4, 2, 1, 3], [2, 2, 3, 4, 1, 3]):
            events = [
                ReviewEvent(rating, start + timedelta(days=index, minutes=index * 7))
                for index, rating in enumerate(ratings)
            ]
            first = replay(events)
            second = replay(events)
            self.assertEqual(first[0].to_dict(), second[0].to_dict())
            self.assertEqual(first[1], second[1])
            self.assertEqual(first[2:], second[2:])


if __name__ == "__main__":
    unittest.main()
