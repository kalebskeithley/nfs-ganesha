/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.25 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "stuff_alloc.h"
#include "SemN.h"

#include <pthread.h>

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;
libzfswrap_handle_t *p_zhd;
libzfswrap_vfs_t *p_vfs;

/* filesystem info for your filesystem */
static fsal_staticfsinfo_t default_zfs_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  1024,                         /* max links for an object of your filesystem */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* min filename */
  TRUE,                         /* no_trunc */
  TRUE,                         /* chown restricted */
  FALSE,                        /* case insensitivity */
  TRUE,                         /* case preserving */
  FSAL_EXPTYPE_PERSISTENT,      /* FH expire type */
  TRUE,                         /* hard link support */
  TRUE,                         /* sym link support */
  FALSE,                        /* lock support */
  TRUE,                         /* named attributes */
  TRUE,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  TRUE,                         /* can change times */
  TRUE,                         /* homogenous */
  POSIX_SUPPORTED_ATTRIBUTES,   /* supported attributes */
  0,                            /* maxread size */
  0,                            /* maxwrite size */
  0,                            /* default umask */
  0,                            /* don't allow cross fileset export path */
  0400                          /* default access rights for xattrs: root=RW, owner=R */
};

/*
 *  Log Descriptor
 */
log_t fsal_log;

/* variables for limiting the calls to the filesystem */
static int limit_calls = FALSE;
semaphore_t sem_fs_calls;

/* threads keys for stats */
static pthread_key_t key_stats;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init keys */
static void init_keys(void)
{
  if(pthread_key_create(&key_stats, NULL) == -1)
    DisplayErrorJd(fsal_log, ERR_SYS, ERR_PTHREAD_KEY_CREATE, errno);

  return;
}                               /* init_keys */

/**
 * fsal_increment_nbcall:
 * Updates fonction call statistics.
 *
 * \param function_index (input):
 *        Index of the function whom number of call is to be incremented.
 * \param status (input):
 *        Status the function returned.
 *
 * \return Nothing.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* verify index */

  if(function_index >= FSAL_NB_FUNC)
    return;

  /* first, we init the keys if this is the first time */

  if(pthread_once(&once_key, init_keys) != 0)
    {
      DisplayErrorJd(fsal_log, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */

  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */

  if(bythread_stat == NULL)
    {
      int i;

      bythread_stat = (fsal_statistics_t *) Mem_Alloc(sizeof(fsal_statistics_t));

      if(bythread_stat == NULL)
        {
          DisplayErrorJd(fsal_log, ERR_SYS, ERR_MALLOC, Mem_Errno);
        }

      /* inits the struct */

      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  /* we increment the values */

  if(bythread_stat)
    {
      bythread_stat->func_stats.nb_call[function_index]++;

      if(!FSAL_IS_ERROR(status))
        bythread_stat->func_stats.nb_success[function_index]++;
      else if(fsal_is_retryable(status))
        bythread_stat->func_stats.nb_err_retryable[function_index]++;
      else
        bythread_stat->func_stats.nb_err_unrecover[function_index]++;
    }

  return;
}

/**
 * fsal_internal_getstats:
 * (For internal use in the FSAL).
 * Retrieve call statistics for current thread.
 *
 * \param output_stats (output):
 *        Pointer to the call statistics structure.
 *
 * \return Nothing.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      DisplayErrorJd(fsal_log, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */
  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */
  if(bythread_stat == NULL)
    {
      int i;

      if((bythread_stat =
          (fsal_statistics_t *) Mem_Alloc(sizeof(fsal_statistics_t))) == NULL)
        DisplayErrorJd(fsal_log, ERR_SYS, ERR_MALLOC, Mem_Errno);

      /* inits the struct */
      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  if(output_stats)
    (*output_stats) = (*bythread_stat);

  return;

}

/**
 *  Used to limit the number of simultaneous calls to Filesystem.
 */
void TakeTokenFSCall()
{
  /* no limits */
  if(limit_calls == FALSE)
    return;

  /* there is a limit */
  semaphore_P(&sem_fs_calls);

}

void ReleaseTokenFSCall()
{
  /* no limits */
  if(limit_calls == FALSE)
    return;

  /* there is a limit */
  semaphore_V(&sem_fs_calls);

}

#define SET_INTEGER_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      cfg._field = (p_init_info)->values._field;                  \
      break;                                                      \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( cfg._field > (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( cfg._field < (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
    }

#define SET_BITMAP_PARAM( cfg, p_init_info, _field )              \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a bit AND */                                     \
      cfg._field &= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a bit OR */                                      \
      cfg._field |= (p_init_info)->values._field ;                \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
    }

#define SET_BOOLEAN_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a boolean AND */                                 \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a boolean OR */                                  \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
    }

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        zfsfs_specific_initinfo_t * fs_specific_info)
{

  /* sanity check */
  if(!fsal_info || !fs_common_info)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* Setting log info */
  fsal_log = fsal_info->log_outputs;

  /* inits FS call semaphore */
  if(fsal_info->max_fs_calls > 0)
    {
      int rc;

      limit_calls = TRUE;

      rc = semaphore_init(&sem_fs_calls, fsal_info->max_fs_calls);

      if(rc != 0)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);

      DisplayLogJdLevel(fsal_log, NIV_DEBUG,
                        "FSAL INIT: Max simultaneous calls to filesystem is limited to %u.",
                        fsal_info->max_fs_calls);

    }
  else
    {
      DisplayLogJdLevel(fsal_log, NIV_DEBUG,
                        "FSAL INIT: Max simultaneous calls to filesystem is unlimited.");
    }

  /* setting default values. */
  global_fs_info = default_zfs_info;

  /* Analyzing fs_common_info struct */

  if((fs_common_info->behaviors.maxfilesize != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxlink != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxnamelen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxpathlen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.no_trunc != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_insensitive != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_preserving != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.named_attr != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.lease_time != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.supported_attrs != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.homogenous != FSAL_INIT_FS_DEFAULT))
    ReturnCode(ERR_FSAL_NOTSUPP, 0);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, symlink_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, link_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FileSystem info :");
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxfilesize  = %llX    ",
                    global_fs_info.maxfilesize);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxlink  = %lu   ", global_fs_info.maxlink);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxnamelen  = %lu  ",
                    global_fs_info.maxnamelen);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxpathlen  = %lu  ",
                    global_fs_info.maxpathlen);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  no_trunc  = %d ", global_fs_info.no_trunc);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  chown_restricted  = %d ",
                    global_fs_info.chown_restricted);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  case_insensitive  = %d ",
                    global_fs_info.case_insensitive);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  case_preserving  = %d ",
                    global_fs_info.case_preserving);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  fh_expire_type  = %hu ",
                    global_fs_info.fh_expire_type);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  link_support  = %d  ",
                    global_fs_info.link_support);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  symlink_support  = %d  ",
                    global_fs_info.symlink_support);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  lock_support  = %d  ",
                    global_fs_info.lock_support);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  named_attr  = %d  ",
                    global_fs_info.named_attr);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  unique_handles  = %d  ",
                    global_fs_info.unique_handles);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  lease_time  = %u.%u     ",
                    global_fs_info.lease_time.seconds,
                    global_fs_info.lease_time.nseconds);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  acl_support  = %hu  ",
                    global_fs_info.acl_support);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  cansettime  = %d  ",
                    global_fs_info.cansettime);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  homogenous  = %d  ",
                    global_fs_info.homogenous);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  supported_attrs  = %llX  ",
                    global_fs_info.supported_attrs);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxread  = %llX     ",
                    global_fs_info.maxread);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  maxwrite  = %llX     ",
                    global_fs_info.maxwrite);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  umask  = %#o ", global_fs_info.umask);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  auth_exportpath_xdev  = %d  ",
                    global_fs_info.auth_exportpath_xdev);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  xattr_access_rights = %#o ",
                    global_fs_info.xattr_access_rights);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}