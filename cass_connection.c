/*-------------------------------------------------------------------------
 *
 * cass_connection.c
 *
 *  Created on: Aug 8, 2015
 *      Author: jaimin
 *
 * IDENTIFICATION
 *		  contrib/cassandra2_fdw/cass_connection.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "cassandra2_fdw.h"

#include "access/hash.h"
#include "access/xact.h"
#include "commands/defrem.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"


typedef struct ConnCacheKey
{
	Oid			serverid;		/* OID of foreign server */
	Oid			userid;			/* OID of local user whose mapping we use */
} ConnCacheKey;

typedef struct ConnCacheEntry
{
	ConnCacheKey key;			/* hash key (must be first) */
	CassSession *conn;			/* connection to foreign server, or NULL */
	int			xact_depth;		/* 0 = no xact open, 1 = main xact open, 2 =
								 * one level of subxact open, etc */
	bool		have_prep_stmt; /* have we prepared any stmts in this xact? */
	bool		have_error;		/* have any subxacts aborted in this xact? */
} ConnCacheEntry;

/*
 * Connection cache (initialized on first use)
 */
static HTAB *ConnectionHash = NULL;

/* tracks whether any work is needed in callback functions */
static bool xact_got_connection = false;

/* prototypes of private functions */
static CassSession *connect_cass_server(ForeignServer *server, UserMapping *user);
static int ExtractOptions(List *defelems, const char **keywords,
						 const char **values);


static CassCluster* cluster;

static void pgcass_close(int code, Datum arg);


CassSession *
pgcass_GetConnection(ForeignServer *server, UserMapping *user,
			  bool will_prep_stmt)
{
	bool		found;
	ConnCacheEntry *entry;
	ConnCacheKey key;

	/* First time through, initialize connection cache hashtable */
	if (ConnectionHash == NULL)
	{
		HASHCTL		ctl;

		MemSet(&ctl, 0, sizeof(ctl));
		ctl.keysize = sizeof(ConnCacheKey);
		ctl.entrysize = sizeof(ConnCacheEntry);
		ctl.hash = tag_hash;
		/* allocate ConnectionHash in the cache context */
		ctl.hcxt = CacheMemoryContext;
		ConnectionHash = hash_create("cassandra2_fdw connections", 8,
									 &ctl,
								   HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

		/*
		 * Register some callback functions that manage connection cleanup.
		 * This should be done just once in each backend.
		 */
//		RegisterXactCallback(pgfdw_xact_callback, NULL);
//		RegisterSubXactCallback(pgfdw_subxact_callback, NULL);
	}

	/* Set flag that we did GetConnection during the current transaction */
	xact_got_connection = true;

	/* Create hash key for the entry.  Assume no pad bytes in key struct */
	key.serverid = server->serverid;
	key.userid = user->userid;

	/*
	 * Find or create cached entry for requested connection.
	 */
	entry = hash_search(ConnectionHash, &key, HASH_ENTER, &found);
	if (!found)
	{
		/* initialize new hashtable entry (key is already filled in) */
		entry->conn = NULL;
		entry->xact_depth = 0;
		entry->have_prep_stmt = false;
		entry->have_error = false;
	}

	/*
	 * We don't check the health of cached connection here, because it would
	 * require some overhead.  Broken connection will be detected when the
	 * connection is actually used.
	 */

	/*
	 * If cache entry doesn't have a connection, we have to establish a new
	 * connection.  (If connect_pg_server throws an error, the cache entry
	 * will be left in a valid empty state.)
	 */
	if (entry->conn == NULL)
	{
		entry->xact_depth = 0;	/* just to be sure */
		entry->have_prep_stmt = false;
		entry->have_error = false;
		entry->conn = connect_cass_server(server, user);
		elog(DEBUG3, "new cassandra2_fdw connection %p for server \"%s\"",
			 entry->conn, server->servername);
	}

	/*
	 * do nothing for connection pre-cmd execute till now.
	 */

	/* Remember if caller will prepare statements */
	entry->have_prep_stmt |= will_prep_stmt;

	return entry->conn;
}

/*
 * Release connection reference count created by calling GetConnection.
 */
void
pgcass_ReleaseConnection(CassSession *session)
{
	/*
	 * Currently, we don't actually track connection references because all
	 * cleanup is managed on a transaction or subtransaction basis instead. So
	 * there's nothing to do here.
	 */
	CassFuture* close_future = NULL;

	return;

	/* Close the session */
	close_future = cass_session_close(session);
	cass_future_wait(close_future);
	cass_future_free(close_future);
}

/*
 * Connect to remote server using specified server and user mapping properties.
 */
static CassSession *
connect_cass_server(ForeignServer *server, UserMapping *user)
{
	CassFuture* conn_future = NULL;
	CassSession* session = NULL;

	if (!cluster)
	{
		cluster = cass_cluster_new();
		on_proc_exit(pgcass_close, 0);
	}

	/*
	 * Use PG_TRY block to ensure closing connection on error.
	 */
	PG_TRY();
	{
		const char **keywords;
		const char **values;
		int			n;
		int			i;
		const char	*svr_host = NULL;
		int			svr_port = 0;
		int			svr_proto = 0;
		const char	*svr_username = NULL;
		const char	*svr_password = NULL;

		/*
		 * Construct connection params from generic options of ForeignServer
		 * and UserMapping.
		 */
		n = list_length(server->options) + list_length(user->options) + 1;
		keywords = (const char **) palloc(n * sizeof(char *));
		values = (const char **) palloc(n * sizeof(char *));

		n = 0;
		n += ExtractOptions(server->options,
							keywords + n, values + n);
		n += ExtractOptions(user->options,
							keywords + n, values + n);

		keywords[n] = values[n] = NULL;

		/* Add contact points */
		for (i = 0; keywords[i] != NULL; i++)
		{
			if (strcmp(keywords[i], "host") == 0)
			{
				svr_host = values[i];
			}
			else if (strcmp(keywords[i], "port") == 0)
			{
				svr_port = atoi(values[i]);
			}
			else if (strcmp(keywords[i], "protocol") == 0)
			{
				svr_proto = atoi(values[i]);
			}
			else if (strcmp(keywords[i], "username") == 0)
			{
				svr_username = values[i];
			}
			else if (strcmp(keywords[i], "password") == 0)
			{
				svr_password = values[i];
			}
		}

		if (svr_host)
			cass_cluster_set_contact_points(cluster, svr_host);
		if (svr_port)
			cass_cluster_set_port(cluster, svr_port);
		if (svr_proto)
			cass_cluster_set_protocol_version(cluster, svr_proto);
		if (svr_username && svr_password)
			cass_cluster_set_credentials(cluster, svr_username, svr_password);

		session = cass_session_new();

		/* Provide the cluster object as configuration to connect the session */
		conn_future = cass_session_connect(session, cluster);
		if (cass_future_error_code(conn_future) != CASS_OK)
		{
			/* Handle error */
			const char* message;
			size_t message_length;

			cass_future_error_message(conn_future, &message, &message_length);

			ereport(ERROR,
					(errcode(ERRCODE_SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION),
							errmsg("could not connect to server \"%s\"",
									server->servername),
					errdetail_internal("%.*s", (int)message_length, message)));
		}

		cass_future_free(conn_future);

		pfree(keywords);
		pfree(values);
	}
	PG_CATCH();
	{
		/* Release data structure if we managed to create one */
		if (conn_future)
			cass_future_free(conn_future);

		if (session)
			cass_session_free(session);
		PG_RE_THROW();
	}
	PG_END_TRY();

	return session;
}


/*
 * pgcass_close
 *		Shuts down the Connection.
 */
static void
pgcass_close(int code, Datum arg)
{
	cass_cluster_free(cluster);
}


/*
 * Report an error we got from the remote server.
 *
 * elevel: error level to use (typically ERROR, but might be less)
 * clear: if true, clear the result (otherwise caller will handle it)
 * sql: NULL, or text of remote command we tried to execute
 *
 * Note: callers that choose not to throw ERROR for a remote error are
 * responsible for making sure that the associated ConnCacheEntry gets
 * marked with have_error = true.
 */
void
pgcass_report_error(int elevel, CassFuture* result_future,
				    bool clear, const char *sql)
{
	PG_TRY();
	{
		const char	   *message_primary = NULL;
		const char	   *message_detail = NULL;
		const char	   *message_hint =  NULL;
		const char	   *message_context = NULL;
		int			sqlstate;
		size_t		message_length;

		sqlstate = ERRCODE_CONNECTION_FAILURE;

		cass_future_error_message(result_future, &message_primary, &message_length);

		ereport(elevel,
				(errcode(sqlstate),
				 message_primary ? errmsg_internal("%s", message_primary) :
				 errmsg("unknown error"),
			   message_detail ? errdetail_internal("%s", message_detail) : 0,
				 message_hint ? errhint("%s", message_hint) : 0,
				 message_context ? errcontext("%s", message_context) : 0,
				 sql ? errcontext("Remote SQL command: %s", sql) : 0));
	}
	PG_CATCH();
	{
		if (clear)
			cass_future_free(result_future);
		PG_RE_THROW();
	}
	PG_END_TRY();
	if (clear)
		cass_future_free(result_future);
}

/*
 * Generate key-value arrays which include only libpq options from the
 * given list (which can contain any kind of options).  Caller must have
 * allocated large-enough arrays.  Returns number of options found.
 */
static int
ExtractOptions(List *defelems, const char **keywords,
						 const char **values)
{
	ListCell   *lc;
	int			i;

	i = 0;
	foreach(lc, defelems)
	{
		DefElem    *d = (DefElem *) lfirst(lc);

		keywords[i] = d->defname;
		values[i] = defGetString(d);
		i++;
	}
	return i;
}
