#include "rktio.h"
#include "rktio_private.h"
#include <errno.h>
#include <stdlib.h>
#ifdef RKTIO_SYSTEM_UNIX
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <sys/select.h>
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
# include <windows.h>
# include <string.h>
#endif
#ifdef HAVE_POLL_SYSCALL
# include <poll.h>
#endif

/*========================================================================*/
/* fd struct                                                              */
/*========================================================================*/

struct rktio_fd_t {
  int modes;

#ifdef RKTIO_SYSTEM_UNIX
  intptr_t fd;
# ifdef SOME_FDS_ARE_NOT_SELECTABLE
  int bufcount;
  char buffer[1];
# endif
# ifdef RKTIO_USE_PENDING_OPEN
  struct open_in_thread_t *pending;
# endif
#endif
  
#ifdef RKTIO_SYSTEM_WINDOWS
  union {
    HANDLE fd;
    SOCKET sock; /* when `modes & RKTIO_OPEN_SOCKET` */
  };
  struct Win_FD_Input_Thread *th; /* input mode */
  struct Win_FD_Output_Thread *oth; /* output mode */
  int unblocked; /* whether non-blocking mode is installed */
  int write_limit; /* non-0 => max on amount to try writing */
  char *buffer; /* shared with reading thread */
  int has_pending_byte; /* for text-mode input, may be dropped by a following lf */
  int pending_byte; /* for text-mode input, either a CR waiting to decode, or byte that didn't fit */
  int leftover_len; /* for bytes that should be written/read, but that form a UTF-8 encoding prefix/suffix */
  char leftover[6];
  int w_buf_count; /* number of console wide chars in `buffer` pending to encode */
#endif

  rktio_result_t res;
};

struct rktio_fd_transfer_t {
  rktio_fd_t fd;
};

/*========================================================================*/
/* Windows I/O helper structs                                             */
/*========================================================================*/

#ifdef RKTIO_SYSTEM_WINDOWS

/* There doesn't appear to be a good answer for fd-style waiting on
   input with a Windows console handle.

   When USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT is 0, the
   implementation here treats the input as ready as soon as there are
   events in the console, but those might be key events that haven't
   been submitted by hitting Return. So, a `sync` could return too
   early and reading can block when it shouldn't.

   When USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT is 1, a background
   thread avoids that problem with blocking, but can consume
   characters while waiting, which means the console handle can't be
   shared (with expeditor, for example). */
#define USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT 0

typedef struct Win_FD_Input_Thread {
  /* This is malloced for use in a Win32 thread */
  HANDLE fd;
  int avail, offset, err, checking, you_clean_up;
  int *refcount;
  HANDLE eof;
  unsigned char *buffer;
  HANDLE lock_sema, checking_sema, ready_sema;
  HANDLE thread;
} Win_FD_Input_Thread;

typedef struct Win_FD_Output_Thread {
  /* This is malloced for use in a Win32 thread */
  HANDLE fd;
  int nonblocking;  /* non-zero => an NT pipe where non-blocking WriteFile
		       works. We still use a thread to detect when the
		       write has ben flushed, which in turn is needed to
		       know whether future writes will immediately succeed. */
  int flushed, needflush; /* Used for non-blocking, only. The flushed
			     flag communicates from the flush-testing thread
			     to the main thread. For efficiency, we request
			     flush checking only when needed (instead of
			     after every write); needflush indicates that
			     a flush check is currently needed, but hasn't
			     been started. */
  int done, err_no, you_clean_up;
  unsigned int buflen; /* used for blocking, only */
  unsigned char *buffer; /* used for blocking, only */
  int *refcount;
  HANDLE lock_sema, work_sema, ready_sema;
  HANDLE thread;
} Win_FD_Output_Thread;

# define RKTIO_FD_BUFFSIZE 4096

static void init_read_fd(rktio_t *rktio, rktio_fd_t *rfd);
static void deinit_read_fd(rktio_fd_t *rfd, int full_close);
static void deinit_write_fd(rktio_fd_t *rfd, int full_close);

static void deinit_fd(rktio_fd_t *rfd, int full_close)
{
  deinit_read_fd(rfd, full_close);
  deinit_write_fd(rfd, full_close);
}

#define DELAYED_CONSOLE_HANDLE ((HANDLE)-2)
static void force_console(rktio_fd_t *rfd);
static HANDLE get_std_handle(int which);

static long WINAPI WindowsFDReader(Win_FD_Input_Thread *th);
static void WindowsFDICleanup(Win_FD_Input_Thread *th, int close_mode);

static long WINAPI WindowsFDWriter(Win_FD_Output_Thread *oth);
static void WindowsFDOCleanup(Win_FD_Output_Thread *oth, int close_mode);

typedef BOOL (WINAPI* CSI_proc)(HANDLE);

static CSI_proc get_csi(void)
{
  static int tried_csi = 0;
  static CSI_proc csi;
  
  if (!tried_csi) {
    HMODULE hm;
    hm = LoadLibraryW(L"kernel32.dll");
    if (hm)
      csi = (CSI_proc)GetProcAddress(hm, "CancelSynchronousIo");
    else
      csi = NULL;
    tried_csi = 1;
  }
  return csi;
}

static intptr_t adjust_input_text(rktio_fd_t *rfd, char *buffer, char *is_converted,
				  intptr_t got, intptr_t offset);
static intptr_t adjust_input_text_for_pending_cr(rktio_fd_t *rfd, char *buffer, char *is_converted,
						 intptr_t got);
static const char *adjust_output_text(const char *buffer, intptr_t *towrite);
static intptr_t recount_output_text(const char *orig_buffer, const char *buffer, intptr_t wrote);

static wchar_t *convert_output_wtext(const char *buffer, intptr_t *_towrite,
				     int *_can_leftover, int *_keep_leftover,
				     int leftover_len, char *leftover,
				     int max_convert);
static intptr_t recount_output_wtext(wchar_t *w_buffer, intptr_t winwrote);

#define MIN_VIA_WIDE_BUFFER_SIZE 6
static int read_console_via_wide(HANDLE fd,
                                 wchar_t *w_buffer, DWORD w_buf_len, int *_w_buf_count,
                                 void *dest_buffer, DWORD dest_len, DWORD *_got);

#endif

/*========================================================================*/
/* returning a result                                                     */
/*========================================================================*/

static rktio_result_t *maybe_success(intptr_t r, rktio_result_t *res, int err_val)
{
  if (r != err_val) {
    res->is_success = 1;
    res->success.i = r;
  } else
    res->is_success = 0;

  return res;
}

/*========================================================================*/
/* creating an fd                                                         */
/*========================================================================*/

/* Internally, we use this function as no-error atomic  */
rktio_fd_t *rktio_system_fd(rktio_t *rktio, intptr_t system_fd, int modes)
{
  rktio_fd_t *rfd;

  rfd = calloc(1, sizeof(rktio_fd_t));
  rfd->modes = (modes - (modes & RKTIO_OPEN_INIT));

#ifdef RKTIO_SYSTEM_UNIX
  rfd->fd = system_fd;
  if (!(modes & (RKTIO_OPEN_REGFILE | RKTIO_OPEN_NOT_REGFILE | RKTIO_OPEN_SOCKET))) {
    struct stat buf;
    int cr;
    do {
      cr = fstat(rfd->fd, &buf);
    } while ((cr == -1) && (errno == EINTR));
   if (S_ISREG(buf.st_mode))
      rfd->modes |= RKTIO_OPEN_REGFILE;
   if (!(modes & (RKTIO_OPEN_DIR | RKTIO_OPEN_NOT_DIR)))
     if (S_ISDIR(buf.st_mode))
       rfd->modes |= RKTIO_OPEN_DIR;
  }
#endif

#ifdef RKTIO_SYSTEM_WINDOWS
  if (modes & RKTIO_OPEN_SOCKET)
    rfd->sock = (SOCKET)system_fd;
  else
    rfd->fd = (HANDLE)system_fd;
  if (!(modes & (RKTIO_OPEN_REGFILE | RKTIO_OPEN_NOT_REGFILE | RKTIO_OPEN_SOCKET))
      && (rfd->fd != DELAYED_CONSOLE_HANDLE)) {
    if ((GetFileType(rfd->fd) == FILE_TYPE_DISK))
      rfd->modes |= RKTIO_OPEN_REGFILE;
    if (!(modes & (RKTIO_OPEN_DIR | RKTIO_OPEN_NOT_DIR))) {
      BY_HANDLE_FILE_INFORMATION info;
      if (GetFileInformationByHandle(rfd->fd, &info)) {
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          rfd->modes |= RKTIO_OPEN_DIR;
        }
      }
    }
  }
#endif
  
  if ((modes & RKTIO_OPEN_SOCKET) && (modes & RKTIO_OPEN_INIT))
    rktio_socket_init(rktio, rfd);
  
  if ((modes & RKTIO_OPEN_SOCKET) && (modes & RKTIO_OPEN_OWN))
    rktio_socket_own(rktio, rfd);

  return rfd;
}

int rktio_fd_is_pending_open(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending)
    rktio_pending_open_poll(rktio, rfd, rfd->pending, &rktio->err);
  return !!rfd->pending;
#else
  return 0;
#endif
}

#ifdef RKTIO_USE_PENDING_OPEN
rktio_fd_t *rktio_pending_system_fd(rktio_t *rktio, struct open_in_thread_t *oit, int modes)
{
  rktio_fd_t *rfd;

  rfd = calloc(1, sizeof(rktio_fd_t));
  rfd->modes = (modes - (modes & RKTIO_OPEN_INIT));
  rfd->pending = oit;

  return rfd;
}

void rktio_update_system_fd(rktio_t *rktio, rktio_fd_t *rfd, int fd, int modes)
/* Convert an open-pending rfd to a normal rfd. The given `modes` adds
   to (and completes) the modes provided to `rktio_pending_system_fd`. */
{
  rfd->fd = fd;
  rfd->modes |= modes;
  rfd->pending = NULL;
}
#endif


intptr_t rktio_internal_fd_system_fd(rktio_fd_t *rfd)
{
#ifdef RKTIO_SYSTEM_UNIX
# ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) return -1;
# endif
  return rfd->fd;
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return (intptr_t)rfd->sock;
  else {
    force_console(rfd);
    return (intptr_t)rfd->fd;
  }
#endif
}

intptr_t rktio_fd_system_fd(rktio_t *rktio, rktio_fd_t *rfd)
{
  return rktio_internal_fd_system_fd(rfd);
}

rktio_fd_t *rktio_std_fd(rktio_t *rktio, int which)
{
  int mode = ((which == RKTIO_STDIN)
              ? RKTIO_OPEN_READ
              : RKTIO_OPEN_WRITE);
#ifdef RKTIO_SYSTEM_UNIX
  return rktio_system_fd(rktio, which, mode | RKTIO_OPEN_NOT_DIR);
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  HANDLE h;
  switch (which) {
  case RKTIO_STDIN:
    which = STD_INPUT_HANDLE;
    break;
  case RKTIO_STDOUT:
    which = STD_OUTPUT_HANDLE;
    break;
  case RKTIO_STDERR:
    which = STD_ERROR_HANDLE;
    break;
  }
  h = get_std_handle(which);
  if ((h == INVALID_HANDLE_VALUE) || (h == NULL)) {
    h = DELAYED_CONSOLE_HANDLE; /* => open a console on demand */
  }
  return rktio_system_fd(rktio,
			 (intptr_t)h,
			 mode | RKTIO_OPEN_NOT_DIR);
#endif
}

int rktio_fd_is_regular_file(rktio_t *rktio, rktio_fd_t *rfd)
{
  return ((rfd->modes & RKTIO_OPEN_REGFILE) ? 1 : 0);
}

int rktio_fd_is_directory(rktio_t *rktio, rktio_fd_t *rfd)
{
  return ((rfd->modes & RKTIO_OPEN_DIR) ? 1 : 0);
}

int rktio_fd_is_socket(rktio_t *rktio, rktio_fd_t *rfd)
{
  return ((rfd->modes & RKTIO_OPEN_SOCKET) ? 1 : 0);
}

int rktio_fd_is_udp(rktio_t *rktio, rktio_fd_t *rfd)
{
  return ((rfd->modes & RKTIO_OPEN_UDP) ? 1 : 0);
}

int rktio_fd_is_open_pending(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) return 1;
#endif
  return 0;
}

int rktio_fd_modes(rktio_t *rktio, rktio_fd_t *rfd)
{
  return rfd->modes;
}

int rktio_system_fd_is_terminal(rktio_t *rktio, intptr_t fd)
{
#ifdef RKTIO_SYSTEM_UNIX
  return isatty(fd);
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if ((HANDLE)fd == DELAYED_CONSOLE_HANDLE)
    return 1; /* delayed console */
  else if (GetFileType((HANDLE)fd) == FILE_TYPE_CHAR) {
    DWORD mode;
    if (GetConsoleMode((HANDLE)fd, &mode))
      return 1;
    else
      return 0;
  } else
    return 0;
#endif
}

int rktio_fd_is_terminal(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) return 0;
#endif
  return rktio_system_fd_is_terminal(rktio, (intptr_t)rfd->fd);
}

int rktio_fd_is_text_converted(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_TEXT)
    return 1;
#endif
  return 0;
}

rktio_fd_t *rktio_dup(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_SYSTEM_UNIX
  intptr_t nfd;

# ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) {
    rktio_pending_open_retain(rktio, rfd->pending);
    return rktio_pending_system_fd(rktio, rfd->pending, rfd->modes);
  }
# endif

  rktio_cloexec_lock();

  do {
    nfd = dup(rfd->fd);
  } while (nfd == -1 && errno == EINTR);

  if (nfd != -1)
    rktio_fd_cloexec(nfd);
  else
    get_posix_error();

  rktio_cloexec_unlock();

  if (nfd == -1) {
    return NULL;
  } else {
    /* Set the `RKTIO_OPEN_INIT` flag, because dup()ing a file
       descriptor does not keep all of its properties on some
       platforms (e.g., the non-blocking property on sockets on
       Linux). */
    return rktio_system_fd(rktio, nfd, rfd->modes | RKTIO_OPEN_INIT);
  }
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET) {
    return rktio_socket_dup(rktio, rfd);
  } else {
    HANDLE  newhandle;
    BOOL rc;

    if (rfd->fd == DELAYED_CONSOLE_HANDLE) {
      newhandle = DELAYED_CONSOLE_HANDLE;
      rc = TRUE;
    } else
      rc = DuplicateHandle(GetCurrentProcess(), rfd->fd,
			   GetCurrentProcess(), &newhandle,
			   0, FALSE, DUPLICATE_SAME_ACCESS);

    if (rc == FALSE) {
      get_windows_error();
      return NULL;
    } else {
      return rktio_system_fd(rktio, (intptr_t)newhandle, rfd->modes);
    }
  }
#endif
}


void rktio_fd_cloexec(intptr_t fd) {
#ifdef RKTIO_SYSTEM_UNIX
# ifdef RKTIO_HAS_CLOEXEC
  int flags;
  flags = fcntl(fd, F_GETFD, 0);
  fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
# endif
#endif
}

/*========================================================================*/
/* closing                                                                */
/*========================================================================*/

#ifdef RKTIO_SYSTEM_UNIX
int rktio_reliably_close_err(intptr_t s) {
  int cr;
  do { 
    cr = close(s);
  } while ((cr == -1) && (errno == EINTR));
  return cr;
}

void rktio_reliably_close(intptr_t s)
{
  (void)rktio_reliably_close_err(s);
}
#endif

static rktio_ok_t do_close(rktio_t *rktio /* maybe NULL */, rktio_fd_t *rfd, int set_error)
/* The `rktio` argument can be NULL for a detached file descriptor */
{
  int ok;

#ifdef RKTIO_SYSTEM_UNIX
  int cr;

# ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending)
    cr = rktio_pending_open_release(rktio, rfd->pending);
  else
# endif
    cr = rktio_reliably_close_err(rfd->fd);

# ifdef RKTIO_USE_FCNTL_AND_FORK_FOR_FILE_LOCKS
  if (rktio && !(rfd->modes & RKTIO_OPEN_SOCKET) && (!cr || !set_error))
    rktio_release_lockf(rktio, rfd->fd);
# endif

  if (cr && set_error) {
    get_posix_error();   
    ok = 0;
  } else
    ok = 1;
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_close(rktio, rfd, set_error);

  deinit_fd(rfd, 1);

  ok = 1;
  if (!rfd->th && !rfd->oth && (rfd->fd != DELAYED_CONSOLE_HANDLE)) {
    if (!CloseHandle(rfd->fd)) {
      ok = 0;
      if (set_error)
        get_windows_error();
    }
  }
  
#endif

  if (ok || !set_error)
    free(rfd);

  return ok;
}

rktio_ok_t rktio_close(rktio_t *rktio, rktio_fd_t *rfd)
{
  return do_close(rktio, rfd, 1);
}

void rktio_close_noerr(rktio_t *rktio, rktio_fd_t *rfd)
{
  (void)do_close(rktio, rfd, 0);
}

rktio_ok_t rktio_internal_close(rktio_t *rktio /* may be NULL */, rktio_fd_t *rfd, int set_error)
{
  return do_close(rktio, rfd, set_error);
}

void rktio_forget(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_WINDOWS_SYSTEM
  deinit_fd(rfd, 1);
#endif
  free(rfd);
}

/*========================================================================*/
/* detach, attach, and abandon                                            */
/*========================================================================*/

rktio_fd_transfer_t *rktio_fd_detach(rktio_t *rktio, rktio_fd_t *rfd)
{
#ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending)
    rktio_pending_open_detach(rktio, rfd->pending);
#endif

  return (rktio_fd_transfer_t *)rfd;
}

rktio_fd_t *rktio_fd_attach(rktio_t *rktio, rktio_fd_transfer_t *rfdt)
{
  rktio_fd_t *rfd = (rktio_fd_t *)rfdt;

#ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending)
    rktio_pending_open_attach(rktio, rfd->pending);
#endif

  return rfd;
}

void rktio_fd_close_transfer(rktio_fd_transfer_t *rfdt)
{
  (void)do_close(NULL, (rktio_fd_t *)rfdt, 0);
}

/*========================================================================*/
/* polling                                                                */
/*========================================================================*/

#ifdef SOME_FDS_ARE_NOT_SELECTABLE
static int try_get_fd_char(int fd, int *ready)
{
  int old_flags, c;
  unsigned char buf[1];

  old_flags = fcntl(fd, F_GETFL, 0);
  if (!(old_flags & RKTIO_NONBLOCKING))
    fcntl(fd, F_SETFL, old_flags | RKTIO_NONBLOCKING);
  do {
    c = read(fd, buf, 1);
  } while ((c == -1) && errno == EINTR);
  if (!(old_flags & RKTIO_NONBLOCKING))
    fcntl(fd, F_SETFL, old_flags);

  if (c < 0) {
    *ready = 0;
    return 0;
  } else {
    *ready = 1;
    if (!c)
      return RKTIO_READ_EOF;
    else
      return buf[0];
  }
}
#endif

static int do_poll_read_ready(rktio_t *rktio, rktio_fd_t *rfd, rktio_err_t *err)
{
  if (rktio_fd_is_regular_file(rktio, rfd))
    return RKTIO_POLL_READY;

#ifdef RKTIO_SYSTEM_UNIX
  {
    int r;

# ifdef SOME_FDS_ARE_NOT_SELECTABLE
    if (rfd->bufcount)
      return RKTIO_POLL_READY;
# endif

# ifdef HAVE_POLL_SYSCALL
    struct pollfd pfd[1];
    pfd[0].fd = rfd->fd;
    pfd[0].events = POLLIN;
    do {
      r = poll(pfd, 1, 0);
    } while ((r == -1) && (errno == EINTR));
# else
    DECL_FDSET(readfds, 1);
    DECL_FDSET(exnfds, 1);
    struct timeval time = {0, 0};

    INIT_DECL_RD_FDSET(readfds);
    INIT_DECL_ER_FDSET(exnfds);

    RKTIO_FD_ZERO(readfds);
    RKTIO_FD_ZERO(exnfds);
    RKTIO_FD_SET(rfd->fd, readfds);
    RKTIO_FD_SET(rfd->fd, exnfds);

    do {
      r = select(rfd->fd + 1, RKTIO_FDS(readfds), NULL, RKTIO_FDS(exnfds), &time);
    } while ((r == -1) && (errno == EINTR));
# endif

# ifdef SOME_FDS_ARE_NOT_SELECTABLE
    /* Try a non-blocking read: */
    if (!r && !(rfd->modes & RKTIO_OPEN_SOCKET)) {
      int c, ready;

      c = try_get_fd_char(rfd->fd, &ready);
      if (ready) {
        if (c != RKTIO_READ_EOF) {
          rfd->buffer[0] = (unsigned char)c;
          rfd->bufcount = 1;
        }
        r = 1;
      }
    }
# endif

    return r != 0;
  }
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_poll_read_ready(rktio, rfd, err);

  init_read_fd(rktio, rfd);

  if (rfd->leftover_len)
    return RKTIO_POLL_READY;

  if (!rfd->th) {
    /* No thread, so use wait. See `USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT`
       for why this isn't ready right for console handles. */
    if (WaitForSingleObject(rfd->fd, 0) == WAIT_OBJECT_0)
      return RKTIO_POLL_READY;
  } else {
    /* Has the reader thread pulled in data? */
    WaitForSingleObject(rfd->th->lock_sema, INFINITE);
    if (rfd->th->checking) {
      /* The thread is still trying.
	 Clean up any signals that we may have ignored before. */
      WaitForSingleObject(rfd->th->ready_sema, 0);
    } else if (rfd->th->avail || rfd->th->err || rfd->th->eof) {
      ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
      return RKTIO_POLL_READY; /* other thread found data */
    } else {
      /* Doesn't have anything, and it's not even looking. Tell it
         to look: */
      rfd->th->checking = 1;
      ReleaseSemaphore(rfd->th->checking_sema, 1, NULL);
    }
    ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
  }

  return 0;
#endif
}

int rktio_poll_read_ready(rktio_t *rktio, rktio_fd_t *rfd)
{
  return do_poll_read_ready(rktio, rfd, &rktio->err);
}

rktio_result_t *rktio_poll_read_ready_r(rktio_t *rktio, rktio_fd_t *rfd)
{
  return maybe_success(do_poll_read_ready(rktio, rfd, &rfd->res.err), &rfd->res, RKTIO_POLL_ERROR);
}

static int poll_write_ready_or_flushed(rktio_t *rktio, rktio_fd_t *rfd, int check_flushed, rktio_err_t *err)
{
#ifdef RKTIO_SYSTEM_UNIX
  if (check_flushed)
    return RKTIO_POLL_READY;
  else {
    int sr;

# ifdef RKTIO_USE_PENDING_OPEN
    if (rfd->pending) {
      int errval = rktio_pending_open_poll(rktio, rfd, rfd->pending, err);
      if (errval) {
        errno = errval;
        rktio_get_posix_error(err);
        return RKTIO_POLL_ERROR;
      } else if (rfd->pending)
        return RKTIO_POLL_NOT_READY;
    }
# endif

# ifdef HAVE_POLL_SYSCALL
    struct pollfd pfd[1];
    pfd[0].fd = rfd->fd;
    pfd[0].events = POLLOUT;
    do {
      sr = poll(pfd, 1, 0);
    } while ((sr == -1) && (errno == EINTR));
# else
    DECL_FDSET(writefds, 1);
    DECL_FDSET(exnfds, 1);
    struct timeval time = {0, 0};
  
    INIT_DECL_WR_FDSET(writefds);
    INIT_DECL_ER_FDSET(exnfds);
  
    RKTIO_FD_ZERO(writefds);
    RKTIO_FD_ZERO(exnfds);
    RKTIO_FD_SET(rfd->fd, writefds);
    RKTIO_FD_SET(rfd->fd, exnfds);
  
    do {
      /* Mac OS X 10.8 and 10.9: select() seems to claim that a pipe
         is always ready for output. To work around that problem,
         kqueue() support might be used for pipes, but that has different
         problems. The pol() code above should be used, instead. */
      sr = select(rfd->fd + 1, NULL, RKTIO_FDS(writefds), RKTIO_FDS(exnfds), &time);
    } while ((sr == -1) && (errno == EINTR));
# endif

    if (sr == -1) {
      rktio_get_posix_error(err);
      return RKTIO_POLL_ERROR;
    } else
      return (sr != 0);
  }
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->fd == DELAYED_CONSOLE_HANDLE)
    return RKTIO_POLL_READY;

  if (rfd->modes & RKTIO_OPEN_SOCKET) {
    if (check_flushed)
      return RKTIO_POLL_READY;
    return rktio_socket_poll_write_ready(rktio, rfd, err);
  }
  
  if (rfd->oth) {
    /* Pipe output that can block or needs a background flush */
    int retval;
    Win_FD_Output_Thread *oth = rfd->oth;

    if (check_flushed && rfd->oth->nonblocking) {
      /* Not Windows 95, so any written data really is in the pipe, as
         good as flushed, and we don't really need to ask the thread. */
      return RKTIO_POLL_READY;
    }

    WaitForSingleObject(oth->lock_sema, INFINITE);
    if (oth->nonblocking) {
      if (oth->needflush) {
	oth->needflush = 0;
	oth->flushed = 0;
	ReleaseSemaphore(oth->work_sema, 1, NULL); /* start trying to flush */
	retval = 0;
      } else
	retval = oth->flushed;
      if (!retval) {
	/* While we hold the lock, clear any leftover notifications */
	WaitForSingleObject(oth->ready_sema, 0);
      }
    } else {
      /* Using separate writing thread for Windows 95 */
      if (oth->err_no) {
	/* Delay error report until next write */
	retval = 1;
      } else
	retval = (check_flushed
		  ? !oth->buflen
		  : (oth->buflen < RKTIO_FD_BUFFSIZE));
      if (!retval && !check_flushed) {
	/* clear any leftover notifications */
	WaitForSingleObject(oth->ready_sema, 0);
      }
    }
    ReleaseSemaphore(oth->lock_sema, 1, NULL);

    return (retval ? RKTIO_POLL_READY : 0);
  } else
    return RKTIO_POLL_READY; /* non-blocking output, such as a console, or haven't written yet */
#endif
}

int rktio_poll_write_ready(rktio_t *rktio, rktio_fd_t *rfd)
{
  return poll_write_ready_or_flushed(rktio, rfd, 0, &rktio->err);
}

int rktio_poll_write_flushed(rktio_t *rktio, rktio_fd_t *rfd)
{
  return poll_write_ready_or_flushed(rktio, rfd, 1, &rktio->err);
}

rktio_result_t *rktio_poll_write_ready_r(rktio_t *rktio, rktio_fd_t *rfd)
{
  return maybe_success(poll_write_ready_or_flushed(rktio, rfd, 0, &rfd->res.err), &rfd->res, RKTIO_POLL_ERROR);
}

rktio_result_t *rktio_poll_write_flushed_r(rktio_t *rktio, rktio_fd_t *rfd)
{
  return maybe_success(poll_write_ready_or_flushed(rktio, rfd, 1, &rfd->res.err), &rfd->res, RKTIO_POLL_ERROR);
}

rktio_result_t *rktio_poll_read_ready_r(rktio_t *rktio, rktio_fd_t *rfd);
RKTIO_EXTERN_RESULT(rktio_result_integer)
rktio_result_t *rktio_poll_write_ready_r(rktio_t *rktio, rktio_fd_t *rfd);


void rktio_poll_add(rktio_t *rktio, rktio_fd_t *rfd, rktio_poll_set_t *fds, int modes)
{
#ifdef RKTIO_SYSTEM_UNIX
  rktio_poll_set_t *fds2;

# ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) {
    rktio_poll_add_pending_open(rktio, rfd, rfd->pending, fds);
    return;
  }
# endif

  if (modes & RKTIO_POLL_READ) {
    RKTIO_FD_SET(rfd->fd, fds);
  }
  if (modes & RKTIO_POLL_WRITE) {
    fds2 = RKTIO_GET_FDSET(fds, 1);
    RKTIO_FD_SET(rfd->fd, fds2);
  }
  fds2 = RKTIO_GET_FDSET(fds, 2);
  RKTIO_FD_SET(rfd->fd, fds2);
#endif  
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET) {
    /* RKTIO_FD_SET(), etc., for Windows expects sockets */
    rktio_poll_set_t *fds2;

    if (modes & RKTIO_POLL_READ) {
      RKTIO_FD_SET((intptr_t)rfd->sock, fds);
    }
    if (modes & RKTIO_POLL_WRITE) {
      fds2 = RKTIO_GET_FDSET(fds, 1);
      RKTIO_FD_SET((intptr_t)rfd->sock, fds2);
    }
    fds2 = RKTIO_GET_FDSET(fds, 2);
    RKTIO_FD_SET((intptr_t)rfd->sock, fds2);
  } else {
    force_console(rfd);

    if (modes & RKTIO_POLL_READ) {
      init_read_fd(rktio, rfd);
      if (rfd->th) {
	WaitForSingleObject(rfd->th->lock_sema, INFINITE);
        if (!rfd->th->checking) {
          if (rfd->th->avail || rfd->th->err || rfd->th->eof) {
            /* Data is ready. We shouldn't be trying to sleep, so force an
               immediate wake-up: */
	    ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
            rktio_poll_set_add_nosleep(rktio, fds);
          } else {
	    /* Ask the reader thread to start checking for data */
            rfd->th->checking = 1;
            ReleaseSemaphore(rfd->th->checking_sema, 1, NULL);
	    /* clear any notifications that we may have ignored before */
	    WaitForSingleObject(rfd->th->ready_sema, 0);
	    ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
            rktio_poll_set_add_handle(rktio, (intptr_t)rfd->th->ready_sema, fds, 1);
          }
        } else {
	  ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
          rktio_poll_set_add_handle(rktio, (intptr_t)rfd->th->ready_sema, fds, 1);
	}
      } else if (rktio_fd_is_regular_file(rktio, rfd)) {
        /* regular files never block */
        rktio_poll_set_add_nosleep(rktio, fds);
      } else {
        /* We get here for console input with
	   USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT is 0, for
	   example.  */
	if (rfd->leftover_len)
	  rktio_poll_set_add_nosleep(rktio, fds);
	else {
	  /* This  is not really right for consoles.
	     See `USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT`. */
	  rktio_poll_set_add_handle(rktio, (intptr_t)rfd->fd, fds, 0);
	}
      }
    }

    if (modes & RKTIO_POLL_WRITE) {
      if (rfd->oth && !rktio_poll_write_ready(rktio, rfd))
        rktio_poll_set_add_handle(rktio, (intptr_t)rfd->oth->ready_sema, fds, 1);
      else
        rktio_poll_set_add_nosleep(rktio, fds);
    }

    if (modes & RKTIO_POLL_FLUSH) {
      if (rfd->oth && !rktio_poll_write_flushed(rktio, rfd))
        rktio_poll_set_add_handle(rktio, (intptr_t)rfd->oth->ready_sema, fds, 1);
      else
        rktio_poll_set_add_nosleep(rktio, fds);
    }
  }
#endif
}

/*========================================================================*/
/* reading                                                                */
/*========================================================================*/

static intptr_t do_read_converted(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t len,
                                  char *is_converted, rktio_err_t *err)
{
#ifdef RKTIO_SYSTEM_UNIX
  intptr_t bc;

  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_read(rktio, rfd, buffer, len, err);

# ifdef SOME_FDS_ARE_NOT_SELECTABLE
  if (rfd->bufcount && len) {
    buffer[0] = rfd->buffer[0];
    rfd->bufcount = 0;
    return 1;
  }
# endif

  len = LIMIT_REQUEST_SIZE(len);

  if (rktio_fd_is_regular_file(rktio, rfd)) {
    /* Reading regular file never blocks */
    do {
      bc = read(rfd->fd, buffer, len);
    } while ((bc == -1) && (errno == EINTR));

    if (bc == -1) {
      rktio_get_posix_error(err);
      return RKTIO_READ_ERROR;
    } else if (bc == 0)
      return RKTIO_READ_EOF;
    else
      return bc;
  } else {
    int old_flags;

    /* Check that input is available. This step is needed
       for fifos to avoid getting an EOF-looking result
       if there's no writer, yet. */
    if (!rktio_poll_read_ready(rktio, rfd))
      return 0;
    
    old_flags = fcntl(rfd->fd, F_GETFL, 0);
    if (!(old_flags & RKTIO_NONBLOCKING))
      fcntl(rfd->fd, F_SETFL, old_flags | RKTIO_NONBLOCKING);
    
    /* We use a non-blocking read here, even though we've waited
       for input above, because an external process might have
       gobbled the characters that we expected to get. */
    do {
      bc = read(rfd->fd, buffer, len);
    } while ((bc == -1) && errno == EINTR);

    if ((bc == -1) && (errno != EAGAIN))
      rktio_get_posix_error(err);
    
    if (!(old_flags & RKTIO_NONBLOCKING))
      fcntl(rfd->fd, F_SETFL, old_flags);
    
    if (bc == -1) {
      if (errno == EAGAIN)
        return 0; /* no bytes from a non-blocking read */
      else
        return RKTIO_READ_ERROR;
    } else if (bc == 0)
      return RKTIO_READ_EOF;
    else
      return bc;
  }
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_read(rktio, rfd, buffer, len, err);

  init_read_fd(rktio, rfd);

  if (!rfd->th) {
    /* We can read directly. This must be a regular file, where
       reading never blocks, or it's a console, where we can check. */
    DWORD rgot, offset = 0;
    int ready;

    if (rktio_fd_is_regular_file(rktio, rfd))
      ready = -1; /* => regfile */
    else {
      if (rfd->leftover_len)
        ready = 1;
      else {
	/* See `USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT` for why this
	   isn't ready right for console handles. */
	ready = (WaitForSingleObject((HANDLE)rfd->fd, 0) == WAIT_OBJECT_0);
      }
    }

    if (rfd->has_pending_byte) {
      if (!len)
        return 0;
      buffer[0] = rfd->pending_byte;
      if (len == 1) {
        if (rfd->pending_byte == '\r') {
          /* We have to read one more byte and then decode,
             shifting the new byte into pending position
             if it's not '\n' */
          if (!ready) return 0;
        } else {
          if (is_converted) is_converted[0] = 0;
          rfd->has_pending_byte = 0;
          return 1;
        }
      } else {
        if (!ready) return 0;
        /* read after first byte installed into the buffer */
        rfd->has_pending_byte = 0;
        offset = 1;
      }
    }

    if (ready < 0)
      ready = ReadFile((HANDLE)rfd->fd, buffer + offset, len - offset, &rgot, NULL);
    else {
      DWORD amt = len - offset;
      if (rfd->leftover_len) {
        if (amt > rfd->leftover_len)
          rgot = rfd->leftover_len;
        else
          rgot = amt;
        memcpy(buffer + offset, rfd->leftover, rgot);
        rfd->leftover_len -= rgot;
        if (rfd->leftover_len)
          memmove(rfd->leftover, rfd->leftover + rgot, rfd->leftover_len);
        ready = 1;
      } else if (amt >= MIN_VIA_WIDE_BUFFER_SIZE) {
        /* limit reading to the max that definitely fits in `amt` */
        DWORD w_fill_len = amt / MIN_VIA_WIDE_BUFFER_SIZE;
        if (w_fill_len > (RKTIO_FD_BUFFSIZE / sizeof(wchar_t))) {
          /* at the same time, we mus not overflow the wide-character buffer */
          w_fill_len = (RKTIO_FD_BUFFSIZE / sizeof(wchar_t));
        }
        ready = read_console_via_wide(rfd->fd,
                                      (wchar_t *)rfd->buffer, w_fill_len, &rfd->w_buf_count,
                                      buffer + offset, len - offset, &rgot);
      } else {
        /* we have to read into a buffer that is definitely large enough to decode
           one wide character; they may be leftover bytes if `amt` isn't big enough
           for the decoding */
        ready = read_console_via_wide(rfd->fd,
                                      (wchar_t *)rfd->buffer, 1, &rfd->w_buf_count,
                                      rfd->leftover, MIN_VIA_WIDE_BUFFER_SIZE, &rgot);
        if (ready && rgot) {
          if (amt > rgot) amt = rgot;
          memcpy(buffer, rfd->leftover, amt);
          rfd->leftover_len = rgot - amt;
          memmove(rfd->leftover, rfd->leftover + amt, rfd->leftover_len);
          rgot = amt;
        }
      }
    }

    if (!ready) {
      rktio_get_windows_error(err);
      return RKTIO_READ_ERROR;
    }

    rgot += offset;
    
    if (rfd->has_pending_byte) {
      /* We had a buffer of size 1 and a pending '\r'... */
      return adjust_input_text_for_pending_cr(rfd, buffer, is_converted, rgot);
    } else if (!rgot)
      return RKTIO_READ_EOF;
    else if (rfd->modes & RKTIO_OPEN_TEXT)
      return adjust_input_text(rfd, buffer, is_converted, rgot, offset);
    else
      return rgot;
  } else {
    if (!rktio_poll_read_ready(rktio, rfd))
      return 0;
    
    /* If we get this far, there should be data available.
       Extract data made available by the reader thread. */
    WaitForSingleObject(rfd->th->lock_sema, INFINITE);
    if (rfd->th->eof) {
      if (rfd->th->eof != INVALID_HANDLE_VALUE) {
        ReleaseSemaphore(rfd->th->eof, 1, NULL);
        rfd->th->eof = NULL;
      }
      ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
      return RKTIO_READ_EOF;
    } else if (rfd->th->err) {
      ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
      rktio_set_windows_error(err, rfd->th->err);
      return RKTIO_READ_ERROR;
    } else {
      intptr_t bc = rfd->th->avail;
      if (bc > len)
	bc = len;
      rfd->th->avail -= bc;
      memcpy(buffer, rfd->buffer + rfd->th->offset, bc);
      rfd->th->offset += bc;
      ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
      return bc;
    }
  }
#endif
}

intptr_t rktio_read_converted(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t len,
                              char *is_converted)
{
  return do_read_converted(rktio, rfd, buffer, len, is_converted, &rktio->err);
}

intptr_t rktio_read_converted_in(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t start, intptr_t end,
                                 char *is_converted, intptr_t converted_start)
{
  return do_read_converted(rktio, rfd, buffer+start, end-start, is_converted+converted_start, &rktio->err);
}

rktio_result_t *rktio_read_converted_in_r(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t start, intptr_t end,
                                          char *is_converted, intptr_t converted_start)
{
  return maybe_success(do_read_converted(rktio, rfd, buffer+start, end-start, is_converted+converted_start,
                                         &rfd->res.err),
                       &rfd->res,
                       RKTIO_READ_ERROR);
}

intptr_t rktio_read(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t len)
{
  return rktio_read_converted(rktio, rfd, buffer, len, NULL);
}

intptr_t rktio_read_in(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t start, intptr_t end)
{
  return rktio_read_converted(rktio, rfd, buffer+start, end-start, NULL);
}

rktio_result_t *rktio_read_in_r(rktio_t *rktio, rktio_fd_t *rfd, char *buffer, intptr_t start, intptr_t end)
{
  return maybe_success(do_read_converted(rktio, rfd, buffer+start, end-start, NULL, &rfd->res.err),
                       &rfd->res,
                       RKTIO_READ_ERROR);
}

RKTIO_EXTERN intptr_t rktio_buffered_byte_count(rktio_t *rktio, rktio_fd_t *fd)
{
#ifdef RKTIO_SYSTEM_UNIX
# ifdef SOME_FDS_ARE_NOT_SELECTABLE
  return fd->bufcount;
# else
  return 0;
# endif
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  return ((fd->has_pending_byte ? 1 : 0) + fd->leftover_len);
#endif
}

#ifdef RKTIO_SYSTEM_WINDOWS

static intptr_t adjust_input_text(rktio_fd_t *rfd, char *buffer, char *is_converted,
				  intptr_t got, intptr_t offset)
{
  int i, j;

  if (got && (buffer[got-1] == '\r') && !offset) {
    /* Save '\r' that might be followed by '\n' */
    rfd->pending_byte = '\r';
    rfd->has_pending_byte = 1;
    --got;
  }
  
  for (i = 0, j = 0; i < got-1; i++) {
    if ((buffer[i] == '\r') && (buffer[i+1] == '\n')) {
      if (is_converted) is_converted[j] = 1;
      buffer[j++] = '\n';
      i++;
    } else {
      if (is_converted) is_converted[j] = 0;
      buffer[j++] = buffer[i];
    }
  }
  if (i < got) {
    if (is_converted) is_converted[j] = 0;
    buffer[j++] = buffer[i];
  }

  return j;
}

static intptr_t adjust_input_text_for_pending_cr(rktio_fd_t *rfd, char *buffer, char *is_converted,
						 intptr_t got)
{
  if (!got) {
    /* There are no more bytes to decode. Report the final '\r' by itself */
    buffer[0] = '\r';
    rfd->has_pending_byte = 0;
    if (is_converted) is_converted[0] = 0;
    return 1;
  } else {
    /* Combine the one new byte with a preceding '\r' */
    if (buffer[0] == '\n') {
      /* Decode */
      rfd->has_pending_byte = 0;
      if (is_converted) is_converted[0] = 1;
      return 1;
    } else {
      /* Save the new byte as pending while returning a '\r' by itself */
      rfd->pending_byte = buffer[0];
      buffer[0] = '\r';
      if (is_converted) is_converted[0] = 0;
      return 1;
    }
  }
}

static void init_read_fd(rktio_t *rktio, rktio_fd_t *rfd)
{
  force_console(rfd);

  if (rfd->th)
    return;
  else if (rktio_fd_is_regular_file(rktio, rfd)) {
    return;
  } else if (!USE_BACKGROUND_THREAD_FOR_CONSOLE_INPUT
	     && rktio_fd_is_terminal(rktio, rfd)) {
    if (!rfd->buffer) {
      char *bfr;
      bfr = malloc(RKTIO_FD_BUFFSIZE);
      rfd->buffer = bfr;
      rfd->w_buf_count = 0;
    }
    return;
  } else {
    /* To get non-blocking I/O for anything that can block, we create
       a separate reader thread.

       Yes, Windows NT pipes support non-blocking reads, but there
       doesn't seem to be any way to use WaitForSingleObject to sleep
       until characters are ready. PeekNamedPipe can be used for
       polling, but not sleeping. */

    Win_FD_Input_Thread *th;
    DWORD id;
    HANDLE h;
    HANDLE sm;
    char *bfr;

    th = calloc(1, sizeof(Win_FD_Input_Thread));
    rfd->th = th;

    bfr = malloc(RKTIO_FD_BUFFSIZE);
    th->buffer = (unsigned char *)bfr;
    rfd->buffer = bfr;

    th->fd = rfd->fd;
    th->avail = 0;
    th->offset = 0;
    th->err = 0;
    th->eof = NULL;
    th->checking = 0;
    
    sm = CreateSemaphore(NULL, 1, 1, NULL);
    th->lock_sema = sm;
    sm = CreateSemaphore(NULL, 0, 1, NULL);
    th->checking_sema = sm;
    sm = CreateSemaphore(NULL, 0, 1, NULL);
    th->ready_sema = sm;

    h = CreateThread(NULL, 4096, (LPTHREAD_START_ROUTINE)WindowsFDReader, th, 0, &id);

    th->thread = h;
  }
}

static void deinit_read_fd(rktio_fd_t *rfd, int full_close)
{
  if (rfd->th) {
    CSI_proc csi;

    WaitForSingleObject(rfd->th->lock_sema, INFINITE);
    
    /* -1 for checking means "shut down" */
    rfd->th->checking = -1;
    ReleaseSemaphore(rfd->th->checking_sema, 1, NULL);

    if (rfd->th->eof && (rfd->th->eof != INVALID_HANDLE_VALUE)) {
      ReleaseSemaphore(rfd->th->eof, 1, NULL);
      rfd->th->eof = NULL;
    }

    csi = get_csi();
    if (csi) {
      /* Helps thread wake up. Otherwise, it's possible for the
         thread to stay stuck trying to read, in which case the
         file handle (probably a pipe) doesn't get closed. */
      csi(rfd->th->thread);
    }

    /* Try to get out of cleaning up the records (since they can't be
       cleaned until the thread is also done: */
    if (rfd->th->you_clean_up) {
      /* The other thread exited and left us with clean-up: */
      WindowsFDICleanup(rfd->th, (full_close ? 1 : -1));
    } else {
      /* otherwise, thread is responsible for clean-up */
      rfd->th->you_clean_up = (full_close ? 1 : -1);
      ReleaseSemaphore(rfd->th->lock_sema, 1, NULL);
    }

    rfd->buffer = NULL;
  } else if (rfd->buffer) {
    free(rfd->buffer);
    rfd->buffer = NULL;
  }
}

static long WINAPI WindowsFDReader(Win_FD_Input_Thread *th)
{
  DWORD toget, got;
  int perma_eof = 0, ft, is_console = 0;
  HANDLE eof_wait = NULL;
# define CONSOLE_BUFFER_IN_SIZE 16
  wchar_t w_buffer[CONSOLE_BUFFER_IN_SIZE];
  int w_buf_count = 0;

  ft = GetFileType((HANDLE)th->fd);
    
  if (ft == FILE_TYPE_PIPE) {
    /* Reading from a pipe will return early when data is available. */
    toget = RKTIO_FD_BUFFSIZE;
  } else {
    /* Non-pipe: get one char at a time: */
    toget = 1;
    is_console = (ft == FILE_TYPE_CHAR);
  }

  while (!perma_eof && !th->err) {
    /* Wait until we're supposed to look for input: */
    WaitForSingleObject(th->checking_sema, INFINITE);

    WaitForSingleObject(th->lock_sema, INFINITE);
    if (th->checking < 0)
      break;

    if (th->avail) {
      /* Spurious wake-up? */
      ReleaseSemaphore(th->lock_sema, 1, NULL);
    } else {
      int ok;
      ReleaseSemaphore(th->lock_sema, 1, NULL);
      if (!is_console)
	ok = ReadFile(th->fd, th->buffer, toget, &got, NULL);
      else
        ok = read_console_via_wide(th->fd, w_buffer, CONSOLE_BUFFER_IN_SIZE, &w_buf_count,
                                   th->buffer, RKTIO_FD_BUFFSIZE, &got);
      if (ok) {
	WaitForSingleObject(th->lock_sema, INFINITE);
	th->avail = got;
	th->offset = 0;
	if (!got) {
	  /* We interpret a send of 0 bytes as a mid-stream EOF. */
	  eof_wait = CreateSemaphore(NULL, 0, 1, NULL);
	  th->eof = eof_wait;
	}
	/* lock is still held... */
      } else {
	int err;
	err = GetLastError();
	WaitForSingleObject(th->lock_sema, INFINITE);
	if (err == ERROR_BROKEN_PIPE) {
	  th->eof = INVALID_HANDLE_VALUE;
	  perma_eof = 1;
	} else
	  th->err = err;
	/* lock is still held... */
      }

      th->checking = 0;

      /* Notify main program that we found something: */
      ReleaseSemaphore(th->ready_sema, 1, NULL);

      if (!perma_eof && !th->err) {
	ReleaseSemaphore(th->lock_sema, 1, NULL);
      
	if (eof_wait) {
	  WaitForSingleObject(eof_wait, INFINITE);
	  CloseHandle(eof_wait);
	  eof_wait = NULL;
	}
      }
    }
  }
  /* lock is still held on `break` out of loop */

  /* We have to clean up if the main program has abandoned us: */
  if (th->you_clean_up) {
    WindowsFDICleanup(th, th->you_clean_up);
  } else {
    /* otherwise, main program is responsible for clean-up */
    th->you_clean_up = 1;
    ReleaseSemaphore(th->lock_sema, 1, NULL);
  }

  return 0;
}

static void WindowsFDICleanup(Win_FD_Input_Thread *th, int close_mode)
{
  CloseHandle(th->lock_sema);
  CloseHandle(th->checking_sema);
  CloseHandle(th->ready_sema);

  if (close_mode != -1)
    CloseHandle(th->fd);

  free(th->buffer);
  free(th);
}

/* result is 0 for fail, non-zero for success */
int read_console_via_wide(HANDLE fd,
                          /* a buffer for pending wide characters; limit the amount of
                             lookahead via `w_fill_len`; the `*_w_buf_count`argument
                             says how many chars are there on entry and exit */
                          wchar_t *w_buffer, DWORD w_fill_len, int *_w_buf_count,
                          /* a buffer to receive encoded UTF-8; this
                             buffer needs to be at least `MIN_VIA_WIDE_BUFFER_SIZE` bytes
                             to ensure decoding progress */
                          void *dest_buffer, DWORD dest_len, DWORD *_got) {
  int w_buf_count = *_w_buf_count;
  int ok;
  DWORD got;
  
  if (w_buf_count) {
    ok = 1;
    got = w_buf_count;
  } else
    ok = ReadConsoleW(fd, w_buffer, w_fill_len, &got, NULL);
  if (ok) {
    /* check for Ctl-Z, and convert it to an EOF */
    int i, move_start = 0, move_len = 0;
    for (i = 0; i < got; i++) {
      if (w_buffer[i] == 26 /* ctl-z */) {
        move_start = i;
        move_len = got - i;
        if (i == 0) {
          /* report EOF now */
          move_start++;
          move_len--;
        }
        got = i;
        break;
      }
    }
    got = WideCharToMultiByte(CP_UTF8, 0, w_buffer, got, dest_buffer, dest_len, NULL, 0);
    if (move_len > 0)
      memmove(w_buffer, w_buffer + move_start, sizeof(wchar_t) * move_len);
    w_buf_count = move_len;
  }

  *_w_buf_count = w_buf_count;
  *_got = got;

  return ok;
}

#endif

/*========================================================================*/
/* writing                                                                */
/*========================================================================*/

static intptr_t do_write(rktio_t *rktio, rktio_fd_t *rfd, const char *buffer, intptr_t len,
                         rktio_err_t *err)
{
#ifdef RKTIO_SYSTEM_UNIX
  int flags, errsaved;
  intptr_t amt;

  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_write(rktio, rfd, buffer, len, err);

# ifdef RKTIO_USE_PENDING_OPEN
  if (rfd->pending) {
    int errval = rktio_pending_open_poll(rktio, rfd, rfd->pending, err);
    if (errval) {
      errno = errval;
      rktio_get_posix_error(err);
      return RKTIO_WRITE_ERROR;
    } else if (rfd->pending)
      return 0;
  }
# endif

  flags = fcntl(rfd->fd, F_GETFL, 0);
  if (!(flags & RKTIO_NONBLOCKING))
    fcntl(rfd->fd, F_SETFL, flags | RKTIO_NONBLOCKING);

  amt = LIMIT_REQUEST_SIZE(len);

  do {
    do {
      len = write(rfd->fd, buffer, amt);
    } while ((len == -1) && (errno == EINTR));
    
    /* If there was no room to write `amt` bytes, then it's
       possible that writing fewer bytes will succeed. That seems
       to be the case with FIFOs on Mac OS X, for example. */
    amt = amt >> 1;
  } while ((len == -1) && (errno == EAGAIN) && (amt > 0));

  if (len == -1) {
    errsaved = errno;
    rktio_get_posix_error(err);
  } else
    errsaved = 0;

  if (!(flags & RKTIO_NONBLOCKING))
    fcntl(rfd->fd, F_SETFL, flags);

  if (len == -1) {
    if (errsaved == EAGAIN)
      return 0;
    else
      return RKTIO_WRITE_ERROR;
  } else
    return len;
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  DWORD winwrote;

  if (rfd->modes & RKTIO_OPEN_SOCKET)
    return rktio_socket_write(rktio, rfd, buffer, len, err);

  force_console(rfd);

  if (rktio_fd_is_regular_file(rktio, rfd)
      || rktio_fd_is_terminal(rktio, rfd)) {
    /* Regular files never block, so this code looks like the Unix
       code. */

    /* If we try to write too much at once, the result
       is ERROR_NOT_ENOUGH_MEMORY (as opposed to a partial write). */
    int ok, to_console, can_leftover = 0, keep_leftover = 0;
    intptr_t towrite = len;
    const char *orig_buffer = buffer;
    wchar_t *w_buffer = NULL;
    DWORD max_winwrote;
    int errv;

    if (rfd->modes & RKTIO_OPEN_TEXT)
      buffer = adjust_output_text(buffer, &towrite);

    max_winwrote = towrite;
  
    to_console = rktio_fd_is_terminal(rktio, rfd);
    if (to_console) {
      /* Decode UTF-8 and write a chunk on a character boundary. */
      w_buffer = convert_output_wtext(buffer, &towrite,
				      &can_leftover, &keep_leftover,
				      rfd->leftover_len, rfd->leftover,
				      1024);
    }
    
    while (1) {
      if (!to_console)
	ok = WriteFile((HANDLE)rfd->fd, buffer, towrite, &winwrote, NULL);
      else {
	if (towrite)
	  ok = WriteConsoleW((HANDLE)rfd->fd, w_buffer, towrite, &winwrote, NULL);
	else {
	  /* can happen if can_leftover is > 0 */
	  ok = 1;
	  winwrote = 0;
	}
      }

      if (!ok)
        errv = GetLastError();

      if (!ok && (errv == ERROR_NOT_ENOUGH_MEMORY)) {
        towrite = towrite >> 1;
        if (towrite && (buffer != orig_buffer)) {
          /* don't write half of a CRLF: */
          if ((buffer[towrite-1] == '\r') && (buffer[towrite-1] == '\n'))
            --towrite;
        }
        if (!towrite)
          break;
      } else
        break;
    }

    if (!ok) {
      rktio_get_windows_error(errv);
      if (buffer != orig_buffer)
	free((char *)buffer);
      return RKTIO_WRITE_ERROR;
    }

    if (to_console) {
      /* Convert wchar count to byte count, taking into account leftovers */
      int wrote_all = (winwrote == towrite);
      if (winwrote) {
	/* Recounting only works right if the output was well-formed
	   UTF-8. Weird things happen otherwise... but we guard against
	   external inconsistency with the `max_winwrote` check below. */
	winwrote = recount_output_wtext(w_buffer, winwrote);
	if (winwrote > rfd->leftover_len)
	  winwrote -= rfd->leftover_len;
	else
	  winwrote = 0;
	rfd->leftover_len = 0;
      }
      if (wrote_all && can_leftover) {
	memcpy(rfd->leftover + keep_leftover, buffer + max_winwrote - can_leftover, can_leftover);
	rfd->leftover_len = can_leftover + keep_leftover;
	winwrote += can_leftover;
      }
      free(w_buffer);
      if (winwrote > max_winwrote)
	winwrote = max_winwrote;
    }

    if (buffer != orig_buffer) {
      /* Convert converted count back to original count: */
      winwrote = recount_output_text(orig_buffer, buffer, winwrote);
      free((char *)buffer);
    }

    return winwrote;
  } else {
    /* If we don't have a thread yet, we'll need to start it. If
       we have a non-blocking pipe, we can try the write (and
       we'll still need the thread to determine when the data is
       flushed). */
    intptr_t out_len = 0;
    int ok, errsaved;

    if (!rfd->oth || rfd->oth->nonblocking) {
      int nonblocking;

      /* If we don't have a thread, this is our first write attempt.
         Determine whether this is a non-blocking pipe: */
      if (!rfd->oth) {
        /* The FILE_TYPE_PIPE test is currently redundant, I think,
           but better safe than sorry. */
        nonblocking = (rktio->windows_nt_or_later
                       && (GetFileType((HANDLE)rfd->fd) == FILE_TYPE_PIPE));
      } else
        nonblocking = 1; /* must be, or we would not have gotten here */
    
      if (nonblocking) {
        /* Unless we're still trying to flush old data, write to the
           pipe and have the other thread start flushing it. */
        int flushed;

        if (rfd->oth) {
          if (rfd->oth->needflush) {
	    /* Not flushed, but we haven't promised to make progress: */
            flushed = 1;
          } else {
            WaitForSingleObject(rfd->oth->lock_sema, INFINITE);
            flushed = rfd->oth->flushed;
            ReleaseSemaphore(rfd->oth->lock_sema, 1, NULL);
          }
        } else
          flushed = 1; /* haven't written anything before */

        if (flushed) {
          /* Put the pipe in non-blocking mode and write. */
          int towrite;

          towrite = len;

          /* Apparently, the semantics of non-blocking pipe writes
             is not partial writes, but giving up entirely when
             the other end isn't being read. In other words, if we
             try to write too much and nothing is being pulled
             from the pipe, winwrote will be set to 0. Also, if
             we try to write too much at once, the result is a
             ERROR_NOT_ENOUGH_MEMORY error. Account for these
             behaviors by trying to write less each iteration when the
             write fails. (Yuck.) */
          while (1) {
            if (!rfd->unblocked) {
	      DWORD nonblock = PIPE_NOWAIT;
              ok = SetNamedPipeHandleState((HANDLE)rfd->fd, &nonblock, NULL, NULL);
              if (ok)
                rfd->unblocked = 1;
              else {
                errsaved = GetLastError();
		if (errsaved == ERROR_INVALID_FUNCTION) {
		  /* The handle (not a pipe?) doesn't support non-blocking mode. But
		     since we only try to write when the pipe is flushed, we can just
		     keep each request under the buffer size. */
		  DWORD bufsz;
		  if (GetNamedPipeInfo((HANDLE)rfd->fd, NULL, &bufsz, NULL, NULL)
		      && (bufsz > 0))
		    rfd->write_limit = bufsz;
		  else {
		    /* 256 should be small enough? */
		    rfd->write_limit = 256;
		  }
		  rfd->unblocked = 1;
		  ok = 1;
		} 
	      }
            } else
              ok = 1;

	    if (rfd->write_limit && (towrite > rfd->write_limit))
	      towrite = rfd->write_limit;

            if (ok) {
              ok = WriteFile((HANDLE)rfd->fd, buffer, towrite, &winwrote, NULL);
              if (!ok)
                errsaved = GetLastError();
            }

            if ((ok && !winwrote)
                || (!ok && (errsaved == ERROR_NOT_ENOUGH_MEMORY))
                /* it doesn't seem like `ERROR_PIPE_BUSY` should happen
                   in response to `WriteFile`, but it seems like this
                   might be happening (and this check is harmless if
                   it can't happen) */
                || (!ok && (errsaved == ERROR_PIPE_BUSY))) {
              towrite = towrite >> 1;
              if (!towrite) {
		/* leave winwrote as 0 */
                ok = 1;
		break;
              }
            } else
              break;
          }
        } else {
          /* Don't try to write while flushing. */
          ok = 1;
          winwrote = 0;
        }

	if (ok && winwrote && rfd->oth)
	  rfd->oth->needflush = 1;

        if (ok)
          out_len = winwrote;
      } else {
	/* Claim success for now to make `rfd->oth` get created,
	   and continuae non-blocking handling after that. */
	ok = 1;
	out_len = 1;
      }
      /* and create the writer thread... */

      if (ok && (out_len > 0) && !rfd->oth) {
        /* We create a thread even for pipes that can be put in
           non-blocking mode, because that seems to be the only
           way to get evt behavior of knowing that a write will
	   succeed because the previous content is flushed. */
        Win_FD_Output_Thread *oth;
        HANDLE h;
        DWORD id;
        unsigned char *bfr;
        HANDLE sm;

        oth = calloc(1, sizeof(Win_FD_Output_Thread));
        rfd->oth = oth;

        oth->nonblocking = nonblocking;

        if (!nonblocking) {
	  /* Create the buffer to communicate with the writing thread. */
          bfr = malloc(RKTIO_FD_BUFFSIZE);
          oth->buffer = bfr;
          oth->flushed = 0;
          oth->needflush = 0;
        } else {
	  /* No buffer needed */
          oth->buffer = NULL;
	  /* Some data was written, so it's not yet flushed,
	     and we need a flush. */
          oth->flushed = 0;
          oth->needflush = 1;
        }

        oth->buflen = 0;

        oth->fd = (HANDLE)rfd->fd;
        oth->err_no = 0;
        oth->done = 0;
        sm = CreateSemaphore(NULL, 1, 1, NULL);
        oth->lock_sema = sm;
        sm = CreateSemaphore(NULL, 0, 1, NULL);
        oth->work_sema = sm;
        sm = CreateSemaphore(NULL, 1, 1, NULL);
        oth->ready_sema = sm;

        h = CreateThread(NULL, 4096, (LPTHREAD_START_ROUTINE)WindowsFDWriter, oth, 0, &id);

        oth->thread = h;
      }
    }

    /* We have a thread, if only to watch when the flush is
       done... */
    
    if (rfd->oth && !rfd->oth->nonblocking) {
      /* This case is for Win 95/98/Me anonymous pipes and
         character devices.  We haven't written anything yet! We
         write to a buffer read by the other thread, and return --
         the other thread takes care of writing. Thus, as long as
         there's room in the buffer, we don't block, and we can
         tell whether there's room. Technical problem: if multiple
         ports are attched to the same underlying pipe (different
         handle, same "device"), the port writes can get out of
         order. We try to avoid the problem by sleeping. */

      /* At this point, `out_len` is set ot 1, and we
	 need to set it to a  value. */

      Win_FD_Output_Thread *oth = rfd->oth;

      WaitForSingleObject(oth->lock_sema, INFINITE);
      if (oth->err_no) {
        errsaved = oth->err_no;
        ok = 0;
      } else if (oth->buflen == RKTIO_FD_BUFFSIZE) {
	/* buffer is full, so can't write */
	out_len = 0;
        ok = 1;
      } else {
        winwrote = RKTIO_FD_BUFFSIZE - oth->buflen;
        if ((intptr_t)winwrote > len)
          winwrote = len;

        memcpy(oth->buffer + oth->buflen, buffer, winwrote);
        oth->buflen += winwrote;
        out_len = winwrote;

        /* Let the other thread know that it should start trying
           to write, if it isn't already: */
        ReleaseSemaphore(oth->work_sema, 1, NULL);
        Sleep(0); /* to decrease the chance of re-ordering flushes */

        ok = 1;
      }
      ReleaseSemaphore(oth->lock_sema, 1, NULL);
    }

    if (ok)
      return out_len;

    rktio_set_windows_error(err, errsaved);
    return RKTIO_WRITE_ERROR;
  }
#endif
}

intptr_t rktio_write(rktio_t *rktio, rktio_fd_t *rfd, const char *buffer, intptr_t len)
{
  return do_write(rktio, rfd, buffer, len, &rktio->err);
}

intptr_t rktio_write_in(rktio_t *rktio, rktio_fd_t *rfd, const char *buffer, intptr_t start, intptr_t end)
{
  return rktio_write(rktio, rfd, buffer+start, end-start);
}

rktio_result_t *rktio_write_in_r(rktio_t *rktio, rktio_fd_t *rfd, const char *buffer, intptr_t start, intptr_t end)
{
  return maybe_success(do_write(rktio, rfd, buffer+start, end-start, &rfd->res.err), &rfd->res, RKTIO_WRITE_ERROR);
}

void rktio_std_write_in_best_effort(rktio_t *rktio, int which, char *buffer, intptr_t start, intptr_t end) {
#ifdef RKTIO_SYSTEM_UNIX
  intptr_t amt, len;

  while (start < end) {
    do {
      amt = end - start;
      amt = LIMIT_REQUEST_SIZE(amt);

      do {
        len = write(which, buffer + start, amt);
      } while ((len == -1) && (errno == EINTR));
    
      amt = amt >> 1;
    } while ((len == -1) && (errno == EAGAIN) && (amt > 0));

    if (len == -1)
      return;

    start += len;
  }
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
  HANDLE h;
  int ok, to_console, can_leftover = 0, keep_leftover = 0;
  intptr_t towrite = end - start, amt;
  wchar_t *w_buffer;
  DWORD winwrote;
  DWORD mode;
  int err;

  switch (which) {
  case RKTIO_STDOUT:
    which = STD_OUTPUT_HANDLE;
    break;
  case RKTIO_STDERR:
    which = STD_ERROR_HANDLE;
    break;
  }

  h = get_std_handle(which);  
  if ((h == INVALID_HANDLE_VALUE) || (h == NULL)) {
    rktio_create_console();
    h = get_std_handle(which);
    if ((h == INVALID_HANDLE_VALUE) || (h == NULL))
      return;
  }

  if ((GetFileType(h) == FILE_TYPE_CHAR) && GetConsoleMode(h, &mode)) {
    /* Decode UTF-8 */
    w_buffer = convert_output_wtext(buffer + start, &towrite,
                                    &can_leftover, &keep_leftover,
                                    0, NULL,
                                    0);
    
    start = 0;
    amt = towrite;
    while (towrite > 0) {
      ok = WriteConsoleW(h, w_buffer + start, amt, &winwrote, NULL);
      
      if (!ok) {
        err = GetLastError();

        if (err == ERROR_NOT_ENOUGH_MEMORY) {
          amt = amt >> 1;
          if (!amt) {
            towrite = 0;
            can_leftover = 0;
          }
        } else {
          towrite = 0;
          can_leftover = 0;
        }
      } else {
        start += winwrote;
        towrite -= winwrote;
        amt = towrite;
      }
    }

    free(w_buffer);
  } else {
    can_leftover = towrite;
  }

  amt = can_leftover;
  while (can_leftover > 0) {
    ok = WriteFile(h, buffer + end - can_leftover, amt, &winwrote, NULL);
    
    if (!ok) {
      err = GetLastError();

      if ((err == ERROR_NOT_ENOUGH_MEMORY)
          || (err == ERROR_PIPE_BUSY)) {
        amt = amt >> 1;
        if (!amt) {
          can_leftover = 0;
        }
      } else {
        can_leftover = 0;
      }
    } if (!winwrote) {
      amt = amt >> 1;
      if (!amt) {
        can_leftover = 0;
      }
    } else {
      can_leftover -= winwrote;
      amt = can_leftover;
    }
  }
#endif
}

#ifdef RKTIO_SYSTEM_WINDOWS

static const char *adjust_output_text(const char *buffer, intptr_t *towrite)
{
  intptr_t len = *towrite, i, j, newlines = 0;
  char *new_buffer;

  /* Limit work done here to avoid O(N^2) work if a client tried
     repeatedly to write a O(N)-sized buffer and only part goes out
     each time. */
  if (len > 4096)
    len = 4096;

  for (i = 0; i < len; i++) {
    if (buffer[i] == '\n')
      newlines++;
  }

  if (!newlines)
    return buffer;

  new_buffer = malloc(len + newlines);
  *towrite = len + newlines;

  for (i = 0, j = 0; i < len; i++) {
    if (buffer[i] == '\n') {
      new_buffer[j++] = '\r';
      new_buffer[j++] = '\n';
    } else
      new_buffer[j++] = buffer[i];
  }

  return new_buffer;
}

static intptr_t recount_output_text(const char *orig_buffer, const char *buffer, intptr_t wrote)
{
  intptr_t i = 0, j = 0;

  while (j < wrote) {
    if (orig_buffer[i] == '\n')
      j += 2;
    else
      j++;
    i++;
  }

  return i;
}

static wchar_t *convert_output_wtext(const char *buffer, intptr_t *_towrite,
				     int *_can_leftover, int *_keep_leftover,
				     int leftover_len, char *leftover,
				     int max_convert)
{
  /* Figure out how many bytes we can convert to complete wide
     characters. To avoid quadratic behavior overall, we'll limit the
     number of bytes.

     The given `leftover_len` and `leftover` is a prefix on `buffer`.
     If the tail (after writing all other bytes) is an incomplete
     UTF-8 prefix, report the prefix length in `_can_leftover`. */
  intptr_t i, count, len = *_towrite;
  char *src_buffer;
  wchar_t *dest_buffer;
  int want, span = 0;

  if (leftover_len) {
    /* Assume that leftover is a valid prefix: */
    int v = ((unsigned char *)leftover)[0];
    if ((v & 0xF8) == 0xF0)
      span = 4;
    else if ((v & 0xF0) == 0xE0)
      span = 3;
    else
      span = 2;
  } else
    span = 0;
  want = span - leftover_len;

  for (i = 0, count = 0; (i < len) && (!max_convert || (count < max_convert)); i++) {
    int v = ((unsigned char *)buffer)[i];
    if (want) {
      if ((v & 0xC0) == 0x80) {
	/* valid continuation byte */
	want--;
	if (!want) {
	  count++;
	  if (span == 4)
	    count++; /* surrogate pair */
	}
      } else {
	/* not a valid continuation byte */
	count++;
	want = 0;
	--i; /* retry byte */
      }
    } else if (!(v & 0x80)) {
      count++;
    } else if ((v & 0xF8) == 0xF0) {
      span = 4;
      want = 3;
    } else if ((v & 0xF0) == 0xE0) {
      span = 3;
      want = 2;
    } else if ((v & 0xE0) == 0xC0) {
      span = 2;
      want = 1;
    } else {
      /* bad continuation byte */
      count++;
    }
  }

  if ((i == len) && (want > 0)) {
    /* consuming all input, so set leftover */
    int keep = span - want;
    if (i >= keep) {
      *_can_leftover = keep;
      i -= keep;
      *_keep_leftover = 0;
    } else {
      *_can_leftover = (keep - leftover_len);
      *_keep_leftover = leftover_len;
      i = 0;
    }
  } else {
    *_can_leftover = 0;
    *_keep_leftover = 0;
  }

  if (leftover_len) {
    src_buffer = malloc(i + leftover_len);
    memcpy(src_buffer, leftover, leftover_len);
    memcpy(src_buffer + leftover_len, buffer, i);
    i += leftover_len;
  } else
    src_buffer = (char *)buffer;

  dest_buffer = (wchar_t *)malloc(sizeof(wchar_t) * count);

  if (count > 0) {
    count = MultiByteToWideChar(CP_UTF8, 0, src_buffer, i, dest_buffer, count);
    if (!count) {
      /* force progress */
      ((wchar_t *)src_buffer)[0] = 0xFFFD;
      count = 1;
    }
  }
  *_towrite = count;

  if (leftover_len)
    free(src_buffer);

  return dest_buffer;
}

static intptr_t recount_output_wtext(wchar_t *w_buffer, intptr_t winwrote)
{
  return WideCharToMultiByte(CP_UTF8, 0, w_buffer, winwrote, NULL, 0, NULL, 0);
}

static void deinit_write_fd(rktio_fd_t *rfd, int full_close)
{
  if (rfd->oth) {
    CSI_proc csi;

    WaitForSingleObject(rfd->oth->lock_sema, INFINITE);

    csi = get_csi();

    if (csi) {
      /* See also call above */
      csi(rfd->oth->thread);
    }
    CloseHandle(rfd->oth->thread);
    rfd->oth->done = 1;
    ReleaseSemaphore(rfd->oth->work_sema, 1, NULL);

    /* Try to leave clean-up to the other thread: */
    if (rfd->oth->you_clean_up) {
      /* Other thread is already done, so we're stuck with clean-up: */
      WindowsFDOCleanup(rfd->oth, (full_close ? 1 : -1));
    } else {
      /* otherwise, thread is responsible for clean-up */
      rfd->oth->you_clean_up = (full_close ? 1 : -1);
      ReleaseSemaphore(rfd->oth->lock_sema, 1, NULL);
    }
  }
}

static long WINAPI WindowsFDWriter(Win_FD_Output_Thread *oth)
{
  DWORD towrite, wrote;
  int ok, more_work = 0, err_no;

  if (oth->nonblocking) {
    /* Non-blocking mode (Win NT pipes). Just handle flush requests. */
    WaitForSingleObject(oth->lock_sema, INFINITE);
    while (!oth->done) {
      ReleaseSemaphore(oth->lock_sema, 1, NULL);
      WaitForSingleObject(oth->work_sema, INFINITE);

      FlushFileBuffers(oth->fd);
      
      WaitForSingleObject(oth->lock_sema, INFINITE);
      oth->flushed = 1;
      ReleaseSemaphore(oth->ready_sema, 1, NULL);
    }
    /* lock held on loop exit */
  } else {
    /* Blocking mode. We do the writing work.  This case is for
       Win 95/98/Me anonymous pipes and character devices (such 
       as LPT1). */
    while (!oth->err_no) {
      if (!more_work)
	WaitForSingleObject(oth->work_sema, INFINITE);

      WaitForSingleObject(oth->lock_sema, INFINITE);

      if (oth->done)
	break;

      towrite = oth->buflen;
      
      ReleaseSemaphore(oth->lock_sema, 1, NULL);

      ok = WriteFile(oth->fd, oth->buffer, towrite, &wrote, NULL);
      if (!ok)
	err_no = GetLastError();
      else
	err_no = 0;

      WaitForSingleObject(oth->lock_sema, INFINITE);
      if (!ok) {
	oth->err_no = err_no;
      } else {
	oth->buflen -= wrote;
	if (oth->buflen)
	  memmove(oth->buffer, oth->buffer + wrote, oth->buflen);
	more_work = oth->buflen > 0;
      }
      if ((oth->buflen < RKTIO_FD_BUFFSIZE) || oth->err_no)
	ReleaseSemaphore(oth->ready_sema, 1, NULL);
      ReleaseSemaphore(oth->lock_sema, 1, NULL);
    }
    /* lock is still held on `break` out of loop */
  }
  
  if (oth->you_clean_up) {
    WindowsFDOCleanup(oth, oth->you_clean_up);
  } else {
    /* otherwise, main thread is responsible for clean-up */
    oth->you_clean_up = 1;
    ReleaseSemaphore(oth->lock_sema, 1, NULL);
  }

  return 0;
}

static void WindowsFDOCleanup(Win_FD_Output_Thread *oth, int close_mode)
{
  CloseHandle(oth->lock_sema);
  CloseHandle(oth->work_sema);

  if (close_mode != -1)
    CloseHandle(oth->fd);

  if (oth->buffer)
    free(oth->buffer);
  free(oth);
}

#endif

/*========================================================================*/
/* console                                                                */
/*========================================================================*/

#ifdef RKTIO_SYSTEM_WINDOWS

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
# define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x4
#endif

static HANDLE get_std_handle(int which) {
  HANDLE h;

  h = GetStdHandle(which);

  if ((h == INVALID_HANDLE_VALUE) || (h == NULL))
    return h;

  if ((which == STD_OUTPUT_HANDLE) || (which == STD_ERROR_HANDLE)) {
    if (GetFileType(h) == FILE_TYPE_CHAR) {
      /* Try to enable ANSI escape codes, which should work for a recent
         enough version of Windows */
      DWORD mode = 0;
      GetConsoleMode(h, &mode);
      SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }

  return h;
}

static void force_console(rktio_fd_t *rfd) {
  /* DELAYED_CONSOLE_HANDLE is used to indicate that a console should be created on demand */
  if (rfd->fd == DELAYED_CONSOLE_HANDLE) {
    HANDLE h;
    int which;

    rktio_create_console();

    if (rfd->modes & RKTIO_OPEN_READ)
      which = STD_INPUT_HANDLE;
    else
      which = STD_OUTPUT_HANDLE;
    h = get_std_handle(which);

    rfd->fd = h;
  }
}

#endif
