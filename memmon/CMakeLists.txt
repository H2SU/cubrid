set(MEMMON_SOURCES
  ${MEMMON_DIR}/memmon.cpp
  )

add_library(memmon SHARED
${MEMMON_SOURCES}
)

set(MEMMON_HEADERS
  ${MEMMON_DIR}/memmon.hpp
)

install(TARGETS memmon
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Library
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  )

target_include_directories(memmon PRIVATE ${EP_INCLUDES})
target_include_directories(memmon PRIVATE ${CUBRID_INCLUDES})

target_link_libraries (memmon LINK_PRIVATE cubrid)