#lang scribble/doc
@(require "mz.rkt" (for-label racket/future))

@(define future-eval (make-base-eval))
@examples[#:hidden #:eval future-eval (require racket/future)]

@(define time-id @racketidfont{time})

@title[#:tag "futures"]{Futures}

@guideintro["effective-futures"]{futures}

@note-lib[racket/future]

@margin-note{
  Support for parallelism via @racket[future] is normally enabled
  for all platforms in the @tech{CS} implementation of Racket.
  In the @tech{BC} implementation,
  support for parallelism is enabled
  by default for Windows, Linux x86/x86_64, and Mac OS x86/x86_64. To
  enable support for other platforms building Racket @tech{BC},
  use @DFlag{enable-futures} with @exec{configure}.}

The @racket[future] and @racket[touch] functions from
@racketmodname[racket/future] provide access to parallelism as supported
by the hardware and operating system.  In contrast to @racket[thread],
which provides concurrency for arbitrary computations without
parallelism, @racket[future] provides parallelism.
A @deftech{future} executes its work in parallel (assuming that
support for parallelism is available) until it detects an attempt to
perform an operation that cannot run safely in
parallel. Similarly, work in a future is suspended if it depends in some
way on the current continuation, such as raising an exception. Operations
that suspend a future are @deftech{blocking} operations. A
suspended computation for a future is resumed when @racket[touch] is
applied to the future.

``Safe'' parallel execution of a future means that all operations
provided by the system must be able to enforce contracts and produce
results as documented. ``Safe'' does not preclude concurrent access to
mutable data that is visible in the program.  For example, a computation
in a future might use @racket[set!] to modify a shared variable, in
which case concurrent assignment to the variable can be visible in other
futures and threads. Furthermore, guarantees about the visibility of
effects and ordering are determined by the operating system and
hardware---which rarely support, for example, the guarantee of
sequential consistency that is provided for @racket[thread]-based
concurrency; see also @secref["memory-order"]. A system operation
that seems obviously safe may have an internal implementation that cannot run in
parallel; see @guidesecref["effective-futures"] in @|Guide| for more
discussion and an introduction to using @racketmodname[future-visualizer #:indirect])
to understand the behavior of system operations.

A future never runs in parallel if all of the @tech{custodians} that
allow its creating thread to run are shut down. Such futures can
execute through a call to @racket[touch], however.

@section{Creating and Touching Futures}

@deftogether[(
  @defproc[(future [thunk (-> any)]) future?]
  @defproc[(touch [f future?]) any]
)]{

  The @racket[future] procedure returns a future value that encapsulates
  @racket[thunk].  The @racket[touch] function forces the evaluation of
  the @racket[thunk] inside the given future, returning the values
  produced by @racket[thunk].  After @racket[touch] forces the evaluation
  of a @racket[thunk], the resulting values are retained by the future
  in place of @racket[thunk], and additional @racket[touch]es of the
  future return those values.

  Between a call to @racket[future] and @racket[touch] for a given
  future, the given @racket[thunk] may run speculatively in parallel to
  other computations, as described above.

  @examples[
    #:eval future-eval
    (let ([f (future (lambda () (+ 1 2)))])
      (list (+ 3 4) (touch f)))
]}

@defproc[(futures-enabled?) boolean?]{
  Returns whether parallel support for futures is enabled 
  in the current Racket configuration.
}

@defproc[(current-future) (or/c #f future?)]{

  Returns the descriptor of the future whose thunk execution is the
  current continuation; that is, if a future descriptor @racket[f] is returned,
  @racket[(touch f)] will produce the result of the current continuation. 
  If a future thunk itself uses @racket[touch],
  future-thunk executions can be nested, in which case the descriptor of
  the most immediately executing future is returned.  If the current
  continuation does not return to the @racket[touch] of any future, the result is
  @racket[#f].
}


@defproc[(future? [v any/c]) boolean?]{

  Returns @racket[#t] if @racket[v] is a future value, @racket[#f]
  otherwise.

}

@defproc[(would-be-future [thunk (-> any)]) future?]{
  Returns a future that never runs in parallel, but that consistently
  logs all potentially ``unsafe'' operations during the execution of
  the future's thunk (i.e., operations that interfere with parallel
  execution).

  With a normal future, certain circumstances might prevent the logging
  of unsafe operations. For example, when executed with debug-level logging,
  
  @racketblock[
    (touch (future (lambda () 
                     (printf "hello1") 
                     (printf "hello2") 
                     (printf "hello3"))))] 

  might log three messages, one for each @racket[printf] 
  invocation.  However, if the @racket[touch] is performed before the future
  has a chance to start running in parallel, the future thunk evaluates
  in the same manner as any ordinary thunk, and no unsafe operations
  are logged.  Replacing @racket[future] with @racket[would-be-future] 
  ensures the logging of all three calls to @racket[printf]. 
}

@defproc[(processor-count) exact-positive-integer?]{

  Returns the number of parallel computation units (e.g., processors or
  cores) that are available on the current machine.

   This is the same binding as available from @racketmodname[racket/place].
}

@deftogether[[
@defform[(for/async (for-clause ...) body-or-break ... body)]
@defform[(for*/async (for-clause ...) body-or-break ... body)]]]{

Like @racket[for] and @racket[for*], but each iteration of the
@racket[body] is executed in a separate @racket[future], and
the futures may be @racket[touch]ed in any order.
}


@; ----------------------------------------

@section{Future Semaphores}

@defproc[(make-fsemaphore [init exact-nonnegative-integer?]) fsemaphore?]{

  Creates and returns a new @deftech{future semaphore} with the
  counter initially set to @racket[init].

  A future semaphore is similar to a plain @tech{semaphore}, but
  future-semaphore operations can be performed safely in parallel (to synchronize
  parallel computations). In contrast, operations on plain @tech{semaphores}
  are not safe to perform in parallel, and they therefore prevent
  a computation from continuing in parallel.

  Beware of trying to use an fsemaphore to implement a lock. A future
  may run concurrently and in parallel to other futures, but a future
  that is not demanded by a Racket thread can be suspended at any
  time---such as just after it takes a lock and before it releases the
  lock. If you must share mutable data among futures, lock-free data
  structures are generally a better fit.

}

@defproc[(fsemaphore? [v any/c]) boolean?]{

  Returns @racket[#t] if @racket[v] is an @tech{future semaphore}
  value, @racket[#f] otherwise.

}

@defproc[(fsemaphore-post [fsema fsemaphore?]) void?]{

  Increments the @tech{future semaphore}'s internal counter and
  returns @|void-const|.

}

@defproc[(fsemaphore-wait [fsema fsemaphore?]) void?]{

  Blocks until the internal counter for @racket[fsema] is non-zero.
  When the counter is non-zero, it is decremented and
  @racket[fsemaphore-wait] returns @|void-const|.

}

@defproc[(fsemaphore-try-wait? [fsema fsemaphore?]) boolean?]{

  Like @racket[fsemaphore-wait], but @racket[fsemaphore-try-wait?]
  never blocks execution.  If @racket[fsema]'s internal counter is zero,
  @racket[fsemaphore-try-wait?] returns @racket[#f] immediately without
  decrementing the counter.  If @racket[fsema]'s counter is positive, it
  is decremented and @racket[#t] is returned.

}

@defproc[(fsemaphore-count [fsema fsemaphore?]) exact-nonnegative-integer?]{

  Returns @racket[fsema]'s current internal counter value.

}

@; ----------------------------------------

@include-section["futures-logging.scrbl"]

@close-eval[future-eval]
