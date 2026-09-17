/* SPDX-License-Identifier: GPL-2.0 */
/* Temporary bounded diagnostic; callers serialize access with history_lock. */
#define MGLRU_HISTORY_SLOTS 8192
#define MGLRU_HISTORY_MATCHES 64

struct mglru_history_event {
	u64 sequence, timestamp;
	unsigned long pfn, flags, caller, max_seq, min_seq;
	long old_size, new_size;
	int owner, charge, old_gen, new_gen, type, zone, delta, pid, reason;
};

struct mglru_history {
	struct mglru_history_event ring[MGLRU_HISTORY_SLOTS];
	struct mglru_history_event matches[MGLRU_HISTORY_MATCHES];
	u64 count;
	unsigned int matched, total_matches;
	bool frozen;
};

static void mglru_history_push(struct mglru_history *history,
			       struct mglru_history_event *event)
{
	if (history->frozen)
		return;
	event->sequence = ++history->count;
	history->ring[(event->sequence - 1) % MGLRU_HISTORY_SLOTS] = *event;
}

/* Retain the newest matching records, in reverse chronological order. */
static void mglru_history_freeze(struct mglru_history *history, unsigned long pfn)
{
	u64 offset, retained = min_t(u64, history->count, MGLRU_HISTORY_SLOTS);

	if (history->frozen)
		return;
	WRITE_ONCE(history->frozen, true);
	for (offset = 0; offset < retained; offset++) {
		struct mglru_history_event *event =
			&history->ring[(history->count - offset - 1) % MGLRU_HISTORY_SLOTS];

		if (event->pfn != pfn)
			continue;
		history->total_matches++;
		if (history->matched < MGLRU_HISTORY_MATCHES)
			history->matches[history->matched++] = *event;
	}
}
