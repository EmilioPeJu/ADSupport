/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:             H5ACproxy_entry.c
 *
 * Purpose:             Functions and a cache client for a "proxy" cache entry.
 *			A proxy cache entry is used as a placeholder for entire
 *			data structures to attach flush dependencies, etc.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/
#include "H5ACmodule.h"         /* This source code file is part of the H5AC module */

/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                    */
#include "H5ACpkg.h"            /* Metadata cache                       */
#include "H5Eprivate.h"         /* Error handling                       */
#include "H5MFprivate.h"	/* File memory management		*/


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/

/* Metadata cache (H5AC) callbacks */
static herr_t H5AC__proxy_entry_image_len(const void *thing, size_t *image_len,
    hbool_t *compressed_ptr, size_t *compressed_image_len_ptr);
static herr_t H5AC__proxy_entry_serialize(const H5F_t *f, void *image_ptr,
    size_t len, void *thing);
static herr_t H5AC__proxy_entry_notify(H5AC_notify_action_t action, void *thing);
static herr_t H5AC__proxy_entry_free_icr(void *thing);

/*********************/
/* Package Variables */
/*********************/

/* H5AC proxy entries inherit cache-like properties from H5AC */
const H5AC_class_t H5AC_PROXY_ENTRY[1] = {{
    H5AC_PROXY_ENTRY_ID,               	/* Metadata client ID */
    "Proxy entry",           		/* Metadata client name (for debugging) */
    H5FD_MEM_SUPER,                     /* File space memory type for client */
    0,					/* Client class behavior flags */
    NULL,    				/* 'get_load_size' callback */
    NULL,				/* 'verify_chksum' callback */
    NULL,    				/* 'deserialize' callback */
    H5AC__proxy_entry_image_len,	/* 'image_len' callback */
    NULL,                               /* 'pre_serialize' callback */
    H5AC__proxy_entry_serialize,	/* 'serialize' callback */
    H5AC__proxy_entry_notify,		/* 'notify' callback */
    H5AC__proxy_entry_free_icr,        	/* 'free_icr' callback */
    NULL,                              	/* 'clear' callback */
    NULL,                              	/* 'fsf_size' callback */
}};


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Declare a free list to manage H5AC_proxy_entry_t objects */
H5FL_DEFINE_STATIC(H5AC_proxy_entry_t);



/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_create
 *
 * Purpose:     Create a new proxy entry
 *
 * Return:	Success:	Pointer to the new proxy entry object.
 *		Failure:	NULL
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
H5AC_proxy_entry_t *
H5AC_proxy_entry_create(void)
{
    H5AC_proxy_entry_t *pentry = NULL;  /* Pointer to new proxy entry */
    H5AC_proxy_entry_t *ret_value = NULL;       /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Allocate new proxy entry */
    if(NULL == (pentry = H5FL_CALLOC(H5AC_proxy_entry_t)))
        HGOTO_ERROR(H5E_CACHE, H5E_CANTALLOC, NULL, "can't allocate proxy entry")

    /* Set non-zero fields */
    pentry->addr = HADDR_UNDEF;

    /* Set return value */
    ret_value = pentry;

done:
    /* Release resources on error */
    if(!ret_value)
        if(pentry)
            pentry = H5FL_FREE(H5AC_proxy_entry_t, pentry);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_create() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_add_parent
 *
 * Purpose:     Add a parent to a proxy entry
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_proxy_entry_add_parent(H5AC_proxy_entry_t *pentry, void *_parent)
{
    H5AC_info_t *parent = (H5AC_info_t *)_parent; /* Parent entry's cache info */
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(parent);
    HDassert(pentry);

    /* Add parent to the list of parents */
    if(NULL == pentry->parents)
        if(NULL == (pentry->parents = H5SL_create(H5SL_TYPE_HADDR, NULL)))
            HGOTO_ERROR(H5E_CACHE, H5E_CANTCREATE, FAIL, "unable to create skip list for parents of proxy entry")

    /* Insert parent address into skip list */
    if(H5SL_insert(pentry->parents, parent, &parent->addr) < 0)
        HGOTO_ERROR(H5E_CACHE, H5E_CANTINSERT, FAIL, "unable to insert parent into proxy's skip list")

    /* Add flush dependency on parent */
    if(pentry->nchildren > 0) {
        /* Sanity check */
        HDassert(H5F_addr_defined(pentry->addr));

        if(H5AC_create_flush_dependency(parent, pentry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTDEPEND, FAIL, "unable to set flush dependency on proxy entry")
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_add_parent() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_remove_parent
 *
 * Purpose:     Removes a parent from a proxy entry
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_proxy_entry_remove_parent(H5AC_proxy_entry_t *pentry, void *_parent)
{
    H5AC_info_t *parent = (H5AC_info_t *)_parent;   /* Pointer to the parent entry */
    H5AC_info_t *rem_parent;            /* Pointer to the removed parent entry */
    herr_t ret_value = SUCCEED;        	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(pentry);
    HDassert(pentry->parents);
    HDassert(parent);

    /* Remove parent from skip list */
    if(NULL == (rem_parent = (H5AC_info_t *)H5SL_remove(pentry->parents, &parent->addr)))
        HGOTO_ERROR(H5E_CACHE, H5E_CANTREMOVE, FAIL, "unable to remove proxy entry parent from skip list")
    if(!H5F_addr_eq(rem_parent->addr, parent->addr))
        HGOTO_ERROR(H5E_CACHE, H5E_BADVALUE, FAIL, "removed proxy entry parent not the same as real parent")

    /* Shut down the skip list, if this is the last parent */
    if(0 == H5SL_count(pentry->parents)) {
        /* Sanity check */
        HDassert(0 == pentry->nchildren);

        if(H5SL_close(pentry->parents) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CLOSEERROR, FAIL, "can't close proxy parent skip list")
        pentry->parents = NULL;
    } /* end if */

    /* Remove flush dependency between the proxy entry and a parent */
    if(pentry->nchildren > 0)
        if(H5AC_destroy_flush_dependency(parent, pentry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTUNDEPEND, FAIL, "unable to remove flush dependency on proxy entry")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_remove_parent() */


/*-------------------------------------------------------------------------
 * Function:	H5AC__proxy_entry_add_child_cb
 *
 * Purpose:	Callback routine for adding an entry as a flush dependency for
 *		a proxy entry.
 *
 * Return:	Success:	Non-negative on success
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		Thursday, September 22, 2016
 *
 *-------------------------------------------------------------------------
 */
static int
H5AC__proxy_entry_add_child_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5AC_info_t *parent = (H5AC_info_t *)_item;   /* Pointer to the parent entry */
    H5AC_proxy_entry_t *pentry = (H5AC_proxy_entry_t *)_udata; /* Pointer to the proxy entry */
    int ret_value = H5_ITER_CONT;     /* Callback return value */

    FUNC_ENTER_STATIC

    /* Add flush dependency on parent for proxy entry */
    if(H5AC_create_flush_dependency(parent, pentry) < 0)
        HGOTO_ERROR(H5E_CACHE, H5E_CANTDEPEND, H5_ITER_ERROR, "unable to set flush dependency for virtual entry")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC__proxy_entry_add_child_cb() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_add_child
 *
 * Purpose:     Add a child a proxy entry
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_proxy_entry_add_child(H5AC_proxy_entry_t *pentry, H5F_t *f, hid_t dxpl_id,
    void *child)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(pentry);
    HDassert(child);

    /* Check for first child */
    if(0 == pentry->nchildren) {
        /* Get an address, if the proxy doesn't already have one */
        if(!H5F_addr_defined(pentry->addr))
            if(HADDR_UNDEF == (pentry->addr = H5MF_alloc_tmp(f, 1)))
                HGOTO_ERROR(H5E_CACHE, H5E_CANTALLOC, FAIL, "temporary file space allocation failed for proxy entry")

        /* Insert the proxy entry into the cache */
        if(H5AC_insert_entry(f, dxpl_id, H5AC_PROXY_ENTRY, pentry->addr, pentry, H5AC__PIN_ENTRY_FLAG) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTINSERT, FAIL, "unable to cache proxy entry")

        /* Proxies start out clean (insertions are automatically marked dirty) */
        if(H5AC_mark_entry_clean(pentry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTCLEAN, FAIL, "can't mark proxy entry clean")

        /* If there are currently parents, iterate over the list of parents, creating flush dependency on them */
        if(pentry->parents)
            if(H5SL_iterate(pentry->parents, H5AC__proxy_entry_add_child_cb, pentry) < 0)
                HGOTO_ERROR(H5E_CACHE, H5E_BADITER, FAIL, "can't visit parents")
    } /* end if */

    /* Add flush dependency on proxy entry */
    if(H5AC_create_flush_dependency(pentry, child) < 0)
        HGOTO_ERROR(H5E_CACHE, H5E_CANTDEPEND, FAIL, "unable to set flush dependency on proxy entry")

    /* Increment count of children */
    pentry->nchildren++;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_add_child() */


/*-------------------------------------------------------------------------
 * Function:	H5AC__proxy_entry_remove_child_cb
 *
 * Purpose:	Callback routine for removing an entry as a flush dependency for
 *		proxy entry.
 *
 * Return:	Success:	Non-negative on success
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *		Thursday, September 22, 2016
 *
 *-------------------------------------------------------------------------
 */
static int
H5AC__proxy_entry_remove_child_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5AC_info_t *parent = (H5AC_info_t *)_item;   /* Pointer to the parent entry */
    H5AC_proxy_entry_t *pentry = (H5AC_proxy_entry_t *)_udata; /* Pointer to the proxy entry */
    int ret_value = H5_ITER_CONT;     /* Callback return value */

    FUNC_ENTER_STATIC

    /* Remove flush dependency on parent for proxy entry */
    if(H5AC_destroy_flush_dependency(parent, pentry) < 0)
        HGOTO_ERROR(H5E_CACHE, H5E_CANTUNDEPEND, H5_ITER_ERROR, "unable to remove flush dependency for proxy entry")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC__proxy_entry_remove_child_cb() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_remove_child
 *
 * Purpose:     Remove a child a proxy entry
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_proxy_entry_remove_child(H5AC_proxy_entry_t *pentry, void *child)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(pentry);
    HDassert(child);

    /* Remove flush dependency on proxy entry */
    if(H5AC_destroy_flush_dependency(pentry, child) < 0)
        HGOTO_ERROR(H5E_CACHE, H5E_CANTUNDEPEND, FAIL, "unable to remove flush dependency on proxy entry")

    /* Decrement count of children */
    pentry->nchildren--;

    /* Check for last child */
    if(0 == pentry->nchildren) {
        /* Check for flush dependencies on proxy's parents */
        if(pentry->parents)
            /* Iterate over the list of parents, removing flush dependency on them */
            if(H5SL_iterate(pentry->parents, H5AC__proxy_entry_remove_child_cb, pentry) < 0)
                HGOTO_ERROR(H5E_CACHE, H5E_BADITER, FAIL, "can't visit parents")

        /* Unpin proxy */
        if(H5AC_unpin_entry(pentry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTUNPIN, FAIL, "can't unpin proxy entry")

        /* Remove proxy entry from cache */
        if(H5AC_remove_entry(pentry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTREMOVE, FAIL, "unable to remove proxy entry")
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_remove_child() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_proxy_entry_dest
 *
 * Purpose:     Destroys a proxy entry in memory.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_proxy_entry_dest(H5AC_proxy_entry_t *pentry)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    HDassert(pentry);
    HDassert(NULL == pentry->parents);
    HDassert(0 == pentry->nchildren);
    HDassert(0 == pentry->ndirty_children);

    /* Free the proxy entry object */
    pentry = H5FL_FREE(H5AC_proxy_entry_t, pentry);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_proxy_entry_dest() */


/*-------------------------------------------------------------------------
 * Function:    H5AC__proxy_entry_image_len
 *
 * Purpose:     Compute the size of the data structure on disk.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5AC__proxy_entry_image_len(const void H5_ATTR_UNUSED *thing, size_t *image_len,
    hbool_t H5_ATTR_UNUSED *compressed_ptr, size_t H5_ATTR_UNUSED *compressed_image_len_ptr)
{
    FUNC_ENTER_STATIC_NOERR

    /* Check arguments */
    HDassert(image_len);

    /* Set the image length size to 1 byte */
    *image_len = 1;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5AC__proxy_entry_image_len() */


/*-------------------------------------------------------------------------
 * Function:    H5AC__proxy_entry_serialize
 *
 * Purpose:	Serializes a data structure for writing to disk.
 *
 * Note:	Should never be invoked.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5AC__proxy_entry_serialize(const H5F_t H5_ATTR_UNUSED *f, void H5_ATTR_UNUSED *image,
    size_t H5_ATTR_UNUSED len, void H5_ATTR_UNUSED *thing)
{
    FUNC_ENTER_STATIC_NOERR /* Yes, even though this pushes an error on the stack */

    /* Should never be invoked */
    HDassert(0 && "Invalid callback?!?");

    HERROR(H5E_CACHE, H5E_CANTSERIALIZE, "called unreachable fcn.");

    FUNC_LEAVE_NOAPI(FAIL)
} /* end H5AC__proxy_entry_serialize() */


/*-------------------------------------------------------------------------
 * Function:    H5AC__proxy_entry_notify
 *
 * Purpose:     Handle cache action notifications
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5AC__proxy_entry_notify(H5AC_notify_action_t action, void *_thing)
{
    H5AC_proxy_entry_t *pentry = (H5AC_proxy_entry_t *)_thing;
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(pentry);

    switch(action) {
        case H5AC_NOTIFY_ACTION_AFTER_INSERT:
	    break;

	case H5AC_NOTIFY_ACTION_AFTER_LOAD:
#ifdef NDEBUG
            HGOTO_ERROR(H5E_CACHE, H5E_BADVALUE, FAIL, "invalid notify action from metadata cache")
#else /* NDEBUG */
            HDassert(0 && "Invalid action?!?");
#endif /* NDEBUG */
            break;

	case H5AC_NOTIFY_ACTION_AFTER_FLUSH:
#ifdef NDEBUG
            HGOTO_ERROR(H5E_CACHE, H5E_BADVALUE, FAIL, "invalid notify action from metadata cache")
#else /* NDEBUG */
            HDassert(0 && "Invalid action?!?");
#endif /* NDEBUG */
	    break;

        case H5AC_NOTIFY_ACTION_BEFORE_EVICT:
            /* Sanity checks */
            HDassert(0 == pentry->ndirty_children);

            /* No action */
            break;

        case H5AC_NOTIFY_ACTION_ENTRY_DIRTIED:
            /* Sanity checks */
            HDassert(pentry->ndirty_children > 0);

            /* No action */
            break;

        case H5AC_NOTIFY_ACTION_ENTRY_CLEANED:
            /* Sanity checks */
            HDassert(0 == pentry->ndirty_children);

            /* No action */
            break;

        case H5AC_NOTIFY_ACTION_CHILD_DIRTIED:
            /* Increment # of dirty children */
            pentry->ndirty_children++;

            /* Check for first dirty child */
            if(1 == pentry->ndirty_children)
                if(H5AC_mark_entry_dirty(pentry) < 0)
                    HGOTO_ERROR(H5E_CACHE, H5E_CANTDIRTY, FAIL, "can't mark proxy entry dirty")
            break;

        case H5AC_NOTIFY_ACTION_CHILD_CLEANED:
            /* Sanity check */
            HDassert(pentry->ndirty_children > 0);

            /* Decrement # of dirty children */
            pentry->ndirty_children--;

            /* Check for last dirty child */
            if(0 == pentry->ndirty_children)
                if(H5AC_mark_entry_clean(pentry) < 0)
                    HGOTO_ERROR(H5E_CACHE, H5E_CANTCLEAN, FAIL, "can't mark proxy entry clean")
            break;

        default:
#ifdef NDEBUG
            HGOTO_ERROR(H5E_CACHE, H5E_BADVALUE, FAIL, "unknown notify action from metadata cache")
#else /* NDEBUG */
            HDassert(0 && "Unknown action?!?");
#endif /* NDEBUG */
    } /* end switch */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC__proxy_entry_notify() */


/*-------------------------------------------------------------------------
 * Function:	H5AC__proxy_entry_free_icr
 *
 * Purpose:	Destroy/release an "in core representation" of a data
 *              structure
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              September 17, 2016
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5AC__proxy_entry_free_icr(void *_thing)
{
    H5AC_proxy_entry_t *pentry = (H5AC_proxy_entry_t *)_thing;
    herr_t ret_value = SUCCEED;     		/* Return value */

    FUNC_ENTER_STATIC

    /* Destroy the proxy entry */
    if(H5AC_proxy_entry_dest(pentry) < 0)
	HGOTO_ERROR(H5E_CACHE, H5E_CANTFREE, FAIL, "unable to destroy proxy entry")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5AC__proxy_entry_free_icr() */

#ifdef OLD_CODE

/*-------------------------------------------------------------------------
 * Function:    H5AC_virt_entry_dirty_parent
 *
 * Purpose:     Indicate that a virtual entry's parent became dirty
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              July 23, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_virt_entry_dirty_parent(H5AC_virt_entry_t *ventry)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    HDassert(ventry);
    HDassert(ventry->track_parents);
    HDassert(ventry->nparents > 0);

    /* If this is the first dirty parent or child, mark the virtual entry dirty */
    if(ventry->in_cache && 0 == ventry->ndirty_parents && 0 == ventry->ndirty_children)
        if(H5AC_mark_entry_dirty(ventry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTDIRTY, FAIL, "can't mark virtual entry dirty")
    
    /* Increment the number of dirty parents */
    ventry->ndirty_parents++;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_virt_entry_dirty_parent() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_virt_entry_clean_parent
 *
 * Purpose:     Indicate that a virtual entry's parent became clean
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              July 23, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_virt_entry_clean_parent(H5AC_virt_entry_t *ventry)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    HDassert(ventry);
    HDassert(ventry->track_parents);
//    HDassert(ventry->nparents > 0);
    HDassert(ventry->ndirty_parents > 0);

    /* Decrement the number of dirty parents */
    ventry->ndirty_parents--;

    /* If this is the last dirty parent or child, mark the virtual entry clean */
    if(ventry->in_cache && 0 == ventry->ndirty_parents && 0 == ventry->ndirty_children)
        if(H5AC_mark_entry_clean(ventry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTCLEAN, FAIL, "can't mark virtual entry clean")
    
    /* Destroy the skip list, if no more parents */
    if(0 == ventry->nparents && 0 == ventry->ndirty_parents) {
//        /* Sanity check */
//        HDassert(0 == ventry->ndirty_parents);

        if(H5SL_close(ventry->parents) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CLOSEERROR, FAIL, "can't close parent list")
        ventry->parents = NULL;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_virt_entry_clean_parent() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_virt_entry_dirty_child
 *
 * Purpose:     Indicate that a virtual entry's child became dirty
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              July 24, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_virt_entry_dirty_child(H5AC_virt_entry_t *ventry)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    HDassert(ventry);
    HDassert(ventry->nchildren > 0);

    /* If this is the first dirty parent or child, mark the virtual entry dirty */
    if(ventry->in_cache && 0 == ventry->ndirty_parents && 0 == ventry->ndirty_children)
        if(H5AC_mark_entry_dirty(ventry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTDIRTY, FAIL, "can't mark virtual entry dirty")
    
    /* Increment the number of dirty children */
    ventry->ndirty_children++;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_virt_entry_dirty_child() */


/*-------------------------------------------------------------------------
 * Function:    H5AC_virt_entry_clean_child
 *
 * Purpose:     Indicate that a virtual entry's child became clean
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Quincey Koziol
 *              July 24, 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5AC_virt_entry_clean_child(H5AC_virt_entry_t *ventry)
{
    herr_t ret_value = SUCCEED;         	/* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    HDassert(ventry);
    HDassert(ventry->nchildren > 0);
    HDassert(ventry->ndirty_children > 0);

    /* Decrement the number of dirty children */
    ventry->ndirty_children--;

    /* If this is the last dirty parent or child, mark the virtual entry clean */
    if(ventry->in_cache && 0 == ventry->ndirty_parents && 0 == ventry->ndirty_children)
        if(H5AC_mark_entry_clean(ventry) < 0)
            HGOTO_ERROR(H5E_CACHE, H5E_CANTCLEAN, FAIL, "can't mark virtual entry clean")
    
done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5AC_virt_entry_clean_child() */

#endif /* OLD_CODE */


