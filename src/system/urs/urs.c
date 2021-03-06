/*
 * Universal Resource System - URS
 */

#include "common/include/data.h"
#include "common/include/syscall.h"
#include "common/include/errno.h"
#include "klibc/include/stdio.h"
#include "klibc/include/stdlib.h"
#include "klibc/include/string.h"
#include "klibc/include/stdstruct.h"
#include "klibc/include/assert.h"
#include "klibc/include/sys.h"
#include "system/include/urs.h"


static int super_salloc_id;
static int node_salloc_id;
static int open_salloc_id;

static hash_t *super_table;
static hash_t *open_table;


/*
 * Hash table
 */
static unsigned int urs_hash_func(void *key, unsigned int size)
{
    char *str = (char *)key;
    unsigned int k = 0;
    
    do {
        k += (unsigned int)(*str);
    } while (*str++);
    
    return k % size;
}

static int urs_hash_cmp(void *cmp_key, void *node_key)
{
    char *cmp_ch = (char *)cmp_key;
    char *node_ch = (char *)node_key;
    
    return strcmp(cmp_ch, node_ch);
}


/*
 * Init URS
 */
void init_urs()
{
    super_table = hash_new(0, urs_hash_func, urs_hash_cmp);
    open_table = hash_new(0, urs_hash_func, urs_hash_cmp);
    
    super_salloc_id = salloc_create(sizeof(struct urs_super), 0, NULL, NULL);
    node_salloc_id = salloc_create(sizeof(struct urs_node), 0, NULL, NULL);
    open_salloc_id = salloc_create(sizeof(struct urs_open), 0, NULL, NULL);
}


/*
 * Super
 */
#define DEFAULT_NAMESPACE           "vfs://"
#define DEFAULT_NAMESPACE_LENGTH    (sizeof(DEFAULT_NAMESPACE) - 1)

static struct urs_super *get_super_by_id(unsigned long id)
{
    return (struct urs_super *)id;
}

static char *normalize_path(char *path)
{
    char *copy = NULL;
    
    int len = strlen(path);
    if (!len) {
        return NULL;
    }
    
    if (len >= 3 && path[len - 1] == '/' && (path[len - 2] != '/' || path[len - 3] != ':')) {
        len--;
    }
    
    if (path[0] == '/') {
        len += DEFAULT_NAMESPACE_LENGTH;   // vfs://
    }
    
    copy = (char *)calloc(len + 1, sizeof(char));
    if (path[0] == '/') {
        memcpy(copy, DEFAULT_NAMESPACE, 6);
        memcpy(copy + DEFAULT_NAMESPACE_LENGTH, path, len - DEFAULT_NAMESPACE_LENGTH);
    } else {
        memcpy(copy, path, len);
    }
    copy[len] = '\0';
    
//     kprintf("len: %d\n", len);
//     kprintf(path);
//     kprintf("Path normalized: %s -> %s @ %p\n", path, copy, copy);
    
    return copy;
}

static struct urs_super *match_super(char *path)
{
    int found = 0;
    struct urs_super *super = NULL;
    
    int cur_pos = 0;
    char *copy = normalize_path(path);
    if (!copy) {
        return NULL;
    }
    cur_pos = strlen(copy) - 1;
    
    do {
//         kprintf("copy: %s\n", copy);
        if (hash_contains(super_table, copy)) {
            found = 1;
            break;
        }
        
        do {
            cur_pos--;
        } while (cur_pos > 0 && copy[cur_pos] != '/');
        
        if (cur_pos && copy[cur_pos] == '/') {
            copy[cur_pos + 1] = '\0';
        } else {
            copy[cur_pos] = '\0';
        }
    } while (cur_pos > 0);
    
    if (!found) {
        free(copy);
        return NULL;
    }
    
    super = (struct urs_super *)hash_obtain(super_table, copy);
    assert(super);
    super->ref_count++;
    hash_release(super_table, copy, super);
    
//     kprintf("Super matched: %s ~ %s @ %p\n", copy, super->path, super);
    
    return super;
}

static struct urs_super *obtain_super(char *path)
{
    struct urs_super *super = NULL;
    
    char *copy = normalize_path(path);
    if (!copy) {
        return NULL;
    }
    
    super = (struct urs_super *)hash_obtain(super_table, copy);
    if (!super) {
        free(copy);
        return NULL;
    }
    
    super->ref_count++;
    hash_release(super_table, copy, super);
    free(copy);
    
    return super;
}

static struct urs_super *release_super(struct urs_super *s)
{
    s->ref_count--;
}

unsigned long urs_register(char *path, char *name, unsigned int flags, struct urs_reg_ops *ops)
{
    int i;
    struct urs_super *super = NULL;
    
    char *copy = normalize_path(path);
    if (!copy) {
        return 0;
    }
    
    super = obtain_super(copy);
    if (super) {
        release_super(super);
        free(copy);
        return 0;
    }
    
    super = (struct urs_super *)salloc(super_salloc_id);
    super->id = (unsigned long)super;
    super->path = copy;
    super->ref_count = 1;
    
    // Set up op
    for (i = 0; i < uop_count; i++) {
        if (ops && ops->entries[i].type == ureg_func) {
            super->ops[i].type = udisp_func;
            super->ops[i].func = ops->entries[i].func;
        }
        
        else if (ops && ops->entries[i].type == ureg_msg) {
            super->ops[i].type = udisp_msg;
            super->ops[i].mbox_id = ops->mbox_id;
            super->ops[i].msg_opcode = ops->entries[i].msg_opcode;
            super->ops[i].msg_func_num = ops->entries[i].msg_func_num;
        }
        
        else {
            super->ops[i].type = udisp_none;
        }
    }
    
    hash_insert(super_table, copy, super);
    return super->id;
}

int urs_unregister(char *path)
{
    return 0;
}

// int urs_register_op(
//     unsigned long id, enum urs_op_type op, void *func,
//     unsigned long mbox_id, unsigned long msg_opcode, unsigned long msg_func_num)
// {
//     struct urs_super *super = get_super_by_id(id);
//     if (!super) {
//         return -1;
//     }
//     
//     if (func) {
//         super->ops[op].type = udisp_func;
//         super->ops[op].func = func;
//     }
//     
//     else {
//         super->ops[op].type = udisp_msg;
//         super->ops[op].mbox_id = mbox_id;
//         super->ops[op].msg_opcode = msg_opcode;
//         super->ops[op].msg_func_num = msg_func_num;
//     }
//     
//     return 0;
// }


/*
 * Dispatch
 */
static msg_t *create_dispatch_msg(struct urs_super *super, enum urs_op_type op, unsigned long id)
{
    msg_t *msg = syscall_msg();
    
    msg->mailbox_id = super->ops[op].mbox_id;
    msg->opcode = super->ops[op].msg_opcode;
    msg->func_num = super->ops[op].msg_func_num;
    
    msg_param_value(msg, (unsigned long)op);
    msg_param_value(msg, super->id);
    msg_param_value(msg, id);   // This can be either node id or open id
    
    return msg;
}

static int dispatch_lookup(struct urs_super *super, unsigned long node_id, unsigned long proc_id, char *next, int *is_link,
                           unsigned long *next_id, void *buf, unsigned long count, unsigned long *actual)
{
    int result = 0;
    enum urs_op_type op = uop_lookup;
    
//     kprintf("To dispatch lookup @ %s\n", next);
    
    if (super->ops[op].type == udisp_none) {
        if (is_link) {
            *is_link = 0;
        }
        if (next_id) {
            *next_id = 0;
        }
        
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, proc_id, next, is_link, next_id, buf, count, actual);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        ulong len = 0;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value(s, proc_id);
        msg_param_buffer(s, next, (size_t)(strlen(next) + 1));
        msg_param_value(s, count);
        
        r = syscall_request();
        if (is_link) {
            *is_link = (int)r->params[0].value;
        }
        if (next_id) {
            *next_id = r->params[1].value;
        }
        
//         kprintf("send win @ %p, recv win @ %p\n", s, r);
//         kprintf("lookup returned, is link: %d, next: %p\n", (int)r->params[0].value, r->params[1].value);
        
//         if (buf) {
//             u8 *src = (u8 *)((unsigned long)r + r->params[2].offset);
//             u8 *dest = buf;
//             ulong s = r->params[3].value;
//             
//             while (len < count && len < s) {
//                 dest[len] = src[len];
//                 len++;
//             }
//         }
//         if (actual) {
//             *actual = len;
//         }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_open(struct urs_super *super, unsigned long node_id, unsigned long proc_id, unsigned long *open_dispatch_id)
{
    int result = 0;
    enum urs_op_type op = uop_open;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, proc_id, open_dispatch_id);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value(s, proc_id);
        
        r = syscall_request();
        if (open_dispatch_id) {
            *open_dispatch_id = r->params[0].value;
        }
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_close(struct urs_super *super, unsigned long node_id)
{
    int result = 0;
    enum urs_op_type op = uop_release;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        s = create_dispatch_msg(super, op, node_id);
        
        r = syscall_request();
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_read(struct urs_super *super, unsigned long node_id, void *buf, unsigned long count, unsigned long *actual)
{
    int result = 0;
    enum urs_op_type op = uop_read;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, buf, count, actual);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        ulong len = 0;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value(s, count);
        
        r = syscall_request();
        if (buf) {
            u8 *src = (u8 *)((unsigned long)r + r->params[0].offset);
            u8 *dest = buf;
            ulong s = r->params[1].value;
            
            while (len < count && len < s) {
                dest[len] = src[len];
                len++;
            }
        }
        if (actual) {
            *actual = len;
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_write(struct urs_super *super, unsigned long node_id, void *buf, unsigned long count, unsigned long *actual)
{
    int result = 0;
    enum urs_op_type op = uop_write;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, buf, count, actual);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
//         kprintf("to dispatch write, buf: %s, size: %p\n", (char *)buf, count);
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_buffer(s, buf, count);
        msg_param_value(s, count);
        
        r = syscall_request();
        if (actual) {
            *actual = r->params[0].value;
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_truncate(struct urs_super *super, unsigned long node_id)
{
    int result = 0;
    enum urs_op_type op = uop_write;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        s = create_dispatch_msg(super, op, node_id);
        
        r = syscall_request();
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_seek_data(struct urs_super *super, unsigned long node_id, u64 offset, enum urs_seek_from from, u64 *newpos)
{
    int result = 0;
    enum urs_op_type op = uop_seek_data;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, offset, from, newpos);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value64(s, offset);
        msg_param_value(s, (unsigned long)from);
        
        r = syscall_request();
        if (newpos) {
            *newpos = r->params[0].value64;
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_list(struct urs_super *super, unsigned long node_id, void *buf, unsigned long count, unsigned long *actual)
{
    int result = 0;
    enum urs_op_type op = uop_list;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, buf, count, actual);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        ulong len = 0;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value(s, count);
        
        r = syscall_request();
        if (buf) {
            u8 *src = (u8 *)((unsigned long)r + r->params[0].offset);
            u8 *dest = buf;
            ulong s = r->params[1].value;
            
            while (len < count && len < s) {
                dest[len] = src[len];
                len++;
            }
            
//             kprintf("received: %s\n", buf);
        }
        if (actual) {
            *actual = len;
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_seek_list(struct urs_super *super, unsigned long node_id, u64 offset, enum urs_seek_from from, u64 *newpos)
{
    int result = 0;
    enum urs_op_type op = uop_seek_list;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, offset, from, newpos);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value64(s, offset);
        msg_param_value(s, (unsigned long)from);
        
        r = syscall_request();
        if (newpos) {
            *newpos = r->params[0].value64;
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_create(struct urs_super *super, unsigned long node_id, char *name, enum urs_create_type type, unsigned int flags, char *target, unsigned long target_id)
{
    int result = 0;
    enum urs_op_type op = uop_create;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, name, type, flags, target, target_id);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_buffer(s, name, strlen(name) + 1);
        msg_param_value(s, (unsigned long)type);
        msg_param_value(s, (unsigned long)flags);
        msg_param_buffer(s, target, strlen(target) + 1);
        msg_param_value(s, target_id);
        
        r = syscall_request();
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_remove(struct urs_super *super, unsigned long node_id, int erase)
{
    int result = 0;
    enum urs_op_type op = uop_remove;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, erase);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_value(s, (unsigned long)erase);
        
        r = syscall_request();
        result = (int)r->params[r->param_count - 1].value;;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_rename(struct urs_super *super, unsigned long node_id, char *name)
{
    int result = 0;
    enum urs_op_type op = uop_rename;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, name);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        
        s = create_dispatch_msg(super, op, node_id);
        msg_param_buffer(s, name, strlen(name) + 1);
        
        r = syscall_request();
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}

static int dispatch_stat(struct urs_super *super, unsigned long node_id, struct urs_stat *stat)
{
    int result = 0;
    enum urs_op_type op = uop_stat;
    
    if (super->ops[op].type == udisp_none) {
        return -1;
    }
    
    else if (super->ops[op].type == udisp_func) {
        result = super->ops[op].func(super->id, node_id, stat);
    }
    
    else if (super->ops[op].type == udisp_msg) {
        msg_t *s, *r;
        struct urs_stat *ret = NULL;
        
        s = create_dispatch_msg(super, op, node_id);
        r = syscall_request();
        
        ret = (struct urs_stat *)((unsigned long)r + r->params[0].offset);
        if (ret && stat) {
            memcpy(stat, ret, sizeof(struct urs_stat));
        }
        
        result = (int)r->params[r->param_count - 1].value;
    }
    
    else {
        return -2;
    }
    
    return result;
}


/*
 * Node operations
 */
static struct urs_open *get_open_by_id(unsigned long id)
{
    return (struct urs_open *)id;
}

static int get_next_name(char *path, int start, char **name)
{
    char *copy = NULL;
    int end = 0;
    int len = 0;
    
    if (name && *name) {
        free(*name);
        *name = NULL;
    }
    
    len = strlen(path);
    if (start >= len) {
        return 0;
    }
    
    if (path[start] == '/') {
        start++;
        if (start >= len) {
            return 0;
        }
    }
    
    end = start;
    while (path[end] != '/' && end < len) {
        end++;
    }
    
    if (name) {
        copy = (char *)calloc(end - start + 1, sizeof(char));
        memcpy(copy, &path[start], end - start);
        copy[end - start] = '\0';
        *name = copy;
    }
    
    return end + 1;
}

static struct urs_node *get_next_node(struct urs_super *super, struct urs_node *cur, unsigned long proc_id, char *name,
                                      int *is_link, void *buf, unsigned long count, unsigned long *actual)
{
    int error = 0;
    unsigned long next_id = 0;
    struct urs_node *next;
    
//     kprintf("get next node\n");
    
    if (!cur) {
        if (!name) {
            error = dispatch_lookup(super, 0, proc_id, "/", is_link,
                                    &next_id, buf, count, actual);
        } else {
            return NULL;
        }
    } else {
        error = dispatch_lookup(super, cur->dispatch_id, proc_id, name, is_link,
                                &next_id, buf, count, actual);
        sfree(cur);
    }
    
    if (!next_id || error) {
//         kprintf("Next node empty!\n");
        return NULL;
    }
    
    next = (struct urs_node *)salloc(node_salloc_id);
    next->dispatch_id = next_id;
    next->id = (unsigned long)next;
    next->ref_count = 1;
    next->super = super;
    
    return next;
}

static int is_absolute_path(char *path)
{
    size_t i;
    size_t len = strlen(path);
    if (!len) {
        return 0;
    }
    
    if (path[0] == '/') {
        return 1;
    }
    
    for (i = 0; i < len; i++) {
        if (
            path[i] == ':' && i < len - 2 &&
            path[i + 1] == '/' && path[i + 2] == '/'
        ) {
            return 1;
        }
    }
    
    return 0;
}

static struct urs_node *follow_link(struct urs_super *super, struct urs_node *cur_node, unsigned long proc_id, char *link_path, struct urs_super **real_super)
{
    char *name = 0;
    char *copy = normalize_path(link_path);
    int cur_pos = 0;
    
    if (is_absolute_path(copy)) {
        super = match_super(link_path);
        if (real_super) {
            *real_super = super;
        }
        
        if (!super) {
            free(copy);
            return NULL;
        }
        
        cur_pos = strlen(super->path);
    } else {
        cur_pos = get_next_name(copy, cur_pos, &name);
    }
    
    do {
        int is_link;
        u8 buf[128];
        unsigned long link_len = 0;
        
        cur_node = get_next_node(super, cur_node, proc_id, name, &is_link, buf, sizeof(buf), &link_len);
        if (is_link) {
            cur_node = follow_link(super, cur_node, proc_id, (char *)buf, real_super);
        }
        
        cur_pos = get_next_name(copy, cur_pos, &name);
    } while (cur_pos && cur_node);
    
    free(copy);
    if (name) {
        free(name);
    }
    
    return cur_node;
}

static struct urs_node *resolve_path(char *path, struct urs_super **real_super, unsigned long proc_id)
{
    char *name = NULL;
    char *copy = normalize_path(path);
    int cur_pos = 0;
    struct urs_node *cur_node = NULL;
    
//     kprintf("to resolve: %s\n", path);
    
    // Find out super
    struct urs_super *super = match_super(copy);
    if (real_super) {
        *real_super = super;
    }
    if (!super) {
        free(copy);
        return NULL;
    }
    
    // Look up the path
    cur_pos = strlen(super->path);
    
    do {
        int is_link;
        u8 buf[128];
        unsigned long link_len = 0;
        
        cur_node = get_next_node(super, cur_node, proc_id, name, &is_link, buf, sizeof(buf), &link_len);
        if (is_link) {
            cur_node = follow_link(super, cur_node, proc_id, (char *)buf, real_super);
        }
        
        cur_pos = get_next_name(copy, cur_pos, &name);
    } while (cur_pos && cur_node);
    
    free(copy);
    if (name) {
        free(name);
    }
    
    return cur_node;
}

unsigned long urs_open_node(char *path, unsigned int flags, unsigned long process_id)
{
    struct urs_open *o = NULL;
    struct urs_super *super = NULL;
    unsigned long open_dispatch_id = 0;
    
    // Resolve path
    struct urs_node *node = resolve_path(path, &super, process_id);
    if (!node) {
        return 0;
    }
    
    // Open the node
    if (dispatch_open(super, node->dispatch_id, process_id, &open_dispatch_id)) {
        return 0;
    }
    
    o = (struct urs_open *)salloc(open_salloc_id);
    o->id = (unsigned long)o;
    o->path = strdup(path);
    o->node = node;
    o->super = super;
    o->open_dispatch_id = open_dispatch_id;
    hash_insert(open_table, o->path, o);
    
    return o->id;
}

int urs_close_node(unsigned long id)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (!o) {
        return EBADF;
    }
    
    error = dispatch_close(o->super, o->open_dispatch_id);
//         kprintf("Node released: %p\n", id);
    if (!error) {
        hash_remove(open_table, o->path);
        release_super(o->node->super);
        sfree(o->node);
        free(o->path);
        sfree(o);
    }
    
    return error;
}

int urs_read_node(unsigned long id, void *buf, unsigned long count, unsigned long *actual)
{
    struct urs_open *o = get_open_by_id(id);
    int error = EOK;
    
    if (o) {
        error = dispatch_read(o->super, o->open_dispatch_id, buf, count, actual);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_write_node(unsigned long id, void *buf, unsigned long count, unsigned long *actual)
{
    struct urs_open *o = get_open_by_id(id);
    int error = EOK;
    
    if (o) {
        error = dispatch_write(o->super, o->open_dispatch_id, buf, count, actual);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_truncate_node(unsigned long id)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_truncate(o->super, o->open_dispatch_id);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_seek_data(unsigned long id, u64 offset, enum urs_seek_from from, u64 *newpos)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_seek_data(o->super, o->open_dispatch_id, offset, from, newpos);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_list_node(unsigned long id, void *buf, unsigned long count, unsigned long *actual)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_list(o->super, o->open_dispatch_id, buf, count, actual);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_seek_list(unsigned long id, u64 offset, enum urs_seek_from from, u64 *newpos)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_seek_list(o->super, o->open_dispatch_id, offset, from, newpos);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_create_node(unsigned long id, char *name, enum urs_create_type type, unsigned int flags, char *target)
{
    int error = EOK;
    unsigned long target_node_id = 0;
    
    struct urs_open *o = get_open_by_id(id);
    if (!o) {
        return EBADF;
    }
    
    switch (type) {
    // Hard link needs to figure out the target dispatch node ID first
    case ucreate_hard_link:
        target_node_id = -1;
    
    // Create the link
    case ucreate_node:
    case ucreate_sym_link:
        error = dispatch_create(o->super, o->open_dispatch_id, name, type, flags, target, target_node_id);
        break;
    
    // Skip for now
    case ucreate_dyn_link:
    case ucreate_none:
    default:
        break;
    }
    
    return error;
}

int urs_remove_node(unsigned long id, int erase)
{
    struct urs_open *o = get_open_by_id(id);
    int error = EOK;
    
    error = dispatch_remove(o->super, o->open_dispatch_id, erase);
    if (error) {
        return error;
    }
    
    hash_remove(open_table, o->path);
    release_super(o->node->super);
    sfree(o->node);
    free(o->path);
    sfree(o);
    
    return 0;
}

int urs_rename_node(unsigned long id, char *name)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_rename(o->super, o->open_dispatch_id, name);
    } else {
        error = EBADF;
    }
    
    return error;
}

int urs_stat_node(unsigned long id, struct urs_stat *stat)
{
    int error = EOK;
    struct urs_open *o = get_open_by_id(id);
    
    if (o) {
        error = dispatch_stat(o->super, o->open_dispatch_id, stat);
    } else {
        error = EBADF;
    }
    
    return error;
}
