#include "rktio.h"
#include "rktio_private.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>

typedef struct err_str_t {
  int id;
  char *str;
} err_str_t;

err_str_t err_strs[]  = { 
  { RKTIO_ERROR_UNSUPPORTED, "unsupported" },
  { RKTIO_ERROR_INVALID_PATH, "invalid path encoding" },
  { RKTIO_ERROR_DOES_NOT_EXIST, "no such file or directory" },
  { RKTIO_ERROR_EXISTS, "file or directory already exists" },
  { RKTIO_ERROR_LINK_FAILED, "link creation failed" },
  { RKTIO_ERROR_NOT_A_LINK, "not a link" },
  { RKTIO_ERROR_BAD_PERMISSION, "unsupported permission value" },
  { RKTIO_ERROR_IS_A_DIRECTORY, "path refers to a directory" },
  { RKTIO_ERROR_NOT_A_DIRECTORY, "path does not refer to a directory" },
  { RKTIO_ERROR_UNSUPPORTED_TEXT_MODE, "cannot use text mode on a non-file device" },
  { RKTIO_ERROR_CANNOT_FILE_POSITION, "cannot get/set position/size on device" },
  { RKTIO_ERROR_NO_TILDE, "path does not start with a tilde" },
  { RKTIO_ERROR_ILL_FORMED_USER, "ill-formed username in path" },
  { RKTIO_ERROR_UNKNOWN_USER, "unknown username in path" },
  { RKTIO_ERROR_INIT_FAILED, "initialization failed" },
  { RKTIO_ERROR_LTPS_NOT_FOUND, "handle not found" },
  { RKTIO_ERROR_LTPS_REMOVED, "handles successfully removed" },
  { RKTIO_ERROR_CONNECT_TRYING_NEXT, "connection failed, but can try again" },
  { RKTIO_ERROR_ACCEPT_NOT_READY, "no connection ready to accept" }, 
  { RKTIO_ERROR_HOST_AND_PORT_BOTH_UNSPECIFIED, "neither hostname nor port number specified" },
  { RKTIO_ERROR_INFO_TRY_AGAIN, "spurious empty UDP message; try again" },
  { RKTIO_ERROR_TRY_AGAIN, "no UDP message available" },
  { RKTIO_ERROR_TRY_AGAIN_WITH_IPV4, "listen failed, but try again with just IPv4 addresses" },
  { RKTIO_ERROR_TIME_OUT_OF_RANGE, "time value out-of-range for date conversion" },
  { RKTIO_ERROR_NO_SUCH_ENVVAR, "no value as an environment variable" },
  { RKTIO_ERROR_SHELL_EXECUTE_FAILED, "ShellExecute failed" },
  { RKTIO_ERROR_CONVERT_NOT_ENOUGH_SPACE, "encoding conversion needs more output space" },
  { RKTIO_ERROR_CONVERT_BAD_SEQUENCE, "ill-formed input encountered in encoding conversion" },
  { RKTIO_ERROR_CONVERT_PREMATURE_END, "input encoding ended prematurely" },
  { RKTIO_ERROR_CONVERT_OTHER, "encoding conversion encountered an error" },
  { RKTIO_ERROR_DLL, "error is from dlopen" },
  { 0, NULL }
};

void rktio_get_posix_error(rktio_err_t *err)
{
  err->errid = errno;
  err->errkind = RKTIO_ERROR_KIND_POSIX;
}

void rktio_set_racket_error(rktio_err_t *err, int new_errid)
{
  err->errid = new_errid;
  err->errkind = RKTIO_ERROR_KIND_RACKET;
}

#ifdef RKTIO_SYSTEM_WINDOWS
void rktio_get_windows_error(rktio_err_t *err)
{
  err->errid = GetLastError();
  err->errkind = RKTIO_ERROR_KIND_WINDOWS;
}

void rktio_set_windows_error(rktio_err_t *err, int errid)
{
  err->errid = errid;
  err->errkind = RKTIO_ERROR_KIND_WINDOWS;
}
#endif

int rktio_get_last_error(rktio_t *rktio)
{
  return rktio->err.errid;
}

int rktio_get_error(rktio_result_t *res)
{
  return res->err.errid;
}

int rktio_get_last_error_kind(rktio_t *rktio)
{
  return rktio->err.errkind;
}

int rktio_get_error_kind(rktio_result_t *res)
{
  return res->err.errkind;
}

int rktio_get_last_error_step(rktio_t *rktio)
{
  return rktio->err.errstep;
}

int rktio_get_error_step(rktio_result_t *res)
{
  return res->err.errstep;
}

void rktio_set_last_error(rktio_t *rktio, int kind, int errid)
{
  rktio->err.errkind = kind;
  rktio->err.errid = errid;
}

void rktio_set_error(rktio_err_t *err, int kind, int errid)
{
  err->errkind = kind;
  err->errid = errid;
}

void rktio_set_last_error_step(rktio_t *rktio, int new_errstep)
{
  rktio->err.errstep = new_errstep;
}

static void do_remap_error(rktio_err_t *err)
{
  if (err->errkind == RKTIO_ERROR_KIND_RACKET) {
    switch (err->errid) {
    case RKTIO_ERROR_DOES_NOT_EXIST:
#ifdef RKTIO_SYSTEM_UNIX
      rktio_set_error(err, RKTIO_ERROR_KIND_POSIX, ENOENT);
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
      rktio_set_error(err, RKTIO_ERROR_KIND_WINDOWS, ERROR_FILE_NOT_FOUND);
#endif
      break;
    case RKTIO_ERROR_EXISTS:
#ifdef RKTIO_SYSTEM_UNIX
      rktio_set_error(err, RKTIO_ERROR_KIND_POSIX, EEXIST);
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
      rktio_set_error(err, RKTIO_ERROR_KIND_WINDOWS, RKTIO_ERROR_EXISTS);
#endif
      break;
    case RKTIO_ERROR_ACCESS_DENIED:
#ifdef RKTIO_SYSTEM_UNIX
      rktio_set_error(err, RKTIO_ERROR_KIND_POSIX, EACCES);
#endif
#ifdef RKTIO_SYSTEM_WINDOWS
      rktio_set_error(err, RKTIO_ERROR_KIND_WINDOWS, ERROR_ACCESS_DENIED);
#endif
      break;
    }
  }
}

void rktio_remap_last_error(rktio_t *rktio)
{
  do_remap_error(&rktio->err);
}

void rktio_remap_error(rktio_result_t *res)
{
  do_remap_error(&res->err);
}

const char *rktio_get_error_string(rktio_t *rktio, int kind, int errid)
{
  const char *s = NULL;
  if (kind == RKTIO_ERROR_KIND_RACKET) {
    int i;
    for (i = 0; err_strs[i].str; i++) {
      if (err_strs[i].id == errid)
        return err_strs[i].str;
    }
  } else if (kind == RKTIO_ERROR_KIND_POSIX) {
#ifndef NO_STRERROR_AVAILABLE
    s = strerror(errid);
#endif
  } else if (kind == RKTIO_ERROR_KIND_GAI)
    s = rktio_gai_strerror(rktio, errid); /* may use `last_err_str` */
#ifdef RKTIO_SYSTEM_WINDOWS
  else if (kind == RKTIO_ERROR_KIND_WINDOWS) {
    wchar_t mbuf[256];
    intptr_t len, i;
    if ((len = FormatMessageW((FORMAT_MESSAGE_FROM_SYSTEM
                               | FORMAT_MESSAGE_IGNORE_INSERTS), 
                              NULL,
                              errid, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                              mbuf, 255, NULL))) {
      char *es;
      if (len == 255)
        mbuf[254] = 0;
      else
        mbuf[len] = 0;
      es = NARROW_PATH_copy(mbuf);
      /* Remove newlines: */
      for (i = strlen(es) - 1; i > 0; i--) {
        if (isspace(es[i]))
          es[i] = 0;
        else
          break;
      }
      
      if (rktio->last_err_str)
        free(rktio->last_err_str);
      rktio->last_err_str = es;
      
      return es;
    }
  }
#endif
  if (s) return s;
  return "???";
}

const char *rktio_get_last_error_string(rktio_t *rktio)
{
  return rktio_get_error_string(rktio,
                                rktio_get_last_error_kind(rktio),
                                rktio_get_last_error(rktio));
}

void rktio_error_clean(rktio_t *rktio)
{
#ifdef RKTIO_SYSTEM_WINDOWS
  if (rktio->last_err_str)
    free(rktio->last_err_str);
#endif
}

rktio_result_t *rktio_make_error(rktio_err_t *err)
{
  rktio_result_t *res = (rktio_result_t *)malloc(sizeof(rktio_result_t));
  res->is_success = 0;
  memcpy(&res->err, err, sizeof(rktio_err_t));
  return res;
}

rktio_result_t *rktio_make_success(void)
{
  rktio_result_t *res = (rktio_result_t *)malloc(sizeof(rktio_result_t));
  res->is_success = 1;
  return res;
}

rktio_bool_t rktio_result_is_success(rktio_result_t *res)
{
  return res->is_success;
}

intptr_t rktio_result_integer(rktio_result_t *res)
{
  return res->success.i;
}

char *rktio_result_string(rktio_result_t *res)
{
  return res->success.str;
}

rktio_directory_list_t *rktio_result_directory_list(rktio_result_t *res)
{
  return res->success.dir_list;
}
