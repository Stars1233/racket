
(define-primitive-table unsafe-table
  [chaperone-struct-unsafe-undefined (known-procedure 2)]
  [check-not-unsafe-undefined (known-procedure 4)]
  [check-not-unsafe-undefined/assign (known-procedure 4)]
  [prop:chaperone-unsafe-undefined (known-constant)]
  [unsafe-abort-current-continuation/no-wind (known-procedure 4)]
  [unsafe-add-global-finalizer (known-procedure 4)]
  [unsafe-add-post-custodian-shutdown (known-procedure 2)]
  [unsafe-add-collect-callbacks (known-procedure 4)]
  [unsafe-assert-unreachable (known-procedure/no-return 1)]
  [unsafe-box*-cas! (known-procedure/succeeds 8)]
  [unsafe-bytes-length (known-procedure/then-pure 2)]
  [unsafe-bytes-ref (known-procedure/succeeds 4)]
  [unsafe-bytes-set! (known-procedure/succeeds 8)]
  [unsafe-bytes-copy! (known-procedure/succeeds 56)]
  [unsafe-bytes->immutable-bytes! (known-procedure/succeeds 2)]
  [unsafe-call-in-os-thread (known-procedure 2)]
  [unsafe-call-with-composable-continuation/no-wind (known-procedure 4)]
  [unsafe-car (known-procedure/then-pure 2)]
  [unsafe-cdr (known-procedure/then-pure 2)]
  [unsafe-chaperone-procedure (known-procedure -4)]
  [unsafe-chaperone-vector (known-procedure -4)]
  [unsafe-char<? (known-procedure/then-pure/folding-unsafe -2 'char<?)]
  [unsafe-char<=? (known-procedure/then-pure/folding-unsafe -2 'char<=?)]
  [unsafe-char=? (known-procedure/then-pure/folding-unsafe -2 'char=?)]
  [unsafe-char>? (known-procedure/then-pure/folding-unsafe -2 'char>?)]
  [unsafe-char>=? (known-procedure/then-pure/folding-unsafe -2 'char>=?)]
  [unsafe-char->integer (known-procedure/then-pure/folding-unsafe 2 'char->integer)]
  [unsafe-cons-list (known-procedure/then-pure 4)]
  [unsafe-custodian-register (known-procedure 32)]
  [unsafe-custodian-unregister (known-procedure 4)]
  [unsafe-end-atomic (known-procedure 1)]
  [unsafe-end-breakable-atomic (known-procedure 1)]
  [unsafe-end-uninterruptible (known-procedure 1)]
  [unsafe-ephemeron-hash-iterate-first (known-procedure 2)]
  [unsafe-ephemeron-hash-iterate-key (known-procedure 12)]
  [unsafe-ephemeron-hash-iterate-key+value (known-procedure 12)]
  [unsafe-ephemeron-hash-iterate-next (known-procedure 4)]
  [unsafe-ephemeron-hash-iterate-pair (known-procedure 12)]
  [unsafe-ephemeron-hash-iterate-value (known-procedure 12)]
  [unsafe-extfl* (known-procedure/then-pure 4)]
  [unsafe-extfl+ (known-procedure/then-pure 4)]
  [unsafe-extfl- (known-procedure/then-pure 4)]
  [unsafe-extfl->fx (known-procedure/then-pure 2)]
  [unsafe-extfl/ (known-procedure/then-pure 4)]
  [unsafe-extfl< (known-procedure/then-pure 4)]
  [unsafe-extfl<= (known-procedure/then-pure 4)]
  [unsafe-extfl= (known-procedure/then-pure 4)]
  [unsafe-extfl> (known-procedure/then-pure 4)]
  [unsafe-extfl>= (known-procedure/then-pure 4)]
  [unsafe-extflabs (known-procedure/then-pure 2)]
  [unsafe-extflmax (known-procedure/then-pure 4)]
  [unsafe-extflmin (known-procedure/then-pure 4)]
  [unsafe-extflsqrt (known-procedure/then-pure 2)]
  [unsafe-extflvector-length (known-procedure/then-pure 2)]
  [unsafe-extflvector-ref (known-procedure/succeeds 4)]
  [unsafe-extflvector-set! (known-procedure/succeeds 8)]
  [unsafe-f64vector-ref (known-procedure/succeeds 4)]
  [unsafe-f64vector-set! (known-procedure/succeeds 8)]
  [unsafe-f80vector-ref (known-procedure/succeeds 4)]
  [unsafe-f80vector-set! (known-procedure/succeeds 8)]
  [unsafe-file-descriptor->port (known-procedure 8)]
  [unsafe-file-descriptor->semaphore (known-procedure 4)]
  [unsafe-fl* (known-procedure/then-pure/folding-unsafe -1 'fl*)]
  [unsafe-fl+ (known-procedure/then-pure/folding-unsafe -1 'fl+)]
  [unsafe-fl- (known-procedure/then-pure/folding-unsafe -2 'fl-)]
  [unsafe-fl->fx (known-procedure/then-pure/folding-unsafe 2 'fl->fx)]
  [unsafe-fl/ (known-procedure/then-pure/folding-unsafe -2 'fl/)]
  [unsafe-fl< (known-procedure/then-pure/folding-unsafe -2 'fl<)]
  [unsafe-fl<= (known-procedure/then-pure/folding-unsafe -2 'fl<=)]
  [unsafe-fl= (known-procedure/then-pure/folding-unsafe -2 'fl=)]
  [unsafe-fl> (known-procedure/then-pure/folding-unsafe -2 'fl>)]
  [unsafe-fl>= (known-procedure/then-pure/folding-unsafe -2 'fl>=)]
  [unsafe-flabs (known-procedure/then-pure/folding-unsafe 2 'flabs)]
  [unsafe-flbit-field (known-procedure/then-pure/folding-unsafe 8 'flbit-field)]
  [unsafe-flimag-part (known-procedure/then-pure/folding-unsafe 2 'flimag-part)]
  [unsafe-flmax (known-procedure/then-pure/folding-unsafe -2 'flmax)]
  [unsafe-flmin (known-procedure/then-pure/folding-unsafe -2 'flmin)]
  [unsafe-flrandom (known-procedure/then-pure/folding-unsafe 2 'flrandom)]
  [unsafe-flreal-part (known-procedure/then-pure/folding-unsafe 2 'flreal-part)]
  [unsafe-flsingle (known-procedure/then-pure/folding-unsafe 2 'flsingle)]
  [unsafe-flsqrt (known-procedure/then-pure/folding-unsafe 2 'flsqrt)]
  [unsafe-flvector-length (known-procedure/then-pure/folding-unsafe 2 'flvector-length)]
  [unsafe-flvector-ref (known-procedure/succeeds 4)]
  [unsafe-flvector-set! (known-procedure/succeeds 8)]
  [unsafe-fx* (known-procedure/then-pure/folding-unsafe -1 'fx*)]
  [unsafe-fx*/wraparound (known-procedure/then-pure/folding-unsafe 4 'fx*/wraparound)]
  [unsafe-fx+ (known-procedure/then-pure/folding-unsafe -1 'fx+)]
  [unsafe-fx+/wraparound (known-procedure/then-pure/folding-unsafe 4 'fx+/wraparound)]
  [unsafe-fx- (known-procedure/then-pure/folding-unsafe -2 'fx-)]
  [unsafe-fx-/wraparound (known-procedure/then-pure/folding-unsafe 6 'fx-/wraparound)]
  [unsafe-fx->extfl (known-procedure/then-pure/folding-unsafe 2 'fx->extfl)]
  [unsafe-fx->fl (known-procedure/then-pure/folding-unsafe 2 'fx->fl)]
  [unsafe-fx< (known-procedure/then-pure/folding-unsafe -2 'fx<)]
  [unsafe-fx<= (known-procedure/then-pure/folding-unsafe -2 'fx<=)]
  [unsafe-fx= (known-procedure/then-pure/folding-unsafe -2 'fx=)]
  [unsafe-fx> (known-procedure/then-pure/folding-unsafe -2 'fx>)]
  [unsafe-fx>= (known-procedure/then-pure/folding-unsafe -2 'fx>=)]
  [unsafe-fxabs (known-procedure/then-pure/folding-unsafe 2 'fxabs)]
  [unsafe-fxand (known-procedure/then-pure/folding-unsafe -1 'fxand)]
  [unsafe-fxior (known-procedure/then-pure/folding-unsafe -1 'fxior)]
  [unsafe-fxlshift (known-procedure/then-pure/folding-unsafe 4 'fxlshift)]
  [unsafe-fxlshift/wraparound (known-procedure/folding/limited 4 'fixnum)]
  [unsafe-fxmax (known-procedure/then-pure/folding-unsafe -2 'fxmax)]
  [unsafe-fxmin (known-procedure/then-pure/folding-unsafe -2 'fxmin)]
  [unsafe-fxmodulo (known-procedure/then-pure/folding-unsafe 4 'fxmodulo)]
  [unsafe-fxnot (known-procedure/then-pure/folding-unsafe 2 'fxnot)]
  [unsafe-fxpopcount (known-procedure/then-pure/folding-unsafe 2 'fxpopcount)]
  [unsafe-fxpopcount32 (known-procedure/then-pure/folding-unsafe 2 'fxpopcount32)]
  [unsafe-fxpopcount16 (known-procedure/then-pure/folding-unsafe 2 'fxpopcount16)]
  [unsafe-fxquotient (known-procedure/then-pure/folding-unsafe 4 'fxquotient)]
  [unsafe-fxremainder (known-procedure/then-pure/folding-unsafe 4 'fxremainder)]
  [unsafe-fxrshift (known-procedure/then-pure/folding-unsafe 4 'fxrshift)]
  [unsafe-fxrshift/logical (known-procedure/then-pure/folding-unsafe 4 'fxrshift/logical)]
  [unsafe-fxvector-length (known-procedure/then-pure/folding-unsafe 2 'fxvector-length)]
  [unsafe-fxvector-ref (known-procedure 4)]
  [unsafe-fxvector-set! (known-procedure 8)]
  [unsafe-fxxor (known-procedure/then-pure/folding-unsafe -1 'fxxor)]
  [unsafe-get-place-table (known-procedure 1)]
  [unsafe-immutable-hash-iterate-first (known-procedure/then-pure 2)]
  [unsafe-immutable-hash-iterate-key (known-procedure/then-pure 12)]
  [unsafe-immutable-hash-iterate-key+value (known-procedure 12)]
  [unsafe-immutable-hash-iterate-next (known-procedure/then-pure 4)]
  [unsafe-immutable-hash-iterate-pair (known-procedure/then-pure 12)]
  [unsafe-immutable-hash-iterate-value (known-procedure/then-pure 12)]
  [unsafe-impersonate-procedure (known-procedure -4)]
  [unsafe-impersonate-vector (known-procedure -4)]
  [unsafe-impersonate-hash (known-procedure/single-valued -64)]
  [unsafe-in-atomic? (known-procedure 1)]
  [unsafe-list-ref (known-procedure/then-pure 4)]
  [unsafe-list-tail (known-procedure/then-pure 4)]
  [unsafe-make-custodian-at-root (known-procedure 1)]
  [unsafe-make-flrectangular (known-procedure/then-pure/folding-unsafe 4 'make-flrectangular)]
  [unsafe-make-place-local (known-procedure/then-pure 2)]
  [unsafe-make-os-semaphore (known-procedure 1)]
  [unsafe-make-security-guard-at-root (known-procedure 15)]
  [unsafe-make-signal-received (known-procedure/succeeds 1)]
  [unsafe-make-srcloc (known-procedure/then-pure 32)]
  [unsafe-make-uninterruptible-lock (known-procedure 1)]
  [unsafe-mcar (known-procedure/succeeds 2)]
  [unsafe-mcdr (known-procedure/succeeds 2)]
  [unsafe-mutable-hash-iterate-first (known-procedure 2)]
  [unsafe-mutable-hash-iterate-key (known-procedure 12)]
  [unsafe-mutable-hash-iterate-key+value (known-procedure 12)]
  [unsafe-mutable-hash-iterate-next (known-procedure 4)]
  [unsafe-mutable-hash-iterate-pair (known-procedure 12)]
  [unsafe-mutable-hash-iterate-value (known-procedure 12)]
  [unsafe-os-semaphore-post (known-procedure 2)]
  [unsafe-os-semaphore-wait (known-procedure 2)]
  [unsafe-os-thread-enabled? (known-procedure 1)]
  [unsafe-place-local-ref (known-procedure/then-pure 2)]
  [unsafe-place-local-set! (known-procedure/then-pure 4)]
  [unsafe-poll-ctx-eventmask-wakeup (known-procedure 4)]
  [unsafe-poll-ctx-fd-wakeup (known-procedure 8)]
  [unsafe-poll-ctx-milliseconds-wakeup (known-procedure 4)]
  [unsafe-poll-fd (known-procedure 12)]
  [unsafe-poller (known-constant)]
  [unsafe-port->file-descriptor (known-procedure 2)]
  [unsafe-port->socket (known-procedure 2)]
  [unsafe-register-process-global (known-procedure 4)]
  [unsafe-remove-collect-callbacks (known-procedure 2)]
  [unsafe-root-continuation-prompt-tag (known-procedure/pure 1)]
  [unsafe-s16vector-ref (known-procedure/succeeds 4)]
  [unsafe-s16vector-set! (known-procedure/succeeds 8)]
  [unsafe-set-box! (known-procedure 4)]
  [unsafe-set-box*! (known-procedure/succeeds 4)]
  [unsafe-set-immutable-car! (known-procedure/succeeds 4)]
  [unsafe-set-immutable-cdr! (known-procedure/succeeds 4)]
  [unsafe-set-mcar! (known-procedure/succeeds 4)]
  [unsafe-set-mcdr! (known-procedure/succeeds 4)]
  [unsafe-set-on-atomic-timeout! (known-procedure 2)]
  [unsafe-set-sleep-in-thread! (known-procedure 4)]
  [unsafe-signal-received (known-procedure 1)]
  [unsafe-socket->port (known-procedure 8)]
  [unsafe-socket->semaphore (known-procedure 4)]
  [unsafe-start-atomic (known-procedure 1)]
  [unsafe-start-breakable-atomic (known-procedure 1)]
  [unsafe-start-uninterruptible (known-procedure 1)]
  [unsafe-stencil-vector (known-procedure/allocates -2)]
  [unsafe-stencil-vector-length (known-procedure/succeeds 2)]
  [unsafe-stencil-vector-mask (known-procedure/succeeds 2)]
  [unsafe-stencil-vector-ref (known-procedure/succeeds 4)]
  [unsafe-stencil-vector-set! (known-procedure/succeeds 8)]
  [unsafe-stencil-vector-update (known-procedure/succeeds -8)]
  [unsafe-string-length (known-procedure/then-pure 2)]
  [unsafe-string-ref (known-procedure/succeeds 4)]
  [unsafe-string-set! (known-procedure/succeeds 8)]
  [unsafe-string->immutable-string! (known-procedure/succeeds 2)]
  [unsafe-struct*-cas! (known-procedure 16)]
  [unsafe-struct*-ref (known-procedure/succeeds 4)]
  [unsafe-struct*-set! (known-procedure/succeeds 8)]
  [unsafe-struct*-type (known-procedure/succeeds 2)]
  [unsafe-struct-ref (known-procedure/succeeds 4)]
  [unsafe-struct-set! (known-procedure/succeeds 8)]
  [unsafe-thread-at-root (known-procedure 2)]
  [unsafe-u16vector-ref (known-procedure/succeeds 4)]
  [unsafe-u16vector-set! (known-procedure/succeeds 8)]
  [unsafe-unbox (known-procedure 2)]
  [unsafe-unbox* (known-procedure/succeeds 2)]
  [unsafe-undefined (known-constant)]
  [unsafe-uninterruptible-lock-acquire (known-procedure 2)]
  [unsafe-uninterruptible-lock-release (known-procedure 2)]
  [unsafe-vector*-append (known-procedure/succeeds -1)]
  [unsafe-vector*-cas! (known-procedure/succeeds 16)]
  [unsafe-vector*-copy (known-procedure/succeeds 14)]
  [unsafe-vector*-length (known-procedure/then-pure 2)]
  [unsafe-vector*-ref (known-procedure/succeeds 4)]
  [unsafe-vector*-set! (known-procedure/succeeds 8)]
  [unsafe-vector*-set/copy (known-procedure/succeeds 8)]
  [unsafe-vector*->immutable-vector! (known-procedure/succeeds 2)]
  [unsafe-vector-append (known-procedure/succeeds -1)]
  [unsafe-vector-copy (known-procedure/succeeds 14)]
  [unsafe-vector-length (known-procedure/then-pure 2)]
  [unsafe-vector-ref (known-procedure 4)]
  [unsafe-vector-set! (known-procedure 8)]
  [unsafe-vector-set/copy (known-procedure/succeeds 8)]
  [unsafe-weak-hash-iterate-first (known-procedure 2)]
  [unsafe-weak-hash-iterate-key (known-procedure 12)]
  [unsafe-weak-hash-iterate-key+value (known-procedure 12)]
  [unsafe-weak-hash-iterate-next (known-procedure 4)]
  [unsafe-weak-hash-iterate-pair (known-procedure 12)]
  [unsafe-weak-hash-iterate-value (known-procedure 12)])
