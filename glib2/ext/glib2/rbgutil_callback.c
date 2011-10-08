/* -*- c-file-style: "ruby"; indent-tabs-mode: nil -*- */
/**********************************************************************

  utils_callback.c -

  $Author: sakai $
  $Date: 2007/07/14 07:16:18 $

  Copyright (C) 2007  Ruby-GNOME2 Project

**********************************************************************/

#include "rbgprivate.h"

#ifdef G_OS_WIN32
#  ifdef HAVE_IO_H
#    include <io.h>
#    define pipe(phandles) _pipe(phandles, 128, _O_BINARY)
#  endif
#else
#  ifdef HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#endif
#include <fcntl.h>
#include <errno.h>

#ifndef HAVE_RUBY_NATIVE_THREAD_P
#  define ruby_native_thread_p() is_ruby_native_thread()
#endif

static VALUE rbgutil_eGLibCallbackNotInitializedError;
static ID id_exit_application;

/**********************************************************************/

VALUE
rbgutil_protect(VALUE (*func) (VALUE), VALUE data)
{
    int state = 0;
    VALUE ret = rb_protect(func, data, &state);
    VALUE e = rb_errinfo();
    if (state && !NIL_P(e))
        rb_funcall(mGLib, id_exit_application, 2, e, INT2NUM(EXIT_FAILURE));
    return ret;
}

/**********************************************************************/

#ifdef HAVE_NATIVETHREAD

typedef struct _CallbackRequest {
    VALUE (*function)(VALUE);
    VALUE argument;
    VALUE result;
    GMutex *done_mutex;
    GCond *done_cond;
} CallbackRequest;

static GMutex *callback_dispatch_thread_mutex = NULL;
static GAsyncQueue *callback_request_queue = NULL;
static ID id_callback_dispatch_thread;
static gint callback_pipe_fds[2] = {-1, -1};

#define CALLBACK_PIPE_READY_MESSAGE "R"
#define CALLBACK_PIPE_READY_MESSAGE_SIZE 1

static VALUE
exec_callback(VALUE data)
{
    CallbackRequest *request = (CallbackRequest *)data;
    return request->function(request->argument);
}

static VALUE
process_request(CallbackRequest *request)
{
    g_mutex_lock(request->done_mutex);
    request->result = rbgutil_protect(exec_callback, (VALUE)request);
    g_cond_signal(request->done_cond);
    g_mutex_unlock(request->done_mutex);

    return Qnil;
}

static VALUE
mainloop(void)
{
    for (;;) {
        CallbackRequest *request;
        gchar ready_message_buffer[CALLBACK_PIPE_READY_MESSAGE_SIZE];

        rb_thread_wait_fd(callback_pipe_fds[0]);
        if (read(callback_pipe_fds[0], ready_message_buffer,
                 CALLBACK_PIPE_READY_MESSAGE_SIZE
                ) != CALLBACK_PIPE_READY_MESSAGE_SIZE ||
            strncmp(ready_message_buffer,
                    CALLBACK_PIPE_READY_MESSAGE,
                    CALLBACK_PIPE_READY_MESSAGE_SIZE) != 0) {
            g_error("failed to read valid callback dispatcher message");
            continue;
        }
        request = g_async_queue_pop(callback_request_queue);
        if (!request)
            break;

        rb_thread_create(process_request, request);
    }

    close(callback_pipe_fds[0]);
    callback_pipe_fds[0] = -1;
    close(callback_pipe_fds[1]);
    callback_pipe_fds[1] = -1;

    return Qnil;
}

static void
queue_callback_request(CallbackRequest *request)
{
    ssize_t written;

    g_async_queue_push(callback_request_queue, request);
    written = write(callback_pipe_fds[1],
                    CALLBACK_PIPE_READY_MESSAGE,
                    CALLBACK_PIPE_READY_MESSAGE_SIZE);
    if (written != CALLBACK_PIPE_READY_MESSAGE_SIZE) {
        rb_warn("couldn't write all callback pipe ready message: "
                "message-size: %d, written: %" G_GSSIZE_FORMAT,
                CALLBACK_PIPE_READY_MESSAGE_SIZE,
                written);
    }
}

static VALUE
invoke_callback_in_ruby_thread(VALUE (*func)(VALUE), VALUE arg)
{
    CallbackRequest request;

    g_mutex_lock(callback_dispatch_thread_mutex);
    if (callback_pipe_fds[0] == -1) {
        g_error("Please call rbgutil_start_callback_dispatch_thread() "
                "to dispatch a callback from non-ruby thread before "
                "callbacks are requested from non-ruby thread.");
        g_mutex_unlock(callback_dispatch_thread_mutex);
        return Qnil;
    }

    request.function = func;
    request.argument = arg;
    request.result = Qnil;
    request.done_mutex = g_mutex_new();
    request.done_cond = g_cond_new();

    g_mutex_lock(request.done_mutex);
    queue_callback_request(&request);
    g_mutex_unlock(callback_dispatch_thread_mutex);

    g_cond_wait(request.done_cond, request.done_mutex);
    g_mutex_unlock(request.done_mutex);

    g_cond_free(request.done_cond);
    g_mutex_free(request.done_mutex);


    return request.result;
}

#ifdef HAVE_RUBY_THREAD_HAS_GVL_P
extern int ruby_thread_has_gvl_p(void);
#endif

#ifdef HAVE_RB_THREAD_CALL_WITH_GVL
extern void *rb_thread_call_with_gvl(void *(*func)(void *), void *data1);

static void *
invoke_callback_with_gvl(void *arg)
{
    CallbackRequest *req = (CallbackRequest*)arg;
    return (void *)rbgutil_protect(req->function, req->argument);
}
#endif

#endif

/**********************************************************************/

VALUE
rbgutil_invoke_callback(VALUE (*func)(VALUE), VALUE arg)
{
#ifdef HAVE_NATIVETHREAD
#ifdef HAVE_RUBY_THREAD_HAS_GVL_P
    if (ruby_thread_has_gvl_p())
        return rbgutil_protect(func, arg);
#endif
    if (ruby_native_thread_p()) {
#ifdef HAVE_RB_THREAD_CALL_WITH_GVL
        CallbackRequest req;
        req.function = func;
        req.argument = arg;
        return (VALUE)rb_thread_call_with_gvl(invoke_callback_with_gvl, &req);
#endif
    } else {
        return invoke_callback_in_ruby_thread(func, arg);
    }
#endif
    return rbgutil_protect(func, arg);
}

/**********************************************************************/

void
rbgutil_start_callback_dispatch_thread(void)
{
#ifdef HAVE_NATIVETHREAD
    VALUE callback_dispatch_thread;

    g_mutex_lock(callback_dispatch_thread_mutex);
    callback_dispatch_thread = rb_ivar_get(mGLib, id_callback_dispatch_thread);
    if (NIL_P(callback_dispatch_thread)) {
        if (pipe(callback_pipe_fds) == -1)
            rb_sys_fail("pipe()");

        callback_dispatch_thread = rb_thread_create(mainloop, NULL);
        rb_ivar_set(mGLib, id_callback_dispatch_thread,
                    callback_dispatch_thread);
    }
    g_mutex_unlock(callback_dispatch_thread_mutex);
#endif
}

void
rbgutil_stop_callback_dispatch_thread(void)
{
#ifdef HAVE_NATIVETHREAD
    VALUE callback_dispatch_thread;

    g_mutex_lock(callback_dispatch_thread_mutex);
    callback_dispatch_thread = rb_ivar_get(mGLib, id_callback_dispatch_thread);
    if (!NIL_P(callback_dispatch_thread)) {
        queue_callback_request(NULL);
        rb_ivar_set(mGLib, id_callback_dispatch_thread, Qnil);
    }
    g_mutex_unlock(callback_dispatch_thread_mutex);
#endif
}

void
Init_gutil_callback()
{
    id_exit_application = rb_intern("exit_application");
    rbgutil_eGLibCallbackNotInitializedError =
        rb_define_class_under(mGLib, "CallbackNotInitializedError",
                              rb_eRuntimeError);

#ifdef HAVE_NATIVETHREAD
    if (!g_thread_supported())
        g_thread_init(NULL);

    id_callback_dispatch_thread = rb_intern("callback_dispatch_thread");
    rb_ivar_set(mGLib, id_callback_dispatch_thread, Qnil);

    callback_request_queue = g_async_queue_new();
    callback_dispatch_thread_mutex = g_mutex_new();
#endif
}
