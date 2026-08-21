/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SMB5_CAPACITY_POLICY_H
#define __SMB5_CAPACITY_POLICY_H

/*
 * The including translation unit supplies bool and the
 * POWER_SUPPLY_CAPACITY_LEVEL_* constants.
 */
static inline int smb5_capacity_level_for_soc(int capacity,
					      bool input_present)
{
	if (capacity == 0)
		return input_present ? POWER_SUPPLY_CAPACITY_LEVEL_LOW :
			POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	if (capacity <= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	if (capacity <= 80)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	if (capacity < 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;

	return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
}

#endif /* __SMB5_CAPACITY_POLICY_H */
