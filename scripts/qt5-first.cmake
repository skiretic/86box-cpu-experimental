# Included after project() (CMAKE_PROJECT_INCLUDE) by setup-and-build.sh on arm64.
# Homebrew links Qt 6's headers into /opt/homebrew/include once qt (6) is installed, and
# other dependencies put that directory on the include path ahead of the keg-only qt@5
# frameworks, so <QtCore/QtCore> resolves to Qt 6 and a fresh configure fails to build
# (moc output included). Putting qt@5's include directory first keeps every compile on Qt 5.
if(Qt5_ROOT AND EXISTS "${Qt5_ROOT}/include")
    include_directories(BEFORE "${Qt5_ROOT}/include")
endif()
