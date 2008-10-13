/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * vtrans2.c - Functions for the translation of virtual queries
 */

#ident "$Id$"

#include <assert.h>

#include "dbi.h"
#include "schema_manager_3.h"
#include "parser.h"
#include "semantic_check.h"
#include "msgexec.h"
#include "view_transform_1.h"
#include "view_transform_2.h"
#include "object_accessor.h"

typedef struct path_lambda_info PATH_LAMBDA_INFO;
struct path_lambda_info
{
  PT_NODE lambda_name;
  PT_NODE *lambda_expr;
  UINTPTR spec_id;
  PT_NODE *new_specs;		/* for adding shared attr specs */
};

typedef struct exists_info EXISTS_INFO;
struct exists_info
{
  PT_NODE *spec;
  int referenced;
};


typedef struct pt_reset_select_spec_info PT_RESET_SELECT_SPEC_INFO;
struct pt_reset_select_spec_info
{
  UINTPTR id;
  PT_NODE **statement;
};

typedef struct replace_name_info REPLACE_NAME_INFO;
struct replace_name_info
{
  PT_NODE *path;
  UINTPTR spec_id;
  PT_NODE *newspec;		/* for new sharedd attr specs */
};

typedef struct spec_reset_info SPEC_RESET_INFO;
struct spec_reset_info
{
  PT_NODE *statement;
  PT_NODE **sub_paths;
  PT_NODE *old_next;
};

typedef struct extra_specs_frame PT_EXTRA_SPECS_FRAME;
struct extra_specs_frame
{
  struct extra_specs_frame *next;
  PT_NODE *extra_specs;
};

typedef struct mq_lambda_arg MQ_LAMBDA_ARG;
struct mq_lambda_arg
{
  PT_NODE *name_list;
  PT_NODE *tree_list;
  PT_EXTRA_SPECS_FRAME *spec_frames;
};

typedef struct set_names_info SET_NAMES_INFO;
struct set_names_info
{
  DB_OBJECT *object;
  UINTPTR id;
};

static PT_NODE *mq_lookup_symbol (PARSER_CONTEXT * parser,
				  PT_NODE * attr_list, PT_NODE * attr);

static int mq_is_vclass_on_oo_ldb (DB_OBJECT * vclass_object);
static PT_NODE *mq_coerce_resolved (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_set_all_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static PT_NODE *mq_reset_all_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				  void *void_arg, int *continue_walk);
static PT_NODE *mq_reset_spec_ids (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *mq_get_references_node (PARSER_CONTEXT * parser,
					PT_NODE * node, void *void_arg,
					int *continue_walk);
static PT_NODE *mq_referenced_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *void_arg, int *continue_walk);
static PT_NODE *mq_referenced_post (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static int mq_is_referenced (PARSER_CONTEXT * parser, PT_NODE * statement,
			     PT_NODE * spec);
static PT_NODE *mq_set_references_local (PARSER_CONTEXT * parser,
					 PT_NODE * statement, PT_NODE * spec);
static PT_NODE *mq_reset_select_spec_node (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *void_arg,
					   int *continue_walk);
static PT_NODE *mq_reset_select_specs (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static PT_NODE *mq_new_spec (PARSER_CONTEXT * parser, const char *class_name);
static PT_NODE *mq_replace_name_with_path (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *void_arg,
					   int *continue_walk);
static PT_NODE *mq_substitute_path (PARSER_CONTEXT * parser, PT_NODE * node,
				    PATH_LAMBDA_INFO * path_info);
static PT_NODE *mq_substitute_path_pre (PARSER_CONTEXT * parser,
					PT_NODE * node, void *void_arg,
					int *continue_walk);
static PT_NODE *mq_path_name_lambda (PARSER_CONTEXT * parser,
				     PT_NODE * statement,
				     PT_NODE * lambda_name,
				     PT_NODE * lambda_expr, UINTPTR spec_id);
static PT_NODE *mq_reset_spec_distr_subpath_pre (PARSER_CONTEXT * parser,
						 PT_NODE * spec,
						 void *void_arg,
						 int *continue_walk);
static PT_NODE *mq_reset_spec_distr_subpath_post (PARSER_CONTEXT * parser,
						  PT_NODE * spec,
						  void *void_arg,
						  int *continue_walk);
static PT_NODE *mq_translate_paths (PARSER_CONTEXT * parser,
				    PT_NODE * statement, PT_NODE * root_spec);
static int mq_occurs_in_from_list (PARSER_CONTEXT * parser, const char *name,
				   PT_NODE * from_list);
static void mq_invert_insert_select (PARSER_CONTEXT * parser, PT_NODE * attr,
				     PT_NODE * subquery);
static void mq_invert_insert_subquery (PARSER_CONTEXT * parser,
				       PT_NODE ** attr, PT_NODE * subquery);
static PT_NODE *mq_push_arg2 (PARSER_CONTEXT * parser, PT_NODE * query,
			      PT_NODE * dot_arg2);
static PT_NODE *mq_lambda_node_pre (PARSER_CONTEXT * parser, PT_NODE * tree,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_lambda_node (PARSER_CONTEXT * parser, PT_NODE * node,
				void *void_arg, int *continue_walk);
static PT_NODE *mq_set_virt_object (PARSER_CONTEXT * parser, PT_NODE * node,
				    void *void_arg, int *continue_walk);
static PT_NODE *mq_fix_derived (PARSER_CONTEXT * parser,
				PT_NODE * select_statement, PT_NODE * spec);
static PT_NODE *mq_translate_value (PARSER_CONTEXT * parser, PT_NODE * value);
static void mq_push_dot_in_query (PARSER_CONTEXT * parser, PT_NODE * query,
				  int i, PT_NODE * name);
static PT_NODE *mq_clean_dot (PARSER_CONTEXT * parser, PT_NODE * node,
			      void *void_arg, int *continue_walk);
static PT_NODE *mq_fetch_subqueries_for_update_local (PARSER_CONTEXT * parser,
						      PT_NODE * class_,
						      PT_FETCH_AS fetch_as,
						      DB_AUTH what_for,
						      PARSER_CONTEXT **
						      qry_cache);
static PT_NODE *mq_fetch_select_for_real_class_update (PARSER_CONTEXT *
						       parser,
						       PT_NODE * vclass,
						       PT_NODE * real_class,
						       PT_FETCH_AS fetch_as,
						       DB_AUTH what_for);
static PT_NODE *mq_fetch_expression_for_real_class_update (PARSER_CONTEXT *
							   parser,
							   DB_OBJECT *
							   vclass_obj,
							   PT_NODE * attr,
							   PT_NODE *
							   real_class,
							   PT_FETCH_AS
							   fetch_as,
							   DB_AUTH what_for,
							   UINTPTR * spec_id);
static PT_NODE *mq_set_names_dbobject (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *void_arg,
				       int *continue_walk);
static int mq_update_attribute_local (DB_OBJECT * vclass_object,
				      const char *attr_name,
				      DB_OBJECT * real_class_object,
				      DB_VALUE * virtual_value,
				      DB_VALUE * real_value,
				      const char **real_name,
				      int translate_proxy, int db_auth);
static PT_NODE *mq_fetch_one_real_class_get_cache (DB_OBJECT * vclass_object,
						   PARSER_CONTEXT **
						   query_cache);
static PT_NODE *mq_reset_specs_from_column (PARSER_CONTEXT * parser,
					    PT_NODE * statement,
					    PT_NODE * column);
static PT_NODE *mq_path_spec_lambda (PARSER_CONTEXT * parser,
				     PT_NODE * statement, PT_NODE * root_spec,
				     PT_NODE ** prev_ptr, PT_NODE * old_spec,
				     PT_NODE * new_spec);
static PT_NODE *mq_generate_unique (PARSER_CONTEXT * parser,
				    PT_NODE * name_list);

static int virtual_to_realval (PARSER_CONTEXT * parser, DB_VALUE * v_val,
			       PT_NODE * expr, DB_VALUE * r_val);


/*
 * pt_lookup_symbol() -
 *   return: symbol we are looking for, or NULL if not found
 *   parser(in):
 *   attr_list(in): attribute list to look for attr in
 *   attr(in): attr to look for
 */
static PT_NODE *
mq_lookup_symbol (PARSER_CONTEXT * parser, PT_NODE * attr_list,
		  PT_NODE * attr)
{
  PT_NODE *list;

  if (!attr || attr->node_type != PT_NAME)
    {
      PT_INTERNAL_ERROR (parser, "resolution");
      return NULL;
    }

  for (list = attr_list;
       (list != NULL) && (!pt_name_equal (parser, list, attr));
       list = list->next)
    {
      ;				/* do nothing */
    }

  return list;

}				/* pt_lookup_symbol */


/*
 * mq_insert_symbol() - appends the symbol to the entities
 *   return: none
 *   parser(in): parser environment
 *   listhead(in/out): entity_spec to add symbol to
 *   attr(in): the attribute to add to the symbol table
 */
void
mq_insert_symbol (PARSER_CONTEXT * parser, PT_NODE ** listhead,
		  PT_NODE * attr)
{
  PT_NODE *new_node;

  if (!attr || attr->node_type != PT_NAME)
    {
      PT_INTERNAL_ERROR (parser, "translate");
      return;
    }

  /* only insert attributes */
  if (attr->info.name.meta_class == PT_PARAMETER)
    {
      return;
    }

  new_node = mq_lookup_symbol (parser, *listhead, attr);

  if (new_node == NULL)
    {
      new_node = parser_copy_tree (parser, attr);

      *listhead = parser_append_node (new_node, *listhead);
    }

}				/* mq_insert_symbol */


/*
 * mq_generate_name() - generates printable names
 *   return:
 *   parser(in):
 *   root(in):
 *   version(in):
 */
const char *
mq_generate_name (PARSER_CONTEXT * parser, const char *root, int *version)
{
  const char *generatedname;
  char temp[10];

  (*version)++;

  sprintf (temp, "_%d", *version);

  /* avoid "stepping" on root */
  generatedname = pt_append_string
    (parser, pt_append_string (parser, NULL, root), temp);

  return generatedname;
}

/*
 * mq_is_vclass_on_oo_ldb() - checks that the class object has an oo style
 *                            intrinsic object id
 *   return: 1 on oo style intrinsic object id
 *   vclass_object(in):
 */
static int
mq_is_vclass_on_oo_ldb (DB_OBJECT * vclass_object)
{
  DB_NAMELIST *oid_attrs;
  int retval = 0;

  oid_attrs = db_get_object_id (vclass_object);

  if (!oid_attrs)
    {
      retval = 0;
    }
  else
    {
      if (oid_attrs->name && !oid_attrs->name[0])
	{
	  /* this is for sqlx ldb's */
	  retval = 1;
	}
      db_namelist_free (oid_attrs);
    }
  return (retval);
}


/*
 * mq_coerce_resolved() - re-sets PT_NAME node resolution to match
 *                        a new printable name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_coerce_resolved (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  PT_NODE *range = (PT_NODE *) void_arg;
  *continue_walk = PT_CONTINUE_WALK;

  /* if its not a name, leave it alone */
  if (node->node_type == PT_NAME)
    {

      if (node->info.name.spec_id == range->info.name.spec_id	/* same entity spec */
	  && node->info.name.resolved	/* and has a resolved name, */
	  && node->info.name.meta_class != PT_CLASS
	  && node->info.name.meta_class != PT_LDBVCLASS
	  && node->info.name.meta_class != PT_VCLASS)
	{
	  /* set the attribute resolved name */
	  node->info.name.resolved = range->info.name.original;
	}

      /* sub nodes of PT_NAME are not names with range variables */
      *continue_walk = PT_LIST_WALK;
    }
  else if (node->node_type == PT_SPEC
	   && node->info.spec.id == range->info.name.spec_id)
    {
      PT_NODE *flat = node->info.spec.flat_entity_list;
      /* sub nodes of PT_SPEC include flat class lists with
       * range variables. Set them even though they are "class" names.
       */

      for (; flat != NULL; flat = flat->next)
	{
	  flat->info.name.resolved = range->info.name.original;
	}
    }

  return node;
}

/*
 * mq_set_all_ids() - sets PT_NAME node ids
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_set_all_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME)
    {
      node->info.name.spec_id = spec->info.spec.id;
      node->info.name.resolved =
	spec->info.spec.range_var->info.name.original;
    }

  node->spec_ident = spec->info.spec.id;

  return node;
}


/*
 * mq_reset_all_ids() - re-sets PT_NAME node ids
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_all_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		  int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.resolved	/* has a resolved name */
	  && node->info.name.meta_class != PT_CLASS
	  && node->info.name.meta_class != PT_LDBVCLASS
	  && node->info.name.meta_class != PT_VCLASS)
	{
	  /* set the attribute resolved name */
	  node->info.name.resolved =
	    spec->info.spec.range_var->info.name.original;
	}

    }
  else if (node->node_type == PT_SPEC
	   && node->info.spec.id == spec->info.spec.id
	   && node->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      /* fix up pseudo specs, although it probably does not matter */
      node->info.spec.id = (UINTPTR) spec;
    }

  if (node->spec_ident == spec->info.spec.id)
    {
      node->spec_ident = (UINTPTR) spec;
    }

  return node;
}


/*
 * mq_reset_ids() - re-sets path entities of a spec by removing unreferenced
 *         paths, reseting ids of remaining paths, and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_reset_ids (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec)
{
  PT_NODE *range;

  /* don't mess with psuedo specs */
  if (spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  /* make sure range var always has same id as spec */
  range = spec->info.spec.range_var;
  if (range)
    {
      range->info.name.spec_id = spec->info.spec.id;
    }

  statement =
    parser_walk_tree (parser, statement, mq_reset_all_ids, spec, NULL, NULL);

  /* spec may or may not be part of statement. If it is, this is
     redundant. If its not, this will reset self references, such
     as in path specs. */
  (void) parser_walk_tree (parser, spec, mq_reset_all_ids, spec, NULL, NULL);

  /* finally, set spec id */
  spec->info.spec.id = (UINTPTR) spec;

  return statement;
}


/*
 * mq_reset_spec_ids() - resets spec ids for a spec node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_spec_ids (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{

  if (node->node_type == PT_SELECT)
    {
      mq_set_references (parser, node, node->info.query.q.select.from);
    }

  return (node);

}

/*
 * mq_reset_ids_in_statement() - walks the statement and for each spec,
 *                               reset ids that reference that spec
 *   return:
 *   parser(in):
 *   statement(in):
 */
PT_NODE *
mq_reset_ids_in_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{

  statement = parser_walk_tree (parser, statement, mq_reset_spec_ids, NULL,
				NULL, NULL);

  return (statement);

}

/*
 * mq_get_references_node() - gets referenced PT_NAME nodes
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_get_references_node (PARSER_CONTEXT * parser, PT_NODE * node,
			void *void_arg, int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.meta_class != PT_METHOD
	  && node->info.name.meta_class != PT_HINT_NAME
	  && node->info.name.meta_class != PT_INDEX_NAME)
	{
	  /* filter out method name, hint argument name, index name nodes */
	  mq_insert_symbol (parser, &spec->info.spec.referenced_attrs, node);
	}
    }

  if (node->node_type == PT_SPEC)
    {
      /* The only part of a spec node that could contain references to
       * the given spec_id are derived tables, path_entities,
       * path_conjuncts, and on_cond.
       * All the rest of the name nodes for the spec are not references,
       * but range variables, class names, etc.
       * We don't want to mess with these. We'll handle the ones that
       * we want by hand. */
      node->info.spec.derived_table =
	parser_walk_tree (parser, node->info.spec.derived_table,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.path_entities =
	parser_walk_tree (parser, node->info.spec.path_entities,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.path_conjuncts =
	parser_walk_tree (parser, node->info.spec.path_conjuncts,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      node->info.spec.on_cond =
	parser_walk_tree (parser, node->info.spec.on_cond,
			  mq_get_references_node, spec, pt_continue_walk,
			  NULL);
      /* don't visit any other leaf nodes */
      *continue_walk = PT_LIST_WALK;
    }

  /* Data type nodes can not contain any valid references.  They do
     contain class names and other things we don't want. */
  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  if (node->spec_ident == spec->info.spec.id)
    {
      node->spec_ident = (UINTPTR) spec;
    }

  return node;
}


/*
 * mq_reset_ids_and_references() - re-sets path entities of a spec by
 *      removing unreferenced paths, reseting ids of remaining paths,
 *      and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_reset_ids_and_references (PARSER_CONTEXT * parser, PT_NODE * statement,
			     PT_NODE * spec)
{
  return mq_reset_ids_and_references_helper (parser, statement, spec,
					     true /* default */ );
}

/*
 * mq_reset_ids_and_references_helper() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   get_spec_referenced_attr(in):
 */
PT_NODE *
mq_reset_ids_and_references_helper (PARSER_CONTEXT * parser,
				    PT_NODE * statement, PT_NODE * spec,
				    bool get_spec_referenced_attr)
{
  /* don't mess with psuedo specs */
  if (spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  statement = mq_reset_ids (parser, statement, spec);

  parser_free_tree (parser, spec->info.spec.referenced_attrs);
  spec->info.spec.referenced_attrs = NULL;

  statement = parser_walk_tree (parser, statement, mq_get_references_node,
				spec, pt_continue_walk, NULL);

  /* spec may or may not be part of statement. If it is, this is
     redundant. If its not, this will reset catch self references, such
     as in path specs. */
  if (get_spec_referenced_attr)
    {
      (void) parser_walk_tree (parser, spec, mq_get_references_node,
			       spec, pt_continue_walk, NULL);
    }

  return statement;
}


/*
 * mq_get_references() - returns a copy of a list of referenced names for
 *                       the given entity spec
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_get_references (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * spec)
{
  return mq_get_references_helper (parser, statement, spec,
				   true /* default */ );
}

/*
 * mq_get_references_helper() -
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 *   get_spec_referenced_attr(in):
 */
PT_NODE *
mq_get_references_helper (PARSER_CONTEXT * parser, PT_NODE * statement,
			  PT_NODE * spec, bool get_spec_referenced_attr)
{
  PT_NODE *references;

  statement = mq_reset_ids_and_references_helper (parser, statement, spec,
						  get_spec_referenced_attr);

  references = spec->info.spec.referenced_attrs;
  spec->info.spec.referenced_attrs = NULL;

  return references;
}

/*
 * mq_referenced_pre() - looks for a name from a given entity spec
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_referenced_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		   int *continue_walk)
{
  EXISTS_INFO *info = (EXISTS_INFO *) void_arg;
  PT_NODE *spec = info->spec;

  /* don't count self references as being referenced. */
  if (node == spec)
    {
      *continue_walk = PT_LIST_WALK;
      return node;
    }

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id)
    {
      node->info.name.spec_id = (UINTPTR) spec;
      if (node->info.name.meta_class != PT_LDBVCLASS
	  && node->info.name.meta_class != PT_VCLASS)
	{
	  info->referenced = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;
}

/*
 * mq_referenced_post() - looks for a name from a given entity spec
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_referenced_post (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  if (*continue_walk != PT_STOP_WALK)
    {
      *continue_walk = PT_CONTINUE_WALK;
    }
  return node;
}


/*
 * mq_is_referenced() - tests if an entity is referenced in a spec
 *   return: 1 on referenced
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
static int
mq_is_referenced (PARSER_CONTEXT * parser, PT_NODE * statement,
		  PT_NODE * spec)
{
  EXISTS_INFO info;
  info.spec = spec;
  info.referenced = 0;

  parser_walk_tree (parser, statement, mq_referenced_pre, &info,
		    mq_referenced_post, &info);

  return info.referenced;
}


/*
 * mq_reset_paths() - re-sets path entities of a spec by removing unreferenced
 *      paths, reseting ids of remaining paths and recursing on sub-paths
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in):
 */
PT_NODE *
mq_reset_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
		PT_NODE * root_spec)
{
  PT_NODE **path_spec_ptr = &root_spec->info.spec.path_entities;
  PT_NODE *path_spec = *path_spec_ptr;

  for (; path_spec != NULL; path_spec = *path_spec_ptr)
    {
      if (mq_is_referenced (parser, statement, path_spec))
	{
	  /* keep it if its still referenced */
	  statement = mq_reset_ids (parser, statement, path_spec);

	  statement = mq_reset_paths (parser, statement, path_spec);

	  path_spec_ptr = &path_spec->next;
	}
      else
	{
#if 0
	  /* its possible inder some perverse conditions for a virtual
	   * spec to disappear, while sub paths still apear.
	   * Hear, we promote the sub-paths to the same level and
	   * re-check them all for references.
	   */
	  parser_append_node (path_spec->info.spec.path_entities, path_spec);
	  path_spec->info.spec.path_entities = NULL;
#endif /* 0 */

	  /* remove path spec */
	  *path_spec_ptr = path_spec->next;
	  path_spec->next = NULL;
	  parser_free_tree (parser, path_spec);
	}
    }

  return statement;
}


/*
 * mq_set_references_local() - sets the referenced attr list of entity
 *                             specifications and its sub-entities
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
static PT_NODE *
mq_set_references_local (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_NODE * spec)
{
  PT_NODE *path_spec;

  parser_free_tree (parser, spec->info.spec.referenced_attrs);
  spec->info.spec.referenced_attrs = NULL;

  statement = parser_walk_tree (parser, statement, mq_get_references_node,
				spec, pt_continue_walk, NULL);

  path_spec = spec->info.spec.path_entities;

  for (; path_spec != NULL; path_spec = path_spec->next)
    {
      statement = mq_set_references_local (parser, statement, path_spec);
    }

  return statement;
}


/*
 * mq_set_references() - sets the referenced attr list of an entity
 *                       specification and all sub-entities
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec(in):
 */
PT_NODE *
mq_set_references (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * spec)
{
  /* don't mess with psuedo specs */
  if (spec->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
    {
      return statement;
    }

  statement = mq_reset_ids (parser, statement, spec);

  statement = mq_reset_paths (parser, statement, spec);

  statement = mq_set_references_local (parser, statement, spec);

  return statement;
}


/*
 * mq_reset_select_spec_node() - re-sets copied spec symbol table information
 * for a select which has just been substituted as a lambda argument in a view
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_select_spec_node (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *void_arg, int *continue_walk)
{
  PT_RESET_SELECT_SPEC_INFO *info = (PT_RESET_SELECT_SPEC_INFO *) void_arg;

  if (node->node_type == PT_SPEC && node->info.spec.id == info->id)
    {
      *info->statement = mq_reset_ids_and_references
	(parser, *info->statement, node);
      *info->statement = mq_translate_paths (parser, *info->statement, node);
      *info->statement = mq_reset_paths (parser, *info->statement, node);
    }

  return node;
}


/*
 * mq_reset_select_specs() - re-sets spec symbol table information for a select
 *      which has just been substituted as a lambda argument in a view
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_reset_select_specs (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  PT_NODE **statement = (PT_NODE **) void_arg;
  PT_RESET_SELECT_SPEC_INFO info;
  PT_NODE *spec;

  if (node->node_type == PT_SELECT)
    {
      spec = node->info.query.q.select.from;
      info.statement = statement;
      for (; spec != NULL; spec = spec->next)
	{
	  info.id = spec->info.spec.id;

	  /* now we know which specs must get reset.
	   * we need to find each instance of this spec in the
	   * statement, and reset it. */
	  *statement = parser_walk_tree (parser, *statement,
					 mq_reset_select_spec_node, &info,
					 NULL, NULL);
	}
    }

  return node;
}


/*
 * mq_reset_specs_from_column() - finds every select in column, then resets
 *                                id's and paths from that selects spec
 *   return:
 *   parser(in):
 *   statement(in):
 *   column(in):
 */
static PT_NODE *
mq_reset_specs_from_column (PARSER_CONTEXT * parser, PT_NODE * statement,
			    PT_NODE * column)
{
  parser_walk_tree (parser, column, mq_reset_select_specs, &statement, NULL,
		    NULL);

  return statement;
}


/*
 * mq_new_spec() - Create a new spec, given a class name
 *   return:
 *   parser(in):
 *   class_name(in):
 */
static PT_NODE *
mq_new_spec (PARSER_CONTEXT * parser, const char *class_name)
{
  PT_NODE *class_spec, *chk_parent = NULL;

  if ((class_spec = parser_new_node (parser, PT_SPEC)) == NULL)
    {
      return NULL;
    }
  class_spec->info.spec.id = (UINTPTR) class_spec;
  class_spec->info.spec.only_all = PT_ONLY;
  class_spec->info.spec.meta_class = PT_META_CLASS;
  if ((class_spec->info.spec.entity_name =
       pt_name (parser, class_name)) == NULL)
    {
      return NULL;
    }
  class_spec = parser_walk_tree (parser, class_spec, pt_flat_spec_pre,
				 &chk_parent, pt_continue_walk, NULL);
  return class_spec;
}


/*
 * mq_replace_name_with_path() - replace them with copies of path supplied,
 *                               ending in name node
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 *
 * Note:
 * ONLY do this for names matching the input expressions spec_id, which
 * is passed in in the info structure. Other names may be unrelated names
 * from subqueries in the expression being walked
 */
static PT_NODE *
mq_replace_name_with_path (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *void_arg, int *continue_walk)
{
  REPLACE_NAME_INFO *info = (REPLACE_NAME_INFO *) void_arg;
  PT_NODE *path = info->path;
  PT_NODE *next;
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == info->spec_id
      && (node->info.name.meta_class == PT_NORMAL
	  || node->info.name.meta_class == PT_SHARED
	  || node->info.name.meta_class == PT_OID_ATTR
	  || node->info.name.meta_class == PT_VID_ATTR))
    {
      next = node->next;
      if (node->info.name.resolved)
	{
	  /* names appearing in right side of dot expressions should not
	   * be replaced. We take advantage of the fact that these do not
	   * have "resolved" set, to identify those names not to touch.
	   * All other names should have "resolved" set, and be handled here.
	   */
	  path = parser_copy_tree (parser, path);
	  if (path)
	    {
	      /* now make this a legitimate path right hand
	       * and make it print right, by setting its resolved to NULL.
	       */
	      node->info.name.resolved = NULL;
	      path->info.expr.arg2 = node;
	      path->type_enum = node->type_enum;
	      parser_free_tree (parser, path->data_type);
	      path->data_type = parser_copy_tree (parser, node->data_type);
	      node = path;
	      node->next = next;
	    }
	}

      *continue_walk = PT_LIST_WALK;
    }

  if (node->node_type == PT_DATA_TYPE)
    {
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * mq_substitute_path() -
 *   return:
 *   parser(in):
 *   node(in):
 *   path_info(in):
 */
static PT_NODE *
mq_substitute_path (PARSER_CONTEXT * parser, PT_NODE * node,
		    PATH_LAMBDA_INFO * path_info)
{
  PT_NODE *column;
  PT_NODE *next;
  REPLACE_NAME_INFO info;
  PT_NODE *query_spec_column = path_info->lambda_expr;
  UINTPTR spec_id = path_info->spec_id;

  /* prune other columns and copy   */
  column = parser_copy_tree (parser, query_spec_column);

  if (column->node_type == PT_NAME)
    {
      if (column->info.name.meta_class == PT_SHARED)
	{
	  PT_NODE *new_spec = mq_new_spec
	    (parser, db_get_class_name (column->info.name.db_object));
	  path_info->new_specs =
	    parser_append_node (new_spec, path_info->new_specs);
	  column->info.name.spec_id = new_spec->info.spec.id;
	  column->next = node->next;
	  column->line_number = node->line_number;
	  column->column_number = node->column_number;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = column;
	}
      else
#if 0
      if (PT_IS_OID_NAME (column))
	{
	  /* path collapses a notch! */
	  next = node->next;
	  node = node->info.expr.arg1;
	  node->next = next;
	}
      else
#endif /* 0 */
	{
	  parser_free_tree (parser, node->info.expr.arg2);
	  node->info.expr.arg2 = column;
	  column->info.name.resolved = NULL;	/* make it print right */
	  if (node->data_type)
	    {
	      parser_free_tree (parser, node->data_type);
	    }
	  node->data_type = parser_copy_tree (parser, column->data_type);
	}
    }
  else
    {
      next = node->next;
      parser_free_tree (parser, node->info.expr.arg2);
      node->info.expr.arg2 = NULL;
      node->next = NULL;
      info.path = node;
      info.spec_id = spec_id;
      node = parser_walk_tree (parser, column, mq_replace_name_with_path,
			       (void *) &info, pt_continue_walk, NULL);
      if (node)
	{
	  node->next = next;
	  if (node->node_type == PT_EXPR)
	    {
	      /* if we replace a path expression with an expression,
	       * put parenthesis around it, because we are likely IN another
	       * expression. If we need to print the outer expression,
	       * parenthesis gurantee the proper expression precedence.
	       */
	      node->info.expr.paren_type = 1;
	    }
	}
    }

  return node;
}


/*
 * mq_substitute_path_pre() - tests and substitutes for path expressions
 *                            matching the given name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_substitute_path_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			void *void_arg, int *continue_walk)
{
  PT_NODE *arg2;
  PT_NODE *next;
  PATH_LAMBDA_INFO *info = (PATH_LAMBDA_INFO *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_DOT_
      && (arg2 = node->info.dot.arg2)
      && pt_name_equal (parser, arg2, &(info->lambda_name)))
    {
      /* need to replace node with the converted expression */
      node = mq_substitute_path (parser, node, info);

      /* no need to revisit these leaves */
      *continue_walk = PT_LIST_WALK;
    }
  else if (node->node_type == PT_NAME)
    {
      if (pt_name_equal (parser, node, &(info->lambda_name)))
	{
	  /* this is a name reference in a spec somewhere */
	  next = node->next;
	  node->next = NULL;
	  parser_free_tree (parser, node);

	  node = parser_copy_tree (parser, info->lambda_expr);
	  node->next = next;
	}

      /* no need to revisit these leaves */
      *continue_walk = PT_LIST_WALK;
    }

  return node;
}


/*
 * mq_path_name_lambda() - Search the tree for path expression right hand sides
 *                         matching the given name, and do path substitution on
 *                         those path expressions with the supplied argument
 *   return:
 *   parser(in):
 *   statement(in):
 *   lambda_name(in):
 *   lambda_expr(in):
 *   spec_id(in):
 */
static PT_NODE *
mq_path_name_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		     PT_NODE * lambda_name, PT_NODE * lambda_expr,
		     UINTPTR spec_id)
{
  PATH_LAMBDA_INFO info;

  /* copy the name because the reference is one of the things
   * that will be replaced.
   */
  info.lambda_name = *lambda_name;
  info.lambda_expr = lambda_expr;
  info.spec_id = spec_id;
  info.new_specs = NULL;

  return parser_walk_tree (parser, statement,
			   mq_substitute_path_pre, &info, pt_continue_walk,
			   NULL);
}


/*
 * mq_reset_spec_distr_subpath_pre() - moving specs from the sub-path list to
 *      the immediate path_entities list, and resetting ids in the statement
 *   return:
 *   parser(in):
 *   spec(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_reset_spec_distr_subpath_pre (PARSER_CONTEXT * parser, PT_NODE * spec,
				 void *void_arg, int *continue_walk)
{
  SPEC_RESET_INFO *info = (SPEC_RESET_INFO *) void_arg;

  if (spec == info->old_next)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      *continue_walk = PT_CONTINUE_WALK;
    }

  return spec;
}

/*
 * mq_reset_spec_distr_subpath_post() -
 *   return:
 *   parser(in):
 *   spec(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_reset_spec_distr_subpath_post (PARSER_CONTEXT * parser, PT_NODE * spec,
				  void *void_arg, int *continue_walk)
{
  SPEC_RESET_INFO *info = (SPEC_RESET_INFO *) void_arg;
  PT_NODE **sub_paths = info->sub_paths;
  PT_NODE *subspec = *sub_paths;
  PT_NODE *subspec_term;
  PT_NODE *arg1;

  *continue_walk = PT_CONTINUE_WALK;	/* un-prune other sub-branches */

  if (spec != info->old_next && spec->node_type == PT_SPEC)
    {
      for (; subspec != NULL; subspec = *sub_paths)
	{
	  subspec_term = subspec->info.spec.path_conjuncts;
	  arg1 = subspec_term->info.expr.arg1;

	  if ((arg1->node_type == PT_NAME
	       && spec->info.spec.id == arg1->info.name.spec_id)
	      || pt_find_id (parser, arg1, spec->info.spec.id))
	    {
	      /* a match. link it to this spec path entities */
	      *sub_paths = subspec->next;
	      subspec->next = spec->info.spec.path_entities;
	      spec->info.spec.path_entities = subspec;
	    }
	  else
	    {
	      /* otherwise advance down the list with no side effects */
	      sub_paths = &subspec->next;
	    }
	}

      /* now that the sub-specs (if any) are attached, we can reset spec_ids
       * and references.
       */
      info->statement = mq_reset_ids_and_references
	(parser, info->statement, spec);
    }

  return spec;
}


/*
 * mq_path_spec_lambda() - Replace old_spec (virtual) with new_spec (real)
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in): points to the spec of the left hand side of the path
 *   prev_ptr(in): points to the reference to old_spec
 *   old_spec(out):
 *   new_spec(in):
 *
 * Note:
 * If the new_spec is a join, this is an error. Only updatable
 * new_specs should be candidates. However, previous checks should
 * have already caught this.
 *
 * If the new_spec has path_entities, then the immedieate sub-path entities
 * of the old_spec must be distributed amoung the new_spec spec nodes.
 */
static PT_NODE *
mq_path_spec_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		     PT_NODE * root_spec, PT_NODE ** prev_ptr,
		     PT_NODE * old_spec, PT_NODE * new_spec)
{
  PT_NODE *root_flat;
  PT_NODE *old_flat;
  PT_NODE *new_flat;
  PT_NODE *sub_paths;

  root_flat = root_spec->info.spec.flat_entity_list;
  if (!root_flat)
    {
      /* its a derived table */
      root_flat =
	old_spec->info.spec.path_conjuncts->info.expr.arg1->data_type->info.
	data_type.entity;
    }
  old_flat = old_spec->info.spec.flat_entity_list;
  new_flat = new_spec->info.spec.flat_entity_list;

  sub_paths = old_spec->info.spec.path_entities;
  old_spec->info.spec.path_entities = NULL;

  if (new_spec->next)
    {
      PT_ERRORmf2 (parser, old_spec, MSGCAT_SET_PARSER_RUNTIME,
		   MSGCAT_RUNTIME_VC_COMP_NOT_UPDATABL,
		   old_flat->info.name.original,
		   new_flat->info.name.original);
    }

  *prev_ptr = new_spec;
  new_spec->next = old_spec->next;
  old_spec->next = NULL;
  new_spec->info.spec.path_conjuncts = old_spec->info.spec.path_conjuncts;
  old_spec->info.spec.path_conjuncts = NULL;
  new_spec->line_number = old_spec->line_number;
  new_spec->column_number = old_spec->column_number;

  if (new_spec->info.spec.path_entities)
    {
      SPEC_RESET_INFO spec_reset;
      /* reset the spec_id's */
      spec_reset.statement = statement;
      spec_reset.sub_paths = &sub_paths;
      spec_reset.old_next = new_spec->next;

      new_spec = parser_walk_tree (parser, new_spec,
				   mq_reset_spec_distr_subpath_pre,
				   &spec_reset,
				   mq_reset_spec_distr_subpath_post,
				   &spec_reset);

      statement = spec_reset.statement;
    }
  else
    {
      /* The swap is one for one. All old sub paths must be
       * direct sub-paths.  */
      new_spec->info.spec.path_entities = sub_paths;

      /* reset the spec_id's */
      statement = mq_reset_ids_and_references (parser, statement, new_spec);
    }

  parser_free_tree (parser, old_spec);

  return statement;
}


/*
 * mq_translate_paths() - translates the composition virtual references to real
 *   return:
 *   parser(in):
 *   statement(in):
 *   root_spec(in):
 *
 * Note:
 * eg ldb vclass foo ( a ... ) as select 42+b... from ldb.foo1
 *    ldb vclass baa ( x foo ... ) select x1, ... from ldb.baa1
 *
 *      "select x.a from baa" has been translated by the above to
 *  "select x1.a from ldb.baa1"
 *  Now "a" nneds to be translated to "42+b", to result in
 *  "select 42+x1.b from ldb.baa1"
 *
 * The list of immediate sub-paths must be re-distributed amoung the
 * resulting real path specs. In the trivial case in which there is
 * a one to one correspondance, this means simply setting the path_entities
 * as it was before. Otherwise the name id's of each spec in the immediate
 * sub-path must be matched against the n candidate real specs, and appended
 * to its path_entities list.
 */
static PT_NODE *
mq_translate_paths (PARSER_CONTEXT * parser, PT_NODE * statement,
		    PT_NODE * root_spec)
{
  PT_NODE *references;
  PT_NODE *reference_list;
  PT_NODE *path_spec;
  PT_NODE *next;
  PT_NODE *flat;
  PT_NODE *join_term;
  PT_NODE **prev_ptr;
  PT_NODE *real_class;
  PT_NODE *expr;
  UINTPTR spec_id;
  PT_NODE *query_spec;
  PT_MISC_TYPE path_type;	/* 0, or PT_PATH_INNER */

  prev_ptr = &root_spec->info.spec.path_entities;
  path_spec = *prev_ptr;
  while (path_spec && statement)
    {
      flat = path_spec->info.spec.flat_entity_list;
      join_term = path_spec->info.spec.path_conjuncts;
      if (!join_term)
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      else if (flat && flat->info.name.meta_class == PT_CLASS	/* NOT PT_META_CLASS */
	       && (db_is_vclass (flat->info.name.db_object)))
	{
	  next = path_spec->next;
	  references = mq_get_references (parser, statement, path_spec);
	  reference_list = references;	/* to be freed later */
	  real_class = join_term->info.expr.arg1->
	    data_type->info.data_type.entity;
	  path_type = path_spec->info.spec.meta_class;

	  while (references)
	    {
	      expr = mq_fetch_expression_for_real_class_update
		(parser, flat->info.name.db_object, references,
		 real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT, &spec_id);

	      if (expr)
		{
		  statement = mq_path_name_lambda
		    (parser, statement, references, expr, spec_id);
		}
	      references = references->next;
	    }
	  parser_free_tree (parser, reference_list);

	  query_spec = mq_fetch_select_for_real_class_update
	    (parser, flat, real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT);
	  flat = flat->next;

	  while (flat && !query_spec)
	    {
	      query_spec = mq_fetch_select_for_real_class_update
		(parser, flat, real_class, PT_NORMAL_SELECT, DB_AUTH_SELECT);
	      flat = flat->next;
	    }

	  /* at this point, if any of the virtual classes had a matching
	   * real class_, we will have found it */
	  if (query_spec)
	    {
	      PT_NODE *temp;
	      PT_NODE *new_spec;

	      new_spec =
		parser_copy_tree_list (parser,
				       query_spec->info.query.q.select.from);

	      /* the following block of code attempts to gurantee that
	       * all candidate subclasses are copied to the entity list
	       * of the path spec we are about to create.

	       * relational proxies are made an exception, because
	       *          1) relational proxies can inherently only refer
	       *             to one table.
	       */
	      if (db_is_class (real_class->info.name.db_object)
		  || mq_is_vclass_on_oo_ldb (real_class->info.name.db_object))
		{
		  /* find all the rest of the matches */
		  for (; flat != NULL; flat = flat->next)
		    {
		      query_spec = mq_fetch_select_for_real_class_update
			(parser, flat, real_class,
			 PT_NORMAL_SELECT, DB_AUTH_SELECT);
		      if (query_spec
			  && (temp = query_spec->info.query.q.select.from)
			  && (temp = temp->info.spec.flat_entity_list)
			  && (temp = parser_copy_tree_list (parser, temp)))
			{
			  new_spec->info.spec.flat_entity_list =
			    parser_append_node (temp,
						new_spec->info.spec.
						flat_entity_list);
			  while (temp)
			    {
			      temp->info.name.spec_id =
				new_spec->info.spec.id;
			      temp = temp->next;
			    }
			}
		    }
		}

	      statement = mq_path_spec_lambda
		(parser, statement, root_spec, prev_ptr, path_spec, new_spec);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "translate");
	    }

	  path_spec = *prev_ptr;	/* this was just over-written */
	  /* if either the virtual or translated guys is an
	   * inner path (selector path) the result must be an
	   * inner path, as opposed to the usual left join path semantics
	   */
	  if (path_type == PT_PATH_INNER)
	    {
	      path_spec->info.spec.meta_class = PT_PATH_INNER;
	    }

	  /* translate virtual sub-paths */
	  statement = mq_translate_paths (parser, statement, path_spec);
	}

      prev_ptr = &path_spec->next;
      path_spec = *prev_ptr;
    }

  return statement;
}


/*
 * mq_rename_resolved() - re-sets name resolution to of an entity spec
 *                        and a tree to match a new printable name
 *   return:
 *   parser(in):
 *   spec(in):
 *   statement(in):
 *   newname(in):
 */
PT_NODE *
mq_rename_resolved (PARSER_CONTEXT * parser, PT_NODE * spec,
		    PT_NODE * statement, const char *newname)
{
  if (!spec || !spec->info.spec.range_var || !statement)
    {
      return statement;
    }

  spec->info.spec.range_var->info.name.original = newname;

  /* this is just to make sure the id is properly set.
     Its probably not necessary.  */
  spec->info.spec.range_var->info.name.spec_id = spec->info.spec.id;

  statement = parser_walk_tree (parser, statement, mq_coerce_resolved,
				spec->info.spec.range_var, pt_continue_walk,
				NULL);

  return statement;
}


/*
 * mq_occurs_in_from_list() - counts the number of times a name appears as an
 *                            exposed name in a list of entity_spec's
 *   return:
 *   parser(in):
 *   name(in):
 *   from_list(in):
 */
static int
mq_occurs_in_from_list (PARSER_CONTEXT * parser, const char *name,
			PT_NODE * from_list)
{
  PT_NODE *spec;
  int i = 0;

  if (!name || !from_list)
    {
      return i;
    }

  for (spec = from_list; spec != NULL; spec = spec->next)
    {
      if (spec->info.spec.range_var
	  && spec->info.spec.range_var->info.name.original
	  && (intl_mbs_casecmp (name,
				spec->info.spec.range_var->info.name.
				original) == 0))
	{
	  i++;
	}
    }

  return i;
}


/*
 * mq_regenerate_if_ambiguous() - regenerate the exposed name
 *                                if ambiguity is detected
 *   return:
 *   parser(in):
 *   spec(in):
 *   statement(in):
 *   from(in):
 */
PT_NODE *
mq_regenerate_if_ambiguous (PARSER_CONTEXT * parser, PT_NODE * spec,
			    PT_NODE * statement, PT_NODE * from)
{
  const char *newexposedname;
  const char *generatedname;
  int ambiguous;
  int i;


  newexposedname = spec->info.spec.range_var->info.name.original;

  if (1 < mq_occurs_in_from_list (parser, newexposedname, from))
    {
      /* Ambiguity is detected. rename the newcomer's
       * printable name to fix this.
       */
      i = 0;
      ambiguous = true;

      while (ambiguous)
	{
	  generatedname = mq_generate_name (parser, newexposedname, &i);

	  ambiguous = 0 < mq_occurs_in_from_list
	    (parser, generatedname, from);
	}

      /* generatedname is now non-ambiguous */
      statement = mq_rename_resolved (parser, spec, statement, generatedname);
    }

  return statement;
}


/*
 * mq_generate_unique() - generates a printable name not found in the name list
 *   return:
 *   parser(in):
 *   name_list(in):
 */
static PT_NODE *
mq_generate_unique (PARSER_CONTEXT * parser, PT_NODE * name_list)
{
  int ambiguous = 1;
  int i = 0;
  PT_NODE *new_name = parser_copy_tree (parser, name_list);
  PT_NODE *temp = name_list;

  while (ambiguous)
    {
      new_name->info.name.original = mq_generate_name (parser, "a", &i);
      temp = name_list;
      while (temp && intl_mbs_casecmp (new_name->info.name.original,
				       temp->info.name.original) != 0)
	{
	  temp = temp->next;
	}
      if (!temp)
	{
	  ambiguous = 0;
	}
    }

  return new_name;
}


/*
 * mq_invert_insert_select() - invert sub-query select lists
 *   return:
 *   parser(in):
 *   attr(in):
 *   subquery(in):
 */
static void
mq_invert_insert_select (PARSER_CONTEXT * parser, PT_NODE * attr,
			 PT_NODE * subquery)
{
  PT_NODE **value;
  PT_NODE *value_next;
  PT_NODE *result;

  value = &subquery->info.query.q.select.list;

  while (*value)
    {
      if (!attr)
	{
	  /* system error, should be caught in semantic pass */
	  PT_ERRORm (parser, (*value), MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS);
	  return;
	}
      value_next = (*value)->next;
      (*value)->next = NULL;

      (*value) = mq_translate_value (parser, *value);
      result = pt_invert (parser, attr, *value);

      if (!result)
	{
	  /* error not invertable/updatable */
	  /* don't want to repeat this error */
	  if (!parser->error_msgs)
	    {
	      PT_ERRORmf (parser, attr, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
			  pt_short_print (parser, attr));
	    }
	  return;
	}

      if (result->next)
	{
	  parser_free_tree (parser, result->next);
	}

      result->next = NULL;
      (*value) = result;	/* the right hand side */

      attr = attr->next;
      (*value)->next = value_next;

      value = &(*value)->next;
    }
}


/*
 * mq_invert_insert_subquery() - invert sub-query select lists
 *   return:
 *   parser(in):
 *   attr(in):
 *   subquery(in):
 */
static void
mq_invert_insert_subquery (PARSER_CONTEXT * parser, PT_NODE ** attr,
			   PT_NODE * subquery)
{
  PT_NODE *attr_next;
  PT_NODE *result;

  switch (subquery->node_type)
    {
    case PT_SELECT:
      mq_invert_insert_select (parser, *attr, subquery);
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      mq_invert_insert_subquery (parser, attr,
				 subquery->info.query.q.union_.arg1);
      if (!parser->error_msgs)
	{
	  mq_invert_insert_subquery (parser, attr,
				     subquery->info.query.q.union_.arg2);
	}
      break;

    default:
      /* should not get here, that is an error! */
      /* its almost certainly recoverable, so ignore it */
      assert (0);
      break;
    }

  while (!parser->error_msgs && *attr)
    {
      attr_next = (*attr)->next;
      (*attr)->next = NULL;

      pt_find_var (*attr, &result);

      if (!result)
	{
	  /* error not invertable/updatable already set */
	  return;
	}

      (*attr) = result;		/* the name */

      (*attr)->next = attr_next;

      attr = &(*attr)->next;
    }
}

/*
 * mq_make_derived_spec() -
 *   return:
 *   parser(in):
 *   node(in):
 *   subquery(in):
 *   idx(in):
 *   spec_ptr(out):
 *   attr_list_ptr(out):
 */
PT_NODE *
mq_make_derived_spec (PARSER_CONTEXT * parser, PT_NODE * node,
		      PT_NODE * subquery, int *idx, PT_NODE ** spec_ptr,
		      PT_NODE ** attr_list_ptr)
{
  PT_NODE *range, *spec, *as_attr_list, *col, *tmp;

  /* remove unnecessary ORDER BY clause.
     if select list has orderby_num(), can not remove ORDER BY clause
     for example: (i, j) = (select i, orderby_num() from t order by i) */
  if (subquery->info.query.orderby_for == NULL &&
      subquery->info.query.order_by)
    {
      for (col = pt_get_select_list (parser, subquery); col; col = col->next)
	{
	  if (col->node_type == PT_EXPR &&
	      col->info.expr.op == PT_ORDERBY_NUM)
	    {
	      break;		/* can not remove ORDER BY clause */
	    }
	}

      if (!col)
	{
	  parser_free_tree (parser, subquery->info.query.order_by);
	  subquery->info.query.order_by = NULL;

	  col = pt_get_select_list (parser, subquery);
	  if (col)
	    {
	      for (tmp = col; tmp->next; tmp = tmp->next)
		{
		  if (IS_HIDDEN_COLUMN (tmp->next))
		    {
		      parser_free_tree (parser, tmp->next);
		      tmp->next = NULL;
		      break;
		    }
		}
	    }
	}
    }

  /* set line number to range name */
  range = pt_name (parser, "av1861");

  /* construct new spec */
  spec = parser_new_node (parser, PT_SPEC);
  spec->info.spec.derived_table = subquery;
  spec->info.spec.derived_table = mq_reset_ids_in_statement (parser,
							     spec->info.spec.
							     derived_table);
  spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  spec->info.spec.range_var = range;
  spec->info.spec.id = (UINTPTR) spec;
  range->info.name.spec_id = (UINTPTR) spec;

  /* add new spec to the spec list */
  node->info.query.q.select.from = parser_append_node (spec,
						       node->info.query.q.
						       select.from);
  /* set spec as unique */
  node = mq_regenerate_if_ambiguous (parser, spec, node,
				     node->info.query.q.select.from);

  /* construct new attr_list */
  spec->info.spec.as_attr_list = as_attr_list = NULL;	/* init */
  for (col = pt_get_select_list (parser, subquery); col; col = col->next)
    {

      tmp = pt_name (parser, mq_generate_name (parser, "av", idx));
      tmp->info.name.meta_class = PT_NORMAL;
      tmp->info.name.resolved = spec->info.spec.range_var->info.name.original;
      tmp->info.name.spec_id = spec->info.spec.id;
      tmp->type_enum = col->type_enum;
      tmp->data_type = parser_copy_tree (parser, col->data_type);
      /* keep out hidden columns from derived select list */
      if (subquery->info.query.order_by && IS_HIDDEN_COLUMN (col))
	{
	  SET_AS_NORMAL_COLUMN (col);	/* change to normal */
	  SET_AS_NORMAL_COLUMN (tmp);	/* change to normal */
	  spec->info.spec.as_attr_list =
	    parser_append_node (tmp, spec->info.spec.as_attr_list);
	}
      else
	{
	  spec->info.spec.as_attr_list =
	    parser_append_node (tmp, spec->info.spec.as_attr_list);
	  as_attr_list =
	    parser_append_node (parser_copy_tree (parser, tmp), as_attr_list);
	}
    }

  /* save spec, attr */
  if (spec_ptr)
    {
      *spec_ptr = spec;
    }

  if (attr_list_ptr)
    {
      *attr_list_ptr = as_attr_list;
    }

  return node;
}				/* mq_make_derived_spec */

/*
 * mq_class_lambda() - replace class specifiers with their corresponding
 *                     virtual from list
 *   return:
 *   parser(in):
 *   statement(in):
 *   class(in):
 *   corresponding_spec(in):
 *   class_where_part(in):
 *   class_check_part(in):
 *   class_group_by_part(in):
 *   class_having_part(in):
 *
 * Note:
 * A subset of general statements is handled, being
 *      select - replace the "entity_spec" node in from list
 *               containing class in its flat_entity_list
 *               append the where_part, if any.
 *      update - replace the "entity_spec" node in entity_spec
 *               if it contains class in its flat_entity_list
 *               append the where_part, if any.
 *      insert - replace the "name" node equal to class
 *      union, difference, intersection
 *             - the recursive result of this function on both arguments.
 */
PT_NODE *
mq_class_lambda (PARSER_CONTEXT * parser, PT_NODE * statement,
		 PT_NODE * class_, PT_NODE * corresponding_spec,
		 PT_NODE * class_where_part, PT_NODE * class_check_part,
		 PT_NODE * class_group_by_part, PT_NODE * class_having_part)
{
  PT_NODE *spec;
  PT_NODE **specptr = NULL;
  PT_NODE **where_part = NULL;
  PT_NODE **check_where_part = NULL;
  PT_NODE *newspec = NULL;
  PT_NODE *oldnext = NULL;
  PT_NODE *assign, *result;
  PT_NODE **attr, *attr_next;
  PT_NODE **value, *value_next;
  PT_NODE **lhs, **rhs, *lhs_next, *rhs_next;
  const char *newresolved = class_->info.name.resolved;

  switch (statement->node_type)
    {
    case PT_SELECT:
      statement->info.query.is_subquery = PT_IS_SUBQUERY;

      specptr = &statement->info.query.q.select.from;
      where_part = &statement->info.query.q.select.where;
      check_where_part = &statement->info.query.q.select.check_where;

      if (class_group_by_part || class_having_part)
	{
	  /* check for derived */
	  if (statement->info.query.vspec_as_derived == 1)
	    {
	      /* set GROUP BY */
	      if (class_group_by_part)
		{
		  if (statement->info.query.q.select.group_by)
		    {
		      /* this is impossible case. give up */
		      return NULL;
		    }
		  else
		    {
		      statement->info.query.q.select.group_by =
			parser_copy_tree_list (parser, class_group_by_part);
		    }
		}

	      /* merge HAVING */
	      if (class_having_part)
		{
		  PT_NODE **having_part;

		  having_part = &statement->info.query.q.select.having;

		  *having_part =
		    parser_append_node (parser_copy_tree_list
					(parser, class_having_part),
					*having_part);
		}
	    }
	  else
	    {
	      statement = NULL;	/* system error */
	    }
	}
      break;


    case PT_UPDATE:
      specptr = &statement->info.update.spec;
      where_part = &statement->info.update.search_cond;
      check_where_part = &statement->info.update.check_where;

      for (assign = statement->info.update.assignment; assign != NULL;
	   assign = assign->next)
	{
	  /* get lhs, rhs */
	  lhs = &(assign->info.expr.arg1);
	  rhs = &(assign->info.expr.arg2);
	  if (PT_IS_N_COLUMN_UPDATE_EXPR (*lhs))
	    {
	      /* get lhs element */
	      lhs = &((*lhs)->info.expr.arg1);

	      /* get rhs element */
	      rhs = &((*rhs)->info.query.q.select.list);
	    }

	  for (; *lhs && *rhs; *lhs = lhs_next, *rhs = rhs_next)
	    {
	      /* cut-off and save next link */
	      lhs_next = (*lhs)->next;
	      (*lhs)->next = NULL;
	      rhs_next = (*rhs)->next;
	      (*rhs)->next = NULL;

	      *rhs = mq_translate_value (parser, *rhs);

	      result = pt_invert (parser, *lhs, *rhs);
	      if (!result)
		{
		  /* error not invertible/updatable */
		  PT_ERRORmf (parser, assign, MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
			      pt_short_print (parser, *lhs));
		  return NULL;
		}

	      if (*lhs)
		{
		  parser_free_tree (parser, *lhs);
		}
	      *lhs = result->next;	/* the name */
	      result->next = NULL;
	      *rhs = result;	/* the right hand side */

	      lhs = &((*lhs)->next);
	      rhs = &((*rhs)->next);
	    }
	}
      break;

    case PT_DELETE:
      specptr = &statement->info.delete_.spec;
      where_part = &statement->info.delete_.search_cond;
      break;

    case PT_INSERT:
      specptr = &statement->info.insert.spec;
      check_where_part = &statement->info.insert.where;

      /* need to invert expressions now */
      attr = &statement->info.insert.attr_list;
      value = &statement->info.insert.value_clause;

      if (statement->info.insert.is_value == PT_IS_VALUE)
	{
	  while (*value)
	    {
	      if (!*attr)
		{
		  /* system error, should be caught in semantic pass */
		  PT_ERRORm (parser, (*value), MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_ATTRS_GT_QSPEC_COLS);
		  statement = NULL;
		  break;
		}
	      attr_next = (*attr)->next;
	      value_next = (*value)->next;
	      (*attr)->next = NULL;
	      (*value)->next = NULL;

	      (*value) = mq_translate_value (parser, *value);
	      result = pt_invert (parser, *attr, *value);

	      if (!result)
		{
		  /* error not invertable/updatable */
		  PT_ERRORmf (parser, (*attr), MSGCAT_SET_PARSER_RUNTIME,
			      MSGCAT_RUNTIME_VASG_TGT_UNINVERTBL,
			      pt_short_print (parser, (*attr)));
		  statement = NULL;
		  break;
		}

	      if (*attr)
		{
		  parser_free_tree (parser, *attr);
		}

	      (*attr) = result->next;	/* the name */
	      result->next = NULL;
	      (*value) = result;	/* the right hand side */

	      (*attr)->next = attr_next;
	      (*value)->next = value_next;

	      attr = &(*attr)->next;
	      value = &(*value)->next;
	    }
	}
      else if (statement->info.insert.is_value == PT_IS_SUBQUERY)
	{
	  mq_invert_insert_subquery (parser, attr, *value);
	}
      break;

#if 0				/* this is impossible case */
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      statement->info.query.q.union_.arg1 =
	mq_class_lambda (parser, statement->info.query.q.union_.arg1,
			 class_, corresponding_spec, class_where_part,
			 class_check_part, class_group_by_part,
			 class_having_part);
      statement->info.query.q.union_.arg2 =
	mq_class_lambda (parser, statement->info.query.q.union_.arg2,
			 class_, corresponding_spec, class_where_part,
			 class_check_part, class_group_by_part,
			 class_having_part);
      break;
#endif /* this is impossible case */

    default:
      /* system error */
      statement = NULL;
      break;
    }

  if (!statement)
    {
      return NULL;
    }

  /* handle is a where parts of view sub-querys */
  if (where_part)
    {
      /* force sub expressions to be parenthesised for correct
       * printing. Otherwise, the associativity may be wrong when
       * the statement is printed and sent to a local database
       */
      if (class_where_part && class_where_part->node_type == PT_EXPR)
	{
	  class_where_part->info.expr.paren_type = 1;
	}
      if ((*where_part) && (*where_part)->node_type == PT_EXPR)
	{
	  (*where_part)->info.expr.paren_type = 1;
	}
      /* The "where clause" is in the form of a list of CNF "and" terms.
       * In order to "and" together the view's "where clause" with the
       * statement's, we must maintain this list of terms.
       * Using a 'PT_AND' node here will have the effect of losing the
       * "and" terms on the tail of either list.
       */
      *where_part =
	parser_append_node (parser_copy_tree_list (parser, class_where_part),
			    *where_part);
    }
  if (check_where_part)
    {
      if (class_check_part && class_check_part->node_type == PT_EXPR)
	{
	  class_check_part->info.expr.paren_type = 1;
	}
      if ((*check_where_part) && (*check_where_part)->node_type == PT_EXPR)
	{
	  (*check_where_part)->info.expr.paren_type = 1;
	}
      *check_where_part =
	parser_append_node (parser_copy_tree_list (parser, class_check_part),
			    *check_where_part);
    }

  if (specptr)
    {
      spec = *specptr;
      while (spec && class_->info.name.spec_id != spec->info.spec.id)
	{
	  specptr = &spec->next;
	  spec = *specptr;
	}
      if (spec)
	{
	  SPEC_RESET_INFO spec_reset;
	  PT_NODE *subpaths;

	  newspec = parser_copy_tree_list (parser, corresponding_spec);
	  oldnext = spec->next;
	  spec->next = NULL;
	  subpaths = spec->info.spec.path_entities;
	  spec_reset.sub_paths = &subpaths;
	  spec_reset.statement = statement;
	  spec_reset.old_next = oldnext;
	  spec->info.spec.path_entities = NULL;
	  if (newspec)
	    {
	      newspec->info.spec.range_var->info.name.original =
		spec->info.spec.range_var->info.name.original;
	      newspec->info.spec.location = spec->info.spec.location;
	      /* move join info */
	      if (spec->info.spec.join_type != PT_JOIN_NONE)
		{
		  newspec->info.spec.join_type = spec->info.spec.join_type;
		  newspec->info.spec.on_cond = spec->info.spec.on_cond;
		  spec->info.spec.on_cond = NULL;
		}
	    }
	  parser_free_tree (parser, spec);

	  if (newspec)
	    {
	      *specptr = newspec;
	      parser_append_node (oldnext, newspec);

	      newspec = parser_walk_tree (parser, newspec,
					  mq_reset_spec_distr_subpath_pre,
					  &spec_reset,
					  mq_reset_spec_distr_subpath_post,
					  &spec_reset);

	      statement = spec_reset.statement;
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "translate");
	      statement = NULL;
	    }
	}
      else
	{
	  /* we are doing a null substitution. ie the classes dont match
	     the spec. The "correct translation" is NULL.  */
	  statement = NULL;
	}
    }

  if (statement)
    {
      /* The spec id's are those copied from the cache.
       * They are unique in this statment tree, but will not be unique
       * if this tree is once more translated against the same
       * virtual class_. Now, the newly introduced entity specs,
       * are gone through and the id's for each and each name reset
       * again to a new (uncopied) unique number, to preserve the uniqueness
       * of the specs.
       */
      for (spec = newspec; spec != NULL; spec = spec->next)
	{
	  if (spec == oldnext)
	    {
	      break;		/* these are already ok */
	    }

	  /* translate virtual sub-paths */
	  statement = mq_translate_paths (parser, statement, spec);

	  /* reset ids of path specs, or toss them, as necessary */
	  statement = mq_reset_paths (parser, statement, spec);

	}

      /* After substituting, we will have an internally "correct"
       * translation of the tree.
       * However, the external representation may be ambiguous because of
       * aliasing introduced in translation. For example,
       *              create fooview
       *              as select * from foo where foo.z < 5;
       * Then
       *              select foo.x from foo, fooview
       *                           where foo.y = fooview.y;
       * translates to
       *              select foo.x from foo, foo where foo.y = foo.y
       *                                           and foo.z <5;
       * Internally, there are identifiers to distinguish foo.x .y and .z
       * but the printed representation is ambiguos. The following
       * detects this case, and corrects for it. The chosen algorithm is
       * to generate a new unique name by appending a numeral, when
       * an ambiguity is detected.
       */

      if (newspec)
	{
	  if (!PT_IS_QUERY_NODE_TYPE (statement->node_type))
	    {
	      /* PT_INSERT, PT_UPDATE, PT_DELETE */
	      statement = mq_rename_resolved (parser, newspec,
					      statement, newresolved);
	      newspec = newspec->next;
	    }
	  for (spec = newspec; spec != NULL; spec = spec->next)
	    {
	      if (spec == oldnext || statement == NULL)
		{
		  break;	/* these are already ok */
		}
	      if (spec->info.spec.range_var->alias_print)
		{
		  char *temp;
		  temp = pt_append_string (parser, NULL, newresolved);
		  temp = pt_append_string (parser, temp, ":");
		  temp = pt_append_string (parser, temp,
					   spec->info.spec.range_var->
					   alias_print);
		  spec->info.spec.range_var->alias_print = temp;
		}
	      else
		{
		  spec->info.spec.range_var->alias_print = newresolved;
		}
	      statement = mq_regenerate_if_ambiguous (parser, spec,
						      statement,
						      statement->info.query.q.
						      select.from);
	    }
	}
    }

  return statement;
}


/*
 * mq_push_arg2() - makes the first item of each top level select into
 *                  path expression with arg2
 *   return:
 *   parser(in):
 *   query(in):
 *   dot_arg2(in):
 */
static PT_NODE *
mq_push_arg2 (PARSER_CONTEXT * parser, PT_NODE * query, PT_NODE * dot_arg2)
{
  PT_NODE *dot;
  PT_NODE *spec = NULL;
  PT_NODE *new_spec;
  PT_NODE *name;

  switch (query->node_type)
    {
    case PT_SELECT:
      if (PT_IS_QUERY_NODE_TYPE (query->info.query.q.select.list->node_type))
	{
	  query->info.query.q.select.list = mq_push_arg2
	    (parser, query->info.query.q.select.list, dot_arg2);
	}
      else
	{
	  name = query->info.query.q.select.list;
	  if (name->node_type != PT_NAME)
	    {
	      if (name->node_type == PT_DOT_)
		{
		  name = name->info.dot.arg2;
		}
	      else if (name->node_type == PT_METHOD_CALL)
		{
		  name = name->info.method_call.method_name;
		}
	      else
		{
		  name = NULL;
		}
	    }
	  if (name)
	    {
	      spec = pt_find_entity (parser, query->info.query.q.select.from,
				     name->info.name.spec_id);
	    }

	  dot = parser_copy_tree (parser, dot_arg2);
	  dot->info.dot.arg1 = query->info.query.q.select.list;
	  query->info.query.q.select.list = dot;
	  new_spec = pt_insert_entity (parser, dot, spec, NULL);
	  parser_free_tree (parser, query->data_type);
	  query->type_enum = dot->type_enum;
	  query->data_type = parser_copy_tree_list (parser, dot->data_type);
	  query = mq_translate_paths (parser, query, spec);
	  query = mq_reset_paths (parser, query, spec);
	}
      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      query->info.query.q.union_.arg1 = mq_push_arg2
	(parser, query->info.query.q.union_.arg1, dot_arg2);
      query->info.query.q.union_.arg2 = mq_push_arg2
	(parser, query->info.query.q.union_.arg2, dot_arg2);
      parser_free_tree (parser, query->data_type);
      query->type_enum = query->info.query.q.union_.arg1->type_enum;
      query->data_type = parser_copy_tree_list
	(parser, query->info.query.q.union_.arg1->data_type);
      break;

    default:
      break;
    }

  return query;
}


/*
 * mq_lambda_node_pre() - creates extra spec frames for each select
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_lambda_node_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg,
		    int *continue_walk)
{
  MQ_LAMBDA_ARG *lambda_arg = (MQ_LAMBDA_ARG *) void_arg;
  PT_EXTRA_SPECS_FRAME *spec_frame;

  if (tree->node_type == PT_SELECT)
    {
      spec_frame =
	(PT_EXTRA_SPECS_FRAME *) malloc (sizeof (PT_EXTRA_SPECS_FRAME));
      spec_frame->next = lambda_arg->spec_frames;
      spec_frame->extra_specs = NULL;
      lambda_arg->spec_frames = spec_frame;
    }

  return tree;

}				/* mq_lambda_node_pre */


/*
 * mq_lambda_node() - applies the lambda test to the node passed to it,
 *             and conditionally substitutes a copy of its corresponding tree
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_lambda_node (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		int *continue_walk)
{
  MQ_LAMBDA_ARG *lambda_arg = (MQ_LAMBDA_ARG *) void_arg;
  PT_NODE *save_node_next, *result, *arg1, *spec;
  PT_NODE *dt1, *dt2;
  PT_EXTRA_SPECS_FRAME *spec_frame;
  PT_NODE *save_data_type;
  PT_NODE *name, *tree;

  result = node;

  switch (node->node_type)
    {

    case PT_DOT_:
      /* Check if the recursive call left an "illegal" path expression */
      if ((arg1 = node->info.dot.arg1))
	{
	  save_node_next = node->next;
	  if (PT_IS_QUERY_NODE_TYPE (arg1->node_type))
	    {
	      node->info.dot.arg1 = NULL;
	      node->next = NULL;

	      result = mq_push_arg2 (parser, arg1, node);

	      parser_free_tree (parser, node);	/* re-use this memory */
	      /* could free data_type, and entity_list here too. */

	      /* if this name was in a name list, keep the list tail */
	      if (result)
		{
		  result->next = save_node_next;
		}
	    }
	  else if (arg1->node_type == PT_NAME && PT_IS_OID_NAME (arg1))
	    {
	      /* we have an artificial path, from a view that selects
	       * an oid, eg
	       *      create view foo (a) as select x from x
	       * It would be nice to translate this to just the RHS,
	       * but subsequent path translation would have nothing to key
	       * off of.
	       */

	    }
	  else if (PT_IS_NULL_NODE (arg1))
	    {
	      /* someone did a select a.b from view, where a is a null
	       * the result is also NULL.
	       */

	      node->info.dot.arg1 = NULL;
	      node->next = NULL;

	      result = arg1;

	      parser_free_tree (parser, node);	/* re-use this memory */

	      /* if this name was in a name list, keep the list tail */
	      result->next = save_node_next;
	    }
	}
      break;

    case PT_NAME:
      for (name = lambda_arg->name_list, tree = lambda_arg->tree_list;
	   name && tree; name = name->next, tree = tree->next)
	{
	  /* If the names are equal, substitute new sub tree
	   * Here we DON't want to do the usual strict name-datatype matching.
	   * This is where we project one object attribute as another, so
	   * we deliberately allow the loosely typed match by nulling
	   * the data_type.
	   */
	  save_data_type = name->data_type;	/* save */
	  name->data_type = NULL;

	  if (pt_name_equal (parser, node, name))
	    {
	      save_node_next = node->next;
	      node->next = NULL;

	      result = parser_copy_tree (parser, tree);	/* substitute */

	      /* Keep hidden column information during view translation */
	      if (result)
		{
		  result->line_number = node->line_number;
		  result->column_number = node->column_number;
#if 0
		  result->info.name.original = node->info.name.original;
#endif /* 0 */
		}

	      /* we may have just copied a whole query,
	       * if so, reset its id's */
	      result = mq_reset_specs_from_column (parser, result, tree);

	      /* If this is a shared attribute referenced in a query,
	       * we must create the extra class spec for the class
	       * attribute scan.
	       * We need this extra scan for SHARED attrs because shared
	       * attrs for views and proxies can not be translated down to
	       * a base class scan.
	       * Note: We want to create an extra spec if the VCLAS attribute
	       * is a shared attr. If the BASE attribute is a shared attr,
	       * the normal mechanisms for dealing with them will suffice.
	       */
	      if (lambda_arg->spec_frames
		  && node->info.name.meta_class == PT_SHARED)
		{
		  PT_NODE *class_spec;
		  PT_NODE *entity;

		  /* check for found */
		  for (class_spec = lambda_arg->spec_frames->extra_specs;
		       class_spec; class_spec = class_spec->next)
		    {
		      entity = class_spec->info.spec.entity_name;
		      if (!intl_mbs_casecmp (entity->info.name.original,
					     result->info.name.resolved))
			{
			  break;	/* found */
			}
		    }

		  if (!class_spec)
		    {		/* not found */
		      class_spec =
			mq_new_spec (parser, result->info.name.resolved);
		      if (class_spec == NULL)
			{
			  return NULL;
			}

		      /* add the new spec to the extra_specs */
		      lambda_arg->spec_frames->extra_specs =
			parser_append_node (class_spec,
					    lambda_arg->spec_frames->
					    extra_specs);
		    }

		  /* resolve the name node to the new spec */
		  result->info.name.spec_id = class_spec->info.spec.id;
		}

	      parser_free_tree (parser, node);	/* re-use this memory */

	      result->next = save_node_next;

	      name->data_type = save_data_type;	/* restore */

	      break;		/* exit for-loop */

	    }			/* if (pt_name_equal(parser, node, name)) */

	  /* name did not match. go ahead */
	  name->data_type = save_data_type;	/* restore */

	}			/* for ( ... ) */

      break;

    case PT_SELECT:
      /* maintain virtual data type information */
      if ((dt1 = result->data_type)
	  && result->info.query.q.select.list
	  && (dt2 = result->info.query.q.select.list->data_type))
	{
	  parser_free_tree (parser, result->data_type);
	  result->data_type = parser_copy_tree_list (parser, dt2);
	}
      /* pop the extra spec frame and add any extra specs to the from list */
      spec_frame = lambda_arg->spec_frames;
      lambda_arg->spec_frames = lambda_arg->spec_frames->next;
      result->info.query.q.select.from =
	parser_append_node (spec_frame->extra_specs,
			    result->info.query.q.select.from);

      /* adding specs may have created ambiguous spec names */
      for (spec = spec_frame->extra_specs; spec != NULL; spec = spec->next)
	{
	  result = mq_regenerate_if_ambiguous (parser, spec, result,
					       result->info.query.q.select.
					       from);
	}

      free_and_init (spec_frame);
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* maintain virtual data type information */
      if ((dt1 = result->data_type)
	  && result->info.query.q.union_.arg1
	  && (dt2 = result->info.query.q.union_.arg1->data_type))
	{
	  parser_free_tree (parser, result->data_type);
	  result->data_type = parser_copy_tree_list (parser, dt2);
	}
      break;

    default:
      break;
    }

  return result;
}

/*
 * mq_lambda() - modifies name nodes with copies of a corresponding tree
 *   return:
 *   parser(in):
 *   tree_with_names(in):
 *   name_node_list(in):
 *   corresponding_tree_list(in):
 */
PT_NODE *
mq_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
	   PT_NODE * name_node_list, PT_NODE * corresponding_tree_list)
{
  MQ_LAMBDA_ARG lambda_arg;
  PT_NODE *tree;
  PT_NODE *name;

  lambda_arg.name_list = name_node_list;
  lambda_arg.tree_list = corresponding_tree_list;
  lambda_arg.spec_frames = NULL;

  for (name = lambda_arg.name_list, tree = lambda_arg.tree_list;
       name && tree; name = name->next, tree = tree->next)
    {
      if (tree->node_type == PT_EXPR)
	{
	  /* make sure it will print with proper precedance.
	   * we don't want to replace "name" with "1+2"
	   * in 4*name, and get 4*1+2. It should be 4*(1+2) instead.
	   */
	  tree->info.expr.paren_type = 1;
	}

      if (name->node_type != PT_NAME)
	{			/* unkonwn error */
	  tree = tree_with_names;
	  goto exit_on_error;
	}

    }


  tree = parser_walk_tree (parser, tree_with_names,
			   mq_lambda_node_pre, &lambda_arg,
			   mq_lambda_node, &lambda_arg);

exit_on_error:

  return tree;
}


/*
 * mq_set_virt_object() - checks and sets name nodes of object type
 *                        virtual object information
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_set_virt_object (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
		    int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;
  PT_NODE *dt;
  PT_NODE *cls;

  if (node->node_type == PT_NAME
      && node->info.name.spec_id == spec->info.spec.id
      && (dt = node->data_type)
      && node->type_enum == PT_TYPE_OBJECT
      && (cls = dt->info.data_type.entity))
    {
      if (db_is_vclass (cls->info.name.db_object))
	{
	  dt->info.data_type.virt_object = cls->info.name.db_object;
	  if (mq_is_updatable (cls->info.name.db_object))
	    {
	      PARSER_CONTEXT *query_cache;
	      PT_NODE *flat;

	      flat =
		mq_fetch_one_real_class_get_cache (cls->info.name.db_object,
						   &query_cache);

	      if (flat)
		{
		  dt->info.data_type.entity =
		    parser_copy_tree_list (parser, flat);
		}
	    }
	  else
	    {
	      dt->info.data_type.entity = NULL;
	    }
	  parser_free_tree (parser, cls);
	}
    }

  return node;
}


/*
 * mq_fix_derived() - fixes derived table and checks for virtual object types
 *   return:
 *   parser(in):
 *   select_statement(in):
 *   spec(in):
 */
static PT_NODE *
mq_fix_derived (PARSER_CONTEXT * parser, PT_NODE * select_statement,
		PT_NODE * spec)
{
  PT_NODE *attr = spec->info.spec.as_attr_list;
  PT_NODE *attr_next;
  PT_NODE *dt;
  PT_NODE *cls;
  int had_virtual, any_had_virtual;

  any_had_virtual = 0;
  while (attr)
    {
      dt = attr->data_type;
      had_virtual = 0;
      if (dt && attr->type_enum == PT_TYPE_OBJECT)
	{
	  cls = dt->info.data_type.entity;
	  while (cls)
	    {
	      if (db_is_vclass (cls->info.name.db_object))
		{
		  dt->info.data_type.virt_object = cls->info.name.db_object;
		  had_virtual = 1;
		}
	      cls = cls->next;
	    }
	}
      attr_next = attr->next;
      if (had_virtual)
	{
	  any_had_virtual = 1;
	}
      attr = attr_next;
    }

  mq_reset_ids (parser, select_statement, spec);

  if (any_had_virtual)
    {
      select_statement = parser_walk_tree
	(parser, select_statement, mq_set_virt_object, spec, NULL, NULL);
      select_statement = mq_translate_paths (parser, select_statement, spec);
      select_statement = mq_reset_paths (parser, select_statement, spec);
    }

  return select_statement;
}


/*
 * mq_fix_derived_in_union() - fixes the derived tables in queries
 *   return:
 *   parser(in):
 *   statement(in):
 *   spec_id(in):
 *
 * Note:
 * It performs two functions
 *      1) In a given select, the outer level derived table spec
 *         is not in general the SAME spec being manipulated here.
 *         This spec is a copy of the outer spec, with the same id.
 *         Thus, we use the spec_id to find the derived table of interest
 *         to 'fix up'.
 *      2) Since the statement may have been translated to a union,
 *         there may be multiple derived tables to fix up. This
 *         recurses for unions to do so.
 */
PT_NODE *
mq_fix_derived_in_union (PARSER_CONTEXT * parser, PT_NODE * statement,
			 UINTPTR spec_id)
{
  PT_NODE *spec;

  switch (statement->node_type)
    {
    case PT_SELECT:
      spec = statement->info.query.q.select.from;
      while (spec && spec->info.spec.id != spec_id)
	{
	  spec = spec->next;
	}
      if (spec)
	{
	  statement = mq_fix_derived (parser, statement, spec);
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "translate");
	}
      break;

    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      statement->info.query.q.union_.arg1 =
	mq_fix_derived_in_union
	(parser, statement->info.query.q.union_.arg1, spec_id);
      statement->info.query.q.union_.arg2 =
	mq_fix_derived_in_union
	(parser, statement->info.query.q.union_.arg2, spec_id);
      break;

    default:
      PT_INTERNAL_ERROR (parser, "translate");
      break;
    }

  return statement;
}


/*
 * mq_translate_value() - translate a virtual object to the real object
 *   return:
 *   parser(in):
 *   value(in):
 */
static PT_NODE *
mq_translate_value (PARSER_CONTEXT * parser, PT_NODE * value)
{
  PT_NODE *data_type, *class_;
  DB_OBJECT *real_object, *real_class;
  DB_VALUE *db_value;

  if (value->node_type == PT_VALUE
      && value->type_enum == PT_TYPE_OBJECT
      && (data_type = value->data_type)
      && (class_ = data_type->info.data_type.entity)
      && class_->node_type == PT_NAME
      && db_is_vclass (class_->info.name.db_object))
    {
      data_type->info.data_type.virt_object = class_->info.name.db_object;
      real_object = db_real_instance (value->info.value.data_value.op);
      if (real_object)
	{
	  real_class = db_get_class (real_object);
	  class_->info.name.db_object = db_get_class (real_object);
	  class_->info.name.original =
	    db_get_class_name (class_->info.name.db_object);
	  value->info.value.data_value.op = real_object;

	  db_value = pt_value_to_db (parser, value);
	  if (db_value)
	    {
	      DB_MAKE_OBJECT (db_value, value->info.value.data_value.op);
	    }

	}
    }

  return value;
}


/*
 * mq_push_dot_in_query() - Generate a new dot expression from the i'th column
 *                          and the name passed in for every select list
 *   return:
 *   parser(in):
 *   query(in):
 *   i(in):
 *   name(in):
 */
static void
mq_push_dot_in_query (PARSER_CONTEXT * parser, PT_NODE * query, int i,
		      PT_NODE * name)
{
  PT_NODE *col;
  PT_NODE *new_col;
  PT_NODE *root;
  PT_NODE *new_spec;

  if (query)
    {
      switch (query->node_type)
	{
	case PT_SELECT:
	  col = query->info.query.q.select.list;
	  while (i > 0 && col)
	    {
	      col = col->next;
	      i--;
	    }
	  if (col && col->node_type == PT_NAME && PT_IS_OID_NAME (col))
	    {
	      root = pt_find_entity (parser, query->info.query.q.select.from,
				     col->info.name.spec_id);
	      new_col = parser_copy_tree (parser, name);
	      new_col->info.name.spec_id = col->info.name.spec_id;
	      new_col->info.name.resolved = col->info.name.resolved;
	      root = pt_find_entity (parser, query->info.query.q.select.from,
				     col->info.name.spec_id);
	    }
	  else
	    {
	      new_col = parser_new_node (parser, PT_DOT_);
	      new_col->info.dot.arg1 = parser_copy_tree (parser, col);
	      new_col->info.dot.arg2 = parser_copy_tree (parser, name);
	      new_col->info.dot.arg2->info.name.spec_id = 0;
	      new_col->info.dot.arg2->info.name.resolved = NULL;
	      new_col->type_enum = name->type_enum;
	      new_col->data_type =
		parser_copy_tree_list (parser, name->data_type);
	      root = NULL;
	      if (col->node_type == PT_NAME)
		{
		  root =
		    pt_find_entity (parser, query->info.query.q.select.from,
				    col->info.name.spec_id);
		}
	      else if (col->node_type == PT_DOT_)
		{
		  root =
		    pt_find_entity (parser, query->info.query.q.select.from,
				    col->info.dot.arg2->info.name.spec_id);
		}
	      if (root)
		{
		  new_spec = pt_insert_entity (parser, new_col, root, NULL);
		  if (new_spec)
		    {
		      new_col->info.dot.arg2->info.name.spec_id =
			new_spec->info.spec.id;
		    }
		  else
		    {
		      /* error is set by pt_insert_entity */
		    }
		}
	    }
	  parser_append_node (new_col, col);
	  break;

	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  mq_push_dot_in_query (parser, query->info.query.q.union_.arg1, i,
				name);
	  mq_push_dot_in_query (parser, query->info.query.q.union_.arg2, i,
				name);
	  break;

	default:
	  /* should not get here, that is an error! */
	  /* its almost certainly recoverable, so ignore it */
	  assert (0);
	  break;
	}
    }
}


/*
 * mq_clean_dot() -
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
mq_clean_dot (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
	      int *continue_walk)
{
  PT_NODE *spec = (PT_NODE *) void_arg;
  PT_NODE *temp;
  PT_NODE *next;

  if (node == NULL)
    {
      return node;
    }

  switch (node->node_type)
    {
    case PT_DOT_:
      if (node->info.dot.arg2->info.name.spec_id == spec->info.spec.id)
	{
	  next = node->next;
	  temp = node->info.dot.arg2;
	  node->info.dot.arg2 = NULL;
	  node->next = NULL;
	  parser_free_tree (parser, node);
	  node = temp;
	  node->next = next;
	}
      break;

    default:
      break;
    }

  return node;
}


/*
 * mq_push_path() -
 *   return:
 *   parser(in):
 *   statement(in): a select statement needing fixing
 *   spec(in): the spec of the derived query
 *   path(in): the path to push inside the spec
 */
PT_NODE *
mq_push_path (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * spec,
	      PT_NODE * path)
{
  PT_NODE *cols = spec->info.spec.as_attr_list;
  PT_NODE *new_col;
  PT_NODE *sub_paths;
  PT_NODE *refs, *free_refs;
  PT_NODE *join = path->info.spec.path_conjuncts;
  int i = pt_find_attribute (parser, join->info.expr.arg1, cols);

  refs = mq_get_references (parser, statement, path);
  free_refs = refs;
  path->info.spec.referenced_attrs = NULL;

  if (i >= 0)
    {
      while (refs)
	{
	  if (!PT_IS_OID_NAME (refs))
	    {
	      /* for each referenced attribute,
	       *  1) Make a new derived table symbol on referenced
	       *     and as_attr_lists.
	       *  2) Create a new path node on each select list made from
	       *     the referenced name and the column corresponding to
	       *     the join arg1.
	       *  3) replace the names in statement corresponding to references
	       *     with generated name.
	       */
	      new_col = mq_generate_unique (parser, cols);
	      parser_free_tree (parser, new_col->data_type);
	      new_col->data_type =
		parser_copy_tree_list (parser, refs->data_type);
	      new_col->type_enum = refs->type_enum;
	      parser_append_node (new_col, cols);

	      mq_push_dot_in_query (parser, spec->info.spec.derived_table,
				    i, refs);

	      /* not mq_lambda ... */
	      statement = pt_lambda (parser, statement, refs, new_col);

	      path = pt_lambda (parser, path, refs, new_col);
	    }

	  refs = refs->next;
	}
    }


  parser_free_tree (parser, free_refs);

  sub_paths = path->info.spec.path_entities;
  for (; sub_paths != NULL; sub_paths = sub_paths->next)
    {
      statement = mq_push_path (parser, statement, spec, sub_paths);
    }

  statement =
    parser_walk_tree (parser, statement, mq_clean_dot, spec, NULL, NULL);

  return statement;
}


/*
 * mq_derived_path() -
 *   return: derived path spec
 *   parser(in):
 *   statement(in): a select statement needing fixing
 *   path(in): the path to rewrite
 */
PT_NODE *
mq_derived_path (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * path)
{
  PT_NODE *join;
  PT_NODE *new_spec = parser_new_node (parser, PT_SPEC);
  PT_NODE *query = parser_new_node (parser, PT_SELECT);
  PT_NODE *temp;
  PT_NODE *sub_paths;
  PT_NODE *new_sub_path;

  path->info.spec.range_var->info.name.resolved = NULL;
  if (path->info.spec.entity_name)
    {
      path->info.spec.entity_name->info.name.resolved = NULL;
    }
  sub_paths = path->info.spec.path_entities;
  path->info.spec.path_entities = NULL;
  join = path->info.spec.path_conjuncts;
  path->info.spec.path_conjuncts = NULL;

  /* move path join term */
  new_spec->info.spec.path_conjuncts = join;
  new_spec->info.spec.path_entities = sub_paths;
  new_spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
  new_spec->info.spec.id = path->info.spec.id;
  new_spec->info.spec.range_var =
    parser_copy_tree (parser, path->info.spec.range_var);
  statement = mq_reset_ids_and_references (parser, statement, new_spec);
  new_spec->info.spec.id = (UINTPTR) new_spec;
  new_spec->info.spec.as_attr_list = new_spec->info.spec.referenced_attrs;
  new_spec->info.spec.referenced_attrs = NULL;

  query->info.query.q.select.from = path;
  query->info.query.is_subquery = PT_IS_SUBQUERY;
  temp = query->info.query.q.select.list =
    parser_copy_tree_list (parser, new_spec->info.spec.as_attr_list);

  for (; temp != NULL; temp = temp->next)
    {
      temp->info.name.spec_id = path->info.spec.id;
    }

  new_spec = parser_walk_tree (parser, new_spec, mq_set_virt_object, new_spec,
			       NULL, NULL);
  statement =
    parser_walk_tree (parser, statement, mq_set_virt_object, new_spec, NULL,
		      NULL);

  new_spec->info.spec.derived_table = query;

  for (new_spec->info.spec.path_entities = NULL; sub_paths; sub_paths = temp)
    {
      temp = sub_paths->next;
      sub_paths->next = NULL;
      new_sub_path = mq_derived_path (parser, statement, sub_paths);
      new_spec->info.spec.path_entities =
	parser_append_node (new_sub_path, new_spec->info.spec.path_entities);
    }

  return new_spec;
}


/*
 * mq_fetch_subqueries_for_update_local() - ask the schema manager for the
 *      cached parser containing the compiled subqueries of the class_.
 *      If that is not already cached, the schema manager will call back to
 *      compute the subqueries
 *   return:
 *   parser(in):
 *   class(in):
 *   fetch_as(in):
 *   what_for(in):
 *   qry_cache(out):
 */
static PT_NODE *
mq_fetch_subqueries_for_update_local (PARSER_CONTEXT * parser,
				      PT_NODE * class_, PT_FETCH_AS fetch_as,
				      DB_AUTH what_for,
				      PARSER_CONTEXT ** qry_cache)
{
  PARSER_CONTEXT *query_cache;
  DB_OBJECT *class_object;

  if (!class_ || !(class_object = class_->info.name.db_object)
      || !qry_cache || db_is_class (class_object))
    {
      return NULL;
    }

  *qry_cache = query_cache = sm_virtual_queries (class_object);

  if (query_cache && query_cache->view_cache)
    {
      if (!(query_cache->view_cache->authorization & what_for))
	{
	  PT_ERRORmf2 (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_IS_NOT_AUTHORIZED_ON,
		       db_auth_name[what_for],
		       db_get_class_name (class_->info.name.db_object));
	  return NULL;
	}
      if (parser)
	{
	  parser->error_msgs =
	    parser_append_node (parser_copy_tree_list
				(parser, query_cache->error_msgs),
				parser->error_msgs);
	}

      if (!query_cache->view_cache->vquery_for_update && parser)
	{
	  PT_ERRORmf (parser, class_, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_VCLASS_NOT_UPDATABLE,
		      /* use function to get name.
		       * class_->info.name.original is not always set. */
		      db_get_class_name (class_object));
	}
      if (fetch_as == PT_INVERTED_ASSIGNMENTS)
	{
	  return query_cache->view_cache->inverted_vquery_for_update_in_gdb;
	}
      if (fetch_as == PT_NORMAL_SELECT)
	{
	  return query_cache->view_cache->vquery_for_update_in_gdb;
	}
    }

  return NULL;
}

/*
 * mq_fetch_subqueries_for_update() - just like ..._for_update_local except
 *      it does not have an output argument for qry_cache
 *   return:
 *   parser(in):
 *   class(in):
 *   fetch_as(in):
 *   what_for(in):
 */
PT_NODE *
mq_fetch_subqueries_for_update (PARSER_CONTEXT * parser, PT_NODE * class_,
				PT_FETCH_AS fetch_as, DB_AUTH what_for)
{
  PARSER_CONTEXT *query_cache;

  return mq_fetch_subqueries_for_update_local
    (parser, class_, fetch_as, what_for, &query_cache);
}


/*
 * mq_fetch_select_for_real_class_update() - fetch the select statement that
 *                                           maps the vclass to the real class
 *   return:
 *   parser(in):
 *   vclass(in):
 *   real_class(in):
 *   fetch_as(in):
 *   what_for(in):
 */
static PT_NODE *
mq_fetch_select_for_real_class_update (PARSER_CONTEXT * parser,
				       PT_NODE * vclass, PT_NODE * real_class,
				       PT_FETCH_AS fetch_as, DB_AUTH what_for)
{
  PT_NODE *select_statements =
    mq_fetch_subqueries_for_update (parser, vclass, fetch_as, what_for);
  PT_NODE *flat;
  DB_OBJECT *class_object = NULL;

  if (!select_statements)
    {
      return NULL;
    }

  if (real_class)
    {
      class_object = real_class->info.name.db_object;
    }

  while (select_statements)
    {
      if (select_statements->info.query.q.select.from)
	{
	  for (flat = select_statements->info.query.q.select.from->
	       info.spec.flat_entity_list; flat; flat = flat->next)
	    {
	      if (class_object == flat->info.name.db_object)
		{
		  return select_statements;
		}
	    }

	  /* if you can't find an exact match, find a sub-class
	     there could be more than one, but what can you do */
	  for (flat = select_statements->info.query.q.select.from->
	       info.spec.flat_entity_list; flat; flat = flat->next)
	    {
	      if (db_is_superclass (class_object, flat->info.name.db_object))
		{
		  return select_statements;
		}
	    }
	}
      select_statements = select_statements->next;
    }

  return NULL;
}


/*
 * mq_fetch_expression_for_real_class_update() - fetch the expression statement
 *      that maps the vclass attribute to the real class
 *   return:
 *   parser(in):
 *   vclass_obj(in):
 *   attr(in):
 *   real_class(in):
 *   fetch_as(in):
 *   what_for(in):
 *   spec_id(out): entity spec id of the specification owning the expression
 */
static PT_NODE *
mq_fetch_expression_for_real_class_update (PARSER_CONTEXT * parser,
					   DB_OBJECT * vclass_obj,
					   PT_NODE * attr,
					   PT_NODE * real_class,
					   PT_FETCH_AS fetch_as,
					   DB_AUTH what_for,
					   UINTPTR * spec_id)
{
  PT_NODE vclass;
  PT_NODE *select_statement;
  PT_NODE *attr_list;
  PT_NODE *select_list;
  PT_NODE *spec;
  const char *attr_name;

  vclass.node_type = PT_NAME;
  parser_init_node (&vclass);
  vclass.line_number = 0;
  vclass.column_number = 0;
  vclass.info.name.original = NULL;
  vclass.info.name.db_object = vclass_obj;

  attr_list = mq_fetch_attributes (parser, &vclass);

  select_statement =
    mq_fetch_select_for_real_class_update (parser, &vclass, real_class,
					   fetch_as, what_for);

  if (!select_statement)
    {
      if (!parser->error_msgs)
	{
	  const char *real_class_name = "<unknown>";
	  if (real_class && real_class->info.name.original)
	    {
	      real_class_name = real_class->info.name.original;
	    }

	  PT_ERRORmf2 (parser, attr, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_VC_COMP_NOT_UPDATABL,
		       db_get_class_name (vclass_obj), real_class_name);
	}
      return NULL;
    }

  if (spec_id)
    {
      *spec_id = 0;
    }
  if (!attr || !attr_list
      || !(select_list = select_statement->info.query.q.select.list)
      || !(attr_name = attr->info.name.original))
    {
      PT_INTERNAL_ERROR (parser, "translate");
      return NULL;
    }

  for (; attr_list && select_list;
       attr_list = attr_list->next, select_list = select_list->next)
    {
      if (intl_mbs_casecmp (attr_name, attr_list->info.name.original) == 0)
	{
	  if (spec_id && (spec = select_statement->info.query.q.select.from))
	    {
	      *spec_id = spec->info.spec.id;
	    }
	  return select_list;
	}
    }

  if (!parser->error_msgs)
    {
      PT_ERRORmf2 (parser, attr, MSGCAT_SET_PARSER_SEMANTIC,
		   MSGCAT_SEMANTIC_CLASS_DOES_NOT_HAVE,
		   db_get_class_name (vclass_obj), attr_name);
    }

  return NULL;
}


/*
 * mq_fetch_attributes() - fetch class's subqueries
 *   return: PT_NODE list of its attribute names, including oid attr
 *   parser(in):
 *   class(in):
 */
PT_NODE *
mq_fetch_attributes (PARSER_CONTEXT * parser, PT_NODE * class_)
{
  PARSER_CONTEXT *query_cache;
  DB_OBJECT *class_object;

  if (!class_ || !(class_object = class_->info.name.db_object)
      || db_is_class (class_object))
    {
      return NULL;
    }

  query_cache = sm_virtual_queries (class_object);

  if (query_cache)
    {
      if (parser && query_cache->error_msgs)
	{
	  /* propagate errors */
	  parser->error_msgs =
	    parser_append_node (parser_copy_tree_list
				(parser, query_cache->error_msgs),
				parser->error_msgs);
	}

      if (query_cache->view_cache)
	{
	  return query_cache->view_cache->attrs;
	}
    }

  return NULL;
}

/*
 * NAME: mq_set_names_dbobject
 *
 * This private routine re-sets PT_NAME node resolution to match
 * a new printable name (usually, used to resolve ambiguity)
 *
 * returns: PT_NODE
 *
 * side effects: none
 *
 */

/*
 * mq_set_names_dbobject() - re-sets PT_NAME node resolution to match a new
 *                           printable name
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
mq_set_names_dbobject (PARSER_CONTEXT * parser, PT_NODE * node,
		       void *void_arg, int *continue_walk)
{
  SET_NAMES_INFO *info = (SET_NAMES_INFO *) void_arg;

  if (node->node_type == PT_NAME
      && node->info.name.meta_class != PT_PARAMETER
      && node->info.name.spec_id == info->id)
    {
      node->info.name.db_object = info->object;

      /* dont walk entity_name_list/flat_entity_spec
       * do walk list especially for method args list
       * for example: set a = func(x, y, z) <-- walk into y, z
       */
      *continue_walk = PT_LIST_WALK;
    }
  if (node->node_type == PT_DATA_TYPE || node->node_type == PT_SPEC)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * mq_is_updatable() - fetches the stored updatable query spec
 *   return: 0 if not, 1 if so
 *   class_object(in):
 */
bool
mq_is_updatable (DB_OBJECT * class_object)
{
  PT_NODE class_;
  PT_NODE *subquery;
  /* static */ PARSER_CONTEXT *parser = NULL;
  if (!parser)
    {
      parser = parser_create_parser ();
    }

  class_.node_type = PT_NAME;
  parser_init_node (&class_);
  class_.line_number = 0;
  class_.column_number = 0;
  class_.info.name.original = NULL;
  class_.info.name.db_object = class_object;

  subquery =
    mq_fetch_subqueries_for_update (parser, &class_, PT_NORMAL_SELECT,
				    DB_AUTH_SELECT);

  /* clean up memory */
  parser_free_parser (parser);

  return subquery != NULL;
}

/*
 * mq_is_updatable_att() -
 *   return: true if vmop's att_nam is updatable
 *   parser(in): the parser context
 *   vmop(in): proxy/vclass object
 *   att_nam(in): one of vmop's attribute names
 *   rmop(in): real (base) class object
 */

bool
mq_is_updatable_att (PARSER_CONTEXT * parser, DB_OBJECT * vmop,
		     const char *att_nam, DB_OBJECT * rmop)
{
  PT_NODE real, attr, *expr;

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = att_nam;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = rmop;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vmop, &attr, &real, PT_INVERTED_ASSIGNMENTS,
     DB_AUTH_SELECT, NULL);

  if (!expr)
    {
      return false;
    }

  return expr->info.expr.arg1 && expr->info.expr.arg2;
}


/*
 * mq_is_updatable_attribute() -
 *   return: false if not, true if so
 *   vclass_object(in):
 *   attr_name(in):
 *   real_class_object(in):
 */
bool
mq_is_updatable_attribute (DB_OBJECT * vclass_object, const char *attr_name,
			   DB_OBJECT * real_class_object)
{
  PARSER_CONTEXT *parser = parser_create_parser ();
  bool rc = mq_is_updatable_att (parser, vclass_object, attr_name,
				 real_class_object);

  parser_free_parser (parser);
  return rc;
}


/*
 * mq_evaluate_expression() -
 *   return: the evaluated expression value, or error
 *   parser(in):
 *   expr(in):
 *   value(in):
 *   object(in): an object to db_get all names from
 *   spec_id(in):
 */
int
mq_evaluate_expression (PARSER_CONTEXT * parser, PT_NODE * expr,
			DB_VALUE * value, DB_OBJECT * object, UINTPTR spec_id)
{
  int error = NO_ERROR;
  SET_NAMES_INFO info;

  info.object = object;
  info.id = spec_id;
  if (expr)
    {
      parser_walk_tree (parser, expr, mq_set_names_dbobject,
			&info, pt_continue_walk, NULL);

      pt_evaluate_tree (parser, expr, value);
      if (pt_has_error (parser))
	{
	  error = PT_SEMANTIC;
	  pt_report_to_ersys (parser, (PT_ERROR_TYPE) error);
	}
    }
  else
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORm (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_NO_EXPR_TO_EVALUATE);
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  return error;
}

/*
 * mq_evaluate_expression_having_serial() -
 *   return:
 *   parser(in):
 *   expr(in):
 *   value(in):
 *   object(in):
 *   spec_id(in):
 */
int
mq_evaluate_expression_having_serial (PARSER_CONTEXT * parser, PT_NODE * expr,
				      DB_VALUE * value, DB_OBJECT * object,
				      UINTPTR spec_id)
{
  int error = NO_ERROR;
  SET_NAMES_INFO info;

  info.object = object;
  info.id = spec_id;
  if (expr)
    {
      parser_walk_tree (parser, expr, mq_set_names_dbobject,
			&info, pt_continue_walk, NULL);

      pt_evaluate_tree_having_serial (parser, expr, value);
      if (pt_has_error (parser))
	{
	  error = PT_SEMANTIC;
	  pt_report_to_ersys (parser, (PT_ERROR_TYPE) error);
	}
    }
  else
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORm (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_NO_EXPR_TO_EVALUATE);
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  return error;
}

/*
 * mq_get_attribute() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   vclass_object(in): the "mop" of the virtual instance's vclass
 *   attr_name(in): the attribute of the virtual instance to updat
 *   real_class_object(in): the "mop" of the virtual instance's real class
 *   virtual_value(out): the value gotten from the virtual instance
 *   real_instance(out): contains real instance of virtual instance
 */
int
mq_get_attribute (DB_OBJECT * vclass_object, const char *attr_name,
		  DB_OBJECT * real_class_object, DB_VALUE * virtual_value,
		  DB_OBJECT * real_instance)
{
  PT_NODE real;
  PT_NODE attr;
  PARSER_CONTEXT *parser = NULL;
  PT_NODE *expr;
  int error = NO_ERROR;
  UINTPTR spec_id;
  int save;

  AU_DISABLE (save);

  if (!parser)
    {
      parser = parser_create_parser ();
    }

  if (parser)
    {
      parser->au_save = save;
    }

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = attr_name;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = real_class_object;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vclass_object, &attr, &real, PT_NORMAL_SELECT, DB_AUTH_SELECT,
     &spec_id);

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }
  else
    {
      error = mq_evaluate_expression (parser, expr, virtual_value,
				      real_instance, spec_id);
    }

  parser_free_parser (parser);

  AU_ENABLE (save);

  return error;
}


/*
 * mq_oid() -
 *   return:
 *   parser(in): the usual context
 *   spec(in):
 */
PT_NODE *
mq_oid (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE *real;
  PT_NODE attr;
  PT_NODE *expr;
  PT_NODE *error_msgs = parser->error_msgs;
  int save;
  DB_OBJECT *virt_class;

  /* DO NOT RETURN FROM WITHIN THE BODY OF THIS PROCEDURE */
  AU_DISABLE (save);
  parser->au_save = save;

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = "";	/* oid's have null string attr name */

  real = spec->info.spec.flat_entity_list;
  virt_class = real->info.name.virt_object;

  parser->error_msgs = NULL;

  expr = mq_fetch_expression_for_real_class_update
    (parser, virt_class, &attr, real, PT_NORMAL_SELECT, DB_AUTH_ALL, NULL);

  /* in case it was NOT updatable just return NULL, no error */
  parser_free_tree (parser, parser->error_msgs);
  parser->error_msgs = error_msgs;

  expr = parser_copy_tree (parser, expr);

  expr = parser_walk_tree (parser, expr, mq_set_all_ids, spec, NULL, NULL);

  AU_ENABLE (save);

  return expr;
}

/*
 * virtual_to_realval() -  translate a virtual value into its corresponding
 *                         real value
 *   return:  NO_ERROR if all OK, a negative ER code otherwise
 *   parser(in): the parser context
 *   v_val(in): the virtual db_value
 *   expr(in): has inverted ldb expression to use for the translation
 *   r_val(out): the corresponding real db_value
 *
 * Note :
 *   when we have more time, make mq_update_attribute_local call
 *   this function and remove the duplicated code from there.
 */
static int
virtual_to_realval (PARSER_CONTEXT * parser, DB_VALUE * v_val, PT_NODE * expr,
		    DB_VALUE * r_val)
{
  PT_NODE *value, *value_holder;

  /* make sure we have reasonable arguments */
  if (!parser || !v_val || !expr || expr->node_type != PT_EXPR || !r_val)
    {
      return ER_GENERIC_ERROR;
    }

  /* stuff virtual value v_val into value_holder */
  value_holder = (PT_NODE *) expr->etc;
  value = pt_dbval_to_value (parser, v_val);
  value_holder->info.value.data_value = value->info.value.data_value;
  value_holder->info.value.db_value = *v_val;
  value_holder->info.value.db_value_is_initialized = true;

  /* somehow this translates virtual value v_val into a real value r_val */
  pt_evaluate_tree (parser, expr->info.expr.arg2, r_val);

  /* clean up */
  parser_free_tree (parser, value);
  DB_MAKE_NULL (&value_holder->info.value.db_value);
  value_holder->info.value.db_value_is_initialized = false;
  /*
   * This is a bit of a kludge since there is no way to clean up
   * the data_value portion of the info structure.  The value_holder
   * node now points into the parse tree, but has been allocated by
   * a different parser (mq_fetch_expression_for_real_class_update).
   * We need to set this pointer to NULL so we won't try to free
   * it when cleaning up the parse tree.  Setting the "set" pointer
   * should be safe for the union.
   */
  value_holder->info.value.data_value.set = NULL;

  if (parser->error_msgs)
    {
      return ER_PT_SEMANTIC;
    }
  else
    {
      return NO_ERROR;
    }
}


/*
 * mq_update_attribute_local() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   vclass_object(in): the "mop" of the virtual instance's vclass
 *   attr_name(in): the attribute of the virtual instance to update
 *   real_class_object(in): the "mop" of the virtual instance's real class
 *   virtual_value(in): the value to put in the virtual instance
 *   real_value(out): contains value to set it to
 *   real_name(out): contains name of real instance attribute to set
 *   translate_proxy(in):
 *   db_auth(in):
 */
static int
mq_update_attribute_local (DB_OBJECT * vclass_object, const char *attr_name,
			   DB_OBJECT * real_class_object,
			   DB_VALUE * virtual_value, DB_VALUE * real_value,
			   const char **real_name, int translate_proxy,
			   int db_auth)
{
  PT_NODE real;
  PT_NODE attr;
  /* static */ PARSER_CONTEXT *parser = NULL;
  PT_NODE *value_holder;
  PT_NODE *expr;
  PT_NODE *value;
  int error = NO_ERROR;
  if (!parser)
    {
      parser = parser_create_parser ();
    }

  attr.node_type = PT_NAME;
  parser_init_node (&attr);
  attr.line_number = 0;
  attr.column_number = 0;
  attr.info.name.original = attr_name;

  real.node_type = PT_NAME;
  parser_init_node (&real);
  real.line_number = 0;
  real.column_number = 0;
  real.info.name.original = NULL;
  real.info.name.db_object = real_class_object;

  expr = mq_fetch_expression_for_real_class_update
    (parser, vclass_object, &attr, &real, PT_INVERTED_ASSIGNMENTS,
     (DB_AUTH) db_auth, NULL);

  if (!expr			/* SM_NOT_UPDATBLE_ATTRIBUTE */
      || !expr->info.expr.arg1 || !expr->info.expr.arg2 || !expr->etc)
    {
      error = ER_GENERIC_ERROR;
    }

  if (error == NO_ERROR)
    {
      (*real_name) = expr->info.expr.arg1->info.name.original;
      value_holder = (PT_NODE *) expr->etc;
      value = pt_dbval_to_value (parser, virtual_value);
      value_holder->info.value.data_value = value->info.value.data_value;
      value_holder->info.value.db_value = *virtual_value;
      value_holder->info.value.db_value_is_initialized = true;
      pt_evaluate_tree (parser, expr->info.expr.arg2, real_value);
      parser_free_tree (parser, value);
      DB_MAKE_NULL (&value_holder->info.value.db_value);
      value_holder->info.value.db_value_is_initialized = false;
      /*
       * This is a bit of a kludge since there is no way to clean up
       * the data_value portion of the info structure.  The value_holder
       * node now points into the parse tree, but has been allocated by
       * a different parser (mq_fetch_expression_for_real_class_update).
       * We need to set this pointer to NULL so we won't try to free
       * it when cleaning up the parse tree.  Setting the "set" pointer
       * should be safe for the union.
       */
      value_holder->info.value.data_value.set = NULL;
    }
  else if (!parser->error_msgs)
    {
      PT_INTERNAL_ERROR (parser, "translate");
    }

  if (parser->error_msgs)
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  /* clean up memory */

  parser_free_parser (parser);

  return error;
}


/*
 * mq_update_attribute() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   vclass_object(in): the "mop" of the virtual instance's vclass
 *   attr_name(in): the attribute of the virtual instance to update
 *   real_class_object(in): the "mop" of the virtual instance's real class
 *   virtual_value(in): the value to put in the virtual instance
 *   real_value(out): contains value to set it to
 *   real_name(out): contains name of real instance attribute to set
 *   db_auth(in):
 */
int
mq_update_attribute (DB_OBJECT * vclass_object, const char *attr_name,
		     DB_OBJECT * real_class_object, DB_VALUE * virtual_value,
		     DB_VALUE * real_value, const char **real_name,
		     int db_auth)
{
  return mq_update_attribute_local (vclass_object,
				    attr_name,
				    real_class_object,
				    virtual_value,
				    real_value, real_name, false, db_auth);
}


/*
 * mq_fetch_one_real_class_get_cache() -
 *   return: a convienient real class DB_OBJECT* of an updatable virtual class
 *          NULL for non-updatable
 *   vclass_object(in): the "mop" of the virtual class
 *   translate_proxy(in): whether to translate proxy or not
 *   query_cache(out): parser holding cached parse trees
 */
static PT_NODE *
mq_fetch_one_real_class_get_cache (DB_OBJECT * vclass_object,
				   PARSER_CONTEXT ** query_cache)
{
  PARSER_CONTEXT *parser = NULL;
  PT_NODE vclass;
  PT_NODE *subquery, *flat = NULL;

  if (!parser)
    {
      parser = parser_create_parser ();
    }
  vclass.node_type = PT_NAME;

  parser_init_node (&vclass);
  vclass.line_number = 0;
  vclass.column_number = 0;
  vclass.info.name.original = NULL;
  vclass.info.name.db_object = vclass_object;

  subquery = mq_fetch_subqueries_for_update_local (parser,
						   &vclass, PT_NORMAL_SELECT,
						   DB_AUTH_SELECT,
						   query_cache);

  if (subquery && subquery->info.query.q.select.from)
    {
      flat = subquery->info.query.q.select.from->info.spec.flat_entity_list;
    }

  if (!flat && !parser->error_msgs)
    {
      PT_NODE dummy;
      dummy.line_number = 0;
      dummy.column_number = 0;
      PT_ERRORmf (parser, &dummy, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_NO_REALCLASS_4_VCLAS,
		  db_get_class_name (vclass_object));
    }

  if (parser->error_msgs)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }
  /* clean up memory */

  parser_free_parser (parser);

  return flat;
}


/*
 * mq_fetch_one_real_class() -
 *   return: a convienient real class DB_OBJECT* of an updatable virtual class
 *          NULL for non-updatable
 *   vclass_object(in): the "mop" of the virtual class
 */
DB_OBJECT *
mq_fetch_one_real_class (DB_OBJECT * vclass_object)
{
  PARSER_CONTEXT *query_cache;
  PT_NODE *flat;
  flat = mq_fetch_one_real_class_get_cache (vclass_object, &query_cache);
  if (flat)
    {
      return flat->info.name.db_object;
    }
  return NULL;
}


/*
 * mq_get_expression() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   object(in): an object to db_get all names from
 *   expr(in): expression tree
 *   value(in/out): the evaluated expression value
 */
int
mq_get_expression (DB_OBJECT * object, const char *expr, DB_VALUE * value)
{
  PARSER_CONTEXT *parser = NULL;
  int error = NO_ERROR;
  PT_NODE **statements;
  PT_NODE *statement = NULL;
  char *buffer;

  if (!parser)
    {
      parser = parser_create_parser ();
    }

  buffer = pt_append_string (parser, NULL, "select ");
  buffer = pt_append_string (parser, buffer, expr);
  buffer = pt_append_string (parser, buffer, " from ");
  buffer = pt_append_string (parser, buffer, db_get_class_name (object));

  statements = parser_parse_string (parser, buffer);

  if (statements)
    {
      /* exclude from auditing statement */
      statement = statements[0];
      statement = pt_compile (parser, statement);
    }

  if (statement && !parser->error_msgs)
    {
      error = mq_evaluate_expression
	(parser, statement->info.query.q.select.list, value, object,
	 statement->info.query.q.select.from->info.spec.id);
    }
  else
    {
      error = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
    }

  /* clean up memory */

  parser_free_parser (parser);

  return error;
}


/*
 * mq_mget_exprs() - bulk db_get_expression of a list of attribute exprs
 *      for a given set of instances of a class
 *   return: number of rows evaluated if all OK, -1 otherwise
 *   objects(in): an array of object instances of a class
 *   rows(in): number of instances in objects array
 *   exprs(in): an array of attribute expressions
 *   cols(in): number of items in exprs array
 *   qOnErr(in): true if caller wants us to quit on first error
 *   values(out): destination db_values for attribute expressions
 *   results(out): array of result codes
 *   emsg(out): a diagnostic message if an error occurred
 */
int
mq_mget_exprs (DB_OBJECT ** objects, int rows, char **exprs,
	       int cols, int qOnErr, DB_VALUE * values,
	       int *results, char *emsg)
{
  char *buffer;
  DB_ATTDESC **attdesc;
  int c, count, err = NO_ERROR, r;
  DB_OBJECT *cls;
  DB_VALUE *v;
  UINTPTR specid, siz;
  PT_NODE **stmts, *stmt = NULL, *xpr;
  PARSER_CONTEXT *parser;

  /* make sure we have reasonable arguments */
  if (!objects || !(*objects) || (cls = db_get_class (*objects)) == NULL ||
      !exprs || !values || rows <= 0 || cols <= 0)
    {
      strcpy (emsg, "invalid argument(s) to mq_mget_exprs");
      return -1;		/* failure */
    }

  /* create a new parser context */
  parser = parser_create_parser ();
  emsg[0] = 0;

  /* compose a "select exprs from target_class" */
  buffer = pt_append_string (parser, NULL, "select ");
  buffer = pt_append_string (parser, buffer, exprs[0]);
  for (c = 1; c < cols; c++)
    {
      buffer = pt_append_string (parser, buffer, ",");
      buffer = pt_append_string (parser, buffer, exprs[c]);
    }
  buffer = pt_append_string (parser, buffer, " from ");
  buffer = pt_append_string (parser, buffer, db_get_class_name (cls));

  /* compile it */
  stmts = parser_parse_string (parser, buffer);
  if (stmts)
    {
      /* exclude from auditing statement */
      stmt = stmts[0];
      stmt = pt_compile (parser, stmt);
    }

  if (!stmt || parser->error_msgs)
    {
      err = ER_PT_SEMANTIC;
      pt_report_to_ersys (parser, PT_SEMANTIC);
      count = -1;		/* failure */
      for (r = 0; r < rows; r++)
	{
	  results[r] = 0;
	}
    }
  else
    {
      /* partition attribute expressions into names and expressions:
       * simple names will be evaluated via db_dget (fast) and
       * expressions will be evaluated via mq_evaluate_expression (slow).
       */
      siz = cols * sizeof (DB_ATTDESC *);
      attdesc = (DB_ATTDESC **) parser_alloc (parser, siz);
      for (c = 0, xpr = stmt->info.query.q.select.list;
	   c < cols && xpr && (err == NO_ERROR || !qOnErr);
	   c++, xpr = xpr->next)
	{
	  /* get attribute descriptors for simple names */
	  if (xpr->node_type == PT_NAME)
	    {
	      err = db_get_attribute_descriptor (cls, xpr->info.name.original,
						 0, 0, &attdesc[c]);
	    }
	}
      if (!attdesc || err != NO_ERROR)
	{
	  strcpy (emsg,
		  "mq_mget_exprs fails in getting attribute descriptors");
	  count = -1;		/* failure */
	  for (r = 0; r < rows; r++)
	    {
	      results[r] = 0;
	    }
	}
      else
	{
	  /* evaluate attribute expressions and deposit results into values */
	  count = 0;
	  specid = stmt->info.query.q.select.from->info.spec.id;
	  for (r = 0, v = values;
	       r < rows && (err == NO_ERROR || !qOnErr);
	       r++, v = values + (r * cols))
	    {
	      for (c = 0, xpr = stmt->info.query.q.select.list;
		   c < cols && xpr && (err == NO_ERROR || !qOnErr);
		   c++, v++, xpr = xpr->next)
		{
		  /* evaluate using the faster db_dget for simple names and
		     the slower mq_evaluate_expression for expressions. */
		  err = xpr->node_type == PT_NAME ?
		    db_dget (objects[r], attdesc[c], v) :
		    mq_evaluate_expression (parser, xpr, v, objects[r],
					    specid);
		}
	      if (err != NO_ERROR)
		{
		  results[r] = 0;
		}
	      else
		{
		  count++;
		  results[r] = 1;
		}
	    }
	}
    }
  /* deposit any error message into emsg */
  if (err != NO_ERROR && !strlen (emsg))
    {
      strcpy (emsg, db_error_string (3));
    }

  /* clean up memory */
  parser_free_parser (parser);

  return count;
}


/*
 * mq_is_real_class_of_vclass() - determine if s_class is one of the real
 *      classes of the virtual class d_class
 *   return: 1 if s_class is a real class of the view d_class
 *   parser(in): the parser context
 *   s_class(in): a PT_NAME node representing a class_, vclass, or proxy
 *   d_class(in): a PT_NAME node representing a view
 */
int
mq_is_real_class_of_vclass (PARSER_CONTEXT * parser, const PT_NODE * s_class,
			    const PT_NODE * d_class)
{
  PT_NODE *saved_msgs;
  int result;

  if (!parser)
    {
      return 0;
    }

  saved_msgs = parser->error_msgs;
  parser->error_msgs = NULL;

  result = (mq_fetch_select_for_real_class_update (parser,
						   (PT_NODE *) d_class,
						   (PT_NODE *) s_class,
						   PT_NORMAL_SELECT,
						   DB_AUTH_SELECT) != NULL);
  if (parser->error_msgs)
    {
      parser_free_tree (parser, parser->error_msgs);
    }

  parser->error_msgs = saved_msgs;
  return result;
}


/*
 * mq_evaluate_check_option() -
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   check_where(in):
 *   object(in): an object to db_get all names from
 *   view_class(in):
 */
int
mq_evaluate_check_option (PARSER_CONTEXT * parser, PT_NODE * check_where,
			  DB_OBJECT * object, PT_NODE * view_class)
{
  DB_VALUE bool_val;
  int error;

  /* evaluate check option */
  if (check_where)
    {
      for (; check_where; check_where = check_where->next)
	{
	  error = mq_evaluate_expression (parser, check_where,
					  &bool_val, object,
					  view_class->info.name.spec_id);
	  if (error < 0)
	    {
	      return error;
	    }

	  if (db_value_is_null (&bool_val) || db_get_int (&bool_val) == 0)
	    {
	      PT_ERRORmf (parser, check_where, MSGCAT_SET_PARSER_RUNTIME,
			  MSGCAT_RUNTIME_CHECK_OPTION_EXCEPT,
			  view_class->info.name.virt_object ?
			  db_get_class_name (view_class->info.name.
					     virt_object) : ""
			  /* an internal error */ );
	      return ER_GENERIC_ERROR;
	    }
	}
    }

  return NO_ERROR;
}
