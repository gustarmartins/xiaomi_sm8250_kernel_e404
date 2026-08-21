// SPDX-License-Identifier: GPL-2.0-only
#include <stdbool.h>
#include <stdio.h>

enum {
	POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN = 0,
	POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
	POWER_SUPPLY_CAPACITY_LEVEL_LOW,
	POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
	POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	POWER_SUPPLY_CAPACITY_LEVEL_FULL,
};

#include "../../../../drivers/power/supply/qcom/smb5-capacity-policy.h"

#define CAPACITY_CASE_COUNT 12

struct capacity_case {
	int reported_level;
	int capacity;
	bool input_present;
	int expected;
};

int main(void)
{
	static const struct capacity_case cases[CAPACITY_CASE_COUNT] = {
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 0, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 0, true,
		  POWER_SUPPLY_CAPACITY_LEVEL_LOW },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 1, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_LOW },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 20, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_LOW },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 21, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_NORMAL },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 80, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_NORMAL },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 81, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_HIGH },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 99, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_HIGH },
		{ POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, 100, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_FULL },
		{ POWER_SUPPLY_CAPACITY_LEVEL_NORMAL, 0, true,
		  POWER_SUPPLY_CAPACITY_LEVEL_NORMAL },
		{ POWER_SUPPLY_CAPACITY_LEVEL_HIGH, 99, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_HIGH },
		{ POWER_SUPPLY_CAPACITY_LEVEL_FULL, 100, false,
		  POWER_SUPPLY_CAPACITY_LEVEL_FULL },
	};
	size_t i;

	printf("TAP version 13\n1..%d\n", CAPACITY_CASE_COUNT);
	for (i = 0; i < CAPACITY_CASE_COUNT; i++) {
		int actual;
		int reported = cases[i].reported_level;
		int cap = cases[i].capacity;
		bool present = cases[i].input_present;

		actual = smb5_capacity_level_resolve(reported, cap, present);

		if (actual != cases[i].expected) {
			printf("not ok %zu - want=%d got=%d\n", i + 1,
			       cases[i].expected, actual);
			return 1;
		}
		printf("ok %zu - reported=%d capacity=%d input=%d\n", i + 1,
		       cases[i].reported_level, cases[i].capacity,
		       cases[i].input_present);
	}

	return 0;
}
