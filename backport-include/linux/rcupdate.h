#ifndef _BACKPORT_LINUX_RCUPDATE_H
#define _BACKPORT_LINUX_RCUPDATE_H
#include <linux/version.h>
#include_next <linux/rcupdate.h>

/**
 *  * rcu_replace_pointer() - replace an RCU pointer, returning its old value
 *   * @rcu_ptr: RCU pointer, whose old value is returned
 *    * @ptr: regular pointer
 *     * @c: the lockdep conditions under which the dereference will take place
 *      *
 *       * Perform a replacement, where @rcu_ptr is an RCU-annotated
 *        * pointer and @c is the lockdep argument that is passed to the
 *         * rcu_dereference_protected() call used to read that pointer.  The old
 *          * value of @rcu_ptr is returned, and @rcu_ptr is set to @ptr.
 *           */
#define rcu_replace_pointer(rcu_ptr, ptr, c)                            \
({                                                                      \
        typeof(ptr) __tmp = rcu_dereference_protected((rcu_ptr), (c));  \
        rcu_assign_pointer((rcu_ptr), (ptr));                           \
        __tmp;                                                          \
})


#endif
