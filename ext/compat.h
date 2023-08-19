/*
 * Licensed under Creative Commons Zero License (CC0; Public Domain)
 */

#ifndef AUX_COMPAT_H

// for older than 2.7.0
#ifndef RUBY_ASSERT_ALWAYS
# define RUBY_ASSERT_ALWAYS(...)
#endif

// for older than 3.0.0
#ifndef TRUE
# define TRUE 1
#endif

// for older than 3.0.0
#ifndef RB_EXT_RACTOR_SAFE
# define RB_EXT_RACTOR_SAFE(FEATURE)
#endif

// for older than 3.2.0
#ifndef RB_UNDEF_P
# define RB_UNDEF_P(OBJ) ((OBJ) == Qundef)
#endif

// for older than 3.2.0
#ifndef RB_NIL_OR_UNDEF_P
# define RB_NIL_OR_UNDEF_P(OBJ) (RB_NIL_P(OBJ) || RB_UNDEF_P(OBJ))
#endif

#endif // AUX_COMPAT_H
