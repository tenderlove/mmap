#include <ruby.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#if HAVE_SEMCTL && HAVE_SHMCTL
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#endif

#include <ruby/io.h>
#include <ruby/re.h>
#include <ruby/util.h>

#ifndef StringValue
#define StringValue(x)            \
    do                            \
    {                             \
        if (TYPE(x) != T_STRING)  \
            x = rb_str_to_str(x); \
    } while (0)
#endif

#ifndef StringValuePtr
#define StringValuePtr(x) STR2CSTR(x)
#endif

#ifndef SafeStringValue
#define SafeStringValue(x) Check_SafeStr(x)
#endif

#ifndef MADV_NORMAL
#ifdef POSIX_MADV_NORMAL
#define MADV_NORMAL POSIX_MADV_NORMAL
#define MADV_RANDOM POSIX_MADV_RANDOM
#define MADV_SEQUENTIAL POSIX_MADV_SEQUENTIAL
#define MADV_WILLNEED POSIX_MADV_WILLNEED
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#define madvise posix_madvise
#endif
#endif

#define BEG(match, no) RMATCH_REGS(match)->beg[no]
#define END(match, no) RMATCH_REGS(match)->end[no]

#ifndef MMAP_RETTYPE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309
#endif /* !_POSIX_C_SOURCE */
#ifdef _POSIX_VERSION
#if _POSIX_VERSION >= 199309
#define MMAP_RETTYPE void *
#endif /* _POSIX_VERSION >= 199309 */
#endif /* _POSIX_VERSION */
#endif /* !MMAP_RETTYPE */

#ifndef MMAP_RETTYPE
#define MMAP_RETTYPE caddr_t
#endif

#ifndef MAP_FAILED
#define MAP_FAILED ((caddr_t) - 1)
#endif /* !MAP_FAILED */

#ifndef MAP_ANON
#ifdef MAP_ANONYMOUS
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

static VALUE mm_cMap;

#define EXP_INCR_SIZE 4096

typedef struct
{
    MMAP_RETTYPE addr;
    int smode, pmode, vscope;
    int advice, flag;
    VALUE key;
    int semid, shmid;
    size_t len, real, incr;
    off_t offset;
    char *path, *template;
} mm_mmap;

typedef struct
{
    int count;
    mm_mmap *t;
} mm_ipc;

typedef struct
{
    VALUE obj, *argv;
    long id;
    int flag, argc;
} mm_bang;

#define MM_MODIFY 1
#define MM_ORIGIN 2
#define MM_CHANGE (MM_MODIFY | 4)
#define MM_PROTECT 8

#define MM_FIXED (1 << 1)
#define MM_ANON (1 << 2)
#define MM_LOCK (1 << 3)
#define MM_IPC (1 << 4)
#define MM_TMP (1 << 5)

#if HAVE_SEMCTL && HAVE_SHMCTL
static char template[1024];
#endif

static void
mm_free(mm_ipc *i_mm)
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    if (i_mm->t->flag & MM_IPC)
    {
        struct shmid_ds buf;

        if (shmctl(i_mm->t->shmid, IPC_STAT, &buf) != -1)
        {
            if (buf.shm_nattch == 1 && (i_mm->t->flag & MM_TMP))
            {
                semctl(i_mm->t->semid, 0, IPC_RMID);
                if (i_mm->t->template)
                {
                    unlink(i_mm->t->template);
                    free(i_mm->t->template);
                }
            }
        }
        shmdt(i_mm->t);
    }
    else
    {
        free(i_mm->t);
    }
#endif
    if (i_mm->t->path)
    {
        munmap(i_mm->t->addr, i_mm->t->len);
        if (i_mm->t->path != (char *)-1)
        {
            if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE &&
                truncate(i_mm->t->path, i_mm->t->real) == -1)
            {
                free(i_mm->t->path);
                free(i_mm);
                rb_raise(rb_eTypeError, "truncate");
            }
            free(i_mm->t->path);
        }
    }
    free(i_mm);
}

static void
mm_lock(mm_ipc *i_mm, int wait_lock)
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    struct sembuf sem_op;

    if (i_mm->t->flag & MM_IPC)
    {
        i_mm->count++;
        if (i_mm->count == 1)
        {
        retry:
            sem_op.sem_num = 0;
            sem_op.sem_op = -1;
            sem_op.sem_flg = IPC_NOWAIT;
            if (semop(i_mm->t->semid, &sem_op, 1) == -1)
            {
                if (errno == EAGAIN)
                {
                    if (!wait_lock)
                    {
                        rb_raise(rb_const_get(rb_mErrno, rb_intern("EAGAIN")), "EAGAIN");
                    }
                    rb_thread_sleep(1);
                    goto retry;
                }
                rb_sys_fail("semop()");
            }
        }
    }
#endif
}

static void
mm_unlock(mm_ipc *i_mm)
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    struct sembuf sem_op;

    if (i_mm->t->flag & MM_IPC)
    {
        i_mm->count--;
        if (!i_mm->count)
        {
        retry:
            sem_op.sem_num = 0;
            sem_op.sem_op = 1;
            sem_op.sem_flg = IPC_NOWAIT;
            if (semop(i_mm->t->semid, &sem_op, 1) == -1)
            {
                if (errno == EAGAIN)
                {
                    rb_thread_sleep(1);
                    goto retry;
                }
                rb_sys_fail("semop()");
            }
        }
    }
#endif
}

#define GetMmap(obj, i_mm, t_modify)            \
    Data_Get_Struct(obj, mm_ipc, i_mm);         \
    if (!i_mm->t->path)                         \
    {                                           \
        rb_raise(rb_eIOError, "unmapped file"); \
    }                                           \
    if ((t_modify & MM_MODIFY))                 \
    {                                           \
        rb_check_frozen(obj);                   \
    }

static VALUE
mm_vunlock(VALUE obj)
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    mm_unlock(i_mm);
    return Qnil;
}

/*
 * call-seq: semlock
 *
 * Create a lock
 */
static VALUE
mm_semlock(int argc, VALUE *argv, VALUE obj)
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (!(i_mm->t->flag & MM_IPC))
    {
        rb_warning("useless use of #semlock");
        rb_yield(obj);
    }
    else
    {
#if HAVE_SEMCTL && HAVE_SHMCTL
        VALUE a;
        int wait_lock = Qtrue;

        if (rb_scan_args(argc, argv, "01", &a))
        {
            wait_lock = RTEST(a);
        }
        mm_lock(i_mm, wait_lock);
        rb_ensure(rb_yield, obj, mm_vunlock, obj);
#endif
    }
    return Qnil;
}

/*
 * call-seq: ipc_key
 *
 * Get the ipc key
 */
static VALUE
mm_ipc_key(VALUE obj)
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (i_mm->t->flag & MM_IPC)
    {
        return LONG2NUM(i_mm->t->key);
    }
    return INT2NUM(-1);
}

/*
 * Document-method: munmap
 * Document-method: unmap
 *
 * call-seq: munmap
 *
 * terminate the association
 */
static VALUE
mm_unmap(VALUE obj)
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (i_mm->t->path)
    {
        mm_lock(i_mm, Qtrue);
        munmap(i_mm->t->addr, i_mm->t->len);
        if (i_mm->t->path != (char *)-1)
        {
            if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE &&
                truncate(i_mm->t->path, i_mm->t->real) == -1)
            {
                rb_raise(rb_eTypeError, "truncate");
            }
            free(i_mm->t->path);
        }
        i_mm->t->path = NULL;
        mm_unlock(i_mm);
    }
    return Qnil;
}

static VALUE
mm_str(VALUE obj, int modify)
{
    mm_ipc *i_mm;
    VALUE ret = Qnil;

    GetMmap(obj, i_mm, modify & ~MM_ORIGIN);
    if (modify & MM_MODIFY)
    {
        rb_check_frozen(obj);
    }
    ret = rb_obj_alloc(rb_cString);
    RSTRING(ret)->as.heap.ptr = i_mm->t->addr;
    RSTRING(ret)->as.heap.aux.capa = i_mm->t->len;
    RSTRING(ret)->as.heap.len = i_mm->t->real;

    if (modify & MM_ORIGIN)
    {
#if HAVE_RB_DEFINE_ALLOC_FUNC
        RSTRING(ret)->as.heap.aux.shared = obj;
        FL_SET(ret, RSTRING_NOEMBED);
        FL_SET(ret, FL_USER18);
#else
        RSTRING(ret)->orig = ret;
#endif
    }
    if (RB_OBJ_FROZEN(obj))
    {
        ret = rb_obj_freeze(ret);
    }
    return ret;
}

/*
 * call-seq: to_str
 *
 * Convert object to a string
 */
static VALUE
mm_to_str(VALUE obj)
{
    return mm_str(obj, MM_ORIGIN);
}

typedef struct
{
    mm_ipc *i_mm;
    size_t len;
} mm_st;

static VALUE
mm_i_expand(VALUE arg)
{
    mm_st *st_mm = (mm_st *)arg;
    int fd;
    mm_ipc *i_mm = st_mm->i_mm;
    size_t len = st_mm->len;

    if (munmap(i_mm->t->addr, i_mm->t->len))
    {
        rb_raise(rb_eArgError, "munmap failed");
    }
    if ((fd = open(i_mm->t->path, i_mm->t->smode)) == -1)
    {
        rb_raise(rb_eArgError, "Can't open %s", i_mm->t->path);
    }
    if (len > i_mm->t->len)
    {
        if (lseek(fd, len - i_mm->t->len - 1, SEEK_END) == -1)
        {
            rb_raise(rb_eIOError, "Can't lseek %lu", len - i_mm->t->len - 1);
        }
        if (write(fd, "\000", 1) != 1)
        {
            rb_raise(rb_eIOError, "Can't extend %s", i_mm->t->path);
        }
    }
    else if (len < i_mm->t->len && truncate(i_mm->t->path, len) == -1)
    {
        rb_raise(rb_eIOError, "Can't truncate %s", i_mm->t->path);
    }
    i_mm->t->addr = mmap(0, len, i_mm->t->pmode, i_mm->t->vscope, fd, i_mm->t->offset);
    close(fd);
    if (i_mm->t->addr == MAP_FAILED)
    {
        rb_raise(rb_eArgError, "mmap failed");
    }
#ifdef MADV_NORMAL
    if (i_mm->t->advice && madvise(i_mm->t->addr, len, i_mm->t->advice) == -1)
    {
        rb_raise(rb_eArgError, "madvise(%d)", errno);
    }
#endif
    if ((i_mm->t->flag & MM_LOCK) && mlock(i_mm->t->addr, len) == -1)
    {
        rb_raise(rb_eArgError, "mlock(%d)", errno);
    }
    i_mm->t->len = len;
    return Qnil;
}

static void
mm_expandf(mm_ipc *i_mm, size_t len)
{
    int status;
    mm_st st_mm;

    if (i_mm->t->vscope == MAP_PRIVATE)
    {
        rb_raise(rb_eTypeError, "expand for a private map");
    }
    if (i_mm->t->flag & MM_FIXED)
    {
        rb_raise(rb_eTypeError, "expand for a fixed map");
    }
    if (!i_mm->t->path || i_mm->t->path == (char *)-1)
    {
        rb_raise(rb_eTypeError, "expand for an anonymous map");
    }
    st_mm.i_mm = i_mm;
    st_mm.len = len;
    if (i_mm->t->flag & MM_IPC)
    {
        mm_lock(i_mm, Qtrue);
        rb_protect(mm_i_expand, (VALUE)&st_mm, &status);
        mm_unlock(i_mm);
        if (status)
        {
            rb_jump_tag(status);
        }
    }
    else
    {
        mm_i_expand((VALUE)&st_mm);
    }
}

static void
mm_realloc(mm_ipc *i_mm, size_t len)
{
    if (len > i_mm->t->len)
    {
        if ((len - i_mm->t->len) < i_mm->t->incr)
        {
            len = i_mm->t->len + i_mm->t->incr;
        }
        mm_expandf(i_mm, len);
    }
}

/*
 * call-seq:
 *   extend(count)
 *
 * add <em>count</em> bytes to the file (i.e. pre-extend the file)
 */
static VALUE
mm_extend(VALUE obj, VALUE a)
{
    mm_ipc *i_mm;
    long len;

    GetMmap(obj, i_mm, MM_MODIFY);
    len = NUM2LONG(a);
    if (len > 0)
    {
        mm_expandf(i_mm, i_mm->t->len + len);
    }
    return ULONG2NUM(i_mm->t->len);
}

static VALUE mm_set_ipc(VALUE self, VALUE value)
{
    mm_ipc *i_mm;
    Data_Get_Struct(self, mm_ipc, i_mm);

#if HAVE_SEMCTL && HAVE_SHMCTL
    if (value != Qtrue && TYPE(value) != T_HASH)
    {
        rb_raise(rb_eArgError, "Expected an Hash for :ipc");
    }
    i_mm->t->shmid = NUM2INT(value);
    i_mm->t->flag |= (MM_IPC | MM_TMP);
#endif

    return self;
}

static VALUE mm_set_increment(VALUE self, VALUE value)
{
    mm_ipc *i_mm;
    Data_Get_Struct(self, mm_ipc, i_mm);

    int incr = NUM2INT(value);
    if (incr < 0)
    {
        rb_raise(rb_eArgError, "Invalid value for increment %d", incr);
    }
    i_mm->t->incr = incr;

    return self;
}

static VALUE mm_set_advice(VALUE self, VALUE value)
{
    mm_ipc *i_mm;
    Data_Get_Struct(self, mm_ipc, i_mm);

    i_mm->t->advice = NUM2INT(value);

    return self;
}

static VALUE mm_set_offset(VALUE self, VALUE value)
{
    mm_ipc *i_mm;
    Data_Get_Struct(self, mm_ipc, i_mm);

    i_mm->t->offset = NUM2INT(value);
    if (i_mm->t->offset < 0)
    {
        rb_raise(rb_eArgError, "Invalid value for offset %lld", i_mm->t->offset);
    }
    i_mm->t->flag |= MM_FIXED;

    return self;
}

static VALUE mm_set_length(VALUE self, VALUE value)
{
    mm_ipc *i_mm;
    Data_Get_Struct(self, mm_ipc, i_mm);

    i_mm->t->len = NUM2UINT(value);
    if (i_mm->t->len <= 0)
    {
        rb_raise(rb_eArgError, "Invalid value for length %zu", i_mm->t->len);
    }
    i_mm->t->flag |= MM_FIXED;

    return self;
}

#if HAVE_SEMCTL && HAVE_SHMCTL

static VALUE
mm_i_ipc(VALUE arg, VALUE obj, int argc, const VALUE *argv, VALUE unused)
{
    mm_ipc *i_mm;
    char *options;
    VALUE key, value;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    key = rb_ary_entry(arg, 0);
    value = rb_ary_entry(arg, 1);
    key = rb_obj_as_string(key);
    options = StringValuePtr(key);
    if (strcmp(options, "key") == 0)
    {
        i_mm->t->key = rb_funcall2(value, rb_intern("to_int"), 0, 0);
    }
    else if (strcmp(options, "permanent") == 0)
    {
        if (RTEST(value))
        {
            i_mm->t->flag &= ~MM_TMP;
        }
    }
    else if (strcmp(options, "mode") == 0)
    {
        i_mm->t->semid = NUM2INT(value);
    }
    else
    {
        rb_warning("Unknown option `%s'", options);
    }
    return Qnil;
}

#endif

/*
 * call-seq:
 *  new(file, mode = "r", protection = Mmap::MAP_SHARED, options = {})
 *
 * create a new Mmap object
 *
 * * <em>file</em>
 *
 *   Pathname of the file, if <em>nil</em> is given an anonymous map
 *   is created <em>Mmanp::MAP_ANON</em>
 *
 * * <em>mode</em>
 *
 *   Mode to open the file, it can be "r", "w", "rw", "a"
 *
 * * <em>protection</em>
 *
 *   specify the nature of the mapping
 *
 *   * <em>Mmap::MAP_SHARED</em>
 *
 *     Creates a mapping that's shared with all other processes
 *     mapping the same areas of the file.
 *     The default value is <em>Mmap::MAP_SHARED</em>
 *
 *   * <em>Mmap::MAP_PRIVATE</em>
 *
 *     Creates a private copy-on-write mapping, so changes to the
 *     contents of the mmap object will be private to this process
 *
 * * <em>options</em>
 *
 *   Hash. If one of the options <em>length</em> or <em>offset</em>
 *   is specified it will not possible to modify the size of
 *   the mapped file.
 *
 *   length:: maps <em>length</em> bytes from the file
 *
 *   offset:: the mapping begin at <em>offset</em>
 *
 *   advice:: the type of the access (see #madvise)
 */

static VALUE
mm_s_alloc(VALUE obj)
{
    VALUE res;
    mm_ipc *i_mm;

    res = Data_Make_Struct(obj, mm_ipc, 0, mm_free, i_mm);
    i_mm->t = ALLOC_N(mm_mmap, 1);
    MEMZERO(i_mm->t, mm_mmap, 1);
    i_mm->t->incr = EXP_INCR_SIZE;
    return res;
}

/*
 * call-seq: initialize
 *
 * Create a new Mmap object
 */
static VALUE
mm_init(int argc, VALUE *argv, VALUE obj)
{
    struct stat st;
    int fd, smode = 0, pmode = 0, vscope, perm, init;
    MMAP_RETTYPE addr;
    VALUE fname, fdv, vmode, scope, options;
    mm_ipc *i_mm;
    char *path, *mode;
    size_t size = 0;
    off_t offset;
    int anonymous;

    options = Qnil;
    if (argc > 1 && TYPE(argv[argc - 1]) == T_HASH)
    {
        options = argv[argc - 1];
        argc--;
    }
    rb_scan_args(argc, argv, "12", &fname, &vmode, &scope);
    vscope = 0;
    path = 0;
    fd = -1;
    anonymous = 0;
    fdv = Qnil;
#ifdef MAP_ANON
    if (NIL_P(fname))
    {
        vscope = MAP_ANON | MAP_SHARED;
        anonymous = 1;
    }
    else
#endif
    {
        if (rb_respond_to(fname, rb_intern("fileno")))
        {
            fdv = rb_funcall2(fname, rb_intern("fileno"), 0, 0);
        }
        if (NIL_P(fdv))
        {
            fname = rb_str_to_str(fname);
            SafeStringValue(fname);
            path = StringValuePtr(fname);
        }
        else
        {
            fd = NUM2INT(fdv);
            if (fd < 0)
            {
                rb_raise(rb_eArgError, "invalid file descriptor %d", fd);
            }
        }
        if (!NIL_P(scope))
        {
            vscope = NUM2INT(scope);
#ifdef MAP_ANON
            if (vscope & MAP_ANON)
            {
                rb_raise(rb_eArgError, "filename specified for an anonymous map");
            }
#endif
        }
    }
    vscope |= NIL_P(scope) ? MAP_SHARED : NUM2INT(scope);
    size = 0;
    perm = 0666;
    if (!anonymous)
    {
        if (NIL_P(vmode))
        {
            mode = (char *)"r";
        }
        else if (rb_respond_to(vmode, rb_intern("to_ary")))
        {
            VALUE tmp;

            vmode = rb_convert_type(vmode, T_ARRAY, "Array", "to_ary");
            if (RARRAY_LEN(vmode) != 2)
            {
                rb_raise(rb_eArgError, "Invalid length %ld (expected 2)",
                         RARRAY_LEN(vmode));
            }
            tmp = rb_ary_entry(vmode, 0);
            mode = StringValuePtr(tmp);
            perm = NUM2INT(rb_ary_entry(vmode, 1));
        }
        else
        {
            mode = StringValuePtr(vmode);
        }
        if (strcmp(mode, "r") == 0)
        {
            smode = O_RDONLY;
            pmode = PROT_READ;
        }
        else if (strcmp(mode, "w") == 0)
        {
            smode = O_RDWR | O_TRUNC;
            pmode = PROT_READ | PROT_WRITE;
        }
        else if (strcmp(mode, "rw") == 0 || strcmp(mode, "wr") == 0)
        {
            smode = O_RDWR;
            pmode = PROT_READ | PROT_WRITE;
        }
        else if (strcmp(mode, "a") == 0)
        {
            smode = O_RDWR | O_CREAT;
            pmode = PROT_READ | PROT_WRITE;
        }
        else
        {
            rb_raise(rb_eArgError, "Invalid mode %s", mode);
        }
        if (NIL_P(fdv))
        {
            if ((fd = open(path, smode, perm)) == -1)
            {
                rb_raise(rb_eArgError, "Can't open %s", path);
            }
        }
        if (fstat(fd, &st) == -1)
        {
            rb_raise(rb_eArgError, "Can't stat %s", path);
        }
        size = st.st_size;
    }
    else
    {
        fd = -1;
        if (!NIL_P(vmode) && TYPE(vmode) != T_STRING)
        {
            size = NUM2INT(vmode);
        }
    }
    Data_Get_Struct(obj, mm_ipc, i_mm);
    rb_check_frozen(obj);
    i_mm->t->shmid = 0;
    i_mm->t->semid = 0;
    offset = 0;
    if (options != Qnil)
    {
        rb_funcall(obj, rb_intern("process_options"), 1, options);
        if (path && ((off_t)i_mm->t->len + i_mm->t->offset) > st.st_size)
        {
            rb_raise(rb_eArgError, "invalid value for length (%ld) or offset (%lld)",
                     i_mm->t->len, i_mm->t->offset);
        }
        if (i_mm->t->len)
            size = i_mm->t->len;
        offset = i_mm->t->offset;
#if HAVE_SEMCTL && HAVE_SHMCTL
        if (i_mm->t->flag & MM_IPC)
        {
            key_t key;
            int shmid, semid, mode;
            union semun sem_val;
            struct shmid_ds buf;
            mm_mmap *data;

            if (!(vscope & MAP_SHARED))
            {
                rb_warning("Probably it will not do what you expect ...");
            }
            i_mm->t->key = -1;
            i_mm->t->semid = 0;
            if (TYPE(i_mm->t->shmid) == T_HASH)
            {
                // rb_iterate(rb_each, i_mm->t->shmid, mm_i_ipc, obj);
                rb_block_call(i_mm->t->shmid, rb_intern("each"), 0, NULL, mm_i_ipc, obj);
            }
            i_mm->t->shmid = 0;
            if (i_mm->t->semid)
            {
                mode = i_mm->t->semid;
                i_mm->t->semid = 0;
            }
            else
            {
                mode = 0644;
            }
            if ((int)i_mm->t->key <= 0)
            {
                mode |= IPC_CREAT;
                strcpy(template, "/tmp/ruby_mmap.XXXXXX");
                if (mkstemp(template) == -1)
                {
                    rb_sys_fail("mkstemp()");
                }
                if ((key = ftok(template, 'R')) == -1)
                {
                    rb_sys_fail("ftok()");
                }
            }
            else
            {
                key = (key_t)i_mm->t->key;
            }
            if ((shmid = shmget(key, sizeof(mm_ipc), mode)) == -1)
            {
                rb_sys_fail("shmget()");
            }
            data = shmat(shmid, (void *)0, 0);
            if (data == (mm_mmap *)-1)
            {
                rb_sys_fail("shmat()");
            }
            if (i_mm->t->flag & MM_TMP)
            {
                if (shmctl(shmid, IPC_RMID, &buf) == -1)
                {
                    rb_sys_fail("shmctl()");
                }
            }
            if ((semid = semget(key, 1, mode)) == -1)
            {
                rb_sys_fail("semget()");
            }
            if (mode & IPC_CREAT)
            {
                sem_val.val = 1;
                if (semctl(semid, 0, SETVAL, sem_val) == -1)
                {
                    rb_sys_fail("semctl()");
                }
            }
            memcpy(data, i_mm->t, sizeof(mm_mmap));
            free(i_mm->t);
            i_mm->t = data;
            i_mm->t->key = key;
            i_mm->t->semid = semid;
            i_mm->t->shmid = shmid;
            if (i_mm->t->flag & MM_TMP)
            {
                i_mm->t->template = ALLOC_N(char, strlen(template) + 1);
                strcpy(i_mm->t->template, template);
            }
        }
#endif
    }
    init = 0;
    if (anonymous)
    {
        if (size <= 0)
        {
            rb_raise(rb_eArgError, "length not specified for an anonymous map");
        }
        if (offset)
        {
            rb_warning("Ignoring offset for an anonymous map");
            offset = 0;
        }
        smode = O_RDWR;
        pmode = PROT_READ | PROT_WRITE;
        i_mm->t->flag |= MM_FIXED | MM_ANON;
    }
    else
    {
        if (size == 0 && (smode & O_RDWR))
        {
            if (lseek(fd, i_mm->t->incr - 1, SEEK_END) == -1)
            {
                rb_raise(rb_eIOError, "Can't lseek %lu", i_mm->t->incr - 1);
            }
            if (write(fd, "\000", 1) != 1)
            {
                rb_raise(rb_eIOError, "Can't extend %s", path);
            }
            init = 1;
            size = i_mm->t->incr;
        }
        if (!NIL_P(fdv))
        {
            i_mm->t->flag |= MM_FIXED;
        }
    }
    addr = mmap(0, size, pmode, vscope, fd, offset);
    if (NIL_P(fdv) && !anonymous)
    {
        close(fd);
    }
    if (addr == MAP_FAILED || !addr)
    {
        rb_raise(rb_eArgError, "mmap failed (%d)", errno);
    }
#ifdef MADV_NORMAL
    if (i_mm->t->advice && madvise(addr, size, i_mm->t->advice) == -1)
    {
        rb_raise(rb_eArgError, "madvise(%d)", errno);
    }
#endif
    if (anonymous && TYPE(options) == T_HASH)
    {
        VALUE val;
        char *ptr;

        val = rb_hash_aref(options, rb_str_new2("initialize"));
        if (!NIL_P(val))
        {
            ptr = StringValuePtr(val);
            memset(addr, ptr[0], size);
        }
    }
    i_mm->t->addr = addr;
    i_mm->t->len = size;
    if (!init)
        i_mm->t->real = size;
    i_mm->t->pmode = pmode;
    i_mm->t->vscope = vscope;
    i_mm->t->smode = smode & ~O_TRUNC;
    i_mm->t->path = (path) ? ruby_strdup(path) : (char *)-1;
    if (smode == O_RDONLY)
    {
        obj = rb_obj_freeze(obj);
    }
    else
    {
        if (smode == O_WRONLY)
        {
            i_mm->t->flag |= MM_FIXED;
        }
    }
    return obj;
}

/*
 * Document-method: msync
 * Document-method: sync
 * Document-method: flush
 *
 * call-seq: msync
 *
 * flush the file
 */
static VALUE
mm_msync(int argc, VALUE *argv, VALUE obj)
{
    mm_ipc *i_mm;
    VALUE oflag;
    int ret;
    int flag = MS_SYNC;

    if (argc)
    {
        rb_scan_args(argc, argv, "01", &oflag);
        flag = NUM2INT(oflag);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    if ((ret = msync(i_mm->t->addr, i_mm->t->len, flag)) != 0)
    {
        rb_raise(rb_eArgError, "msync(%d)", ret);
    }
    if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE)
        mm_expandf(i_mm, i_mm->t->real);
    return obj;
}

/*
 * Document-method: mprotect
 * Document-method: protect
 *
 * call-seq: mprotect(mode)
 *
 * change the mode, value must be "r", "w" or "rw"
 */
static VALUE
mm_mprotect(VALUE obj, VALUE a)
{
    mm_ipc *i_mm;
    int ret, pmode;
    char *smode;

    GetMmap(obj, i_mm, 0);
    if (TYPE(a) == T_STRING)
    {
        smode = StringValuePtr(a);
        if (strcmp(smode, "r") == 0)
            pmode = PROT_READ;
        else if (strcmp(smode, "w") == 0)
            pmode = PROT_WRITE;
        else if (strcmp(smode, "rw") == 0 || strcmp(smode, "wr") == 0)
            pmode = PROT_READ | PROT_WRITE;
        else
        {
            rb_raise(rb_eArgError, "Invalid mode %s", smode);
        }
    }
    else
    {
        pmode = NUM2INT(a);
    }
    if ((pmode & PROT_WRITE) && RB_OBJ_FROZEN(obj))
        rb_check_frozen(obj);
    if ((ret = mprotect(i_mm->t->addr, i_mm->t->len, pmode | PROT_READ)) != 0)
    {
        rb_raise(rb_eArgError, "mprotect(%d)", ret);
    }
    i_mm->t->pmode = pmode;
    if (pmode & PROT_READ)
    {
        if (pmode & PROT_WRITE)
            i_mm->t->smode = O_RDWR;
        else
        {
            i_mm->t->smode = O_RDONLY;
            obj = rb_obj_freeze(obj);
        }
    }
    else if (pmode & PROT_WRITE)
    {
        i_mm->t->flag |= MM_FIXED;
        i_mm->t->smode = O_WRONLY;
    }
    return obj;
}

#ifdef MADV_NORMAL
/*
 * Document-method: madvise
 * Document-method: advise
 *
 * call-seq: madvise(advice)
 *
 * <em>advice</em> can have the value <em>Mmap::MADV_NORMAL</em>,
 * <em>Mmap::MADV_RANDOM</em>, <em>Mmap::MADV_SEQUENTIAL</em>,
 * <em>Mmap::MADV_WILLNEED</em>, <em>Mmap::MADV_DONTNEED</em>
 *
 */
static VALUE
mm_madvise(VALUE obj, VALUE a)
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (madvise(i_mm->t->addr, i_mm->t->len, NUM2INT(a)) == -1)
    {
        rb_raise(rb_eTypeError, "madvise(%d)", errno);
    }
    i_mm->t->advice = NUM2INT(a);
    return Qnil;
}
#endif

#define StringMmap(b, bp, bl)                                                \
    do                                                                       \
    {                                                                        \
        if (TYPE(b) == T_DATA && RDATA(b)->dfree == (RUBY_DATA_FUNC)mm_free) \
        {                                                                    \
            mm_ipc *b_mm;                                                    \
            GetMmap(b, b_mm, 0);                                             \
            bp = b_mm->t->addr;                                              \
            bl = b_mm->t->real;                                              \
        }                                                                    \
        else                                                                 \
        {                                                                    \
            bp = StringValuePtr(b);                                          \
            bl = RSTRING_LEN(b);                                             \
        }                                                                    \
    } while (0);

static void
mm_update(mm_ipc *str, long beg, long len, VALUE val)
{
    char *valp;
    long vall;

    if (len < 0)
        rb_raise(rb_eIndexError, "negative length %ld", len);
    mm_lock(str, Qtrue);
    if (beg < 0)
    {
        beg += str->t->real;
    }
    if (beg < 0 || str->t->real < (size_t)beg)
    {
        if (beg < 0)
        {
            beg -= str->t->real;
        }
        mm_unlock(str);
        rb_raise(rb_eIndexError, "index %ld out of string", beg);
    }
    if (str->t->real < (size_t)(beg + len))
    {
        len = str->t->real - beg;
    }

    mm_unlock(str);
    StringMmap(val, valp, vall);
    mm_lock(str, Qtrue);

    if ((str->t->flag & MM_FIXED) && vall != len)
    {
        mm_unlock(str);
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    if (len < vall)
    {
        mm_realloc(str, str->t->real + vall - len);
    }

    if (vall != len)
    {
        memmove((char *)str->t->addr + beg + vall,
                (char *)str->t->addr + beg + len,
                str->t->real - (beg + len));
    }
    if (str->t->real < (size_t)beg && len < 0)
    {
        MEMZERO((char *)str->t->addr + str->t->real, char, -len);
    }
    if (vall > 0)
    {
        memmove((char *)str->t->addr + beg, valp, vall);
    }
    str->t->real += vall - len;
    mm_unlock(str);
}

/*
 * call-seq: =~(other)
 *
 * return an index of the match
 */
static VALUE
mm_match(VALUE x, VALUE y)
{
    VALUE reg, res;
    long start;

    x = mm_str(x, MM_ORIGIN);
    if (TYPE(y) == T_DATA && RDATA(y)->dfree == (RUBY_DATA_FUNC)mm_free)
    {
        y = mm_to_str(y);
    }
    switch (TYPE(y))
    {
    case T_REGEXP:
        res = rb_reg_match(y, x);
        break;

    case T_STRING:
        reg = rb_reg_regcomp(y);
        start = rb_reg_search(reg, x, 0, 0);
        if (start == -1)
            res = Qnil;
        else
            res = LONG2NUM(start);
        break;

    default:
        res = rb_funcall(y, rb_intern("=~"), 1, x);
        break;
    }
    return res;
}

static VALUE
get_pat(VALUE pat)
{
    switch (TYPE(pat))
    {
    case T_REGEXP:
        break;

    case T_STRING:
        pat = rb_reg_regcomp(pat);
        break;

    default:
        /* type failed */
        Check_Type(pat, T_REGEXP);
    }
    return pat;
}

static long
mm_correct_backref()
{
    VALUE match;
    long i, start;

    match = rb_backref_get();
    if (NIL_P(match))
        return 0;
    if (BEG(match, 0) == -1)
        return 0;
    start = BEG(match, 0);
    RMATCH(match)->str = rb_str_new(StringValuePtr(RMATCH(match)->str) + start,
                                    END(match, 0) - start);
    for (i = 0; i < RMATCH_REGS(match)->num_regs && BEG(match, i) != -1; i++)
    {
        BEG(match, i) -= start;
        END(match, i) -= start;
    }
    rb_backref_set(match);
    return start;
}

static VALUE
mm_sub_bang_int(VALUE arg)
{
    mm_bang *bang_st = (mm_bang *)arg;
    int argc = bang_st->argc;
    VALUE *argv = bang_st->argv;
    VALUE obj = bang_st->obj;
    VALUE pat, repl = Qnil, match, str, res;
    struct re_registers *regs;
    long start, iter = 0;
    long plen;
    mm_ipc *i_mm;

    if (argc == 1 && rb_block_given_p())
    {
        iter = 1;
    }
    else if (argc == 2)
    {
        repl = rb_str_to_str(argv[1]);
    }
    else
    {
        rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);

    pat = get_pat(argv[0]);
    res = Qnil;
    if (rb_reg_search(pat, str, 0, 0) >= 0)
    {
        start = mm_correct_backref();
        match = rb_backref_get();
        regs = RMATCH_REGS(match);
        if (iter)
        {
            rb_match_busy(match);
            repl = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
            rb_backref_set(match);
        }
        else
        {
            RSTRING(str)->as.heap.ptr += start;
            repl = rb_reg_regsub(repl, str, regs, match);
            RSTRING(str)->as.heap.ptr -= start;
        }
        plen = END(match, 0) - BEG(match, 0);
        if (RSTRING_LEN(repl) > plen)
        {
            mm_realloc(i_mm, RSTRING_LEN(str) + RSTRING_LEN(repl) - plen);
            RSTRING(str)->as.heap.ptr = i_mm->t->addr;
        }
        if (RSTRING_LEN(repl) != plen)
        {
            if (i_mm->t->flag & MM_FIXED)
            {
                rb_raise(rb_eTypeError, "try to change the size of a fixed map");
            }
            memmove(RSTRING_PTR(str) + start + BEG(match, 0) + RSTRING_LEN(repl),
                    RSTRING_PTR(str) + start + BEG(match, 0) + plen,
                    RSTRING_LEN(str) - start - BEG(match, 0) - plen);
        }
        memcpy(RSTRING_PTR(str) + start + BEG(match, 0),
               RSTRING_PTR(repl), RSTRING_LEN(repl));
        i_mm->t->real += RSTRING_LEN(repl) - plen;

        res = obj;
    }
    return res;
}

/*
 * call-seq:
 *    str.sub!(pattern, replacement)      => str or nil
 *    str.sub!(pattern) {|match| block }  => str or nil
 *
 * substitution
 */
static VALUE
mm_sub_bang(int argc, VALUE *argv, VALUE obj)
{
    VALUE res;
    mm_bang bang_st;
    mm_ipc *i_mm;

    bang_st.argc = argc;
    bang_st.argv = argv;
    bang_st.obj = obj;
    GetMmap(obj, i_mm, MM_MODIFY);
    if (i_mm->t->flag & MM_IPC)
    {
        mm_lock(i_mm, Qtrue);
        res = rb_ensure(mm_sub_bang_int, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else
    {
        res = mm_sub_bang_int((VALUE)&bang_st);
    }
    return res;
}

static VALUE
mm_gsub_bang_int(VALUE arg)
{
    mm_bang *bang_st = (mm_bang *)arg;
    int argc = bang_st->argc;
    VALUE *argv = bang_st->argv;
    VALUE obj = bang_st->obj;
    VALUE pat, val, repl = Qnil, match, str;
    struct re_registers *regs;
    long beg, offset;
    long start, iter = 0;
    long plen;
    mm_ipc *i_mm;

    if (argc == 1 && rb_block_given_p())
    {
        iter = 1;
    }
    else if (argc == 2)
    {
        repl = rb_str_to_str(argv[1]);
    }
    else
    {
        rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);

    pat = get_pat(argv[0]);
    offset = 0;
    beg = rb_reg_search(pat, str, 0, 0);
    if (beg < 0)
    {
        return Qnil;
    }
    while (beg >= 0)
    {
        start = mm_correct_backref();
        match = rb_backref_get();
        regs = RMATCH_REGS(match);
        if (iter)
        {
            rb_match_busy(match);
            val = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
            rb_backref_set(match);
        }
        else
        {
            RSTRING(str)->as.heap.ptr += start;
            val = rb_reg_regsub(repl, str, regs, match);
            RSTRING(str)->as.heap.ptr -= start;
        }
        plen = END(match, 0) - BEG(match, 0);
        if ((i_mm->t->real + RSTRING_LEN(val) - plen) > i_mm->t->len)
        {
            mm_realloc(i_mm, RSTRING_LEN(str) + RSTRING_LEN(val) - plen);
        }
        if (RSTRING_LEN(val) != plen)
        {
            if (i_mm->t->flag & MM_FIXED)
            {
                rb_raise(rb_eTypeError, "try to change the size of a fixed map");
            }
            memmove(RSTRING_PTR(str) + start + BEG(match, 0) + RSTRING_LEN(val),
                    RSTRING_PTR(str) + start + BEG(match, 0) + plen,
                    RSTRING_LEN(str) - start - BEG(match, 0) - plen);
        }
        memcpy(RSTRING_PTR(str) + start + BEG(match, 0),
               RSTRING_PTR(val), RSTRING_LEN(val));
        RSTRING(str)->as.heap.len += RSTRING_LEN(val) - plen;

        i_mm->t->real = RSTRING_LEN(str);
        if (BEG(match, 0) == END(match, 0))
        {
            long end_pos = END(match, 0);
            offset = start + end_pos + rb_enc_mbclen(RSTRING_PTR(str) + end_pos, RSTRING_END(str), rb_enc_get(str));
            offset += RSTRING_LEN(val) - plen;
        }
        else
        {
            offset = start + END(match, 0) + RSTRING_LEN(val) - plen;
        }
        if (offset > RSTRING_LEN(str))
            break;
        beg = rb_reg_search(pat, str, offset, 0);
    }
    rb_backref_set(match);
    return obj;
}

/*
 * call-seq:
 *    str.gsub!(pattern, replacement)        => str or nil
 *    str.gsub!(pattern) {|match| block }    => str or nil
 *
 * global substitution
 */
static VALUE
mm_gsub_bang(int argc, VALUE *argv, VALUE obj)
{
    VALUE res;
    mm_bang bang_st;
    mm_ipc *i_mm;

    bang_st.argc = argc;
    bang_st.argv = argv;
    bang_st.obj = obj;
    GetMmap(obj, i_mm, MM_MODIFY);
    if (i_mm->t->flag & MM_IPC)
    {
        mm_lock(i_mm, Qtrue);
        res = rb_ensure(mm_gsub_bang_int, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else
    {
        res = mm_gsub_bang_int((VALUE)&bang_st);
    }
    return res;
}

static VALUE mm_index __((int, VALUE *, VALUE));

#if HAVE_RB_DEFINE_ALLOC_FUNC

static void mm_subpat_set(VALUE obj, VALUE re, int offset, VALUE val)
{
    VALUE str, match;
    long start, end, len;
    mm_ipc *i_mm;

    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);
    if (rb_reg_search(re, str, 0, 0) < 0)
    {
        rb_raise(rb_eIndexError, "regexp not matched");
    }
    match = rb_backref_get();
    if (offset >= RMATCH_REGS(match)->num_regs)
    {
        rb_raise(rb_eIndexError, "index %d out of regexp", offset);
    }

    start = BEG(match, offset);
    if (start == -1)
    {
        rb_raise(rb_eIndexError, "regexp group %d not matched", offset);
    }
    end = END(match, offset);
    len = end - start;
    GetMmap(obj, i_mm, MM_MODIFY);
    mm_update(i_mm, start, len, val);
}

#endif

static VALUE
mm_aset(VALUE str, VALUE indx, VALUE val)
{
    long idx;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    switch (TYPE(indx))
    {
    case T_FIXNUM:
    num_index:
        idx = NUM2INT(indx);
        if (idx < 0)
        {
            idx += i_mm->t->real;
        }
        if (idx < 0 || i_mm->t->real <= (size_t)idx)
        {
            rb_raise(rb_eIndexError, "index %ld out of string", idx);
        }
        if (FIXNUM_P(val))
        {
            if (i_mm->t->real == (size_t)idx)
            {
                i_mm->t->real += 1;
                mm_realloc(i_mm, i_mm->t->real);
            }
            ((char *)i_mm->t->addr)[idx] = NUM2INT(val) & 0xff;
        }
        else
        {
            mm_update(i_mm, idx, 1, val);
        }
        return val;

    case T_REGEXP:
#if HAVE_RB_DEFINE_ALLOC_FUNC
        mm_subpat_set(str, indx, 0, val);
#else
    {
        VALUE args[2];
        args[0] = indx;
        args[1] = val;
        mm_sub_bang(2, args, str);
    }
#endif
        return val;

    case T_STRING:
    {
        VALUE res;

        res = mm_index(1, &indx, str);
        if (!NIL_P(res))
        {
            mm_update(i_mm, NUM2LONG(res), RSTRING_LEN(indx), val);
        }
        return val;
    }

    default:
        /* check if indx is Range */
        {
            long beg, len;
            if (rb_range_beg_len(indx, &beg, &len, i_mm->t->real, 2))
            {
                mm_update(i_mm, beg, len, val);
                return val;
            }
        }
        idx = NUM2LONG(indx);
        goto num_index;
    }
}

/*
 * call-seq: []=(args)
 *
 * Element assignement - with the following syntax
 *
 *   self[nth] = val
 *
 * change the <em>nth</em> character with <em>val</em>
 *
 *   self[start..last] = val
 *
 * change substring from <em>start</em> to <em>last</em> with <em>val</em>
 *
 *   self[start, len] = val
 *
 * replace <em>length</em> characters from <em>start</em> with <em>val</em>.
 *
 */
static VALUE
mm_aset_m(int argc, VALUE *argv, VALUE str)
{
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    if (argc == 3)
    {
        long beg, len;

#if HAVE_RB_DEFINE_ALLOC_FUNC
        if (TYPE(argv[0]) == T_REGEXP)
        {
            mm_subpat_set(str, argv[0], NUM2INT(argv[1]), argv[2]);
        }
        else
#endif
        {
            beg = NUM2INT(argv[0]);
            len = NUM2INT(argv[1]);
            mm_update(i_mm, beg, len, argv[2]);
        }
        return argv[2];
    }
    if (argc != 2)
    {
        rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    return mm_aset(str, argv[0], argv[1]);
}

#if HAVE_RB_STR_INSERT

/*
 * call-seq: insert(index, str)
 *
 * insert <em>str</em> at <em>index</em>
 */
static VALUE
mm_insert(VALUE str, VALUE idx, VALUE str2)
{
    mm_ipc *i_mm;
    long pos = NUM2LONG(idx);

    GetMmap(str, i_mm, MM_MODIFY);
    if (pos == -1)
    {
        pos = RSTRING_LEN(str);
    }
    else if (pos < 0)
    {
        pos++;
    }
    mm_update(i_mm, pos, 0, str2);
    return str;
}

#endif

static VALUE mm_aref_m _((int, VALUE *, VALUE));

/*
 * call-seq: slice!(str)
 *
 * delete the specified portion of the file
 */
static VALUE
mm_slice_bang(int argc, VALUE *argv, VALUE str)
{
    VALUE result;
    VALUE buf[3];
    int i;

    if (argc < 1 || 2 < argc)
    {
        rb_raise(rb_eArgError, "wrong # of arguments(%d for 1)", argc);
    }
    for (i = 0; i < argc; i++)
    {
        buf[i] = argv[i];
    }
    buf[i] = rb_str_new(0, 0);
    result = mm_aref_m(argc, buf, str);
    if (!NIL_P(result))
    {
        mm_aset_m(argc + 1, buf, str);
    }
    return result;
}

static VALUE
mm_cat(VALUE str, const char *ptr, long len)
{
    mm_ipc *i_mm;
    char *sptr;

    GetMmap(str, i_mm, MM_MODIFY);
    if (len > 0)
    {
        long poffset = -1;
        sptr = (char *)i_mm->t->addr;

        if (sptr <= ptr &&
            ptr < sptr + i_mm->t->real)
        {
            poffset = ptr - sptr;
        }
        mm_lock(i_mm, Qtrue);
        mm_realloc(i_mm, i_mm->t->real + len);
        sptr = (char *)i_mm->t->addr;
        if (ptr)
        {
            if (poffset >= 0)
                ptr = sptr + poffset;
            memcpy(sptr + i_mm->t->real, ptr, len);
        }
        i_mm->t->real += len;
        mm_unlock(i_mm);
    }
    return str;
}

static VALUE
mm_append(VALUE str1, VALUE str2)
{
    str2 = rb_str_to_str(str2);
    str1 = mm_cat(str1, StringValuePtr(str2), RSTRING_LEN(str2));
    return str1;
}

/*
 * Document-method: concat
 * Document-method: <<
 *
 * call-seq: concat(other)
 *
 * append the contents of <em>other</em>
 */
static VALUE
mm_concat(VALUE str1, VALUE str2)
{
    if (FIXNUM_P(str2))
    {
        int i = FIX2INT(str2);
        if (0 <= i && i <= 0xff)
        { /* byte */
            char c = i;
            return mm_cat(str1, &c, 1);
        }
    }
    str1 = mm_append(str1, str2);
    return str1;
}

#ifndef HAVE_RB_STR_LSTRIP

/*
 * call-seq: strip!
 *
 * removes leading and trailing whitespace
 */
static VALUE
mm_strip_bang(VALUE str)
{
    char *s, *t, *e;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    e = t = s + i_mm->t->real;
    while (s < t && ISSPACE(*s))
        s++;
    t--;
    while (s <= t && ISSPACE(*t))
        t--;
    t++;

    if (i_mm->t->real != (t - s) && (i_mm->t->flag & MM_FIXED))
    {
        mm_unlock(i_mm);
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t - s;
    if (s > (char *)i_mm->t->addr)
    {
        memmove(i_mm->t->addr, s, i_mm->t->real);
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
    }
    else if (t < e)
    {
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
    }
    else
    {
        str = Qnil;
    }
    mm_unlock(i_mm);
    return str;
}

#else

/*
 * call-seq: lstrip!
 *
 * removes leading whitespace
 */
static VALUE
mm_lstrip_bang(VALUE str)
{
    char *s, *t;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    t = s + i_mm->t->real;

    while (s < t && ISSPACE(*s))
        s++;

    if (i_mm->t->real != (size_t)(t - s) && (i_mm->t->flag & MM_FIXED))
    {
        mm_unlock(i_mm);
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t - s;
    if (s > (char *)i_mm->t->addr)
    {
        memmove(i_mm->t->addr, s, i_mm->t->real);
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
        mm_unlock(i_mm);
        return str;
    }
    mm_unlock(i_mm);
    return Qnil;
}

/*
 * call-seq: rstrip!
 *
 * removes trailing whitespace
 */
static VALUE
mm_rstrip_bang(VALUE str)
{
    char *s, *t, *e;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    e = t = s + i_mm->t->real;
    t--;
    while (s <= t && ISSPACE(*t))
        t--;
    t++;
    if (i_mm->t->real != (size_t)(t - s) && (i_mm->t->flag & MM_FIXED))
    {
        mm_unlock(i_mm);
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t - s;
    if (t < e)
    {
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
        mm_unlock(i_mm);
        return str;
    }
    mm_unlock(i_mm);
    return Qnil;
}

static VALUE
mm_strip_bang(VALUE str)
{
    VALUE l = mm_lstrip_bang(str);
    VALUE r = mm_rstrip_bang(str);

    if (NIL_P(l) && NIL_P(r))
        return Qnil;
    return str;
}

#endif

#define MmapStr(b)                                                           \
    do                                                                       \
    {                                                                        \
        if (TYPE(b) == T_DATA && RDATA(b)->dfree == (RUBY_DATA_FUNC)mm_free) \
        {                                                                    \
            b = mm_str(b, MM_ORIGIN);                                        \
        }                                                                    \
        else                                                                 \
        {                                                                    \
            b = rb_str_to_str(b);                                            \
        }                                                                    \
    } while (0);

/*
 * call-seq: <=>(other)
 *
 * comparison : return -1, 0, 1
 */
static VALUE
mm_cmp(VALUE a, VALUE b)
{
    int result;

    a = mm_str(a, MM_ORIGIN);
    MmapStr(b);
    result = rb_str_cmp(a, b);
    return INT2FIX(result);
}

#if HAVE_RB_STR_CASECMP

/*
 * call-seq: casecmp(other)
 *
 * only with ruby >= 1.7.1
 */
static VALUE
mm_casecmp(VALUE a, VALUE b)
{
    VALUE result;

    a = mm_str(a, MM_ORIGIN);
    MmapStr(b);
    result = rb_funcall2(a, rb_intern("casecmp"), 1, &b);
    return result;
}

#endif

/*
 * Document-method: ==
 * Document-method: ===
 *
 * call-seq: ==
 *
 * comparison
 */
static VALUE
mm_equal(VALUE a, VALUE b)
{
    VALUE result;
    mm_ipc *i_mm, *u_mm;

    if (a == b)
        return Qtrue;
    if (TYPE(b) != T_DATA || RDATA(b)->dfree != (RUBY_DATA_FUNC)mm_free)
        return Qfalse;

    GetMmap(a, i_mm, 0);
    GetMmap(b, u_mm, 0);
    if (i_mm->t->real != u_mm->t->real)
        return Qfalse;
    a = mm_str(a, MM_ORIGIN);
    b = mm_str(b, MM_ORIGIN);
    result = rb_funcall2(a, rb_intern("=="), 1, &b);
    return result;
}

/*
 * call-seq: eql?(other)
 *
 * Is this eql? to +other+ ?
 */
static VALUE
mm_eql(VALUE a, VALUE b)
{
    mm_ipc *i_mm, *u_mm;

    if (a == b)
        return Qtrue;
    if (TYPE(b) != T_DATA || RDATA(b)->dfree != (RUBY_DATA_FUNC)mm_free)
        return Qfalse;

    GetMmap(a, i_mm, 0);
    GetMmap(b, u_mm, 0);
    if (i_mm->t->real != u_mm->t->real)
        return Qfalse;
    a = mm_str(a, MM_ORIGIN);
    b = mm_str(b, MM_ORIGIN);
    return rb_funcall2(a, rb_intern("eql?"), 1, &b);
}

/*
 * call-seq: hash
 *
 * Get the hash value
 */
static VALUE
mm_hash(VALUE a)
{
    VALUE b;
    long res;

    b = mm_str(a, MM_ORIGIN);
    res = rb_str_hash(b);
    return LONG2FIX(res);
}

/*
 * Document-method: length
 * Document-method: size
 *
 * return the size of the file
 */
static VALUE
mm_size(VALUE a)
{
    mm_ipc *i_mm;

    GetMmap(a, i_mm, 0);
    return ULONG2NUM(i_mm->t->real);
}

/*
 * call-seq: empty?
 *
 * return <em>true</em> if the file is empty
 */
static VALUE
mm_empty(VALUE a)
{
    mm_ipc *i_mm;

    GetMmap(a, i_mm, 0);
    if (i_mm->t->real == 0)
        return Qtrue;
    return Qfalse;
}

static VALUE
mm_protect_bang(VALUE arg)
{
    VALUE *t = (VALUE *)arg;
    return rb_funcall2(t[0], (ID)t[1], (int)t[2], (VALUE *)t[3]);
}

static VALUE
mm_recycle(VALUE str)
{
    return str;
}

static VALUE
mm_i_bang(VALUE arg)
{
    mm_bang *bang_st = (mm_bang *)arg;
    VALUE str, res;
    mm_ipc *i_mm;

    str = mm_str(bang_st->obj, bang_st->flag);
    if (bang_st->flag & MM_PROTECT)
    {
        VALUE tmp[4];
        tmp[0] = str;
        tmp[1] = (VALUE)bang_st->id;
        tmp[2] = (VALUE)bang_st->argc;
        tmp[3] = (VALUE)bang_st->argv;
        res = rb_ensure(mm_protect_bang, (VALUE)tmp, mm_recycle, str);
    }
    else
    {
        res = rb_funcall2(str, bang_st->id, bang_st->argc, bang_st->argv);
        RB_GC_GUARD(res);
    }
    if (res != Qnil)
    {
        GetMmap(bang_st->obj, i_mm, 0);
        i_mm->t->real = RSTRING_LEN(str);
    }
    return res;
}

static VALUE
mm_bang_i(VALUE obj, int flag, ID id, int argc, VALUE *argv)
{
    VALUE res;
    mm_ipc *i_mm;
    mm_bang bang_st;

    GetMmap(obj, i_mm, 0);
    if ((flag & MM_CHANGE) && (i_mm->t->flag & MM_FIXED))
    {
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    bang_st.obj = obj;
    bang_st.flag = flag;
    bang_st.id = id;
    bang_st.argc = argc;
    bang_st.argv = argv;
    if (i_mm->t->flag & MM_IPC)
    {
        mm_lock(i_mm, Qtrue);
        res = rb_ensure(mm_i_bang, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else
    {
        res = mm_i_bang((VALUE)&bang_st);
    }
    if (res == Qnil)
        return res;
    return (flag & MM_ORIGIN) ? res : obj;
}

#if HAVE_RB_STR_MATCH

/*
 * call-seq: match(pattern)
 *
 * convert <em>pattern</em> to a <em>Regexp</em> and then call
 * <em>match</em> on <em>self</em>
 */
static VALUE
mm_match_m(VALUE a, VALUE b)
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("match"), 1, &b);
}

#endif

/*
 * call-seq: upcase!
 *
 * replaces all lowercase characters to downcase characters
 */
static VALUE
mm_upcase_bang(VALUE a)
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("upcase!"), 0, 0);
}

/*
 * call-seq: downcase!
 *
 * change all uppercase character to lowercase character
 */
static VALUE
mm_downcase_bang(VALUE a)
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("downcase!"), 0, 0);
}

/*
 * call-seq: capitalize!
 *
 * change the first character to uppercase letter
 */
static VALUE
mm_capitalize_bang(VALUE a)
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("capitalize!"), 0, 0);
}

/*
 * call-seq: swapcase!
 *
 * replaces all lowercase characters to uppercase characters, and vice-versa
 */
static VALUE
mm_swapcase_bang(VALUE a)
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("swapcase!"), 0, 0);
}

/*
 * call-seq: reverse!
 *
 * reverse the content of the file
 */
static VALUE
mm_reverse_bang(VALUE a)
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("reverse!"), 0, 0);
}

/*
 * call-seq: chop!
 *
 * chop off the last character
 */
static VALUE
mm_chop_bang(VALUE a)
{
    return mm_bang_i(a, MM_CHANGE, rb_intern("chop!"), 0, 0);
}

/*
 * call-seq: chomp!(rs = $/)
 *
 * chop off the  line ending character, specified by <em>rs</em>
 */
static VALUE
mm_chomp_bang(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("chomp!"), argc, argv);
}

/*
 * call-seq: delete!(str)
 *
 * delete every characters included in <em>str</em>
 */
static VALUE
mm_delete_bang(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("delete!"), argc, argv);
}

/*
 * squeeze!(str)
 *
 * squeezes sequences of the same characters which is included in <em>str</em>
 */
static VALUE
mm_squeeze_bang(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("squeeze!"), argc, argv);
}

/*
 * call-seq: tr!(search, replace)
 *
 * translate the character from <em>search</em> to <em>replace</em>
 */
static VALUE
mm_tr_bang(VALUE obj, VALUE a, VALUE b)
{
    VALUE tmp[2];
    tmp[0] = a;
    tmp[1] = b;
    return mm_bang_i(obj, MM_MODIFY | MM_PROTECT, rb_intern("tr!"), 2, tmp);
}

/*
 * call-seq: tr_s!(search, replace)
 *
 * translate the character from <em>search</em> to <em>replace</em>, then
 * squeeze sequence of the same characters
 */
static VALUE
mm_tr_s_bang(VALUE obj, VALUE a, VALUE b)
{
    VALUE tmp[2];
    tmp[0] = a;
    tmp[1] = b;
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("tr_s!"), 2, tmp);
}

/*
 * call-seq: crypt
 *
 * crypt with <em>salt</em>
 */
static VALUE
mm_crypt(VALUE a, VALUE b)
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("crypt"), 1, &b);
}

/*
 * call-seq: include?(other)
 *
 * return <em>true</em> if <em>other</em> is found
 */
static VALUE
mm_include(VALUE a, VALUE b)
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("include?"), 1, &b);
}

/*
 * call-seq: index
 *
 * return the index of <em>substr</em>
 */
static VALUE
mm_index(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("index"), argc, argv);
}

/*
 * call-seq: rindex(sibstr, pos = nil)
 *
 * return the index of the last occurrence of <em>substr</em>
 */
static VALUE
mm_rindex(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("rindex"), argc, argv);
}

/*
 * Document-method: []
 * Document-method: slice
 *
 * call-seq: [](args)
 *
 * Element reference - with the following syntax:
 *
 *   self[nth]
 *
 * retrieve the <em>nth</em> character
 *
 *   self[start..last]
 *
 * return a substring from <em>start</em> to <em>last</em>
 *
 *   self[start, length]
 *
 * return a substring of <em>lenght</em> characters from <em>start</em>
 */
static VALUE
mm_aref_m(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("[]"), argc, argv);
}

/*
 * call-seq: sum(bits = 16)
 *
 * return a checksum
 */
static VALUE
mm_sum(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("sum"), argc, argv);
}

/*
 * call-seq: split(sep, limit = 0)
 *
 * splits into a list of strings and return this array
 */
static VALUE
mm_split(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("split"), argc, argv);
}

/*
 * call-seq: count(o1, *args)
 *
 * each parameter defines a set of character to count
 */
static VALUE
mm_count(int argc, VALUE *argv, VALUE obj)
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("count"), argc, argv);
}

/*
 * Document-method: lockall
 * Document-method: mlockall
 *
 * call-seq:
 *  lockall(flag)
 *
 * disable paging of all pages mapped. <em>flag</em> can be
 * <em>Mmap::MCL_CURRENT</em> or <em>Mmap::MCL_FUTURE</em>
 */
static VALUE
mm_mlockall(VALUE obj, VALUE flag)
{
    if (mlockall(NUM2INT(flag)) == -1)
    {
        rb_raise(rb_eArgError, "mlockall(%d)", errno);
    }
    return Qnil;
}

/*
 * Document-method: unlockall
 * Document-method: munlockall
 *
 * call-seq: unlockall
 *
 * reenable paging
 */
static VALUE
mm_munlockall(VALUE obj)
{
    if (munlockall() == -1)
    {
        rb_raise(rb_eArgError, "munlockall(%d)", errno);
    }
    return Qnil;
}

/*
 * Document-method: lock
 * Document-method: mlock
 *
 * call-seq: mlock
 *
 * disable paging
 */
static VALUE
mm_mlock(VALUE obj)
{
    mm_ipc *i_mm;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    if (i_mm->t->flag & MM_LOCK)
    {
        return obj;
    }
    if (i_mm->t->flag & MM_ANON)
    {
        rb_raise(rb_eArgError, "mlock(anonymous)");
    }
    if (mlock(i_mm->t->addr, i_mm->t->len) == -1)
    {
        rb_raise(rb_eArgError, "mlock(%d)", errno);
    }
    i_mm->t->flag |= MM_LOCK;
    return obj;
}

/*
 * Document-method: munlock
 * Document-method: unlock
 *
 * call-seq: unlock
 *
 * reenable paging
 */
static VALUE
mm_munlock(VALUE obj)
{
    mm_ipc *i_mm;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    if (!(i_mm->t->flag & MM_LOCK))
    {
        return obj;
    }
    if (munlock(i_mm->t->addr, i_mm->t->len) == -1)
    {
        rb_raise(rb_eArgError, "munlock(%d)", errno);
    }
    i_mm->t->flag &= ~MM_LOCK;
    return obj;
}

void Init_mmap()
{
    if (rb_const_defined_at(rb_cObject, rb_intern("Mmap")))
    {
        mm_cMap = rb_const_get(rb_cObject, rb_intern("Mmap"));
    } else {
        mm_cMap = rb_define_class("Mmap", rb_cObject);
    }
    rb_define_const(mm_cMap, "MS_SYNC", INT2FIX(MS_SYNC));
    rb_define_const(mm_cMap, "MS_ASYNC", INT2FIX(MS_ASYNC));
    rb_define_const(mm_cMap, "MS_INVALIDATE", INT2FIX(MS_INVALIDATE));
    rb_define_const(mm_cMap, "PROT_READ", INT2FIX(PROT_READ));
    rb_define_const(mm_cMap, "PROT_WRITE", INT2FIX(PROT_WRITE));
    rb_define_const(mm_cMap, "PROT_EXEC", INT2FIX(PROT_EXEC));
    rb_define_const(mm_cMap, "PROT_NONE", INT2FIX(PROT_NONE));
    rb_define_const(mm_cMap, "MAP_SHARED", INT2FIX(MAP_SHARED));
    rb_define_const(mm_cMap, "MAP_PRIVATE", INT2FIX(MAP_PRIVATE));
#ifdef MADV_NORMAL
    rb_define_const(mm_cMap, "MADV_NORMAL", INT2FIX(MADV_NORMAL));
    rb_define_const(mm_cMap, "MADV_RANDOM", INT2FIX(MADV_RANDOM));
    rb_define_const(mm_cMap, "MADV_SEQUENTIAL", INT2FIX(MADV_SEQUENTIAL));
    rb_define_const(mm_cMap, "MADV_WILLNEED", INT2FIX(MADV_WILLNEED));
    rb_define_const(mm_cMap, "MADV_DONTNEED", INT2FIX(MADV_DONTNEED));
#endif
#ifdef MAP_DENYWRITE
    rb_define_const(mm_cMap, "MAP_DENYWRITE", INT2FIX(MAP_DENYWRITE));
#endif
#ifdef MAP_EXECUTABLE
    rb_define_const(mm_cMap, "MAP_EXECUTABLE", INT2FIX(MAP_EXECUTABLE));
#endif
#ifdef MAP_NORESERVE
    rb_define_const(mm_cMap, "MAP_NORESERVE", INT2FIX(MAP_NORESERVE));
#endif
#ifdef MAP_LOCKED
    rb_define_const(mm_cMap, "MAP_LOCKED", INT2FIX(MAP_LOCKED));
#endif
#ifdef MAP_GROWSDOWN
    rb_define_const(mm_cMap, "MAP_GROWSDOWN", INT2FIX(MAP_GROWSDOWN));
#endif
#ifdef MAP_ANON
    rb_define_const(mm_cMap, "MAP_ANON", INT2FIX(MAP_ANON));
#endif
#ifdef MAP_ANONYMOUS
    rb_define_const(mm_cMap, "MAP_ANONYMOUS", INT2FIX(MAP_ANONYMOUS));
#endif
#ifdef MAP_NOSYNC
    rb_define_const(mm_cMap, "MAP_NOSYNC", INT2FIX(MAP_NOSYNC));
#endif
#ifdef MCL_CURRENT
    rb_define_const(mm_cMap, "MCL_CURRENT", INT2FIX(MCL_CURRENT));
    rb_define_const(mm_cMap, "MCL_FUTURE", INT2FIX(MCL_FUTURE));
#endif

    rb_define_alloc_func(mm_cMap, mm_s_alloc);
    rb_define_singleton_method(mm_cMap, "mlockall", mm_mlockall, 1);
    rb_define_singleton_method(mm_cMap, "lockall", mm_mlockall, 1);
    rb_define_singleton_method(mm_cMap, "munlockall", mm_munlockall, 0);
    rb_define_singleton_method(mm_cMap, "unlockall", mm_munlockall, 0);

    rb_define_method(mm_cMap, "initialize", mm_init, -1);

    rb_define_method(mm_cMap, "unmap", mm_unmap, 0);
    rb_define_method(mm_cMap, "munmap", mm_unmap, 0);
    rb_define_method(mm_cMap, "msync", mm_msync, -1);
    rb_define_method(mm_cMap, "sync", mm_msync, -1);
    rb_define_method(mm_cMap, "flush", mm_msync, -1);
    rb_define_method(mm_cMap, "mprotect", mm_mprotect, 1);
    rb_define_method(mm_cMap, "protect", mm_mprotect, 1);
#ifdef MADV_NORMAL
    rb_define_method(mm_cMap, "madvise", mm_madvise, 1);
    rb_define_method(mm_cMap, "advise", mm_madvise, 1);
#endif
    rb_define_method(mm_cMap, "mlock", mm_mlock, 0);
    rb_define_method(mm_cMap, "lock", mm_mlock, 0);
    rb_define_method(mm_cMap, "munlock", mm_munlock, 0);
    rb_define_method(mm_cMap, "unlock", mm_munlock, 0);

    rb_define_method(mm_cMap, "extend", mm_extend, 1);
    rb_define_method(mm_cMap, "<=>", mm_cmp, 1);
    rb_define_method(mm_cMap, "==", mm_equal, 1);
    rb_define_method(mm_cMap, "===", mm_equal, 1);
    rb_define_method(mm_cMap, "eql?", mm_eql, 1);
    rb_define_method(mm_cMap, "hash", mm_hash, 0);
#if HAVE_RB_STR_CASECMP
    rb_define_method(mm_cMap, "casecmp", mm_casecmp, 1);
#endif
    rb_define_method(mm_cMap, "[]", mm_aref_m, -1);
    rb_define_method(mm_cMap, "[]=", mm_aset_m, -1);
#if HAVE_RB_STR_INSERT
    rb_define_method(mm_cMap, "insert", mm_insert, 2);
#endif
    rb_define_method(mm_cMap, "length", mm_size, 0);
    rb_define_method(mm_cMap, "size", mm_size, 0);
    rb_define_method(mm_cMap, "empty?", mm_empty, 0);
    rb_define_method(mm_cMap, "=~", mm_match, 1);
#if HAVE_RB_STR_MATCH
    rb_define_method(mm_cMap, "match", mm_match_m, 1);
#endif
    rb_define_method(mm_cMap, "index", mm_index, -1);
    rb_define_method(mm_cMap, "rindex", mm_rindex, -1);

    rb_define_method(mm_cMap, "to_str", mm_to_str, 0);

    rb_define_method(mm_cMap, "upcase!", mm_upcase_bang, 0);
    rb_define_method(mm_cMap, "downcase!", mm_downcase_bang, 0);
    rb_define_method(mm_cMap, "capitalize!", mm_capitalize_bang, 0);
    rb_define_method(mm_cMap, "swapcase!", mm_swapcase_bang, 0);

    rb_define_method(mm_cMap, "split", mm_split, -1);
    rb_define_method(mm_cMap, "reverse!", mm_reverse_bang, 0);
    rb_define_method(mm_cMap, "concat", mm_concat, 1);
    rb_define_method(mm_cMap, "<<", mm_concat, 1);
    rb_define_method(mm_cMap, "crypt", mm_crypt, 1);

    rb_define_method(mm_cMap, "include?", mm_include, 1);

    rb_define_method(mm_cMap, "sub!", mm_sub_bang, -1);
    rb_define_method(mm_cMap, "gsub!", mm_gsub_bang, -1);
    rb_define_method(mm_cMap, "strip!", mm_strip_bang, 0);
#if HAVE_RB_STR_LSTRIP
    rb_define_method(mm_cMap, "lstrip!", mm_lstrip_bang, 0);
    rb_define_method(mm_cMap, "rstrip!", mm_rstrip_bang, 0);
#endif
    rb_define_method(mm_cMap, "chop!", mm_chop_bang, 0);
    rb_define_method(mm_cMap, "chomp!", mm_chomp_bang, -1);

    rb_define_method(mm_cMap, "count", mm_count, -1);

    rb_define_method(mm_cMap, "tr!", mm_tr_bang, 2);
    rb_define_method(mm_cMap, "tr_s!", mm_tr_s_bang, 2);
    rb_define_method(mm_cMap, "delete!", mm_delete_bang, -1);
    rb_define_method(mm_cMap, "squeeze!", mm_squeeze_bang, -1);

    rb_define_method(mm_cMap, "sum", mm_sum, -1);

    rb_define_method(mm_cMap, "slice", mm_aref_m, -1);
    rb_define_method(mm_cMap, "slice!", mm_slice_bang, -1);
    rb_define_method(mm_cMap, "semlock", mm_semlock, -1);
    rb_define_method(mm_cMap, "ipc_key", mm_ipc_key, 0);

    rb_define_private_method(mm_cMap, "set_length", mm_set_length, 1);
    rb_define_private_method(mm_cMap, "set_offset", mm_set_offset, 1);
    rb_define_private_method(mm_cMap, "set_advice", mm_set_advice, 1);
    rb_define_private_method(mm_cMap, "set_increment", mm_set_increment, 1);
    rb_define_private_method(mm_cMap, "set_ipc", mm_set_ipc, 1);
}
