/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_RPC_H
#define GANESHA_RPC_H


#include <rpc/xdr_inline.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/svc_dg.h>
#include <rpc/clnt.h>

#ifdef _HAVE_GSSAPI
#include <rpc/auth_gss.h>
#include <rpc/svc_auth.h>
#endif

#include <rpc/svc_rqst.h>
#include  <rpc/svc_dplx.h>

#include "HashTable.h"

void socket_setoptions(int socketFd);

#ifdef _APPLE
#define __FDS_BITS(set) ((set)->fds_bits)
#endif

#ifdef _USE_TIRPC
typedef struct sockaddr_storage sockaddr_t;
#else
typedef struct sockaddr_in sockaddr_t;
#endif

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
#define XP_SOCK xp_fd
#else
#define XP_SOCK xp_sock
#endif

#ifndef AUTH_SYS
#define AUTH_SYS 1
#endif

#ifdef _USE_TIRPC
extern void Svc_dg_soft_destroy(SVCXPRT * xport);
extern struct netconfig *getnetconfigent(const char *netid);
extern void freenetconfigent(struct netconfig *);
extern SVCXPRT *Svc_vc_create(int, u_int, u_int);
extern SVCXPRT *Svc_dg_create(int, u_int, u_int);

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)
extern int CheckXprt(SVCXPRT *xprt);
#else
#define CheckXprt(ptr)
#endif

#else                       /* not _USE_TIRPC */

extern void Svcudp_soft_destroy(SVCXPRT * xprt);
extern SVCXPRT *Svctcp_create(register int sock, u_int sendsize, u_int recvsize);
extern SVCXPRT *Svcudp_bufcreate(register int sock, u_int sendsz, u_int recvsz);
extern bool_t Svc_register(SVCXPRT * xprt, u_long prog, u_long vers, void (*dispatch) (),
                    int protocol);
#define CheckXprt(ptr)

#endif                          /* _USE_TIRPC */

#ifdef _SOLARIS
#define _authenticate __authenticate
#endif

#ifdef _HAVE_GSSAPI
struct svc_rpc_gss_data
{
  bool_t established;           /* context established */
  gss_ctx_id_t ctx;             /* context id */
  struct rpc_gss_sec sec;       /* security triple */
  gss_buffer_desc cname;        /* GSS client name */
  u_int seq;                    /* sequence number */
  u_int win;                    /* sequence window */
  u_int seqlast;                /* last sequence number */
  uint32_t seqmask;             /* bitmask of seqnums */
  gss_name_t client_name;       /* unparsed name string */
  gss_buffer_desc checksum;     /* so we can free it */
};

typedef struct nfs_krb5_param__
{
  char keytab[MAXPATHLEN];
  char ccache_dir[MAXPATHLEN];
    /* XXX representation of GSSAPI service, independent of
     * GSSRPC or TI-RPC global variables.  Initially, use it just
     * for callbacks. */
  struct {
      char principal[MAXPATHLEN];
      gss_name_t gss_name;
  } svc;
  bool_t active_krb5;
  hash_parameter_t hash_param;
} nfs_krb5_parameter_t;

#define SVCAUTH_PRIVATE(auth) \
  ((struct svc_rpc_gss_data *)(auth)->svc_ah_private)

bool_t Svcauth_gss_import_name(char *service);
bool_t Svcauth_gss_acquire_cred(void);
bool_t Svcauth_gss_set_svc_name(gss_name_t name);
int Gss_ctx_Hash_Init(nfs_krb5_parameter_t param);
enum auth_stat Rpcsecgss__authenticate(register struct svc_req *rqst,
                                       struct rpc_msg *msg,
                                       bool_t * no_dispatch);
#endif

#ifdef _HAVE_GSSAPI
void log_sperror_gss(char *outmsg, OM_uint32 maj_stat, OM_uint32 min_stat);
uint32_t gss_ctx_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
uint64_t gss_ctx_rbt_hash_func(hash_parameter_t * p_hparam,
                               hash_buffer_t * buffclef);
int compare_gss_ctx(hash_buffer_t * buff1, hash_buffer_t * buff2);
int display_gss_ctx(hash_buffer_t * pbuff, char *str);
int display_gss_svc_data(hash_buffer_t * pbuff, char *str);
const char *str_gc_proc(rpc_gss_proc_t gc_proc);
#endif                          /* _HAVE_GSSAPI */

#ifdef _USE_TIRPC
#define SOCK_NAME_MAX 128
#else
#define SOCK_NAME_MAX 32
#endif
extern int copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt);
extern int sprint_sockaddr(sockaddr_t *addr, char *buf, int len);
extern int sprint_sockip(sockaddr_t *addr, char *buf, int len);
extern SVCXPRT *Svcxprt_copy(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);
extern SVCXPRT *Svcxprt_copycreate();

extern const char *xprt_type_to_str(xprt_type_t type);

typedef enum _ignore_port
{
	IGNORE_PORT,
	CHECK_PORT
} ignore_port_t;

extern int cmp_sockaddr(sockaddr_t *addr_1,
                        sockaddr_t *addr_2,
                        ignore_port_t ignore_port);
extern unsigned long hash_sockaddr(sockaddr_t *addr,
                                   ignore_port_t ignore_port);

extern in_addr_t get_in_addr(sockaddr_t *addr);
extern int get_port(sockaddr_t *addr);

/* Returns an EAI value, accepts only numeric strings */
extern int ipstring_to_sockaddr(const char *str, sockaddr_t *addr);

extern CLIENT *Clnt_create(char *host,
                           unsigned long prog,
                           unsigned long vers,
                           char *proto);

void Clnt_destroy(CLIENT *clnt);

#endif