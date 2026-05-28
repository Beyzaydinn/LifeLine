"""Unit test for serial_bridge.vitals_stable (no hardware needed)."""

from serial_bridge import vitals_stable


def test_too_few_readings_not_stable():
    assert vitals_stable([72, 72], tolerance=3, need=4) is False


def test_stable_when_last_n_within_tolerance():
    # last 4 within +/-3 -> stable
    assert vitals_stable([120, 60, 75, 76, 77, 78], tolerance=3, need=4) is True


def test_not_stable_when_spread_too_wide():
    assert vitals_stable([70, 90, 71, 95], tolerance=3, need=4) is False


def test_uses_only_the_last_n():
    # early jitter ignored once the tail settles
    assert vitals_stable([10, 200, 80, 81, 82, 83], tolerance=3, need=4) is True


if __name__ == "__main__":
    test_too_few_readings_not_stable()
    test_stable_when_last_n_within_tolerance()
    test_not_stable_when_spread_too_wide()
    test_uses_only_the_last_n()
    print("ALL TESTS PASSED")
