#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/cpu.h>
#include <linux/jbd2.h>

#define RELATIVEJUMP_SIZE	5
#define RELATIVEJUMP_OPCODE	0xe9

unsigned char inst[RELATIVEJUMP_SIZE];
static void *(*my_text_poke_smp)(void *addr, const void *opcode, size_t len);
static void *orig___jbd2_log_start_commit;

struct mutex *my_text_mutex;

static int overwrite___jbd2_log_start_commit(journal_t *journal, tid_t target)
{
	/* Return if the txn has already requested to be commited */
	if (journal->j_commit_request == target)
		return 0;

	/*
	 * The only transaction we can possibly wait upon is the
	 * currently running transaction (if it exists).  Otherwise,
	 * the target tid must be on old one.
	 */
	if (journal->j_running_transaction &&
	    journal->j_running_transaction->t_tid == target) {
		/*
		 * We want a new commit: OK, mark the request and wakeup the
		 * commit thread.  We do _not_ do the commit ourselves.
		 */

		journal->j_commit_request = target;
		/*
		jdb_debug(1, "JBD2: requesting commit %d/%d\n",
			  journal->j_commit_request,
			  journal->j_commit_sequence);
		*/
		wake_up(&journal->j_wait_commit);
		return 1;
	} else if (!tid_geq(journal->j_commit_request, target))
		/* This should never happen, but if it does, preserve
		 * the evidence before kjournald goes into a loop and
		 * increments j_commit_sequence beyond all recognition. */
		WARN_ONCE(1, "JBD2: bad log_start_commit: %u %u %u %u\n",
			  journal->j_commit_request,
			  journal->j_commit_sequence,
			  target, journal->j_running_transaction ?
			  journal->j_running_transaction->t_tid : 0);
	return 0;
}

static int __init jbd2_tid_init(void)
{
	unsigned char e9_jmp[RELATIVEJUMP_SIZE];
	s32 offset;

	my_text_poke_smp = (void *)kallsyms_lookup_name("text_poke_smp");
	if (!my_text_poke_smp)
		return -EINVAL;

	my_text_mutex = (void *)kallsyms_lookup_name("text_mutex");
	if (!my_text_mutex)
		return -EINVAL;

	orig___jbd2_log_start_commit =
		(void *)kallsyms_lookup_name("__jbd2_log_start_commit");
	if (!orig___jbd2_log_start_commit)
		return -EINVAL;

	offset = (s32)((long)overwrite___jbd2_log_start_commit -
			(long)orig___jbd2_log_start_commit - RELATIVEJUMP_SIZE);

	memcpy(inst, orig___jbd2_log_start_commit, RELATIVEJUMP_SIZE);

	e9_jmp[0] = RELATIVEJUMP_OPCODE;
	(*(s32 *)(&e9_jmp[1])) = offset;

	get_online_cpus();
	mutex_lock(my_text_mutex);
	my_text_poke_smp(orig___jbd2_log_start_commit, e9_jmp, RELATIVEJUMP_SIZE);
	mutex_unlock(my_text_mutex);
	put_online_cpus();
	
	return 0;
}

static void __exit jbd2_tid_exit(void)
{
	get_online_cpus();
	mutex_lock(my_text_mutex);
	my_text_poke_smp(orig___jbd2_log_start_commit, inst, RELATIVEJUMP_SIZE);
	mutex_unlock(my_text_mutex);
	put_online_cpus();

	smp_mb();
}

module_init(jbd2_tid_init);
module_exit(jbd2_tid_exit);

MODULE_AUTHOR("Zheng Liu <wenqing.lz@taobao.com>");
MODULE_DESCRIPTION("Hotfix for jbd2 tid bug");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
