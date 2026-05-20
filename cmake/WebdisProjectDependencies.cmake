#
# webdis dependencies
#
#set(CMAKE_FIND_DEBUG_MODE 1)

find_package(Threads REQUIRED)
if (WITH_OPENSSL)
  find_package(OpenSSL REQUIRED)
endif()

# will search packages with pkg-config if no config modules
find_package(PkgConfig QUIET)

find_package(Libevent QUIET) # todo: check this name from conan...
if (NOT TARGET libevent::libevent)
  if (PKG_CONFIG_FOUND)
    if (WITH_OPENSSL)
      pkg_check_modules(libevent REQUIRED libevent libevent_openssl libevent_pthreads)
    else()
      pkg_check_modules(libevent REQUIRED libevent libevent_pthreads)
    endif()
    add_library(libevent::libevent INTERFACE IMPORTED)
    set_target_properties(libevent::libevent PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${libevent_INCLUDE_DIRS}")
    set_target_properties(libevent::libevent PROPERTIES INTERFACE_LINK_LIBRARIES "${libevent_LIBRARIES}")
  else()
    message(FATAL_ERROR "Unable to find libevent")
  endif()
endif()

if (NOT WITH_OWN_HIREDIS)
find_package(Hiredis QUIET)
if (NOT TARGET hiredis::hiredis)
  if (PKG_CONFIG_FOUND)
    if (WITH_OPENSSL)
      pkg_check_modules(hiredis REQUIRED hiredis hiredis_ssl)
    else()
      pkg_check_modules(hiredis REQUIRED hiredis)
    endif()
    add_library(hiredis::hiredis INTERFACE IMPORTED)
    set_target_properties(hiredis::hiredis PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${hiredis_INCLUDE_DIRS}")
    set_target_properties(hiredis::hiredis PROPERTIES INTERFACE_LINK_LIBRARIES "${hiredis_LIBRARIES}")
  else()
    message(STATUS "Unable to find system hiredis... Useing local sources")
    set(WITH_OWN_HIREDIS ON CACHE FORCE)
  endif()
endif()
endif() # not WITH_OWN_HIREDIS

if (WITH_MSGPACK)
  find_package(msgpack QUIET)
endif()