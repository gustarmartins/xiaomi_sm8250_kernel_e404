// SPDX-License-Identifier: GPL-2.0
/* Included by services.c to reuse its parser and AV calculator. No enforcement
 * caller uses this snapshot. Published storage is retained until reboot. */
#if IS_BUILTIN(CONFIG_KSU)
static DEFINE_MUTEX(compat_query_mutex);
static struct selinux_ss compat_query_ss;
static bool compat_query_attempted;
static bool compat_query_ready;
static bool compat_query_invalidated;
static bool compat_query_enabled;

extern bool ksu_compat_query_eligible(void);

/* Called only after the first policy load has validated the original input.
 * Re-parsing those bytes avoids a policydb_write round trip: normalization can
 * grow a policy beyond its original serialized length, notably in recovery.
 * The caller retains the input for this call; policydb_read owns its copies.
 */
void security_compat_capture_stock(void *data, size_t len)
{
	struct policy_file fp;
	u32 generation;
	int rc = -EAGAIN;

	mutex_lock(&compat_query_mutex);
	if (compat_query_attempted)
		goto out;
	WRITE_ONCE(compat_query_attempted, true);
	if (!selinux_initialized(&selinux_state))
		goto failed;
	if (!data || !len) {
		rc = -EINVAL;
		goto failed;
	}
	read_lock(&selinux_state.ss->policy_rwlock);
	generation = selinux_state.ss->latest_granting;
	read_unlock(&selinux_state.ss->policy_rwlock);
	rwlock_init(&compat_query_ss.policy_rwlock);
	mutex_init(&compat_query_ss.status_lock);
	compat_query_ss.sidtab = kzalloc(sizeof(*compat_query_ss.sidtab), GFP_KERNEL);
	if (!compat_query_ss.sidtab) {
		rc = -ENOMEM;
		goto failed;
	}
	fp.data = data;
	fp.len = len;
	/* policydb_read cleans its partial database on failure. */
	rc = policydb_read(&compat_query_ss.policydb, &fp);
	if (rc)
		goto free_sidtab;
	/* This initializes the table and cleans it internally on failure. */
	rc = policydb_load_isids(&compat_query_ss.policydb, compat_query_ss.sidtab);
	if (rc)
		goto free_policy;
	read_lock(&selinux_state.ss->policy_rwlock);
	rc = generation == selinux_state.ss->latest_granting ? 0 : -ESTALE;
	read_unlock(&selinux_state.ss->policy_rwlock);
	if (rc || READ_ONCE(compat_query_invalidated)) {
		rc = -ESTALE;
		sidtab_destroy(compat_query_ss.sidtab);
		goto free_policy;
	}
	smp_store_release(&compat_query_ready, true);
	pr_info("SELinux: compatibility query snapshot ready (%zu bytes, generation %u)\n",
		len, generation);
	goto out;
free_policy:
	policydb_destroy(&compat_query_ss.policydb);
free_sidtab:
	kfree(compat_query_ss.sidtab);
	compat_query_ss.sidtab = NULL;
failed:
	pr_warn("SELinux: compatibility query snapshot unavailable: %d\n", rc);
out:
	mutex_unlock(&compat_query_mutex);
}

/* Called before a later real-policy replacement/boolean mutation. It is safe
 * under policy_rwlock: no sleeping, freeing, or callback into KernelSU. */
void security_compat_invalidate(void)
{
	if (READ_ONCE(compat_query_attempted)) {
		WRITE_ONCE(compat_query_invalidated, true);
		WRITE_ONCE(compat_query_enabled, false);
	}
}

bool security_compat_get_enabled(void)
{
	return READ_ONCE(compat_query_enabled) &&
		smp_load_acquire(&compat_query_ready) &&
		!READ_ONCE(compat_query_invalidated);
}

int security_compat_set_enabled(bool enabled)
{
	int rc = 0;

	mutex_lock(&compat_query_mutex);
	if (enabled && (!smp_load_acquire(&compat_query_ready) ||
			READ_ONCE(compat_query_invalidated)))
		rc = -EAGAIN;
	else
		WRITE_ONCE(compat_query_enabled, enabled);
	mutex_unlock(&compat_query_mutex);
	return rc;
}

bool security_compat_active(void)
{
	return security_compat_get_enabled() && ksu_compat_query_eligible();
}

/* Contexts remain transient: repeated MLS queries cannot grow a second SID
 * cache. The original parser performs policy, role, type and MLS validation. */
static int compat_query_context(const char *text, u32 len, struct context *ctx)
{
	char *copy;
	int rc;

	if (!smp_load_acquire(&compat_query_ready))
		return -EAGAIN;
	if (!len || len > PAGE_SIZE)
		return -EINVAL;
	copy = kmemdup_nul(text, len, GFP_KERNEL);
	if (!copy)
		return -ENOMEM;
	rc = string_to_context_struct(&compat_query_ss.policydb,
		compat_query_ss.sidtab, copy, ctx, SECSID_NULL);
	kfree(copy);
	return rc;
}

int security_compat_context(const char *text, u32 len, char **canon, u32 *canon_len)
{
	struct context ctx;
	int rc;

	rc = compat_query_context(text, len, &ctx);
	if (rc)
		return rc;
	if (canon)
		rc = context_struct_to_string(&compat_query_ss.policydb, &ctx,
			canon, canon_len);
	context_destroy(&ctx);
	return rc;
}

int security_compat_access(const char *source, const char *target, u16 tclass,
			   struct av_decision *avd)
{
	struct policydb *p = &compat_query_ss.policydb;
	struct context scontext, tcontext;
	int rc;

	rc = compat_query_context(source, strlen(source), &scontext);
	if (rc)
		return rc;
	rc = compat_query_context(target, strlen(target), &tcontext);
	if (rc)
		goto out_source;
	/* Use the real generation, never a fabricated status/sequence pair. */
	read_lock(&selinux_state.ss->policy_rwlock);
	avd_init(&selinux_state, avd);
	read_unlock(&selinux_state.ss->policy_rwlock);
	if (ebitmap_get_bit(&p->permissive_map, scontext.type))
		avd->flags |= AVD_FLAGS_PERMISSIVE;
	if (!tclass) {
		if (p->allow_unknown)
			avd->allowed = 0xffffffff;
	} else {
		context_struct_compute_av(p, &scontext, &tcontext, tclass, avd, NULL);
	}
	context_destroy(&tcontext);
out_source:
	context_destroy(&scontext);
	return rc;
}
#endif
