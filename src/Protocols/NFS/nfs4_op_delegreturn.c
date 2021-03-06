/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs4_op_delegreturn.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"

/**
 * @brief NFS4_OP_DELEGRETURN
 *
 * This function implements the NFS4_OP_DELEGRETURN operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC 5661, p. 364
 */
int nfs4_op_delegreturn(struct nfs_argop4 *op, compound_data_t *data,
			struct nfs_resop4 *resp)
{
	DELEGRETURN4args * const arg_DELEGRETURN4 =
	    &op->nfs_argop4_u.opdelegreturn;
	DELEGRETURN4res * const res_DELEGRETURN4 =
	    &resp->nfs_resop4_u.opdelegreturn;

	state_status_t state_status;
	state_t *state_found;
	state_owner_t *lock_owner;
	fsal_lock_param_t lock_desc;
	struct glist_head *glist;
	struct deleg_data *found_deleg;
	struct deleg_data *iter_deleg;
	const char *tag = "DELEGRETURN";

	LogDebug(COMPONENT_NFS_V4_LOCK,
		 "Entering NFS v4 DELEGRETURN handler -----------------------------------------------------");

	/* Initialize to sane default */
	resp->resop = NFS4_OP_DELEGRETURN;

	/* If the filehandle is invalid */
	res_DELEGRETURN4->status = nfs4_Is_Fh_Invalid(&data->currentFH);

	if (res_DELEGRETURN4->status != NFS4_OK)
		return res_DELEGRETURN4->status;

	/* Check stateid correctness and get pointer to state */
	res_DELEGRETURN4->status = nfs4_Check_Stateid(&arg_DELEGRETURN4->
						      deleg_stateid,
						      data->current_entry,
						      &state_found,
						      data,
						      STATEID_SPECIAL_FOR_LOCK,
						      0,
						      false,
						      tag);

	if (res_DELEGRETURN4->status != NFS4_OK)
		return res_DELEGRETURN4->status;

	/* Delegations are only supported on regular files at the moment */
	if (data->current_filetype != REGULAR_FILE) {
		res_DELEGRETURN4->status = NFS4ERR_INVAL;
		return NFS4ERR_INVAL;
	}

	found_deleg = NULL;
	PTHREAD_RWLOCK_wrlock(&data->current_entry->state_lock);
	glist_for_each(glist, &data->current_entry->object.file.deleg_list) {
		iter_deleg = glist_entry(glist, struct deleg_data, dd_list);
		LogDebug(COMPONENT_NFS_V4_LOCK, "iter deleg entry %p",
			 iter_deleg);
		assert(iter_deleg->dd_state->state_type == STATE_TYPE_DELEG);
		if (SAME_STATEID(&arg_DELEGRETURN4->deleg_stateid,
				 iter_deleg->dd_state)) {
			found_deleg = iter_deleg;
			break;
		}
	}

	if (found_deleg == NULL) {
		LogWarn(COMPONENT_NFS_V4_LOCK,
			"Found state, but not deleg lock!");
		res_DELEGRETURN4->status = NFS4ERR_BAD_STATEID;
		goto unlock;
	}

	LogDebug(COMPONENT_NFS_V4_LOCK, "Matching delegation found!");

	lock_owner = found_deleg->dd_owner;

	/* lock_type doesn't matter as we are going to do unlock */
	lock_desc.lock_type = FSAL_LOCK_R;
	lock_desc.lock_start = 0;
	lock_desc.lock_length = 0;
	lock_desc.lock_sle_type = FSAL_LEASE_LOCK;

	LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG, tag, data->current_entry,
		lock_owner, &lock_desc);

	deleg_heuristics_recall(found_deleg);

	/* Now we have a lock owner and a stateid.
	 * Go ahead and push unlock into SAL (and FSAL) to return
	 * the delegation.
	 */
	state_status = release_lease_lock(data->current_entry, lock_owner,
					  state_found, &lock_desc);
	if (state_status != STATE_SUCCESS) {
		/* Save the response in the lock owner */
		Copy_nfs4_state_req(lock_owner,
				    arg_DELEGRETURN4->deleg_stateid.seqid,
				    op,
				    data->current_entry,
				    resp,
				    tag);
		res_DELEGRETURN4->status = nfs4_Errno_state(state_status);
		goto unlock;
	}

	state_del_locked(state_found, data->current_entry);

	/* Successful exit */
	res_DELEGRETURN4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4_LOCK, "Successful exit");

	/* Save the response in the lock owner */
	Copy_nfs4_state_req(lock_owner,
			    arg_DELEGRETURN4->deleg_stateid.seqid,
			    op,
			    data->current_entry,
			    resp,
			    tag);

unlock:
	PTHREAD_RWLOCK_unlock(&data->current_entry->state_lock);
	return res_DELEGRETURN4->status;
}				/* nfs4_op_delegreturn */

/**
 * @brief Free memory allocated for DELEGRETURN result
 *
 * This function frees any memory allocated for the result of the
 * DELEGRETURN operation.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 */
void nfs4_op_delegreturn_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_delegreturn_Free */

void nfs4_op_delegreturn_CopyRes(DELEGRETURN4res *resp_dst,
				 DELEGRETURN4res *resp_src)
{
	/* Nothing to deep copy */
	return;
}
