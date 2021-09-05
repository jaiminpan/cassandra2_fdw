/*-------------------------------------------------------------------------
 *
 * Cassandra2 Foreign Data Wrapper for PostgreSQL
 *
 * Copyright (c) 2015 Jaimin Pan
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author: Jaimin Pan <jaimin.pan@gmail.com>
 *
 * IDENTIFICATION
 *		cassandra2_fdw/cassandra2_fdw.c
 *
 *-------------------------------------------------------------------------
 */
//#if (PG_VERSION_NUM < 90200)
//# error The file must compiler when pg version large than 90200
//#endif

#include "postgres.h"

#include <cassandra.h>

#include "cassandra2_fdw.h"

#include "access/htup_details.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "executor/spi.h"
#include "foreign/fdwapi.h"
#if PG_VERSION_NUM < 100000
#include "libpq/md5.h"
#else
#include "common/md5.h"
#endif  /* PG_VERSION_NUM */
#include "funcapi.h"
#include "miscadmin.h"
#include "mb/pg_wchar.h"
#include "optimizer/cost.h"
#include "optimizer/restrictinfo.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_user_mapping.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "commands/vacuum.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/pathnode.h"
#if PG_VERSION_NUM >= 130000
#include "optimizer/paths.h"
#endif  /* PG_VERSION_NUM */
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#if PG_VERSION_NUM < 120000
#include "nodes/relation.h"
#include "optimizer/var.h"
#include "utils/tqual.h"
#else
#include "nodes/pathnodes.h"
#include "optimizer/optimizer.h"
#include "access/heapam.h"
#endif  /* PG_VERSION_NUM */
#include "optimizer/paths.h"
#include "parser/parsetree.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

/* defined in backend/commands/analyze.c */
#ifndef WIDTH_THRESHOLD
#define WIDTH_THRESHOLD 1024
#endif  /* WIDTH_THRESHOLD */

#if PG_VERSION_NUM >= 90500
#define IMPORT_API

/* array_create_iterator has a new signature from 9.5 on */
#define array_create_iterator(arr, slice_ndim) array_create_iterator(arr, slice_ndim, NULL)
#else
#undef IMPORT_API
#endif  /* PG_VERSION_NUM */

#if PG_VERSION_NUM >= 90600
#define JOIN_API

/* the useful macro IS_SIMPLE_REL is defined in v10, backport */
#ifndef IS_SIMPLE_REL
#define IS_SIMPLE_REL(rel) \
	((rel)->reloptkind == RELOPT_BASEREL || \
	(rel)->reloptkind == RELOPT_OTHER_MEMBER_REL)
#endif

#if (PG_VERSION_NUM < 110000)
#define TupleDescAttr(tupdesc, i) ((tupdesc)->attrs[(i)])
#else
#define get_relid_attribute_name(relid, varattno) get_attname((relid), (varattno), false)
#endif

/* GetConfigOptionByName has a new signature from 9.6 on */
#define GetConfigOptionByName(name, varname) GetConfigOptionByName(name, varname, false)
#else
#undef JOIN_API
#endif  /* PG_VERSION_NUM */

#if PG_VERSION_NUM < 110000
/* backport macro from V11 */
#define TupleDescAttr(tupdesc, i) ((tupdesc)->attrs[(i)])
#endif  /* PG_VERSION_NUM */

/* list API has changed in v13 */
#if PG_VERSION_NUM < 130000
#define list_next(l, e) lnext((e))
#define do_each_cell(cell, list, element) for_each_cell(cell, (element))
#else
#define list_next(l, e) lnext((l), (e))
#define do_each_cell(cell, list, element) for_each_cell(cell, (list), (element))
#endif  /* PG_VERSION_NUM */

/* "table_open" was "heap_open" before v12 */
#if PG_VERSION_NUM < 120000
#define table_open(x, y) heap_open(x, y)
#define table_close(x, y) heap_close(x, y)
#endif  /* PG_VERSION_NUM */

PG_MODULE_MAGIC;

/* Default CPU cost to start up a foreign query. */
#define DEFAULT_FDW_STARTUP_COST	100.0

/* Default CPU cost to process 1 row (above and beyond cpu_tuple_cost). */
#define DEFAULT_FDW_TUPLE_COST		0.01


/*
 * Describes the valid options for objects that use this wrapper.
 */
struct CassFdwOption
{
	const char	*optname;
	Oid			optcontext;		/* Oid of catalog in which option may appear */
};

/*
 * Valid options for cassandra2_fdw.
 */
static struct CassFdwOption valid_options[] =
{
	/* Connection options */
	{ "host",			ForeignServerRelationId },
	{ "port",			ForeignServerRelationId },
	{ "protocol",		ForeignServerRelationId },
	{ "username",		UserMappingRelationId },
	{ "password",		UserMappingRelationId },
	{ "query",			ForeignTableRelationId },
	{ "schema_name",	ForeignTableRelationId },
	{ "table_name",		ForeignTableRelationId },

	/* Sentinel */
	{ NULL,			InvalidOid }
};

/*
 * FDW-specific information for RelOptInfo.fdw_private.
 */
typedef struct CassFdwPlanState
{
	/* baserestrictinfo clauses, broken down into safe and unsafe subsets. */
	List	   *remote_conds;
	List	   *local_conds;

	/* Bitmap of attr numbers we need to fetch from the remote server. */
	Bitmapset  *attrs_used;

	/* Estimated size and cost for a scan with baserestrictinfo quals. */
	double		rows;
	int			width;
	Cost		startup_cost;
	Cost		total_cost;
} CassFdwPlanState;

/*
 * FDW-specific information for ForeignScanState.fdw_state.
 */
typedef struct CassFdwScanState
{
	Relation	rel;			/* relcache entry for the foreign table */
	AttInMetadata *attinmeta;	/* attribute datatype conversion metadata */

	/* extracted fdw_private data */
	char	   *query;			/* text of SELECT command */
	List	   *retrieved_attrs;	/* list of retrieved attribute numbers */

	int		NumberOfColumns;

	/* for remote query execution */
	CassSession	   *cass_conn;			/* connection for the scan */
	bool			sql_sended;
	CassStatement  *statement;

	/* for storing result tuples */
	HeapTuple  *tuples;			/* array of currently-retrieved tuples */
	int			num_tuples;		/* # of tuples in array */
	int			next_tuple;		/* index of next one to return */

	/* batch-level state, for optimizing rewinds and avoiding useless fetch */
	int			fetch_ct_2;		/* Min(# of fetches done, 2) */
	bool		eof_reached;	/* true if last fetch reached EOF */

	/* working memory contexts */
	MemoryContext batch_cxt;	/* context holding current batch of tuples */
	MemoryContext temp_cxt;		/* context for per-tuple temporary data */
} CassFdwScanState;

enum CassFdwScanPrivateIndex
{
	/* SQL statement to execute remotely (as a String node) */
	CassFdwScanPrivateSelectSql,
	/* Integer list of attribute numbers retrieved by the SELECT */
	CassFdwScanPrivateRetrievedAttrs
};


/*
 * SQL functions
 */
extern Datum cassandra2_fdw_handler(PG_FUNCTION_ARGS);
extern Datum cassandra2_fdw_validator(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(cassandra2_fdw_handler);
PG_FUNCTION_INFO_V1(cassandra2_fdw_validator);


/*
 * FDW callback routines
 */
static void cassGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
static void cassGetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
static ForeignScan *cassGetForeignPlan(
							PlannerInfo *root,
							RelOptInfo *baserel,
							Oid foreigntableid,
							ForeignPath *best_path,
							List *tlist,
							List *scan_clauses
#if (PG_VERSION_NUM >= 90500)
							,
							Plan *outer_plan
#endif
							);
static void cassExplainForeignScan(ForeignScanState *node, ExplainState *es);
static void cassBeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *cassIterateForeignScan(ForeignScanState *node);
static void cassReScanForeignScan(ForeignScanState *node);
static void cassEndForeignScan(ForeignScanState *node);

/*
 * Helper functions
 */
static void estimate_path_cost_size(PlannerInfo *root,
						RelOptInfo *baserel,
						List *join_conds,
						double *p_rows, int *p_width,
						Cost *p_startup_cost, Cost *p_total_cost);
static bool cassIsValidOption(const char *option, Oid context);
static void cassGetOptions(Oid foreigntableid,
				char **host, int *port,
				char **username, char **password,
				char **query, char **tablename);

static void create_cursor(ForeignScanState *node);
static void close_cursor(CassFdwScanState *fsstate);
static void fetch_more_data(ForeignScanState *node);
static void pgcass_transferValue(StringInfo buf, const CassValue* value);
static HeapTuple make_tuple_from_result_row(const CassRow* row,
										   int ncolumn,
										   Relation rel,
										   AttInMetadata *attinmeta,
										   List *retrieved_attrs,
										   MemoryContext temp_context);

static void classifyConditions(PlannerInfo *root,
				   RelOptInfo *baserel,
				   List *input_conds,
				   List **remote_conds,
				   List **local_conds);
static void
deparseSelectSql(StringInfo buf,
				 PlannerInfo *root,
				 RelOptInfo *baserel,
				 Bitmapset *attrs_used,
				 List **retrieved_attrs);

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to my callback routines.
 */
Datum
cassandra2_fdw_handler(PG_FUNCTION_ARGS)
{
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	fdwroutine->GetForeignRelSize = cassGetForeignRelSize;
	fdwroutine->GetForeignPaths = cassGetForeignPaths;
	fdwroutine->GetForeignPlan = cassGetForeignPlan;

	fdwroutine->ExplainForeignScan = cassExplainForeignScan;
	fdwroutine->BeginForeignScan = cassBeginForeignScan;
	fdwroutine->IterateForeignScan = cassIterateForeignScan;
	fdwroutine->ReScanForeignScan = cassReScanForeignScan;
	fdwroutine->EndForeignScan = cassEndForeignScan;
	fdwroutine->AnalyzeForeignTable = NULL;

	PG_RETURN_POINTER(fdwroutine);
}

/*
 * Validate the generic options given to a FOREIGN DATA WRAPPER, SERVER,
 * USER MAPPING or FOREIGN TABLE that uses file_fdw.
 *
 * Raise an ERROR if the option or its value is considered invalid.
 */
Datum
cassandra2_fdw_validator(PG_FUNCTION_ARGS)
{
	List		*options_list = untransformRelOptions(PG_GETARG_DATUM(0));
	Oid			catalog = PG_GETARG_OID(1);
	char		*svr_host = NULL;
	int			svr_port = 0;
	char		*svr_username = NULL;
	char		*svr_password = NULL;
	char		*svr_query = NULL;
	char		*svr_schema = NULL;
	char		*svr_table = NULL;
	ListCell	*cell;

	/*
	 * Check that only options supported by cassandra2_fdw,
	 * and allowed for the current object type, are given.
	*/
	foreach(cell, options_list)
	{
		DefElem    *def = (DefElem *) lfirst(cell);

		if (!cassIsValidOption(def->defname, catalog))
		{
			const struct CassFdwOption *opt;
			StringInfoData buf;

			/*
			 * Unknown option specified, complain about it. Provide a hint
			 * with list of valid options for the object.
			 */
			initStringInfo(&buf);
			for (opt = valid_options; opt->optname; opt++)
			{
				if (catalog == opt->optcontext)
					appendStringInfo(&buf, "%s%s", (buf.len > 0) ? ", " : "",
							 opt->optname);
			}

			ereport(ERROR,
					(errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
					 errmsg("invalid option \"%s\"", def->defname),
					 buf.len > 0
					 ? errhint("Valid options in this context are: %s",
							   buf.data)
				  : errhint("There are no valid options in this context.")));
		}

		if (strcmp(def->defname, "host") == 0)
		{
			if (svr_host)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			svr_host = defGetString(def);
		}
		if (strcmp(def->defname, "port") == 0)
		{
			if (svr_port)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			svr_port = atoi(defGetString(def));
		}
		else if (strcmp(def->defname, "username") == 0)
		{
			if (svr_username)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			svr_username = defGetString(def);
		}
		else if (strcmp(def->defname, "password") == 0)
		{
			if (svr_password)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			svr_password = defGetString(def);
		}
		else if (strcmp(def->defname, "query") == 0)
		{
			if (svr_table)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options: query cannot be used with table")));

			if (svr_query)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));

			svr_query = defGetString(def);
		}
		else if (strcmp(def->defname, "schema_name") == 0)
		{
			if (svr_schema)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));

			svr_schema = defGetString(def);
		}
		else if (strcmp(def->defname, "table_name") == 0)
		{
			if (svr_query)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options: table_name cannot be used with query")));

			if (svr_table)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));

			svr_table = defGetString(def);
		}
	}

	if (catalog == ForeignServerRelationId && svr_host == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("host must be specified")));

	if (catalog == ForeignTableRelationId &&
		svr_query == NULL && svr_table == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("either table_name or query must be specified")));

	PG_RETURN_VOID();
}


/*
 * Check if the provided option is one of the valid options.
 * context is the Oid of the catalog holding the object the option is for.
 */
static bool
cassIsValidOption(const char *option, Oid context)
{
	const struct CassFdwOption *opt;

	for (opt = valid_options; opt->optname; opt++)
	{
		if (context == opt->optcontext && strcmp(opt->optname, option) == 0)
			return true;
	}
	return false;
}


/*
 * Fetch the options for a fdw foreign table.
 */
static void
cassGetOptions(Oid foreigntableid, char **host, int *port,
				char **username, char **password, char **query, char **tablename)
{
	ForeignTable  *table;
	ForeignServer *server;
	UserMapping   *user;
	List	   *options;
	ListCell   *lc;

	/*
	 * Extract options from FDW objects.
	 */
	table = GetForeignTable(foreigntableid);
	server = GetForeignServer(table->serverid);
	user = GetUserMapping(GetUserId(), server->serverid);

	options = NIL;
	options = list_concat(options, table->options);
	options = list_concat(options, server->options);
	options = list_concat(options, user->options);

	/* Loop through the options, and get the server/port */
	foreach(lc, options)
	{
		DefElem *def = (DefElem *) lfirst(lc);

		if (strcmp(def->defname, "username") == 0)
		{
			*username = defGetString(def);
		}
		else if (strcmp(def->defname, "password") == 0)
		{
			*password = defGetString(def);
		}
		else if (strcmp(def->defname, "query") == 0)
		{
			*query = defGetString(def);
		}
		else if (strcmp(def->defname, "table_name") == 0)
		{
			*tablename = defGetString(def);
		}
		else if (strcmp(def->defname, "host") == 0)
		{
			*host = defGetString(def);
		}
		else if (strcmp(def->defname, "port") == 0)
		{
			*port = atoi(defGetString(def));
		}

	}
}

//#if (PG_VERSION_NUM >= 90200)

/*
 * cassGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
static void
cassGetForeignRelSize(PlannerInfo *root,
					  RelOptInfo *baserel,
					  Oid foreigntableid)
{
	CassFdwPlanState *fpinfo;
	ListCell   *lc;

	fpinfo = (CassFdwPlanState *) palloc0(sizeof(CassFdwPlanState));
	baserel->fdw_private = (void *) fpinfo;

	/*
	 * Identify which baserestrictinfo clauses can be sent to the remote
	 * server and which can't.
	 */
	classifyConditions(root, baserel, baserel->baserestrictinfo,
					   &fpinfo->remote_conds, &fpinfo->local_conds);

	fpinfo->attrs_used = NULL;
#if (PG_VERSION_NUM >= 90600)
	pull_varattnos((Node *) baserel->reltarget->exprs, baserel->relid,
				   &fpinfo->attrs_used);
#else
	pull_varattnos((Node *) baserel->reltargetlist, baserel->relid,
				   &fpinfo->attrs_used);
#endif
	foreach(lc, fpinfo->local_conds)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		pull_varattnos((Node *) rinfo->clause, baserel->relid,
					   &fpinfo->attrs_used);
	}

	//TODO
	/* Fetch options  */

	/* Estimate relation size */
	{
		/*
		 * If the foreign table has never been ANALYZEd, it will have relpages
		 * and reltuples equal to zero, which most likely has nothing to do
		 * with reality.  We can't do a whole lot about that if we're not
		 * allowed to consult the remote server, but we can use a hack similar
		 * to plancat.c's treatment of empty relations: use a minimum size
		 * estimate of 10 pages, and divide by the column-datatype-based width
		 * estimate to get the corresponding number of tuples.
		 */
		if (baserel->pages == 0 && baserel->tuples == 0)
		{
			baserel->pages = 10;
			baserel->tuples =
#if (PG_VERSION_NUM >= 90600)
				(10 * BLCKSZ) / (baserel->reltarget->width + sizeof(HeapTupleHeaderData));
#else
				(10 * BLCKSZ) / (baserel->width + sizeof(HeapTupleHeaderData));
#endif
		}

		/* Estimate baserel size as best we can with local statistics. */
		set_baserel_size_estimates(root, baserel);

		/* Fill in basically-bogus cost estimates for use later. */
		estimate_path_cost_size(root, baserel, NIL,
								&fpinfo->rows, &fpinfo->width,
								&fpinfo->startup_cost, &fpinfo->total_cost);
	}
}

static void
estimate_path_cost_size(PlannerInfo *root,
						RelOptInfo *baserel,
						List *join_conds,
						double *p_rows, int *p_width,
						Cost *p_startup_cost, Cost *p_total_cost)
{
	*p_rows = baserel->rows;
#if (PG_VERSION_NUM >= 90600)
	*p_width = baserel->reltarget->width;
#else
	*p_width = baserel->width;
#endif

	*p_startup_cost = DEFAULT_FDW_STARTUP_COST;
	*p_total_cost = DEFAULT_FDW_TUPLE_COST * 100;
}

/*
 * cassGetForeignPaths
 *		(9.2+) Get the foreign paths
 */
static void
cassGetForeignPaths(PlannerInfo *root,
					RelOptInfo *baserel,
					Oid foreigntableid)
{
	CassFdwPlanState *fpinfo = (CassFdwPlanState *) baserel->fdw_private;
	ForeignPath *path;

	/*
	 * Create simplest ForeignScan path node and add it to baserel.  This path
	 * corresponds to SeqScan path of regular tables (though depending on what
	 * baserestrict conditions we were able to send to remote, there might
	 * actually be an indexscan happening there).  We already did all the work
	 * to estimate cost and size of this path.
	 */
	path = create_foreignscan_path(root, baserel,
#if (PG_VERSION_NUM >= 10000)
								   NULL,
#endif
								   fpinfo->rows + baserel->rows,
								   fpinfo->startup_cost,
								   fpinfo->total_cost,
								   NIL, /* no pathkeys */
								   NULL,		/* no outer rel either */
#if (PG_VERSION_NUM >= 90500)
								   NULL,		/* no fdw_outerpath */
#endif
								   NIL);		/* no fdw_private list */

	add_path(baserel, (Path *) path);

	//TODO
}

/*
 * cassGetForeignPlan
 *		Create ForeignScan plan node which implements selected best path
 */
static ForeignScan *
cassGetForeignPlan(PlannerInfo *root,
					   RelOptInfo *baserel,
					   Oid foreigntableid,
					   ForeignPath *best_path,
					   List *tlist,
					   List *scan_clauses
#if (PG_VERSION_NUM >= 90500)
					   ,
					   Plan *outer_plan
#endif
					   )
{
	CassFdwPlanState *fpinfo = (CassFdwPlanState *) baserel->fdw_private;
	Index		scan_relid = baserel->relid;
	List	   *fdw_private;
	List	   *local_exprs = NIL;
	StringInfoData sql;
	List	   *retrieved_attrs;

	local_exprs = extract_actual_clauses(scan_clauses, false);

	/*
	 * Build the query string to be sent for execution, and identify
	 * expressions to be sent as parameters.
	 */
	initStringInfo(&sql);
	deparseSelectSql(&sql, root, baserel, fpinfo->attrs_used,
					 &retrieved_attrs);

	/*
	 * Build the fdw_private list that will be available to the executor.
	 * Items in the list must match enum CassFdwScanPrivateIndex, above.
	 */
	fdw_private = list_make2(makeString(sql.data),
							 retrieved_attrs);

	/*
	 * Create the ForeignScan node from target list, local filtering
	 * expressions, remote parameter expressions, and FDW private information.
	 *
	 * Note that the remote parameter expressions are stored in the fdw_exprs
	 * field of the finished plan node; we can't keep them in private state
	 * because then they wouldn't be subject to later planner processing.
	 */
	return make_foreignscan(tlist,
							local_exprs,
							scan_relid,
							NIL,
							fdw_private
#if (PG_VERSION_NUM >= 90500)
							,
							NIL,		/* no custom tlist */
							NIL,		/* no remote quals */
							outer_plan
#endif
							);
}

//#endif /* #if (PG_VERSION_NUM >= 90200) */

/*
 * cassExplainForeignScan
 *		Produce extra output for EXPLAIN
 */
static void
cassExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
	List	   *fdw_private;
	char	   *sql;

	char	*svr_host = NULL;
	int		svr_port = 0;
	char	*svr_username = NULL;
	char	*svr_password = NULL;
	char	*svr_query = NULL;
	char	*svr_table = NULL;

	if (es->verbose)
	{
		/* Fetch options  */
		cassGetOptions(RelationGetRelid(node->ss.ss_currentRelation),
						&svr_host, &svr_port,
						&svr_username, &svr_password,
						&svr_query, &svr_table);

		fdw_private = ((ForeignScan *) node->ss.ps.plan)->fdw_private;
		sql = strVal(list_nth(fdw_private, CassFdwScanPrivateSelectSql));
		ExplainPropertyText("Remote SQL", sql, es);
	}
}


/*
 * cassBeginForeignScan
 *		Initiate access to the database
 */
static void
cassBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *fsplan = (ForeignScan *) node->ss.ps.plan;
	EState	   *estate = node->ss.ps.state;
	CassFdwScanState   *fsstate;
	RangeTblEntry *rte;
	Oid			userid;
	ForeignTable *table;
	ForeignServer *server;
	UserMapping *user;

	/*
	 * Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL.
	 */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	/*
	 * We'll save private state in node->fdw_state.
	 */
	fsstate = (CassFdwScanState *) palloc0(sizeof(CassFdwScanState));
	node->fdw_state = (void *) fsstate;

	/*
	 * Identify which user to do the remote access as.  This should match what
	 * ExecCheckRTEPerms() does.
	 */
	rte = rt_fetch(fsplan->scan.scanrelid, estate->es_range_table);
	userid = rte->checkAsUser ? rte->checkAsUser : GetUserId();

	/* Get info about foreign table. */
	fsstate->rel = node->ss.ss_currentRelation;
	table = GetForeignTable(RelationGetRelid(fsstate->rel));
	server = GetForeignServer(table->serverid);
	user = GetUserMapping(userid, server->serverid);

	/*
	 * Get connection to the foreign server.  Connection manager will
	 * establish new connection if necessary.
	 */
	fsstate->cass_conn = pgcass_GetConnection(server, user, false);
	fsstate->sql_sended = false;

	/* Get private info created by planner functions. */
	fsstate->query = strVal(list_nth(fsplan->fdw_private,
									 CassFdwScanPrivateSelectSql));
	fsstate->retrieved_attrs = (List *) list_nth(fsplan->fdw_private,
											   CassFdwScanPrivateRetrievedAttrs);

	/* Create contexts for batches of tuples and per-tuple temp workspace. */
	fsstate->batch_cxt = AllocSetContextCreate(estate->es_query_cxt,
											   "cassandra2_fdw tuple data",
											   ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);

	fsstate->temp_cxt = AllocSetContextCreate(estate->es_query_cxt,
											  "cassandra2_fdw temporary data",
											  ALLOCSET_SMALL_MINSIZE,
											  ALLOCSET_SMALL_INITSIZE,
											  ALLOCSET_SMALL_MAXSIZE);

	/* Get info we'll need for input data conversion. */
	fsstate->attinmeta = TupleDescGetAttInMetadata(RelationGetDescr(fsstate->rel));
}


/*
 * cassIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot*
cassIterateForeignScan(ForeignScanState *node)
{
	CassFdwScanState *fsstate = (CassFdwScanState *) node->fdw_state;
	TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;

	/*
	 * If this is the first call after Begin or ReScan, we need to create the
	 * cursor on the remote side.
	 */
	if (!fsstate->sql_sended)
		create_cursor(node);

	/*
	 * Get some more tuples, if we've run out.
	 */
	if (fsstate->next_tuple >= fsstate->num_tuples)
	{
		/* No point in another fetch if we already detected EOF, though. */
		if (!fsstate->eof_reached)
			fetch_more_data(node);
		/* If we didn't get any tuples, must be end of data. */
		if (fsstate->next_tuple >= fsstate->num_tuples)
			return ExecClearTuple(slot);
	}

	/*
	 * Return the next tuple.
	 */
#if (PG_VERSION_NUM < 120000)
	ExecStoreTuple(fsstate->tuples[fsstate->next_tuple++],
				   slot,
				   InvalidBuffer,
				   false);
#else
	ExecStoreHeapTuple(fsstate->tuples[fsstate->next_tuple++],
					   slot,
					   false);
#endif

	return slot;
}

/*
 * cassReScanForeignScan
 *		Rescan table, possibly with new parameters
 */
static void
cassReScanForeignScan(ForeignScanState *node)
{
	CassFdwScanState *fsstate = (CassFdwScanState *) node->fdw_state;

	/* If we haven't created the cursor yet, nothing to do. */
	if (!fsstate->sql_sended)
		return;

	{
		/* Easy: just rescan what we already have in memory, if anything */
		fsstate->next_tuple = 0;
		return;
	}

	/* Now force a fresh FETCH. */
	fsstate->tuples = NULL;
	fsstate->num_tuples = 0;
	fsstate->next_tuple = 0;
	fsstate->fetch_ct_2 = 0;
	fsstate->eof_reached = false;
}

/*
 * cassEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void
cassEndForeignScan(ForeignScanState *node)
{
	CassFdwScanState *fsstate = (CassFdwScanState *) node->fdw_state;

	/* if fsstate is NULL, we are in EXPLAIN; nothing to do */
	if (fsstate == NULL)
		return;

	/* Close the cursor if open, to prevent accumulation of cursors */
	if (fsstate->sql_sended)
		close_cursor(fsstate);

	/* Release remote connection */
	pgcass_ReleaseConnection(fsstate->cass_conn);
	fsstate->cass_conn = NULL;

	/* MemoryContexts will be deleted automatically. */
}


/*
 * Create cursor for node's query with current parameter values.
 */
static void
create_cursor(ForeignScanState *node)
{
	CassFdwScanState *fsstate = (CassFdwScanState *) node->fdw_state;

	/* Build statement and execute query */
	fsstate->statement = cass_statement_new(fsstate->query, 0);

	/* Mark the cursor as created, and show no tuples have been retrieved */
	fsstate->sql_sended = true;
	fsstate->tuples = NULL;
	fsstate->num_tuples = 0;
	fsstate->next_tuple = 0;
	fsstate->fetch_ct_2 = 0;
	fsstate->eof_reached = false;
}

/*
 * Utility routine to close a cursor.
 */
static void
close_cursor(CassFdwScanState *fsstate)
{
	if (fsstate->statement)
		cass_statement_free(fsstate->statement);
}

/*
 * Fetch some more rows from the node's cursor.
 */
static void
fetch_more_data(ForeignScanState *node)
{
	CassFdwScanState *fsstate = (CassFdwScanState *) node->fdw_state;

	/*
	 * We'll store the tuples in the batch_cxt.  First, flush the previous
	 * batch.
	 */
	fsstate->tuples = NULL;
	MemoryContextReset(fsstate->batch_cxt);
	MemoryContextSwitchTo(fsstate->batch_cxt);

	{
		CassFuture* result_future = cass_session_execute(fsstate->cass_conn, fsstate->statement);
		if (cass_future_error_code(result_future) == CASS_OK)
		{
			const CassResult* res;
			int			numrows;
			CassIterator* rows;
			int			k;

			/* Retrieve result set and iterate over the rows */
			res = cass_future_get_result(result_future);

			/* Stash away the state info we have already */
			fsstate->NumberOfColumns = cass_result_column_count(res);

			/* Convert the data into HeapTuples */
			numrows = cass_result_row_count(res);
			fsstate->tuples = (HeapTuple *) palloc0(numrows * sizeof(HeapTuple));
			fsstate->num_tuples = numrows;
			fsstate->next_tuple = 0;

			rows = cass_iterator_from_result(res);
			k = 0;
			while (cass_iterator_next(rows))
			{
				const CassRow* row = cass_iterator_get_row(rows);

				fsstate->tuples[k] = make_tuple_from_result_row(row,
															fsstate->NumberOfColumns,
															fsstate->rel,
															fsstate->attinmeta,
															fsstate->retrieved_attrs,
															fsstate->temp_cxt);

				Assert(k < numrows);
				k++;
			}

			fsstate->eof_reached = true;

			cass_result_free(res);
			cass_iterator_free(rows);
		}
		else
		{
			/* On error, report the original query. */
			pgcass_report_error(ERROR, result_future, true, fsstate->query);

			fsstate->eof_reached = true;
		}

		cass_future_free(result_future);
	}
}

static HeapTuple
make_tuple_from_result_row(const CassRow* row,
						   int ncolumn,
						   Relation rel,
						   AttInMetadata *attinmeta,
						   List *retrieved_attrs,
						   MemoryContext temp_context)
{
	HeapTuple	tuple;
	TupleDesc	tupdesc = RelationGetDescr(rel);
	Datum	   *values;
	bool	   *nulls;
	MemoryContext oldcontext;
	ListCell   *lc;
	int			j;
	StringInfoData buf;

	/*
	 * Do the following work in a temp context that we reset after each tuple.
	 * This cleans up not only the data we have direct access to, but any
	 * cruft the I/O functions might leak.
	 */
	oldcontext = MemoryContextSwitchTo(temp_context);

	values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	/* Initialize to nulls for any columns not present in result */
	memset(nulls, true, tupdesc->natts * sizeof(bool));

	initStringInfo(&buf);

	/*
	 * i indexes columns in the relation, j indexes columns in the PGresult.
	 */
	j = 0;
	foreach(lc, retrieved_attrs)
	{
		int			i = lfirst_int(lc);
		const char	   *valstr;

		const CassValue* cassVal = cass_row_get_column(row, j);
		if (cass_true == cass_value_is_null(cassVal))
			valstr = NULL;
		else
		{
			pgcass_transferValue(&buf, cassVal);
			valstr = buf.data;
		}

		if (i > 0)
		{
			/* ordinary column */
			Assert(i <= tupdesc->natts);
			nulls[i - 1] = (valstr == NULL);
			/* Apply the input function even to nulls, to support domains */
			values[i - 1] = InputFunctionCall(&attinmeta->attinfuncs[i - 1],
											  (char *) valstr,
											  attinmeta->attioparams[i - 1],
											  attinmeta->atttypmods[i - 1]);
		}

		resetStringInfo(&buf);

		j++;
	}

	/*
	 * Check we got the expected number of columns.  Note: j == 0 and
	 * PQnfields == 1 is expected, since deparse emits a NULL if no columns.
	 */
	if (j > 0 && j != ncolumn)
		elog(ERROR, "remote query result does not match the foreign table");

	/*
	 * Build the result tuple in caller's memory context.
	 */
	MemoryContextSwitchTo(oldcontext);

	tuple = heap_form_tuple(tupdesc, values, nulls);

	/* Clean up */
	MemoryContextReset(temp_context);

	return tuple;
}

static void
pgcass_transferValue(StringInfo buf, const CassValue* value)
{
	CassValueType type = cass_value_type(value);
	switch (type)
	{
	case CASS_VALUE_TYPE_SMALL_INT:
	case CASS_VALUE_TYPE_TINY_INT:
	{
		cass_int32_t i;
		cass_value_get_int16(value, &i);
		appendStringInfo(buf, "%d", i);
		break;
	}
	case CASS_VALUE_TYPE_INT:
	{
		cass_int32_t i;
		cass_value_get_int32(value, &i);
		appendStringInfo(buf, "%d", i);
		break;
	}
	case CASS_VALUE_TYPE_BIGINT:
	case CASS_VALUE_TYPE_TIMESTAMP:
	{
		cass_int64_t i;
		cass_value_get_int64(value, &i);
		appendStringInfo(buf, "%ld ", i);
		break;
	}
	case CASS_VALUE_TYPE_BOOLEAN:
	{
		cass_bool_t b;
		cass_value_get_bool(value, &b);
		appendStringInfoString(buf, b ? "true" : "false");
		break;
	}
	case CAS_VALUE_FLOAT:
	{
		cass_value_get_float(value, &d);
		appendStringInfo(buf, "%f", d);
		break;
	}
	case CASS_VALUE_TYPE_DOUBLE:
	{
		cass_double_t d;
		cass_value_get_double(value, &d);
		appendStringInfo(buf, "%f", d);
		break;
	}

	case CASS_VALUE_TYPE_TEXT:
	case CASS_VALUE_TYPE_ASCII:
	case CASS_VALUE_TYPE_VARCHAR:
	case CASS_VALUE_TYPE_SET:
	{
		const char* s;
		size_t s_length;
		cass_value_get_string(value, &s, &s_length);
		appendStringInfo(buf, "%.*s", (int)s_length, s);
		break;
	}
	case CASS_VALUE_TYPE_UUID:
	{
		CassUuid u;

		cass_value_get_uuid(value, &u);
		cass_uuid_string(u, buf->data + buf->len);
		buf->len += CASS_UUID_STRING_LENGTH;
		buf->data[buf->len] = '\0';
		break;
	}
	case CASS_VALUE_TYPE_LIST:
	case CASS_VALUE_TYPE_MAP:
	default:
		appendStringInfoString(buf, "<unhandled type>");
		break;
	}
}


static void deparseTargetList(StringInfo buf,
				  PlannerInfo *root,
				  Index rtindex,
				  Relation rel,
				  Bitmapset *attrs_used,
				  List **retrieved_attrs);
static void deparseColumnRef(StringInfo buf, int varno, int varattno,
				 PlannerInfo *root);
static void deparseRelation(StringInfo buf, Relation rel);

/*
 * Examine each qual clause in input_conds, and classify them into two groups,
 * which are returned as two lists:
 *	- remote_conds contains expressions that can be evaluated remotely
 *	- local_conds contains expressions that can't be evaluated remotely
 */
static void
classifyConditions(PlannerInfo *root,
				   RelOptInfo *baserel,
				   List *input_conds,
				   List **remote_conds,
				   List **local_conds)
{
	ListCell   *lc;

	*remote_conds = NIL;
	*local_conds = NIL;

	foreach(lc, input_conds)
	{
		RestrictInfo *ri = (RestrictInfo *) lfirst(lc);

		*local_conds = lappend(*local_conds, ri);
	}
}

/*
 * Construct a simple SELECT statement that retrieves desired columns
 * of the specified foreign table, and append it to "buf".  The output
 * contains just "SELECT ... FROM tablename".
 *
 * We also create an integer List of the columns being retrieved, which is
 * returned to *retrieved_attrs.
 */
static void
deparseSelectSql(StringInfo buf,
				 PlannerInfo *root,
				 RelOptInfo *baserel,
				 Bitmapset *attrs_used,
				 List **retrieved_attrs)
{
	RangeTblEntry *rte = planner_rt_fetch(baserel->relid, root);
	Relation	rel;

	/*
	 * Core code already has some lock on each rel being planned, so we can
	 * use NoLock here.
	 */
	rel = table_open(rte->relid, NoLock);

	/*
	 * Construct SELECT list
	 */
	appendStringInfoString(buf, "SELECT ");
	deparseTargetList(buf, root, baserel->relid, rel, attrs_used,
					  retrieved_attrs);

	/*
	 * Construct FROM clause
	 */
	appendStringInfoString(buf, " FROM ");
	deparseRelation(buf, rel);

#if PG_VERSION_NUM < 120000
	heap_close(rel, NoLock);
#endif
}


/*
 * Emit a target list that retrieves the columns specified in attrs_used.
 * This is used for both SELECT and RETURNING targetlists.
 *
 * The tlist text is appended to buf, and we also create an integer List
 * of the columns being retrieved, which is returned to *retrieved_attrs.
 */
static void
deparseTargetList(StringInfo buf,
				  PlannerInfo *root,
				  Index rtindex,
				  Relation rel,
				  Bitmapset *attrs_used,
				  List **retrieved_attrs)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	bool		have_wholerow;
	bool		first;
	int			i;

	*retrieved_attrs = NIL;

	/* If there's a whole-row reference, we'll need all the columns. */
	have_wholerow = bms_is_member(0 - FirstLowInvalidHeapAttributeNumber,
								  attrs_used);

	first = true;
	for (i = 1; i <= tupdesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i - 1);

		/* Ignore dropped attributes. */
		if (attr->attisdropped)
			continue;

		if (have_wholerow ||
			bms_is_member(i - FirstLowInvalidHeapAttributeNumber,
						  attrs_used))
		{
			if (!first)
				appendStringInfoString(buf, ", ");
			first = false;

			deparseColumnRef(buf, rtindex, i, root);

			*retrieved_attrs = lappend_int(*retrieved_attrs, i);
		}
	}

	/*
	 * Add ctid if needed.  We currently don't support retrieving any other
	 * system columns.
	 */
	if (bms_is_member(SelfItemPointerAttributeNumber - FirstLowInvalidHeapAttributeNumber,
					  attrs_used))
	{
		if (!first)
			appendStringInfoString(buf, ", ");
		first = false;

		appendStringInfoString(buf, "ctid");

		*retrieved_attrs = lappend_int(*retrieved_attrs,
									   SelfItemPointerAttributeNumber);
	}

	/* Don't generate bad syntax if no undropped columns */
	if (first)
		appendStringInfoString(buf, "NULL");
}

/*
 * Construct name to use for given column, and emit it into buf.
 * If it has a column_name FDW option, use that instead of attribute name.
 */
static void
deparseColumnRef(StringInfo buf, int varno, int varattno, PlannerInfo *root)
{
	RangeTblEntry *rte;
	char	   *colname = NULL;
	List	   *options;
	ListCell   *lc;

	/* varno must not be any of OUTER_VAR, INNER_VAR and INDEX_VAR. */
	Assert(!IS_SPECIAL_VARNO(varno));

	/* Get RangeTblEntry from array in PlannerInfo. */
	rte = planner_rt_fetch(varno, root);

	/*
	 * If it's a column of a foreign table, and it has the column_name FDW
	 * option, use that value.
	 */
	options = GetForeignColumnOptions(rte->relid, varattno);
	foreach(lc, options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (strcmp(def->defname, "column_name") == 0)
		{
			colname = defGetString(def);
			break;
		}
	}

	/*
	 * If it's a column of a regular table or it doesn't have column_name FDW
	 * option, use attribute name.
	 */
	if (colname == NULL)
		colname = get_relid_attribute_name(rte->relid, varattno);

	appendStringInfoString(buf, quote_identifier(colname));
}


/*
 * Append remote name of specified foreign table to buf.
 * Use value of table_name FDW option (if any) instead of relation's name.
 * Similarly, schema_name FDW option overrides schema name.
 */
static void
deparseRelation(StringInfo buf, Relation rel)
{
	ForeignTable *table;
	const char *nspname = NULL;
	const char *relname = NULL;
	ListCell   *lc;

	/* obtain additional catalog information. */
	table = GetForeignTable(RelationGetRelid(rel));

	/*
	 * Use value of FDW options if any, instead of the name of object itself.
	 */
	foreach(lc, table->options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (strcmp(def->defname, "schema_name") == 0)
			nspname = defGetString(def);
		else if (strcmp(def->defname, "table_name") == 0)
			relname = defGetString(def);
	}

	/*
	 * Note: we could skip printing the schema name if it's pg_catalog, but
	 * that doesn't seem worth the trouble.
	 */
	if (nspname == NULL)
		nspname = get_namespace_name(RelationGetNamespace(rel));
	if (relname == NULL)
		relname = RelationGetRelationName(rel);

	appendStringInfo(buf, "%s.%s",
					 quote_identifier(nspname), quote_identifier(relname));
}
