# $Id: stl-ext.m4,v 1.4 2004/01/12 16:39:55 kir Exp $
#
# M4 macros used by autoconf. Checks for some STL extensions.
# Written by Kir Kolyshkin <kir@asplinux.ru>
# Date: 11 Nov 2002

# This macro checks if hash_map header file is in standard location (gcc2)
# or under ext/ subdirectory (gcc3).
# $stl_ext_include_dir holds either '' or 'ext/'.
#
# It is assumed you have called AC_PROG_CXX and AC_PROG_CXXCPP
AC_DEFUN([SW_STL_EXT_INCLUDEDIR], [
 AC_MSG_CHECKING([for STL extensions include directory])
 AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 STL_EXT_DIRS="ext/"
 STL_EXT_INCLUDEDIR="not found"

 for dir in '' $STL_EXT_DIRS; do
   AC_TRY_CPP([#include <${dir}hash_map>],
     [STL_EXT_INCLUDEDIR=$dir; break;])
 done
 AC_LANG_RESTORE
 if test "x$STL_EXT_INCLUDEDIR" = "xnot found"; then
   AC_MSG_ERROR([
** Unable to find hash_map include header in $STL_EXT_DIRS])
 fi
 AC_MSG_RESULT($STL_EXT_INCLUDEDIR)
])

# This macro tries to find out in what namespace some non-standard STL
# classes (like hash_map) are defined. For gcc-3.2, this is '__gxx_ext',
# while earlier versions define that in 'std' namespace.
# Namespace is defined in STL_EXT_NAMESPACE macro.
#
# NOTE: it is assumed you have already called SW_STL_EXT_INCLUDEDIR macro.
AC_DEFUN([SW_STL_EXT_NAMESPACE], [
 AC_MSG_CHECKING([for namespace of non-standard STL classes])
 AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 STL_EXT_NAMESPACE=
 STL_EXT_NSS="std __gnu_cxx";
 for ns in $STL_EXT_NSS; do
   AC_TRY_COMPILE([#include <${STL_EXT_INCLUDEDIR}hash_map>],
     [${ns}::hash_map<int, int> iimap;],
     STL_EXT_NAMESPACE=${ns}
   )
   if test "x$STL_EXT_NAMESPACE" != "x"; then
     break;
   fi
 done
 AC_LANG_RESTORE
 if test "x$STL_EXT_NAMESPACE" = "x"; then
   AC_MSG_ERROR([
** Unable to find hash_map class in namespaces $STL_EXT_NSS])
 fi
 AC_DEFINE_UNQUOTED(STL_EXT_NAMESPACE, $STL_EXT_NAMESPACE,
  [STL namespace in which extensions such as hash_map are defined.])
 AC_MSG_RESULT($STL_EXT_NAMESPACE)
])
