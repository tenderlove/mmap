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

#include <rubyio.h>
#include <intern.h>
#include <re.h>

#ifndef StringValue
#define StringValue(x) do { 				\
    if (TYPE(x) != T_STRING) x = rb_str_to_str(x); 	\
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
#define MADV_NORMAL     POSIX_MADV_NORMAL 
#define MADV_RANDOM     POSIX_MADV_RANDOM 
#define MADV_SEQUENTIAL POSIX_MADV_SEQUENTIAL
#define MADV_WILLNEED   POSIX_MADV_WILLNEED
#define MADV_DONTNEED   POSIX_MADV_DONTNEED
#define madvise posix_madvise
#endif
#endif

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

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
#define MAP_FAILED ((caddr_t)-1)
#endif /* !MAP_FAILED */

#ifndef MAP_ANON
#ifdef MAP_ANONYMOUS
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

static VALUE mm_cMap;

#define EXP_INCR_SIZE 4096

typedef struct {
    MMAP_RETTYPE addr;
    int smode, pmode, vscope;
    int advice, flag;
    VALUE key;
    int semid, shmid;
    size_t len, real, incr;
    off_t offset;
    char *path, *template;
} mm_mmap;

typedef struct {
    int count;
    mm_mmap *t;
} mm_ipc;

typedef struct {
    VALUE obj, *argv;
    int flag, id, argc;
} mm_bang;

#define MM_MODIFY 1
#define MM_ORIGIN 2
#define MM_CHANGE (MM_MODIFY | 4)
#define MM_PROTECT 8

#define MM_FROZEN (1<<0)
#define MM_FIXED  (1<<1)
#define MM_ANON   (1<<2)
#define MM_LOCK   (1<<3)
#define MM_IPC    (1<<4)
#define MM_TMP    (1<<5)

#if HAVE_SEMCTL && HAVE_SHMCTL
static char template[1024];

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short int *array;
    struct seminfo *__buf;
};
#endif

static void
mm_free(i_mm)
    mm_ipc *i_mm;
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    if (i_mm->t->flag & MM_IPC) {
	struct shmid_ds buf;

	if (shmctl(i_mm->t->shmid, IPC_STAT, &buf) != -1) {
	    if (buf.shm_nattch == 1 && (i_mm->t->flag & MM_TMP)) {
		semctl(i_mm->t->semid, 0, IPC_RMID);
		if (i_mm->t->template) {
		    unlink(i_mm->t->template);
		    free(i_mm->t->template);
		}
	    }
	}
	shmdt(i_mm->t);
    }
    else {
	free(i_mm->t);
    }
#endif
    if (i_mm->t->path) {
	munmap(i_mm->t->addr, i_mm->t->len);
	if (i_mm->t->path != (char *)-1) {
	    if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE &&
		truncate(i_mm->t->path, i_mm->t->real) == -1) {
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
mm_lock(i_mm, wait_lock)
    mm_ipc *i_mm;
    int wait_lock;
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    struct sembuf sem_op;

    if (i_mm->t->flag & MM_IPC) {
	i_mm->count++;
	if (i_mm->count == 1) {
	retry:
	    sem_op.sem_num = 0;
	    sem_op.sem_op = -1;
	    sem_op.sem_flg = IPC_NOWAIT;
	    if (semop(i_mm->t->semid, &sem_op, 1) == -1) {
		if (errno == EAGAIN) {
		    if (!wait_lock) {
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
mm_unlock(i_mm)
    mm_ipc *i_mm;
{
#if HAVE_SEMCTL && HAVE_SHMCTL
    struct sembuf sem_op;

    if (i_mm->t->flag & MM_IPC) {
	i_mm->count--;
	if (!i_mm->count) {
	retry:
	    sem_op.sem_num = 0;
	    sem_op.sem_op = 1;
	    sem_op.sem_flg = IPC_NOWAIT;
	    if (semop(i_mm->t->semid, &sem_op, 1) == -1) {
		if (errno == EAGAIN) {
		    rb_thread_sleep(1);
		    goto retry;
		}
		rb_sys_fail("semop()");
	    }
	}
    }
#endif
}

#define GetMmap(obj, i_mm, t_modify)					\
    Data_Get_Struct(obj, mm_ipc, i_mm);					\
    if (!i_mm->t->path) {						\
	rb_raise(rb_eIOError, "unmapped file");				\
    }									\
    if ((t_modify & MM_MODIFY) && (i_mm->t->flag & MM_FROZEN)) {	\
	rb_error_frozen("mmap");					\
    }

static VALUE
mm_vunlock(obj)
    VALUE obj;
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    mm_unlock(i_mm);
    return Qnil;
}

static VALUE
mm_semlock(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (!(i_mm->t->flag & MM_IPC)) {
	rb_warning("useless use of #semlock");
	rb_yield(obj);
    }
    else {
#if HAVE_SEMCTL && HAVE_SHMCTL
	VALUE a;
	int wait_lock = Qtrue;

	if (rb_scan_args(argc, argv, "01", &a)) {
	    wait_lock = RTEST(a);
	}
	mm_lock(i_mm, wait_lock);
	rb_ensure(rb_yield, obj, mm_vunlock, obj);
#endif
    }
    return Qnil;
}

static VALUE
mm_ipc_key(obj)
    VALUE obj;
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (i_mm->t->flag & MM_IPC) {
	return INT2NUM(i_mm->t->key);
    }
    return INT2NUM(-1);
}

static VALUE
mm_unmap(obj)
    VALUE obj;
{
    mm_ipc *i_mm;

    GetMmap(obj, i_mm, 0);
    if (i_mm->t->path) {
	mm_lock(i_mm, Qtrue);
	munmap(i_mm->t->addr, i_mm->t->len);
	if (i_mm->t->path != (char *)-1) {
	    if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE &&
		truncate(i_mm->t->path, i_mm->t->real) == -1) {
		rb_raise(rb_eTypeError, "truncate");
	    }
	    free(i_mm->t->path);
	}
	i_mm->t->path = '\0';
	mm_unlock(i_mm);
    }
    return Qnil;
}

static VALUE
mm_freeze(obj)
    VALUE obj;
{
    mm_ipc *i_mm;
    rb_obj_freeze(obj);
    GetMmap(obj, i_mm, 0);
    i_mm->t->flag |= MM_FROZEN;
    return obj;
}

static VALUE
mm_str(obj, modify)
    VALUE obj;
    int modify;
{
    mm_ipc *i_mm;
    VALUE ret = Qnil;

    GetMmap(obj, i_mm, modify & ~MM_ORIGIN);
    if (modify & MM_MODIFY) {
	if (i_mm->t->flag & MM_FROZEN) rb_error_frozen("mmap");
	if (!OBJ_TAINTED(ret) && rb_safe_level() >= 4)
	    rb_raise(rb_eSecurityError, "Insecure: can't modify mmap");
    }
#if HAVE_RB_DEFINE_ALLOC_FUNC
    ret = rb_obj_alloc(rb_cString);
    if (rb_obj_tainted(obj)) {
	OBJ_TAINT(ret);
    }
#else
    if (rb_obj_tainted(obj)) {
	ret = rb_tainted_str_new2("");
    }
    else {
	ret = rb_str_new2("");
    }
    free(RSTRING(ret)->ptr);
#endif
    RSTRING(ret)->ptr = i_mm->t->addr;
    RSTRING(ret)->len = i_mm->t->real;
    if (modify & MM_ORIGIN) {
#if HAVE_RB_DEFINE_ALLOC_FUNC
	RSTRING(ret)->aux.shared = ret;
	FL_SET(ret, ELTS_SHARED);
#else
	RSTRING(ret)->orig = ret;
#endif
    }
    if (i_mm->t->flag & MM_FROZEN) {
	ret = rb_obj_freeze(ret);
    }
    return ret;
}

static VALUE
mm_to_str(obj)
    VALUE obj;
{
    return mm_str(obj, MM_ORIGIN);
}
 
extern char *ruby_strdup();

typedef struct {
    mm_ipc *i_mm;
    size_t len;
} mm_st;

static VALUE
mm_i_expand(st_mm)
    mm_st *st_mm;
{
    int fd;
    mm_ipc *i_mm = st_mm->i_mm;
    size_t len = st_mm->len;

    if (munmap(i_mm->t->addr, i_mm->t->len)) {
	rb_raise(rb_eArgError, "munmap failed");
    }
    if ((fd = open(i_mm->t->path, i_mm->t->smode)) == -1) {
	rb_raise(rb_eArgError, "Can't open %s", i_mm->t->path);
    }
    if (len > i_mm->t->len) {
	if (lseek(fd, len - i_mm->t->len - 1, SEEK_END) == -1) {
	    rb_raise(rb_eIOError, "Can't lseek %d", len - i_mm->t->len - 1);
	}
	if (write(fd, "\000", 1) != 1) {
	    rb_raise(rb_eIOError, "Can't extend %s", i_mm->t->path);
	}
    }
    else if (len < i_mm->t->len && truncate(i_mm->t->path, len) == -1) {
	rb_raise(rb_eIOError, "Can't truncate %s", i_mm->t->path);
    }
    i_mm->t->addr = mmap(0, len, i_mm->t->pmode, i_mm->t->vscope, fd, i_mm->t->offset);
    close(fd);
    if (i_mm->t->addr == MAP_FAILED) {
	rb_raise(rb_eArgError, "mmap failed");
    }
#ifdef MADV_NORMAL
    if (i_mm->t->advice && madvise(i_mm->t->addr, len, i_mm->t->advice) == -1) {
	rb_raise(rb_eArgError, "madvise(%d)", errno);
    }
#endif
    if ((i_mm->t->flag & MM_LOCK) && mlock(i_mm->t->addr, len) == -1) {
	rb_raise(rb_eArgError, "mlock(%d)", errno);
    }
    i_mm->t->len  = len;
    return Qnil;
}

static void
mm_expandf(i_mm, len)
    mm_ipc *i_mm;
    size_t len;
{
    int status;
    mm_st st_mm;

    if (i_mm->t->vscope == MAP_PRIVATE) {
	rb_raise(rb_eTypeError, "expand for a private map");
    }
    if (i_mm->t->flag & MM_FIXED) {
	rb_raise(rb_eTypeError, "expand for a fixed map");
    }
    if (!i_mm->t->path || i_mm->t->path == (char *)-1) {
	rb_raise(rb_eTypeError, "expand for an anonymous map");
    }
    st_mm.i_mm = i_mm;
    st_mm.len = len;
    if (i_mm->t->flag & MM_IPC) {
	mm_lock(i_mm, Qtrue);
	rb_protect(mm_i_expand, (VALUE)&st_mm, &status);
	mm_unlock(i_mm);
	if (status) {
	    rb_jump_tag(status);
	}
    }
    else {
	mm_i_expand(&st_mm);
    }
}

static void
mm_realloc(i_mm, len)
    mm_ipc *i_mm;
    size_t len;
{
    if (i_mm->t->flag & MM_FROZEN) rb_error_frozen("mmap");
    if (len > i_mm->t->len) {
	if ((len - i_mm->t->len) < i_mm->t->incr) {
	    len = i_mm->t->len + i_mm->t->incr;
	}
	mm_expandf(i_mm, len);
    }
}

static VALUE
mm_extend(obj, a)
    VALUE obj, a;
{
    mm_ipc *i_mm;
    long len;

    GetMmap(obj, i_mm, MM_MODIFY);
    len = NUM2LONG(a);
    if (len > 0) {
	mm_expandf(i_mm, i_mm->t->len + len);
    }
    return UINT2NUM(i_mm->t->len);
}

static VALUE
mm_i_options(arg, obj)
    VALUE arg, obj;
{
    mm_ipc *i_mm;
    char *options;
    VALUE key, value;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    key = rb_ary_entry(arg, 0);
    value = rb_ary_entry(arg, 1);
    key = rb_obj_as_string(key);
    options = StringValuePtr(key);
    if (strcmp(options, "length") == 0) {
	i_mm->t->len = NUM2UINT(value);
	if (i_mm->t->len <= 0) {
	    rb_raise(rb_eArgError, "Invalid value for length %d", i_mm->t->len);
	}
	i_mm->t->flag |= MM_FIXED;
    }
    else if (strcmp(options, "offset") == 0) {
	i_mm->t->offset = NUM2INT(value);
	if (i_mm->t->offset < 0) {
	    rb_raise(rb_eArgError, "Invalid value for offset %d", i_mm->t->offset);
	}
	i_mm->t->flag |= MM_FIXED;
    }
    else if (strcmp(options, "advice") == 0) {
	i_mm->t->advice = NUM2INT(value);
    }
    else if (strcmp(options, "increment") == 0) {
	int incr =  NUM2INT(value);
	if (incr < 0) {
	    rb_raise(rb_eArgError, "Invalid value for increment %d", incr);
	}
	i_mm->t->incr = incr;
    }
    else if (strcmp(options, "initialize") == 0) {
    }
#if HAVE_SEMCTL && HAVE_SHMCTL
    else if (strcmp(options, "ipc") == 0) {
	if (value != Qtrue && TYPE(value) != T_HASH) {
	    rb_raise(rb_eArgError, "Expected an Hash for :ipc");
	}
	i_mm->t->shmid = value;
	i_mm->t->flag |= (MM_IPC | MM_TMP);
    }
#endif
    else {
	rb_warning("Unknown option `%s'", options);
    }
    return Qnil;
}

#if HAVE_SEMCTL && HAVE_SHMCTL

static VALUE
mm_i_ipc(arg, obj)
    VALUE arg, obj;
{
    mm_ipc *i_mm;
    char *options;
    VALUE key, value;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    key = rb_ary_entry(arg, 0);
    value = rb_ary_entry(arg, 1);
    key = rb_obj_as_string(key);
    options = StringValuePtr(key);
    if (strcmp(options, "key") == 0) {
	i_mm->t->key = rb_funcall2(value, rb_intern("to_int"), 0, 0);
    }
    else if (strcmp(options, "permanent") == 0) {
	if (RTEST(value)) {
	    i_mm->t->flag &= ~MM_TMP;
	}
    }
    else if (strcmp(options, "mode") == 0) {
	i_mm->t->semid = NUM2INT(value);
    }
    else {
	rb_warning("Unknown option `%s'", options);
    }
    return Qnil;
}

#endif

static VALUE
mm_s_new(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    VALUE res = rb_funcall2(obj, rb_intern("allocate"), 0, 0);
    rb_obj_call_init(res, argc, argv);
    return res;
}

static VALUE
mm_s_alloc(obj)
    VALUE obj;
{
    VALUE res;
    mm_ipc *i_mm;

    res = Data_Make_Struct(obj, mm_ipc, 0, mm_free, i_mm);
    i_mm->t = ALLOC_N(mm_mmap, 1);
    MEMZERO(i_mm->t, mm_mmap, 1);
    i_mm->t->incr = EXP_INCR_SIZE;
    return res;
}

static VALUE
mm_init(argc, argv, obj)
    VALUE obj, *argv;
    int argc;
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
    if (argc > 1 && TYPE(argv[argc - 1]) == T_HASH) {
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
    if (NIL_P(fname)) {
	vscope = MAP_ANON | MAP_SHARED;
	anonymous = 1;
    }
    else 
#endif
    {
	if (rb_safe_level() > 0 && OBJ_TAINTED(fname)){
	    rb_raise(rb_eSecurityError, "Insecure operation");
	}
	rb_secure(4);
	if (rb_respond_to(fname, rb_intern("fileno"))) {
	    fdv = rb_funcall2(fname, rb_intern("fileno"), 0, 0);
	}
	if (NIL_P(fdv)) {
	    fname = rb_str_to_str(fname);
	    SafeStringValue(fname);
	    path = StringValuePtr(fname);
	}
	else {
	    fd = NUM2INT(fdv);
	    if (fd < 0) {
		rb_raise(rb_eArgError, "invalid file descriptor %d", fd);
	    }
	}
	if (!NIL_P(scope)) {
	    vscope = NUM2INT(scope);
#ifdef MAP_ANON
	    if (vscope & MAP_ANON) {
		rb_raise(rb_eArgError, "filename specified for an anonymous map");
	    }
#endif
	}
    }
    vscope |= NIL_P(scope) ? MAP_SHARED : NUM2INT(scope);
    size = 0;
    perm = 0666;
    if (!anonymous) {
	if (NIL_P(vmode)) {
	    mode = "r";
	}
	else if (rb_respond_to(vmode, rb_intern("to_ary"))) {
            VALUE tmp;

            vmode = rb_convert_type(vmode, T_ARRAY, "Array", "to_ary");
            if (RARRAY(vmode)->len != 2) {
                rb_raise(rb_eArgError, "Invalid length %d (expected 2)",
                         RARRAY(vmode)->len);
            }
	    tmp = RARRAY(vmode)->ptr[0];
	    mode = StringValuePtr(tmp);
	    perm = NUM2INT(RARRAY(vmode)->ptr[1]);
	}
	else {
	    mode = StringValuePtr(vmode);
	}
	if (strcmp(mode, "r") == 0) {
	    smode = O_RDONLY;
	    pmode = PROT_READ;
	}
	else if (strcmp(mode, "w") == 0) {
	    smode = O_RDWR | O_TRUNC;
	    pmode = PROT_READ | PROT_WRITE;
	}
	else if (strcmp(mode, "rw") == 0 || strcmp(mode, "wr") == 0) {
	    smode = O_RDWR;
	    pmode = PROT_READ | PROT_WRITE;
	}
	else if (strcmp(mode, "a") == 0) {
	    smode = O_RDWR | O_CREAT;
	    pmode = PROT_READ | PROT_WRITE;
	}
	else {
	    rb_raise(rb_eArgError, "Invalid mode %s", mode);
	}
	if (NIL_P(fdv)) {
	    if ((fd = open(path, smode, perm)) == -1) {
		rb_raise(rb_eArgError, "Can't open %s", path);
	    }
	}
	if (fstat(fd, &st) == -1) {
	    rb_raise(rb_eArgError, "Can't stat %s", path);
	}
	size = st.st_size;
    }
    else {
	fd = -1;
	if (!NIL_P(vmode) && TYPE(vmode) != T_STRING) {
	    size = NUM2INT(vmode);
	}
    }
    Data_Get_Struct(obj, mm_ipc, i_mm);
    if (i_mm->t->flag & MM_FROZEN) {
	rb_raise(rb_eArgError, "frozen mmap");
    }
    i_mm->t->shmid = 0;
    i_mm->t->semid = 0;
    offset = 0;
    if (options != Qnil) {
	rb_iterate(rb_each, options, mm_i_options, obj);
	if (path && (i_mm->t->len + i_mm->t->offset) > st.st_size) {
	    rb_raise(rb_eArgError, "invalid value for length (%d) or offset (%d)",
		     i_mm->t->len, i_mm->t->offset);
	}
	if (i_mm->t->len) size = i_mm->t->len;
	offset = i_mm->t->offset;
#if HAVE_SEMCTL && HAVE_SHMCTL
	if (i_mm->t->flag & MM_IPC) {
	    key_t key;
	    int shmid, semid, mode;
	    union semun sem_val;
	    struct shmid_ds buf;
	    mm_mmap *data;

	    if (!(vscope & MAP_SHARED)) {
		rb_warning("Probably it will not do what you expect ...");
	    }
	    i_mm->t->key = -1;
	    i_mm->t->semid = 0;
	    if (TYPE(i_mm->t->shmid) == T_HASH) {
		rb_iterate(rb_each, i_mm->t->shmid, mm_i_ipc, obj);
	    }
	    i_mm->t->shmid = 0;
	    if (i_mm->t->semid) {
		mode = i_mm->t->semid;
		i_mm->t->semid = 0;
	    }
	    else {
		mode = 0644;
	    }
	    if ((int)i_mm->t->key <= 0) {
		mode |= IPC_CREAT;
		strcpy(template, "/tmp/ruby_mmap.XXXXXX");
		if (mkstemp(template) == -1) {
		    rb_sys_fail("mkstemp()");
		}
		if ((key = ftok(template, 'R')) == -1) {
		    rb_sys_fail("ftok()");
		}
	    }
	    else {
		key = (key_t)i_mm->t->key;
	    }
	    if ((shmid = shmget(key, sizeof(mm_ipc), mode)) == -1) {
		rb_sys_fail("shmget()");
	    }
	    data = shmat(shmid, (void *)0, 0);
	    if (data == (mm_mmap *)-1) {
		rb_sys_fail("shmat()");
	    }
	    if (i_mm->t->flag & MM_TMP) {
		if (shmctl(shmid, IPC_RMID, &buf) == -1) {
		    rb_sys_fail("shmctl()");
		}
	    }
	    if ((semid = semget(key, 1, mode)) == -1) {
		rb_sys_fail("semget()");
	    }
	    if (mode & IPC_CREAT) {
		sem_val.val = 1;
		if (semctl(semid, 0, SETVAL, sem_val) == -1) {
		    rb_sys_fail("semctl()");
		}
	    }
	    memcpy(data, i_mm->t, sizeof(mm_mmap));
	    free(i_mm->t);
	    i_mm->t = data;
	    i_mm->t->key = key;
	    i_mm->t->semid = semid;
	    i_mm->t->shmid = shmid;
	    if (i_mm->t->flag & MM_TMP) {
		i_mm->t->template = ALLOC_N(char, strlen(template) + 1);
		strcpy(i_mm->t->template, template);
	    }
        }
#endif
    }
    init = 0;
    if (anonymous) {
	if (size <= 0) {
	    rb_raise(rb_eArgError, "length not specified for an anonymous map");
	}
	if (offset) {
	    rb_warning("Ignoring offset for an anonymous map");
	    offset = 0;
	}
	smode = O_RDWR;
	pmode = PROT_READ | PROT_WRITE;
	i_mm->t->flag |= MM_FIXED | MM_ANON;
    }
    else {
	if (size == 0 && (smode & O_RDWR)) {
	    if (lseek(fd, i_mm->t->incr - 1, SEEK_END) == -1) {
		rb_raise(rb_eIOError, "Can't lseek %d", i_mm->t->incr - 1);
	    }
	    if (write(fd, "\000", 1) != 1) {
		rb_raise(rb_eIOError, "Can't extend %s", path);
	    }
	    init = 1;
	    size = i_mm->t->incr;
	}
	if (!NIL_P(fdv)) {
	    i_mm->t->flag |= MM_FIXED;
	}
    }
    addr = mmap(0, size, pmode, vscope, fd, offset);
    if (NIL_P(fdv) && !anonymous) {
	close(fd);
    }
    if (addr == MAP_FAILED || !addr) {
	rb_raise(rb_eArgError, "mmap failed (%d)", errno);
    }
#ifdef MADV_NORMAL
    if (i_mm->t->advice && madvise(addr, size, i_mm->t->advice) == -1) {
	rb_raise(rb_eArgError, "madvise(%d)", errno);
    }
#endif
    if (anonymous && TYPE(options) == T_HASH) {
	VALUE val;
	char *ptr;

	val = rb_hash_aref(options, rb_str_new2("initialize"));
	if (!NIL_P(val)) {
	    ptr = StringValuePtr(val);
	    memset(addr, ptr[0], size);
	}
    }
    i_mm->t->addr  = addr;
    i_mm->t->len = size;
    if (!init) i_mm->t->real = size;
    i_mm->t->pmode = pmode;
    i_mm->t->vscope = vscope;
    i_mm->t->smode = smode & ~O_TRUNC;
    i_mm->t->path = (path)?ruby_strdup(path):(char *)-1;
    if (smode == O_RDONLY) {
	obj = rb_obj_freeze(obj);
	i_mm->t->flag |= MM_FROZEN;
    }
    else {
	if (smode == O_WRONLY) {
	    i_mm->t->flag |= MM_FIXED;
	}
	OBJ_TAINT(obj);
    }
    return obj;
}

static VALUE
mm_msync(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    mm_ipc *i_mm;
    VALUE oflag;
    int ret;
    int flag = MS_SYNC;

    if (argc) {
	rb_scan_args(argc, argv, "01", &oflag);
	flag = NUM2INT(oflag);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    if ((ret = msync(i_mm->t->addr, i_mm->t->len, flag)) != 0) {
	rb_raise(rb_eArgError, "msync(%d)", ret);
    }
    if (i_mm->t->real < i_mm->t->len && i_mm->t->vscope != MAP_PRIVATE)
	mm_expandf(i_mm, i_mm->t->real);
    return obj;
}

static VALUE
mm_mprotect(obj, a)
    VALUE obj, a;
{
    mm_ipc *i_mm;
    int ret, pmode;
    char *smode;

    GetMmap(obj, i_mm, 0);
    if (TYPE(a) == T_STRING) {
	smode = StringValuePtr(a);
	if (strcmp(smode, "r") == 0) pmode = PROT_READ;
	else if (strcmp(smode, "w") == 0) pmode = PROT_WRITE;
	else if (strcmp(smode, "rw") == 0 || strcmp(smode, "wr") == 0)
	    pmode = PROT_READ | PROT_WRITE;
	else {
	    rb_raise(rb_eArgError, "Invalid mode %s", smode);
	}
    }
    else {
	pmode = NUM2INT(a);
    }
    if ((pmode & PROT_WRITE) && (i_mm->t->flag & MM_FROZEN)) 
	rb_error_frozen("mmap");
    if ((ret = mprotect(i_mm->t->addr, i_mm->t->len, pmode | PROT_READ)) != 0) {
	rb_raise(rb_eArgError, "mprotect(%d)", ret);
    }
    i_mm->t->pmode = pmode;
    if (pmode & PROT_READ) {
	if (pmode & PROT_WRITE) i_mm->t->smode = O_RDWR;
	else {
	    i_mm->t->smode = O_RDONLY;
	    obj = rb_obj_freeze(obj);
	    i_mm->t->flag |= MM_FROZEN;
	}
    }
    else if (pmode & PROT_WRITE) {
	i_mm->t->flag |= MM_FIXED;
	i_mm->t->smode = O_WRONLY;
    }
    return obj;
}

#ifdef MADV_NORMAL
static VALUE
mm_madvise(obj, a)
    VALUE obj, a;
{
    mm_ipc *i_mm;
    
    GetMmap(obj, i_mm, 0);
    if (madvise(i_mm->t->addr, i_mm->t->len, NUM2INT(a)) == -1) {
	rb_raise(rb_eTypeError, "madvise(%d)", errno);
    }
    i_mm->t->advice = NUM2INT(a);
    return Qnil;
}
#endif

#define StringMmap(b, bp, bl)						   \
do {									   \
    if (TYPE(b) == T_DATA && RDATA(b)->dfree == (RUBY_DATA_FUNC)mm_free) { \
	mm_ipc *b_mm;							   \
	GetMmap(b, b_mm, 0);						   \
	bp = b_mm->t->addr;						   \
	bl = b_mm->t->real;						   \
    }									   \
    else {								   \
	bp = StringValuePtr(b);						   \
	bl = RSTRING(b)->len;						   \
    }									   \
} while (0);

static void
mm_update(str, beg, len, val)
    mm_ipc *str;
    VALUE val;
    long beg;
    long len;
{
    char *valp;
    long vall;

    if (str->t->flag & MM_FROZEN) rb_error_frozen("mmap");
    if (len < 0) rb_raise(rb_eIndexError, "negative length %d", len);
    mm_lock(str);
    if (beg < 0) {
	beg += str->t->real;
    }
    if (beg < 0 || str->t->real < (size_t)beg) {
	if (beg < 0) {
	    beg -= str->t->real;
	}
	mm_unlock(str);
	rb_raise(rb_eIndexError, "index %d out of string", beg);
    }
    if (str->t->real < (size_t)(beg + len)) {
	len = str->t->real - beg;
    }

    mm_unlock(str);
    StringMmap(val, valp, vall);
    mm_lock(str);

    if ((str->t->flag & MM_FIXED) && vall != len) {
	mm_unlock(str);
	rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    if (len < vall) {
	mm_realloc(str, str->t->real + vall - len);
    }

    if (vall != len) {
	memmove((char *)str->t->addr + beg + vall,
		(char *)str->t->addr + beg + len,
		str->t->real - (beg + len));
    }
    if (str->t->real < (size_t)beg && len < 0) {
	MEMZERO(str->t->addr + str->t->real, char, -len);
    }
    if (vall > 0) {
	memmove((char *)str->t->addr + beg, valp, vall);
    }
    str->t->real += vall - len;
    mm_unlock(str);
}

static VALUE
mm_match(x, y)
    VALUE x, y;
{
    VALUE reg, res;
    long start;

    x = mm_str(x, MM_ORIGIN);
    if (TYPE(y) == T_DATA && RDATA(y)->dfree == (RUBY_DATA_FUNC)mm_free) {
	y = mm_to_str(y);
    }
    switch (TYPE(y)) {
      case T_REGEXP:
	res = rb_reg_match(y, x);
	break;

      case T_STRING:
	reg = rb_reg_regcomp(y);
	start = rb_reg_search(reg, x, 0, 0);
	if (start == -1) res = Qnil;
	else res = INT2NUM(start);
	break;

      default:
	res = rb_funcall(y, rb_intern("=~"), 1, x);
	break;
    }
    return res;
}

static VALUE
get_pat(pat)
    VALUE pat;
{
    switch (TYPE(pat)) {
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

static int
mm_correct_backref()
{
    VALUE match;
    int i, start;

    match = rb_backref_get();
    if (NIL_P(match)) return 0;
    if (RMATCH(match)->BEG(0) == -1) return 0;
    start = RMATCH(match)->BEG(0);
    RMATCH(match)->str = rb_str_new(StringValuePtr(RMATCH(match)->str) + start,
				    RMATCH(match)->END(0) - start);
    if (OBJ_TAINTED(match)) OBJ_TAINT(RMATCH(match)->str);
    for (i = 0; i < RMATCH(match)->regs->num_regs && RMATCH(match)->BEG(i) != -1; i++) {
	RMATCH(match)->BEG(i) -= start;
	RMATCH(match)->END(i) -= start;
    }
    rb_backref_set(match);
    return start;
}

static VALUE
mm_sub_bang_int(bang_st)
    mm_bang *bang_st;
{
    int argc = bang_st->argc;
    VALUE *argv = bang_st->argv;
    VALUE obj = bang_st->obj;
    VALUE pat, repl = Qnil, match, str, res;
    struct re_registers *regs;
    int start, iter = 0;
    int tainted = 0;
    long plen;
    mm_ipc *i_mm;

    if (argc == 1 && rb_block_given_p()) {
	iter = 1;
    }
    else if (argc == 2) {
	repl = rb_str_to_str(argv[1]);
	if (OBJ_TAINTED(repl)) tainted = 1;
    }
    else {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);

    pat = get_pat(argv[0]);
    res = Qnil;
    if (rb_reg_search(pat, str, 0, 0) >= 0) {
	start = mm_correct_backref();
	match = rb_backref_get();
	regs = RMATCH(match)->regs;
	if (iter) {
	    rb_match_busy(match);
	    repl = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
	    rb_backref_set(match);
	}
	else {
	    RSTRING(str)->ptr += start;
	    repl = rb_reg_regsub(repl, str, regs);
	    RSTRING(str)->ptr -= start;
	}
	if (OBJ_TAINTED(repl)) tainted = 1;
	plen = END(0) - BEG(0);
	if (RSTRING(repl)->len > plen) {
	    mm_realloc(i_mm, RSTRING(str)->len + RSTRING(repl)->len - plen);
	    RSTRING(str)->ptr = i_mm->t->addr;
	}
	if (RSTRING(repl)->len != plen) {
	    if (i_mm->t->flag & MM_FIXED) {
		rb_raise(rb_eTypeError, "try to change the size of a fixed map");
	    }
	    memmove(RSTRING(str)->ptr + start + BEG(0) + RSTRING(repl)->len,
		    RSTRING(str)->ptr + start + BEG(0) + plen,
		    RSTRING(str)->len - start - BEG(0) - plen);
	}
	memcpy(RSTRING(str)->ptr + start + BEG(0),
	       RSTRING(repl)->ptr, RSTRING(repl)->len);
	i_mm->t->real += RSTRING(repl)->len - plen;
	if (tainted) OBJ_TAINT(obj);

	res = obj;
    }
    rb_gc_force_recycle(str);
    return res;
}

static VALUE
mm_sub_bang(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    VALUE res;
    mm_bang bang_st;
    mm_ipc *i_mm;

    bang_st.argc = argc;
    bang_st.argv = argv;
    bang_st.obj = obj;
    GetMmap(obj, i_mm, MM_MODIFY);
    if (i_mm->t->flag & MM_IPC) {
	mm_lock(i_mm, Qtrue);
	res = rb_ensure(mm_sub_bang_int, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else {
	res = mm_sub_bang_int(&bang_st);
    }
    return res;
}

static VALUE
mm_gsub_bang_int(bang_st)
    mm_bang *bang_st;
{
    int argc = bang_st->argc;
    VALUE *argv = bang_st->argv;
    VALUE obj = bang_st->obj;
    VALUE pat, val, repl = Qnil, match, str;
    struct re_registers *regs;
    long beg, offset;
    int start, iter = 0;
    int tainted = 0;
    long plen;
    mm_ipc *i_mm;

    if (argc == 1 && rb_block_given_p()) {
	iter = 1;
    }
    else if (argc == 2) {
	repl = rb_str_to_str(argv[1]);
	if (OBJ_TAINTED(repl)) tainted = 1;
    }
    else {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    GetMmap(obj, i_mm, MM_MODIFY);
    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);

    pat = get_pat(argv[0]);
    offset = 0;
    beg = rb_reg_search(pat, str, 0, 0);
    if (beg < 0) {
	rb_gc_force_recycle(str);
	return Qnil;
    }
    while (beg >= 0) {
	start = mm_correct_backref();
	match = rb_backref_get();
	regs = RMATCH(match)->regs;
	if (iter) {
	    rb_match_busy(match);
	    val = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
	    rb_backref_set(match);
	}
	else {
	    RSTRING(str)->ptr += start;
	    val = rb_reg_regsub(repl, str, regs);
	    RSTRING(str)->ptr -= start;
	}
	if (OBJ_TAINTED(repl)) tainted = 1;
	plen = END(0) - BEG(0);
	if ((i_mm->t->real + RSTRING(val)->len - plen) > i_mm->t->len) {
	    mm_realloc(i_mm, RSTRING(str)->len + RSTRING(val)->len - plen);
	}
	if (RSTRING(val)->len != plen) {
	    if (i_mm->t->flag & MM_FIXED) {
		rb_raise(rb_eTypeError, "try to change the size of a fixed map");
	    }
	    memmove(RSTRING(str)->ptr + start + BEG(0) + RSTRING(val)->len,
		    RSTRING(str)->ptr + start + BEG(0) + plen,
		    RSTRING(str)->len - start - BEG(0) - plen);
	}
	memcpy(RSTRING(str)->ptr + start + BEG(0),
	       RSTRING(val)->ptr, RSTRING(val)->len);
	RSTRING(str)->len += RSTRING(val)->len - plen;
	i_mm->t->real = RSTRING(str)->len;
	if (BEG(0) == END(0)) {
	    offset = start + END(0) + mbclen2(RSTRING(str)->ptr[END(0)], pat);
	    offset += RSTRING(val)->len - plen;
	}
	else {
	    offset = start + END(0) + RSTRING(val)->len - plen;
	}
	if (offset > RSTRING(str)->len) break;
	beg = rb_reg_search(pat, str, offset, 0);
    }
    rb_backref_set(match);
    if (tainted) OBJ_TAINT(obj);
    rb_gc_force_recycle(str);
    return obj;
}

static VALUE
mm_gsub_bang(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    VALUE res;
    mm_bang bang_st;
    mm_ipc *i_mm;

    bang_st.argc = argc;
    bang_st.argv = argv;
    bang_st.obj = obj;
    GetMmap(obj, i_mm, MM_MODIFY);
    if (i_mm->t->flag & MM_IPC) {
	mm_lock(i_mm, Qtrue);
	res = rb_ensure(mm_gsub_bang_int, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else {
	res = mm_gsub_bang_int(&bang_st);
    }
    return res;
}

static VALUE mm_index __((int, VALUE *, VALUE));

#if HAVE_RB_DEFINE_ALLOC_FUNC

static void
mm_subpat_set(obj, re, offset, val)
    VALUE obj, re;
    int offset;
    VALUE val;
{
    VALUE str, match;
    int start, end, len;
    mm_ipc *i_mm;
    
    str = mm_str(obj, MM_MODIFY | MM_ORIGIN);
    if (rb_reg_search(re, str, 0, 0) < 0) {
	rb_raise(rb_eIndexError, "regexp not matched");
    }
    match = rb_backref_get();
    if (offset >= RMATCH(match)->regs->num_regs) {
	rb_raise(rb_eIndexError, "index %d out of regexp", offset);
    }

    start = RMATCH(match)->BEG(offset);
    if (start == -1) {
	rb_raise(rb_eIndexError, "regexp group %d not matched", offset);
    }
    end = RMATCH(match)->END(offset);
    len = end - start;
    GetMmap(obj, i_mm, MM_MODIFY);
    mm_update(i_mm, start, len, val);
}

#endif

static VALUE
mm_aset(str, indx, val)
    VALUE str;
    VALUE indx, val;
{
    long idx;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    switch (TYPE(indx)) {
      case T_FIXNUM:
      num_index:
	idx = NUM2INT(indx);
	if (idx < 0) {
	    idx += i_mm->t->real;
	}
	if (idx < 0 || i_mm->t->real <= (size_t)idx) {
	    rb_raise(rb_eIndexError, "index %d out of string", idx);
	}
	if (FIXNUM_P(val)) {
	    if (i_mm->t->real == (size_t)idx) {
		i_mm->t->real += 1;
		mm_realloc(i_mm, i_mm->t->real);
	    }
	    ((char *)i_mm->t->addr)[idx] = NUM2INT(val) & 0xff;
	}
	else {
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
	  if (!NIL_P(res)) {
	      mm_update(i_mm, NUM2LONG(res), RSTRING(indx)->len, val);
	  }
	  return val;
      }

      default:
	/* check if indx is Range */
	{
	    long beg, len;
	    if (rb_range_beg_len(indx, &beg, &len, i_mm->t->real, 2)) {
		mm_update(i_mm, beg, len, val);
		return val;
	    }
	}
	idx = NUM2LONG(indx);
	goto num_index;
    }
}

static VALUE
mm_aset_m(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    if (argc == 3) {
	long beg, len;

#if HAVE_RB_DEFINE_ALLOC_FUNC
	if (TYPE(argv[0]) == T_REGEXP) {
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
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 2)", argc);
    }
    return mm_aset(str, argv[0], argv[1]);
}

#if HAVE_RB_STR_INSERT

static VALUE
mm_insert(str, idx, str2)
    VALUE str, idx, str2;
{
    mm_ipc *i_mm;
    long pos = NUM2LONG(idx);

    GetMmap(str, i_mm, MM_MODIFY);
    if (pos == -1) {
	pos = RSTRING(str)->len;
    }
    else if (pos < 0) {
	pos++;
    }
    mm_update(i_mm, pos, 0, str2);
    return str;
}

#endif

static VALUE mm_aref_m _((int, VALUE *, VALUE));

static VALUE
mm_slice_bang(argc, argv, str)
    int argc;
    VALUE *argv;
    VALUE str;
{
    VALUE result;
    VALUE buf[3];
    int i;

    if (argc < 1 || 2 < argc) {
	rb_raise(rb_eArgError, "wrong # of arguments(%d for 1)", argc);
    }
    for (i = 0; i < argc; i++) {
	buf[i] = argv[i];
    }
    buf[i] = rb_str_new(0,0);
    result = mm_aref_m(argc, buf, str);
    if (!NIL_P(result)) {
	mm_aset_m(argc+1, buf, str);
    }
    return result;
}

static VALUE
mm_cat(str, ptr, len)
    VALUE str;
    const char *ptr;
    long len;
{
    mm_ipc *i_mm;
    char *sptr;

    GetMmap(str, i_mm, MM_MODIFY);
    if (len > 0) {
	int poffset = -1;
	sptr = (char *)i_mm->t->addr;

	if (sptr <= ptr &&
	    ptr < sptr + i_mm->t->real) {
	    poffset = ptr - sptr;
	}
	mm_lock(i_mm, Qtrue);
	mm_realloc(i_mm, i_mm->t->real + len);
	sptr = (char *)i_mm->t->addr;
	if (ptr) {
	    if (poffset >= 0) ptr = sptr + poffset;
	    memcpy(sptr + i_mm->t->real, ptr, len);
	}
	i_mm->t->real += len;
	mm_unlock(i_mm);
    }
    return str;
}

static VALUE
mm_append(str1, str2)
    VALUE str1, str2;
{
    str2 = rb_str_to_str(str2);
    str1 = mm_cat(str1, StringValuePtr(str2), RSTRING(str2)->len);
    return str1;
}

static VALUE
mm_concat(str1, str2)
    VALUE str1, str2;
{
    if (FIXNUM_P(str2)) {
	int i = FIX2INT(str2);
	if (0 <= i && i <= 0xff) { /* byte */
	    char c = i;
	    return mm_cat(str1, &c, 1);
	}
    }
    str1 = mm_append(str1, str2);
    return str1;
}

#ifndef HAVE_RB_STR_LSTRIP

static VALUE
mm_strip_bang(str)
    VALUE str;
{
    char *s, *t, *e;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    e = t = s + i_mm->t->real;
    while (s < t && ISSPACE(*s)) s++;
    t--;
    while (s <= t && ISSPACE(*t)) t--;
    t++;

    if (i_mm->t->real != (t - s) && (i_mm->t->flag & MM_FIXED)) {
	mm_unlock(i_mm);
        rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t-s;
    if (s > (char *)i_mm->t->addr) { 
        memmove(i_mm->t->addr, s, i_mm->t->real);
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
    }
    else if (t < e) {
        ((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
    }
    else {
        str = Qnil;
    }
    mm_unlock(i_mm);
    return str;
}

#else

static VALUE
mm_lstrip_bang(str)
    VALUE str;
{
    char *s, *t, *e;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    e = t = s + i_mm->t->real;
    while (s < t && ISSPACE(*s)) s++;

    if (i_mm->t->real != (size_t)(t - s) && (i_mm->t->flag & MM_FIXED)) {
	mm_unlock(i_mm);
	rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t - s;
    if (s > (char *)i_mm->t->addr) { 
	memmove(i_mm->t->addr, s, i_mm->t->real);
	((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
	mm_unlock(i_mm);
	return str;
    }
    mm_unlock(i_mm);
    return Qnil;
}

static VALUE
mm_rstrip_bang(str)
    VALUE str;
{
    char *s, *t, *e;
    mm_ipc *i_mm;

    GetMmap(str, i_mm, MM_MODIFY);
    mm_lock(i_mm, Qtrue);
    s = (char *)i_mm->t->addr;
    e = t = s + i_mm->t->real;
    t--;
    while (s <= t && ISSPACE(*t)) t--;
    t++;
    if (i_mm->t->real != (size_t)(t - s) && (i_mm->t->flag & MM_FIXED)) {
	mm_unlock(i_mm);
	rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    i_mm->t->real = t - s;
    if (t < e) {
	((char *)i_mm->t->addr)[i_mm->t->real] = '\0';
	mm_unlock(i_mm);
	return str;
    }
    mm_unlock(i_mm);
    return Qnil;
}

static VALUE
mm_strip_bang(str)
    VALUE str;
{
    VALUE l = mm_lstrip_bang(str);
    VALUE r = mm_rstrip_bang(str);

    if (NIL_P(l) && NIL_P(r)) return Qnil;
    return str;
}

#endif

#define MmapStr(b, recycle)						    \
do {									    \
    recycle = 0;							    \
    if (TYPE(b) == T_DATA &&  RDATA(b)->dfree == (RUBY_DATA_FUNC)mm_free) { \
	recycle = 1;							    \
	b = mm_str(b, MM_ORIGIN);					    \
    }									    \
    else {								    \
	b = rb_str_to_str(b);						    \
    }									    \
} while (0);
 
 
static VALUE
mm_cmp(a, b)
    VALUE a, b;
{
    int result;
    int recycle = 0;

    a = mm_str(a, MM_ORIGIN);
    MmapStr(b, recycle);
    result = rb_str_cmp(a, b);
    rb_gc_force_recycle(a);
    if (recycle) rb_gc_force_recycle(b);
    return INT2FIX(result);
}

#if HAVE_RB_STR_CASECMP

static VALUE
mm_casecmp(a, b)
    VALUE a, b;
{
    VALUE result;
    int recycle = 0;

    a = mm_str(a, MM_ORIGIN);
    MmapStr(b, recycle);
    result = rb_funcall2(a, rb_intern("casecmp"), 1, &b);
    rb_gc_force_recycle(a);
    if (recycle) rb_gc_force_recycle(b);
    return result;
}

#endif

static VALUE
mm_equal(a, b)
    VALUE a, b;
{
    VALUE result;
    mm_ipc *i_mm, *u_mm;

    if (a == b) return Qtrue;
    if (TYPE(b) != T_DATA || RDATA(b)->dfree != (RUBY_DATA_FUNC)mm_free)
	return Qfalse;

    GetMmap(a, i_mm, 0);
    GetMmap(b, u_mm, 0);
    if (i_mm->t->real != u_mm->t->real)
        return Qfalse;
    a = mm_str(a, MM_ORIGIN);
    b = mm_str(b, MM_ORIGIN);
    result = rb_funcall2(a, rb_intern("=="), 1, &b);
    rb_gc_force_recycle(a);
    rb_gc_force_recycle(b);
    return result;
}

static VALUE
mm_eql(a, b)
    VALUE a, b;
{
    VALUE result;
    mm_ipc *i_mm, *u_mm;
    
    if (a == b) return Qtrue;
    if (TYPE(b) != T_DATA || RDATA(b)->dfree != (RUBY_DATA_FUNC)mm_free)
	return Qfalse;

    GetMmap(a, i_mm, 0);
    GetMmap(b, u_mm, 0);
    if (i_mm->t->real != u_mm->t->real)
        return Qfalse;
    a = mm_str(a, MM_ORIGIN);
    b = mm_str(b, MM_ORIGIN);
    result = rb_funcall2(a, rb_intern("eql?"), 1, &b);
    rb_gc_force_recycle(a);
    rb_gc_force_recycle(b);
    return result;
}

static VALUE
mm_hash(a)
    VALUE a;
{
    VALUE b;
    int res;

    b = mm_str(a, MM_ORIGIN);
    res = rb_str_hash(b);
    rb_gc_force_recycle(b);
    return INT2FIX(res);
}

static VALUE
mm_size(a)
    VALUE a;
{
    mm_ipc *i_mm;

    GetMmap(a, i_mm, 0);
    return UINT2NUM(i_mm->t->real);
}

static VALUE
mm_empty(a)
    VALUE a;
{
    mm_ipc *i_mm;

    GetMmap(a, i_mm, 0);
    if (i_mm->t->real == 0) return Qtrue;
    return Qfalse;
}

static VALUE
mm_protect_bang(t)
    VALUE *t;
{
    return rb_funcall2(t[0], (ID)t[1], (int)t[2], (VALUE *)t[3]);
}

static VALUE
mm_recycle(str)
    VALUE str;
{
    rb_gc_force_recycle(str);
    return str;
}

static VALUE
mm_i_bang(bang_st)
    mm_bang *bang_st;
{
    VALUE str, res;
    mm_ipc *i_mm;
    
    str = mm_str(bang_st->obj, bang_st->flag);
    if (bang_st->flag & MM_PROTECT) {
	VALUE tmp[4];
	tmp[0] = str;
	tmp[1] = (VALUE)bang_st->id;
	tmp[2] = (VALUE)bang_st->argc;
	tmp[3] = (VALUE)bang_st->argv;
	res = rb_ensure(mm_protect_bang, (VALUE)tmp, mm_recycle, str);
    }
    else {
	res = rb_funcall2(str, bang_st->id, bang_st->argc, bang_st->argv);
	rb_gc_force_recycle(str);
    }
    if (res != Qnil) {
	GetMmap(bang_st->obj, i_mm, 0);
	i_mm->t->real = RSTRING(str)->len;
    }
    return res;
}


static VALUE
mm_bang_i(obj, flag, id, argc, argv)
    VALUE obj, *argv;
    int flag, id, argc;
{
    VALUE res;
    mm_ipc *i_mm;
    mm_bang bang_st;

    GetMmap(obj, i_mm, 0);
    if ((flag & MM_CHANGE) && (i_mm->t->flag & MM_FIXED)) {
	rb_raise(rb_eTypeError, "try to change the size of a fixed map");
    }
    bang_st.obj = obj;
    bang_st.flag = flag;
    bang_st.id = id;
    bang_st.argc = argc;
    bang_st.argv = argv;
    if (i_mm->t->flag & MM_IPC) {
	mm_lock(i_mm, Qtrue);
	res = rb_ensure(mm_i_bang, (VALUE)&bang_st, mm_vunlock, obj);
    }
    else {
	res = mm_i_bang(&bang_st);
    }
    if (res == Qnil) return res;
    return (flag & MM_ORIGIN)?res:obj;

}

#if HAVE_RB_STR_MATCH

static VALUE
mm_match_m(a, b)
    VALUE a, b;
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("match"), 1, &b);
}

#endif

static VALUE
mm_upcase_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("upcase!"), 0, 0);
}

static VALUE
mm_downcase_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("downcase!"), 0, 0);
}

static VALUE
mm_capitalize_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("capitalize!"), 0, 0);
}

static VALUE
mm_swapcase_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("swapcase!"), 0, 0);
}
 
static VALUE
mm_reverse_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_MODIFY, rb_intern("reverse!"), 0, 0);
}

static VALUE
mm_chop_bang(a)
    VALUE a;
{
    return mm_bang_i(a, MM_CHANGE, rb_intern("chop!"), 0, 0);
}

static VALUE
mm_inspect(a)
    VALUE a;
{
    return rb_any_to_s(a);
}

static VALUE
mm_chomp_bang(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("chomp!"), argc, argv);
}

static VALUE
mm_delete_bang(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("delete!"), argc, argv);
}

static VALUE
mm_squeeze_bang(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("squeeze!"), argc, argv);
}

static VALUE
mm_tr_bang(obj, a, b)
    VALUE obj, a, b;
{
    VALUE tmp[2];
    tmp[0] = a;
    tmp[1] = b;
    return mm_bang_i(obj, MM_MODIFY | MM_PROTECT, rb_intern("tr!"), 2, tmp);
}

static VALUE
mm_tr_s_bang(obj, a, b)
    VALUE obj, a, b;
{
    VALUE tmp[2];
    tmp[0] = a;
    tmp[1] = b;
    return mm_bang_i(obj, MM_CHANGE | MM_PROTECT, rb_intern("tr_s!"), 2, tmp);
}

static VALUE
mm_crypt(a, b)
    VALUE a, b;
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("crypt"), 1, &b);
}

static VALUE
mm_include(a, b)
    VALUE a, b;
{
    return mm_bang_i(a, MM_ORIGIN, rb_intern("include?"), 1, &b);
}

static VALUE
mm_index(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("index"), argc, argv);
}

static VALUE
mm_rindex(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("rindex"), argc, argv);
}

static VALUE
mm_aref_m(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("[]"), argc, argv);
}

static VALUE
mm_sum(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("sum"), argc, argv);
}

static VALUE
mm_split(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("split"), argc, argv);
}

static VALUE
mm_count(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    return mm_bang_i(obj, MM_ORIGIN, rb_intern("count"), argc, argv);
}

static VALUE
mm_internal_each(tmp)
    VALUE *tmp;
{
    return rb_funcall2(tmp[0], (ID)tmp[1], (int)tmp[2], (VALUE *)tmp[3]);
}

static VALUE
mm_scan(obj, a)
    VALUE obj, a;
{
    VALUE tmp[4];

    if (!rb_block_given_p()) {
	return rb_funcall(mm_str(obj, MM_ORIGIN), rb_intern("scan"), 1, a);
    }
    tmp[0] = mm_str(obj, MM_ORIGIN);
    tmp[1] = (VALUE)rb_intern("scan");
    tmp[2] = (VALUE)1;
    tmp[3] = (VALUE)&a;
    rb_iterate(mm_internal_each, (VALUE)tmp, rb_yield, 0);
    return obj;
}

static VALUE
mm_each_line(argc, argv, obj)
    int argc;
    VALUE obj, *argv;
{
    VALUE tmp[4];

    tmp[0] = mm_str(obj, MM_ORIGIN);
    tmp[1] = (VALUE)rb_intern("each_line");
    tmp[2] = (VALUE)argc;
    tmp[3] = (VALUE)argv;
    rb_iterate(mm_internal_each, (VALUE)tmp, rb_yield, 0);
    return obj;
}

static VALUE
mm_each_byte(argc, argv, obj)
    int argc;
    VALUE obj, *argv;
{
    VALUE tmp[4];

    tmp[0] = mm_str(obj, MM_ORIGIN);
    tmp[1] = (VALUE)rb_intern("each_byte");
    tmp[2] = (VALUE)argc;
    tmp[3] = (VALUE)argv;
    rb_iterate(mm_internal_each, (VALUE)tmp, rb_yield, 0);
    return obj;
}

static VALUE
mm_undefined(argc, argv, obj)
    int argc;
    VALUE *argv, obj;
{
    rb_raise(rb_eNameError, "not yet implemented");
}

static VALUE
mm_mlockall(obj, flag)
    VALUE obj, flag;
{
    if (mlockall(NUM2INT(flag)) == -1) {
	rb_raise(rb_eArgError, "mlockall(%d)", errno);
    }
    return Qnil;
}

static VALUE
mm_munlockall(obj)
    VALUE obj;
{
    if (munlockall() == -1) {
	rb_raise(rb_eArgError, "munlockall(%d)", errno);
    }
    return Qnil;
}

static VALUE
mm_mlock(obj)
    VALUE obj;
{
    mm_ipc *i_mm;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    if (i_mm->t->flag & MM_LOCK) {
	return obj;
    }
    if (i_mm->t->flag & MM_ANON) {
	rb_raise(rb_eArgError, "mlock(anonymous)");
    }
    if (mlock(i_mm->t->addr, i_mm->t->len) == -1) {
	rb_raise(rb_eArgError, "mlock(%d)", errno);
    }
    i_mm->t->flag |= MM_LOCK;
    return obj;
}

static VALUE
mm_munlock(obj)
    VALUE obj;
{
    mm_ipc *i_mm;

    Data_Get_Struct(obj, mm_ipc, i_mm);
    if (!(i_mm->t->flag & MM_LOCK)) {
	return obj;
    }
    if (munlock(i_mm->t->addr, i_mm->t->len) == -1) {
	rb_raise(rb_eArgError, "munlock(%d)", errno);
    }
    i_mm->t->flag &= ~MM_LOCK;
    return obj;
}

void
Init_mmap()
{
    if (rb_const_defined_at(rb_cObject, rb_intern("Mmap"))) {
	rb_raise(rb_eNameError, "class already defined");
    }
    mm_cMap = rb_define_class("Mmap", rb_cObject);
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
    rb_include_module(mm_cMap, rb_mComparable);
    rb_include_module(mm_cMap, rb_mEnumerable);

#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(mm_cMap, mm_s_alloc);
#else
    rb_define_singleton_method(mm_cMap, "allocate", mm_s_alloc, 0);
#endif
    rb_define_singleton_method(mm_cMap, "new", mm_s_new, -1);
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
    rb_define_method(mm_cMap, "freeze", mm_freeze, 0);
    rb_define_method(mm_cMap, "clone", mm_undefined, -1);
    rb_define_method(mm_cMap, "initialize_copy", mm_undefined, -1);
    rb_define_method(mm_cMap, "dup", mm_undefined, -1);
    rb_define_method(mm_cMap, "<=>", mm_cmp, 1);
    rb_define_method(mm_cMap, "==", mm_equal, 1);
    rb_define_method(mm_cMap, "===", mm_equal, 1);
    rb_define_method(mm_cMap, "eql?", mm_eql, 1);
    rb_define_method(mm_cMap, "hash", mm_hash, 0);
#if HAVE_RB_STR_CASECMP
    rb_define_method(mm_cMap, "casecmp", mm_casecmp, 1);
#endif
    rb_define_method(mm_cMap, "+", mm_undefined, -1);
    rb_define_method(mm_cMap, "*", mm_undefined, -1);
    rb_define_method(mm_cMap, "%", mm_undefined, -1);
    rb_define_method(mm_cMap, "[]", mm_aref_m, -1);
    rb_define_method(mm_cMap, "[]=", mm_aset_m, -1);
#if HAVE_RB_STR_INSERT
    rb_define_method(mm_cMap, "insert", mm_insert, 2);
#endif
    rb_define_method(mm_cMap, "length", mm_size, 0);
    rb_define_method(mm_cMap, "size", mm_size, 0);
    rb_define_method(mm_cMap, "empty?", mm_empty, 0);
    rb_define_method(mm_cMap, "=~", mm_match, 1);
    rb_define_method(mm_cMap, "~", mm_undefined, -1);
#if HAVE_RB_STR_MATCH
    rb_define_method(mm_cMap, "match", mm_match_m, 1);
#endif
    rb_define_method(mm_cMap, "succ", mm_undefined, -1);
    rb_define_method(mm_cMap, "succ!", mm_undefined, -1);
    rb_define_method(mm_cMap, "next", mm_undefined, -1);
    rb_define_method(mm_cMap, "next!", mm_undefined, -1);
    rb_define_method(mm_cMap, "upto", mm_undefined, -1);
    rb_define_method(mm_cMap, "index", mm_index, -1);
    rb_define_method(mm_cMap, "rindex", mm_rindex, -1);
    rb_define_method(mm_cMap, "replace", mm_undefined, -1);

    rb_define_method(mm_cMap, "to_i", mm_undefined, -1);
    rb_define_method(mm_cMap, "to_f", mm_undefined, -1);
    rb_define_method(mm_cMap, "to_sym", mm_undefined, -1);
    rb_define_method(mm_cMap, "to_s", rb_any_to_s, 0);
    rb_define_method(mm_cMap, "to_str", mm_to_str, 0);
    rb_define_method(mm_cMap, "inspect", mm_inspect, 0);
    rb_define_method(mm_cMap, "dump", mm_undefined, -1);

    rb_define_method(mm_cMap, "upcase", mm_undefined, -1);
    rb_define_method(mm_cMap, "downcase", mm_undefined, -1);
    rb_define_method(mm_cMap, "capitalize", mm_undefined, -1);
    rb_define_method(mm_cMap, "swapcase", mm_undefined, -1);

    rb_define_method(mm_cMap, "upcase!", mm_upcase_bang, 0);
    rb_define_method(mm_cMap, "downcase!", mm_downcase_bang, 0);
    rb_define_method(mm_cMap, "capitalize!", mm_capitalize_bang, 0);
    rb_define_method(mm_cMap, "swapcase!", mm_swapcase_bang, 0);

    rb_define_method(mm_cMap, "hex", mm_undefined, -1);
    rb_define_method(mm_cMap, "oct", mm_undefined, -1);
    rb_define_method(mm_cMap, "split", mm_split, -1);
    rb_define_method(mm_cMap, "reverse", mm_undefined, -1);
    rb_define_method(mm_cMap, "reverse!", mm_reverse_bang, 0);
    rb_define_method(mm_cMap, "concat", mm_concat, 1);
    rb_define_method(mm_cMap, "<<", mm_concat, 1);
    rb_define_method(mm_cMap, "crypt", mm_crypt, 1);
    rb_define_method(mm_cMap, "intern", mm_undefined, -1);

    rb_define_method(mm_cMap, "include?", mm_include, 1);

    rb_define_method(mm_cMap, "scan", mm_scan, 1);

    rb_define_method(mm_cMap, "ljust", mm_undefined, -1);
    rb_define_method(mm_cMap, "rjust", mm_undefined, -1);
    rb_define_method(mm_cMap, "center", mm_undefined, -1);

    rb_define_method(mm_cMap, "sub", mm_undefined, -1);
    rb_define_method(mm_cMap, "gsub", mm_undefined, -1);
    rb_define_method(mm_cMap, "chop", mm_undefined, -1);
    rb_define_method(mm_cMap, "chomp", mm_undefined, -1);
    rb_define_method(mm_cMap, "strip", mm_undefined, -1);
#if HAVE_RB_STR_LSTRIP
    rb_define_method(mm_cMap, "lstrip", mm_undefined, -1);
    rb_define_method(mm_cMap, "rstrip", mm_undefined, -1);
#endif

    rb_define_method(mm_cMap, "sub!", mm_sub_bang, -1);
    rb_define_method(mm_cMap, "gsub!", mm_gsub_bang, -1);
    rb_define_method(mm_cMap, "strip!", mm_strip_bang, 0);
#if HAVE_RB_STR_LSTRIP
    rb_define_method(mm_cMap, "lstrip!", mm_lstrip_bang, 0);
    rb_define_method(mm_cMap, "rstrip!", mm_rstrip_bang, 0);
#endif
    rb_define_method(mm_cMap, "chop!", mm_chop_bang, 0);
    rb_define_method(mm_cMap, "chomp!", mm_chomp_bang, -1);

    rb_define_method(mm_cMap, "tr", mm_undefined, -1);
    rb_define_method(mm_cMap, "tr_s", mm_undefined, -1);
    rb_define_method(mm_cMap, "delete", mm_undefined, -1);
    rb_define_method(mm_cMap, "squeeze", mm_undefined, -1);
    rb_define_method(mm_cMap, "count", mm_count, -1);

    rb_define_method(mm_cMap, "tr!", mm_tr_bang, 2);
    rb_define_method(mm_cMap, "tr_s!", mm_tr_s_bang, 2);
    rb_define_method(mm_cMap, "delete!", mm_delete_bang, -1);
    rb_define_method(mm_cMap, "squeeze!", mm_squeeze_bang, -1);

    rb_define_method(mm_cMap, "each_line", mm_each_line, -1);
    rb_define_method(mm_cMap, "each", mm_each_line, -1);
    rb_define_method(mm_cMap, "each_byte", mm_each_byte, -1);

    rb_define_method(mm_cMap, "sum", mm_sum, -1);

    rb_define_method(mm_cMap, "slice", mm_aref_m, -1);
    rb_define_method(mm_cMap, "slice!", mm_slice_bang, -1);
    rb_define_method(mm_cMap, "semlock", mm_semlock, -1);
    rb_define_method(mm_cMap, "ipc_key", mm_ipc_key, 0);
}
