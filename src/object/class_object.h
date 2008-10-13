/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * class_object.h - Definitions for structures used in the representation
 *                  of classes
 */

#ifndef _CLASS_OBJECT_H_
#define _CLASS_OBJECT_H_

#ident "$Id$"

#include "object_domain.h"
#include "work_space.h"
#include "object_primitive.h"
#include "common.h"
#include "statistics.h"

/*
 *    This macro should be used whenever comparisons need to be made
 *    on the class or component names. Basically this will perform
 *    a case insensitive comparison
 */
#define SM_COMPARE_NAMES intl_mbs_casecmp

/*
 *    Shorthand macros for iterating over a component, attribute, method list
 */

#define FOR_COMPONENTS(list, var) \
  for (var = (SM_COMPONENT *)list ; var != NULL ; var = var->next)

#define FOR_ATTRIBUTES(list, var) \
  for (var = list ; var != NULL ; var = (SM_ATTRIBUTE *)var->header.next)

#define FOR_METHODS(list, var) \
  for (var = list ; var != NULL ; var = (SM_METHOD *)var->header.next)

#define SM_IS_ATTFLAG_AUTO_INCREMENT(c) (c == SM_ATTFLAG_AUTO_INCREMENT)

#define SM_IS_ATTFLAG_UNIQUE_FAMILY(c) \
                                    ( ((c) == SM_ATTFLAG_UNIQUE             || \
				       (c) == SM_ATTFLAG_PRIMARY_KEY        || \
				       (c) == SM_ATTFLAG_REVERSE_UNIQUE)       \
                                      ? true : false )

#define SM_IS_ATTFLAG_INDEX_FAMILY(c) \
                                    ( (SM_IS_ATTFLAG_UNIQUE_FAMILY(c)       || \
				       (c) == SM_ATTFLAG_FOREIGN_KEY        || \
                                       (c) == SM_ATTFLAG_INDEX              || \
                                       (c) == SM_ATTFLAG_REVERSE_INDEX)        \
                                      ? true : false )

#define SM_IS_ATTFLAG_REVERSE_INDEX_FAMILY(c) \
                                    ( ((c) == SM_ATTFLAG_REVERSE_UNIQUE     || \
                                       (c) == SM_ATTFLAG_REVERSE_INDEX)        \
                                      ? true : false )

#define SM_IS_ATTFLAG_UNIQUE_FAMILY_OR_FOREIGN_KEY(c) \
                                    ( (SM_IS_ATTFLAG_UNIQUE_FAMILY(c)       || \
				       (c) == SM_ATTFLAG_FOREIGN_KEY)          \
                                      ? true : false )

#define SM_MAP_INDEX_ATTFLAG_TO_CONSTRAINT(c) \
	((c) == SM_ATTFLAG_UNIQUE         ? SM_CONSTRAINT_UNIQUE : \
	 (c) == SM_ATTFLAG_PRIMARY_KEY    ? SM_CONSTRAINT_PRIMARY_KEY : \
	 (c) == SM_ATTFLAG_FOREIGN_KEY    ? SM_CONSTRAINT_FOREIGN_KEY : \
	 (c) == SM_ATTFLAG_INDEX          ? SM_CONSTRAINT_INDEX : \
	 (c) == SM_ATTFLAG_REVERSE_UNIQUE ? SM_CONSTRAINT_REVERSE_UNIQUE : \
	                                    SM_CONSTRAINT_REVERSE_INDEX	)

#define SM_MAP_CONSTRAINT_ATTFAG_TO_PROPERTY(c) \
	((c) == SM_ATTFLAG_UNIQUE         ? SM_PROPERTY_UNIQUE: \
	 (c) == SM_ATTFLAG_PRIMARY_KEY    ? SM_PROPERTY_PRIMARY_KEY: \
	 (c) == SM_ATTFLAG_FOREIGN_KEY    ? SM_PROPERTY_FOREIGN_KEY: \
	                                    SM_PROPERTY_REVERSE_UNIQUE)


#define SM_MAP_CONSTRAINT_TO_ATTFLAG(c) \
	((c) == DB_CONSTRAINT_UNIQUE         ? SM_ATTFLAG_UNIQUE: \
	 (c) == DB_CONSTRAINT_PRIMARY_KEY    ? SM_ATTFLAG_PRIMARY_KEY: \
	 (c) == DB_CONSTRAINT_FOREIGN_KEY    ? SM_ATTFLAG_FOREIGN_KEY: \
	 (c) == DB_CONSTRAINT_REVERSE_UNIQUE ? SM_ATTFLAG_REVERSE_UNIQUE: \
	                                       SM_ATTFLAG_NON_NULL)

#define SM_MAP_DB_INDEX_CONSTRAINT_TO_SM_CONSTRAINT(c) \
	((c) == DB_CONSTRAINT_UNIQUE         ? SM_CONSTRAINT_UNIQUE: \
	 (c) == DB_CONSTRAINT_PRIMARY_KEY    ? SM_CONSTRAINT_PRIMARY_KEY: \
	 (c) == DB_CONSTRAINT_FOREIGN_KEY    ? SM_CONSTRAINT_FOREIGN_KEY: \
	 (c) == DB_CONSTRAINT_INDEX          ? SM_CONSTRAINT_INDEX: \
	 (c) == DB_CONSTRAINT_REVERSE_UNIQUE ? SM_CONSTRAINT_REVERSE_UNIQUE: \
	                                       SM_CONSTRAINT_REVERSE_INDEX)

#define SM_IS_CONSTRAINT_UNIQUE_FAMILY(c) \
                                    ( ((c) == SM_CONSTRAINT_UNIQUE          || \
				       (c) == SM_CONSTRAINT_PRIMARY_KEY     || \
				       (c) == SM_CONSTRAINT_REVERSE_UNIQUE)    \
                                      ? true : false )

#define SM_IS_CONSTRAINT_INDEX_FAMILY(c) \
                                    ( (SM_IS_CONSTRAINT_UNIQUE_FAMILY(c)    || \
				       (c) == SM_CONSTRAINT_FOREIGN_KEY     || \
                                       (c) == SM_CONSTRAINT_INDEX           || \
                                       (c) == SM_CONSTRAINT_REVERSE_INDEX)     \
                                      ? true : false )

#define SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(c) \
                                    ( ((c) == SM_CONSTRAINT_REVERSE_UNIQUE ||  \
                                       (c) == SM_CONSTRAINT_REVERSE_INDEX)     \
                                      ? true : false )

#define SM_FIND_NAME_IN_COMPONENT_LIST(complist, name) classobj_complist_search((SM_COMPONENT *)complist, name)

/*
 *    This constant defines the maximum size in bytes of a class name,
 *    attribute name, method name, or any other named entity in the schema.
 */
#define SM_MAX_IDENTIFIER_LENGTH 255


typedef void (*METHOD_FUNCTION) ();
typedef void (*METHOD_FUNC_ARG4) (DB_OBJECT *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *);
typedef void (*METHOD_FUNC_ARG5) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *);
typedef void (*METHOD_FUNC_ARG6) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG7) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG8) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *);
typedef void (*METHOD_FUNC_ARG9) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *, DB_VALUE *,
				  DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG10) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG11) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG12) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG13) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG14) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG15) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG16) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG17) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG18) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG19) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG20) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG21) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG22) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG23) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG24) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG25) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG26) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG27) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG28) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG29) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG30) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG31) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *);
typedef void (*METHOD_FUNC_ARG32) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *);
typedef void (*METHOD_FUNC_ARG33) (DB_OBJECT *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE *, DB_VALUE *,
				   DB_VALUE *, DB_VALUE_LIST *);

typedef struct tp_domain SM_DOMAIN;

/*
 *    These identify "namespaces" for class components like attributes
 *    and methods.  A name_space identifier is frequently used
 *    in conjunction with a name so the correct component can be found
 *    in a class definition.  Since the namespaces for classes and
 *    instances can overlap, a name alone is not enough to uniquely
 *    identify a component.
 */
typedef enum
{
  ID_ATTRIBUTE,
  ID_SHARED_ATTRIBUTE,
  ID_CLASS_ATTRIBUTE,
  ID_METHOD,
  ID_CLASS_METHOD,
  ID_INSTANCE,			/* attriubtes/shared attributes/methods */
  ID_CLASS,			/* class methods/class attributes */
  ID_NULL
} SM_NAME_SPACE;

/*
 *    Bit field identifiers for attribute flags.  These could be defined
 *    with individual unsigned bit fields but this makes it easier
 *    to save them as a single integer.
 *    The "new" flag is used only at run time and shouldn't be here.
 *    Need to re-design the template functions to operate from a different
 *    memory structure during flattening.
 */
typedef enum
{

  SM_ATTFLAG_INDEX = 1,		/* attribute has an index 0x01 */
  SM_ATTFLAG_UNIQUE = 2,	/* attribute has UNIQUE constraint 0x02 */
  SM_ATTFLAG_NON_NULL = 4,	/* attribute has NON_NULL constraint 0x04 */
  SM_ATTFLAG_VID = 8,		/* attribute is part of virtual object id 0x08 */
  SM_ATTFLAG_NEW = 16,		/* is a new attribute  0x10 */
  SM_ATTFLAG_REVERSE_INDEX = 32,	/* attribute has a reverse index 0x20 */
  SM_ATTFLAG_REVERSE_UNIQUE = 64,	/* attribute has a reverse unique 0x40 */
  SM_ATTFLAG_PRIMARY_KEY = 128,	/* attribute has a primary key 0x80 */
  SM_ATTFLAG_AUTO_INCREMENT = 256,	/* auto increment attribute 0x0100 */
  SM_ATTFLAG_FOREIGN_KEY = 512	/* attribute has a primary key 0x200 */
} SM_ATTRIBUTE_FLAG;

/* attribute constraint types */
typedef enum
{
  SM_CONSTRAINT_UNIQUE,
  SM_CONSTRAINT_INDEX,
  SM_CONSTRAINT_NOT_NULL,
  SM_CONSTRAINT_REVERSE_UNIQUE,
  SM_CONSTRAINT_REVERSE_INDEX,
  SM_CONSTRAINT_PRIMARY_KEY,
  SM_CONSTRAINT_FOREIGN_KEY
} SM_CONSTRAINT_TYPE;

/* delete or update action type for foreign key */
typedef enum
{
  SM_FOREIGN_KEY_CASCADE,
  SM_FOREIGN_KEY_RESTRICT,
  SM_FOREIGN_KEY_NO_ACTION
} SM_FOREIGN_KEY_ACTION;

/*
 *    These are used as tags in the SM_CLASS structure and indicates one
 *    of the several class types
 */
typedef enum
{
  SM_CLASS_CT,			/* default SQL/X class */
  SM_VCLASS_CT,			/* SQL/M virtual class */
  SM_LDBVCLASS_CT,		/* component db virtual class */
  SM_ADT_CT			/* Abstract data type-psuedo class */
} SM_CLASS_TYPE;

/*
 *    Flags for misc information about a class.  These must be defined
 *    as powers of two because they are stored packed in a single integer.
 */
typedef enum
{
  SM_CLASSFLAG_SYSTEM = 1,	/* a system defined class */
  SM_CLASSFLAG_WITHCHECKOPTION = 2,	/* a view with check option */
  SM_CLASSFLAG_LOCALCHECKOPTION = 4,	/* view w/local check option */
} SM_CLASS_FLAG;

/*
 *    These identify whether a proxy vclass is to be added, removed, or
 *    updated in the cache of proxies.
 */
typedef enum
{
  CACHE_NA,
  CACHE_ADD,
  CACHE_DROP,
  CACHE_UPDATE
} SM_PROXY_CACHE;

/*
 *    These are used to tag the "meta" objects
 *    This type is used in the definition of SM_CLASS_HEADER
 */
typedef enum
{
  Meta_root,			/* the object is the root class */
  Meta_class			/* the object is a normal class */
} SM_METATYPE;

/*
 *    This is used at the top of all "meta" objects that are represented
 *    with C structures rather than in the usual instance memory format
 *    It serves as a convenient way to store common information
 *    for the class objects and the root class object and eliminates
 *    a lot of special case checking
 */
typedef struct sm_class_header SM_CLASS_HEADER;

struct sm_class_header
{
  WS_OBJECT_HEADER obj_header;	/* always have the object header (chn ) */

  const char *name;
  SM_METATYPE type;		/* doesn't need to be a full word */

  HFID heap;
};


/*
 *    Structure used to cache an attribute constraint.  Attribute constraints
 *    are maintained in the property list.  They are also cached using this
 *    structure for faster retrieval
 */
typedef struct sm_constraint SM_CONSTRAINT;

struct sm_constraint
{
  struct sm_constraint *next;

  char *name;
  SM_CONSTRAINT_TYPE type;
  BTID index;
};

/*
 *    This structure is used as a header for attribute and methods
 *    so they can be manipulated by generic functions written to
 *    operate on heterogeneous lists of attributes and methods
 */
typedef struct sm_component SM_COMPONENT;

struct sm_component
{

  struct sm_component *next;	/* can be used with list_ routines */
  const char *name;		/* can be used with name_ routines */
  SM_NAME_SPACE name_space;	/* identifier tag                  */

};

/*
 *    Memory representation for attributes.
 *    Used for representing all types of attributes: instance, shared,
 *    and class.  For instance attributes, the value field will be
 *    the initial value for new instances.  For shared and class attributes,
 *    the value field has the current value.
 *
 *    Instance attributes must maintain two values.  The "original_value"
 *    is the value specified when the attribute was first defined.  This
 *    is the value that must be used when old objects are converted to
 *    a new representation or when the predicate processor attempts to
 *    access an attribute that is not found in the old representation of
 *    an object.
 */
typedef struct sm_attribute SM_ATTRIBUTE;

struct sm_attribute
{
  SM_COMPONENT header;		/* next, name, header */

  int id;			/* unique id number */

  PR_TYPE *type;		/* basic type */
  TP_DOMAIN *domain;		/* allowable types */

  MOP class_mop;		/* origin class */

  int offset;			/* memory offset */

  DB_VALUE original_value;	/* initial or shared */
  DB_VALUE value;		/* current initial or shared */

  SM_CONSTRAINT *constraints;	/* cached constraint list */

  unsigned int flags;		/* bit flags */

  /* see tfcl and the discussion on attribute extensions */
  DB_SEQ *properties;		/* property list */

  int order;			/* definition order number */
  struct sm_attribute *order_link;	/* list in definition order */

  int storage_order;		/* storage order number */

  void *triggers;		/* trigger cache */

  MOP auto_increment;		/* instance of db_serial */
  bool is_fk_cache_attr;
};

typedef struct sm_foreign_key_info SM_FOREIGN_KEY_INFO;

struct sm_foreign_key_info
{
  struct sm_foreign_key_info *next;
  const char *ref_class;
  OID ref_class_oid;
  BTID ref_class_pk_btid;
  const char **ref_attrs;
  OID self_oid;
  BTID self_btid;
  SM_FOREIGN_KEY_ACTION delete_action;
  SM_FOREIGN_KEY_ACTION update_action;
  const char *cache_attr;
  int cache_attr_id;
  char *name;
  bool is_dropped;
};

typedef struct sm_class_constraint SM_CLASS_CONSTRAINT;

struct sm_class_constraint
{
  struct sm_class_constraint *next;

  const char *name;
  SM_CONSTRAINT_TYPE type;
  SM_ATTRIBUTE **attributes;
  const int *asc_desc;		/* asc/desc info list */
  BTID index;
  SM_FOREIGN_KEY_INFO *fk_info;
  char *shared_cons_name;
};

/*
 *    Holds information about a method argument.  This will be used
 *    in a SM_METHOD_SIGNATURE signature structure.
 */

typedef struct sm_method_argument SM_METHOD_ARGUMENT;

struct sm_method_argument
{
  struct sm_method_argument *next;

  int index;			/* argument index (one based) */
  PR_TYPE *type;		/* basic type */
  TP_DOMAIN *domain;		/* full domain */

};

/*
 *    Describes a method signature including the argument domains,
 *    the return value domain, and the function used to implement
 *    the method.
 *    All methods must have a single signature.
 *    While all methods will have a single signature structure, the
 *    SM_METHOD_ARGUMENT descriptions of the argument list and return value are
 *    optional.  If they are not present, no checks can be made on the
 *    values passed to a method.
 *    In theory, it is possible to have multiple signatures, the language
 *    does not yet support this.  The structures have been left with
 *    support for multiple signatures in case it needs to be added
 *    in the future.
 */
typedef struct sm_method_signature SM_METHOD_SIGNATURE;

struct sm_method_signature
{
  struct sm_method_signature *next;

  const char *function_name;	/* C function name */
  METHOD_FUNCTION function;	/* C function pointer */

  const char *sqlx_definition;	/* interpreted string (future) */

  SM_METHOD_ARGUMENT *value;	/* definition of return value */

  int num_args;			/* number of arguments */
  SM_METHOD_ARGUMENT *args;	/* list of argument descriptions */

};

/*
 *    Contains information about a method.  Methods have the SM_COMPONENT
 *    header like SM_ATTRIBUTE.  The function pointer from the
 *    signature is cached in the method structure so we don't have to
 *    decend another level and the system only supports one signature.
 */
typedef struct sm_method SM_METHOD;

struct sm_method
{
  SM_COMPONENT header;		/* next, name, group */

  int id;			/* method id */
  SM_METHOD_SIGNATURE *signatures;	/* signature list (currently only one) */
  METHOD_FUNCTION function;	/* cached function pointer */
  MOP class_mop;		/* source class */
  unsigned unused:1;		/* formerly static_link flag, delete */

  int order;			/* during modification only, not saved */

  DB_SEQ *properties;		/* property list */
};


/*
 *    Used to maintain a list of method files that contain the
 *    implementations of the methods for a class.
 *    These could be extended to have MOPs to Glo objects that contain
 *    the method source as well.
 *    NOTE: Keep the next & name fields at the top so this can be used
 *    with the NLIST utilities.
 */
typedef struct sm_method_file SM_METHOD_FILE;

struct sm_method_file
{
  struct sm_method_file *next;
  const char *name;

  const char *expanded_name;	/* without environment vars */
  const char *source_name;	/* future expansion */

  MOP class_mop;		/* source class */
};


/*
 *    These provide manual control over the handling of name conflicts
 *    that occur with multiple inheritance.  They may be added to the
 *    class definition automatically in some cases if the resolution has
 *    to be handled by the system.
 *    If the alias field is NULL, this indicates an absolute preference
 *    for the method or attribute of the specified class.  If the alias
 *    field has a value, this will be a replacement name to avoid the
 *    conflict.
 *    The name_space field is used to indicate if the resolution applies
 *    to the ID_INSTANCE name_space or the ID_CLASS name_space since
 *    resolutions for both spaces are kept on the same list.
 */
struct sm_resolution
{
  struct sm_resolution *next;

  MOP class_mop;		/* source class */
  const char *name;		/* component name */
  const char *alias;		/* optional alias */
  SM_NAME_SPACE name_space;	/* component name_space */

};

typedef struct sm_resolution SM_RESOLUTION;

/*
 *    This contains information about an instance attribute in an
 *    obsolete representation.  We need only keep the information required
 *    by the transformer to convert the old instances to the newest
 *    representation.
 */
typedef struct sm_repr_attribute SM_REPR_ATTRIBUTE;

struct sm_repr_attribute
{
  struct sm_repr_attribute *next;

  int attid;			/* old attribute id */
  DB_TYPE typeid_;		/* type id */
  TP_DOMAIN *domain;		/* full domain, think about merging with type id */
};

/*
 *    These contain information about old class representations so that
 *    obsolete objects can be converted to the latest representation as
 *    they are encountered.  Only the minimum amount of information required
 *    to do the conversion is kept.  Since methods, shared attributes, and
 *    class attributes do not effect the disk representation of instances,
 *    they are not part of the representation.
 */
typedef struct sm_representation SM_REPRESENTATION;

struct sm_representation
{
  struct sm_representation *next;

  int id;			/* unique identifier for this rep */
  int fixed_count;		/* number of fixed attributes */
  int variable_count;		/* number of variable attributes */

  SM_REPR_ATTRIBUTE *attributes;	/* list of attribute descriptions */
};

/*
 *    This is used in virtual and component class definitions.
 *    It represents in text form the query(s) which can instantiate a class.
 */
typedef struct sm_query_spec SM_QUERY_SPEC;

struct sm_query_spec
{
  struct sm_query_spec *next;

  const char *specification;
};

typedef struct sm_class SM_CLASS;
typedef struct sm_template *SMT;
typedef struct sm_template SM_TEMPLATE;
/*
 *    This is the primary class structure.  Most of the other
 *    structures in this file are attached to this at some level.
 */
struct sm_class
{
  SM_CLASS_HEADER header;

  SM_CLASS_TYPE class_type;	/* what kind of class variant is this? */
  DB_OBJLIST *users;		/* immediate sub classes */
  int repid;			/* current representation id */

  SM_REPRESENTATION *representations;	/* list of old representations */

  DB_OBJLIST *inheritance;	/* immediate super classes */
  int object_size;		/* memory size in bytes */
  int att_count;		/* number of instance attributes */
  SM_ATTRIBUTE *attributes;	/* list of attribute definitions */
  int shared_count;		/* number of shared attributes */
  SM_ATTRIBUTE *shared;		/* list of shared attribute definitions */
  int class_attribute_count;	/* number of class attributes */
  SM_ATTRIBUTE *class_attributes;	/* list of class attribute definitions */

  SM_METHOD_FILE *method_files;	/* list of method files */
  const char *loader_commands;	/* command string to the dynamic loader */

  int method_count;		/* number of instance methods */
  SM_METHOD *methods;		/* list of method definitions */
  int class_method_count;	/* number of class methods */
  SM_METHOD *class_methods;	/* list of class method definitions */

  SM_RESOLUTION *resolutions;	/* list of instance and class resolutions */

  int fixed_count;		/* number of fixed size attributes */
  int variable_count;		/* number of variable size attributes */
  int fixed_size;		/* byte size of fixed attributes */

  int att_ids;			/* attribute id counter */
  int method_ids;		/* method id counter */
  int unused;			/* formerly repid counter, delete */

  unsigned int flags;

  unsigned methods_loaded:1;	/* set when dynamic linking was performed */
  unsigned post_load_cleanup:1;	/* set if post load cleanup has occured */
  unsigned transaction_cache:1;	/* set if transaction cache is valid */

  unsigned triggers_validated:1;	/* set when trigger cache is validated */
  unsigned has_active_triggers:1;	/* set if trigger processing is required */

  SM_QUERY_SPEC *query_spec;	/* virtual class query_spec information */

  SM_TEMPLATE *new_;		/* temporary structure */

  CLASS_STATS *stats;		/* server statistics, loaded on demand */


  MOP owner;			/* authorization object */
  void *auth_cache;		/* compiled cache */

  SM_ATTRIBUTE *ordered_attributes;

  DB_SEQ *properties;		/* property list */

  unsigned int virtual_cache_schema_id;
  struct parser_context *virtual_query_cache;

  void *triggers;		/* Trigger cache */

  SM_CLASS_CONSTRAINT *constraints;	/* Constraint cache */

  MOP partition_of;		/* Partition information */

  SM_CLASS_CONSTRAINT *fk_ref;	/* fk ref cache */
};


/*
 *    This is the state structure maintained during class definition
 *    and editing.  The structure is modified as necessary to describe
 *    the modification to the class.  When editing is complete, the template
 *    is flattened and checked for errors.  If there are no errors in
 *    the template, the class will be updated.
 *    If there were errors in the template, none of the operations specified
 *    in the template will be applied.  This is usefull for the
 *    DDL interpreter since it must construct a parse tree of schema
 *    operations while parsing the CREATE CLASS and ALTER CLASS statements.
 *    If any of the clauses in these statements is in error, none of the
 *    clauses must take effect.
 */
struct sm_template
{
  MOP op;			/* class MOP (if editing existing class) */
  SM_CLASS_TYPE class_type;	/* what kind of class variant is this? */
  SM_CLASS *current;		/* current class structure (if editing existing) */
  int tran_index;		/* transaction index when template was created */

  const char *name;		/* class name */
  DB_OBJLIST *inheritance;	/* immediate super classes */

  SM_ATTRIBUTE *attributes;	/* instance attribute definitions */
  SM_METHOD *methods;		/* instance method definitions */
  SM_RESOLUTION *resolutions;	/* resolutions */

  SM_ATTRIBUTE *class_attributes;	/* class attribute definitions */
  SM_METHOD *class_methods;	/* class method definitions */
  SM_RESOLUTION *class_resolutions;	/* class resolutions */

  SM_METHOD_FILE *method_files;	/* method files */
  const char *loader_commands;	/* loader commands */
  SM_QUERY_SPEC *query_spec;	/* query_spec list */
  SM_PROXY_CACHE flag;		/* add, drop, or update cache entry */

  SM_ATTRIBUTE *instance_attributes;
  SM_ATTRIBUTE *shared_attributes;

  DB_OBJLIST *ext_references;

  DB_SEQ *properties;

  int *super_id_map;		/* super class id mapping table */

  void *triggers;		/* flattened trigger cache */

  MOP partition_of;
  DB_ATTRIBUTE *partition_parent_atts;	/* partition parent class attributes
					   (if creating partition child class) */
};


/*
 *    This is used for "browsing" functions that need to obtain a lot
 *    of information about the class but do not want to go through the full
 *    overhead of object de-referencing for each piece of information.
 *    It encapsulates a snapshot of a class definition that can be
 *    walked through as necessary.  Also since the copy is not part of
 *    an actual database object, we don't have to worry about swapping
 *    or GCing the structure out from under the caller.
 */
typedef struct sm_class_info SM_CLASS_INFO;
struct sm_class_info
{
  const char *name;
  SM_CLASS_TYPE class_type;	/* what kind of class variant is this? */
  DB_OBJECT *owner;		/* owner's user object */
  DB_OBJLIST *superclasses;	/* external OBJLIST of super classes */
  DB_OBJLIST *subclasses;	/* external OBJLIST of subclasses */

  int att_count;		/* number of instance attributes */
  SM_ATTRIBUTE *attributes;	/* list of attribute definitions */
  int shared_count;		/* number of shared attributes */
  SM_ATTRIBUTE *shared;		/* list of shared attribute definitions */
  int class_attribute_count;	/* number of class attributes */
  SM_ATTRIBUTE *class_attributes;	/* list of class attribute definitions */
  int method_count;		/* number of instance methods */
  SM_METHOD *methods;		/* list of method definitions */
  int class_method_count;	/* number of class methods */
  SM_METHOD *class_methods;	/* list of class method definitions */

  SM_METHOD_FILE *method_files;	/* list of method files */
  const char *loader_commands;	/* command string to the dynamic loader */
  SM_RESOLUTION *resolutions;	/* instance/class resolution list */
  SM_QUERY_SPEC *query_spec;	/* virtual class query_spec list */

  unsigned int flags;		/* persistent flags */
};


/*
 *    This structure is used to maintain a list of class/component mappings
 *    in an attribute or method descriptor.  Since the same descriptor
 *    can be applied an instance of any subclass, we dynamically cache
 *    up pointers to the subclass components as we need them.
 */

typedef struct sm_descriptor_list SM_DESCRIPTOR_LIST;
struct sm_descriptor_list
{
  struct sm_descriptor_list *next;

  MOP classobj;
  SM_CLASS *class_;
  SM_COMPONENT *comp;

  unsigned write_access:1;
};

/*
 *    Cache structure used to make validation of incomming values faster.
 *    One of these can optionally be attached to an SM_DESCRIPTOR and if so
 *    will contain additional information about previously validated values
 *    that can be used to speed up the checking of other values.  This can
 *    be especially significant in tight loops with repeated assignments,
 *    particularly if we would normally have to search a class hierarchy
 *    to do domain validation on a subclass.
 *
 *    Note that things get added to the cache only if coercion is not
 *    required.  We could try to be smarter still and keep hints around about
 *    the coersion that needs to be performed when the cached types come
 *    in but its less clear that would have any benifit.
 */
typedef struct sm_validation SM_VALIDATION;
struct sm_validation
{
  DB_OBJECT *last_class;	/* DB_TYPE_OBJECT validation cache */
  DB_OBJLIST *validated_classes;

  DB_DOMAIN *last_setdomain;	/* DB_TYPE_COLLECTION validation cache */

  DB_TYPE last_type;		/* Other validation caches */
  int last_precision;
  int last_scale;
};

/*
 *    This structure is used as a "descriptor" for improved
 *    performance on repeated access to an attribute.
 */
typedef struct sm_descriptor SM_DESCRIPTOR;
struct sm_descriptor
{
  struct sm_descriptor *next;

  char *name;			/* component name */
  SM_NAME_SPACE name_space;	/* component type */

  SM_DESCRIPTOR_LIST *map;	/* class/component map */
  SM_VALIDATION *valid;		/* validation cache */

  DB_OBJECT *class_mop;		/* root class */
};


extern const int SM_MAX_STRING_LENGTH;

/*
 * These are the names for the system defined properties on classes,
 * attributes and methods.  For the built in properties, try
 * to use short names.  User properties if they are ever allowed
 * should have more descriptive names.
 *
 * Lets adopt the convention that names beginning with a '*' are
 * reserved for system properties.
 */
#define SM_PROPERTY_UNIQUE "*U"
#define SM_PROPERTY_INDEX "*I"
#define SM_PROPERTY_NOT_NULL "*N"
#define SM_PROPERTY_REVERSE_UNIQUE "*RU"
#define SM_PROPERTY_REVERSE_INDEX "*RI"
#define SM_PROPERTY_LDB_NAME "*L_NM"
#define SM_PROPERTY_LDB_INTRINSIC_OID "*L_ISX"	/* for backward compatibility */
#define SM_PROPERTY_LDB_ID "*L_ID"
#define SM_PROPERTY_VID_KEY "*V_KY"
#define SM_PROPERTY_LDB_OBJ "*L_OBJ"
#define SM_PROPERTY_PRIMARY_KEY "*P"
#define SM_PROPERTY_PARTITION "*PT"
#define SM_PROPERTY_FOREIGN_KEY "*FK"


/* Allocation areas */
extern void classobj_area_init (void);

/* Threaded arrays */
extern DB_LIST *classobj_alloc_threaded_array (int size, int count);
extern void classobj_free_threaded_array (DB_LIST * array, LFREEER clear);

/* Property lists */
extern DB_SEQ *classobj_make_prop (void);
extern int classobj_copy_props (DB_SEQ * properties, MOP filter_class,
				DB_SEQ ** new_);
extern void classobj_free_prop (DB_SEQ * properties);
extern int classobj_get_prop (DB_SEQ * properties, const char *name,
			      DB_VALUE * pvalue);
extern int classobj_put_prop (DB_SEQ * properties, const char *name,
			      DB_VALUE * pvalue);
extern int classobj_drop_prop (DB_SEQ * properties, const char *name);
extern int classobj_put_index (DB_SEQ ** properties,
			       SM_CONSTRAINT_TYPE type,
			       const char *constraint_name,
			       SM_ATTRIBUTE ** atts,
			       const int *asc_desc,
			       const BTID * id,
			       SM_FOREIGN_KEY_INFO * fk_info,
			       char *shared_cons_name);
extern int classobj_put_index_id (DB_SEQ ** properties,
				  SM_CONSTRAINT_TYPE type,
				  const char *constraint_name,
				  SM_ATTRIBUTE ** atts,
				  const int *asc_desc,
				  const BTID * id,
				  SM_FOREIGN_KEY_INFO * fk_info,
				  char *shared_cons_name);
extern int classobj_find_prop_constraint (DB_SEQ * properties,
					  const char *prop_name,
					  const char *cnstr_name,
					  DB_VALUE * cnstr_val);
extern int classobj_get_cached_constraint (SM_CONSTRAINT * constraints,
					   SM_CONSTRAINT_TYPE type,
					   BTID * id);
extern int classobj_decompose_property_oid (const char *buffer, int *volid,
					    int *fileid, int *pageid);
extern int classobj_btid_from_property_value (DB_VALUE * value, BTID * btid,
					      char **shared_cons_name);
extern int classobj_oid_from_property_value (DB_VALUE * value, OID * oid);

/* Constraints */
extern bool classobj_cache_constraints (SM_CLASS * class_);

extern int classobj_make_class_constraints (DB_SET * props,
					    SM_ATTRIBUTE * attributes,
					    SM_CLASS_CONSTRAINT ** con_ptr);
extern void classobj_free_foreign_key_ref (SM_FOREIGN_KEY_INFO * fk_info);
extern void classobj_free_class_constraints (SM_CLASS_CONSTRAINT *
					     constraints);
extern void classobj_decache_class_constraints (SM_CLASS * class_);
extern int classobj_cache_class_constraints (SM_CLASS * class_);

extern SM_CLASS_CONSTRAINT
  * classobj_find_class_constraint (SM_CLASS_CONSTRAINT * constraints,
				    SM_CONSTRAINT_TYPE type,
				    const char *name);
extern SM_CLASS_CONSTRAINT *classobj_find_class_index (SM_CLASS * class_,
						       const char *name);
extern SM_CLASS_CONSTRAINT *classobj_find_cons_index (SM_CLASS_CONSTRAINT *
						      cons_list,
						      const char *name);
extern SM_CLASS_CONSTRAINT *classobj_find_class_index2 (SM_CLASS * class_,
							CLASS_STATS * stats,
							DB_CONSTRAINT_TYPE
							new_cons,
							const char
							**att_names,
							const int *asc_desc);
extern SM_CLASS_CONSTRAINT *classobj_find_cons_index2 (SM_CLASS_CONSTRAINT *
						       cons_list,
						       CLASS_STATS * stats,
						       DB_CONSTRAINT_TYPE
						       new_cons,
						       const char **att_names,
						       const int *asc_desc);
extern TP_DOMAIN *classobj_find_cons_index2_col_type_list (SM_CLASS_CONSTRAINT
							   * cons,
							   CLASS_STATS *
							   stats);
extern void classobj_remove_class_constraint_node (SM_CLASS_CONSTRAINT **
						   constraints,
						   SM_CLASS_CONSTRAINT *
						   node);

extern int classobj_populate_class_properties (DB_SET ** properties,
					       SM_CLASS_CONSTRAINT *
					       constraints,
					       SM_CONSTRAINT_TYPE type);

extern bool classobj_class_has_indexes (SM_CLASS * class_);

/* Attribute */
extern SM_ATTRIBUTE *classobj_make_attribute (const char *name,
					      PR_TYPE * type,
					      SM_NAME_SPACE name_space);
extern SM_ATTRIBUTE *classobj_copy_attribute (SM_ATTRIBUTE * src,
					      const char *alias);
extern void classobj_free_attribute (SM_ATTRIBUTE * att);

/* Method argument */
extern SM_METHOD_ARGUMENT *classobj_make_method_arg (int index);
extern SM_METHOD_ARGUMENT *classobj_find_method_arg (SM_METHOD_ARGUMENT **
						     arglist, int index,
						     int create);

/* Method signature */
extern SM_METHOD_SIGNATURE *classobj_make_method_signature (const char *name);
extern void classobj_free_method_signature (SM_METHOD_SIGNATURE * sig);

/* Method */
extern SM_METHOD *classobj_make_method (const char *name,
					SM_NAME_SPACE name_space);
extern SM_METHOD *classobj_copy_method (SM_METHOD * src, const char *alias);
extern void classobj_free_method (SM_METHOD * method);

/* Conflict resolution */
extern SM_RESOLUTION *classobj_make_resolution (MOP class_mop,
						const char *name,
						const char *alias,
						SM_NAME_SPACE name_space);
extern int classobj_copy_reslist (SM_RESOLUTION * src, SM_NAME_SPACE resspace,
				  SM_RESOLUTION ** copy_ptr);
extern void classobj_free_resolution (SM_RESOLUTION * res);
extern SM_RESOLUTION *classobj_find_resolution (SM_RESOLUTION * reslist,
						MOP class_mop,
						const char *name,
						SM_NAME_SPACE name_space);

/* Method file */
extern SM_METHOD_FILE *classobj_make_method_file (const char *name);
extern int classobj_copy_methfiles (SM_METHOD_FILE * files, MOP filter_class,
				    SM_METHOD_FILE ** copy_ptr);
extern void classobj_free_method_file (SM_METHOD_FILE * file);

/* Representatino attribute */
extern SM_REPR_ATTRIBUTE *classobj_make_repattribute (int attid,
						      DB_TYPE typeid_,
						      TP_DOMAIN * domain);

/* Representation */
extern SM_REPRESENTATION *classobj_make_representation (void);
extern void classobj_free_representation (SM_REPRESENTATION * rep);

/* Query_spec */
extern SM_QUERY_SPEC *classobj_make_query_spec (const char *);
extern SM_QUERY_SPEC *classobj_copy_query_spec_list (SM_QUERY_SPEC *);
extern void classobj_free_query_spec (SM_QUERY_SPEC *);

/* Editing template */
extern SM_TEMPLATE *classobj_make_template (const char *name, MOP op,
					    SM_CLASS * class_);
extern void classobj_free_template (SM_TEMPLATE * template_ptr);
extern int classobj_add_template_reference (SM_TEMPLATE * template_ptr,
					    MOP obj);

/* Class */
extern SM_CLASS *classobj_make_class (const char *name);
extern void classobj_free_class (SM_CLASS * class_);
extern int classobj_class_size (SM_CLASS * class_);

extern int classobj_install_template (SM_CLASS * class_, SM_TEMPLATE * flat,
				      int saverep);

extern int classobj_snapshot_representation (SM_CLASS * class_);

extern SM_REPRESENTATION *classobj_find_representation (SM_CLASS * class_,
							int id);

extern void classobj_fixup_loaded_class (SM_CLASS * class_);

extern SM_COMPONENT *classobj_filter_components (SM_COMPONENT ** complist,
						 SM_NAME_SPACE name_space);

extern int classobj_annotate_method_files (SM_CLASS * class_, MOP classmop);

extern SM_ATTRIBUTE *classobj_find_attribute (SM_CLASS * class_,
					      const char *name,
					      int class_attribute);

extern SM_ATTRIBUTE *classobj_find_attribute_id (SM_CLASS * class_, int id,
						 int class_attribute);

extern SM_ATTRIBUTE *classobj_find_attribute_list (SM_ATTRIBUTE * attlist,
						   const char *name, int id);

extern SM_METHOD *classobj_find_method (SM_CLASS * class_, const char *name,
					int class_method);

extern SM_COMPONENT *classobj_find_component (SM_CLASS * class_,
					      const char *name,
					      int class_component);

extern SM_COMPONENT *classobj_complist_search (SM_COMPONENT * list,
					       const char *name);

/* Descriptors */
extern SM_DESCRIPTOR *classobj_make_descriptor (MOP class_mop,
						SM_CLASS * classobj,
						SM_COMPONENT * comp,
						int write_access);
extern SM_DESCRIPTOR_LIST *classobj_make_desclist (MOP class_mop,
						   SM_CLASS * classobj,
						   SM_COMPONENT * comp,
						   int write_access);

extern void classobj_free_desclist (SM_DESCRIPTOR_LIST * dl);
extern void classobj_free_descriptor (SM_DESCRIPTOR * desc);

/* Debug */
extern void classobj_print (SM_CLASS * class_);

/* primary key */
extern SM_CLASS_CONSTRAINT *classobj_find_class_primary_key (SM_CLASS *
							     class_);
extern int classobj_count_class_foreign_key (SM_CLASS * class_);
extern int classobj_count_cons_attributes (SM_CLASS_CONSTRAINT * cons);

extern SM_CLASS_CONSTRAINT
  * classobj_find_cons_primary_key (SM_CLASS_CONSTRAINT * cons_list);

extern const char *classobj_map_constraint_to_property (SM_CONSTRAINT_TYPE
							constraint);
extern char *classobj_describe_foreign_key_action (SM_FOREIGN_KEY_ACTION
						   action);

extern bool classobj_is_pk_refer_other (MOP clsop,
					SM_FOREIGN_KEY_INFO * fk_info);

#endif /* _CLASS_OBJECT_H_ */
