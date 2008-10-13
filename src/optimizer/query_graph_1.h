/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * env.h - Query environment scaffolding
 * TODO: merge this file to query_graph_2.h
 *
 * Note:
 */

#ifndef _ENV_H_
#define _ENV_H_

#ident "$Id$"

#include <setjmp.h>

#include "work_space.h"
#include "qo.h"
#include "parser.h"
#include "bitset.h"

/*
 *  This structure is the head of a list of QO_INDEX_ENTRY index structures.
 *  The purpose for this node is to have a place to store cumulative
 *  statistics of the indexes on the list and to store the list pointer.
 */
struct qo_node_index_entry
{
  /*
   *  Number of classes on the list (depth of the list).
   */
  int n;

  /* cumulative stats for all indexes in this list */
  QO_ATTR_CUM_STATS cum_stats;

  /*
   *  Pointer to a linked list of compatible index nodes.
   */
  QO_INDEX_ENTRY *head;
};

/*
 *  Contains pointers to usable indexes which span class heirarchies.
 *  Each element in the array contains an index header which consists
 *  of statistical information and a pointer to a list of compatible
 *  index structures where each node of the list represents an index
 *  at each level in the class heirarchy.
 */
struct qo_node_index
{
  /*
   *  Number of usable indexes (size of the array).
   */
  int n;

  /*
   *  Array of usable indexes
   */
  struct qo_node_index_entry index[1];
};

#define QO_NI_N(ni)		((ni)->n)
#define QO_NI_ENTRY(ni, n)	(&(ni)->index[(n)])


/* index names for the node specfied in USING INDEX clause */
struct qo_using_index
{
  int n;			/* number of indexes (size of the array) */
  /* 0 if USING INDEX NONE in the query */
  struct
  {
    const char *name;
    int force;
  } index[1];			/* array of index names */
};				/* struct QO_USING_INDEX */

#define QO_UI_N(ui)         ((ui)->n)
#define QO_UI_INDEX(ui, n)  ((ui)->index[(n)].name)
#define QO_UI_FORCE(ui, n)  ((ui)->index[(n)].force)


struct qo_node
{
  /*
   * A node in the join graph.  Each node stands for a class (or a
   * collection of classes that are conceptually summed together) that
   * must be examined in order to produce the final result.  Notice
   * that the same class can serve as the basis for a node if it is
   * somehow joined with itself.  For example, the queries
   *
   *  SELECT name, spouse.name
   *  FROM   person
   *  WHERE  sex = 'f'
   *
   *  SELECT s.name, t.name
   *  FROM   person s, person t
   *  WHERE  s.sex = 'f' AND s.spouse = t
   *
   * (which are semantically identical) require that the class person
   * (plus its subclasses, if any) serve as the basis for two different
   * join graph nodes.
   */

  /*
   * The environment in which this Node is embedded, and to which it
   * refers for other important information.
   */
  QO_ENV *env;

  /*
   * The PT_NODE from env->pt_tree that gave rise to this graph
   * node.  entity_spec holds all of the interesting information about
   * class oids, etc. which we need to communicate with the statistics
   * manager.
   */
  PT_NODE *entity_spec;

  /*
   * The segments (and their equivalence classes) that cover (i.e.,
   * emanate from) this node.
   */
  BITSET segs;
  BITSET eqclasses;

  /*
   * The partition (see below) to which this node belongs.
   */
  QO_PARTITION *partition;

  /*
   * If non-NULL, oid_seg is a segment corresponding to the (virtual)
   * oid attribute of a class.  It is often necessary to retrieve the
   * oid of an object along with other of its attributes (in order to,
   * for example, supply enough information for update queries, or for
   * certain kinds of joins), and it is convenient to be able to treat
   * this the same as any other kind of attribute.
   */
  QO_SEGMENT *oid_seg;

  /*
   * The set of all nodes that this node MAY be dependent on if it is
   * a derived table that is correlated to this level.  It will be all
   * nodes with an idx less than this node.  This is coarser than
   * perhaps necessary, but makes the implementation considerably
   * cleaner.
   */
  BITSET dep_set;

  /*
   * The set of sargs that apply to this node.  This is an implicit
   * conjunction; elements produced from a scan of this node will
   * satisfy all sargs in the set.  selectivity is the product of the
   * selectivities of the sargs in the set.
   */
  BITSET sargs;
  double selectivity;

  /*
   * The set of all subqueries that must be evaluated whenever a new
   * row is produced from this node.
   */
  BITSET subqueries;

  /*
   * Interesting information gleaned from the actual class objects in
   * the database.  This is actually a vector, one entry per class
   * underlying this node.  This complication arises primarily from the
   * presence of inheritance in the object model; the join graph for a
   * query such as
   *
   *  SELECT a FROM ALL x;
   *
   * has only one node, but that node represents the class x and all of
   * its subclasses.
   */
  QO_CLASS_INFO *info;

  /*
   * Summaries of size information from the various constituent
   * classes.  ncard is the total number of objects represented by this
   * node, while tcard is the total number of disk pages occupied by
   * those objects.  These silly names are historical artifacts that
   * correspond to the names in the 197? IBM report on query
   * optimization in System R.
   */
  unsigned long int ncard;
  unsigned long int tcard;

  /*
   * The nominal name of this node, used pretty much only for dumping
   * debugging information.  Right now, we just pick the name of the
   * first class in the list of classes and subclasses underlying this
   * node (see above).
   */
  const char *class_name;

  /*
   * The ordinal id of this node; this is the id used to identify this
   * node in various bitsets.
   */
  int idx;

  /*
   * The relative id of this node in partition; this is the id used to
   * identify this node in join_info vector.
   */
  int rel_idx;

  /*
   *  Indexes can be viewed from a variety of perspectives.  Each class
   *  associated with this node has a collection of indexes which are
   *  possible candidates for use in an index scan.  For scans accross
   *  the class heirarchy, it is necessary to find compatible indexes
   *  at each class in the heirarchy.  This heirarchical grouping is
   *  maintened in QO_NODE_INDEX.
   */
  QO_NODE_INDEX *indexes;
  QO_USING_INDEX *using_index;	/* indexes specifed in USING INDEX clause */
  /* NULL if no USING INDEX clause in the query */

  BITSET outer_dep_set;		/* outer join dependency; to preseve join sequence */
  bool sargable;		/* whether sargs are applicable to this node */
  /*
   * hint comment contained in given
   */
  PT_HINT_ENUM hint;
};

#define QO_NODE_ENV(node)		(node)->env
#define QO_NODE_ENTITY_SPEC(node)	(node)->entity_spec
#define QO_NODE_PT_JOIN_TYPE(node)      (node)->entity_spec->info.spec.join_type
#define QO_NODE_LOCATION(node)          (node)->entity_spec->info.spec.location
#define QO_NODE_OID_SEG(node)		(node)->oid_seg
#define QO_NODE_EQCLASSES(node)		(node)->eqclasses
#define QO_NODE_PARTITION(node)		(node)->partition
#define QO_NODE_DEP_SET(node)           (node)->dep_set
#define QO_NODE_SARGS(node)		(node)->sargs
#define QO_NODE_SELECTIVITY(node)	(node)->selectivity
#define QO_NODE_SUBQUERIES(node)	(node)->subqueries
#define QO_NODE_SEGS(node)		(node)->segs
#define QO_NODE_IDX(node)		(node)->idx
#define QO_NODE_REL_IDX(node)		(node)->rel_idx
#define QO_NODE_INDEXES(node)		(node)->indexes
#define QO_NODE_USING_INDEX(node)       (node)->using_index

#define QO_NODE_OUTER_DEP_SET(node)     (node)->outer_dep_set
#define QO_NODE_SARGABLE(node)          (node)->sargable

#define QO_NODE_NAME(node)		(node)->class_name
#define QO_NODE_OIDP(node)		(&(node)->info->info[0].oid)
#define QO_NODE_INFO(node)		(node)->info
#define QO_NODE_INFO_N(node)            (node)->info->n
#define QO_NODE_NCARD(node)		(node)->ncard
#define QO_NODE_TCARD(node)		(node)->tcard
#define QO_NODE_HINT(node)		(node)->hint


struct qo_segment
{
  /*
   * Segments are to attributes what nodes are to classes: every unique
   * use of an attribute in a query gives rise to a unique segment for
   * the optimizer to consider.  Thus, in the query
   *
   *  SELECT name, spouse.name
   *  FROM   person
   *  WHERE  sex = 'f'
   *
   * The two occurrences of the 'name' attribute give rise to different
   * segments since they emanate from different nodes.
   */

  /*
   * The environment in which this Segment is embedded.
   */
  QO_ENV *env;

  /*
   * The parse node that gave rise to this Segment.
   */
  PT_NODE *pt_node;


  /*
   * The Node at the head (start) of this segment, i.e., the node from
   * which this segment emanates.
   */
  QO_NODE *head;

  /*
   * The Node at the tail (end) of this segment, if any.  This will be
   * non-NULL only if the segment is a join segment, i.e., if the
   * domain of the underlying attribute is object-based.
   */
  QO_NODE *tail;

  /*
   * The link field used to chain together (in trees, actually)
   * segments that belong to the same equivalence class.  This is used
   * by env_assign_eq_classes() to actually create and initialize the
   * EqClass objects.
   */
  QO_SEGMENT *eq_root;
  QO_EQCLASS *eqclass;

  /*
   * The actual name of the attribute, squirreled away here for
   * convenience.
   */
  const char *name;

  /*
   * A flags indicating whether this segment is set valued, a class
   * attribute, or a shared attribute.
   */
  bool set_valued;
  bool class_attr;
  bool shared_attr;

  /*
   * Statistics information gleaned from the underlying attributes for
   * this segment.  This vector should have the same number of entries
   * as the number of classes underlying the node from which this
   * segment emanates; each entry in this vector corresponds to the
   * actual attribute emanating from the actual class in the
   * corresponding node.
   */
  QO_ATTR_INFO *info;

  /*
   * The index of this segment in the corresponding Env's seg array.
   */
  int idx;
  /* indexable terms to which this segment belings */
  BITSET index_terms;
  /* is index term equatity expression? */
  bool index_term_eq_expr;
};

#define QO_SEG_ENV(seg)			(seg)->env
#define QO_SEG_PT_NODE(seg)             (seg)->pt_node
#define QO_SEG_HEAD(seg)		(seg)->head
#define QO_SEG_TAIL(seg)		(seg)->tail
#define QO_SEG_EQ_ROOT(seg)		(seg)->eq_root
#define QO_SEG_EQCLASS(seg)		(seg)->eqclass
#define QO_SEG_NAME(seg)		(seg)->name
#define QO_SEG_SET_VALUED(seg)          (seg)->set_valued
#define QO_SEG_CLASS_ATTR(seg)          (seg)->class_attr
#define QO_SEG_SHARED_ATTR(seg)         (seg)->shared_attr
#define QO_SEG_INFO(seg)		(seg)->info
#define QO_SEG_IDX(seg)			(seg)->idx
#define QO_SEG_IS_SET_VALUED(seg)       (seg)->set_valued
#define	QO_SEG_ATTR_ID(seg)		QO_SEG_ATTR_STATS(seg)->id
#define QO_SEG_IS_OID_SEG(seg)		(QO_NODE_OID_SEG(QO_SEG_HEAD(seg)) == seg)
#define QO_SEG_INDEX_TERMS(seg)         (seg)->index_terms
#define OID_SEG_NAME                    "OID$"

struct qo_eqclass
{
  /*
   * An equivalence class is a collection of segments that are related
   * by equality predicates (directed edges).  For example, in the
   * following query
   *
   *  SELECT name, spouse.name
   *  FROM   person
   *  WHERE  age = spouse.age
   *
   * the segments corresponding to age and spouse.age are in the same
   * equivalence class.  This is important for specifying sort orders
   * that cross different classes with different attributes; if two
   * classes are sorted on attributes that are in the same equivalence
   * class, the two classes are sorted in the same order.
   */

  /*
   * The Env in which this EqClass is embedded.
   */
  QO_ENV *env;

  /*
   * The set of segments that belong to this equivalence class (i.e.,
   * that are related by some join term).
   */
  BITSET segs;

  /*
   * The QO_TERM associated with this equivalence class if this class
   * was fabricated specially to deal with complex merge terms.  It
   * should always be the case that this is NULL if segs is non-empty,
   * and segs should be empty if this is non-NULL.
   */
  QO_TERM *term;

  /*
   * The index of this EqClass in the corresponding Env's eqclasses
   * array.
   */
  int idx;
};

#define QO_UNORDERED		((QO_EQCLASS*)NULL)

#define QO_EQCLASS_ENV(e)	(e)->env
#define QO_EQCLASS_SEGS(e)	(e)->segs
#define QO_EQCLASS_TERM(e)	(e)->term
#define QO_EQCLASS_IDX(e)	(e)->idx


/*
 * These codes are arranged so that we can easily distinguish path
 * expression terms from terms introduced in a where part (i.e.,
 * relational expressions):  a path expression term has bit 4 turned on.
 * Similarly, any edge term has bit 3 turned on.
 *
 * QO_TC_DEP_LINK is introduced to handle dependency links for dependent
 * derived tables:  they serve only to connect a dependent node with the
 * nodes it depends on, in order that the join graph won't be incorrectly
 * partitioned.  They require no actual implementation, since that happens
 * via side effect of the variables referenced by the XASL code that
 * implements the derived table (the independent nodes fill xvariables
 * with values, and the subquery (or whatever) for the dependent table
 * then runs and uses those values).
 *
 * QO_TC_DEP_JOIN terms simply link together the dependency nodes (the
 * nodes in the term's dep_set) involved in a dependent derived table.
 * These terms will only come about when there is more than one
 * dependency node involved.  They're necessary because of the way nodes
 * are introduced during the searching phase of optimization: a node is
 * added to the plan we're building only because a join graph edge leads
 * to it.  In the case of a dependent derived table with multiple
 * dependencies, there is no such edge, so we have to synthesize one.
 */
typedef enum
{
  /*
   *                                      p  e  f   n
   *                                      a  d  a   u
   *                                      t  g  k   m
   *                                      h  e  e
   */
  QO_TC_PATH = 0x30,		/*  1  1  0  000  */
  QO_TC_JOIN = 0x11,		/*  0  1  0  001  */
  QO_TC_SARG = 0x02,		/*  0  0  0  010  */
  QO_TC_OTHER = 0x03,		/*  0  0  0  011  */
  QO_TC_DEP_LINK = 0x1c,	/*  0  1  1  100  */
  QO_TC_DEP_JOIN = 0x1d,	/*  0  1  1  101  */
  QO_TC_DURING_JOIN = 0x04,	/*  0  0  0  100  */
  QO_TC_AFTER_JOIN = 0x05,	/*  0  0  0  101  */
  QO_TC_TOTALLY_AFTER_JOIN = 0x06,	/*  0  0  0  110  */
  QO_TC_DUMMY_JOIN = 0x1f	/*  0  1  1  111  */
} QO_TERMCLASS;

#define QO_IS_PATH_TERM(t)	(QO_TERM_CLASS(t) & 0x20)
#define QO_IS_EDGE_TERM(t)	(QO_TERM_CLASS(t) & 0x10)
#define QO_IS_FAKE_TERM(t)	(QO_TERM_CLASS(t) & 0x08)

struct qo_term
{
  /*
   * Terms are a superset of edges: those terms that involve exactly
   * two nodes are considered to be edges, and are eligible for
   * implementation as joins.  The remainder are implemented as
   * sargs (if they involve only one node) or residual predicates.
   * Notice that segments that connect two nodes are also considered to
   * be terms.  In fact, the optimizer sees no difference between the
   * following two queries
   *
   *  SELECT name, spouse.name
   *  FROM   person
   *
   *  SELECT s.name, t.name
   *  FROM   person s, person t
   *  WHERE  s.spouse = t
   */

  /*
   * WARNING!!! WARNING!!! WARNING!!!
   *
   * If you add any more elements to this struct, be sure to update the
   * body of qo_exchange (in env.c).  Sadly, it needs to about all of
   * the elements of this struct.
   */

  /*
   * The env in which this term is embedded.
   */
  QO_ENV *env;

  /*
   * The "flavor" of this term.  This is determined by analysis of the
   * segment or where-clause disjunct that gives rise to the term.
   */
  QO_TERMCLASS term_class;

  /*
   * The nodes referenced by this term (i.e., the heads of all segments
   * used in this term).
   *
   * If this term is a dependent term, this is the set of all nodes on
   * which the dependent node depends, PLUS the dependent node.
   */
  BITSET nodes;

  /*
   * The set of segments involved in the expression that gives rise to
   * this term.
   */
  BITSET segments;

  /*
   * The selectivity of this term (i.e., the fraction of candidates
   * that we expect to satisfy the term) when it is not used as an
   * index.  If T is the cartesian product of all nodes referenced by
   * the term, selectivity is the fraction of the rows in T that
   * (are expected to) satisfy the term.
   */
  double selectivity;

  /*
   * The rank of this term. used for the same selectivity
   */
  int rank;

  /*
   * The expression that gave rise to this term.  This is either a
   * conjunct from the conjunctive normal form of the query's search
   * condition, or a "manufactured" term spawned by a path term.
   */
  PT_NODE *pt_expr;
  short location;

  /*
   * The set of all correlated subqueries appearing in this term.
   */
  BITSET subqueries;

  /*
   * -1 == NO_JOIN iff this term is not suitable as a join predicate
   * Currently only applicable to path terms.
   */
  JOIN_TYPE join_type;

  /*
   * can_use_index is non-zero if this term can be implemented with an
   * index under appropriate circumstances, i.e., one or both sides are
   * local names, and each side is "independent" of the other (no node
   * contributes segments to both sides).
   *
   * if can_use_index is non-zero, the first "can_use_index" entries in
   * index_seg hold the segments that can use an index.
   */
  int can_use_index;
  QO_SEGMENT *index_seg[2];

  /*
   * The two segments involved in a path join term.  It's relatively
   * important to know which one is the (virtual) oid segment and which
   * is the real segment.
   */
  QO_SEGMENT *seg;
  QO_SEGMENT *oid_seg;

  /*
   * The head and tail nodes joined by this term if it is a join term.
   * This is redundant, but it is convenient to cache this information
   * here since it tends to get accessed a lot.
   *
   * If the term is a dependency link term, tail identifies the
   * dependent node.
   */
  QO_NODE *head;
  QO_NODE *tail;

  /*
   * The equivalence class to which the segments in this term belong.
   * Again, valid only if this is a join term.
   *
   * Nominal_seg is some member of that equivalence class.  Because
   * equivalence classes aren't assigned until after we've done some
   * preliminary work on the terms, it's convenient to hold onto a
   * nominal representative here to make it easy find the associated
   * equivalence class later.
   */
  QO_EQCLASS *eqclass;
  QO_SEGMENT *nominal_seg;

  int flag;			/* flags */

  /*
   * The ordinal id of this term; this is the id used to identify this
   * node in various bitsets.
   */
  int idx;

  /*
   * WARNING!!! WARNING!!! WARNING!!!
   *
   * If you add any more elements to this struct, be sure to update the
   * body of qo_exchange (in env.c).  Sadly, it needs to about all of
   * the elements of this struct.
   */
};

#define QO_TERM_ENV(t)		(t)->env
#define QO_TERM_CLASS(t)	(t)->term_class
#define QO_TERM_NODES(t)	(t)->nodes
#define QO_TERM_SEGS(t)		(t)->segments
#define QO_TERM_SELECTIVITY(t)	(t)->selectivity
#define QO_TERM_RANK(t)		(t)->rank
#define QO_TERM_HEAD(t)		(t)->head
#define QO_TERM_TAIL(t)		(t)->tail
#define QO_TERM_IDX(t)		(t)->idx
#define QO_TERM_PT_EXPR(t)	(t)->pt_expr
#define QO_TERM_LOCATION(t)     (t)->location
#define QO_TERM_SUBQUERIES(t)	(t)->subqueries
#define QO_TERM_SEG(t)		(t)->seg
#define QO_TERM_OID_SEG(t)	(t)->oid_seg
#define QO_TERM_EQCLASS(t)	(t)->eqclass
#define QO_TERM_NOMINAL_SEG(t)	(t)->nominal_seg
#define QO_TERM_CAN_USE_INDEX(t) (t)->can_use_index
#define QO_TERM_INDEX_SEG(t, i) (t)->index_seg[(i)]
#define QO_TERM_JOIN_TYPE(t)    (t)->join_type
#define QO_TERM_FLAG(t)	        (t)->flag


#define QO_TERM_EQUAL_OP             1	/* is equal op ? */
#define QO_TERM_SINGLE_PRED          2	/* is single_pred ? */
#define QO_TERM_COPY_PT_EXPR         4	/* pt_expr is copyed ? */
#define QO_TERM_MERGEABLE_EDGE       8	/* suitable as a m-join edge ? */

#define QO_TERM_IS_FLAGED(t, f)        (QO_TERM_FLAG(t) & (int) (f))
#define QO_TERM_SET_FLAG(t, f)         QO_TERM_FLAG(t) |= (int) (f)
#define QO_TERM_CLEAR_FLAG(t, f)       QO_TERM_FLAG(t) &= (int) ~(f)


struct qo_subquery
{
  /*
   * Interesting information about subqueries that are directly
   * correlated to this query (i.e., the query being optimized is the
   * innermost query to which the subqueries have correlated
   * references).
   */

  /*
   * The parse tree for the subquery itself.
   */
  PT_NODE *node;

  /*
   * The set of segments (and corresponding nodes) to which the
   * subquery actually refers.
   */
  BITSET segs;
  BITSET nodes;

  /*
   * The QO_TERMs in which this subquery appears, if any.  This will
   * have a cardinality of 1 for normal terms, but it could be larger
   * for dependent derived tables since they can depend upon more than
   * one antecedent and they'll be marked as a participant in each
   * dependency term.
   */
  BITSET terms;

  /*
   * This entry's offset in env->subqueries.
   */
  int idx;
};


struct qo_partition
{
  /*
   * Since the overall join graph can actually be disconnected (this
   * corresponds to a situation where the luser has specified one or
   * more cartesian products), we partition it first and optimize each
   * partition separately.  The partitions are then combined by
   * cartesian product operators in some nominally optimal order; since
   * cartesian products are almost always stupid, and almost never
   * present, we don't expend much effort in determining that order.
   */

  /*
   * The nodes, edges, and dependencies (sargable terms) in the
   * partition.  This partition might have dependent tables in it that
   * depend on tables in other partitions, so we will need this
   * information to make sure that we order the cartesian product of
   * partitions correctly.
   */
  BITSET nodes;
  BITSET edges;
  BITSET dependencies;

  /* the starting point of this partition's join_info vector that
   * will be allocated.
   */
  int M_offset;

  /*
   * The optimized plan created for this partition.
   */
  QO_PLAN *plan;

  /*
   * The id of this partition.
   */
  int idx;
};

#define QO_PARTITION_NODES(p)		(p)->nodes
#define QO_PARTITION_EDGES(p)		(p)->edges
#define QO_PARTITION_DEPENDENCIES(p)	(p)->dependencies
#define QO_PARTITION_M_OFFSET(p)	(p)->M_offset
#define QO_PARTITION_PLAN(p)		(p)->plan
#define QO_PARTITION_IDX(p)		(p)->idx

struct qo_env
{
  /*
   * The repository of all optimizer data structures.  These things are
   * all collected into one data structure in case the optimizer ever
   * needs to live in a multi-threaded world.  This adds a little
   * tedium (most procedures must accept the environment as an added
   * parameter), but it's not too bad.
   */

  /*
   * The parser environment that is associated with the pt_tree
   */
  PARSER_CONTEXT *parser;

  /*
   * The path expression tree for which we are to develop a plan.
   * The environment initializer will crawl all over this tree to glean
   * relevant information for the optimizer.
   */
  PT_NODE *pt_tree;

  /*
   * Counts and vectors that hold the various collections of nodes,
   * segments, etc.
   */
  int nsegs, Nsegs;
  int nnodes, Nnodes;
  int neqclasses, Neqclasses;
  int nterms, Nterms;
  int nsubqueries;
  int npartitions;
  int nedges;

  QO_SEGMENT *segs;
  QO_NODE *nodes;
  QO_EQCLASS *eqclasses;
  QO_TERM *terms;
  QO_SUBQUERY *subqueries;
  QO_PARTITION *partitions;

  /*
   * A temporary bitset.  This is needed for expr_segs() in build_query_graph
   */
  BITSET *tmp_bitset;

  /*
   * The final plan produced by the optimizer.
   */
  QO_PLAN *final_plan;

  /*
   * The segments (attributes) to be produced as the ultimate result of
   * the plan, i.e., the attributes that the luser expects to receive.
   */
  BITSET final_segs;

  /*
   * True iff we found a conjunct which was not an expression.  We assume
   * that this is a false conjunct and we don't need to optimize a query
   * that will return no values.
   */
  int bail_out;

  /*
   * The planner structure used during the search of the plan space.
   * We keep a pointer to it here to simplify cleanup, whether natural
   * or forced by an error.
   */
  QO_PLANNER *planner;

  /*
   * A JMPBUF for aborting from fatal problems.  A fatal problem for
   * the optimizer isn't necessarily a fatal problem for anyone else,
   * since we can always return to the outer context and try the
   * default plan.
   */
  jmp_buf catch_;

  /*
   * Controls the amount of garbage dumped with plans.  Can be
   * overriden with the environment variable CUBRID_QO_DUMP_LEVEL.
   */
  bool dump_enable;

  /*
   * A bitset holding the idx's of all fake terms.  This is convenient
   * for a quick exclusion test necessary during the search for good
   * plans, because fake terms can't be used in certain contexts.
   */
  BITSET fake_terms;
};

#define QO_ENV_SEG(env, n)		(&(env)->segs[(n)])
#define QO_ENV_NODE(env, n)		(&(env)->nodes[(n)])
#define QO_ENV_EQCLASS(env, n)		(&(env)->eqclasses[(n)])
#define QO_ENV_TERM(env, n)		(&(env)->terms[(n)])
#define QO_ENV_PARTITION(env, n)	(&(env)->partitions[(n)])
#define QO_ENV_PREV_SEG(env)            (env)->prev_seg
#define QO_ENV_PARSER(env)              (env)->parser
#define QO_ENV_PT_TREE(env)		(env)->pt_tree
#define QO_ENV_TMP_BITSET(env)          (env)->tmp_bitset

/*
 *  QO_XASL_INDEX_INFO gathers information about the indexed terms which
 *  will be used in the generation of the XASL tree.
 */
struct qo_xasl_index_info
{
  /*
   *  Number of term expressions.
   *  Array of term expressions which are associated with this index.
   */
  int nterms;
  PT_NODE **term_exprs;

  /*
   *  Pointer to the node index entry structure which contains
   *  infomation regarding the index itself.
   */
  struct qo_node_index_entry *ni_entry;
};


extern void qo_env_free (QO_ENV *);
extern void qo_env_dump (QO_ENV *, FILE *);

extern void qo_seg_fprint (QO_SEGMENT *, FILE *);
extern void qo_node_fprint (QO_NODE *, FILE *);
extern void qo_term_fprint (QO_TERM *, FILE *);

extern void qo_print_stats (FILE *);

extern QO_SEGMENT *qo_eqclass_wrt (QO_EQCLASS *, BITSET *);
extern void qo_eqclass_fprint_wrt (QO_EQCLASS *, BITSET *, FILE *);

extern void qo_termset_fprint (QO_ENV *, BITSET *, FILE *);

extern int qo_is_merge_term (QO_TERM *);


#define QO_INNER_JOIN_TERM(term) \
        (QO_TERM_CLASS(term) == QO_TC_JOIN && \
         QO_TERM_JOIN_TYPE(term) == JOIN_INNER)

#define QO_OUTER_JOIN_TERM(term) \
        ((QO_TERM_CLASS(term) == QO_TC_JOIN || \
          QO_TERM_CLASS(term) == QO_TC_DUMMY_JOIN) && \
         (QO_TERM_JOIN_TYPE(term) == JOIN_LEFT || \
          QO_TERM_JOIN_TYPE(term) == JOIN_RIGHT || \
          QO_TERM_JOIN_TYPE(term) == JOIN_OUTER))

#define QO_LEFT_OUTER_JOIN_TERM(term) \
        ((QO_TERM_CLASS(term) == QO_TC_JOIN || \
          QO_TERM_CLASS(term) == QO_TC_DUMMY_JOIN) && \
         QO_TERM_JOIN_TYPE(term) == JOIN_LEFT)

#define QO_RIGHT_OUTER_JOIN_TERM(term) \
        ((QO_TERM_CLASS(term) == QO_TC_JOIN || \
          QO_TERM_CLASS(term) == QO_TC_DUMMY_JOIN) && \
         QO_TERM_JOIN_TYPE(term) == JOIN_RIGHT)

#define QO_FULL_OUTER_JOIN_TERM(term) \
        ((QO_TERM_CLASS(term) == QO_TC_JOIN || \
          QO_TERM_CLASS(term) == QO_TC_DUMMY_JOIN) && \
         QO_TERM_JOIN_TYPE(term) == JOIN_OUTER)

#define QO_JOIN_INFO_SIZE(_partition) \
        (int)(1 << bitset_cardinality(&(QO_PARTITION_NODES(_partition))))

extern double QO_INFINITY;

extern size_t qo_seg_width (QO_SEGMENT * seg);
extern void qo_node_clear (QO_ENV *, int);
extern void qo_seg_clear (QO_ENV *, int);
extern void qo_term_clear (QO_ENV *, int);
extern void qo_discover_edges (QO_ENV *);
extern void qo_assign_eq_classes (QO_ENV *);
extern void qo_discover_indexes (QO_ENV *);
extern void qo_discover_partitions (QO_ENV *);
extern QO_ENV *qo_env_new (PARSER_CONTEXT *, PT_NODE *);
extern void qo_seg_nodes (QO_ENV *, BITSET *, BITSET *);
extern void qo_equivalence (QO_SEGMENT *, QO_SEGMENT *);

#endif /* _ENV_H_ */
